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

#if defined(_WIN32)
#include "Common/CommonWindows.h"
#endif
#include "i18n/i18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include <Core/HLE/sceNet.h>
#include "Core/Dialog/PSPNetconfDialog.h"
#include <ext/native/util/text/utf8.h>


#define NETCONF_CONNECT_APNET 0
#define NETCONF_STATUS_APNET 1
#define NETCONF_CONNECT_ADHOC 2
#define NETCONF_CONNECT_APNET_LAST 3
#define NETCONF_CREATE_ADHOC 4
#define NETCONF_JOIN_ADHOC 5

static const float FONT_SCALE = 0.65f;

// Needs testing.
const static int NET_INIT_DELAY_US = 300000;
const static int NET_SHUTDOWN_DELAY_US = 26000;
const static int NET_RUNNING_DELAY_US = 1000000; // KHBBS is showing adhoc dialog for about 3-4 seconds, but feels too long, so we're faking it to 1 sec instead to let players read the text

PSPNetconfDialog::PSPNetconfDialog() {
}

PSPNetconfDialog::~PSPNetconfDialog() {
}

int PSPNetconfDialog::Init(u32 paramAddr) {
	// Already running
	if (status != SCE_UTILITY_STATUS_NONE)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	requestAddr = paramAddr;
	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	ChangeStatusInit(NET_INIT_DELAY_US);

	// Eat any keys pressed before the dialog inited.
	UpdateButtons();
	okButtonImg = ImageID("I_CIRCLE");
	cancelButtonImg = ImageID("I_CROSS");
	okButtonFlag = CTRL_CIRCLE;
	cancelButtonFlag = CTRL_CROSS;
	if (request.common.buttonSwap == 1)
	{
		okButtonImg = ImageID("I_CROSS");
		cancelButtonImg = ImageID("I_CIRCLE");
		okButtonFlag = CTRL_CROSS;
		cancelButtonFlag = CTRL_CIRCLE;
	}

	StartFade(true);
	return 0;
}

void PSPNetconfDialog::DrawBanner() {

	PPGeDrawRect(0, 0, 480, 22, CalcFadedColor(0x65636358));

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
	textStyle.hasShadow = false;

	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 5, 11.0f, 10.0f, 1, 10, 1, 10, 10, 10, CalcFadedColor(0xFFFFFFFF));
	auto di = GetI18NCategory("Dialog");
	PPGeDrawText(di->T("Network Connection"), 31, 10, textStyle);
}

void PSPNetconfDialog::DrawIndicator() {

	// TODO: Draw animated circle as processing indicator
	PPGeDrawImage(456, 248, 20.0f, 20.0f, 1, 10, 1, 10, 10, 10, CalcFadedColor(0xFFFFFFFF));
}

