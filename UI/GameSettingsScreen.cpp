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

#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "UI/EmuScreen.h"
#include "UI/PluginScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "Core/Config.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"

namespace UI {

// Reads and writes value to determine the current selection.
class PopupMultiChoice : public Choice {
public:
	PopupMultiChoice(int *value, const std::string &text, const char **choices, int minVal, int numChoices,
		I18NCategory *category, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), choices_(choices), minVal_(minVal), numChoices_(numChoices), 
		category_(category), screenManager_(screenManager) {
		if (*value < minVal) *value = minVal;
		OnClick.Handle(this, &PopupMultiChoice::HandleClick);
		UpdateText();
	}

	virtual void Draw(UIContext &dc);

private:
	void UpdateText();
	EventReturn HandleClick(EventParams &e);

	void ChoiceCallback(int num);

	int *value_;
	const char **choices_;
	int minVal_;
	int numChoices_;
	I18NCategory *category_;
	ScreenManager *screenManager_;
	std::string valueText_;
};

EventReturn PopupMultiChoice::HandleClick(EventParams &e) {
	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category_ ? category_->T(choices_[i]) : choices_[i]);
	}

	Screen *popupScreen = new ListPopupScreen(text_, choices, *value_ - minVal_,
		std::bind(&PopupMultiChoice::ChoiceCallback, this, placeholder::_1));
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

void PopupMultiChoice::UpdateText() {
	valueText_ = category_ ? category_->T(choices_[*value_ - minVal_]) : choices_[*value_ - minVal_];
}

void PopupMultiChoice::ChoiceCallback(int num) {
	*value_ = num + minVal_;
	UpdateText();
}

void PopupMultiChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	dc.Draw()->DrawText(dc.theme->uiFont, valueText_.c_str(), bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

class PopupSliderChoice : public Choice {
public:
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
		OnClick.Handle(this, &PopupSliderChoice::HandleClick);
	}

	void Draw(UIContext &dc);

private:
	EventReturn HandleClick(EventParams &e);

	int *value_;
	int minValue_;
	int maxValue_;
	ScreenManager *screenManager_;
};

class PopupSliderChoiceFloat : public Choice {
public:
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
		OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
	}

	void Draw(UIContext &dc);

private:
	EventReturn HandleClick(EventParams &e);

	float *value_;
	float minValue_;
	float maxValue_;
	ScreenManager *screenManager_;
};

EventReturn PopupSliderChoice::HandleClick(EventParams &e) {
	Screen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, text_);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}


void PopupSliderChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[4];
	sprintf(temp, "%i", *value_);
	dc.Draw()->DrawText(dc.theme->uiFont, temp, bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	Screen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, text_);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

void PopupSliderChoiceFloat::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[4];
	sprintf(temp, "%2.2f", *value_);
	dc.Draw()->DrawText(dc.theme->uiFont, temp, bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

}


void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	cap60FPS_ = g_Config.iForceMaxEmulatedFPS == 60;

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *s = GetI18NCategory("System");

	Margins actionMenuMargins(0, 0, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0)));
	leftColumn->Add(new Choice(g->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);


	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new LinearLayoutParams(800, FILL_PARENT, actionMenuMargins));

	root_->Add(tabHolder);

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab("Graphics", graphicsSettingsScroll);

	graphicsSettings->Add(new ItemHeader(gs->T("Rendering Mode")));
	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering", 
#ifndef USING_GLES2
	"Read Framebuffers To Memory(CPU)", 
#endif
	"Read Framebuffers To Memory(GPU)"
	};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gs->T("Hardware Transform")));
	graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	graphicsSettings->Add(new CheckBox(&g_Config.bUseVBO, gs->T("Stream VBO")));
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	graphicsSettings->Add(new CheckBox(&g_Config.bTrueColor, gs->T("True Color")));
	graphicsSettings->Add(new CheckBox(&g_Config.bDisplayFramebuffer, gs->T("Display Raw Framebuffer")));
#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gs->T("VSync")));
	graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gs->T("FullScreen")));
