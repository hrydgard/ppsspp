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
#include "UI/EmuScreen.h"
#include "UI/PluginScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "Core/Config.h"

void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

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
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
	graphicsSettings->Add(new CheckBox(&g_Config.bBufferedRendering, gs->T("Buffered Rendering")));
	graphicsSettings->Add(new CheckBox(&g_Config.bDisplayFramebuffer, gs->T("Display Raw Framebuffer")));
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	graphicsSettings->Add(new CheckBox(&g_Config.bUseVBO, gs->T("Stream VBO")));
	graphicsSettings->Add(new CheckBox(&g_Config.SSAntiAliasing, gs->T("Anti Aliasing")));
	// graphicsSettings->Add(new CheckBox(&g_Config.iShowFPSCounter, gs->T("Show FPS")));
	
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab("Audio", audioSettingsScroll);
	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableAtrac3plus, a->T("Enable Atrac3+")));
	audioSettings->Add(new Choice(a->T("Download Atrac3+ plugin")))->OnClick.Handle(this, &GameSettingsScreen::OnDownloadPlugin);
	
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
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);
	::DrawBackground(1.0f);
}
