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
#include "Common/System/System.h"
#include "Core/Dialog/PSPHtmlViewerDialog.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/Util/PPGeDraw.h"

// Guesses. The real browser takes much longer to come up.
static const int HTMLVIEWER_INIT_DELAY_US = 200000;
static const int HTMLVIEWER_SHUTDOWN_DELAY_US = 40000;

struct SceUtilityHtmlViewerParam {
	pspUtilityDialogCommon common;
	u32_le memAddr;
	u32_le memSize;
	s32_le unknown1;
	s32_le unknown2;
	u32_le initialUrl;
	u32_le numTabs;
	u32_le interfaceMode;
	u32_le options;
	u32_le dlDirName;
	u32_le dlFileName;
	u32_le ulDirName;
	u32_le ulFileName;
	u32_le cookieMode;
	u32_le unknown3;
	u32_le homeUrl;
	u32_le textSize;
	// The smallest request (0x70) ends here.
};

// From sceUtility_Driver's InitStart for the HtmlViewer, firmware 6.61: the request size says which
// firmware's layout it is: 0x70, 0x78 and 0x80 are 2.00 to 2.60, 0x98, 0xA4 and 0xA8 2.70 to 3.00.
// The working memory it allocates for the browser depends on that and on bit 0x400 of options.
static u32 HtmlViewerWorkSize(u32 size, u32 options) {
	return size >= 0x98 && (options & 0x400) ? 0x480000 : 0x380000;
}

static std::string ReadUrl(u32 addr) {
	if (!Memory::IsValidNullTerminatedString(addr)) {
		return "";
	}
	return Memory::GetCharPointerUnchecked(addr);
}

// Only plain web addresses get handed to the host: printable ASCII, nothing a shell or URL parser
// could take for something else.
static bool IsWebUrl(std::string_view url) {
	if (!startsWithNoCase(url, "http://") && !startsWithNoCase(url, "https://")) {
		return false;
	}
	for (char c : url) {
		if (c <= 0x20 || c >= 0x7F) {
			return false;
		}
	}
	return true;
}

PSPHtmlViewerDialog::PSPHtmlViewerDialog(UtilityDialogType type) : PSPDialog(type) {
}

int PSPHtmlViewerDialog::Init(u32 paramAddr) {
	if (GetStatus() != SCE_UTILITY_STATUS_NONE) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}
	const int check = CheckRequest(paramAddr, { 0x70, 0x78, 0x80, 0x98, 0xA4, 0xA8 });
	if (check < 0) {
		return check;
	}
	const u32 size = Memory::ReadUnchecked_U32(paramAddr);
	const SceUtilityHtmlViewerParam *param = (const SceUtilityHtmlViewerParam *)Memory::GetPointerUnchecked(paramAddr);
	u32 workSize = HtmlViewerWorkSize(size, param->options);

	u32 addr = userMemory.Alloc(workSize, false, "HtmlViewer");
	if (addr == (u32)-1) {
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	workMemory_ = addr;

	params_ = paramAddr;
	url_ = ReadUrl(param->initialUrl);
	if (url_.empty()) {
		url_ = ReadUrl(param->homeUrl);
	}
	started_ = true;
	scrollPos_ = 0.0f;
	framesUpHeld_ = 0;
	framesDownHeld_ = 0;

	ChangeStatus(SCE_UTILITY_STATUS_INITIALIZE, 0);
	ChangeStatus(SCE_UTILITY_STATUS_RUNNING, HTMLVIEWER_INIT_DELAY_US);
	InitCommon();
	// Presses from before (like the one that opened this) mustn't open the page.
	UpdateButtons();
	StartFade(true);
	return 0;
}

int PSPHtmlViewerDialog::Update(int animSpeed) {
	if (GetStatus() != SCE_UTILITY_STATUS_RUNNING) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	auto di = GetI18NCategory(I18NCat::DIALOG);
	const bool hasUrl = IsWebUrl(url_);
	const bool canOpen = hasUrl && System_GetPropertyBool(SYSPROP_CAN_LAUNCH_URL);

	UpdateButtons();
	UpdateCommon();
	UpdateFade(animSpeed);

	StartDraw();
	PPGeDrawRect(0, 0, 480, 272, CalcFadedColor(0xC0C8B2AC));
	if (hasUrl) {
		DisplayMessage2(std::string(di->T("The game wants to open a web page:")) + "\n\n" + url_);
	} else {
		DisplayMessage2(di->T("The game wants to open a web page, but gave no address."));
	}

	// Fixed buttons, whatever the game's confirm button is: X opens, O backs out.
	PPGeStyle textStyle = FadedStyle(PPGeAlign::BOX_LEFT, 0.55f);
	if (canOpen) {
		PPGeDrawImage(ImageID("I_CROSS"), 183.5f, 256, 11.5f, 11.5f, textStyle);
		PPGeDrawText(di->T("Open in browser"), 183.5f + 14.5f, 252, textStyle);
	}
	PPGeDrawImage(ImageID("I_CIRCLE"), 321.5f, 256, 11.5f, 11.5f, textStyle);
	PPGeDrawText(di->T("Back"), 321.5f + 14.5f, 252, textStyle);

	bool done = false;
	if (canOpen && IsButtonPressed(CTRL_CROSS)) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url_);
		done = true;
	} else if (IsButtonPressed(CTRL_CIRCLE)) {
		done = true;
	}
	if (done) {
		// As if the player had browsed and then left the browser.
		params_->result = 0;
		params_.NotifyWrite("DialogResult");
		StartFade(false);
	}

	EndDraw();
	return 0;
}

void PSPHtmlViewerDialog::FreeWorkMemory() {
	if (workMemory_ != 0) {
		userMemory.Free(workMemory_);
		workMemory_ = 0;
	}
}

int PSPHtmlViewerDialog::Shutdown(bool force) {
	if (ReadStatus() != SCE_UTILITY_STATUS_FINISHED && !force) {
		return SCE_ERROR_UTILITY_INVALID_STATUS;
	}

	if (force) {
		// Only when the emulator shuts down, and user memory goes with it.
		workMemory_ = 0;
		PSPDialog::Shutdown(true);
	} else {
		FreeWorkMemory();
		ChangeStatus(SCE_UTILITY_STATUS_SHUTDOWN, 0);
		ChangeStatus(SCE_UTILITY_STATUS_NONE, HTMLVIEWER_SHUTDOWN_DELAY_US);
	}
	return 0;
}

void PSPHtmlViewerDialog::DoState(PointerWrap &p) {
	PSPDialog::DoState(p);

	auto s = p.Section("PSPHtmlViewerDialog", 1);
	if (!s)
		return;

	Do(p, params_);
	Do(p, url_);
	Do(p, workMemory_);
	Do(p, started_);
}

pspUtilityDialogCommon *PSPHtmlViewerDialog::GetCommonParam() {
	if (params_.IsValid())
		return params_;
	return nullptr;
}
