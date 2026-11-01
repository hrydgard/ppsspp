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

#pragma once

#include "ppsspp_config.h"

namespace UI {
class View;
class ViewGroup;
}

class SettingsPage;
class SettingsPageHost;

// The settings screen's tabs.
void BuildGraphicsSettings(SettingsPage *page);
void BuildControlsSettings(SettingsPage *page);
void BuildAudioSettings(SettingsPage *page);
void BuildNetworkingSettings(SettingsPage *page);
void BuildToolsSettings(SettingsPage *page);
void BuildVRSettings(SettingsPage *page);

// What the General and System tabs edit that isn't a config setting. The settings screen keeps it, and
// writes enableReports back when it closes.
struct SystemPageState {
	bool enableReports = false;
	// Windows: where the memstick is, and which of the two checkboxes can be changed.
	bool installed = false;
	bool otherinstalled = false;
	bool memstickMyDocEnabled = false;
	bool memstickOtherEnabled = false;
};

void BuildGeneralSettings(SettingsPage *page, SystemPageState *state);
void BuildSystemSettings(SettingsPage *page, SystemPageState *state);
// Also called after restoring defaults, so the old values aren't written back over them.
void ReadSystemPageState(SystemPageState *state);
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
void CheckMemstickInstallState(SystemPageState *state);
#endif

// The nickname is shown on both the Networking and System tabs.
UI::View *AddNicknameChoice(SettingsPageHost *host, UI::ViewGroup *parent);
