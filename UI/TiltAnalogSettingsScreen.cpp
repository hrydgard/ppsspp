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

#include "TiltAnalogSettingsScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "i18n/i18n.h"

void TiltAnalogSettingsScreen::CreateViews() {
	using namespace UI;

	I18NCategory *co = GetI18NCategory("Controls");
	I18NCategory *di = GetI18NCategory("Dialog");

	root_ = new ScrollView(ORIENT_VERTICAL);
	root_->SetTag("TiltAnalogSettings");

	LinearLayout *settings = new LinearLayout(ORIENT_VERTICAL);

	settings->SetSpacing(0);
	settings->Add(new ItemHeader(co->T("Invert Axes")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltX, co->T("Invert Tilt along X axis")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltY, co->T("Invert Tilt along Y axis")));

	settings->Add(new ItemHeader(co->T("Sensitivity")));
	//TODO: allow values greater than 100? I'm not sure if that's needed.
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityX, 0, 100, co->T("Tilt Sensitivity along X axis"), screenManager(),"%"));
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityY, 0, 100, co->T("Tilt Sensitivity along Y axis"), screenManager(),"%"));
	settings->Add(new PopupSliderChoiceFloat(&g_Config.fDeadzoneRadius, 0.0, 1.0, co->T("Deadzone Radius"), 0.01f, screenManager(),"/ 1.0"));

	settings->Add(new ItemHeader(co->T("Calibration")));
	InfoItem *calibrationInfo = new InfoItem(co->T("To Calibrate", "To calibrate, keep device on a flat surface and press calibrate."), "");
	settings->Add(calibrationInfo);

	Choice *calibrate = new Choice(co->T("Calibrate D-Pad"));
	calibrate->OnClick.Handle(this, &TiltAnalogSettingsScreen::OnCalibrate);
	settings->Add(calibrate);

	root_->Add(settings);
	settings->Add(new ItemHeader(""));
	settings->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void TiltAnalogSettingsScreen::update(InputState &input) {
	UIScreen::update(input);
	//I'm not sure why y is x and x is y. i's probably because of the orientation
	//of the screen (the x and y are in portrait coordinates). once portrait and 
	//reverse-landscape is enabled, this will probably have to change.
	//If needed, we can add a "swap x and y" option. 
	currentTiltX_ = input.acc.y;
	currentTiltY_ = input.acc.x;
}

UI::EventReturn TiltAnalogSettingsScreen::OnCalibrate(UI::EventParams &e) {
	g_Config.fTiltBaseX = currentTiltX_;
	g_Config.fTiltBaseY = currentTiltY_;

	return UI::EVENT_DONE;
}

