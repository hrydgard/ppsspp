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

#include "Common/Render/TextureAtlas.h"
#include "MiscScreens.h"

namespace UI {
	class CheckBox;
}

struct TouchButtonToggle {
	std::string key;
	bool *show;
	ImageID img;
	std::function<UI::EventReturn(UI::EventParams&)> handle;
};

class TouchControlVisibilityScreen : public UIDialogScreenWithGameBackground {
public:
	TouchControlVisibilityScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	void CreateViews() override;
	void onFinish(DialogResult result) override;

	const char *tag() const override { return "TouchControlVisibility"; }

protected:
	UI::EventReturn OnToggleAll(UI::EventParams &e);

private:
	std::vector<TouchButtonToggle> toggles_;
	bool nextToggleAll_ = true;
};

class RightAnalogMappingScreen : public UIDialogScreenWithGameBackground {
public:
	RightAnalogMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	void CreateViews() override;

	const char *tag() const override { return "RightAnalogMapping"; }
};
