// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "i18n/i18n.h"
#include "native/thread/thread.h"
#include "native/thread/threadutil.h"

#include "Common/ChunkFile.h"

#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MemMapHelpers.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Dialog/PSPSaveDialog.h"

const static float FONT_SCALE = 0.55f;

// These are rough, it seems to take at least 100ms or so to init, and shutdown depends on threads.
// Some games seem to required slightly longer delays to work, so we try 200ms as a compromise.
const static int SAVEDATA_INIT_DELAY_US = 200000;
const static int SAVEDATA_SHUTDOWN_DELAY_US = 2000;

// These are the only sizes which are allowed.
// TODO: We should test what the different behavior is for each.
const static int SAVEDATA_DIALOG_SIZE_V1 = 1480;
const static int SAVEDATA_DIALOG_SIZE_V2 = 1500;
const static int SAVEDATA_DIALOG_SIZE_V3 = 1536;


PSPSaveDialog::PSPSaveDialog()
	: PSPDialog()
	, display(DS_NONE)
	, currentSelectedSave(0)
	, ioThread(0)
{
	param.SetPspParam(0);
}

PSPSaveDialog::~PSPSaveDialog() {
}

int PSPSaveDialog::Init(int paramAddr)
{
	// Ignore if already running
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(SCEUTILITY, "A save request is already running, not starting a new one");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	JoinIOThread();
	ioThreadStatus = SAVEIO_NONE;

	requestAddr = paramAddr;
	int size = Memory::Read_U32(requestAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different save request format
	if (size != SAVEDATA_DIALOG_SIZE_V1 && size != SAVEDATA_DIALOG_SIZE_V2 && size != SAVEDATA_DIALOG_SIZE_V3) {
		ERROR_LOG_REPORT(SCEUTILITY, "sceUtilitySavedataInitStart: invalid size %d", size);
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}
	Memory::Memcpy(&request, requestAddr, size);
	Memory::Memcpy(&originalRequest, requestAddr, size);

	int retval = param.SetPspParam(&request);

	const u32 mode = (u32)param.GetPspParam()->mode;
	const char *modeName = mode < ARRAY_SIZE(utilitySavedataTypeNames) ? utilitySavedataTypeNames[mode] : "UNKNOWN";
	INFO_LOG(SCEUTILITY,"sceUtilitySavedataInitStart(%08x) - %s (%d)", paramAddr, modeName, mode);
	INFO_LOG(SCEUTILITY,"sceUtilitySavedataInitStart(%08x) : Game key (hex): %s", paramAddr, param.GetKey(param.GetPspParam()).c_str());

	yesnoChoice = 1;
	switch ((SceUtilitySavedataFocus)(u32)param.GetPspParam()->focus)
	{
	case SCE_UTILITY_SAVEDATA_FOCUS_NAME:
		currentSelectedSave = param.GetSaveNameIndex(param.GetPspParam());
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_FIRSTLIST:
		currentSelectedSave = param.GetFirstListSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_LASTLIST:
		currentSelectedSave = param.GetLastListSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_LATEST:
		currentSelectedSave = param.GetLatestSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_OLDEST:
		currentSelectedSave = param.GetOldestSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_FIRSTDATA:
		currentSelectedSave = param.GetFirstDataSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_LASTDATA:
		currentSelectedSave = param.GetLastDataSave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_FIRSTEMPTY:
		currentSelectedSave = param.GetFirstEmptySave();
		break;
	case SCE_UTILITY_SAVEDATA_FOCUS_LASTEMPTY:
		currentSelectedSave = param.GetLastEmptySave();
		break;
	default:
		WARN_LOG(SCEUTILITY, "Unknown save list focus option: %d", param.GetPspParam()->focus);
		currentSelectedSave = 0;
		break;
	}

	switch ((SceUtilitySavedataType)(u32)param.GetPspParam()->mode)
	{
		case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
			DEBUG_LOG(SCEUTILITY, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0)
				display = DS_LOAD_CONFIRM;
			else
				display = DS_LOAD_NODATA;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
			DEBUG_LOG(SCEUTILITY, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD:
			DEBUG_LOG(SCEUTILITY, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_LOAD_NODATA;
			else
				display = DS_LOAD_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
			DEBUG_LOG(SCEUTILITY, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0)
			{
				yesnoChoice = 0;
				display = DS_SAVE_CONFIRM_OVERWRITE;
			}
			else
				display = DS_SAVE_CONFIRM;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
			DEBUG_LOG(SCEUTILITY, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE:
			DEBUG_LOG(SCEUTILITY, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_SAVE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE:
			DEBUG_LOG(SCEUTILITY, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_DELETE_NODATA;
			else
				display = DS_DELETE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
		case SCE_UTILITY_SAVEDATA_TYPE_LIST:
		case SCE_UTILITY_SAVEDATA_TYPE_FILES:
		case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
		case SCE_UTILITY_SAVEDATA_TYPE_SINGLEDELETE:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
			display = DS_NONE;
			break;

		case SCE_UTILITY_SAVEDATA_TYPE_DELETE: // When run on a PSP, displays a list of all saves on the PSP. Weird. (Not really, it's to let you free up space)
			display = DS_DELETE_LIST_CHOICE;
			break;
		default:
		{
			ERROR_LOG_REPORT(SCEUTILITY, "Load/Save function %d not coded. Title: %s Save: %s File: %s", (SceUtilitySavedataType)(u32)param.GetPspParam()->mode, param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			param.GetPspParam()->common.result = 0;
			ChangeStatusInit(SAVEDATA_INIT_DELAY_US);
			display = DS_NONE;
			return 0; // Return 0 should allow the game to continue, but missing function must be implemented and returning the right value or the game can block.
		}
		break;
	}

	if (retval < 0) {
		ChangeStatusShutdown(SAVEDATA_SHUTDOWN_DELAY_US);
	} else {
		ChangeStatusInit(SAVEDATA_INIT_DELAY_US);
	}

	UpdateButtons();
	StartFade(true);

	/*INFO_LOG(SCEUTILITY,"Dump Param :");
	INFO_LOG(SCEUTILITY,"size : %d",param.GetPspParam()->size);
	INFO_LOG(SCEUTILITY,"language : %d",param.GetPspParam()->language);
	INFO_LOG(SCEUTILITY,"buttonSwap : %d",param.GetPspParam()->buttonSwap);
	INFO_LOG(SCEUTILITY,"result : %d",param.GetPspParam()->common.result);
	INFO_LOG(SCEUTILITY,"mode : %d",param.GetPspParam()->mode);
	INFO_LOG(SCEUTILITY,"bind : %d",param.GetPspParam()->bind);
	INFO_LOG(SCEUTILITY,"overwriteMode : %d",param.GetPspParam()->overwriteMode);
	INFO_LOG(SCEUTILITY,"gameName : %s",param.GetGameName(param.GetPspParam()).c_str());
	INFO_LOG(SCEUTILITY,"saveName : %s",param.GetPspParam()->saveName);
	INFO_LOG(SCEUTILITY,"saveNameList : %08x",*((unsigned int*)&param.GetPspParam()->saveNameList));
	INFO_LOG(SCEUTILITY,"fileName : %s",param.GetPspParam()->fileName);
	INFO_LOG(SCEUTILITY,"dataBuf : %08x",*((unsigned int*)&param.GetPspParam()->dataBuf));
	INFO_LOG(SCEUTILITY,"dataBufSize : %u",param.GetPspParam()->dataBufSize);
	INFO_LOG(SCEUTILITY,"dataSize : %u",param.GetPspParam()->dataSize);

	INFO_LOG(SCEUTILITY,"sfo title : %s",param.GetPspParam()->sfoParam.title);
	INFO_LOG(SCEUTILITY,"sfo savedataTitle : %s",param.GetPspParam()->sfoParam.savedataTitle);
	INFO_LOG(SCEUTILITY,"sfo detail : %s",param.GetPspParam()->sfoParam.detail);

	INFO_LOG(SCEUTILITY,"icon0 data : %08x",*((unsigned int*)&param.GetPspParam()->icon0FileData.buf));
	INFO_LOG(SCEUTILITY,"icon0 size : %u",param.GetPspParam()->icon0FileData.bufSize);

	INFO_LOG(SCEUTILITY,"icon1 data : %08x",*((unsigned int*)&param.GetPspParam()->icon1FileData.buf));
	INFO_LOG(SCEUTILITY,"icon1 size : %u",param.GetPspParam()->icon1FileData.bufSize);

	INFO_LOG(SCEUTILITY,"pic1 data : %08x",*((unsigned int*)&param.GetPspParam()->pic1FileData.buf));
	INFO_LOG(SCEUTILITY,"pic1 size : %u",param.GetPspParam()->pic1FileData.bufSize);

	INFO_LOG(SCEUTILITY,"snd0 data : %08x",*((unsigned int*)&param.GetPspParam()->snd0FileData.buf));
	INFO_LOG(SCEUTILITY,"snd0 size : %u",param.GetPspParam()->snd0FileData.bufSize);*/
	return retval;
}

const std::string PSPSaveDialog::GetSelectedSaveDirName() const
{
	switch ((SceUtilitySavedataType)(u32)param.GetPspParam()->mode)
	{
	case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
		return param.GetSaveDirName(param.GetPspParam());

	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
		return param.GetSaveDirName(param.GetPspParam());

	// TODO: Maybe also SINGLEDELETE/etc?

	// SIZES ignores saveName it seems.

	default:
		return param.GetSaveDirName(param.GetPspParam(), currentSelectedSave);
		break;
	}
}

void PSPSaveDialog::DisplayBanner(int which)
{
	I18NCategory *d = GetI18NCategory("Dialog");
	PPGeDrawRect(0, 0, 480, 23, CalcFadedColor(0x65636358));
	const char *title;
	switch (which)
	{
	case DB_SAVE:
		title = d->T("Save");
		break;
	case DB_LOAD:
		title = d->T("Load");
		break;
	case DB_DELETE:
		title = d->T("Delete");
		break;
	default:
		title = "";
		break;
	}
	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 6, 12.0f, 12.0f, 1, 10, 1, 10, 10, 10, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawText(title, 30, 11, PPGE_ALIGN_VCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
}

void PSPSaveDialog::DisplaySaveList(bool canMove)
{
	lock_guard guard(paramLock);
	static int upFramesHeld = 0;
	static int downFramesHeld = 0;

	for (int displayCount = 0; displayCount < param.GetFilenameCount(); displayCount++)
	{
		int textureColor = 0xFFFFFFFF;
		auto fileInfo = param.GetFileInfo(displayCount);

		if (fileInfo.size == 0 && fileInfo.texture != NULL)
			textureColor = 0xFF777777;

		// Calc save image position on screen
		float w, h , x, b;
		float y = 97;
		if (displayCount != currentSelectedSave) {
			w = 81;
			h = 45;
			x = 58.5f;
		} else {
			w = 144;
			h = 80;
			x = 27;
			b = 1.2;
			PPGeDrawRect(x-b, y-b, x+w+b, y, CalcFadedColor(0xD0FFFFFF)); // top border
			PPGeDrawRect(x-b, y, x, y+h, CalcFadedColor(0xD0FFFFFF)); // left border
			PPGeDrawRect(x-b, y+h, x+w+b, y+h+b, CalcFadedColor(0xD0FFFFFF)); //bottom border
			PPGeDrawRect(x+w, y, x+w+b, y+h, CalcFadedColor(0xD0FFFFFF)); //right border
		}
		if (displayCount < currentSelectedSave)
			y -= 13 + 45 * (currentSelectedSave - displayCount);
		else if (displayCount > currentSelectedSave)
			y += 48 + 45 * (displayCount - currentSelectedSave);

		// Skip if it's well outside the screen.
		if (y > 472.0f || y < -200.0f)
			continue;

		int tw = 256;
		int th = 256;
		if (fileInfo.texture != NULL) {
			fileInfo.texture->SetTexture();
			tw = fileInfo.texture->Width();
			th = fileInfo.texture->Height();
			PPGeDrawImage(x, y, w, h, 0, 0, 1, 1, tw, th, textureColor);
		}
		PPGeSetDefaultTexture();
	}

	if (canMove) {
		if ( (IsButtonPressed(CTRL_UP) || IsButtonHeld(CTRL_UP, upFramesHeld)) && currentSelectedSave > 0)
			currentSelectedSave--;

		else if ( (IsButtonPressed(CTRL_DOWN) || IsButtonHeld(CTRL_DOWN, downFramesHeld)) && currentSelectedSave < (param.GetFilenameCount() - 1))
			currentSelectedSave++;
	}
}

void PSPSaveDialog::DisplaySaveIcon()
{
	lock_guard guard(paramLock);
	int textureColor = CalcFadedColor(0xFFFFFFFF);
	auto curSave = param.GetFileInfo(currentSelectedSave);

	if (curSave.size == 0)
		textureColor = CalcFadedColor(0xFF777777);

	// Calc save image position on screen
	float w = 144;
	float h = 80;
	float x = 27;
	float y = 97;

	int tw = 256;
	int th = 256;
	if (curSave.texture != NULL) {
		curSave.texture->SetTexture();
		tw = curSave.texture->Width();
		th = curSave.texture->Height();
	} else {
		PPGeDisableTexture();
	}
	PPGeDrawImage(x, y, w, h, 0, 0, 1, 1, tw, th, textureColor);
	if (curSave.texture != NULL)
		PPGeSetDefaultTexture();
}

void PSPSaveDialog::DisplaySaveDataInfo1()
{
	lock_guard guard(paramLock);
	if (param.GetFileInfo(currentSelectedSave).size == 0) {
		I18NCategory *d = GetI18NCategory("Dialog");
		PPGeDrawText(d->T("NEW DATA"), 180, 136, PPGE_ALIGN_VCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
	} else {
		char title[512];
		char time[512];
		char saveTitle[512];
		char saveDetail[512];

		char am_pm[] = "AM";
		char hour_time[10] ;
		int hour = param.GetFileInfo(currentSelectedSave).modif_time.tm_hour;
		int min  = param.GetFileInfo(currentSelectedSave).modif_time.tm_min;
		switch (g_Config.iTimeFormat) {
		case 1:
			if (hour > 12) {
				strcpy(am_pm, "PM");
				hour -= 12;
			}
			snprintf(hour_time,10,"%02d:%02d %s", hour, min, am_pm);
			break;
		case 2:
			snprintf(hour_time,10,"%02d:%02d", hour, min); 
			break;
		default:
			if (hour > 12) {
				strcpy(am_pm, "PM");
				hour -= 12;
			}
			snprintf(hour_time,10,"%02d:%02d %s", hour, min, am_pm);
		}

		snprintf(title, 512, "%s", param.GetFileInfo(currentSelectedSave).title);
		int day   = param.GetFileInfo(currentSelectedSave).modif_time.tm_mday;
		int month = param.GetFileInfo(currentSelectedSave).modif_time.tm_mon + 1;
		int year  = param.GetFileInfo(currentSelectedSave).modif_time.tm_year + 1900;
		s64 sizeK = param.GetFileInfo(currentSelectedSave).size / 1024;
		switch (g_Config.iDateFormat) {
		case 1:
			snprintf(time, 512, "%d/%02d/%02d   %s  %lld KB", year, month, day, hour_time, sizeK);
			break;
		case 2:
			snprintf(time, 512, "%02d/%02d/%d   %s  %lld KB", month, day, year, hour_time, sizeK);
			break;
		case 3:
			snprintf(time, 512, "%02d/%02d/%d   %s  %lld KB", day, month, year, hour_time, sizeK);
			break;
		default:
			snprintf(time, 512, "%d/%02d/%02d   %s  %lld KB", year, month, day, hour_time, sizeK);
		}
		snprintf(saveTitle, 512, "%s", param.GetFileInfo(currentSelectedSave).saveTitle);
		snprintf(saveDetail, 512, "%s", param.GetFileInfo(currentSelectedSave).saveDetail);
		
		PPGeDrawRect(180, 136, 480, 137, CalcFadedColor(0xFFFFFFFF));
		std::string titleTxt = title;
		std::string timeTxt = time;
		std::string saveTitleTxt = saveTitle;
		std::string saveDetailTxt = saveDetail;

		PPGeDrawText(titleTxt.c_str(), 181, 138, PPGE_ALIGN_BOTTOM, 0.6f, CalcFadedColor(0x80000000));
		PPGeDrawText(titleTxt.c_str(), 180, 136, PPGE_ALIGN_BOTTOM, 0.6f, CalcFadedColor(0xFFC0C0C0));
		PPGeDrawText(timeTxt.c_str(), 181, 139, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0x80000000));
		PPGeDrawText(timeTxt.c_str(), 180, 137, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(saveTitleTxt.c_str(), 176, 162, PPGE_ALIGN_LEFT, 0.55f, CalcFadedColor(0x80000000));
		PPGeDrawText(saveTitleTxt.c_str(), 175, 159, PPGE_ALIGN_LEFT, 0.55f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(saveDetailTxt.c_str(), 176, 183, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0x80000000));
		PPGeDrawText(saveDetailTxt.c_str(), 175, 181, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
	}
}

void PSPSaveDialog::DisplaySaveDataInfo2()
{
	lock_guard guard(paramLock);
	if (param.GetFileInfo(currentSelectedSave).size == 0) {		
	} else {
		char txt[1024];
		char date[256];
		char am_pm[] = "AM";
		char hour_time[10] ;
		int hour = param.GetFileInfo(currentSelectedSave).modif_time.tm_hour;
		int min  = param.GetFileInfo(currentSelectedSave).modif_time.tm_min;
		switch (g_Config.iTimeFormat) {
		case 1:
			if (hour > 12) {
				strcpy(am_pm, "PM");
				hour -= 12;
			}
			snprintf(hour_time,10,"%02d:%02d %s", hour, min, am_pm);
			break;
		case 2:
			snprintf(hour_time,10,"%02d:%02d", hour, min); 
			break;
		default:
			if (hour > 12) {
				strcpy(am_pm, "PM");
				hour -= 12;
			}
			snprintf(hour_time,10,"%02d:%02d %s", hour, min, am_pm);
		}

		const char *saveTitle = param.GetFileInfo(currentSelectedSave).saveTitle;
		int day   = param.GetFileInfo(currentSelectedSave).modif_time.tm_mday;
		int month = param.GetFileInfo(currentSelectedSave).modif_time.tm_mon + 1;
		int year  = param.GetFileInfo(currentSelectedSave).modif_time.tm_year + 1900;
		s64 sizeK = param.GetFileInfo(currentSelectedSave).size / 1024;
		switch (g_Config.iDateFormat) {
		case 1:
			snprintf(date, 256, "%d/%02d/%02d", year, month, day);
			break;
		case 2:
			snprintf(date, 256, "%02d/%02d/%d", month, day, year);
			break;
		case 3:
			snprintf(date, 256, "%02d/%02d/%d", day, month, year);
			break;
		default:
			snprintf(date, 256, "%d/%02d/%02d", year, month, day);
		}
		snprintf(txt, 1024, "%s\n%s  %s\n%lld KB", saveTitle, date, hour_time, sizeK);
		std::string saveinfoTxt = txt;
		PPGeDrawText(saveinfoTxt.c_str(), 9, 202, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0x80000000));
		PPGeDrawText(saveinfoTxt.c_str(), 8, 200, PPGE_ALIGN_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));
	}
}

void PSPSaveDialog::DisplayMessage(std::string text, bool hasYesNo)
{
	const float WRAP_WIDTH = 254.0f;
	float y = 136.0f, h;
	int n;
	PPGeMeasureText(0, &h, &n, text.c_str(), FONT_SCALE, PPGE_LINE_WRAP_WORD, WRAP_WIDTH);
	float h2 = h * (float)n / 2.0f;
	if (hasYesNo)
	{
		I18NCategory *d = GetI18NCategory("Dialog");
		const char *choiceText;
		u32 yesColor, noColor;
		float x, w;
		if (yesnoChoice == 1) {
			choiceText = d->T("Yes");
			x = 302.0f;
			yesColor = 0xFFFFFFFF;
			noColor  = 0xFFFFFFFF;
		}
		else {
			choiceText = d->T("No");
			x = 366.0f;
			yesColor = 0xFFFFFFFF;
			noColor  = 0xFFFFFFFF;
		}
		PPGeMeasureText(&w, &h, 0, choiceText, FONT_SCALE);
		w = w / 2.0f + 5.5f;
		h /= 2.0f;
		float y2 = y + h2 + 4.0f;
		h2 += h + 4.0f;
		y = 132.0f - h;
		PPGeDrawRect(x - w, y2 - h, x + w, y2 + h, CalcFadedColor(0x40C0C0C0));
		PPGeDrawText(d->T("Yes"), 303.0f, y2+2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(0x80000000));
		PPGeDrawText(d->T("Yes"), 302.0f, y2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(yesColor));
		PPGeDrawText(d->T("No"), 367.0f, y2+2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(0x80000000));
		PPGeDrawText(d->T("No"), 366.0f, y2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(noColor));
		if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0) {
			yesnoChoice = 1;
		}
		else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1) {
			yesnoChoice = 0;
		}
	}
	PPGeDrawTextWrapped(text.c_str(), 335.0f, y+2, WRAP_WIDTH, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(0x80000000));
	PPGeDrawTextWrapped(text.c_str(), 334.0f, y, WRAP_WIDTH, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
	float sy = 122.0f - h2, ey = 150.0f + h2;
	PPGeDrawRect(202.0f, sy, 466.0f, sy + 1.0f, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawRect(202.0f, ey, 466.0f, ey + 1.0f, CalcFadedColor(0xFFFFFFFF));
}

int PSPSaveDialog::Update(int animSpeed)
{
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	if (!param.GetPspParam()) {
		ChangeStatusShutdown(SAVEDATA_SHUTDOWN_DELAY_US);
		return 0;
	}

	if (pendingStatus != SCE_UTILITY_STATUS_RUNNING) {
		// We're actually done, we're just waiting to tell the game that.
		return 0;
	}

	// The struct may have been updated by the game.  This happens in "Where Is My Heart?"
	// Check if it has changed, reload it.
	// TODO: Cut down on preloading?  This rebuilds the list from scratch.
	int size = Memory::Read_U32(requestAddr);
	if (memcmp(Memory::GetPointer(requestAddr), &originalRequest, size) != 0) {
		memset(&request, 0, sizeof(request));
		Memory::Memcpy(&request, requestAddr, size);
		Memory::Memcpy(&originalRequest, requestAddr, size);
		lock_guard guard(paramLock);
		param.SetPspParam(&request);
	}

	UpdateButtons();
	UpdateFade(animSpeed);

	okButtonImg = I_CIRCLE;
	cancelButtonImg = I_CROSS;
	okButtonFlag = CTRL_CIRCLE;
	cancelButtonFlag = CTRL_CROSS;
	if (param.GetPspParam()->common.buttonSwap == 1) {
		okButtonImg = I_CROSS;
		cancelButtonImg = I_CIRCLE;
		okButtonFlag = CTRL_CROSS;
		cancelButtonFlag = CTRL_CIRCLE;
	}

	I18NCategory *d = GetI18NCategory("Dialog");

	switch (display)
	{
		case DS_SAVE_LIST_CHOICE:
			StartDraw();

			DisplaySaveList();
			DisplaySaveDataInfo1();

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_SAVE);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
				StartFade(false);
			} else if (IsButtonPressed(okButtonFlag)) {
				// Save exist, ask user confirm
				if (param.GetFileInfo(currentSelectedSave).size > 0) {
					yesnoChoice = 0;
					display = DS_SAVE_CONFIRM_OVERWRITE;
				} else {
					display = DS_SAVE_SAVING;
					StartIOThread();
				}
			}
			EndDraw();
		break;
		case DS_SAVE_CONFIRM:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Confirm Save", "Do you want to save this data?"), true);

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_SAVE);

			if (IsButtonPressed(cancelButtonFlag) || (IsButtonPressed(okButtonFlag) && yesnoChoice == 0)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
				StartFade(false);
			} else if (IsButtonPressed(okButtonFlag)) {
				display = DS_SAVE_SAVING;
				StartIOThread();
			}

			EndDraw();
		break;
		case DS_SAVE_CONFIRM_OVERWRITE:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Confirm Overwrite","Do you want to overwrite the data?"), true);

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_SAVE);

			if (IsButtonPressed(cancelButtonFlag) || (IsButtonPressed(okButtonFlag) && yesnoChoice == 0)) {
				if (param.GetPspParam()->mode != SCE_UTILITY_SAVEDATA_TYPE_SAVE)
					display = DS_SAVE_LIST_CHOICE;
				else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
					StartFade(false);
				}
			} else if (IsButtonPressed(okButtonFlag)) {
				display = DS_SAVE_SAVING;
				StartIOThread();
			}

			EndDraw();
		break;
		case DS_SAVE_SAVING:
			if (ioThreadStatus != SAVEIO_PENDING) {
				JoinIOThread();
			}

			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Saving","Saving\nPlease Wait..."));

			DisplayBanner(DB_SAVE);

			EndDraw();
		break;
		case DS_SAVE_FAILED:
			JoinIOThread();
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("SavingFailed", "Unable to save data."));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_SAVE);

			if (IsButtonPressed(cancelButtonFlag)) {
				// Go back to the list so they can try again.
				if (param.GetPspParam()->mode != SCE_UTILITY_SAVEDATA_TYPE_SAVE) {
					display = DS_SAVE_LIST_CHOICE;
				} else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_SAVE_DONE:
			if (ioThread) {
				JoinIOThread();
				param.SetPspParam(param.GetPspParam());
			}
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Save completed"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_SAVE);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_SUCCESS;
				// Set the save to use for autosave and autoload
				param.SetSelectedSave(param.GetFileInfo(currentSelectedSave).idx);
				StartFade(false);
			}

			EndDraw();
		break;

		case DS_LOAD_LIST_CHOICE:
			StartDraw();
			
			DisplaySaveList();
			DisplaySaveDataInfo1();

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
				StartFade(false);
			} else if (IsButtonPressed(okButtonFlag)) {
				display = DS_LOAD_LOADING;
				StartIOThread();
			}

			EndDraw();
		break;
		case DS_LOAD_CONFIRM:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("ConfirmLoad", "Load this data?"), true);

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag) || (IsButtonPressed(okButtonFlag) && yesnoChoice == 0)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
				StartFade(false);
			} else if (IsButtonPressed(okButtonFlag)) {
				display = DS_LOAD_LOADING;
				StartIOThread();
			}

			EndDraw();
		break;
		case DS_LOAD_LOADING:
			if (ioThreadStatus != SAVEIO_PENDING) {
				JoinIOThread();
			}

			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Loading","Loading\nPlease Wait..."));

			DisplayBanner(DB_LOAD);

			EndDraw();
		break;
		case DS_LOAD_FAILED:
			JoinIOThread();
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("LoadingFailed", "Unable to load data."));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag)) {
				// Go back to the list so they can try again.
				if (param.GetPspParam()->mode != SCE_UTILITY_SAVEDATA_TYPE_LOAD) {
					display = DS_LOAD_LIST_CHOICE;
				} else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_LOAD_DONE:
			JoinIOThread();
			StartDraw();
			
			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Load completed"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			// Allow OK to be pressed as well to confirm the save.
			// The PSP only allows cancel, but that's generally not great UX.
			// Allowing this here makes it quicker for most users to get into the actual game.
			if (IsButtonPressed(cancelButtonFlag) || IsButtonPressed(okButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_SUCCESS;
				// Set the save to use for autosave and autoload
				param.SetSelectedSave(param.GetFileInfo(currentSelectedSave).idx);
				StartFade(false);
			}

			EndDraw();
		break;
		case DS_LOAD_NODATA:
			StartDraw();

			DisplayMessage(d->T("There is no data"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
				StartFade(false);
			}

			EndDraw();
		break;

		case DS_DELETE_LIST_CHOICE:
			StartDraw();
			
			DisplaySaveList();
			DisplaySaveDataInfo1();

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
				StartFade(false);
			} else if (IsButtonPressed(okButtonFlag)) {
				yesnoChoice = 0;
				display = DS_DELETE_CONFIRM;
			}

			EndDraw();
		break;
		case DS_DELETE_CONFIRM:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("DeleteConfirm", 
						"This save data will be deleted.\nAre you sure you want to continue?"), 
						true);

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag))
				display = DS_DELETE_LIST_CHOICE;
			else if (IsButtonPressed(okButtonFlag)) {
				if (yesnoChoice == 0)
					display = DS_DELETE_LIST_CHOICE;
				else {
					display = DS_DELETE_DELETING;
					StartIOThread();
				}
			}

			EndDraw();
		break;
		case DS_DELETE_DELETING:
			if (ioThreadStatus != SAVEIO_PENDING) {
				JoinIOThread();
			}

			StartDraw();

			DisplayMessage(d->T("Deleting","Deleting\nPlease Wait..."));

			DisplayBanner(DB_DELETE);

			EndDraw();
		break;
		case DS_DELETE_FAILED:
			JoinIOThread();
			StartDraw();

			DisplayMessage(d->T("DeleteFailed", "Unable to delete data."));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				display = DS_DELETE_LIST_CHOICE;
			}

			EndDraw();
		break;
		case DS_DELETE_DONE:
			if (ioThread) {
				JoinIOThread();
				param.SetPspParam(param.GetPspParam());
			}
			StartDraw();
			
			DisplayMessage(d->T("Delete completed"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				if (param.GetFilenameCount() == 0)
					display = DS_DELETE_NODATA;
				else
					display = DS_DELETE_LIST_CHOICE;
			}

			EndDraw();
		break;
		case DS_DELETE_NODATA:
			StartDraw();
			
			DisplayMessage(d->T("There is no data"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
				StartFade(false);
			}

			EndDraw();
		break;

		case DS_NONE: // For action which display nothing
			switch (ioThreadStatus) {
			case SAVEIO_NONE:
				StartIOThread();
				break;
			case SAVEIO_PENDING:
			case SAVEIO_DONE:
				// To make sure there aren't any timing variations, we sync the next frame.
				JoinIOThread();
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
				break;
			}
		break;

		default:
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
		break;
	}

	if (status == SCE_UTILITY_STATUS_FINISHED || pendingStatus == SCE_UTILITY_STATUS_FINISHED)
		Memory::Memcpy(requestAddr, &request, request.common.size);
	
	return 0;
}

