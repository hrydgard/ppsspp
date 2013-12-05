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
#include "gfx_es2/gl_state.h"
#include "file/vfs.h"
#include "math/curves.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "UI/MiscScreens.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/HLE/sceUtility.h"
#include "Common/CPUDetect.h"

#include "ui_atlas.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#pragma execution_character_set("utf-8")
#endif

#include "base/timeutil.h"
#include "base/colorutil.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/gl_state.h"
#include "util/random/rng.h"

#include "UI/ui_atlas.h"

static const int symbols[4] = {
	I_CROSS,
	I_CIRCLE,
	I_SQUARE,
	I_TRIANGLE
};

static const uint32_t colors[4] = {
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
};

void DrawBackground(float alpha) {
	static float xbase[100] = {0};
	static float ybase[100] = {0};
	static int last_dp_xres = 0;
	static int last_dp_yres = 0;
	if (xbase[0] == 0.0f || last_dp_xres != dp_xres || last_dp_yres != dp_yres) {
		GMRng rng;
		for (int i = 0; i < 100; i++) {
			xbase[i] = rng.F() * dp_xres;
			ybase[i] = rng.F() * dp_yres;
		}
		last_dp_xres = dp_xres;
		last_dp_yres = dp_yres;
	}
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.1f,0.2f,0.43f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ui_draw2d.DrawImageStretch(I_BG, 0, 0, dp_xres, dp_yres);
	float t = time_now();
	for (int i = 0; i < 100; i++) {
		float x = xbase[i];
		float y = ybase[i] + 40*cos(i * 7.2 + t * 1.3);
		float angle = sin(i + t);
		int n = i & 3;
		ui_draw2d.DrawImageRotated(symbols[n], x, y, 1.0f, angle, colorAlpha(colors[n], alpha * 0.1f));
	}
}

void HandleCommonMessages(const char *message, const char *value, ScreenManager *manager) {
	if (!strcmp(message, "clear jit")) {
		if (MIPSComp::jit) {
			MIPSComp::jit->ClearCache();
		}
	}
}

void UIScreenWithBackground::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
	dc.Flush();
}

void UIScreenWithBackground::sendMessage(const char *message, const char *value) {
	HandleCommonMessages(message, value, screenManager());
	I18NCategory *de = GetI18NCategory("Developer");
	if (!strcmp(message, "language screen")) {
		auto langScreen = new NewLanguageScreen(de->T("Language"));
		langScreen->OnChoice.Handle(this, &UIScreenWithBackground::OnLanguageChange);
		screenManager()->push(langScreen);
	}
}

UI::EventReturn UIScreenWithBackground::OnLanguageChange(UI::EventParams &e) {
	RecreateViews();
	if (host) {
		host->UpdateUI();
	}

	return UI::EVENT_DONE;
}

UI::EventReturn UIDialogScreenWithBackground::OnLanguageChange(UI::EventParams &e) {
	RecreateViews();
	if (host) {
		host->UpdateUI();
	}

	return UI::EVENT_DONE;
}

void UIDialogScreenWithBackground::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
	dc.Flush();
}

void UIDialogScreenWithBackground::sendMessage(const char *message, const char *value) {
	HandleCommonMessages(message, value, screenManager());
	I18NCategory *de = GetI18NCategory("Developer");
	if (!strcmp(message, "language screen")) {
		auto langScreen = new NewLanguageScreen(de->T("Language"));
		langScreen->OnChoice.Handle(this, &UIDialogScreenWithBackground::OnLanguageChange);
		screenManager()->push(langScreen);
	}
}

PromptScreen::PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText, std::function<void(bool)> callback)
	: message_(message), callback_(callback) {
		I18NCategory *d = GetI18NCategory("Dialog");
		yesButtonText_ = d->T(yesButtonText.c_str());
		noButtonText_ = d->T(noButtonText.c_str());
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

	leftColumn->Add(new TextView(message_, ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));

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

PostProcScreen::PostProcScreen(const std::string &title) : ListPopupScreen(title) {
	I18NCategory *ps = GetI18NCategory("PostShaders");
	shaders_ = GetAllPostShaderInfo();
	std::vector<std::string> items;
	int selected = -1;
	for (int i = 0; i < (int)shaders_.size(); i++) {
		if (shaders_[i].section == g_Config.sPostShaderName)
			selected = i;
		items.push_back(ps->T(shaders_[i].name.c_str()));
	}
	adaptor_ = UI::StringVectorListAdaptor(items, selected);
}

void PostProcScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	g_Config.sPostShaderName = shaders_[listView_->GetSelected()].section;
}

