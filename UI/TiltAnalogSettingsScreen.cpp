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

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/TiltEventProcessor.h"

#include "Common/Math/math_util.h"
#include "Common/Log.h"
#include "Common/Data/Text/I18n.h"

#include "UI/JoystickHistoryView.h"
#include "UI/TiltAnalogSettingsScreen.h"

void TiltAnalogSettingsScreen::CreateViews() {
	using namespace UI;

	auto co = GetI18NCategory("Controls");
	auto di = GetI18NCategory("Dialog");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	root_->SetTag("TiltAnalogSettings");

	ScrollView *menuRoot = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(600, FILL_PARENT));
	root_->Add(menuRoot);

	// AnchorLayout *rightSide = new AnchorLayout(new LinearLayoutParams(1.0));
	tilt_ = new JoystickHistoryView(StickHistoryViewType::OTHER, "", new LinearLayoutParams(1.0f));
	root_->Add(tilt_);

	LinearLayout *settings = new LinearLayoutList(ORIENT_VERTICAL);

	settings->SetSpacing(0);

	settings->Add(new ItemHeader(co->T("Calibration")));
	InfoItem *calibrationInfo = new InfoItem(co->T("To Calibrate", "Keep device on a flat surface and press calibrate."), "");
	settings->Add(calibrationInfo);
	Choice *calibrate = new Choice(co->T("Calibrate"));
	calibrate->OnClick.Handle(this, &TiltAnalogSettingsScreen::OnCalibrate);
	settings->Add(calibrate);

	settings->Add(new ItemHeader(co->T("Invert Axes")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltX, co->T("Invert Tilt along X axis")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltY, co->T("Invert Tilt along Y axis")));

	settings->Add(new ItemHeader(co->T("Sensitivity")));
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityX, 0, 100, co->T("Tilt Sensitivity along X axis"), screenManager(), "%"));
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityY, 0, 100, co->T("Tilt Sensitivity along Y axis"), screenManager(), "%"));
	settings->Add(new PopupSliderChoiceFloat(&g_Config.fDeadzoneRadius, 0.0, 1.0, co->T("Deadzone radius"), 0.01f, screenManager(), "/ 1.0"));

	menuRoot->Add(settings);

	settings->Add(new BorderView(BORDER_BOTTOM, BorderStyle::HEADER_FG, 2.0f, new LayoutParams(FILL_PARENT, 40.0f)));
	settings->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void TiltAnalogSettingsScreen::axis(const AxisInput &axis) {
	UIDialogScreenWithGameBackground::axis(axis);

	if (axis.deviceId == DEVICE_ID_ACCELEROMETER) {
		switch (axis.axisId) {
		case JOYSTICK_AXIS_ACCELEROMETER_X: down_.x = axis.value; break;
		case JOYSTICK_AXIS_ACCELEROMETER_Y: down_.y = axis.value; break;
		case JOYSTICK_AXIS_ACCELEROMETER_Z: down_.z = axis.value; break;
		}
	}
}

UI::EventReturn TiltAnalogSettingsScreen::OnCalibrate(UI::EventParams &e) {
	Lin::Vec3 down = down_.normalized();
	g_Config.fTiltBaseAngleY = atan2(down.z, down.x);
	return UI::EVENT_DONE;
}

void TiltAnalogSettingsScreen::update() {
	UIDialogScreenWithGameBackground::update();
	tilt_->SetXY(
		Clamp(TiltEventProcessor::rawTiltAnalogX, -1.0f, 1.0f),
		Clamp(TiltEventProcessor::rawTiltAnalogY, -1.0f, 1.0f));
}
