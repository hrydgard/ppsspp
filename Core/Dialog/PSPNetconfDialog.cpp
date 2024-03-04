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
#include "Common/CommonWindows.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PPGeDraw.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Dialog/PSPNetconfDialog.h"
#include "Common/Data/Encoding/Utf8.h"


#define NETCONF_CONNECT_APNET 0
#define NETCONF_STATUS_APNET 1
#define NETCONF_CONNECT_ADHOC 2
#define NETCONF_CONNECT_APNET_LAST 3
#define NETCONF_CREATE_ADHOC 4
#define NETCONF_JOIN_ADHOC 5

static const float FONT_SCALE = 0.65f;

// Needs testing.
const static int NET_INIT_DELAY_US = 200000; 
const static int NET_SHUTDOWN_DELAY_US = 200000; 
const static int NET_CONNECT_TIMEOUT = 15000000; // Using 15 secs to match the timeout on Adhoc Server side (SERVER_USER_TIMEOUT)

struct ScanInfos {
	s32_le sz;
	SceNetAdhocctlScanInfoEmu si;
} PACK;


PSPNetconfDialog::PSPNetconfDialog(UtilityDialogType type) : PSPDialog(type) {
}

PSPNetconfDialog::~PSPNetconfDialog() {
}

int PSPNetconfDialog::Init(u32 paramAddr) {
	// Already running
	if (ReadStatus() != SCE_UTILITY_STATUS_NONE)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	requestAddr = paramAddr;
	int size = Memory::Read_U32(paramAddr);
	memset(&request, 0, sizeof(request));
	// Only copy the right size to support different request format
	Memory::Memcpy(&request, paramAddr, size);

	ChangeStatusInit(NET_INIT_DELAY_US);

	// Eat any keys pressed before the dialog inited.
	InitCommon();
	UpdateButtons();

	connResult = -1;
	scanInfosAddr = 0;
	scanStep = 0;
	startTime = (u64)(time_now_d() * 1000000.0);

	StartFade(true);
	return 0;
}

void PSPNetconfDialog::DrawBanner() {

	PPGeDrawRect(0, 0, 480, 22, CalcFadedColor(0x65636358));

	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_VCENTER, 0.6f);
	textStyle.hasShadow = false;

	// TODO: Draw a hexagon icon
	PPGeDrawImage(10, 5, 11.0f, 10.0f, 1, 10, 1, 10, 10, 10, FadedImageStyle());
	auto di = GetI18NCategory(I18NCat::DIALOG);
	PPGeDrawText(di->T("Network Connection"), 31, 10, textStyle);
}

void PSPNetconfDialog::DrawIndicator() {
	// TODO: Draw animated circle as processing indicator
	PPGeDrawImage(456, 248, 20.0f, 20.0f, 1, 10, 1, 10, 10, 10, FadedImageStyle());
}

