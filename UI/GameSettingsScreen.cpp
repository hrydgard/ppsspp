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

	const char **choices_;
	int numChoices_;
	int minVal_;
	int *value_;
	ScreenManager *screenManager_;
	I18NCategory *category_;
	std::string valueText_;
};

EventReturn PopupMultiChoice::HandleClick(EventParams &e) {
	std::vector<std::string> choices;
	for (size_t i = 0; i < numChoices_; i++) {
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

	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0)));
	leftColumn->Add(new Choice("Back"))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new LinearLayoutParams(600, FILL_PARENT, actionMenuMargins));
	root_->Add(tabHolder);

	// TODO: These currently point to global settings, not game specific ones.

	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab("Graphics", graphicsSettingsScroll);
	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
	graphicsSettings->Add(new CheckBox(&g_Config.bBufferedRendering, gs->T("Buffered Rendering")));
	graphicsSettings->Add(new CheckBox(&g_Config.bDisplayFramebuffer, gs->T("Display Raw Framebuffer")));
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	graphicsSettings->Add(new CheckBox(&g_Config.bUseVBO, gs->T("Stream VBO")));
	graphicsSettings->Add(new CheckBox(&g_Config.SSAntiAliasing, gs->T("Anti Aliasing")));
	graphicsSettings->Add(new CheckBox(&g_Config.bFramebuffersToMem, gs->T("Read Framebuffer to memory")));

	// TODO: Does frame rate belong among the graphics settings?
	graphicsSettings->Add(new ItemHeader(gs->T("Frame rate")));
	graphicsSettings->Add(new CheckBox(&cap60FPS_, gs->T("Read Framebuffer to memory")));

	graphicsSettings->Add(new ItemHeader(gs->T("Texture filtering")));

	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Aniso Filter"), anisoLevels, 0, 5, gs, screenManager()));
	
	graphicsSettings->Add(new ItemHeader(gs->T("Texture scaling")));
	static const char *texScaleLevels[] = {
		"Off (1x)", "2x", "3x",
#ifndef USING_GLES2
		"4x", "5x",
#endif
	};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale"), texScaleLevels, 1, 5, gs, screenManager()));
	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale type"), texScaleAlgos, 0, 4, gs, screenManager()));
	static const char *texFilters[] = { "Default (auto)", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Upscale type"), texFilters, 1, 4, gs, screenManager()));

#ifdef USING_GLES2
	g_Config.bFramebuffersCPUConvert = g_Config.bFramebuffersToMem;
#endif

#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bAutoSaveSymbolMap, gs->T("VSync")));
#endif


	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab("Audio", audioSettingsScroll);
	audioSettings->Add(new Choice(a->T("Download Atrac3+ plugin")))->OnClick.Handle(this, &GameSettingsScreen::OnDownloadPlugin);
	audioSettings->Add(new PopupSliderChoice(&g_Config.iSEVolume, 0, 8, a->T("FX volume"), screenManager()));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iBGMVolume, 0, 8, a->T("BGM volume"), screenManager()));
	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableAtrac3plus, a->T("Enable Atrac3+")));
	
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab("Controls", controlsSettingsScroll);
	controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bLargeControls, c->T("Large Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bShowAnalogStick, c->T("Show Analog Stick")));
	controlsSettings->Add(new CheckBox(&g_Config.bAccelerometerToAnalogHoriz, c->T("Tilt", "Tilt to Analog (horizontal)")));

	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab("System", systemSettingsScroll);
	systemSettings->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (unstable)")));
}

UI::EventReturn GameSettingsScreen::OnDownloadPlugin(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}

void DrawBackground(float alpha);

void GameSettingsScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);

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
	list->Add(new CheckBox(&g_Config.bNewUI, gs->T("New UI")));
	list->Add(new CheckBox(&enableReports_, gs->T("Enable error reporting")));

	static const char *fpsChoices[] = {
		"None", "Speed", "FPS", "Both"
	};

	list->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS"), fpsChoices, 0, 4, gs, screenManager()));
	list->Add(new Choice(gs->T("Language")))->OnClick.Handle(this, &GlobalSettingsScreen::OnLanguage);
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

void GlobalSettingsScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
}

void DeveloperToolsScreen::DrawBackground(UIContext &dc) {
	::DrawBackground(1.0f);
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *d = GetI18NCategory("Developer");

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
