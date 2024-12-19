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
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/Dialog/PSPMsgDialog.h"
#include "Core/Dialog/PSPSaveDialog.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"

static const float FONT_SCALE = 0.65f;

// These are rough, it seems to take a long time to init, and probably depends on threads.
// TODO: This takes like 700ms on a PSP but that's annoyingly long.
const static int MSG_INIT_DELAY_US = 300000;
const static int MSG_SHUTDOWN_DELAY_US = 26000;

PSPMsgDialog::PSPMsgDialog(UtilityDialogType type) : PSPDialog(type) {
}

PSPMsgDialog::~PSPMsgDialog() {
}

int PSPMsgDialog::Init(unsigned int paramAddr) {
	// Ignore if already running
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		ERROR_LOG_REPORT(Log::sceUtility, "sceUtilityMsgDialogInitStart: invalid status");
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	messageDialogAddr = paramAddr;
	if (!Memory::IsValidAddress(messageDialogAddr))
	{
		return 0;
	}
	int size = Memory::Read_U32(paramAddr);
	memset(&messageDialog,0,sizeof(messageDialog));
	// Only copy the right size to support different request format
	Memory::Memcpy(&messageDialog,paramAddr,size);

	// debug info
	int optionsNotCoded = messageDialog.options & ~SCE_UTILITY_MSGDIALOG_OPTION_SUPPORTED;
	if(optionsNotCoded)
	{
		ERROR_LOG_REPORT(Log::sceUtility, "PSPMsgDialog options not coded : 0x%08x", optionsNotCoded);
	}

	flag = 0;
	scrollPos_ = 0.0f;
	framesUpHeld_ = 0;
	framesDownHeld_ = 0;

	// Check request invalidity
	if(messageDialog.type == 0 && !(messageDialog.errorNum & 0x80000000))
	{
		flag |= DS_ERROR;
		messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_ERRORCODEINVALID;
	}
	else if(size == SCE_UTILITY_MSGDIALOG_SIZE_V2 && messageDialog.type == 1)
	{
		unsigned int validOp = SCE_UTILITY_MSGDIALOG_OPTION_TEXTSOUND |
				SCE_UTILITY_MSGDIALOG_OPTION_YESNO |
				SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO;
		if (((messageDialog.options | validOp) ^ validOp) != 0)
		{
			flag |= DS_ERROR;
			messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION;
		}
	}
	else if(size == SCE_UTILITY_MSGDIALOG_SIZE_V3)
	{
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO) &&
				!(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_YESNO))
		{
			flag |= DS_ERROR;
			messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION;
		}
		if (messageDialog.options & ~SCE_UTILITY_MSGDIALOG_OPTION_SUPPORTED)
		{
			flag |= DS_ERROR;
			messageDialog.result = SCE_UTILITY_MSGDIALOG_ERROR_BADOPTION;
		}
	}

	if(flag == 0)
	{
		yesnoChoice = 1;
		if(messageDialog.type == 1)
			flag |= DS_MSG;
		if(messageDialog.type == 0)
			flag |= DS_ERRORMSG;
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_YESNO) &&
				((size == SCE_UTILITY_MSGDIALOG_SIZE_V3) ||
						(size == SCE_UTILITY_MSGDIALOG_SIZE_V2 && messageDialog.type == 1)))
			flag |= DS_YESNO;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO)
		{
			yesnoChoice = 0;
			flag |= DS_DEFNO;
		}
		if((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_OK) && (size == SCE_UTILITY_MSGDIALOG_SIZE_V3))
		{
			yesnoChoice = 1;
			flag |= DS_OK;
		}
		if((flag & DS_YESNO) || (flag & DS_OK))
			flag |= DS_VALIDBUTTON;
		if(!((messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_NOCANCEL)  && (size == SCE_UTILITY_MSGDIALOG_SIZE_V3)))
			flag |= DS_CANCELBUTTON;
		if(messageDialog.options & SCE_UTILITY_MSGDIALOG_OPTION_NOSOUND)
			flag |= DS_NOSOUND;
	}

	if (flag & DS_ERRORMSG) {
		FormatErrorCode(messageDialog.errorNum);
	} else {
		truncate_cpy(msgText, messageDialog.string);
	}

	ChangeStatusInit(MSG_INIT_DELAY_US);

	UpdateButtons();
	InitCommon();
	StartFade(true);
	return 0;
}


