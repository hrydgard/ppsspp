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

#include "PSPSaveDialog.h"
#include "../Util/PPGeDraw.h"
#include "../HLE/sceCtrl.h"
#include "../HLE/sceUtility.h"
#include "../Core/MemMap.h"
#include "../Config.h"
#include "Core/Reporting.h"
#include "Core/HW/MemoryStick.h"
#include "i18n/i18n.h"

const float FONT_SCALE = 0.55f;

PSPSaveDialog::PSPSaveDialog()
	: PSPDialog()
	, display(DS_NONE)
	, currentSelectedSave(0)
{
	param.SetPspParam(0);
}

PSPSaveDialog::~PSPSaveDialog() {
}

int PSPSaveDialog::Init(int paramAddr)
{
	// Ignore if already running
	if (status != SCE_UTILITY_STATUS_NONE && status != SCE_UTILITY_STATUS_SHUTDOWN)	{
		ERROR_LOG(HLE,"A save request is already running !");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}
	
	requestAddr = paramAddr;
	int size = Memory::Read_U32(requestAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different save request format
	Memory::Memcpy(&request, requestAddr, size);
	Memory::Memcpy(&originalRequest, requestAddr, size);

	u32 retval = param.SetPspParam(&request);

	INFO_LOG(HLE,"sceUtilitySavedataInitStart(%08x)", paramAddr);
	INFO_LOG(HLE,"Mode: %i", param.GetPspParam()->mode);

	yesnoChoice = 1;
	switch (param.GetPspParam()->focus)
	{
	case SCE_UTILITY_SAVEDATA_FOCUS_NAME:
		// TODO: This should probably force not using the list?
		currentSelectedSave = 0;
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
		WARN_LOG(HLE, "Unknown save list focus option: %d", param.GetPspParam()->focus);
		currentSelectedSave = 0;
		break;
	}

	switch (param.GetPspParam()->mode)
	{
		case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
			DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0)
				display = DS_LOAD_CONFIRM;
			else
				display = DS_LOAD_NODATA;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
			DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD:
			DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_LOAD_NODATA;
			else
				display = DS_LOAD_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
			DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0)
			{
				yesnoChoice = 0;
				display = DS_SAVE_CONFIRM_OVERWRITE;
			}
			else
				display = DS_SAVE_CONFIRM;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
			DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE:
			DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_SAVE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE:
			DEBUG_LOG(HLE, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
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

		case SCE_UTILITY_SAVEDATA_TYPE_DELETE: // This run on PSP display a list of all save on the PSP. Weird. (Not really, it's to let you free up space)
			display = DS_DELETE_LIST_CHOICE;
			break;
		default:
		{
			ERROR_LOG_REPORT(HLE, "Load/Save function %d not coded. Title: %s Save: %s File: %s", param.GetPspParam()->mode, param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			param.GetPspParam()->common.result = 0;
			status = SCE_UTILITY_STATUS_INITIALIZE;
			display = DS_NONE;
			return 0; // Return 0 should allow the game to continue, but missing function must be implemented and returning the right value or the game can block.
		}
		break;
	}

	status = (int)retval < 0 ? SCE_UTILITY_STATUS_SHUTDOWN : SCE_UTILITY_STATUS_INITIALIZE;

	lastButtons = __CtrlPeekButtons();
	StartFade(true);

	/*INFO_LOG(HLE,"Dump Param :");
	INFO_LOG(HLE,"size : %d",param.GetPspParam()->size);
	INFO_LOG(HLE,"language : %d",param.GetPspParam()->language);
	INFO_LOG(HLE,"buttonSwap : %d",param.GetPspParam()->buttonSwap);
	INFO_LOG(HLE,"result : %d",param.GetPspParam()->common.result);
	INFO_LOG(HLE,"mode : %d",param.GetPspParam()->mode);
	INFO_LOG(HLE,"bind : %d",param.GetPspParam()->bind);
	INFO_LOG(HLE,"overwriteMode : %d",param.GetPspParam()->overwriteMode);
	INFO_LOG(HLE,"gameName : %s",param.GetGameName(param.GetPspParam()).c_str());
	INFO_LOG(HLE,"saveName : %s",param.GetPspParam()->saveName);
	INFO_LOG(HLE,"saveNameList : %08x",*((unsigned int*)&param.GetPspParam()->saveNameList));
	INFO_LOG(HLE,"fileName : %s",param.GetPspParam()->fileName);
	INFO_LOG(HLE,"dataBuf : %08x",*((unsigned int*)&param.GetPspParam()->dataBuf));
	INFO_LOG(HLE,"dataBufSize : %u",param.GetPspParam()->dataBufSize);
	INFO_LOG(HLE,"dataSize : %u",param.GetPspParam()->dataSize);

	INFO_LOG(HLE,"sfo title : %s",param.GetPspParam()->sfoParam.title);
	INFO_LOG(HLE,"sfo savedataTitle : %s",param.GetPspParam()->sfoParam.savedataTitle);
	INFO_LOG(HLE,"sfo detail : %s",param.GetPspParam()->sfoParam.detail);

	INFO_LOG(HLE,"icon0 data : %08x",*((unsigned int*)&param.GetPspParam()->icon0FileData.buf));
	INFO_LOG(HLE,"icon0 size : %u",param.GetPspParam()->icon0FileData.bufSize);

	INFO_LOG(HLE,"icon1 data : %08x",*((unsigned int*)&param.GetPspParam()->icon1FileData.buf));
	INFO_LOG(HLE,"icon1 size : %u",param.GetPspParam()->icon1FileData.bufSize);

	INFO_LOG(HLE,"pic1 data : %08x",*((unsigned int*)&param.GetPspParam()->pic1FileData.buf));
	INFO_LOG(HLE,"pic1 size : %u",param.GetPspParam()->pic1FileData.bufSize);

	INFO_LOG(HLE,"snd0 data : %08x",*((unsigned int*)&param.GetPspParam()->snd0FileData.buf));
	INFO_LOG(HLE,"snd0 size : %u",param.GetPspParam()->snd0FileData.bufSize);*/
	return retval;
}

const std::string PSPSaveDialog::GetSelectedSaveDirName()
{
	switch (param.GetPspParam()->mode)
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

	// SZIES ignores saveName it seems.

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
	PPGeDrawText(title, 30, 11, PPGE_ALIGN_VCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
}

void PSPSaveDialog::DisplaySaveList(bool canMove)
{
	int displayCount = 0;
	for (int i = 0; i < param.GetFilenameCount(); i++)
	{
		int textureColor = CalcFadedColor(0xFFFFFFFF);

		if (param.GetFileInfo(i).size == 0 && param.GetFileInfo(i).textureData == 0) 
			textureColor = CalcFadedColor(0xFF777777);

		// Calc save image position on screen
		float w = 144;
		float h = 80;
		float x = 27;
		if (displayCount != currentSelectedSave) {
			w = 81;
			h = 45;
			x = 58.5f;
		}
		float y = 97;
		if (displayCount < currentSelectedSave)
			y -= 13 + 45 * (currentSelectedSave - displayCount);
		else if (displayCount > currentSelectedSave)
			y += 48 + 45 * (displayCount - currentSelectedSave);

		int tw = 256;
		int th = 256;
		if (param.GetFileInfo(i).textureData != 0) {
			tw = param.GetFileInfo(i).textureWidth;
			th = param.GetFileInfo(i).textureHeight;
			PPGeSetTexture(param.GetFileInfo(i).textureData, param.GetFileInfo(i).textureWidth, param.GetFileInfo(i).textureHeight);
		} else
			PPGeDisableTexture();
		PPGeDrawImage(x, y, w, h, 0, 0, 1, 1, tw, th, textureColor);
		PPGeSetDefaultTexture();
		displayCount++;
	}

	if (canMove) {
		if (IsButtonPressed(CTRL_UP) && currentSelectedSave > 0)
			currentSelectedSave--;
		else if (IsButtonPressed(CTRL_DOWN) && currentSelectedSave < (param.GetFilenameCount()-1))
			currentSelectedSave++;
	}
}

void PSPSaveDialog::DisplaySaveIcon()
{
	int textureColor = CalcFadedColor(0xFFFFFFFF);

	if (param.GetFileInfo(currentSelectedSave).size == 0)
		textureColor = CalcFadedColor(0xFF777777);

	// Calc save image position on screen
	float w = 144;
	float h = 80;
	float x = 27;
	float y = 97;

	int tw = 256;
	int th = 256;
	if (param.GetFileInfo(currentSelectedSave).textureData != 0) {
		tw = param.GetFileInfo(currentSelectedSave).textureWidth;
		th = param.GetFileInfo(currentSelectedSave).textureHeight;
		PPGeSetTexture(param.GetFileInfo(currentSelectedSave).textureData, param.GetFileInfo(currentSelectedSave).textureWidth, param.GetFileInfo(currentSelectedSave).textureHeight);
	} else
		PPGeDisableTexture();
	PPGeDrawImage(x, y, w, h, 0, 0 ,1 ,1 ,tw, th, textureColor);
	if (param.GetFileInfo(currentSelectedSave).textureData != 0)
		PPGeSetDefaultTexture();
}

void PSPSaveDialog::DisplaySaveDataInfo1()
{
	if (param.GetFileInfo(currentSelectedSave).size == 0) {
		I18NCategory *d = GetI18NCategory("Dialog");
		PPGeDrawText(d->T("New Save"), 180, 136, PPGE_ALIGN_VCENTER, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
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
		
		PPGeDrawRect(180, 136, 980, 137, CalcFadedColor(0xFFFFFFFF));
		std::string titleTxt = title;
		std::string timeTxt = time;
		std::string saveTitleTxt = saveTitle;
		std::string saveDetailTxt = saveDetail;

		PPGeDrawText(titleTxt.c_str(), 180, 136, PPGE_ALIGN_BOTTOM, 0.6f, CalcFadedColor(0xFFC0C0C0));
		PPGeDrawText(timeTxt.c_str(), 180, 137, PPGE_ALIGN_LEFT, 0.45f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(saveTitleTxt.c_str(), 175, 159, PPGE_ALIGN_LEFT, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(saveDetailTxt.c_str(), 175, 181, PPGE_ALIGN_LEFT, 0.45f, CalcFadedColor(0xFFFFFFFF));
	}
}

void PSPSaveDialog::DisplaySaveDataInfo2()
{
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
		PPGeDrawText(saveinfoTxt.c_str(), 8, 200, PPGE_ALIGN_LEFT, 0.45f, CalcFadedColor(0xFFFFFFFF));
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
			yesColor = 0xFF0FFFFF;
			noColor  = 0xFFFFFFFF;
		}
		else {
			choiceText = d->T("No");
			x = 366.0f;
			yesColor = 0xFFFFFFFF;
			noColor  = 0xFF0FFFFF;
		}
		PPGeMeasureText(&w, &h, 0, choiceText, FONT_SCALE);
		w = w / 2.0f + 5.5f;
		h /= 2.0f;
		float y2 = y + h2 + 4.0f;
		h2 += h + 4.0f;
		y = 132.0f - h;
		PPGeDrawRect(x - w, y2 - h, x + w, y2 + h, CalcFadedColor(0x6DCFCFCF));
		PPGeDrawText(d->T("Yes"), 302.0f, y2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(yesColor));
		PPGeDrawText(d->T("No"), 366.0f, y2, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(noColor));
		if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0) {
			yesnoChoice = 1;
		}
		else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1) {
			yesnoChoice = 0;
		}
	}
	PPGeDrawTextWrapped(text.c_str(), 334.0f, y, WRAP_WIDTH, PPGE_ALIGN_CENTER, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
	float sy = 122.0f - h2, ey = 150.0f + h2;
	PPGeDrawRect(202.0f, sy, 466.0f, sy + 1.0f, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawRect(202.0f, ey, 466.0f, ey + 1.0f, CalcFadedColor(0xFFFFFFFF));
}

void PSPSaveDialog::DisplayTitle(std::string name)
{
	PPGeDrawText(name.c_str(), 10, 10, PPGE_ALIGN_LEFT, 0.45f, CalcFadedColor(0xFFFFFFFF));
}

int PSPSaveDialog::Update()
{
	switch (status) {
	case SCE_UTILITY_STATUS_FINISHED:
		status = SCE_UTILITY_STATUS_SHUTDOWN;
		break;
	default:
		break;
	}

	if (status != SCE_UTILITY_STATUS_RUNNING)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	if (!param.GetPspParam()) {
		status = SCE_UTILITY_STATUS_SHUTDOWN;
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
		param.SetPspParam(&request);
	}

	buttons = __CtrlPeekButtons();
	UpdateFade();

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
					if (param.Save(param.GetPspParam(), GetSelectedSaveDirName())) {
						param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
						display = DS_SAVE_DONE;
					} else
						display = DS_SAVE_LIST_CHOICE; // This will probably need error message ?
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
				if (param.Save(param.GetPspParam(), GetSelectedSaveDirName())) {
					param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
					display = DS_SAVE_DONE;
				} else {
					// TODO: This should probably show an error message?
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_SAVE_CONFIRM_OVERWRITE:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Do you want to overwrite the data?"), true);

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
				if (param.Save(param.GetPspParam(), GetSelectedSaveDirName())) {
					param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
					display = DS_SAVE_DONE;
				} else {
					// TODO: This should probably show an error message?
					if (param.GetPspParam()->mode != SCE_UTILITY_SAVEDATA_TYPE_SAVE)
						display = DS_SAVE_LIST_CHOICE;
					else
						StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_SAVE_SAVING:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Saving","Saving\nPlease Wait..."));

			DisplayBanner(DB_SAVE);

			EndDraw();
		break;
		case DS_SAVE_DONE:
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
				if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave))
					display = DS_LOAD_DONE;
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
				if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave))
					display = DS_LOAD_DONE;
				else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_LOAD_LOADING:
			StartDraw();

			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Loading","Loading\nPlease Wait..."));

			DisplayBanner(DB_LOAD);

			EndDraw();
		break;
		case DS_LOAD_DONE:
			StartDraw();
			
			DisplaySaveIcon();
			DisplaySaveDataInfo2();

			DisplayMessage(d->T("Load completed"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag)) {
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
					if (param.Delete(param.GetPspParam(),currentSelectedSave)) {
						param.SetPspParam(param.GetPspParam()); // Optim : Just Update modified save
						display = DS_DELETE_DONE;
					} else 		
						display = DS_DELETE_LIST_CHOICE; // This will probably need error message ?
				}
			}

			EndDraw();
		break;
		case DS_DELETE_DELETING:
			StartDraw();

			DisplayMessage(d->T("Deleting","Deleting\nPlease Wait..."));

			DisplayBanner(DB_DELETE);

			EndDraw();
		break;
		case DS_DELETE_DONE:
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
		{
			switch (param.GetPspParam()->mode)
			{
				case SCE_UTILITY_SAVEDATA_TYPE_LOAD: // Only load and exit
				case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
					if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_SAVE: // Only save and exit
				case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
					if (param.Save(param.GetPspParam(), GetSelectedSaveDirName()))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
					param.GetPspParam()->common.result = param.GetSizes(param.GetPspParam());
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_LIST:
					param.GetList(param.GetPspParam());
					param.GetPspParam()->common.result = 0;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_FILES:
					param.GetPspParam()->common.result = param.GetFilesList(param.GetPspParam());
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
					{
						bool result = param.GetSize(param.GetPspParam());
						// TODO: According to JPCSP, should test/verify this part but seems edge casey.
						if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_DRIVER_READY)
							param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK;
						else if (result)
							param.GetPspParam()->common.result = 0;
						else
							param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
						status = SCE_UTILITY_STATUS_FINISHED;
					}
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
					// TODO: This should probably actually delete something.
					// For now, always say it couldn't be deleted.
					WARN_LOG(HLE, "FAKE sceUtilitySavedata DELETEDATA: %s", param.GetPspParam()->saveName);
					param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_STATUS;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				//case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
				case SCE_UTILITY_SAVEDATA_TYPE_SINGLEDELETE:
					if (param.Delete(param.GetPspParam(), param.GetSelectedSave()))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				// TODO: Should reset the directory's other files.
				case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
				case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
					if (param.Save(param.GetPspParam(), GetSelectedSaveDirName(), param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
				case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
					if (param.Save(param.GetPspParam(), GetSelectedSaveDirName(), param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
				case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
					if (param.Load(param.GetPspParam(), GetSelectedSaveDirName(), currentSelectedSave, param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE))
						param.GetPspParam()->common.result = 0;
					else
						param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA; // not sure if correct code
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
				default:
					status = SCE_UTILITY_STATUS_FINISHED;
				break;
			}
		}
		break;
		default:
			status = SCE_UTILITY_STATUS_FINISHED;
		break;
	}

	lastButtons = buttons;

	if (status == SCE_UTILITY_STATUS_FINISHED)
		Memory::Memcpy(requestAddr, &request, request.common.size);
	
	return 0;
}

int PSPSaveDialog::Shutdown(bool force)
{
	if (status != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown();
	param.SetPspParam(0);

	return 0;
}

void PSPSaveDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);
	p.Do(display);
	param.DoState(p);
	p.Do(request);
	// Just reset it.
	bool hasParam = param.GetPspParam() != NULL;
	p.Do(hasParam);
	if (hasParam)
		param.SetPspParam(&request);
	p.Do(requestAddr);
	p.Do(currentSelectedSave);
	p.Do(yesnoChoice);
	p.DoMarker("PSPSaveDialog");
}

pspUtilityDialogCommon *PSPSaveDialog::GetCommonParam()
{
	return &param.GetPspParam()->common;
}
