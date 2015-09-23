// Copyright (c) 2012- PPSSPP Project.

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

#include "input/input_state.h"
#include "ui/ui.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/MIPS/JitCommon/NativeJit.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "UI/GamepadEmu.h"

#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/DarkFrostScreen.h"

static DarkFrostEngine *darkFrostEngine;

void DarkFrostScreen::init() {
	//initialize the class
	darkFrostEngine=new DarkFrostEngine();
	darkFrostEngine->setEngine(darkFrostEngine);
}

void DarkFrostScreen::CreateViews() {

	using namespace UI;
	I18NCategory *df = GetI18NCategory("DarkFrost");
	I18NCategory *di = GetI18NCategory("Dialog");

	init();

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));//new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	//MAIN PAGE

	//left menu
	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	TabHolder *tabHolder = new TabHolder(ORIENT_HORIZONTAL, FILL_PARENT, new AnchorLayoutParams(10, 0, 10, 0, false));
	leftColumn->Add(tabHolder);
	root_->Add(leftColumn);

	//tabs
	tabHolder->Add(new ItemHeader(df->T("Menu")));
	tabHolder->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	tabHolder->Add(new Choice(df->T("Cheater")))->OnClick.Handle(this, &DarkFrostScreen::OnCheater);
	tabHolder->Add(new Choice(df->T("Options")))->OnClick.Handle(this, &DarkFrostScreen::OnOptions);
	tabHolder->Add(new Choice(df->T("Searcher")))->OnClick.Handle(this, &DarkFrostScreen::OnSearcher);
	tabHolder->Add(new Choice(df->T("RAM")))->OnClick.Handle(this, &DarkFrostScreen::OnRAM);
	tabHolder->Add(new Choice(df->T("Decoder")))->OnClick.Handle(this, &DarkFrostScreen::OnDecoder);

	//right content
	ViewGroup *rightScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//rightScroll=rightColumn
	LinearLayout *rightContent = new LinearLayout(ORIENT_VERTICAL);
	rightContent->SetSpacing(0);
	rightScroll->Add(rightContent);
	root_->Add(rightScroll);

	//content
	rightContent->Add(new ItemHeader(df->T("DarkFrost for PPSSPP by demon450")));
	rightContent->Add(new TextView(df->T("- DarkFrost is a plugin for the psp ported to work on PPSSPP")));
	rightContent->Add(new TextView(df->T("- Based off of DarkFrost v6 BETA")));

	//CHEATER

	//OPTIONS PAGE

	//right content
	ViewGroup *optionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//optionsScroll=rightColumn
	LinearLayout *optionsContent = new LinearLayout(ORIENT_VERTICAL);
	optionsContent->SetSpacing(0);
	optionsScroll->Add(optionsContent);
	root_->Add(optionsScroll);

	//content
	optionsContent->Add(new ItemHeader(df->T("DarkFrost Options")));
	optionsContent->Add(new Checkbox(df->T("REAL Addressing")))->OnClick.Handle(this, &DarkFrostScreen::OnRealAddressing);//checkbox
	optionsContent->Add(new Choice(di->T("Load Cheats")))->OnClick.Handle(this, &DarkFrostScreen::OnLoadCheats);
	optionsContent->Add(new Choice(df->T("Save Cheats")))->OnClick.Handle(this, &DarkFrostScreen::OnSaveCheats);
	optionsContent->Add(new Choice(df->T("Reset Copier")))->OnClick.Handle(this, &DarkFrostScreen::OnResetCopier);
	//optionsContent->Add(new Checkbox(df->T("Cheats Activated")))->OnClick.Handle(this, &DarkFrostScreen::OnCheatsActivitated);//checkbox
	CheckBox *cheatsActivatedCB = optionsContent->Add(new CheckBox(darkFrostEngine->getCheatsEnabled(), df->T("Cheats Activitated")));
	//cheatsActivatedCB->SetDisabledPtr(darkFrostEngine->getCheatsEnabled());
}

UI::EventReturn DarkFrostScreen::OnCheater(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnOptions(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnSearcher(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnRAM(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnDecoder(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnRealAddressing(UI::EventParams &params) {
	darkFrostEngine->toggleRealAddressing();
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnLoadCheats(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnSaveCheats(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnResetCopier(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnCheatsActivitated(UI::EventParams &params) {
	//
	return UI::EVENT_DONE;
}