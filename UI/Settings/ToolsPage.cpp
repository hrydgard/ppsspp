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
#include "Common/System/System.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "UI/DeveloperSettingsPages.h"
#include "UI/FirmwareScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/SavedataScreen.h"
#include "UI/SettingsPage.h"
#include "UI/Settings/MainSettingsPages.h"
#include "UI/SystemInfoScreen.h"

void BuildToolsSettings(SettingsPage *page) {
	using namespace UI;
	SettingsPageHost *host = page->Host();

	page->Header("Tools");

	const bool showRetroAchievements = System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR;
	if (showRetroAchievements) {
		page->Choice({ I18NCat::SYSTEM, "RetroAchievements" }).OnClick([host](UI::EventParams &) {
			host->PageScreenManager()->push(new RetroAchievementsSettingsScreen(host->PageGamePath()));
		}).Setup([](View *view) {
			static_cast<Choice *>(view)->SetIconRight(ImageID("I_RETROACHIEVEMENTS_LOGO"));
		});
	}

	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	page->Choice({ I18NCat::SAVEDATA, "Savedata Manager" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new SavedataScreen(host->PageGamePath()));
	});
	page->Choice({ I18NCat::DEVELOPER, "System Information" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new SystemInfoScreen(host->PageGamePath()));
	});
	page->Link({ I18NCat::SYSTEM, "Developer Tools" }, I18NCat::DEVELOPER, &BuildDeveloperToolsPage);
	page->Choice({ I18NCat::REMOTEISO, "Remote disc streaming" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new RemoteISOScreen(host->PageGamePath()));
	});
	page->Choice({ I18NCat::SYSTEM, "PSP Firmware" }).OnClick([host](UI::EventParams &) {
		host->PageScreenManager()->push(new FirmwareScreen(host->PageGamePath()));
	});
}
