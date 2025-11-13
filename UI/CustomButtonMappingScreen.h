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

#include "UI/BaseScreens.h"
#include "UI/GamepadEmu.h"

namespace UI {
	class CheckBox;
}

class CustomButtonMappingScreen : public UIBaseDialogScreen {
public:
	CustomButtonMappingScreen(DeviceOrientation deviceOrientation, const Path &gamePath, int id) : UIBaseDialogScreen(gamePath), deviceOrientation_(deviceOrientation), id_(id) {}

	const char *tag() const override { return "CustomButton"; }

	void CreateViews() override;
	void onFinish(DialogResult result) override;

protected:
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	std::string_view GetTitle() const;
private:
	void saveArray();

	bool array[ARRAY_SIZE(CustomKeyData::g_customKeyList)]{};
	int id_;

	class ChoiceEventHandler{
	public:
		ChoiceEventHandler(UI::CheckBox *checkbox) : checkbox_(checkbox) {}
		void onChoiceClick(UI::EventParams &e);
	private:
		UI::CheckBox *checkbox_;
	};

	DeviceOrientation deviceOrientation_;
};
