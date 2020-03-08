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
#include "i18n/i18n.h"
#include "ComboKeyMappingScreen.h"
#include "base/colorutil.h"
#include "base/display.h"
#include "base/timeutil.h"
#include "file/path.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx/texture_atlas.h"
#include "math/curves.h"
#include "base/stringutil.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

void ComboKeyScreen::CreateViews() {
	using namespace UI;
	auto co = GetI18NCategory("Controls");
	root_ = new LinearLayout(ORIENT_VERTICAL);
	root_->Add(new ItemHeader(co->T("Combo Key Setting")));
	LinearLayout *root__ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0));
	root_->Add(root__);
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(120, FILL_PARENT));
	auto di = GetI18NCategory("Dialog");

	static const ImageID comboKeyImages[5] = {
		ImageID("I_1"), ImageID("I_2"), ImageID("I_3"), ImageID("I_4"), ImageID("I_5"),
	};

	comboselect = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(10, 10, NONE, NONE));
	comboselect->SetSpacing(10);
	for (int i = 0; i < 5; i++) {
		comboselect->AddChoice(comboKeyImages[i]);
	}
	comboselect->SetSelection(*mode);
	comboselect->OnChoice.Handle(this, &ComboKeyScreen::onCombo);
	leftColumn->Add(comboselect);
	root__->Add(leftColumn);
	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root__->Add(rightScroll_);
	
	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = rightScroll_->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	bool *toggle = nullptr;
	memset(array, 0, sizeof(array));
	switch (*mode) {
	case 0: 
		toggle = &g_Config.bComboToggle0;
		for (int i = 0; i < 16; i++)
			array[i] = (0x01 == ((g_Config.iCombokey0 >> i) & 0x01));
		break;
	case 1:
		toggle = &g_Config.bComboToggle1;
		for (int i = 0; i < 16; i++)
			array[i] = (0x01 == ((g_Config.iCombokey1 >> i) & 0x01));
		break;
	case 2:
		toggle = &g_Config.bComboToggle2;
		for (int i = 0; i < 16; i++)
			array[i] = (0x01 == ((g_Config.iCombokey2 >> i) & 0x01));
		break;
	case 3:
		toggle = &g_Config.bComboToggle3;
		for (int i = 0; i < 16; i++)
			array[i] = (0x01 == ((g_Config.iCombokey3 >> i) & 0x01));
		break;
	case 4:
		toggle = &g_Config.bComboToggle4;
		for (int i = 0; i < 16; i++)
			array[i] = (0x01 == ((g_Config.iCombokey4 >> i) & 0x01));
		break;
	default:
		// This shouldn't happen, let's just not crash.
		toggle = &g_Config.bComboToggle0;
		break;
	}

	std::map<std::string, ImageID> keyImages;
	keyImages["Circle"] = ImageID("I_CIRCLE");
	keyImages["Cross"] = ImageID("I_CROSS");
	keyImages["Square"] = ImageID("I_SQUARE");
	keyImages["Triangle"] = ImageID("I_TRIANGLE");
	keyImages["L"] = ImageID("I_L");
	keyImages["R"] = ImageID("I_R");
	keyImages["Start"] = ImageID("I_START");
	keyImages["Select"] = ImageID("I_SELECT");
	keyToggles["Circle"] = &array[13];
	keyToggles["Cross"] = &array[14];
	keyToggles["Square"] = &array[15];
	keyToggles["Triangle"] = &array[12];
	keyToggles["L"] = &array[8];
	keyToggles["R"] = &array[9];
	keyToggles["Left"] = &array[7];
	keyToggles["Up"] = &array[4];
	keyToggles["Right"] = &array[5];
	keyToggles["Down"] = &array[6];
	keyToggles["Start"] = &array[3];
	keyToggles["Select"] = &array[0];

	std::map<std::string, ImageID>::iterator imageFinder;

	auto mc = GetI18NCategory("MappableControls");

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

	LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	row->SetSpacing(0);

	CheckBox *checkbox = new CheckBox(toggle, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
	row->Add(checkbox);

	Choice *choice = new Choice(mc->T("Toggle mode"), new LinearLayoutParams(1.0f));
	ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
	choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);
	choice->SetCentered(true);

	row->Add(choice);
	grid->Add(row);
}

static int arrayToInt(bool ary[16]) {
	int value = 0;
	for (int i = 15; i >= 0; i--) {
		value |= ary[i] ? 1 : 0;
		value = value << 1;
	}
	return value >> 1;
}

void ComboKeyScreen::onFinish(DialogResult result) {
	switch (*mode) {
	case 0:
		g_Config.iCombokey0 = arrayToInt(array);
		break;
	case 1:
		g_Config.iCombokey1 = arrayToInt(array);
		break;
	case 2:
		g_Config.iCombokey2 = arrayToInt(array);
		break;
	case 3:
		g_Config.iCombokey3 = arrayToInt(array);
		break;
	case 4:
		g_Config.iCombokey4 = arrayToInt(array);
		break;
	}
	g_Config.Save("ComboKeyScreen::onFInish");
}

UI::EventReturn ComboKeyScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();


	return UI::EVENT_DONE;
};

UI::EventReturn ComboKeyScreen::onCombo(UI::EventParams &e) {
	switch (*mode){
	case 0:g_Config.iCombokey0 = arrayToInt(array);
		break;
	case 1:g_Config.iCombokey1 = arrayToInt(array);
		break;
	case 2:g_Config.iCombokey2 = arrayToInt(array);
		break;
	case 3:g_Config.iCombokey3 = arrayToInt(array);
		break;
	case 4:g_Config.iCombokey4 = arrayToInt(array);
	}
	*mode = comboselect->GetSelection();
	CreateViews();
	return UI::EVENT_DONE;
}


