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

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "file/vfs.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"
#include "Core/Config.h"
#include "Core/HLE/sceUtility.h"

void DrawBackground(float alpha);

void PromptScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
}

void PromptScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new TextView(0, message_, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 10, NONE, NONE)));

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumnItems);
	rightColumnItems->Add(new Choice(yesButtonText_))->OnClick.Handle(this, &PromptScreen::OnYes);
	if (noButtonText_ != "")
		rightColumnItems->Add(new Choice(noButtonText_))->OnClick.Handle(this, &PromptScreen::OnNo);
}

UI::EventReturn PromptScreen::OnYes(UI::EventParams &e) {
	callback_(true);
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn PromptScreen::OnNo(UI::EventParams &e) {
	callback_(false);
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

NewLanguageScreen::NewLanguageScreen() : ListPopupScreen("Language") {
	langValuesMapping["ja_JP"] = std::make_pair("日本語", PSP_SYSTEMPARAM_LANGUAGE_JAPANESE);
	langValuesMapping["en_US"] = std::make_pair("English",PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["fr_FR"] = std::make_pair("Français", PSP_SYSTEMPARAM_LANGUAGE_FRENCH);
	langValuesMapping["es_ES"] = std::make_pair("Castellano", PSP_SYSTEMPARAM_LANGUAGE_SPANISH);
	langValuesMapping["es_LA"] = std::make_pair("Latino", PSP_SYSTEMPARAM_LANGUAGE_SPANISH);
	langValuesMapping["de_DE"] = std::make_pair("Deutsch", PSP_SYSTEMPARAM_LANGUAGE_GERMAN);
	langValuesMapping["it_IT"] = std::make_pair("Italiano", PSP_SYSTEMPARAM_LANGUAGE_ITALIAN); 
	langValuesMapping["nl_NL"] = std::make_pair("Nederlands", PSP_SYSTEMPARAM_LANGUAGE_DUTCH);
	langValuesMapping["pt_PT"] = std::make_pair("Português", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE);
	langValuesMapping["pt_BR"] = std::make_pair("Brasileiro", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE);
	langValuesMapping["ru_RU"] = std::make_pair("Русский", PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN);
	langValuesMapping["ko_KR"] = std::make_pair("한국어", PSP_SYSTEMPARAM_LANGUAGE_KOREAN);
	langValuesMapping["zh_TW"] = std::make_pair("繁體中文", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL);
	langValuesMapping["zh_CN"] = std::make_pair("简体中文", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED);

	//langValuesMapping["ar_AE"] = std::make_pair("العربية", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["az_AZ"] = std::make_pair("Azeri", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["ca_ES"] = std::make_pair("Català", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["gr_EL"] = std::make_pair("ελληνικά", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["he_IL"] = std::make_pair("עברית", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["hu_HU"] = std::make_pair("Magyar", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["id_ID"] = std::make_pair("Indonesia", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["pl_PL"] = std::make_pair("Polski", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["ro_RO"] = std::make_pair("Român", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["sv_SE"] = std::make_pair("Svenska", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["tr_TR"] = std::make_pair("Türk", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["uk_UA"] = std::make_pair("Українська", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);

#ifdef ANDROID
	VFSGetFileListing("assets/lang", &langs_, "ini");
#else
	VFSGetFileListing("lang", &langs_, "ini");
#endif
	std::vector<std::string> listing;
	for (size_t i = 0; i < langs_.size(); i++) {
		// Skip README
		if (langs_[i].name.find("README") != std::string::npos) {
			continue;
		}

		std::string code;
		size_t dot = langs_[i].name.find('.');
		if (dot != std::string::npos)
			code = langs_[i].name.substr(0, dot);

		std::string buttonTitle = langs_[i].name;

		if (!code.empty()) {
			if (langValuesMapping.find(code) == langValuesMapping.end()) {
				// No title found, show locale code
				buttonTitle = code;
			} else {
				buttonTitle = langValuesMapping[code].first;
			}
		}
		listing.push_back(buttonTitle);
	}

	adaptor_ = UI::StringVectorListAdaptor(listing, 0);
}

void NewLanguageScreen::OnCompleted() {
	std::string oldLang = g_Config.languageIni;
	
	std::string iniFile = langs_[listView_->GetSelected()].name;

	size_t dot = iniFile.find('.');
	std::string code;
	if (dot != std::string::npos)
		code = iniFile.substr(0, dot);

	if (code.empty())
		return;

	g_Config.languageIni = code;
	
	if (i18nrepo.LoadIni(g_Config.languageIni)) {
		// Dunno what else to do here.
		if (langValuesMapping.find(code) == langValuesMapping.end()) {
			// Fallback to English
			g_Config.ilanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		} else {
			g_Config.ilanguage = langValuesMapping[code].second;
		}
	} else {
		g_Config.languageIni = oldLang;
	}
}