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
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"

#include "Core/Config.h"
#include "Core/Host.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "Core/HW/atrac3plus.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Common/KeyMap.h"

#ifdef _WIN32
namespace MainWindow {
	enum { 
		WM_USER_LOG_STATUS_CHANGED = WM_USER + 101,
		WM_USER_ATRAC_STATUS_CHANGED = WM_USER + 102,
	};
	extern HWND hwndMain;
}
#endif
#ifdef IOS
extern bool isJailed;
#endif

static const int alternateSpeedTable[9] = {
	0, 15, 30, 45, 60, 75, 90, 120, 180
};

void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	cap60FPS_ = g_Config.iForceMaxEmulatedFPS == 60;
	
	iAlternateSpeedPercent_ = 3;
	for (int i = 0; i < 8; i++) {
		if (g_Config.iFpsLimit <= alternateSpeedTable[i]) {
			iAlternateSpeedPercent_ = i;
			break;
		}
	}

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *ms = GetI18NCategory("MainSettings");
	I18NCategory *dev = GetI18NCategory("Developer");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	root_->Add(new Choice(d->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle(this, &GameSettingsScreen::OnBack);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));

	root_->Add(tabHolder);

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettings->SetSpacing(0);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab(ms->T("Graphics"), graphicsSettingsScroll);

	graphicsSettings->Add(new ItemHeader(gs->T("Rendering Mode")));
	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering", "Read Framebuffers To Memory (CPU)", "Read Framebuffers To Memory (GPU)"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, 4, gs, screenManager()))->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingMode);

	graphicsSettings->Add(new ItemHeader(gs->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "Auto", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gs->T("Frame Skipping"), frameSkip, 0, 10, gs, screenManager()));
	static const char *fpsChoices[] = {"None", "Speed", "FPS", "Both"};

	graphicsSettings->Add(new CheckBox(&cap60FPS_, gs->T("Force max 60 FPS (helps GoW)")));
	static const char *customSpeed[] = {"Unlimited", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%"};
	graphicsSettings->Add(new PopupMultiChoice(&iAlternateSpeedPercent_, gs->T("Alternative Speed"), customSpeed, 0, 9, gs, screenManager()));

	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gs->T("Hardware Transform")));
	graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	graphicsSettings->Add(new CheckBox(&g_Config.bAntiAliasing, gs->T("Anti-Aliasing")));
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
#ifdef BLACKBERRY
	if (pixel_xres == pixel_yres)
		graphicsSettings->Add(new CheckBox(&g_Config.bPartialStretch, gs->T("Partial Vertical Stretch")));
#endif
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	// This setting is not really useful for anyone atm.
	// graphicsSettings->Add(new CheckBox(&g_Config.bTrueColor, gs->T("True Color")));
#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gs->T("VSync")));
	graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gs->T("FullScreen")));
#endif

	graphicsSettings->Add(new ItemHeader(gs->T("Overlay Information")));
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS Counter"), fpsChoices, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gs->T("Show Debug Statistics")));
	
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Scaling")));
#ifndef USING_GLES2
	static const char *texScaleLevels[] = {"Off", "2x", "3x","4x", "5x"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 1, 5, gs, screenManager()));
#else
	static const char *texScaleLevels[] = {"Off", "2x", "3x"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 1, 3, gs, screenManager()));
#endif
	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale Type"), texScaleAlgos, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gs->T("Deposterize")));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Anisotropic Filtering"), anisoLevels, 0, 5, gs, screenManager()));
	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Texture Filter"), texFilters, 1, 4, gs, screenManager()));

	graphicsSettings->Add(new ItemHeader(gs->T("Hack Settings")));
	graphicsSettings->Add(new CheckBox(&g_Config.bDisableStencilTest, gs->T("Disable Stencil Test")));
	graphicsSettings->Add(new CheckBox(&g_Config.bAlwaysDepthWrite, gs->T("Always Depth Write")));

	// Developer tools are not accessible ingame, so it goes here
	graphicsSettings->Add(new ItemHeader(gs->T("Debugging")));
	Choice *dump = graphicsSettings->Add(new Choice(gs->T("Dump next frame to log")));
	dump->OnClick.Handle(this, &GameSettingsScreen::OnDumpNextFrameToLog);
	if (!PSP_IsInited())
		dump->SetEnabled(false);

	// Audio
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettings->SetSpacing(0);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab(ms->T("Audio"), audioSettingsScroll);

	std::string atracString;
	atracString.assign(Atrac3plus_Decoder::IsInstalled() ? "Redownload Atrac3+ plugin" : "Download Atrac3+ plugin");
	audioSettings->Add(new ItemHeader(ms->T("Audio")));
	audioSettings->Add(new Choice(a->T(atracString.c_str())))->OnClick.Handle(this, &GameSettingsScreen::OnDownloadPlugin);

	audioSettings->Add(new PopupSliderChoice(&g_Config.iSFXVolume, 0, 8, a->T("SFX volume"), screenManager()));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iBGMVolume, 0, 8, a->T("BGM volume"), screenManager()));
	 
	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableAtrac3plus, a->T("Enable Atrac3+")));
	audioSettings->Add(new CheckBox(&g_Config.bLowLatencyAudio, a->T("Low latency audio")));

	// Control
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettings->SetSpacing(0);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab(ms->T("Controls"), controlsSettingsScroll);
	controlsSettings->Add(new ItemHeader(ms->T("Controls")));
	controlsSettings->Add(new Choice(c->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);
	controlsSettings->Add(new CheckBox(&g_Config.bAccelerometerToAnalogHoriz, c->T("Tilt", "Tilt to Analog (horizontal)")));
	controlsSettings->Add(new ItemHeader(c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 85, c->T("Button Opacity"), screenManager()));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fButtonScale, 1.15, 2.05, c->T("Button Scaling"), screenManager()));
	controlsSettings->Add(new CheckBox(&g_Config.bShowAnalogStick, c->T("Show Left Analog Stick")));
	
	// System
	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettings->SetSpacing(0);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab(ms->T("System"), systemSettingsScroll);

	systemSettings->Add(new ItemHeader(s->T("Emulation")));
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (Unstable)")));

