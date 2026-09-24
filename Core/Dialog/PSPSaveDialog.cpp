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

#include <algorithm>
#include <ctime>
#include <thread>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Util/PathUtil.h"
#include "Core/Util/PPGeDraw.h"
#include "Common/TimeUtil.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HW/MemoryStick.h"
#include "Core/MemMapHelpers.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/SaveState.h"

static double g_lastSaveTime = -1.0;

// Actually this should be called on both saves and loads, since just after a load it's safe to exit.
void ResetSecondsSinceLastGameSave() {
	g_lastSaveTime = time_now_d();
}

void ShowSaveLoadIndicator(bool save) {
	if (g_Config.bShowSaveLoadIndicator) {
		g_OSD.Show(OSDType::STATUS_ICON, "", "", save ? "I_ROTATE_RIGHT" : "I_ROTATE_LEFT", 1.0f, "save_indicator");
		g_OSD.SetFlags("save_indicator", (save ? OSDMessageFlags::SpinRight : OSDMessageFlags::SpinLeft) | OSDMessageFlags::Transparent);
	}
}

double SecondsSinceLastGameSave() {
	if (g_lastSaveTime < 0) {
		return -1.0;
	} else {
		return time_now_d() - g_lastSaveTime;
	}
}

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

static bool IsNotVisibleAction(SceUtilitySavedataType type) {
	switch (type) {
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
	case SCE_UTILITY_SAVEDATA_TYPE_LIST:
	case SCE_UTILITY_SAVEDATA_TYPE_FILES:
	case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
		return true;

	default:
		break;
	}
	return false;
}

PSPSaveDialog::PSPSaveDialog(UtilityDialogType type) : PSPDialog(type) {
	param.SetPspParam(0);
}

PSPSaveDialog::~PSPSaveDialog() {
	if (ioThread.joinable()) {
		ioThread.join();
	}
}

