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

#include "PSPNetconfDialog.h"
#include "ChunkFile.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "i18n/i18n.h"

#define NETCONF_CONNECT_APNET 0
#define NETCONF_STATUS_APNET 1
#define NETCONF_CONNECT_ADHOC 2
#define NETCONF_CONNECT_APNET_LAST 3
#define NETCONF_CREATE_ADHOC 4
#define NETCONF_JOIN_ADHOC 5

// Needs testing.
const static int NET_INIT_DELAY_US = 300000;
const static int NET_SHUTDOWN_DELAY_US = 26000;

PSPNetconfDialog::PSPNetconfDialog() {
}

PSPNetconfDialog::~PSPNetconfDialog() {
}

int PSPNetconfDialog::Init(u32 paramAddr) {
	// Already running
	if (status != SCE_UTILITY_STATUS_NONE)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	ChangeStatusInit(NET_INIT_DELAY_US);

	// Eat any keys pressed before the dialog inited.
	UpdateButtons();

	StartFade(true);
	return 0;
}

void PSPNetconfDialog::DrawBanner() {

	PPGeDrawRect(0, 0, 480, 23, CalcFadedColor(0x65636358));

	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 6, 12.0f, 12.0f, 1, 10, 1, 10, 10, 10, CalcFadedColor(0xFFFFFFFF));
	auto di = GetI18NCategory("Dialog");
	PPGeDrawText(di->T("Network Connection"), 30, 11, PPGeAlign::BOX_VCENTER, 0.6f, CalcFadedColor(0xFFFFFFFF));
}

int PSPNetconfDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	UpdateButtons();
	auto di = GetI18NCategory("Dialog");
	auto err = GetI18NCategory("Error");
	const float WRAP_WIDTH = 254.0f;
	const ImageID confirmBtnImage = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? ImageID("I_CROSS") : ImageID("I_CIRCLE");
	const int confirmBtn = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CROSS : CTRL_CIRCLE;

	if (request.netAction == NETCONF_CONNECT_APNET || request.netAction == NETCONF_STATUS_APNET || request.netAction == NETCONF_CONNECT_APNET_LAST) {
		UpdateFade(animSpeed);
		StartDraw();
		DrawBanner();
		PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x63636363));
		PPGeDrawTextWrapped(err->T("PPSSPPDoesNotSupportInternet", "PPSSPP currently does not support connecting to the Internet for DLC, PSN, or game updates."), 241, 132, WRAP_WIDTH, 0, PPGeAlign::BOX_CENTER, 0.5f, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawImage(confirmBtnImage, 195, 250, 20, 20, 0, CalcFadedColor(0xFFFFFFFF));
		PPGeDrawText(di->T("OK"), 225, 252, PPGeAlign::BOX_LEFT, 0.5f, CalcFadedColor(0xFFFFFFFF));

		if (IsButtonPressed(confirmBtn)) {
			StartFade(false);
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
			// TODO: When the dialog is aborted, does it really set the result to this?
			// It seems to make Phantasy Star Portable 2 happy, so it should be okay for now.
			request.common.result = SCE_UTILITY_DIALOG_RESULT_ABORT;
		}

		EndDraw();
	} else if (request.netAction == NETCONF_CONNECT_ADHOC || request.netAction == NETCONF_CREATE_ADHOC || request.netAction == NETCONF_JOIN_ADHOC) {
		if (request.NetconfData.IsValid()) {
			Shutdown(true);
			if (sceNetAdhocctlCreate(request.NetconfData->groupName) == 0) {
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, 0);
				return 0;
			}
			return -1;
		}
	}

	return 0;
}

int PSPNetconfDialog::Shutdown(bool force) {
	if (status != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(NET_SHUTDOWN_DELAY_US);
	}

	return 0;
}

void PSPNetconfDialog::DoState(PointerWrap &p) {	
	PSPDialog::DoState(p);

	auto s = p.Section("PSPNetconfigDialog", 0, 1);
	if (!s)
		return;

	p.Do(request);
}
