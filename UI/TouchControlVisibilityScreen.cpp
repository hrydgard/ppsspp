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
	//topBar->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &TouchControlVisibilityScreen::OnToggleAll);

	vert->Add(topBar);
	I18NCategory *co = GetI18NCategory("Controls");
	vert->Add(new ItemHeader(co->T("Touch Control Visibility")));

	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["Start"] = I_START;
	keyImages["Select"] = I_SELECT;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;
	keyImages["Combo"] = g_Config.iComboButtonStyle ? I_SQUARE1 : I_STAR;
	keyImages["Combo1"] = g_Config.iComboButtonStyle ? I_TRIANGLE1 : I_EYE;
	keyImages["Combo2"] = g_Config.iComboButtonStyle ? I_CROSS1 : I_GC;
	keyImages["LEFT"] = I_ARROW;
	keyImages["UP"] = I_ARROW1;
	keyImages["RIGHT"] = I_ARROW2;
	keyImages["DOWN"] = I_ARROW3;


	keyToggles["Circle"] = &g_Config.bShowTouchCircle;
	keyToggles["Cross"] = &g_Config.bShowTouchCross;
	keyToggles["Square"] = &g_Config.bShowTouchSquare;
	keyToggles["Triangle"] = &g_Config.bShowTouchTriangle;
	keyToggles["L"] = &g_Config.bShowTouchLTrigger;
	keyToggles["R"] = &g_Config.bShowTouchRTrigger;
	keyToggles["Start"] = &g_Config.bShowTouchStart;
	keyToggles["Select"] = &g_Config.bShowTouchSelect;
	keyToggles["Dpad"] = &g_Config.bShowTouchDpad;
	keyToggles["Analog Stick"] = &g_Config.bShowTouchAnalogStick;
	keyToggles["Unthrottle"] = &g_Config.bShowTouchUnthrottle;
	keyToggles["Combo"] = &g_Config.bShowComboKey;
	keyToggles["Combo1"] = &g_Config.bShowComboKey1;
	keyToggles["Combo2"] = &g_Config.bShowComboKey2;

	keyToggles["LEFT"] = &g_Config.bShowTouchDpad_LEFT;
	keyToggles["UP"] = &g_Config.bShowTouchDpad_UP;
	keyToggles["RIGHT"] = &g_Config.bShowTouchDpad_RIGHT;
	keyToggles["DOWN"] = &g_Config.bShowTouchDpad_DOWN;

	std::map<std::string, int>::iterator imageFinder;

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(i->second, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		imageFinder = keyImages.find(i->first);
		Choice *choice;

		if (imageFinder != keyImages.end()) {
			choice = new Choice(keyImages[imageFinder->first], new LinearLayoutParams(1.0f));	
		} else {
			choice = new Choice(mc->T(i->first.c_str()), new LinearLayoutParams(1.0f));
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
	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		*i->second = toggleSwitch;
	}

	toggleSwitch = !toggleSwitch;

	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlVisibilityScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};

void Combo_keyScreen::CreateViews() {
	using namespace UI;

	root_ = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL);
	I18NCategory *di = GetI18NCategory("Dialog");
	topBar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);



	vert->Add(topBar);
	I18NCategory *co = GetI18NCategory("Controls");
	vert->Add(new ItemHeader(co->T("Combo Key")));

	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;
	keyImages["ULeft"] = I_ARROW;
	keyImages["VUp"] = I_ARROW1;
	keyImages["WRight"] = I_ARROW2;
	keyImages["XDown"] = I_ARROW3;






	keyToggles["Circle"] = &g_Config.cComboCircle;
	keyToggles["Cross"] = &g_Config.cComboCross;
	keyToggles["Square"] = &g_Config.cComboSquare;
	keyToggles["Triangle"] = &g_Config.cComboTriangle;
	keyToggles["L"] = &g_Config.cComboLTrigger;
	keyToggles["R"] = &g_Config.cComboRTrigger;
	keyToggles["ULeft"] = &g_Config.cComboLeft;
	keyToggles["VUp"] = &g_Config.cComboUp;
	keyToggles["WRight"] = &g_Config.cComboRight;
	keyToggles["XDown"] = &g_Config.cComboDown;


	std::map<std::string, int>::iterator imageFinder;

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(i->second, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		imageFinder = keyImages.find(i->first);
		Choice *choice;

		if (imageFinder != keyImages.end()) {
			choice = new Choice(keyImages[imageFinder->first], new LinearLayoutParams(1.0f));
		}
		else {
			choice = new Choice(mc->T(i->first.c_str()), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);

		row->Add(choice);
		grid->Add(row);
	}
}