int PSPSaveDialog::Init(int paramAddr) {
	// Ignore if already running
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(Log::sceUtility, "A save request is already running, not starting a new one");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	if (ioThread.joinable()) {
		// Normally shouldn't be the case here.
		ioThread.join();
	}

	ioThreadStatus = SAVEIO_NONE;

	requestAddr = 0;
	const int check = CheckRequest(paramAddr, { SAVEDATA_DIALOG_SIZE_V1, SAVEDATA_DIALOG_SIZE_V2, SAVEDATA_DIALOG_SIZE_V3 });
	if (check < 0) {
		ERROR_LOG(Log::sceUtility, "sceUtilitySavedataInitStart: bad request at %08x: %08x", paramAddr, check);
		return check;
	}
	if (!Memory::IsValid4AlignedAddress(paramAddr)) {
		return SCE_KERNEL_ERROR_BAD_ARGUMENT;  // untested
	}
	requestAddr = paramAddr;

	int size = Memory::ReadUnchecked_U32(requestAddr);
	memset(&request, 0, sizeof(request));
	Memory::Memcpy(&request, requestAddr, size);
	Memory::Memcpy(&originalRequest, requestAddr, size);

	// gameName/saveName/fileName become parts of host filesystem paths.
	// Reject path separators and bare dot components so a crafted request
	// can't escape the save directory (path traversal).
	if (HasPathTraversal(StringViewFromFixedSizeField(request.gameName)) ||
		HasPathTraversal(StringViewFromFixedSizeField(request.saveName)) ||
		HasPathTraversal(StringViewFromFixedSizeField(request.fileName))) {
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilitySavedataInitStart: path separator in name fields");
		return SCE_ERROR_UTILITY_INVALID_PARAM_SIZE;
	}

	param.SetIgnoreTextures(IsNotVisibleAction((SceUtilitySavedataType)(u32)request.mode));
	param.ClearSFOCache();
	int retval = param.SetPspParam(&request);

	const u32 mode = (u32)param.GetPspParam()->mode;
	const char *modeName = mode < ARRAY_SIZE(utilitySavedataTypeNames) ? utilitySavedataTypeNames[mode] : "UNKNOWN";
	INFO_LOG(Log::sceUtility,"sceUtilitySavedataInitStart(%08x) - %s (%d)", paramAddr, modeName, mode);
	INFO_LOG(Log::sceUtility,"sceUtilitySavedataInitStart(%08x) : Game key (hex): %s", paramAddr, param.GetKey(param.GetPspParam()).c_str());

	yesnoChoice = 1;
	switch ((SceUtilitySavedataFocus)(u32)param.GetPspParam()->focus) {
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
		WARN_LOG(Log::sceUtility, "Unknown save list focus option: %d", param.GetPspParam()->focus);
		currentSelectedSave = 0;
		break;
	}

	if (!param.WouldHaveMultiSaveName(param.GetPspParam()))
		currentSelectedSave = 0;

	switch ((SceUtilitySavedataType)(u32)param.GetPspParam()->mode)
	{
		case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
			DEBUG_LOG(Log::sceUtility, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0) {
				if (param.GetFileInfo(0).broken) {
					param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN;
					display = DS_LOAD_FAILED;
				} else {
					display = DS_LOAD_CONFIRM;
				}
			} else
				display = DS_LOAD_NODATA;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
			DEBUG_LOG(Log::sceUtility, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetSaveName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD:
			DEBUG_LOG(Log::sceUtility, "Loading. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_LOAD_NODATA;
			else
				display = DS_LOAD_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
			DEBUG_LOG(Log::sceUtility, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0) {
				yesnoChoice = 0;
				display = DS_SAVE_CONFIRM_OVERWRITE;
			}
			else
				display = DS_SAVE_CONFIRM;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
			DEBUG_LOG(Log::sceUtility, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			// Is this necessary?
			// currentSelectedSave = param.GetSelectedSave();
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE:
			DEBUG_LOG(Log::sceUtility, "Saving. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_SAVE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE:
			DEBUG_LOG(Log::sceUtility, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if(param.GetFilenameCount() == 0)
				display = DS_DELETE_NODATA;
			else
				display = DS_DELETE_LIST_CHOICE;
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
		case SCE_UTILITY_SAVEDATA_TYPE_LIST:
		case SCE_UTILITY_SAVEDATA_TYPE_FILES:
		case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE:
		case SCE_UTILITY_SAVEDATA_TYPE_ERASE:
		case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
			display = DS_NONE;
			break;

		case SCE_UTILITY_SAVEDATA_TYPE_DELETE:
			DEBUG_LOG(Log::sceUtility, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFileInfo(0).size != 0) {
				yesnoChoice = 0;
				display = DS_DELETE_CONFIRM;
			} else
				display = DS_DELETE_NODATA;
			break;

		case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
			DEBUG_LOG(Log::sceUtility, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			display = DS_NONE;
			break;

		case SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE: 
			DEBUG_LOG(Log::sceUtility, "Delete. Title: %s Save: %s File: %s", param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
			if (param.GetFilenameCount() == 0)
				display = DS_DELETE_NODATA;
			else
				display = DS_DELETE_LIST_CHOICE;
			break;
		default:
		{
			ERROR_LOG_REPORT(Log::sceUtility, "Load/Save function %d not coded. Title: %s Save: %s File: %s", (SceUtilitySavedataType)(u32)param.GetPspParam()->mode, param.GetGameName(param.GetPspParam()).c_str(), param.GetGameName(param.GetPspParam()).c_str(), param.GetFileName(param.GetPspParam()).c_str());
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

	param.ClearSFOCache();
	InitCommon();
	UpdateButtons();
	StartFade(true);

	/*INFO_LOG(Log::sceUtility,"Dump Param :");
	INFO_LOG(Log::sceUtility,"size : %d",param.GetPspParam()->common.size);
	INFO_LOG(Log::sceUtility,"language : %d",param.GetPspParam()->common.language);
	INFO_LOG(Log::sceUtility,"buttonSwap : %d",param.GetPspParam()->common.buttonSwap);
	INFO_LOG(Log::sceUtility,"result : %d",param.GetPspParam()->common.result);
	INFO_LOG(Log::sceUtility,"mode : %d",param.GetPspParam()->mode);
	INFO_LOG(Log::sceUtility,"bind : %d",param.GetPspParam()->bind);
	INFO_LOG(Log::sceUtility,"overwriteMode : %d",param.GetPspParam()->overwriteMode);
	INFO_LOG(Log::sceUtility,"gameName : %s",param.GetGameName(param.GetPspParam()).c_str());
	INFO_LOG(Log::sceUtility,"saveName : %s",param.GetPspParam()->saveName);
	INFO_LOG(Log::sceUtility,"saveNameList : %08x",*((unsigned int*)&param.GetPspParam()->saveNameList));
	INFO_LOG(Log::sceUtility,"fileName : %s",param.GetPspParam()->fileName);
	INFO_LOG(Log::sceUtility,"dataBuf : %08x",*((unsigned int*)&param.GetPspParam()->dataBuf));
	INFO_LOG(Log::sceUtility,"dataBufSize : %u",param.GetPspParam()->dataBufSize);
	INFO_LOG(Log::sceUtility,"dataSize : %u",param.GetPspParam()->dataSize);

	INFO_LOG(Log::sceUtility,"sfo title : %s",param.GetPspParam()->sfoParam.title);
	INFO_LOG(Log::sceUtility,"sfo savedataTitle : %s",param.GetPspParam()->sfoParam.savedataTitle);
	INFO_LOG(Log::sceUtility,"sfo detail : %s",param.GetPspParam()->sfoParam.detail);

	INFO_LOG(Log::sceUtility,"icon0 data : %08x",*((unsigned int*)&param.GetPspParam()->icon0FileData.buf));
	INFO_LOG(Log::sceUtility,"icon0 size : %u",param.GetPspParam()->icon0FileData.bufSize);

	INFO_LOG(Log::sceUtility,"icon1 data : %08x",*((unsigned int*)&param.GetPspParam()->icon1FileData.buf));
	INFO_LOG(Log::sceUtility,"icon1 size : %u",param.GetPspParam()->icon1FileData.bufSize);

	INFO_LOG(Log::sceUtility,"pic1 data : %08x",*((unsigned int*)&param.GetPspParam()->pic1FileData.buf));
	INFO_LOG(Log::sceUtility,"pic1 size : %u",param.GetPspParam()->pic1FileData.bufSize);

	INFO_LOG(Log::sceUtility,"snd0 data : %08x",*((unsigned int*)&param.GetPspParam()->snd0FileData.buf));
	INFO_LOG(Log::sceUtility,"snd0 size : %u",param.GetPspParam()->snd0FileData.bufSize);*/
	INFO_LOG(Log::sceUtility, "Return value: %d", retval);
	return retval;
}

std::string PSPSaveDialog::GetSelectedSaveDirName() const
{
	switch ((SceUtilitySavedataType)(u32)param.GetPspParam()->mode)
	{
	case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
	case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
	case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETE:
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

	// SIZES ignores saveName it seems.

	default:
		return param.GetSaveDirName(param.GetPspParam(), currentSelectedSave);
		break;
	}
}

void PSPSaveDialog::DisplayBanner(int which)
{
	auto di = GetI18NCategory(I18NCat::DIALOG);
	PPGeDrawRect(0, 0, 480, 23, CalcFadedColor(0x65636358));

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
	textStyle.hasShadow = false;

	std::string_view title;
	switch (which) {
	case DB_SAVE:
		title = di->T("Save");
		break;
	case DB_LOAD:
		title = di->T("Load");
		break;
	case DB_DELETE:
		title = di->T("Delete");
		break;
	default:
		title = "";
		break;
	}
	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 6, 12.0f, 12.0f, 1, 10, 1, 10, 10, 10, FadedImageStyle());
	PPGeDrawText(title, 30, 11, textStyle);
}

void PSPSaveDialog::DisplaySaveList(bool canMove) {
	std::lock_guard<std::mutex> guard(paramLock);
	static int upFramesHeld = 0;
	static int downFramesHeld = 0;

	for (int displayCount = 0; displayCount < param.GetFilenameCount(); displayCount++) {
		PPGeImageStyle imageStyle = FadedImageStyle();
		imageStyle.alphaBlend = false;
		auto fileInfo = param.GetFileInfo(displayCount);

		if (fileInfo.size == 0 && fileInfo.texture && fileInfo.texture->IsValid())
			imageStyle.color = CalcFadedColor(0xFF777777);

		// Calc save image position on screen
		float w, h, x;
		float y = 97;
		if (displayCount != currentSelectedSave) {
			w = 81;
			h = 45;
			x = 58.5f;
		} else {
			w = 144;
			h = 80;
			x = 27;
		}
		if (displayCount < currentSelectedSave)
			y -= 13 + 45 * (currentSelectedSave - displayCount);
		else if (displayCount > currentSelectedSave)
			y += 48 + 45 * (displayCount - currentSelectedSave);

		// Skip if it's well outside the screen.
		if (y > 472.0f || y < -200.0f)
			continue;

		int pad = 0;
		if (fileInfo.texture != nullptr && fileInfo.texture->IsValid()) {
			fileInfo.texture->SetTexture();
			int tw = fileInfo.texture->Width();
			int th = fileInfo.texture->Height();
			float scale = (float)h / (float)th;
			int scaledW = (int)(tw * scale);
			pad = (w - scaledW) / 2;
			w = scaledW;

			PPGeDrawImage(x + pad, y, w, h, 0, 0, 1, 1, tw, th, imageStyle);
		} else {
			PPGeDrawRect(x, y, x + w, y + h, 0x88666666);
		}
		if (displayCount == currentSelectedSave) {
			float b = 1.2f;
			uint32_t bc = CalcFadedColor(0xD0FFFFFF);
			PPGeDrawRect(x + pad - b, y - b, x + pad + w + b, y, bc); // top border
			PPGeDrawRect(x + pad - b, y, x + pad, y + h, bc); // left border
			PPGeDrawRect(x + pad - b, y + h, x + pad + w + b, y + h + b, bc); //bottom border
			PPGeDrawRect(x + pad + w, y, x + pad + w + b, y + h, bc); //right border
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

void PSPSaveDialog::DisplaySaveIcon(bool checkExists) {
	std::lock_guard<std::mutex> guard(paramLock);
	PPGeImageStyle imageStyle = FadedImageStyle();
	imageStyle.alphaBlend = false;

	auto curSave = param.GetFileInfo(currentSelectedSave);

	if (curSave.size == 0 && checkExists)
		imageStyle.color = CalcFadedColor(0xFF777777);

	// Calc save image position on screen
	float w = 144;
	float h = 80;
	float x = 27;
	float y = 97;

	int tw = 256;
	int th = 256;
	if (curSave.texture != nullptr && curSave.texture->IsValid()) {
		curSave.texture->SetTexture();
		tw = curSave.texture->Width();
		th = curSave.texture->Height();
		float scale = (float)h / (float)th;
		int scaledW = (int)(tw * scale);
		x += (w - scaledW) / 2;
		w = scaledW;
	} else {
		PPGeDisableTexture();
	}
	PPGeDrawImage(x, y, w, h, 0, 0, 1, 1, tw, th, imageStyle);
	PPGeSetDefaultTexture();
}

static void FormatSaveHourMin(char *hour_time, size_t sz, const tm &t) {
	const char *am_pm = "AM";
	int hour = t.tm_hour;
	switch (g_Config.iTimeFormat) {
	case 1:
		if (hour == 12) {
			am_pm = "PM";
		} else if (hour > 12) {
			am_pm = "PM";
			hour -= 12;
		} else if (hour == 0) {
			hour = 12;
		}
		snprintf(hour_time, sz, "%02d:%02d %s", hour, t.tm_min, am_pm);
		break;
	case 0:
	default:
		snprintf(hour_time, sz, "%02d:%02d", hour, t.tm_min);
		break;
	}
}

static void FormatSaveDate(char *date, size_t sz, const tm &t) {
	int year = t.tm_year + 1900;
	int month = t.tm_mon + 1;
	switch (g_Config.iDateFormat) {
	case 1:
		snprintf(date, sz, "%02d/%02d/%04d", month, t.tm_mday, year);
		break;
	case 2:
		snprintf(date, sz, "%02d/%02d/%04d", t.tm_mday, month, year);
		break;
	case 0:
	default:
		snprintf(date, sz, "%04d/%02d/%02d", year, month, t.tm_mday);
		break;
	}
}

void PSPSaveDialog::DisplaySaveDataInfo1() {
	std::lock_guard<std::mutex> guard(paramLock);
	const SaveFileInfo &saveInfo = param.GetFileInfo(currentSelectedSave);
	PPGeStyle saveTitleStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.55f);

	if (saveInfo.broken) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
		PPGeDrawText(di->T("Corrupted Data"), 180, 136, textStyle);
		PPGeDrawText(saveInfo.title, 175, 159, saveTitleStyle);
	} else if (saveInfo.size == 0) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
		PPGeDrawText(di->T("NEW DATA"), 180, 136, textStyle);
	} else {
		char hour_time[32];
		FormatSaveHourMin(hour_time, sizeof(hour_time), saveInfo.modif_time);

		char date_year[32];
		FormatSaveDate(date_year, sizeof(date_year), saveInfo.modif_time);

		s64 sizeK = saveInfo.size / 1024;

		PPGeDrawRect(180, 136, 480, 137, CalcFadedColor(0xFFFFFFFF));
		std::string titleTxt = saveInfo.title;
		std::string timeTxt = StringFromFormat("%s   %s  %lld KB", date_year, hour_time, sizeK);
		std::string saveTitleTxt = saveInfo.saveTitle;
		std::string saveDetailTxt = saveInfo.saveDetail;

		PPGeStyle titleStyle = FadedStyle(PPGeAlign::BOX_BOTTOM, 0.6f);
		titleStyle.color = CalcFadedColor(0xFFC0C0C0);
		PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.5f);

		PPGeDrawText(titleTxt, 180, 136, titleStyle);
		PPGeDrawText(timeTxt, 180, 137, textStyle);
		PPGeDrawText(saveTitleTxt, 175, 159, saveTitleStyle);
		PPGeDrawTextWrapped(saveDetailTxt, 175, 181, 480 - 175, 250 - 181, textStyle);
	}
}

void PSPSaveDialog::DisplaySaveDataInfo2(bool showNewData) {
	std::lock_guard<std::mutex> guard(paramLock);

	tm modif_time;
	const char *save_title;
	u32 data_size;

	if (showNewData || param.GetFileInfo(currentSelectedSave).size == 0) {
		time_t t;
		time(&t);
		localtime_r(&t, &modif_time);
		save_title = param.GetPspParam()->sfoParam.savedataTitle;
		// TODO: Account for icon, etc., etc.
		data_size = param.GetPspParam()->dataSize;
	} else {
		modif_time = param.GetFileInfo(currentSelectedSave).modif_time;
		save_title = param.GetFileInfo(currentSelectedSave).saveTitle;
		data_size = param.GetFileInfo(currentSelectedSave).size;
	}

	char hour_time[32];
	FormatSaveHourMin(hour_time, sizeof(hour_time), modif_time);

	char date_year[32];
	FormatSaveDate(date_year, sizeof(date_year), modif_time);

	s64 sizeK = data_size / 1024;

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.5f);
	std::string title = SanitizeUTF8(std::string(save_title, strnlen(save_title, 128)));
	std::string saveinfoTxt = StringFromFormat("%s\n%s  %s\n%lld KB", title.c_str(), date_year, hour_time, sizeK);
	PPGeDrawText(saveinfoTxt.c_str(), 8, 200, textStyle);
}

void PSPSaveDialog::DisplayMessage(std::string_view text, bool hasYesNo)
{
	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_CENTER, FONT_SCALE);

	const float WRAP_WIDTH = 254.0f;
	float y = 136.0f, h;
	PPGeMeasureText(nullptr, &h, text, FONT_SCALE, PPGE_LINE_WRAP_WORD, WRAP_WIDTH);
	float h2 = h / 2.0f;
	if (hasYesNo)
	{
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string_view choiceText;
		float x, w;
		if (yesnoChoice == 1) {
			choiceText = di->T("Yes");
			x = 302.0f;
		}
		else {
			choiceText = di->T("No");
			x = 366.0f;
		}
		PPGeMeasureText(&w, &h, choiceText, FONT_SCALE);
		w = w / 2.0f + 5.5f;
		h /= 2.0f;
		float y2 = y + h2 + 4.0f;
		h2 += h + 4.0f;
		y = 132.0f - h;
		PPGeDrawRect(x - w, y2 - h, x + w, y2 + h, CalcFadedColor(0x40C0C0C0));
		PPGeDrawText(di->T("Yes"), 302.0f, y2, textStyle);
		PPGeDrawText(di->T("No"), 366.0f, y2, textStyle);
		if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0) {
			yesnoChoice = 1;
		}
		else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1) {
			yesnoChoice = 0;
		}
	}
	PPGeDrawTextWrapped(text, 334.0f, y, WRAP_WIDTH, 0, textStyle);
	float sy = 122.0f - h2, ey = 150.0f + h2;
	PPGeDrawRect(202.0f, sy, 466.0f, sy + 1.0f, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawRect(202.0f, ey, 466.0f, ey + 1.0f, CalcFadedColor(0xFFFFFFFF));
}

int PSPSaveDialog::Update(int animSpeed) {
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
	int size = std::min((u32)sizeof(originalRequest), Memory::ReadUnchecked_U32(requestAddr));
	const u8 *updatedRequest = Memory::GetPointerRangeOrException(requestAddr, size);
	if (updatedRequest && memcmp(updatedRequest, &originalRequest, size) != 0) {
		memset(&request, 0, sizeof(request));
		Memory::Memcpy(&request, requestAddr, size);
		Memory::Memcpy(&originalRequest, requestAddr, size);
		std::lock_guard<std::mutex> guard(paramLock);
		param.SetPspParam(&request);
	}

	param.ClearSFOCache();
	UpdateButtons();
	UpdateFade(animSpeed);

	UpdateCommon();

	auto di = GetI18NCategory(I18NCat::DIALOG);

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

			DisplaySaveIcon(false);
			DisplaySaveDataInfo2(true);

			DisplayMessage(di->T("Confirm Save", "Do you want to save this data?"), true);

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

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("Confirm Overwrite","Do you want to overwrite the data?"), true);

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
			// The dialog keeps drawing while the IO runs, and takes the results once it's done.
			FinishIO(false);

			StartDraw();

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2(true);

			DisplayMessage(di->T("Saving","Saving\nPlease Wait..."));

			DisplayBanner(DB_SAVE);

			EndDraw();
		break;
		case DS_SAVE_FAILED:
			StartDraw();

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2(true);

			DisplayMessage(di->T("SavingFailed", "Unable to save data."));

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
			StartDraw();

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("Save completed"));

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

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("ConfirmLoad", "Load this data?"), true);

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
			// The dialog keeps drawing while the IO runs, and takes the results once it's done.
			FinishIO(false);

			StartDraw();

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("Loading","Loading\nPlease Wait..."));

			DisplayBanner(DB_LOAD);

			EndDraw();
		break;
		case DS_LOAD_FAILED:
			StartDraw();

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("LoadingFailed", "Load failed\nThe data is corrupted."));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_LOAD);

			if (IsButtonPressed(cancelButtonFlag)) {
				// Go back to the list so they can try again.
				if (param.GetPspParam()->mode != SCE_UTILITY_SAVEDATA_TYPE_LOAD) {
					display = DS_LOAD_LIST_CHOICE;
				} else {
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_LOAD_DONE:
			StartDraw();
			
			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("Load completed"));

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

			DisplayMessage(di->T("There is no data"));

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

			DisplaySaveIcon(true);
			DisplaySaveDataInfo2();

			DisplayMessage(di->T("DeleteConfirm", 
						"This save data will be deleted.\nAre you sure you want to continue?"), 
						true);

			DisplayButtons(DS_BUTTON_OK | DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag) || (IsButtonPressed(okButtonFlag) && yesnoChoice == 0)) {
				if (param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE || param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE)
					display = DS_DELETE_LIST_CHOICE;
				else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_CANCEL;
					StartFade(false);
				}
			} else if (IsButtonPressed(okButtonFlag)) {
				display = DS_DELETE_DELETING;
				StartIOThread();
			}

			EndDraw();
		break;
		case DS_DELETE_DELETING:
			// The dialog keeps drawing while the IO runs, and takes the results once it's done.
			FinishIO(false);

			StartDraw();

			DisplayMessage(di->T("Deleting","Deleting\nPlease Wait..."));

			DisplayBanner(DB_DELETE);

			EndDraw();
		break;
		case DS_DELETE_FAILED:
			StartDraw();

			DisplayMessage(di->T("DeleteFailed", "Unable to delete data."));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				if (param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE || param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE)
					display = DS_DELETE_LIST_CHOICE;
				else
					StartFade(false);
			}

			EndDraw();
		break;
		case DS_DELETE_DONE:
			StartDraw();
			
			DisplayMessage(di->T("Delete completed"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				if (param.GetFilenameCount() == 0)
					display = DS_DELETE_NODATA;
				else if (param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE || param.GetPspParam()->mode == SCE_UTILITY_SAVEDATA_TYPE_LISTALLDELETE) {
					if (currentSelectedSave > param.GetFilenameCount() - 1)
						currentSelectedSave = param.GetFilenameCount() - 1;
					display = DS_DELETE_LIST_CHOICE;
				} else {
					param.GetPspParam()->common.result = SCE_UTILITY_DIALOG_RESULT_SUCCESS;
					StartFade(false);
				}
			}

			EndDraw();
		break;
		case DS_DELETE_NODATA:
			StartDraw();
			
			DisplayMessage(di->T("There is no data"));

			DisplayButtons(DS_BUTTON_CANCEL);
			DisplayBanner(DB_DELETE);

			if (IsButtonPressed(cancelButtonFlag)) {
				param.GetPspParam()->common.result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
				StartFade(false);
			}

			EndDraw();
		break;

		case DS_NONE: // For action which display nothing
			if (ioThreadStatus == SAVEIO_NONE) {
				StartIOThread();
			} else if (FinishIO(g_Config.iIOTimingMethod != IOTIMING_HOST)) {
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
			}
		break;

		default:
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
		break;
	}

	if (ReadStatus() == SCE_UTILITY_STATUS_FINISHED || pendingStatus == SCE_UTILITY_STATUS_FINISHED)
		Memory::Memcpy(requestAddr, &request, request.common.size, "SaveDialogParam");
	param.ClearSFOCache();
	
	return 0;
}