NewLanguageScreen::NewLanguageScreen(const std::string &title) : ListPopupScreen(title) {
	// Disable annoying encoding warning
#ifdef _MSC_VER
#pragma warning(disable:4566)
#endif
	langValuesMapping = GetLangValuesMapping();

	std::vector<FileInfo> tempLangs;
#ifdef ANDROID
	VFSGetFileListing("assets/lang", &tempLangs, "ini");
#else
	VFSGetFileListing("lang", &tempLangs, "ini");
#endif
	std::vector<std::string> listing;
	int selected = -1;
	int counter = 0;
	for (size_t i = 0; i < tempLangs.size(); i++) {
		// Skip README
		if (tempLangs[i].name.find("README") != std::string::npos) {
			continue;
		}

#ifndef _WIN32
		// ar_AE only works on Windows.
		if (tempLangs[i].name.find("ar_AE") != std::string::npos) {
			continue;
		}
#endif
		FileInfo lang = tempLangs[i];
		langs_.push_back(lang);

		std::string code;
		size_t dot = lang.name.find('.');
		if (dot != std::string::npos)
			code = lang.name.substr(0, dot);

		std::string buttonTitle = lang.name;

		if (!code.empty()) {
			if (langValuesMapping.find(code) == langValuesMapping.end()) {
				// No title found, show locale code
				buttonTitle = code;
			} else {
				buttonTitle = langValuesMapping[code].first;
			}
		}
		if (g_Config.sLanguageIni == code)
			selected = counter;
		listing.push_back(buttonTitle);
		counter++;
	}

	adaptor_ = UI::StringVectorListAdaptor(listing, selected);
}

void NewLanguageScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	std::string oldLang = g_Config.sLanguageIni;
	std::string iniFile = langs_[listView_->GetSelected()].name;

	size_t dot = iniFile.find('.');
	std::string code;
	if (dot != std::string::npos)
		code = iniFile.substr(0, dot);

	if (code.empty())
		return;

	g_Config.sLanguageIni = code;

	if (i18nrepo.LoadIni(g_Config.sLanguageIni)) {
		// Dunno what else to do here.
		if (langValuesMapping.find(code) == langValuesMapping.end()) {
			// Fallback to English
			g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
		} else {
			g_Config.iLanguage = langValuesMapping[code].second;
		}
		RecreateViews();
	} else {
		g_Config.sLanguageIni = oldLang;
	}
}