void Combo_keyScreen::onFinish(DialogResult result) {
	g_Config.Save();
}



UI::EventReturn Combo_keyScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};

void Combo_keyScreen1::CreateViews() {
	using namespace UI;

	root_ = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL);
	I18NCategory *di = GetI18NCategory("Dialog");
	topBar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);



	vert->Add(topBar);
	I18NCategory *co = GetI18NCategory("Controls");
	vert->Add(new ItemHeader(co->T("Combo Key 1")));

	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;
	keyImages["ULeft"] = I_ARROW;
	keyImages["VUp"] = I_ARROW1;
	keyImages["WRight"] = I_ARROW2;
	keyImages["XDown"] = I_ARROW3;






	keyToggles["Circle"] = &g_Config.cComboCircle1;
	keyToggles["Cross"] = &g_Config.cComboCross1;
	keyToggles["Square"] = &g_Config.cComboSquare1;
	keyToggles["Triangle"] = &g_Config.cComboTriangle1;
	keyToggles["L"] = &g_Config.cComboLTrigger1;
	keyToggles["R"] = &g_Config.cComboRTrigger1;
	keyToggles["ULeft"] = &g_Config.cComboLeft1;
	keyToggles["VUp"] = &g_Config.cComboUp1;
	keyToggles["WRight"] = &g_Config.cComboRight1;
	keyToggles["XDown"] = &g_Config.cComboDown1;


	std::map<std::string, int>::iterator imageFinder;

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(i->second, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		imageFinder = keyImages.find(i->first);
		Choice *choice;

		if (imageFinder != keyImages.end()) {
			choice = new Choice(keyImages[imageFinder->first], new LinearLayoutParams(1.0f));
		}
		else {
			choice = new Choice(mc->T(i->first.c_str()), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);

		row->Add(choice);
		grid->Add(row);
	}
}

void Combo_keyScreen1::onFinish(DialogResult result) {
	g_Config.Save();
}



UI::EventReturn Combo_keyScreen1::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};

void Combo_keyScreen2::CreateViews() {
	using namespace UI;

	root_ = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	vert->SetSpacing(0);

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL);
	I18NCategory *di = GetI18NCategory("Dialog");
	topBar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);



	vert->Add(topBar);
	I18NCategory *co = GetI18NCategory("Controls");
	vert->Add(new ItemHeader(co->T("Combo Key 2")));

	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;
	keyImages["ULeft"] = I_ARROW;
	keyImages["VUp"] = I_ARROW1;
	keyImages["WRight"] = I_ARROW2;
	keyImages["XDown"] = I_ARROW3;






	keyToggles["Circle"] = &g_Config.cComboCircle2;
	keyToggles["Cross"] = &g_Config.cComboCross2;
	keyToggles["Square"] = &g_Config.cComboSquare2;
	keyToggles["Triangle"] = &g_Config.cComboTriangle2;
	keyToggles["L"] = &g_Config.cComboLTrigger2;
	keyToggles["R"] = &g_Config.cComboRTrigger2;
	keyToggles["ULeft"] = &g_Config.cComboLeft2;
	keyToggles["VUp"] = &g_Config.cComboUp2;
	keyToggles["WRight"] = &g_Config.cComboRight2;
	keyToggles["XDown"] = &g_Config.cComboDown2;


	std::map<std::string, int>::iterator imageFinder;

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(i->second, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		imageFinder = keyImages.find(i->first);
		Choice *choice;

		if (imageFinder != keyImages.end()) {
			choice = new Choice(keyImages[imageFinder->first], new LinearLayoutParams(1.0f));
		}
		else {
			choice = new Choice(mc->T(i->first.c_str()), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);

		row->Add(choice);
		grid->Add(row);
	}
}

void Combo_keyScreen2::onFinish(DialogResult result) {
	g_Config.Save();
}



UI::EventReturn Combo_keyScreen2::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();

	return UI::EVENT_DONE;
};