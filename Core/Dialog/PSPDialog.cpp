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

#include "Common/Data/Text/I18n.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/Dialog/PSPDialog.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/scePower.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PPGeDraw.h"

#define FADE_TIME 1.0
const float FONT_SCALE = 0.55f;

PSPDialog::PSPDialog(UtilityDialogType type) : dialogType_(type) {
}

PSPDialog::~PSPDialog() {
}

void PSPDialog::InitCommon() {
	UpdateCommon();

	if (GetCommonParam() && GetCommonParam()->language != g_Config.GetPSPLanguage()) {
		WARN_LOG(Log::sceUtility, "Game requested language %d, ignoring and using user language", GetCommonParam()->language);
	}
}

void PSPDialog::UpdateCommon() {
	okButtonImg = ImageID("I_CIRCLE");
	cancelButtonImg = ImageID("I_CROSS");
	okButtonFlag = CTRL_CIRCLE;
	cancelButtonFlag = CTRL_CROSS;
	if (GetCommonParam() && GetCommonParam()->buttonSwap == 1) {
		okButtonImg = ImageID("I_CROSS");
		cancelButtonImg = ImageID("I_CIRCLE");
		okButtonFlag = CTRL_CROSS;
		cancelButtonFlag = CTRL_CIRCLE;
	}
}

PSPDialog::DialogStatus PSPDialog::GetStatus() {
	if (pendingStatusTicks != 0 && CoreTiming::GetTicks() >= pendingStatusTicks) {
		bool changeAllowed = true;
		if (pendingStatus == SCE_UTILITY_STATUS_NONE && status == SCE_UTILITY_STATUS_SHUTDOWN) {
			FinishVolatile();
		} else if (pendingStatus == SCE_UTILITY_STATUS_RUNNING && status == SCE_UTILITY_STATUS_INITIALIZE) {
			if (!volatileLocked_) {
				volatileLocked_ = KernelVolatileMemLock(0, 0, 0) == 0;
				changeAllowed = volatileLocked_;
			}
		}
		if (changeAllowed) {
			status = pendingStatus;
			pendingStatusTicks = 0;
		}
	}

	PSPDialog::DialogStatus retval = status;
	if (UseAutoStatus()) {
		if (status == SCE_UTILITY_STATUS_SHUTDOWN)
			status = SCE_UTILITY_STATUS_NONE;
		if (status == SCE_UTILITY_STATUS_INITIALIZE)
			status = SCE_UTILITY_STATUS_RUNNING;
	}
	return retval;
}

void PSPDialog::ChangeStatus(DialogStatus newStatus, int delayUs) {
	if (delayUs <= 0) {
		if (newStatus == SCE_UTILITY_STATUS_NONE && status == SCE_UTILITY_STATUS_SHUTDOWN) {
			FinishVolatile();
		} else if (newStatus == SCE_UTILITY_STATUS_RUNNING && status == SCE_UTILITY_STATUS_INITIALIZE) {
			if (!volatileLocked_) {
				// TODO: Should probably make the status pending instead?
				volatileLocked_ = KernelVolatileMemLock(0, 0, 0) == 0;
			}
		}
		status = newStatus;
		pendingStatus = newStatus;
		pendingStatusTicks = 0;
	} else {
		pendingStatus = newStatus;
		pendingStatusTicks = CoreTiming::GetTicks() + usToCycles(delayUs);
	}
}

void PSPDialog::FinishVolatile() {
	if (!volatileLocked_)
		return;

	if (KernelVolatileMemUnlock(0) == 0) {
		volatileLocked_ = false;
		// Simulate modifications to volatile memory.
		Memory::Memset(PSP_GetVolatileMemoryStart(), 0, PSP_GetVolatileMemoryEnd() - PSP_GetVolatileMemoryStart());
	}
}

int PSPDialog::FinishInit() {
	if (ReadStatus() != SCE_UTILITY_STATUS_INITIALIZE)
		return -1;
	// The thread already locked.
	volatileLocked_ = true;
	ChangeStatus(SCE_UTILITY_STATUS_RUNNING, 0);
	return 0;
}

int PSPDialog::FinishShutdown() {
	if (ReadStatus() != SCE_UTILITY_STATUS_SHUTDOWN)
		return -1;
	ChangeStatus(SCE_UTILITY_STATUS_NONE, 0);
	return 0;
}

void PSPDialog::ChangeStatusInit(int delayUs) {
	ChangeStatus(SCE_UTILITY_STATUS_INITIALIZE, 0);

	auto params = GetCommonParam();
	if (params)
		UtilityDialogInitialize(DialogType(), delayUs, params->accessThread);
	else
		ChangeStatus(SCE_UTILITY_STATUS_RUNNING, delayUs);
}

void PSPDialog::ChangeStatusShutdown(int delayUs) {
	// If we're doing shutdown right away and skipped start, we don't run the dialog thread.
	bool skipDialogShutdown = status == SCE_UTILITY_STATUS_NONE && pendingStatus == SCE_UTILITY_STATUS_NONE;
	ChangeStatus(SCE_UTILITY_STATUS_SHUTDOWN, 0);

	auto params = GetCommonParam();
	if (params && !skipDialogShutdown)
		UtilityDialogShutdown(DialogType(), delayUs, params->accessThread);
	else
		ChangeStatus(SCE_UTILITY_STATUS_NONE, delayUs);
}

void PSPDialog::StartDraw()
{
	PPGeBegin();
	PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x20000000));
}

void PSPDialog::EndDraw()
{
	PPGeEnd();
}

