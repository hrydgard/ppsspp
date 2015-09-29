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
#include "UI/DFExtMenu.h"
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
	I18NCategory *ch = GetI18NCategory("Cheater");
	I18NCategory *opt = GetI18NCategory("Options");
	I18NCategory *sr = GetI18NCategory("Searcher");
	I18NCategory *ram = GetI18NCategory("RAM");
	I18NCategory *de = GetI18NCategory("Decoder");
	I18NCategory *ab = GetI18NCategory("About");

	init();

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));//new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	//MAIN PAGE

	//left menu
	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	TabHolder *tabHolder = new TabHolder(ORIENT_HORIZONTAL, FILL_PARENT, new AnchorLayoutParams(10, 0, 10, 0, false));
	leftColumn->Add(tabHolder);
	root_->Add(leftColumn);

	//CHEATER
	ViewGroup *cheaterScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//cheaterScroll=rightColumn
	LinearLayout *cheaterContent = new LinearLayout(ORIENT_VERTICAL);
	cheaterContent->SetSpacing(0);
	cheaterScroll->Add(cheaterContent);
	tabHolder->AddTab(df->T("Cheater"), cheaterScroll);

	//CHEATER CONTENT
	cheaterContent->Add(new ItemHeader(ch->T("Cheater")));

	//OPTIONS
	ViewGroup *optionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//optionsScroll=rightColumn
	LinearLayout *optionsContent = new LinearLayout(ORIENT_VERTICAL);
	optionsContent->SetSpacing(0);
	optionsScroll->Add(optionsContent);
	tabHolder->AddTab(df->T("Options"), optionsScroll);

	//OPTIONS CONTENT
	optionsContent->Add(new ItemHeader(df->T("DarkFrost Options")));
	//optionsContent->Add(new CheckBox(&darkFrostEngine->getRealAddressing(), opt->T("REAL Addressing"))));
	optionsContent->Add(new Choice(opt->T("Load Cheats")))->OnClick.Handle(this, &DarkFrostScreen::OnLoadCheats);
	optionsContent->Add(new Choice(opt->T("Save Cheats")))->OnClick.Handle(this, &DarkFrostScreen::OnSaveCheats);
	optionsContent->Add(new Choice(opt->T("Reset Copier")))->OnClick.Handle(this, &DarkFrostScreen::OnResetCopier);
	//optionsContent->Add(new CheckBox(&darkFrostEngine->getCheatsEnabled(), opt->T("Cheats Activated")))->OnClick.Handle(this, &DarkFrostScreen::OnCheatsActivated);

	//SEARCHER
	ViewGroup *searcherScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//searcherScroll=rightColumn
	LinearLayout *searcherContent = new LinearLayout(ORIENT_VERTICAL);
	searcherContent->SetSpacing(0);
	searcherScroll->Add(searcherContent);
	tabHolder->AddTab(df->T("Searcher"), searcherScroll);

	//SEARCHER CONTENT
	searcherContent->Add(new ItemHeader(sr->T("Searcher")));
	searcherContent->Add(new Choice(opt->T("Find Exact Value")))->OnClick.Handle(this, &DarkFrostScreen::OnExactValue);
	searcherContent->Add(new Choice(opt->T("Find Unknown Value - 8bit/BYTE")))->OnClick.Handle(this, &DarkFrostScreen::OnUnknownValue8);
	searcherContent->Add(new Choice(opt->T("Find Unknown Value - 16bit/WORD")))->OnClick.Handle(this, &DarkFrostScreen::OnUnknownValue16);
	searcherContent->Add(new Choice(opt->T("Find Unknown Value - 32bit/DWORD")))->OnClick.Handle(this, &DarkFrostScreen::OnUnknownValue32);
	searcherContent->Add(new Choice(opt->T("Find Text")))->OnClick.Handle(this, &DarkFrostScreen::OnFindText);
	searcherContent->Add(new Choice(opt->T("Search Range")))->OnClick.Handle(this, &DarkFrostScreen::OnSearchRange);
	searcherContent->Add(new Choice(opt->T("DMA Searcher")))->OnClick.Handle(this, &DarkFrostScreen::OnDMASearcher);
	searcherContent->Add(new Choice(opt->T("Remove Searches")))->OnClick.Handle(this, &DarkFrostScreen::OnRemoveSearches);

	//RAM
	ViewGroup *ramScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//ramScroll=rightColumn
	LinearLayout *ramContent = new LinearLayout(ORIENT_VERTICAL);
	ramContent->SetSpacing(0);
	ramScroll->Add(ramContent);
	tabHolder->AddTab(df->T("RAM"), ramScroll);

	//RAM CONTENT
	ramContent->Add(new ItemHeader(ram->T("RAM")));

	//DECODER
	ViewGroup *decoderScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//decoderScroll=rightColumn
	LinearLayout *decoderContent = new LinearLayout(ORIENT_VERTICAL);
	decoderContent->SetSpacing(0);
	decoderScroll->Add(decoderContent);
	tabHolder->AddTab(df->T("Decoder"), decoderScroll);

	//DECODER CONTENT
	decoderContent->Add(new ItemHeader(de->T("Decoder")));

	//ABOUT
	ViewGroup *aboutScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//aboutScroll=rightColumn
	LinearLayout *aboutContent = new LinearLayout(ORIENT_VERTICAL);
	aboutContent->SetSpacing(0);
	aboutScroll->Add(aboutContent);
	tabHolder->AddTab(df->T("About"), aboutScroll);

	//ABOUT CONTENT
	aboutContent->Add(new ItemHeader(df->T("DarkFrost for PPSSPP by demon450")));
	aboutContent->Add(new TextView(ab->T("- DarkFrost is a plugin for the psp ported to work on PPSSPP")));
	aboutContent->Add(new TextView(ab->T("- Based off of DarkFrost v6 BETA")));

	//BACK BUTTON
	tabHolder->Add(new Choice(df->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	//ViewGroup *backScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));//need it just cuz
	//tabHolder->AddTab(df->T("Back"), backScroll)->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	//BANNER
	root_->Add(new ImageView(I_DF, IS_DEFAULT, new LinearLayoutParams(FILL_PARENT, FILL_PARENT)));
}

UI::EventReturn DarkFrostScreen::OnBack(UI::EventParams &params) {
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

UI::EventReturn DarkFrostScreen::OnExactValue(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(1);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnUnknownValue8(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(2);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnUnknownValue16(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(3);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnUnknownValue32(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(4);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnFindText(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(5);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnSearchRange(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(6);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnDMASearcher(UI::EventParams &params) {
	auto extMenu = new DFExtMenu(7);
	screenManager()->push(extMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnRemoveSearches(UI::EventParams &params) {
	//delete the search files here
	return UI::EVENT_DONE;
}

UI::EventReturn DarkFrostScreen::OnCheatsActivated(UI::EventParams &params) {
	darkFrostEngine->toggleCheatsEnabled();
	return UI::EVENT_DONE;
}