void LogoScreen::Next() {
	if (!switched_) {
		switched_ = true;
		if (boot_filename.size()) {
			screenManager()->switchScreen(new EmuScreen(boot_filename));
		} else {
			screenManager()->switchScreen(new MainScreen());
		}
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
	sprintf(temp, "%s Henrik Rydg\xc3\xa5rd", c->T("created", "Created by"));
#ifdef GOLD
	dc.Draw()->DrawImage(I_ICONGOLD, (dp_xres / 2) - 120, (dp_yres / 2) - 30, 1.2f, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
#else
	dc.Draw()->DrawImage(I_ICON, (dp_xres / 2) - 120, (dp_yres / 2) - 30, 1.2f, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
#endif
	dc.Draw()->DrawImage(I_LOGO, (dp_xres / 2) + 40, dp_yres / 2 - 30, 1.5f, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	//dc.Draw()->DrawTextShadow(UBUNTU48, "PPSSPP", dp_xres / 2, dp_yres / 2 - 30, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.Draw()->SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, dp_xres / 2, dp_yres / 2 + 40, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.DrawText(c->T("license", "Free Software under GPL 2.0"), dp_xres / 2, dp_yres / 2 + 70, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	dc.DrawText("www.ppsspp.org", dp_xres / 2, dp_yres / 2 + 130, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	if (boot_filename.size()) {
		ui_draw2d.DrawTextShadow(UBUNTU24, boot_filename.c_str(), dp_xres / 2, dp_yres / 2 + 180, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	}

	dc.End();
	dc.Flush();
}

void CreditsScreen::CreateViews() {
	using namespace UI;
	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *c = GetI18NCategory("PSPCredits");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(new Button(d->T("Back"), new AnchorLayoutParams(260, 64, NONE, NONE, 10, 10, false)))->OnClick.Handle(this, &CreditsScreen::OnOK);
#ifndef GOLD
	root_->Add(new Button(c->T("Buy Gold"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 10, false)))->OnClick.Handle(this, &CreditsScreen::OnSupport);
#endif
	if (g_Config.sLanguageIni == "zh_CN" ||g_Config.sLanguageIni == "zh_TW") {
		root_->Add(new Button(c->T("PPSSPP Chinese Forum"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 84, false)))->OnClick.Handle(this, &CreditsScreen::OnChineseForum);
		root_->Add(new Button(c->T("PPSSPP Forums"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 154, false)))->OnClick.Handle(this, &CreditsScreen::OnForums);
		root_->Add(new Button("www.ppsspp.org", new AnchorLayoutParams(260, 64, 10, NONE, NONE, 228, false)))->OnClick.Handle(this, &CreditsScreen::OnPPSSPPOrg);
	}
	else {
		root_->Add(new Button(c->T("PPSSPP Forums"), new AnchorLayoutParams(260, 64, 10, NONE, NONE, 84, false)))->OnClick.Handle(this, &CreditsScreen::OnForums);
		root_->Add(new Button("www.ppsspp.org", new AnchorLayoutParams(260, 64, 10, NONE, NONE, 158, false)))->OnClick.Handle(this, &CreditsScreen::OnPPSSPPOrg);
	}
#ifdef GOLD
	root_->Add(new ImageView(I_ICONGOLD, IS_DEFAULT, new AnchorLayoutParams(100, 64, 10, 10, NONE, NONE, false)));
#else
	root_->Add(new ImageView(I_ICON, IS_DEFAULT, new AnchorLayoutParams(100, 64, 10, 10, NONE, NONE, false)));
#endif
}

UI::EventReturn CreditsScreen::OnSupport(UI::EventParams &e) {
#ifdef ANDROID
	LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
#else
	LaunchBrowser("http://central.ppsspp.org/buygold");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnPPSSPPOrg(UI::EventParams &e) {
	LaunchBrowser("http://www.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnForums(UI::EventParams &e) {
	LaunchBrowser("http://forums.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnChineseForum(UI::EventParams &e) {
	LaunchBrowser("http://tieba.baidu.com/f?ie=utf-8&kw=ppsspp");
	return UI::EVENT_DONE;
}

UI::EventReturn CreditsScreen::OnOK(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

void CreditsScreen::update(InputState &input_state) {
	UIScreen::update(input_state);
	UpdateUIState(UISTATE_MENU);
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
		c->T("title", "A fast and portable PSP emulator"),
		"",
		"",
		c->T("created", "Created by"),
		"Henrik Rydg\xc3\xa5rd",
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
		"bollu",
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
		"arnastia",
		"lioncash",
		"JulianoAmaralChaves",
		"vnctdj",
		"kaienfr",
		"shenweip",
		"Danyal Zia",
		"",
		"",
		c->T("specialthanks", "Special thanks to:"),
		"Maxim for his amazing Atrac3+ decoder work",
		"Keith Galocy at nVidia (hw, advice)",
		"Orphis (build server)",
		"angelxwind (iOS builds)",
		"W.MS (iOS builds)",
		"solarmystic (testing)",
		"all the forum mods",
		"",
		c->T("this translation by", ""),   // Empty string as this is the original :)
		c->T("translators1", ""),
		c->T("translators2", ""),
		c->T("translators3", ""),
		c->T("translators4", ""),
		c->T("translators5", ""),
		c->T("translators6", ""),
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
#elif !defined(_WIN32)
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
		c->T("info1", "PPSSPP is only intended to play games you own."),
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
			dc.SetFontScale(ease(alpha), ease(alpha));
			dc.DrawText(credits[i], dp_xres/2, y, whiteAlpha(alpha), ALIGN_HCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		y += itemHeight;
	}

	dc.End();
	dc.Flush();
}