void PSPMsgDialog::FormatErrorCode(uint32_t code) {
	auto err = GetI18NCategory(I18NCat::DIALOG);

	switch (code) {
	case SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN:
		snprintf(msgText, 512, "%s (%08x)", err->T_cstr("MsgErrorSavedataDataBroken", "Save data was corrupt."), code);
		break;

	case SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_MS:
	case SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK:
	case SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_MS:
	case SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_MS:
	case SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_MS:
		snprintf(msgText, 512, "%s (%08x)", err->T_cstr("MsgErrorSavedataNoMS", "Memory stick not inserted."), code);
		break;

	case SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA:
	case SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA:
	case SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA:
	case SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA:
		snprintf(msgText, 512, "%s (%08x)", err->T_cstr("MsgErrorSavedataNoData", "Warning: no save data was found."), code);
		break;

	case SCE_UTILITY_SAVEDATA_ERROR_RW_MEMSTICK_FULL:
	case SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE:
		snprintf(msgText, 512, "%s (%08x)", err->T_cstr("MsgErrorSavedataMSFull", "Memory stick full.  Check your storage space."), code);
		break;

	default:
		snprintf(msgText, 512, "%s %08x", err->T_cstr("MsgErrorCode", "Error code:"), code);
	}
}

void PSPMsgDialog::DisplayMessage(const std::string &text, bool hasYesNo, bool hasOK) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	PPGeStyle buttonStyle = FadedStyle(PPGeAlign::BOX_CENTER, FONT_SCALE);
	PPGeStyle messageStyle = FadedStyle(PPGeAlign::BOX_HCENTER, FONT_SCALE);

	// Without the scrollbar, we have 390 total pixels.
	float WRAP_WIDTH = 340.0f;
	if ((size_t)UTF8StringNonASCIICount(text) >= text.size() / 4) {
		WRAP_WIDTH = 376.0f;
		if (text.size() > 12) {
			messageStyle.scale = 0.6f;
		}
	}

	float totalHeight = 0.0f;
	PPGeMeasureText(nullptr, &totalHeight, text, FONT_SCALE, PPGE_LINE_WRAP_WORD, WRAP_WIDTH);
	// The PSP normally only shows about 8 lines at a time.
	// For improved UX, we intentionally show part of the next line.
	float visibleHeight = std::min(totalHeight, 175.0f);
	float h2 = visibleHeight / 2.0f;

	float centerY = 135.0f;
	float sy = centerY - h2 - 15.0f;
	float ey = centerY + h2 + 20.0f;
	float buttonY = centerY + h2 + 5.0f;

	auto drawSelectionBoxAndAdjust = [&](float x) {
		// Box has a fixed size.
		float w = 15.0f;
		float h = 8.0f;
		PPGeDrawRect(x - w, buttonY - h, x + w, buttonY + h, CalcFadedColor(0x6DCFCFCF));

		centerY -= h + 5.0f;
		sy -= h + 5.0f;
		ey = buttonY + h * 2.0f + 5.0f;
	};

	if (hasYesNo) {
		if (yesnoChoice == 1) {
			drawSelectionBoxAndAdjust(204.0f);
		} else {
			drawSelectionBoxAndAdjust(273.0f);
		}

		PPGeDrawText(di->T("Yes"), 203.0f, buttonY - 1.0f, buttonStyle);
		PPGeDrawText(di->T("No"), 272.0f, buttonY - 1.0f, buttonStyle);
		if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0) {
			yesnoChoice = 1;
		} else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1) {
			yesnoChoice = 0;
		}
		buttonY += 8.0f + 5.0f;
	} 
	
	if (hasOK) {
		drawSelectionBoxAndAdjust(240.0f);

		PPGeDrawText(di->T("OK"), 239.0f, buttonY - 1.0f, buttonStyle);
		buttonY += 8.0f + 5.0f;
	}

	PPGeScissor(0, (int)(centerY - h2 - 2), 480, (int)(centerY + h2 + 2));
	PPGeDrawTextWrapped(text.c_str(), 240.0f, centerY - h2 - scrollPos_, WRAP_WIDTH, 0, messageStyle);
	PPGeScissorReset();

	// Do we need a scrollbar?
	if (visibleHeight < totalHeight) {
		float scrollSpeed = 5.0f;
		float scrollMax = totalHeight - visibleHeight;

		float bobHeight = (visibleHeight / totalHeight) * visibleHeight;
		float bobOffset = (scrollPos_ / scrollMax) * (visibleHeight - bobHeight);
		float bobY1 = centerY - h2 + bobOffset;
		PPGeDrawRect(435.0f, bobY1, 440.0f, bobY1 + bobHeight, CalcFadedColor(0xFFCCCCCC));

		auto buttonDown = [this](int btn, int &held) {
			if (IsButtonPressed(btn)) {
				held = 0;
				return true;
			}
			return IsButtonHeld(btn, held, 1, 1);
		};
		if (buttonDown(CTRL_DOWN, framesDownHeld_) && scrollPos_ < scrollMax) {
			scrollPos_ = std::min(scrollMax, scrollPos_ + scrollSpeed);
		}
		if (buttonDown(CTRL_UP, framesUpHeld_) && scrollPos_ > 0.0f) {
			scrollPos_ = std::max(0.0f, scrollPos_ - scrollSpeed);
		}
	}

	PPGeDrawRect(40.0f, sy, 440.0f, sy + 1.0f, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawRect(40.0f, ey, 440.0f, ey + 1.0f, CalcFadedColor(0xFFFFFFFF));
}

int PSPMsgDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	if (flag & (DS_ERROR | DS_ABORT)) {
		ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
	} else {
		UpdateButtons();
		UpdateCommon();
		UpdateFade(animSpeed);

		StartDraw();
		// white -> RGB(168,173,189), black -> RGB(129,134,150)
		// (255 - a) + (x * a / 255) = 173,  x * a / 255 = 134
		// a = 255 - w + b = 158, x = b * 255 / a = ?
		// but is not drawn using x * a + y * (255 - a) here?
		//PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x9EF2D8D0));
		PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));

		if ((flag & DS_MSG) || (flag & DS_ERRORMSG))
			DisplayMessage(msgText, (flag & DS_YESNO) != 0, (flag & DS_OK) != 0);

		if (flag & (DS_OK | DS_VALIDBUTTON)) 
			DisplayButtons(DS_BUTTON_OK, messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V3 ? messageDialog.okayButton : "");

		if (flag & DS_CANCELBUTTON)
			DisplayButtons(DS_BUTTON_CANCEL, messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V3 ? messageDialog.cancelButton : "");

		if (IsButtonPressed(cancelButtonFlag) && (flag & DS_CANCELBUTTON))
		{
			if(messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V3 ||
					((messageDialog.common.size == SCE_UTILITY_MSGDIALOG_SIZE_V2) && (flag & DS_YESNO)))
				messageDialog.buttonPressed = 3;
			else
				messageDialog.buttonPressed = 0;
			StartFade(false);
		}
		else if (IsButtonPressed(okButtonFlag) && (flag & DS_VALIDBUTTON))
		{
			if (yesnoChoice == 0)
			{
				messageDialog.buttonPressed = 2;
			}
			else
			{
				messageDialog.buttonPressed = 1;
			}
			StartFade(false);
		}


		EndDraw();

		messageDialog.result = 0;
	}

	Memory::Memcpy(messageDialogAddr, &messageDialog, messageDialog.common.size, "MsgDialogParam");
	return 0;
}

int PSPMsgDialog::Abort() {
	// Katekyoushi Hitman Reborn! Battle Arena expects this to fail when not running.
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	} else {
		// Status is not actually changed until Update().
		flag |= DS_ABORT;
		return 0;
	}
}

int PSPMsgDialog::Shutdown(bool force) {
	if (GetStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(MSG_SHUTDOWN_DELAY_US);
	}

	return 0;
}

void PSPMsgDialog::DoState(PointerWrap &p)
{
	PSPDialog::DoState(p);

	auto s = p.Section("PSPMsgDialog", 1);
	if (!s)
		return;

	Do(p, flag);
	Do(p, messageDialog);
	Do(p, messageDialogAddr);
	DoArray(p, msgText, sizeof(msgText));
	Do(p, yesnoChoice);

	// We don't save state this, you'll just have to scroll down again.
	if (p.mode == p.MODE_READ) {
		scrollPos_ = 0.0f;
		framesUpHeld_ = 0;
		framesDownHeld_ = 0;
	}
}

pspUtilityDialogCommon *PSPMsgDialog::GetCommonParam()
{
	return &messageDialog.common;
}