#ifdef IOS
	if (isJailed) {
		systemSettings->Add(new TextView(s->T("DynarecisJailed", "Dynarec (JIT) - (Not jailbroken - JIT not available)")));
	} else {
		systemSettings->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
	}
#else
	systemSettings->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
#endif

#ifndef __SYMBIAN32__
	systemSettings->Add(new CheckBox(&g_Config.bSeparateCPUThread, s->T("Multithreaded (experimental)")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new CheckBox(&g_Config.bSeparateIOThread, s->T("I/O on thread (experimental)")))->SetEnabled(!PSP_IsInited());
#endif
	systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, s->T("Change CPU Clock", "Change CPU Clock (0 = default)"), screenManager()));

	enableReports_ = Reporting::IsEnabled();
//#ifndef ANDROID 
	systemSettings->Add(new ItemHeader(s->T("Cheats", "Cheats (experimental, see forums)")));
	systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, s->T("Enable Cheats")));
//#endif
	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	systemSettings->SetSpacing(0);
	systemSettings->Add(new ItemHeader(s->T("General")));
	systemSettings->Add(new Choice(dev->T("Language", "Language")))->OnClick.Handle(this, &GameSettingsScreen::OnLanguage);
	systemSettings->Add(new Choice(s->T("Developer Tools")))->OnClick.Handle(this, &GameSettingsScreen::OnDeveloperTools);
#ifdef _WIN32
	// Screenshot functionality is not yet available on non-Windows
	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, s->T("Screenshots as PNG")));
#endif

	// TODO: Come up with a way to display a keyboard for mobile users,
	// so until then, this is Windows/Desktop only.
#ifdef _WIN32
	systemSettings->Add(new Choice(s->T("Change Nickname")))->OnClick.Handle(this, &GameSettingsScreen::OnChangeNickname);
#endif
	systemSettings->Add(new Choice(s->T("Clear Recent Games List")))->OnClick.Handle(this, &GameSettingsScreen::OnClearRecents);
	enableReportsCheckbox_ = new CheckBox(&enableReports_, s->T("Enable Compatibility Server Reports"));
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	systemSettings->Add(enableReportsCheckbox_);


	systemSettings->Add(new ItemHeader(s->T("PSP Settings")));
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, s->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, s->T("Date Format"), dateFormat, 1, 3, s, screenManager()));
	static const char *timeFormat[] = { "12HR", "24HR"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, s->T("Time Format"), timeFormat, 1, 2, s, screenManager()));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, s->T("Confirmation Button"), buttonPref, 0, 2, s, screenManager()));
}

UI::EventReturn GameSettingsScreen::OnClearRecents(UI::EventParams &e) {
	g_Config.recentIsos.clear();
	OnRecentChanged.Trigger(e);

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnReloadCheats(UI::EventParams &e) {
	// Hmm, strange mechanism.
	g_Config.bReloadCheats = true;
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingMode(UI::EventParams &e) {
	enableReports_ = Reporting::IsEnabled();
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	return UI::EVENT_DONE;
}

void DrawBackground(float alpha);

UI::EventReturn GameSettingsScreen::OnDumpNextFrameToLog(UI::EventParams &e) {
	if (gpu) {
		gpu->DumpNextFrame();
	}
	return UI::EVENT_DONE;
}

void GameSettingsScreen::DrawBackground(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, true);
	dc.Flush();

	dc.RebindTexture();
	::DrawBackground(1.0f);
	dc.Flush();

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		dc.Flush();
		dc.RebindTexture();
	}
	/*
	if (ginfo && ginfo->pic0Texture) {
		ginfo->pic0Texture->Bind(0);
		// Pic0 is drawn in the bottom right corner, overlaying pic1.
		float sizeX = dp_xres / 480 * ginfo->pic0Texture->Width();
		float sizeY = dp_yres / 272 * ginfo->pic0Texture->Height();
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 2)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(dp_xres - sizeX, dp_yres - sizeY, dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		dc.RebindTexture();
	}*/
}

