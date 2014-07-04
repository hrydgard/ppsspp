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
#include "base/functional.h"
#include "ui/view.h"
#include "ui/ui_screen.h"

#include "UI/MiscScreens.h"

class ControlMapper;

class ControlMappingScreen : public UIDialogScreenWithBackground {
public:
	ControlMappingScreen() {}
	void KeyMapped(int pspkey);  // Notification to let us refocus the same one after recreating views.
protected:
	virtual void CreateViews() override;
	virtual void sendMessage(const char *message, const char *value) override;
private:
	UI::EventReturn OnDefaultMapping(UI::EventParams &params);
	UI::EventReturn OnClearMapping(UI::EventParams &params);
	UI::EventReturn OnAutoConfigure(UI::EventParams &params);

	virtual void dialogFinished(const Screen *dialog, DialogResult result) override;

	UI::ScrollView *rightScroll_;
	std::vector<ControlMapper *> mappers_;
};

class KeyMappingNewKeyDialog : public PopupScreen {
public:
	explicit KeyMappingNewKeyDialog(int btn, bool replace, std::function<void(KeyDef)> callback)
		: PopupScreen("Map Key", "Cancel", ""), callback_(callback), mapped_(false) {
		pspBtn_ = btn;
	}

	virtual bool key(const KeyInput &key) override;
	virtual bool axis(const AxisInput &axis) override;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;

	virtual bool FillVertical() const override { return false; }
	virtual bool ShowButtons() const override { return true; }
	virtual void OnCompleted(DialogResult result) override {}

private:
	int pspBtn_;
	bool replace_;
	std::function<void(KeyDef)> callback_;
	bool mapped_;  // Prevent double registrations
};