int PSPDialog::Shutdown(bool force)
{
	if (force) {
		ChangeStatus(SCE_UTILITY_STATUS_NONE, 0);
	} else {
		ChangeStatus(SCE_UTILITY_STATUS_SHUTDOWN, 0);
	}
	return 0;
}

void PSPDialog::StartFade(bool fadeIn_)
{
	isFading = true;
	fadeTimer = 0;
	fadeIn = fadeIn_;
}

void PSPDialog::UpdateFade(int animSpeed) {
	if (isFading) {
		fadeTimer += 1.0f/30.0f * animSpeed; // Probably need a more real value of delta time
		if (fadeTimer < FADE_TIME) {
			if (fadeIn)
				fadeValue = (u32) (fadeTimer / FADE_TIME * 255);
			else
				fadeValue = 255 - (u32) (fadeTimer / FADE_TIME * 255);
		} else {
			fadeValue = (fadeIn ? 255 : 0);
			isFading = false;
			if (!fadeIn) {
				FinishFadeOut();
			}
		}
	}
}

void PSPDialog::FinishFadeOut() {
	ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
}

u32 PSPDialog::CalcFadedColor(u32 inColor) const {
	u32 alpha = inColor >> 24;
	alpha = alpha * fadeValue / 255;
	return (inColor & 0x00FFFFFF) | (alpha << 24);
}

void PSPDialog::DoState(PointerWrap &p) {
	auto s = p.Section("PSPDialog", 1, 3);
	if (!s)
		return;

	Do(p, status);
	Do(p, lastButtons);
	Do(p, buttons);
	Do(p, fadeTimer);
	Do(p, isFading);
	Do(p, fadeIn);
	Do(p, fadeValue);

	// I don't think we should save these two... Let's just ignore them for now for compat.
	int okButtonImg = 0;
	Do(p, okButtonImg);
	int cancelButtonImg = 0;
	Do(p, cancelButtonImg);

	Do(p, okButtonFlag);
	Do(p, cancelButtonFlag);

	if (s >= 2) {
		Do(p, pendingStatus);
		Do(p, pendingStatusTicks);
	} else {
		pendingStatusTicks = 0;
	}

	if (s >= 3) {
		Do(p, volatileLocked_);
	} else {
		volatileLocked_ = false;
	}
}

pspUtilityDialogCommon *PSPDialog::GetCommonParam()
{
	// FIXME
	return 0;
}

void PSPDialog::UpdateButtons()
{
	lastButtons = __CtrlPeekButtons();
	buttons = __CtrlReadLatch();
}

bool PSPDialog::IsButtonPressed(int checkButton)
{
	return !isFading && (buttons & checkButton);
}

bool PSPDialog::IsButtonHeld(int checkButton, int &framesHeld, int framesHeldThreshold, int framesHeldRepeatRate)
{
	bool btnWasHeldLastFrame = (lastButtons & checkButton) && (__CtrlPeekButtons() & checkButton);
	if (!isFading && btnWasHeldLastFrame) {
		framesHeld++;
	}
	else {
		framesHeld = 0;
		return false;
	}

	// It's considered held for dialog purposes after 30 frames (~0.5 seconds),
	// and set to repeat every 10 frames, by default.
	if (framesHeld >= framesHeldThreshold && ((framesHeld % framesHeldRepeatRate) == 0))
		return true;

	return false;
}

PPGeStyle PSPDialog::FadedStyle(PPGeAlign align, float scale) {
	PPGeStyle textStyle;
	textStyle.align = align;
	textStyle.scale = scale;
	textStyle.color = CalcFadedColor(textStyle.color);
	textStyle.hasShadow = true;
	textStyle.shadowColor = CalcFadedColor(textStyle.shadowColor);
	return textStyle;
}

PPGeImageStyle PSPDialog::FadedImageStyle() {
	PPGeImageStyle style;
	style.color = CalcFadedColor(style.color);
	return style;
}

void PSPDialog::DisplayButtons(int flags, std::string_view caption) {
	bool useCaption = false;
	char safeCaption[65] = {0};
	if (!caption.empty()) {
		useCaption = true;
		truncate_cpy(safeCaption, sizeof(safeCaption), caption);
	}

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_LEFT, FONT_SCALE);

	auto di = GetI18NCategory(I18NCat::DIALOG);
	float x1 = 183.5f, x2 = 261.5f;
	if (GetCommonParam()->buttonSwap == 1) {
		x1 = 261.5f;
		x2 = 183.5f;
	}
	if (flags & DS_BUTTON_OK) {
		std::string_view text = useCaption ? safeCaption : di->T("Enter");
		PPGeDrawImage(okButtonImg, x2, 256, 11.5f, 11.5f, textStyle);
		PPGeDrawText(text, x2 + 14.5f, 252, textStyle);
	}
	if (flags & DS_BUTTON_CANCEL) {
		std::string_view text = useCaption ? safeCaption : di->T("Back");
		PPGeDrawImage(cancelButtonImg, x1, 256, 11.5f, 11.5f, textStyle);
		PPGeDrawText(text, x1 + 14.5f, 252, textStyle);
	}
}

int PSPDialog::GetConfirmButton() {
	if (PSP_CoreParameter().compat.flags().ForceCircleButtonConfirm) {
		return CTRL_CIRCLE;
	}
	return g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CROSS : CTRL_CIRCLE;
}

int PSPDialog::GetCancelButton() {
	if (PSP_CoreParameter().compat.flags().ForceCircleButtonConfirm) {
		return CTRL_CROSS;
	}
	return g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CIRCLE : CTRL_CROSS;
}
