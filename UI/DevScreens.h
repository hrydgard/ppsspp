// Copyright (c) 2013- PPSSPP Project.

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

#include <vector>
#include <map>
#include <string>

#include "base/functional.h"
#include "file/file_util.h"
#include "ui/ui_screen.h"

#include "UI/MiscScreens.h"

class DevMenu : public PopupScreen {
public:
	DevMenu() : PopupScreen("Dev Tools") {}

	virtual void CreatePopupContents(UI::ViewGroup *parent);

	virtual void dialogFinished(const Screen *dialog, DialogResult result);

protected:
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
};

class LogConfigScreen : public UIDialogScreenWithBackground {
public:
	LogConfigScreen() {}
	virtual void CreateViews();

private:
	UI::EventReturn OnToggleAll(UI::EventParams &e);
};

class SystemInfoScreen : public UIDialogScreenWithBackground {
public:
	SystemInfoScreen() {}
	virtual void CreateViews();
};