void PSPNetconfDialog::DisplayMessage(std::string text1, std::string text2a, std::string text2b, std::string text3a, std::string text3b, bool hasYesNo, bool hasOK) {
	auto di = GetI18NCategory("Dialog");

	PPGeStyle buttonStyle = FadedStyle(PPGeAlign::BOX_CENTER, FONT_SCALE);
	PPGeStyle messageStyle = FadedStyle(PPGeAlign::BOX_HCENTER, FONT_SCALE);
	PPGeStyle messageStyleRight = FadedStyle(PPGeAlign::BOX_RIGHT, FONT_SCALE);
	PPGeStyle messageStyleLeft = FadedStyle(PPGeAlign::BOX_LEFT, FONT_SCALE);

	std::string text2 = text2a + "  " + text2b;
	std::string text3 = text3a + "  " + text3b;

	// Without the scrollbar, we have 350 total pixels.
	float WRAP_WIDTH = 300.0f;
	if (UTF8StringNonASCIICount(text1.c_str()) >= text1.size() / 4) {
		WRAP_WIDTH = 336.0f;
		if (text1.size() > 12) {
			messageStyle.scale = 0.6f;
		}
	}

	float totalHeight1 = 0.0f;
	PPGeMeasureText(nullptr, &totalHeight1, text1.c_str(), FONT_SCALE, PPGE_LINE_WRAP_WORD, WRAP_WIDTH);
	float totalHeight2 = 0.0f;
	if (text2 != "  ")
		PPGeMeasureText(nullptr, &totalHeight2, text2.c_str(), FONT_SCALE, PPGE_LINE_USE_ELLIPSIS, WRAP_WIDTH);
	float totalHeight3 = 0.0f;
	if (text3 != "  ")
		PPGeMeasureText(nullptr, &totalHeight3, text3.c_str(), FONT_SCALE, PPGE_LINE_USE_ELLIPSIS, WRAP_WIDTH);
	float marginTop = 0.0f;
	if (text2 != "  " || text3 != "  ")
		marginTop = 11.0f;
	float totalHeight = totalHeight1 + totalHeight2 + totalHeight3 + marginTop;
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
		}
		else {
			drawSelectionBoxAndAdjust(273.0f);
		}

		PPGeDrawText(di->T("Yes"), 203.0f, buttonY - 1.0f, buttonStyle);
		PPGeDrawText(di->T("No"), 272.0f, buttonY - 1.0f, buttonStyle);
		if (IsButtonPressed(CTRL_LEFT) && yesnoChoice == 0) {
			yesnoChoice = 1;
		}
		else if (IsButtonPressed(CTRL_RIGHT) && yesnoChoice == 1) {
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
	PPGeDrawTextWrapped(text1.c_str(), 240.0f, centerY - h2 - scrollPos_, WRAP_WIDTH, 0, messageStyle);
	if (text2a != "")
		PPGeDrawTextWrapped(text2a.c_str(), 240.0f - 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + marginTop, WRAP_WIDTH, 0, messageStyleRight);
	if (text2b != "")
		PPGeDrawTextWrapped(text2b.c_str(), 240.0f + 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + marginTop, WRAP_WIDTH, 0, messageStyleLeft);
	if (text3a != "")
		PPGeDrawTextWrapped(text3a.c_str(), 240.0f - 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + totalHeight2 + marginTop, WRAP_WIDTH, 0, messageStyleRight);
	if (text3b != "")
		PPGeDrawTextWrapped(text3b.c_str(), 240.0f + 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + totalHeight2 + marginTop, WRAP_WIDTH, 0, messageStyleLeft);
	PPGeScissorReset();

	// Do we need a scrollbar?
	if (visibleHeight < totalHeight) {
		float scrollSpeed = 5.0f;
		float scrollMax = totalHeight - visibleHeight;

		float bobHeight = (visibleHeight / totalHeight) * visibleHeight;
		float bobOffset = (scrollPos_ / scrollMax) * (visibleHeight - bobHeight);
		float bobY1 = centerY - h2 + bobOffset;
		PPGeDrawRect(415.0f, bobY1, 420.0f, bobY1 + bobHeight, CalcFadedColor(0xFFCCCCCC));

		auto buttonDown = [this](int btn, int& held) {
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

	PPGeDrawRect(60.0f, sy, 420.0f, sy + 1.0f, CalcFadedColor(0xFFFFFFFF));
	PPGeDrawRect(60.0f, ey, 420.0f, ey + 1.0f, CalcFadedColor(0xFFFFFFFF));
}

int PSPNetconfDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	UpdateButtons();
	auto di = GetI18NCategory("Dialog");
	auto err = GetI18NCategory("Error");
	
	// It seems JPCSP doesn't check for NETCONF_STATUS_APNET
	if (request.netAction == NETCONF_CONNECT_APNET || request.netAction == NETCONF_STATUS_APNET || request.netAction == NETCONF_CONNECT_APNET_LAST) {
		int state = NetApctl_GetState();

		UpdateFade(animSpeed);
		StartDraw();

		if (!hideNotice) {
			const float WRAP_WIDTH = 254.0f;
			const ImageID confirmBtnImage = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? ImageID("I_CROSS") : ImageID("I_CIRCLE");
			const int confirmBtn = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CROSS : CTRL_CIRCLE;
			const ImageID cancelBtnImage = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? ImageID("I_CIRCLE") : ImageID("I_CROSS");
			const int cancelBtn = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CIRCLE : CTRL_CROSS;

			PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_CENTER, 0.5f);
			PPGeStyle buttonStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.5f);

			PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0x63636363));
			DrawBanner();
			PPGeDrawTextWrapped(err->T("PPSSPPDoesNotSupportInternet", "PPSSPP currently does not support connecting to the Internet for DLC, PSN, or game updates.\nContinuing may cause unexpected behavior or freezes."), 241, 132, WRAP_WIDTH, 0, textStyle);
			PPGeDrawImage(confirmBtnImage, 185, 240, 20, 20, buttonStyle);
			PPGeDrawText(di->T("OK"), 215, 243, buttonStyle);
			PPGeDrawImage(cancelBtnImage, 255, 240, 20, 20, buttonStyle);
			PPGeDrawText(di->T("Cancel"), 285, 243, buttonStyle);

			// Since we don't support Infrastructure API yet.. Let the Player read the message first and choose to continue or not (ie. for testing networks API)
			if (IsButtonPressed(cancelBtn)) {
				StartFade(false);
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_SHUTDOWN_DELAY_US);
				// TODO: When the dialog is aborted, does it really set the result to this?
				// It seems to make Phantasy Star Portable 2 happy, so it should be okay for now.
				request.common.result = SCE_UTILITY_DIALOG_RESULT_ABORT;
			}
			else if (IsButtonPressed(confirmBtn)) {
				hideNotice = true;
				StartFade(true);
			}
		}
		else {
			PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));
			DrawBanner();
			DrawIndicator();

			if (state == PSP_NET_APCTL_STATE_GOT_IP || state == PSP_NET_APCTL_STATE_GETTING_IP) {
				DisplayMessage(di->T("ObtainingIP", "Obtaining IP address.\nPlease wait..."), di->T("ConnectionName", "Connection Name"), netApctlInfo.name, di->T("SSID"), netApctlInfo.ssid);
			}
			else {
				// Skipping the Select Connection screen since we only have 1 fake profile
				DisplayMessage(di->T("ConnectingAP", "Connecting to the access point.\nPlease wait..."), di->T("ConnectionName", "Connection Name"), netApctlInfo.name, di->T("SSID"), netApctlInfo.ssid);
			}
			DisplayButtons(DS_BUTTON_CANCEL, di->T("Cancel"));

			// The Netconf dialog stays visible until the network reaches
			// the state PSP_NET_APCTL_STATE_GOT_IP.			
			if (state == PSP_NET_APCTL_STATE_GOT_IP) {
				if (pendingStatus != SCE_UTILITY_STATUS_FINISHED) {
					ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_RUNNING_DELAY_US);
				}
				else if (GetStatus() == SCE_UTILITY_STATUS_FINISHED) {
					// We are done!
					StartFade(false);
				}
			}

			else if (state == PSP_NET_APCTL_STATE_GETTING_IP) {
				// Switch to the next message
				StartFade(true);
			}

			else if (state == PSP_NET_APCTL_STATE_DISCONNECTED) {
				// When connecting with infrastructure, simulate a connection
				// using the first network configuration entry.
				connResult = sceNetApctlConnect(1);
			}
		}

		EndDraw();
	}
	else if (request.netAction == NETCONF_CONNECT_ADHOC || request.netAction == NETCONF_CREATE_ADHOC || request.netAction == NETCONF_JOIN_ADHOC) {
		int state = NetAdhocctl_GetState();

		UpdateFade(animSpeed);
		StartDraw();
		PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));
		DrawBanner();
		DrawIndicator();

		std::string channel = std::to_string(g_Config.iWlanAdhocChannel);
		if (g_Config.iWlanAdhocChannel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC)
			channel = "Automatic";

		DisplayMessage(di->T("ConnectingChannel", "Connecting.\nPlease wait...\n\nChannel")+std::string("  ")+di->T(channel));

		// Only Join mode is showing Cancel button on KHBBS and the button will fade out before the dialog is fading out, probably because it's already connected thus can't be canceled anymore
		if (request.netAction == NETCONF_JOIN_ADHOC)
			DisplayButtons(DS_BUTTON_CANCEL, di->T("Cancel"));

		if (state == ADHOCCTL_STATE_DISCONNECTED && request.NetconfData.IsValid()) {
			connResult = sceNetAdhocctlCreate(request.NetconfData->groupName);
		}

		// The Netconf dialog stays visible until the network reaches
		// the state ADHOCCTL_STATE_CONNECTED.
		if (state == ADHOCCTL_STATE_CONNECTED) {
			// Checking pendingStatus to make sure ChangeStatus not to continously extending the delay ticks on every call for eternity
			if (pendingStatus != SCE_UTILITY_STATUS_FINISHED) {
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_RUNNING_DELAY_US);
			}
			// Start fading only when the actual status has changed
			else if (GetStatus() == SCE_UTILITY_STATUS_FINISHED) {
				StartFade(false);
			}
		}

		if (request.netAction == NETCONF_JOIN_ADHOC && IsButtonPressed(cancelButtonFlag)) {
			StartFade(false);
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_SHUTDOWN_DELAY_US);
			request.common.result = SCE_UTILITY_DIALOG_RESULT_ABORT;
		}

		EndDraw();
	}

	if (GetStatus() == SCE_UTILITY_STATUS_FINISHED || pendingStatus == SCE_UTILITY_STATUS_FINISHED)
		Memory::Memcpy(requestAddr, &request, request.common.size);

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

	Do(p, request);
}

pspUtilityDialogCommon* PSPNetconfDialog::GetCommonParam()
{
	return &request.common;
}
