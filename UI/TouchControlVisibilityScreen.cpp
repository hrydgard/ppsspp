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

#include "TouchControlVisibilityScreen.h"
#include "Core/Config.h"
#include "UI/ui_atlas.h"
#include "i18n/i18n.h"

void TouchControlVisibilityScreen::CreateViews() {
	using namespace UI;

	root_ = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL);
	I18NCategory *di = GetI18NCategory("Dialog");
	topBar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topBar->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &TouchControlVisibilityScreen::OnToggleAll);

	vert->Add(topBar);
	I18NCategory *co = GetI18NCategory("Controls");
	vert->Add(new ItemHeader(co->T("Touch Control Visibility")));

	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	toggles_.clear();
	toggles_.push_back({ "Circle", &g_Config.bShowTouchCircle, I_CIRCLE });
	toggles_.push_back({ "Cross", &g_Config.bShowTouchCross, I_CROSS });
	toggles_.push_back({ "Square", &g_Config.bShowTouchSquare, I_SQUARE });
	toggles_.push_back({ "Triangle", &g_Config.bShowTouchTriangle, I_TRIANGLE });
	toggles_.push_back({ "L", &g_Config.touchLKey.show, I_L });
	toggles_.push_back({ "R", &g_Config.touchRKey.show, I_R });
	toggles_.push_back({ "Start", &g_Config.touchStartKey.show, I_START });
	toggles_.push_back({ "Select", &g_Config.touchSelectKey.show, I_SELECT });
	toggles_.push_back({ "Dpad", &g_Config.touchDpad.show, -1 });
	toggles_.push_back({ "Analog Stick", &g_Config.touchAnalogStick.show, -1 });
	toggles_.push_back({ "Unthrottle", &g_Config.touchUnthrottleKey.show, -1 });
	toggles_.push_back({ "Combo0", &g_Config.touchCombo0.show, I_1 });
	toggles_.push_back({ "Combo1", &g_Config.touchCombo1.show, I_2 });
	toggles_.push_back({ "Combo2", &g_Config.touchCombo2.show, I_3 });
	toggles_.push_back({ "Combo3", &g_Config.touchCombo3.show, I_4 });
	toggles_.push_back({ "Combo4", &g_Config.touchCombo4.show, I_5 });
	toggles_.push_back({ "Alt speed 1", &g_Config.touchSpeed1Key.show, -1 });
	toggles_.push_back({ "Alt speed 2", &g_Config.touchSpeed2Key.show, -1 });

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto toggle : toggles_) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(toggle.show, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		Choice *choice;
		if (toggle.img != -1) {
			choice = new Choice(toggle.img, new LinearLayoutParams(1.0f));
		} else {
			choice = new Choice(mc->T(toggle.key), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);
		
		row->Add(choice);
		grid->Add(row);
	}
}

void TouchControlVisibilityScreen::onFinish(DialogResult result) {
	g_Config.Save();
}

UI::EventReturn TouchControlVisibilityScreen::OnToggleAll(UI::EventParams &e) {
	for (auto toggle : toggles_) {
		*toggle.show = nextToggleAll_;
	}
	nextToggleAll_ = !nextToggleAll_;

	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlVisibilityScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};