// Runs on the IO thread. Of the dialog, it only touches the io* members (see StartIOThread and FinishIO).
void PSPSaveDialog::ExecuteIOAction() {
	ioParam_.ClearSFOCache();
	auto &result = ioRequest_.common.result;
	ioDisplay_ = ioAction_;
	switch (ioAction_) {
	case DS_LOAD_LOADING:
		result = ioParam_.Load(&ioRequest_, ioSaveDirName_, ioSaveId_);
		ioDisplay_ = result == 0 ? DS_LOAD_DONE : DS_LOAD_FAILED;
		break;
	case DS_SAVE_SAVING:
		ioDisplay_ = ioParam_.Save(&ioRequest_, ioSaveDirName_) == 0 ? DS_SAVE_DONE : DS_SAVE_FAILED;
		break;
	case DS_DELETE_DELETING:
		if (ioParam_.Delete(&ioRequest_, ioDeleteDir_)) {
			result = 0;
			ioDisplay_ = DS_DELETE_DONE;
		} else {
			//result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;// What the result should be?
			ioDisplay_ = DS_DELETE_FAILED;
		}
		break;
	case DS_NONE:
		ExecuteNotVisibleIOAction();
		break;

	default:
		// Nothing to do here.
		break;
	}

	ioParam_.ClearSFOCache();
	ioThreadStatus = SAVEIO_READY;
}

