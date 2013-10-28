#include "TiltAnalogSettingsScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "i18n/i18n.h"

TiltAnalogSettingsScreen::TiltAnalogSettingsScreen() : currentTiltX_(0), currentTiltY_(0) {};

void TiltAnalogSettingsScreen::CreateViews(){
	using namespace UI;

	I18NCategory *c = GetI18NCategory("Controls");

	root_ = root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *settings = new LinearLayout(ORIENT_VERTICAL);
	settings->SetSpacing(0);
	

	settings->Add(new ItemHeader(c->T("Invert Axes")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltX, c->T("Invert Tilt along X axis")));
	settings->Add(new CheckBox(&g_Config.bInvertTiltY, c->T("Invert Tilt along Y axis")));

	settings->Add(new ItemHeader(c->T("Sensitivity")));
	//TODO: allow values greater than 100? I'm not sure if that's needed.
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityX, 0, 100, c->T("Tilt Sensitivity along X axis"), screenManager()));
	settings->Add(new PopupSliderChoice(&g_Config.iTiltSensitivityY, 0, 100, c->T("Tilt Sensitivity along Y axis"), screenManager()));
	settings->Add(new PopupSliderChoiceFloat(&g_Config.fDeadzoneRadius, 0.0, 1.0, c->T("Deadzone Radius"), screenManager()));
	

	settings->Add(new ItemHeader(c->T("Calibration")));
	InfoItem *calibrationInfo = new InfoItem("To calibrate, keep device on a flat surface and press calibrate.", "");
	settings->Add(calibrationInfo);

	Choice *calibrate = new Choice(c->T("Calibrate D-Pad"));
	calibrate->OnClick.Handle(this, &TiltAnalogSettingsScreen::OnCalibrate);
	settings->Add(calibrate);

	root_->Add(settings);
};

void TiltAnalogSettingsScreen::update(InputState &input){
	UIScreen::update(input);
	//I'm not sure why y is x and x is y. i's probably because of the orientation
	//of the screen (the x and y are in portrait coordinates). once portrait and 
	//reverse-landscape is enabled, this will probably have to change.
	//If needed, we can add a "swap x and y" option. 
	currentTiltX_ = input.acc.y;
	currentTiltY_ = input.acc.x;
};


UI::EventReturn TiltAnalogSettingsScreen::OnBack(UI::EventParams &e){
	if (PSP_IsInited()) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else {
		screenManager()->finishDialog(this, DR_OK);
	}

	return UI::EVENT_DONE;
};


UI::EventReturn TiltAnalogSettingsScreen::OnCalibrate(UI::EventParams &e){
	g_Config.fTiltBaseX = currentTiltX_;
	g_Config.fTiltBaseY = currentTiltY_;

	return UI::EVENT_DONE;
};

