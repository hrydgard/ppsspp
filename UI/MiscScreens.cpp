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

#include "base/display.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "file/vfs.h"
#include "math/curves.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"
#include "UI/MenuScreens.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"

#include "ui_atlas.h"

void DrawBackground(float alpha);

void UIScreenWithBackground::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
	dc.Flush();
}

void UIDialogScreenWithBackground::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
	dc.Flush();
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
	// Disable annoying encoding warning
#ifdef _MSC_VER
#pragma warning(disable:4566)
#endif

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

	//langValuesMapping["ar_AE"] = std::make_pair("العربية", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH)
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
	int selected = -1;
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
		if (g_Config.languageIni == code)
			selected = (int)i;
		listing.push_back(buttonTitle);
	}

	adaptor_ = UI::StringVectorListAdaptor(listing, selected);
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

void LogoScreen::Next() {
	if (bootFilename_.size()) {
		screenManager()->switchScreen(new EmuScreen(bootFilename_));
	} else {
		if (g_Config.bNewUI)
			screenManager()->switchScreen(new MainScreen());
		else
			screenManager()->switchScreen(new MenuScreen());
	}
}

void LogoScreen::update(InputState &input_state) {
	UIScreen::update(input_state);
	frames_++;
	if (frames_ > 180 || input_state.pointer_down[0]) {
		Next();
	}
}

void LogoScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void LogoScreen::key(const KeyInput &key) {
	if (key.deviceId != DEVICE_ID_MOUSE) {
		Next();
	}
}

void LogoScreen::render() {
	UIScreen::render();

	UIContext &dc = *screenManager()->getUIContext();

	dc.Begin();
	float t = (float)frames_ / 60.0f;

	float alpha = t;
	if (t > 1.0f) alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f) alphaText = 3.0f - t;

	::DrawBackground(alpha);

	I18NCategory *c = GetI18NCategory("PSPCredits");
	char temp[256];
	sprintf(temp, "%s Henrik Rydgård", c->T("created", "Created by"));

	dc.Draw()->SetFontScale(1.5f, 1.5f);
	dc.Draw()->DrawTextShadow(UBUNTU48, "PPSSPP", dp_xres / 2, dp_yres / 2 - 30, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.Draw()->SetFontScale(1.0f, 1.0f);
	dc.Draw()->DrawTextShadow(UBUNTU24, temp, dp_xres / 2, dp_yres / 2 + 40, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.Draw()->DrawTextShadow(UBUNTU24, c->T("license", "Free Software under GPL 2.0"), dp_xres / 2, dp_yres / 2 + 70, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.Draw()->DrawTextShadow(UBUNTU24, "www.ppsspp.org", dp_xres / 2, dp_yres / 2 + 130, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	if (bootFilename_.size()) {
		ui_draw2d.DrawTextShadow(UBUNTU24, bootFilename_.c_str(), dp_xres / 2, dp_yres / 2 + 180, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	}
	
	dc.End();
	dc.Flush();
}

void CreditsScreen::CreateViews() {
	using namespace UI;
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(new Button("OK", new AnchorLayoutParams(200, 50, NONE, NONE, 10, 10, false)))->OnClick.Handle(this, &CreditsScreen::OnOK);
}

UI::EventReturn CreditsScreen::OnOK(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

void CreditsScreen::update(InputState &input_state) {
	UIScreen::update(input_state);
	globalUIState = UISTATE_MENU;
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_OK);
	}
	frames_++;
}

void CreditsScreen::render() {
	UIScreen::render();

	I18NCategory *c = GetI18NCategory("PSPCredits");

	const char * credits[] = {
		"PPSSPP",
		"",
		"",
		c->T("title", "A fast and portable PSP emulator"),	
		"",
		c->T("created", "Created by"),
		"Henrik Rydgård",
		"(aka hrydgard, ector)",
		"",
		"",
		c->T("contributors", "Contributors:"),
		"unknownbrackets",
		"oioitff",
		"xsacha",
		"raven02",
		"tpunix",
		"orphis",
		"sum2012",
		"mikusp",
		"aquanull",
		"The Dax",
		"tmaul",
		"artart78",
		"ced2911",
		"soywiz",
		"kovensky",
		"xele",
		"chaserhjk",
		"evilcorn",
		"daniel dressler",
		"makotech222",
		"CPkmn",
		"mgaver",
		"jeid3",
		"cinaera/BeaR",
		"jtraynham",
		"Kingcom",
		"aquanull",
		"arnastia",
		"lioncash",
		"JulianoAmaralChaves",
		"",
		c->T("written", "Written in C++ for speed and portability"),
		"",
		"",
		c->T("tools", "Free tools used:"),
#ifdef ANDROID
		"Android SDK + NDK",
#elif defined(BLACKBERRY)
		"Blackberry NDK",
#endif
#if defined(USING_QT_UI)
		"Qt",
#else
		"SDL",
#endif
		"CMake",
		"freetype2",
		"zlib",
		"PSP SDK",
		"",
		"",
		c->T("website", "Check out the website:"),
		"www.ppsspp.org",
		c->T("list", "compatibility lists, forums, and development info"),
		"",
		"",
		c->T("check", "Also check out Dolphin, the best Wii/GC emu around:"),
		"http://www.dolphin-emu.org",
		"",
		"",
		c->T("info1", "PPSSPP is intended for educational purposes only."),
		"",
		c->T("info2", "Please make sure that you own the rights to any games"),
		c->T("info3", "you play by owning the UMD or by buying the digital"),
		c->T("info4", "download from the PSN store on your real PSP."),
		"",	
		"",
		c->T("info5", "PSP is a trademark by Sony, Inc."),
	};

	// TODO: This is kinda ugly, done on every frame...
	char temp[256];
	sprintf(temp, "PPSSPP %s", PPSSPP_GIT_VERSION);
	credits[0] = (const char *)temp;

	UIContext &dc = *screenManager()->getUIContext();
	dc.Begin();

	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int totalHeight = numItems * itemHeight + dp_yres + 200;
	int y = dp_yres - (frames_ % totalHeight);
	for (int i = 0; i < numItems; i++) {
		float alpha = linearInOut(y+32, 64, dp_yres - 192, 64);
		if (alpha > 0.0f) {
			dc.Draw()->SetFontScale(ease(alpha), ease(alpha));
			dc.Draw()->DrawText(UBUNTU24, credits[i], dp_xres/2, y, whiteAlpha(alpha), ALIGN_HCENTER);
			dc.Draw()->SetFontScale(1.0f, 1.0f);
		}
		y += itemHeight;
	}
	/*
	I18NCategory *g = GetI18NCategory("General");

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), 200, 0, g->T("Back"), ALIGN_BOTTOMRIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

#ifdef ANDROID
#ifndef GOLD
	if (UIButton(GEN_ID, Pos(10, dp_yres - 10), 300, 0, g->T("Buy PPSSPP Gold"), ALIGN_BOTTOMLEFT)) {
		LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
	}
#endif
#else
#ifndef GOLD
	if (UIButton(GEN_ID, Pos(10, dp_yres - 10), 300, 0, g->T("Buy PPSSPP Gold"), ALIGN_BOTTOMLEFT)) {
		LaunchBrowser("http://central.ppsspp.org/buygold");
	}
#endif
#endif
	UIEnd();
	*/

	dc.End();
	dc.Flush();
}
