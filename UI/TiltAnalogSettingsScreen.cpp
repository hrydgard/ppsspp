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
#include "Core/ConfigValues.h"

#include "Common/Math/math_util.h"
#include "Common/Log.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"

#include "UI/JoystickHistoryView.h"
#include "UI/GamepadEmu.h"
#include "UI/TiltAnalogSettingsScreen.h"

const char *g_tiltTypes[] = { "None (Disabled)", "Analog Stick", "D-PAD", "PSP Action Buttons", "L/R Trigger Buttons" };
const size_t g_numTiltTypes = ARRAY_SIZE(g_tiltTypes);

void TiltAnalogSettingsScreen::CreateCalibrationView(UI::ViewGroup *parent, UI::LayoutParams *layoutParams) {
	using namespace UI;

	if (g_Config.iTiltInputType == TILT_ANALOG) {
		tilt_ = new JoystickHistoryView(StickHistoryViewType::OTHER, "", layoutParams);
		parent->Add(tilt_);
	} else {
		tilt_ = nullptr;
		switch (g_Config.iTiltInputType) {
		case TILT_DPAD:
		{
			parent->Add(new PSPDpad(ImageID("I_DIR_LINE"), "D-pad", ImageID("I_DIR_LINE"), ImageID("I_ARROW"), 1.5f, 1.3f, new AnchorLayoutParams(NONE, NONE, NONE, NONE, Centering::Both)));
			break;
		}
		case TILT_ACTION_BUTTON:
		{
			AnchorLayout *rightSide = new AnchorLayout(layoutParams);
			parent->Add(rightSide);

			PSPButton *circle = new PSPButton(CTRL_CIRCLE, "Circle button", ImageID("I_ROUND_LINE"), ImageID("I_ROUND"), ImageID("I_CIRCLE"), 1.5f, new AnchorLayoutParams(NONE, NONE, 100.0f, NONE, Centering::Both));
			PSPButton *cross = new PSPButton(CTRL_CROSS, "Cross button", ImageID("I_ROUND_LINE"), ImageID("I_ROUND"), ImageID("I_CROSS"), 1.5f, new AnchorLayoutParams(NONE, NONE, NONE, 100.0f, Centering::Both));
			PSPButton *triangle = new PSPButton(CTRL_TRIANGLE, "Triangle button", ImageID("I_ROUND_LINE"), ImageID("I_ROUND"), ImageID("I_TRIANGLE"), 1.5f, new AnchorLayoutParams(NONE, 100.0f, NONE, NONE, Centering::Both));
			PSPButton *square = new PSPButton(CTRL_SQUARE, "Square button", ImageID("I_ROUND_LINE"), ImageID("I_ROUND"), ImageID("I_SQUARE"), 1.5f, new AnchorLayoutParams(100.0f, NONE, NONE, NONE, Centering::Both));
			rightSide->Add(circle);
			rightSide->Add(cross);
			rightSide->Add(triangle);
			rightSide->Add(square);
			break;
		}
		case TILT_TRIGGER_BUTTONS:
		{
			AnchorLayout *rightSide = new AnchorLayout(layoutParams);
			parent->Add(rightSide);

			PSPButton *lTrigger = new PSPButton(CTRL_LTRIGGER, "Left shoulder button", ImageID("I_SHOULDER_LINE"), ImageID("I_SHOULDER"), ImageID("I_L"), 1.5f, new AnchorLayoutParams(100.0f, NONE, NONE, NONE, Centering::Both));
			PSPButton *rTrigger = new PSPButton(CTRL_RTRIGGER, "Right shoulder button", ImageID("I_SHOULDER_LINE"), ImageID("I_SHOULDER"), ImageID("I_R"), 1.5f, new AnchorLayoutParams(NONE, NONE, 100.0f, NONE, Centering::Both));
			rTrigger->FlipImageH(true);
			rightSide->Add(lTrigger);
			rightSide->Add(rTrigger);
			break;
		}
		}
	}
}

std::string_view TiltAnalogSettingsScreen::GetTitle() const {
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	return co->T("Tilt control setup");
}

void TiltAnalogSettingsScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	CreateCalibrationView(parent, new LinearLayoutParams(300.0f, 300.0f, 1.0f, Gravity::G_CENTER));
}

void TiltAnalogSettingsScreen::CreateSettingsViews(UI::ViewGroup *settings) {
	using namespace UI;
	auto co = GetI18NCategory(I18NCat::CONTROLS);

	GamepadUpdateOpacity(1.0f);

	auto enabledFunc = [=]() -> bool {
		return g_Config.iTiltInputType != 0;
	};

	settings->Add(new ItemHeader(co->T("Tilt control setup")));
	CheckBox *enabled = settings->Add(new CheckBox(&g_Config.bTiltInputEnabled, co->T("Enabled")));
	PopupMultiChoice *typeChoice = settings->Add(new PopupMultiChoice(&g_Config.iTiltInputType, co->T("Tilt Input Type"), g_tiltTypes, 0, g_numTiltTypes, I18NCat::CONTROLS, screenManager()));
	if (g_Config.iTiltInputType != 0) {
		typeChoice->HideChoice(0);   // The null choice is no longer relevant since we added an enabled toggle
	}
	typeChoice->OnChoice.Add(
		[this](UI::EventParams &p) {
			//when the tilt event type is modified, we need to reset all tilt settings.
			//refer to the ResetTiltEvents() function for a detailed explanation.
			TiltEventProcessor::ResetTiltEvents();
			RecreateViews();
		});
	typeChoice->SetEnabledPtr(&g_Config.bTiltInputEnabled);
	settings->Add(new ItemHeader(co->T("Calibration")));
	TextView *calibrationInfo = new TextView(co->T("To Calibrate", "Hold device at your preferred angle and press Calibrate."));
	calibrationInfo->SetSmall(true);
	calibrationInfo->SetPadding(Margins(5));
	settings->Add(calibrationInfo);
	Choice *calibrate = new Choice(co->T("Calibrate"));
	calibrate->OnClick.Handle(this, &TiltAnalogSettingsScreen::OnCalibrate);
	calibrate->SetEnabledPtr(&g_Config.bTiltInputEnabled);
	settings->Add(calibrate);

	settings->Add(new ItemHeader(co->T("Sensitivity")));
	if (g_Config.iTiltInputType == 1) {
		settings->Add(new PopupSliderChoiceFloat(&g_Config.fTiltAnalogDeadzoneRadius, 0.0f, 0.8f, 0.0f, co->T("Deadzone radius"), 0.02f, screenManager(), "/ 1.0"))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
		settings->Add(new PopupSliderChoiceFloat(&g_Config.fTiltInverseDeadzone, 0.0f, 0.8f, 0.0f, co->T("Low end radius"), 0.02f, screenManager(), "/ 1.0"))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
		settings->Add(new CheckBox(&g_Config.bTiltCircularDeadzone, co->T("Circular deadzone")))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
	}
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityX, 0, 100, 60, co->T("Tilt Sensitivity along X axis"), screenManager(), "%"))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityY, 0, 100, 60, co->T("Tilt Sensitivity along Y axis"), screenManager(), "%"))->SetEnabledPtr(&g_Config.bTiltInputEnabled);

	settings->Add(new ItemHeader(co->T("Invert Axes")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltX, co->T("Invert Tilt along X axis")))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
	settings->Add(new CheckBox(&g_Config.bInvertTiltY, co->T("Invert Tilt along Y axis")))->SetEnabledPtr(&g_Config.bTiltInputEnabled);
}

void TiltAnalogSettingsScreen::OnCalibrate(UI::EventParams &e) {
	g_Config.fTiltBaseAngleY = TiltEventProcessor::GetCurrentYAngle();
}

void TiltAnalogSettingsScreen::update() {
	UITwoPaneBaseDialogScreen::update();
	if (tilt_) {
		tilt_->SetXY(
			Clamp(TiltEventProcessor::rawTiltAnalogX, -1.0f, 1.0f),
			Clamp(TiltEventProcessor::rawTiltAnalogY, -1.0f, 1.0f));
	}
}