void PSPSaveDialog::ExecuteIOAction() {
	lock_guard guard(paramLock);
	switch (display) {
	case DS_LOAD_LOADING:
		if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave)) {
			display = DS_LOAD_DONE;
		} else {
			display = DS_LOAD_FAILED;
		}
		break;
	case DS_SAVE_SAVING:
		if (param.Save(param.GetPspParam(), GetSelectedSaveDirName())) {
			display = DS_SAVE_DONE;
		} else {
			display = DS_SAVE_FAILED;
		}
		break;
	case DS_DELETE_DELETING:
		if (param.Delete(param.GetPspParam(),currentSelectedSave)) {
			display = DS_DELETE_DONE;
		} else {
			display = DS_DELETE_FAILED;
		}
		break;
	case DS_NONE:
		ExecuteNotVisibleIOAction();
		break;

	default:
		// Nothing to do here.
		break;
	}

	ioThreadStatus = SAVEIO_DONE;
}

void PSPSaveDialog::ExecuteNotVisibleIOAction() {
	switch ((SceUtilitySavedataType)(u32)param.GetPspParam()->mode) {
	case SCE_UTILITY_SAVEDATA_TYPE_LOAD: // Only load and exit
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
		if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave)) {
			param.GetPspParam()->common.result = 0;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_SAVE: // Only save and exit
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
		if (param.Save(param.GetPspParam(), GetSelectedSaveDirName())) {
			param.GetPspParam()->common.result = 0;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
		param.GetPspParam()->common.result = param.GetSizes(param.GetPspParam());
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_LIST:
		param.GetList(param.GetPspParam());
		param.GetPspParam()->common.result = 0;
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_FILES:
		param.GetPspParam()->common.result = param.GetFilesList(param.GetPspParam());
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
		{
			bool result = param.GetSize(param.GetPspParam());
			// TODO: According to JPCSP, should test/verify this part but seems edge casey.
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_DRIVER_READY) {
				param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK;
			} else if (result) {
				param.GetPspParam()->common.result = 0;
			} else {
				param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
			}
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
		DEBUG_LOG(SCEUTILITY, "sceUtilitySavedata DELETEDATA: %s", param.GetPspParam()->saveName);
		param.GetPspParam()->common.result = param.DeleteData(param.GetPspParam());
		break;
	//case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
	case SCE_UTILITY_SAVEDATA_TYPE_SINGLEDELETE:
		if (param.Delete(param.GetPspParam(), param.GetSelectedSave())) {
			param.GetPspParam()->common.result = 0;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
		}
		break;
	// TODO: Should reset the directory's other files.
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
		if (param.Save(param.GetPspParam(), GetSelectedSaveDirName(), param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE)) {
			param.GetPspParam()->common.result = 0;
		} else if (MemoryStick_FreeSpace() == 0) {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_MEMSTICK_FULL;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
		if (param.Save(param.GetPspParam(), GetSelectedSaveDirName(), param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE)) {
			param.GetPspParam()->common.result = 0;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
		if (!param.IsSaveDirectoryExist(param.GetPspParam())){
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
		} else if (!param.IsSfoFileExist(param.GetPspParam())) {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN;
		} else if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave, param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE)) {
			param.GetPspParam()->common.result = 0;
		} else {
			param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND;
		}
		break;
	default:
		break;
	}
}

