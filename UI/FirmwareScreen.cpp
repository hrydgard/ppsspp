// Copyright (c) 2026- PPSSPP Project.

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
#include "Common/Data/Text/Parsers.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/UI/Notice.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"

#include "UI/FirmwareScreen.h"
#include "UI/InstallUpdateScreen.h"
#include "UI/MiscViews.h"

std::string_view FirmwareScreen::GetTitle() const {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	return sy->T("PSP Firmware");
}

void FirmwareScreen::BeforeCreateViews() {
	nandRoot_ = GetSysDirectory(DIRECTORY_NAND);
	ReadInstalledFirmwareInfo(nandRoot_, &info_);
}

void FirmwareScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto st = GetI18NCategory(I18NCat::STORE);  // Borrow "Size" from here, like GameScreen does.

	LinearLayout *content = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	if (!info_.anythingInstalled) {
		content->Add(new NoticeView(NoticeLevel::INFO, sy->T("No firmware installed"),
			sy->T("FirmwareSources", "PPSSPP has its own replacements for the PSP's system files, so a firmware is optional. Installing one gets you the real fonts, and lets you launch the XMB. Most game discs carry a firmware updater - open a game's info screen to install from it.")));
		content->Add(new TextWithImage(ImageID("I_FOLDER"), GetFriendlyPath(nandRoot_)));
		return;
	}

	if (!info_.version.empty()) {
		std::string versionLine = std::string(sy->T("Firmware version")) + ": " + info_.version;
		content->Add(new TextView(versionLine, ALIGN_LEFT, false))->SetBig(true);
		if (!info_.buildDate.empty() || !info_.target.empty()) {
			std::string details = info_.buildDate;
			if (!info_.target.empty()) {
				if (!details.empty()) {
					details += " - ";
				}
				details += info_.target;
			}
			content->Add(new TextView(details, ALIGN_LEFT, false));
		}
	} else {
		// No flash0:/vsh/etc/version.txt, so this isn't a full firmware - most likely just the
		// fonts, which is what installing from a running game's disc leaves behind.
		content->Add(new NoticeView(NoticeLevel::INFO, sy->T("Partial firmware installed"),
			sy->T("Some system files are present, but not a complete firmware")));
	}

	content->Add(new ItemHeader(sy->T("Contents")));
	content->Add(new InfoItem(sy->T("Fonts"), info_.fontCount));
	content->Add(new InfoItem(sy->T("Kernel modules"), info_.kernelModuleCount));
	content->Add(new InfoItem(sy->T("XMB (VSH)"), info_.hasVsh ? di->T("Yes") : di->T("No")));
	content->Add(new InfoItem(sy->T("Files"), info_.fileCount));
	content->Add(new InfoItem(st->T("Size"), NiceSizeFormat(info_.totalSize)));

	content->Add(new ItemHeader(iz->T("Install into folder")));
	content->Add(new TextView(GetFriendlyPath(nandRoot_), ALIGN_LEFT | FLAG_WRAP_TEXT, false));
}

void FirmwareScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	// Booting anything replaces the running game, and erasing the NAND out from under a game
	// that has flash0 mounted is worse - so while a game is running, this screen is read-only.
	const bool gameRunning = PSP_IsInited();

	Choice *launch = parent->Add(new Choice(sy->T("Launch XMB"), ImageID("I_PLAY")));
	launch->OnClick.Add([this](UI::EventParams &) {
		LaunchVSH();
	});
	launch->SetEnabled(!gameRunning && info_.hasVsh && FirmwareVersionSupportsVSH(info_.version));

	if (info_.hasVsh && !FirmwareVersionSupportsVSH(info_.version)) {
		parent->Add(new NoticeView(NoticeLevel::WARN, sy->T("XMB unsupported"),
			sy->T("XMBUnsupportedVersion", "Only firmware 6.61 can boot the XMB so far")));
	}

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		Choice *install = parent->Add(new Choice(iz->T("Install PSP firmware update"), ImageID("I_FOLDER_UPLOAD")));
		install->OnClick.Add([this](UI::EventParams &) {
			BrowseForUpdater();
		});
		install->SetEnabled(!gameRunning);
	}

	if (info_.anythingInstalled) {
		Choice *erase = parent->Add(new Choice(sy->T("Erase firmware"), ImageID("I_TRASHCAN")));
		erase->OnClick.Add([this](UI::EventParams &) {
			AskToErase();
		});
		erase->SetEnabled(!gameRunning);
	}

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		parent->Add(new Choice(sy->T("Show firmware folder")))->OnClick.Add([this](UI::EventParams &) {
			System_LaunchUrl(LaunchUrlType::LOCAL_FOLDER, nandRoot_.ToString());
		});
	}

	if (gameRunning) {
		parent->Add(new NoticeView(NoticeLevel::INFO,
			sy->T("FirmwareNeedsNoGame", "Stop the game to change the installed firmware"), ""));
	}
}

void FirmwareScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Coming back from the installer (or the erase confirmation), the NAND may well look
	// different than it did when we scanned it. Rescanning is cheap, so just always redo it -
	// BeforeCreateViews does the scan.
	RecreateViews();
	UITwoPaneBaseDialogScreen::dialogFinished(dialog, result);
}

void FirmwareScreen::LaunchVSH() {
	System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, (nandRoot_ / "flash0/vsh/module/vshmain.prx").ToString());
}

void FirmwareScreen::BrowseForUpdater() {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	System_BrowseForFile(GetRequesterToken(), iz->T("Install PSP firmware update"), BrowseFileType::BOOTABLE,
		[this](std::string_view value, int) {
		screenManager()->push(new InstallUpdateScreen(Path(value), "", false));
	});
}

void FirmwareScreen::AskToErase() {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::string question(sy->T("EraseFirmwareConfirm", "This deletes everything in the NAND folder, including the fonts."));
	screenManager()->push(new UI::MessagePopupScreen(sy->T("Erase firmware"), question, di->T("Delete"), di->T("Cancel"),
		[this](bool erase) {
		if (!erase) {
			return;
		}
		std::string error;
		if (!EraseInstalledFirmware(nandRoot_, &error)) {
			ERROR_LOG(Log::Loader, "Failed to erase the firmware: %s", error.c_str());
		}
		RecreateViews();
	}));
}
