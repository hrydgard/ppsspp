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

#include "Common/ChunkFile.h"
#include "Common/StringUtils.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/Dialog/PSPDialog.h"

#define FADE_TIME 1.0
const float FONT_SCALE = 0.55f;

PSPDialog::PSPDialog()
	: status(SCE_UTILITY_STATUS_NONE), pendingStatus(SCE_UTILITY_STATUS_NONE),
	  pendingStatusTicks(0), lastButtons(0), buttons(0)
{

}

PSPDialog::~PSPDialog() {
}

PSPDialog::DialogStatus PSPDialog::GetStatus()
{
	if (pendingStatusTicks != 0 && CoreTiming::GetTicks() >= pendingStatusTicks) {
		status = pendingStatus;
		pendingStatusTicks = 0;
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
		status = newStatus;
		pendingStatusTicks = 0;
	} else {
		pendingStatus = newStatus;
		pendingStatusTicks = CoreTiming::GetTicks() + usToCycles(delayUs);
	}
}

void PSPDialog::ChangeStatusInit(int delayUs) {
	status = SCE_UTILITY_STATUS_INITIALIZE;
	ChangeStatus(SCE_UTILITY_STATUS_RUNNING, delayUs);
}

void PSPDialog::ChangeStatusShutdown(int delayUs) {
	status = SCE_UTILITY_STATUS_SHUTDOWN;
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

u32 PSPDialog::CalcFadedColor(u32 inColor)
{
	u32 alpha = inColor >> 24;
	alpha = alpha * fadeValue / 255;
	return (inColor & 0x00FFFFFF) | (alpha << 24);
}

void PSPDialog::DoState(PointerWrap &p)
{
	auto s = p.Section("PSPDialog", 1, 2);
	if (!s)
		return;

	p.Do(status);
	p.Do(lastButtons);
	p.Do(buttons);
	p.Do(fadeTimer);
	p.Do(isFading);
	p.Do(fadeIn);
	p.Do(fadeValue);
	p.Do(okButtonImg);
	p.Do(cancelButtonImg);
	p.Do(okButtonFlag);
	p.Do(cancelButtonFlag);

	if (s >= 2) {
		p.Do(pendingStatus);
		p.Do(pendingStatusTicks);
	} else {
		pendingStatusTicks = 0;
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

void PSPDialog::DisplayButtons(int flags, const char *caption)
{
	bool useCaption = false;
	char safeCaption[65] = {0};
	if (caption != NULL && *caption != '\0') {
		useCaption = true;
		truncate_cpy(safeCaption, caption);
	}

	I18NCategory *di = GetI18NCategory("Dialog");
	float x1 = 183.5f, x2 = 261.5f;
	if (GetCommonParam()->buttonSwap == 1) {
		x1 = 261.5f;
		x2 = 183.5f;
	}
	if (flags & DS_BUTTON_OK) {
		const char *text = useCaption ? safeCaption : di->T("Enter");
		PPGeDrawImage(okButtonImg, x2, 258, 11.5f, 11.5f, 0, CalcFadedColor(0x80000000));
		PPGeDrawImage(okButtonImg, x2, 256, 11.5f, 11.5f, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(text, x2 + 15.5f, 254, PPGE_ALIGN_LEFT, FONT_SCALE, CalcFadedColor(0x80000000));
		PPGeDrawText(text, x2 + 14.5f, 252, PPGE_ALIGN_LEFT, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
	}
	if (flags & DS_BUTTON_CANCEL) {
		const char *text = useCaption ? safeCaption : di->T("Back");
		PPGeDrawText(text, x1 + 15.5f, 254, PPGE_ALIGN_LEFT, FONT_SCALE, CalcFadedColor(0x80000000));
		PPGeDrawText(text, x1 + 14.5f, 252, PPGE_ALIGN_LEFT, FONT_SCALE, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawImage(cancelButtonImg, x1, 258, 11.5f, 11.5f, 0, CalcFadedColor(0x80000000));
		PPGeDrawImage(cancelButtonImg, x1, 256, 11.5f, 11.5f, 0, CalcFadedColor(0xFFFFFFFF));
	}
}