void PSPSaveDialog::ExecuteNotVisibleIOAction() {
	auto &result = ioRequest_.common.result;

	SceUtilitySavedataType utilityMode = (SceUtilitySavedataType)(u32)ioRequest_.mode;
	switch (utilityMode) {
	case SCE_UTILITY_SAVEDATA_TYPE_LOAD: // Only load and exit
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
		result = ioParam_.Load(&ioRequest_, ioSaveDirName_, ioSaveId_);
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_SAVE: // Only save and exit
	case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
		result = ioParam_.Save(&ioRequest_, ioSaveDirName_);
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_SIZES:
		result = ioParam_.GetSizes(&ioRequest_);
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_LIST:
		ioParam_.GetList(&ioRequest_);
		result = 0;
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_FILES:
		result = ioParam_.GetFilesList(&ioRequest_, requestAddr, ioListSaveDirName_);
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_GETSIZE:
		{
			bool sizeResult = ioParam_.GetSize(&ioRequest_);
			// TODO: According to JPCSP, should test/verify this part but seems edge casey.
			if (MemoryStick_State() != PSP_MEMORYSTICK_STATE_INSERTED) {
				result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK;
			} else if (sizeResult) {
				result = 0;
			} else {
				result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
			}
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA:
		DEBUG_LOG(Log::sceUtility, "sceUtilitySavedata DELETEDATA: %s", ioRequest_.saveName);
		if (ioParam_.Delete(&ioRequest_, ioDeleteDir_)) {
			result = 0;
		} else {
			result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE:
	case SCE_UTILITY_SAVEDATA_TYPE_DELETE:
		if (ioParam_.Delete(&ioRequest_, ioDeleteDir_)) {
			result = 0;
		} else {
			result = SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA;
		}
		break;
	// TODO: Should reset the directory's other files.
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
		result = ioParam_.Save(&ioRequest_, ioSaveDirName_, ioRequest_.mode == SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE);
		if (result == SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE) {
			result = SCE_UTILITY_SAVEDATA_ERROR_RW_MEMSTICK_FULL;
		}
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
		result = ioParam_.Save(&ioRequest_, ioSaveDirName_, ioRequest_.mode == SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE);
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
	case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
		result = ioParam_.Load(&ioRequest_, ioSaveDirName_, ioSaveId_, ioRequest_.mode == SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE);
		if (result == SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN)
			result = SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN;
		if (result == SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA)
			result = SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
		break;
	case SCE_UTILITY_SAVEDATA_TYPE_ERASE:
	case SCE_UTILITY_SAVEDATA_TYPE_ERASESECURE:
		result = ioParam_.DeleteData(&ioRequest_);
		break;
	default:
		break;
	}
}

void PSPSaveDialog::StartIOThread() {
	if (ioThread.joinable()) {
		WARN_LOG_REPORT(Log::sceUtility, "Starting a save io thread when one already pending, uh oh.");
		ioThread.join();
	}

	// Show save indicator. It's strange how "display" is just as much an action as what to display.
	if (display == DS_SAVE_SAVING || display == DS_LOAD_LOADING) {
		const bool save = display != DS_LOAD_LOADING;
		ResetSecondsSinceLastGameSave();
		ShowSaveLoadIndicator(save);
	}

	// Everything the IO thread needs from the dialog, taken now: it doesn't look at the dialog's state.
	const SceUtilitySavedataType mode = (SceUtilitySavedataType)(u32)request.mode;
	ioAction_ = display;
	ioRequest_ = request;
	ioRequestStart_ = request;
	ioSaveId_ = currentSelectedSave;
	ioSaveDirName_ = GetSelectedSaveDirName();
	ioListSaveDirName_.clear();
	if (display == DS_NONE && mode == SCE_UTILITY_SAVEDATA_TYPE_FILES) {
		ioListSaveDirName_ = param.GetSaveDirName(&request, 0);
	}
	ioDeleteDir_.clear();
	if (display == DS_DELETE_DELETING) {
		ioDeleteDir_ = param.GetSaveDir(currentSelectedSave);
	} else if (display == DS_NONE && (mode == SCE_UTILITY_SAVEDATA_TYPE_DELETEDATA || mode == SCE_UTILITY_SAVEDATA_TYPE_AUTODELETE || mode == SCE_UTILITY_SAVEDATA_TYPE_DELETE)) {
		ioDeleteDir_ = param.GetSaveDir(param.GetSelectedSave());
	}

	ioThreadStatus = SAVEIO_PENDING;
	ioThread = std::thread([this]() {
		SetCurrentThreadName("SaveIO");

		AndroidJNIThreadContext jniContext;
		this->ExecuteIOAction();
	});
}

// Takes the IO thread's results back. Without wait, only if it's done: returns whether it was.
bool PSPSaveDialog::FinishIO(bool wait) {
	if (ioThreadStatus == SAVEIO_PENDING && !wait) {
		return false;
	}
	if (ioThread.joinable()) {
		ioThread.join();
	}
	if (ioThreadStatus != SAVEIO_READY) {
		return true;
	}

	{
		// Only what the IO changed: the game may have changed its request meanwhile (Update reloads it).
		std::lock_guard<std::mutex> guard(paramLock);
		const u8 *before = (const u8 *)&ioRequestStart_;
		const u8 *after = (const u8 *)&ioRequest_;
		u8 *live = (u8 *)&request;
		for (size_t i = 0; i < sizeof(request); ++i) {
			if (after[i] != before[i]) {
				live[i] = after[i];
			}
		}
	}

	switch (ioAction_) {
	case DS_LOAD_LOADING:
		if (ioDisplay_ == DS_LOAD_DONE) {
			g_lastSaveTime = time_now_d();
		}
		break;
	case DS_SAVE_SAVING:
		SaveState::NotifySaveData();
		if (ioDisplay_ == DS_SAVE_DONE) {
			g_lastSaveTime = time_now_d();
		}
		break;
	case DS_NONE:
		switch ((SceUtilitySavedataType)(u32)request.mode) {
		case SCE_UTILITY_SAVEDATA_TYPE_LOAD:
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_READDATASECURE:
			ResetSecondsSinceLastGameSave();
			ShowSaveLoadIndicator(false);
			break;
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_MAKEDATASECURE:
			if (request.common.result == SCE_UTILITY_SAVEDATA_ERROR_RW_MEMSTICK_FULL) {
				break;
			}
			[[fallthrough]];
		case SCE_UTILITY_SAVEDATA_TYPE_SAVE:
		case SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATA:
		case SCE_UTILITY_SAVEDATA_TYPE_WRITEDATASECURE:
			SaveState::NotifySaveData();
			ResetSecondsSinceLastGameSave();
			ShowSaveLoadIndicator(true);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (ioAction_ != DS_NONE) {
		display = ioDisplay_;
		// Show what was saved or deleted in the list.
		if (display == DS_SAVE_DONE || display == DS_DELETE_DONE) {
			std::lock_guard<std::mutex> guard(paramLock);
			param.SetPspParam(param.GetPspParam());
		}
	}

	ioThreadStatus = SAVEIO_DONE;
	return true;
}

void PSPSaveDialog::WaitForIO() {
	if (ioThread.joinable()) {
		ioThread.join();
	}
}

int PSPSaveDialog::Shutdown(bool force) {
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	if (ioThread.joinable()) {
		ioThread.join();
	}
	ioThreadStatus = SAVEIO_NONE;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(SAVEDATA_SHUTDOWN_DELAY_US);
	}
	param.SetPspParam(0);
	param.ClearSFOCache();

	return 0;
}

// Version 4 of the dialog's state (never in a release) kept the IO's writes to PSP memory until the
// results were taken. Late is better than never.
static void DoStateOldPendingWrites(PointerWrap &p) {
	auto s = p.Section("SavedataMemory", 1);
	if (!s) {
		return;
	}
	u32 count = 0;
	Do(p, count);
	for (u32 i = 0; i < count; ++i) {
		u32 addr = 0;
		std::string tag;
		std::vector<u8> data;
		Do(p, addr);
		Do(p, tag);
		Do(p, data);
		if (p.mode == PointerWrap::MODE_READ && Memory::IsValidRange(addr, (u32)data.size())) {
			Memory::Memcpy(addr, data.data(), (u32)data.size(), tag.c_str(), tag.size());
		}
	}
}

void PSPSaveDialog::DoState(PointerWrap &p) {
	if (ioThread.joinable()) {
		ioThread.join();
	}
	PSPDialog::DoState(p);

	// Version 3 activates the s > 2 branch below, so ioThreadStatus survives
	// a savestate. Safe to restore: the IO thread was joined above, so the
	// value is never SAVEIO_PENDING. Without this, loading a state taken
	// while a savedata operation was in flight would restart the operation
	// instead of resuming from its recorded status. Version 4 keeps the
	// results of a finished operation that haven't been taken yet.
	auto s = p.Section("PSPSaveDialog", 1, 5);
	if (!s) {
		return;
	}

	Do(p, display);
	param.DoState(p);
	Do(p, request);
	// Just reset it.
	bool hasParam = param.GetPspParam() != NULL;
	Do(p, hasParam);
	if (hasParam && p.mode == p.MODE_READ) {
		param.SetPspParam(&request);
	}
	Do(p, requestAddr);
	Do(p, currentSelectedSave);
	Do(p, yesnoChoice);
	SaveIOStatus ioStatus = ioThreadStatus;
	if (s > 2) {
		Do(p, ioStatus);
	} else {
		ioStatus = SAVEIO_NONE;
	}
	ioThreadStatus = ioStatus;
	if (s >= 4) {
		Do(p, ioAction_);
		Do(p, ioDisplay_);
		Do(p, ioRequest_);
		Do(p, ioRequestStart_);
	}
	if (s == 4) {
		DoStateOldPendingWrites(p);
	}
}

pspUtilityDialogCommon *PSPSaveDialog::GetCommonParam() {
	return &param.GetPspParam()->common;
}
