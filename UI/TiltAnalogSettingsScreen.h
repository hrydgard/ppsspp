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

#include "Common/Math/math_util.h"
#include "Common/UI/View.h"
#include "SimpleDialogScreen.h"

class JoystickHistoryView;
class GamepadComponent;

class TiltAnalogSettingsScreen : public UITwoPaneBaseDialogScreen {
public:
	TiltAnalogSettingsScreen(const Path &gamePath) : UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::SettingsCanScroll) {}

	void CreateSettingsViews(UI::ViewGroup *parent) override;
	void CreateContentViews(UI::ViewGroup*parent) override;
	std::string_view GetTitle() const override;

	void update() override;
	const char *tag() const override { return "TiltAnalogSettings"; }

private:
	void OnCalibrate(UI::EventParams &e);
	void CreateCalibrationView(UI::ViewGroup *parent, UI::LayoutParams *layoutParams);

	Lin::Vec3 down_{};
	JoystickHistoryView *tilt_ = nullptr;
};

extern const char *g_tiltTypes[];
extern const size_t g_numTiltTypes;