#endif
	graphicsSettings->Add(new ItemHeader(gs->T("Frame Rate Control")));
	static const char *fpsChoices[] = {"None", "Speed", "FPS", "Both"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS Counter"), fpsChoices, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gs->T("Show Debug Statistics")));
	graphicsSettings->Add(new PopupSliderChoice(&g_Config.iFrameSkip, 0, 9, gs->T("Frame Skipping"), screenManager()));
	graphicsSettings->Add(new ItemHeader(gs->T("Anisotropic Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Anisotropic Filtering"), anisoLevels, 0, 5, gs, screenManager()));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Scaling")));
	static const char *texScaleLevels[] = {
		"Off (1x)", "2x", "3x",
#ifndef USING_GLES2
		"4x", "5x",
#endif
	};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 1, 5, gs, screenManager()));
	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale Type"), texScaleAlgos, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Filtering")));
	static const char *texFilters[] = { "Default (auto)", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Upscale Type"), texFilters, 1, 4, gs, screenManager()));

	// Audio
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab("Audio", audioSettingsScroll);
	audioSettings->Add(new Choice(a->T("Download Atrac3+ plugin")))->OnClick.Handle(this, &GameSettingsScreen::OnDownloadPlugin);
	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableAtrac3plus, a->T("Enable Atrac3+")));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iSEVolume, 0, 8, a->T("FX volume"), screenManager()));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iBGMVolume, 0, 8, a->T("BGM volume"), screenManager()));

	// Control
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab("Controls", controlsSettingsScroll);
	controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bShowAnalogStick, c->T("Show Left Analog Stick")));
	controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 15, 65, c->T("Button Opacity"), screenManager()));
	controlsSettings->Add(new CheckBox(&g_Config.bAccelerometerToAnalogHoriz, c->T("Tilt", "Tilt to Analog (horizontal)")));

	// System
	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab("System", systemSettingsScroll);
	systemSettings->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (Unstable)")));
	systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 1, 1000, gs->T("Unlock CPU Clock"), screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, s->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, gs->T("Date Format"), dateFormat, 1, 3, s, screenManager()));
	static const char *timeFormat[] = { "12HR", "24HR"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, gs->T("Time Format"), timeFormat, 1, 2, s, screenManager()));
	static const char *buttonPref[] = { "Use X to confirm", "Use O to confirm"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, gs->T("Button Perference"), buttonPref, 1, 2, s, screenManager()));
}

void GameSettingsScreen::update(InputState &input) {
	UIScreen::update(input);
	g_Config.iForceMaxEmulatedFPS = cap60FPS_ ? 60 : 0;
}

void GlobalSettingsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableReports_ = g_Config.sReportHost != "";

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->Add(new ItemHeader("General"));
	list->Add(new CheckBox(&g_Config.bNewUI, gs->T("Enable New UI")));
	list->Add(new CheckBox(&enableReports_, gs->T("Enable Error Reporting")));
	list->Add(new CheckBox(&g_Config.bEnableCheats, gs->T("Enable Cheats")));
	list->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, gs->T("Screenshots as PNG")));
	list->Add(new Choice(gs->T("System Language")))->OnClick.Handle(this, &GlobalSettingsScreen::OnLanguage);
	list->Add(new Choice(gs->T("Developer Tools")))->OnClick.Handle(this, &GlobalSettingsScreen::OnDeveloperTools);
	list->Add(new Choice(g->T("Back")))->OnClick.Handle(this, &GlobalSettingsScreen::OnBack);
}

UI::EventReturn GlobalSettingsScreen::OnFactoryReset(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnLanguage(UI::EventParams &e) {
	screenManager()->push(new NewLanguageScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	g_Config.sReportHost = enableReports_ ? "report.ppsspp.org" : "";
	g_Config.Save();
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *d = GetI18NCategory("Developer");
	I18NCategory *a = GetI18NCategory("Audio");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->Add(new ItemHeader(g->T("General")));
	list->Add(new Choice(d->T("Run CPU Tests")))->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);

	list->Add(new Choice(g->T("Back")))->OnClick.Handle(this, &DeveloperToolsScreen::OnBack);
}

UI::EventReturn DeveloperToolsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
	RunTests();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDownloadPlugin(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}
