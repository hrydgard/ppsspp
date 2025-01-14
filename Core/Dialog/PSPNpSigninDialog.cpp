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
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceNp.h"
#include "Core/Dialog/PSPNpSigninDialog.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Core/Reporting.h"

// Needs testing.
const static int NP_INIT_DELAY_US = 200000; 
const static int NP_SHUTDOWN_DELAY_US = 501000; 
const static int NP_RUNNING_DELAY_US = 1000000; // faked delay to simulate signin process to give chance for players to read the text on the dialog

int PSPNpSigninDialog::Init(u32 paramAddr) {
	// Already running
	if (ReadStatus() != SCE_UTILITY_STATUS_NONE)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	requestAddr = paramAddr;
	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);
	
	WARN_LOG_REPORT_ONCE(PSPNpSigninDialogInit, Log::sceNet, "NpSignin Init Params: %08x, %08x, %08x, %08x", request.npSigninStatus, request.unknown1, request.unknown2, request.unknown3);

	ChangeStatusInit(NP_INIT_DELAY_US);

	// Eat any keys pressed before the dialog inited.
	UpdateButtons();
	InitCommon();

	//npSigninResult = -1;
	startTime = (u64)(time_now_d() * 1000000.0);
	step = 0;

	StartFade(true);
	return 0;
}

void PSPNpSigninDialog::DrawBanner() {
	PPGeDrawRect(0, 0, 480, 22, CalcFadedColor(0x65636358));

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
	textStyle.hasShadow = false;

	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 5, 11.0f, 10.0f, 1, 10, 1, 10, 10, 10, FadedImageStyle());
	auto di = GetI18NCategory(I18NCat::DIALOG);
	PPGeDrawText(di->T("Sign In"), 31, 10, textStyle);
}

void PSPNpSigninDialog::DrawIndicator() {
	// TODO: Draw animated circle as processing indicator
	PPGeDrawImage(456, 248, 20.0f, 20.0f, 1, 10, 1, 10, 10, 10, FadedImageStyle());
}

void PSPNpSigninDialog::DrawLogo() {
	// TODO: Draw OpenDNAS logo
	PPGeDrawImage(416, 22, 64.0f, 64.0f, 1, 10, 1, 10, 64, 64, FadedImageStyle());
}

int PSPNpSigninDialog::Update(int animSpeed) {
	if (ReadStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	UpdateButtons();
	UpdateCommon();
	auto err = GetI18NCategory(I18NCat::ERRORS);
	u64 now = (u64)(time_now_d() * 1000000.0);
	
	if (request.npSigninStatus == NP_SIGNIN_STATUS_NONE) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		UpdateFade(animSpeed);
		StartDraw();

		PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));
		DrawBanner();
		DrawIndicator();

		// TODO: Not sure what should happen here.. may be something like this https://pastebin.com/1eW48zBb ? but we can do test on Open DNAS Server later https://dnas.hashsploit.net/us-gw/
		// DNAS dialog
		if (step >= 2 && now - startTime > NP_RUNNING_DELAY_US) {
			DrawLogo();
			DisplayMessage2(di->T("PleaseWait", "Please wait..."));
			step++;
		}
		// Signin dialog
		else {
			// Skipping the Select Connection screen since we only have 1 fake profile
			DisplayMessage2(di->T("SigninPleaseWait", "Signing in...\nPlease wait."));
		}
		DisplayButtons(DS_BUTTON_CANCEL, di->T("Cancel"));
		
		if (step >= 2 && now - startTime > NP_RUNNING_DELAY_US*2) {
			if (pendingStatus != SCE_UTILITY_STATUS_FINISHED) {
				StartFade(false);
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NP_SHUTDOWN_DELAY_US);
				step++;
			}
		}

		else if (step == 1 && now - startTime > NP_RUNNING_DELAY_US) {
			// Switch to the next message (with DNAS logo)
			StartFade(true);
			step++;
		}

		else if (step == 0) {
			/*if (npAuthResult < 0 && request.NpSigninData.IsValid()) {
				npAuthResult = sceNpAuthCreateStartRequest(request.NpSigninData->paramAddr);
			}*/
			step++;
		}

		if (/*npAuthResult >= 0 &&*/ IsButtonPressed(cancelButtonFlag)) {
			StartFade(false);
			//sceNpAuthAbortRequest(npAuthResult);
			//sceNpAuthDestroyRequest(npAuthResult);
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NP_SHUTDOWN_DELAY_US);
			request.common.result = SCE_UTILITY_DIALOG_RESULT_ABORT;
			request.npSigninStatus = NP_SIGNIN_STATUS_CANCELED;
			//step = 0;
		}

		EndDraw();
	}

	if (ReadStatus() == SCE_UTILITY_STATUS_FINISHED || pendingStatus == SCE_UTILITY_STATUS_FINISHED) {
		npSigninState = NP_SIGNIN_STATUS_SUCCESS;
		__RtcTimeOfDay(&npSigninTimestamp);
		request.npSigninStatus = npSigninState;
	}
	return 0;
}

int PSPNpSigninDialog::Shutdown(bool force) {
	if (ReadStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(NP_SHUTDOWN_DELAY_US);
	}

	// FIXME: This should probably be done within FinishShutdown to prevent some games (ie. UNO) from progressing further while the Dialog is still being faded-out, since we can't override non-virtual method... so here is the closes one to FinishShutdown.
	if (Memory::IsValidAddress(requestAddr)) // Need to validate first to prevent Invalid address when the game is being Shutdown/Exited to menu
		Memory::Memcpy(requestAddr, &request, request.common.size, "NpSigninDialogParam");

	return 0;
}

void PSPNpSigninDialog::DoState(PointerWrap &p) {
	PSPDialog::DoState(p);

	auto s = p.Section("PSPNpSigninDialog", 1, 1);
	if (!s)
		return;

	Do(p, request);
	Do(p, step);
	//Do(p, npSigninResult);

	if (p.mode == p.MODE_READ) {
		startTime = 0;
	}
}

pspUtilityDialogCommon* PSPNpSigninDialog::GetCommonParam()
{
	return &request.common;
}