void PSPNetconfDialog::DisplayMessage(std::string_view text1, std::string_view text2a, std::string_view text2b, std::string_view text3a, std::string_view text3b, bool hasYesNo, bool hasOK) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	PPGeStyle buttonStyle = FadedStyle(PPGeAlign::BOX_CENTER, FONT_SCALE);
	PPGeStyle messageStyle = FadedStyle(PPGeAlign::BOX_HCENTER, FONT_SCALE);
	PPGeStyle messageStyleRight = FadedStyle(PPGeAlign::BOX_RIGHT, FONT_SCALE);
	PPGeStyle messageStyleLeft = FadedStyle(PPGeAlign::BOX_LEFT, FONT_SCALE);

	std::string text2 = std::string(text2a) + "  " + std::string(text2b);
	std::string text3 = std::string(text3a) + "  " + std::string(text3b);

	// Without the scrollbar, we have 350 total pixels.
	float WRAP_WIDTH = 300.0f;
	if (UTF8StringNonASCIICount(text1) >= (int)text1.size() / 4) {
		WRAP_WIDTH = 336.0f;
		if (text1.size() > 12) {
			messageStyle.scale = 0.6f;
		}
	}

	float totalHeight1 = 0.0f;
	PPGeMeasureText(nullptr, &totalHeight1, text1, FONT_SCALE, PPGE_LINE_WRAP_WORD, WRAP_WIDTH);
	float totalHeight2 = 0.0f;
	if (text2 != "  ")
		PPGeMeasureText(nullptr, &totalHeight2, text2, FONT_SCALE, PPGE_LINE_USE_ELLIPSIS, WRAP_WIDTH);
	float totalHeight3 = 0.0f;
	if (text3 != "  ")
		PPGeMeasureText(nullptr, &totalHeight3, text3, FONT_SCALE, PPGE_LINE_USE_ELLIPSIS, WRAP_WIDTH);
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
	PPGeDrawTextWrapped(text1, 240.0f, centerY - h2 - scrollPos_, WRAP_WIDTH, 0, messageStyle);
	if (!text2a.empty()) {
		if (!text2b.empty())
			PPGeDrawTextWrapped(text2a, 240.0f - 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + marginTop, WRAP_WIDTH, 0, messageStyleRight);
		else
			PPGeDrawTextWrapped(text2a, 240.0f, centerY - h2 - scrollPos_ + totalHeight1 + marginTop, WRAP_WIDTH, 0, messageStyle);
	}
	if (!text2b.empty())
		PPGeDrawTextWrapped(text2b, 240.0f + 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + marginTop, WRAP_WIDTH, 0, messageStyleLeft);
	if (!text3a.empty()) {
		if (!text3b.empty())
			PPGeDrawTextWrapped(text3a, 240.0f - 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + totalHeight2 + marginTop, WRAP_WIDTH, 0, messageStyleRight);
		else
			PPGeDrawTextWrapped(text3a, 240.0f, centerY - h2 - scrollPos_ + totalHeight1 + totalHeight2 + marginTop, WRAP_WIDTH, 0, messageStyle);
	}
	if (!text3b.empty())
		PPGeDrawTextWrapped(text3b, 240.0f + 5.0f, centerY - h2 - scrollPos_ + totalHeight1 + totalHeight2 + marginTop, WRAP_WIDTH, 0, messageStyleLeft);
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
	if (ReadStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	UpdateButtons();
	UpdateCommon();
	auto di = GetI18NCategory(I18NCat::DIALOG);
	u64 now = (u64)(time_now_d() * 1000000.0);
	
	// It seems JPCSP doesn't check for NETCONF_STATUS_APNET
	if (request.netAction == NETCONF_CONNECT_APNET || request.netAction == NETCONF_STATUS_APNET || request.netAction == NETCONF_CONNECT_APNET_LAST) {
		int state = NetApctl_GetState();

		UpdateFade(animSpeed);
		StartDraw();

		if (!hideNotice) {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			const float WRAP_WIDTH = 254.0f;
			const int confirmBtn = GetConfirmButton();
			const int cancelBtn = GetCancelButton();
			const ImageID confirmBtnImage = confirmBtn == CTRL_CROSS ? ImageID("I_CROSS") : ImageID("I_CIRCLE");
			const ImageID cancelBtnImage = cancelBtn == CTRL_CIRCLE ? ImageID("I_CIRCLE") : ImageID("I_CROSS");

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

			// The Netconf dialog stays visible until the network reaches the state PSP_NET_APCTL_STATE_GOT_IP.			
			if (state == PSP_NET_APCTL_STATE_GOT_IP) {
				if (pendingStatus != SCE_UTILITY_STATUS_FINISHED) {
					StartFade(false);
					ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_SHUTDOWN_DELAY_US);
				}
			}

			else if (state == PSP_NET_APCTL_STATE_JOINING) {
				// Switch to the next message
				StartFade(true);
			}

			else if (state == PSP_NET_APCTL_STATE_DISCONNECTED) {
				// When connecting with infrastructure, simulate a connection using the first network configuration entry.
				if (connResult < 0) {
					connResult = sceNetApctlConnect(1);
				}
			}
		}

		EndDraw();
	}
	else if (request.netAction == NETCONF_CONNECT_ADHOC || request.netAction == NETCONF_CREATE_ADHOC || request.netAction == NETCONF_JOIN_ADHOC) {
		int state = NetAdhocctl_GetState();
		bool timedout = (state == ADHOCCTL_STATE_DISCONNECTED && now - startTime > NET_CONNECT_TIMEOUT);

		UpdateFade(animSpeed);
		StartDraw();
		PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));
		DrawBanner();
		DrawIndicator();

		if (timedout) {
			// FIXME: Do we need to show error message?
			std::string message(di->T("InternalError", "An internal error has occurred."));
			DisplayMessage(message + StringFromFormat("\n(%08X)", connResult));
			DisplayButtons(DS_BUTTON_CANCEL, di->T("Back"));
		}
		else {
			std::string channel = std::to_string(g_Config.iWlanAdhocChannel);
			if (g_Config.iWlanAdhocChannel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC)
				channel = "Automatic";

			DisplayMessage(di->T("ConnectingPleaseWait", "Connecting.\nPlease wait..."), std::string(di->T("Channel:")) + std::string(" ") + std::string(di->T(channel)));

			// Only Join mode is showing Cancel button on KHBBS and the button will fade out before the dialog is fading out, probably because it's already connected thus can't be canceled anymore
			if (request.netAction == NETCONF_JOIN_ADHOC)
				DisplayButtons(DS_BUTTON_CANCEL, di->T("Cancel"));

			// KHBBS will first enter the arena using NETCONF_CONNECT_ADHOC (auto-create group when not exist yet?), but when the event started the event's creator use NETCONF_CREATE_ADHOC while the joining players use NETCONF_JOIN_ADHOC
			if (request.NetconfData.IsValid()) {
				if (state == ADHOCCTL_STATE_DISCONNECTED) {
					switch (request.netAction)
					{
					case NETCONF_CREATE_ADHOC:
						if (connResult < 0) {
							connResult = sceNetAdhocctlCreate(request.NetconfData->groupName);
						}
						break;
					case NETCONF_JOIN_ADHOC:
						// FIXME: Should we Scan for a matching group first before Joining a Group (like adhoc games normally do)? Or Is it really allowed to join non-existing group?
						if (scanStep == 0) {
							if (sceNetAdhocctlScan() >= 0) {
								u32 structsz = sizeof(ScanInfos);
								if (Memory::IsValidAddress(scanInfosAddr))
									userMemory.Free(scanInfosAddr);
								scanInfosAddr = userMemory.Alloc(structsz, false, "NetconfScanInfo");
								Memory::Write_U32(sizeof(SceNetAdhocctlScanInfoEmu), scanInfosAddr);
								scanStep = 1;
							}
						}
						else if (scanStep == 1) {
							s32 sz = Memory::Read_U32(scanInfosAddr);
							// Get required buffer size
							if (sceNetAdhocctlGetScanInfo(scanInfosAddr, 0) >= 0) {
								s32 reqsz = Memory::Read_U32(scanInfosAddr);
								if (reqsz > sz) {
									sz = reqsz;
									if (Memory::IsValidAddress(scanInfosAddr))
										userMemory.Free(scanInfosAddr);
									u32 structsz = sz + sizeof(s32);
									scanInfosAddr = userMemory.Alloc(structsz, false, "NetconfScanInfo");
									Memory::Write_U32(sz, scanInfosAddr);
								}
								if (reqsz > 0) {
									if (sceNetAdhocctlGetScanInfo(scanInfosAddr, scanInfosAddr + sizeof(s32)) >= 0) {
										ScanInfos* scanInfos = (ScanInfos*)Memory::GetPointer(scanInfosAddr);
										int n = scanInfos->sz / sizeof(SceNetAdhocctlScanInfoEmu);
										// Assuming returned SceNetAdhocctlScanInfoEmu(s) are contagious where next is pointing to current addr + sizeof(SceNetAdhocctlScanInfoEmu)
										while (n > 0) {
											SceNetAdhocctlScanInfoEmu* si = (SceNetAdhocctlScanInfoEmu*)Memory::GetPointer(scanInfosAddr + sizeof(s32) + sizeof(SceNetAdhocctlScanInfoEmu) * (n - 1LL));
											if (memcmp(si->group_name.data, request.NetconfData->groupName, ADHOCCTL_GROUPNAME_LEN) == 0) {
												// Moving found group info to the front so we can use it on sceNetAdhocctlJoin easily
												memcpy((char*)scanInfos + sizeof(s32), si, sizeof(SceNetAdhocctlScanInfoEmu));
												scanStep = 2;
												break;
											}
											n--;
										}
										// Target group not found, try to scan again later
										if (n <= 0) {
											scanStep = 0;
										}
									}
								}
								// No group found, try to scan again later
								else {
									scanStep = 0;
								}
							}
						}
						else if (scanStep == 2) {
							if (connResult < 0) {
								connResult = sceNetAdhocctlJoin(scanInfosAddr + sizeof(s32));
								if (connResult >= 0) {
									// We are done!
									if (Memory::IsValidAddress(scanInfosAddr))
										userMemory.Free(scanInfosAddr);
									scanInfosAddr = 0;
								}
							}
						}
						break;
					default:
						if (connResult < 0) {
							connResult = sceNetAdhocctlConnect(request.NetconfData->groupName);
						}
						break;
					}
				}
			}
		}

		// The Netconf dialog stays visible until the network reaches the state ADHOCCTL_STATE_CONNECTED.
		if (state == ADHOCCTL_STATE_CONNECTED) {
			// Checking pendingStatus to make sure ChangeStatus not to continously extending the delay ticks on every call for eternity
			if (pendingStatus != SCE_UTILITY_STATUS_FINISHED) {
				StartFade(false);
				ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_SHUTDOWN_DELAY_US);
			}

			// Let's not leaks any memory
			if (Memory::IsValidAddress(scanInfosAddr))
				userMemory.Free(scanInfosAddr);
			scanInfosAddr = 0;
		}

		if ((request.netAction == NETCONF_JOIN_ADHOC || timedout) && IsButtonPressed(cancelButtonFlag)) {
			StartFade(false);
			ChangeStatus(SCE_UTILITY_STATUS_FINISHED, NET_SHUTDOWN_DELAY_US);
			request.common.result = SCE_UTILITY_DIALOG_RESULT_ABORT;
			// Let's not leaks any memory
			if (Memory::IsValidAddress(scanInfosAddr))
				userMemory.Free(scanInfosAddr);
			scanInfosAddr = 0;
		}

		EndDraw();
	}

	if (ReadStatus() == SCE_UTILITY_STATUS_FINISHED || pendingStatus == SCE_UTILITY_STATUS_FINISHED)
		Memory::Memcpy(requestAddr, &request, request.common.size, "NetConfDialogParam");

	return 0;
}

int PSPNetconfDialog::Shutdown(bool force) {
	if (ReadStatus() != SCE_UTILITY_STATUS_FINISHED && !force)
		return SCE_ERROR_UTILITY_INVALID_STATUS;

	PSPDialog::Shutdown(force);
	if (!force) {
		ChangeStatusShutdown(NET_SHUTDOWN_DELAY_US);
	}

	return 0;
}

void PSPNetconfDialog::DoState(PointerWrap &p) {	
	PSPDialog::DoState(p);

	auto s = p.Section("PSPNetconfigDialog", 0, 2);
	if (!s)
		return;

	Do(p, request);
	if (s >= 2) {
		Do(p, scanInfosAddr);
		Do(p, scanStep);
		Do(p, connResult);
	}
	else {
		scanInfosAddr = 0;
		scanStep = 0;
		connResult = -1;
	}

	if (p.mode == p.MODE_READ) {
		startTime = 0;
	}
}

pspUtilityDialogCommon* PSPNetconfDialog::GetCommonParam()
{
	return &request.common;
}