void GameSettingsScreen::update(InputState &input) {
	UIScreen::update(input);
	g_Config.iForceMaxEmulatedFPS = cap60FPS_ ? 60 : 0;
	g_Config.iFpsLimit = alternateSpeedTable[iAlternateSpeedPercent_];
}

void GameSettingsScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "language")) {
		RecreateViews();
	}
}

UI::EventReturn GameSettingsScreen::OnDownloadPlugin(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnBack(UI::EventParams &e) {
	// If we're in-game, return to the game via DR_CANCEL.
	if(PSP_IsInited()) {
		screenManager()->finishDialog(this, DR_CANCEL);
		host->UpdateScreen();
	} else {
		screenManager()->finishDialog(this, DR_OK);
	}

	if(g_Config.bEnableSound) {
		if(PSP_IsInited() && !IsAudioInitialised())
			Audio_Init();
	}
	// It doesn't matter if audio is inited or not, it'll still output no sound
	// if the mixer isn't available, so go ahead and init/shutdown at our leisure.
	if(Atrac3plus_Decoder::IsInstalled()) {
		if(g_Config.bEnableAtrac3plus)
			Atrac3plus_Decoder::Init();
		else Atrac3plus_Decoder::Shutdown();
	}
	Reporting::Enable(enableReports_, "report.ppsspp.org");
	g_Config.Save();

	host->UpdateUI();

	KeyMap::UpdateConfirmCancelKeys();

	return UI::EVENT_DONE;
}

/*
void GlobalSettingsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableReports_ = Reporting::IsEnabled();
}*/

UI::EventReturn GameSettingsScreen::OnChangeNickname(UI::EventParams &e) {
	#ifdef _WIN32

	const size_t name_len = 256;

	char name[name_len];
	memset(name, 0, sizeof(name));

	if (host->InputBoxGetString("Enter a new PSP nickname", g_Config.sNickName.c_str(), name, name_len)) {
		g_Config.sNickName = name;
	}

	#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFactoryReset(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguage(UI::EventParams &e) {
	I18NCategory *de = GetI18NCategory("Developer");
	auto langScreen = new NewLanguageScreen(de->T("Language"));
	langScreen->OnChoice.Handle(this, &GameSettingsScreen::OnLanguageChange);
	screenManager()->push(langScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguageChange(UI::EventParams &e) {
	RecreateViews();
	OnLanguageChanged.Trigger(e);
	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnControlMapping(UI::EventParams &e) {
	screenManager()->push(new ControlMappingScreen());
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableLogging_ = g_Config.bEnableLogging;

	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *de = GetI18NCategory("Developer");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *s = GetI18NCategory("System");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->SetSpacing(0);
	list->Add(new ItemHeader(s->T("General")));
	list->Add(new Choice(de->T("System Information")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSysInfo);
	list->Add(new CheckBox(&enableLogging_, de->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	list->Add(new Choice(de->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new CheckBox(&g_Config.bShowDeveloperMenu, de->T("Show Developer Menu")));
	list->Add(new Choice(de->T("Run CPU Tests")))->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);
	list->Add(new Choice(de->T("Restore Default Settings")))->OnClick.Handle(this, &DeveloperToolsScreen::OnRestoreDefaultSettings);
#ifndef __SYMBIAN32__
	list->Add(new CheckBox(&g_Config.bSoftwareRendering, gs->T("Software Rendering", "Software Rendering (experimental)")));
#endif
	list->Add(new ItemHeader(de->T("Language")));
	list->Add(new Choice(de->T("Load language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoadLanguageIni);
	list->Add(new Choice(de->T("Save language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSaveLanguageIni);
	list->Add(new Choice(d->T("Back")))->OnClick.Handle(this, &DeveloperToolsScreen::OnBack);
}

UI::EventReturn DeveloperToolsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);

	g_Config.bEnableLogging = enableLogging_;
#ifdef _WIN32
	PostMessage(MainWindow::hwndMain, MainWindow::WM_USER_LOG_STATUS_CHANGED, 0, 0);
#endif
	g_Config.Save();

	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CallbackRestoreDefaults(bool yes) {
	if(yes)
		g_Config.RestoreDefaults();
}

UI::EventReturn DeveloperToolsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	I18NCategory *de = GetI18NCategory("Developer");
	I18NCategory *d = GetI18NCategory("Dialog");
	screenManager()->push(
		new PromptScreen(de->T("RestoreDefaultSettings", "Are you sure you want to restore all settings(except control mapping)\nback to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings."), d->T("OK"), d->T("Cancel"),
	std::bind(&DeveloperToolsScreen::CallbackRestoreDefaults, this, placeholder::_1)));

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
#ifdef _WIN32
	PostMessage(MainWindow::hwndMain, MainWindow::WM_USER_LOG_STATUS_CHANGED, 0, 0);
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnSysInfo(UI::EventParams &e) {
	screenManager()->push(new SystemInfoScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
	RunTests();
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnSaveLanguageIni(UI::EventParams &e) {
	i18nrepo.SaveIni(g_Config.languageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoadLanguageIni(UI::EventParams &e) {
	i18nrepo.LoadIni(g_Config.languageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}