void PSPSaveDialog::JoinIOThread() {
	if (ioThread) {
		ioThread->join();
		delete ioThread;
		ioThread = 0;
	}
}

static void DoExecuteIOAction(PSPSaveDialog *dialog) {
	setCurrentThreadName("SaveIO");
	dialog->ExecuteIOAction();
}

void PSPSaveDialog::StartIOThread() {
	if (ioThread) {
		WARN_LOG_REPORT(SCEUTILITY, "Starting a save io thread when one already pending, uh oh.");
		JoinIOThread();
	}

	ioThreadStatus = SAVEIO_PENDING;
	ioThread = new std::thread(&DoExecuteIOAction, this);
}

int PSPSaveDialog::Shutdown(bool force) {
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	JoinIOThread();
	ioThreadStatus = SAVEIO_NONE;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(SAVEDATA_SHUTDOWN_DELAY_US);
	}
	param.SetPspParam(0);

	return 0;
}

void PSPSaveDialog::DoState(PointerWrap &p) {
	JoinIOThread();
	PSPDialog::DoState(p);

	auto s = p.Section("PSPSaveDialog", 1, 2);
	if (!s) {
		return;
	}

	p.Do(display);
	param.DoState(p);
	p.Do(request);
	// Just reset it.
	bool hasParam = param.GetPspParam() != NULL;
	p.Do(hasParam);
	if (hasParam) {
		param.SetPspParam(&request);
	}
	p.Do(requestAddr);
	p.Do(currentSelectedSave);
	p.Do(yesnoChoice);
	if (s > 2) {
		p.Do(ioThreadStatus);
	} else {
		ioThreadStatus = SAVEIO_NONE;
	}
}

pspUtilityDialogCommon *PSPSaveDialog::GetCommonParam() {
	return &param.GetPspParam()->common;
}
