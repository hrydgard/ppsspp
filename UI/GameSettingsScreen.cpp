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

#include "base/display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "gfx_es2/gpu_features.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "UI/EmuScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/TiltEventProcessor.h"

#include "Common/KeyMap.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"
#include "GPU/GLES/Framebuffer.h"

#ifdef IOS
extern bool iosCanUseJit;
#endif

static const int alternateSpeedTable[9] = {
	0, 15, 30, 45, 60, 75, 90, 120, 180
};

void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	cap60FPS_ = g_Config.iForceMaxEmulatedFPS == 60;
	showDebugStats_ = g_Config.bShowDebugStats;

	iAlternateSpeedPercent_ = 3;
	for (int i = 0; i < (int)ARRAY_SIZE(alternateSpeedTable); i++) {
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

	root_->Add(new Choice(d->T("Back"), "", false, new AnchorLayoutParams(150, 64, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));

	root_->Add(tabHolder);
	root_->SetDefaultFocusView(tabHolder);

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettings->SetSpacing(0);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab(ms->T("Graphics"), graphicsSettingsScroll);

	graphicsSettings->Add(new ItemHeader(gs->T("Rendering Mode")));
	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering", "Read Framebuffers To Memory (CPU)", "Read Framebuffers To Memory (GPU)"};
	PopupMultiChoice *rm = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, ARRAY_SIZE(renderingMode), gs, screenManager()));
	rm->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingMode);


	graphicsSettings->Add(new ItemHeader(gs->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gs->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), gs, screenManager()))->OnChoice.Handle(this, &GameSettingsScreen::OnFrameSkipChange);
	frameSkipAuto_ = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gs->T("Auto FrameSkip")));
	frameSkipAuto_->SetEnabled(g_Config.iFrameSkip != 0);
	graphicsSettings->Add(new CheckBox(&cap60FPS_, gs->T("Force max 60 FPS (helps GoW)")));
	static const char *customSpeed[] = {"Unlimited", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%"};
	graphicsSettings->Add(new PopupMultiChoice(&iAlternateSpeedPercent_, gs->T("Alternative Speed"), customSpeed, 0, ARRAY_SIZE(customSpeed), gs, screenManager()));

	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	postProcChoice_ = graphicsSettings->Add(new Choice(gs->T("Postprocessing Shader")));
	postProcChoice_->OnClick.Handle(this, &GameSettingsScreen::OnPostProcShader);
	postProcChoice_->SetEnabled(g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);

#if defined(_WIN32) || defined(USING_QT_UI)
	graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gs->T("FullScreen")))->OnClick.Handle(this, &GameSettingsScreen::OnFullscreenChange);
#endif
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
	// Small Display: To avoid overlapping touch controls on large tablets. Better control over this will be coming later.
	graphicsSettings->Add(new CheckBox(&g_Config.bSmallDisplay, gs->T("Small Display")));
	if (pixel_xres < pixel_yres * 1.3) // Smaller than 4:3
		graphicsSettings->Add(new CheckBox(&g_Config.bPartialStretch, gs->T("Partial Vertical Stretch")));
#ifdef ANDROID
	graphicsSettings->Add(new CheckBox(&g_Config.bImmersiveMode, gs->T("Immersive Mode")))->OnClick.Handle(this, &GameSettingsScreen::OnImmersiveModeChange);
#endif

	graphicsSettings->Add(new ItemHeader(gs->T("Performance")));
#ifndef MOBILE_DEVICE
	static const char *internalResolutions[] = {"Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
#else
	static const char *internalResolutions[] = {"Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
#endif
	resolutionChoice_ = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInternalResolution, gs->T("Rendering Resolution"), internalResolutions, 0, ARRAY_SIZE(internalResolutions), gs, screenManager()));
	resolutionChoice_->OnClick.Handle(this, &GameSettingsScreen::OnResolutionChange);
	resolutionChoice_->SetEnabled(g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gs->T("VSync")));
#endif
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gs->T("Hardware Transform")));
	CheckBox *swSkin = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareSkinning, gs->T("Software Skinning")));
	CheckBox *vtxCache = graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	vtxCache->SetEnabledPtr((bool *)&g_Config.bHardwareTransform);
	graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gs->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	graphicsSettings->Add(new CheckBox(&g_Config.bTextureSecondaryCache, gs->T("Retain changed textures", "Retain changed textures (speedup, mem hog)")));

	// Seems solid, so we hide the setting.
	// CheckBox *vtxJit = graphicsSettings->Add(new CheckBox(&g_Config.bVertexDecoderJit, gs->T("Vertex Decoder JIT")));

	if (PSP_IsInited()) {
		swSkin->SetEnabled(false);
		// vtxJit->SetEnabled(false);
	}

	graphicsSettings->Add(new CheckBox(&g_Config.bLowQualitySplineBezier, gs->T("LowCurves", "Low quality spline/bezier curves")));

	// In case we're going to add few other antialiasing option like MSAA in the future.
	// graphicsSettings->Add(new CheckBox(&g_Config.bFXAA, gs->T("FXAA")));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Scaling")));
#ifndef MOBILE_DEVICE
	static const char *texScaleLevelsNPOT[] = {"Auto", "Off", "2x", "3x", "4x", "5x"};
	static const char *texScaleLevelsPOT[] = {"Auto", "Off", "2x", "4x"};
#else
	static const char *texScaleLevelsNPOT[] = {"Auto", "Off", "2x", "3x"};
	static const char *texScaleLevelsPOT[] = {"Auto", "Off", "2x"};
#endif

	static const char **texScaleLevels;
	static int numTexScaleLevels;
	if (gl_extensions.OES_texture_npot) {
		texScaleLevels = texScaleLevelsNPOT;
		numTexScaleLevels = ARRAY_SIZE(texScaleLevelsNPOT);
	} else {
		texScaleLevels = texScaleLevelsPOT;
		numTexScaleLevels = ARRAY_SIZE(texScaleLevelsPOT);
	}
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 0, numTexScaleLevels, gs, screenManager()));
	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale Type"), texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos), gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gs->T("Deposterize")));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Anisotropic Filtering"), anisoLevels, 0, ARRAY_SIZE(anisoLevels), gs, screenManager()));
	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Texture Filter"), texFilters, 1, ARRAY_SIZE(texFilters), gs, screenManager()));

	graphicsSettings->Add(new ItemHeader(gs->T("Hack Settings", "Hack Settings (these WILL cause glitches)")));
	graphicsSettings->Add(new CheckBox(&g_Config.bTimerHack, gs->T("Timer Hack")));
	graphicsSettings->Add(new CheckBox(&g_Config.bDisableStencilTest, gs->T("Disable Stencil Test")));
	graphicsSettings->Add(new CheckBox(&g_Config.bAlphaMaskHack, gs->T("Alpha Mask Hack (3rd Birthday)")));
	graphicsSettings->Add(new CheckBox(&g_Config.bAlwaysDepthWrite, gs->T("Always Depth Write")));
	CheckBox *prescale = graphicsSettings->Add(new CheckBox(&g_Config.bPrescaleUV, gs->T("Texture Coord Speedhack")));
	if (PSP_IsInited())
		prescale->SetEnabled(false);

	graphicsSettings->Add(new ItemHeader(gs->T("Overlay Information")));
	static const char *fpsChoices[] = {
		"None", "Speed", "FPS", "Both"
#ifdef BLACKBERRY
     , "Statistics"
#endif
	};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS Counter"), fpsChoices, 0, ARRAY_SIZE(fpsChoices), gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&showDebugStats_, gs->T("Show Debug Statistics")));

	// Developer tools are not accessible ingame, so it goes here.
	graphicsSettings->Add(new ItemHeader(gs->T("Debugging")));
	Choice *dump = graphicsSettings->Add(new Choice(gs->T("Dump next frame to log")));
	dump->OnClick.Handle(this, &GameSettingsScreen::OnDumpNextFrameToLog);
	if (!PSP_IsInited())
		dump->SetEnabled(false);

	// We normally use software rendering to debug so put it in debugging.
	CheckBox *softwareGPU = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareRendering, gs->T("Software Rendering", "Software Rendering (experimental)")));
	if (PSP_IsInited())
		softwareGPU->SetEnabled(false);

	// Audio
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettings->SetSpacing(0);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab(ms->T("Audio"), audioSettingsScroll);

	audioSettings->Add(new ItemHeader(ms->T("Audio")));

	PopupSliderChoice *sfxVol = audioSettings->Add(new PopupSliderChoice(&g_Config.iSFXVolume, 0, MAX_CONFIG_VOLUME, a->T("SFX volume"), screenManager()));
	sfxVol->SetEnabledPtr(&g_Config.bEnableSound);
	PopupSliderChoice *bgmVol = audioSettings->Add(new PopupSliderChoice(&g_Config.iBGMVolume, 0, MAX_CONFIG_VOLUME, a->T("BGM volume"), screenManager()));
	bgmVol->SetEnabledPtr(&g_Config.bEnableSound);

	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	CheckBox *lowAudio = audioSettings->Add(new CheckBox(&g_Config.bLowLatencyAudio, a->T("Low latency audio")));
	lowAudio->SetEnabledPtr(&g_Config.bEnableSound);

	audioSettings->Add(new ItemHeader(a->T("Audio hacks")));
	audioSettings->Add(new CheckBox(&g_Config.bSoundSpeedHack, a->T("Sound speed hack (DOA etc.)")));

	// Control
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettings->SetSpacing(0);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab(ms->T("Controls"), controlsSettingsScroll);
	controlsSettings->Add(new ItemHeader(ms->T("Controls")));
	controlsSettings->Add(new Choice(c->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);

#if defined(MOBILE_DEVICE)
	controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, c->T("HapticFeedback", "Haptic Feedback (vibration)")));
	static const char *tiltTypes[] = { "None (Disabled)", "Analog Stick", "D-PAD", "PSP Action Buttons"};
	controlsSettings->Add(new PopupMultiChoice(&g_Config.iTiltInputType, c->T("Tilt Input Type"), tiltTypes, 0, ARRAY_SIZE(tiltTypes), c, screenManager()))->OnClick.Handle(this, &GameSettingsScreen::OnTiltTypeChange);

	Choice *customizeTilt = controlsSettings->Add(new Choice(c->T("Customize tilt")));
	customizeTilt->OnClick.Handle(this, &GameSettingsScreen::OnTiltCuztomize);
	customizeTilt->SetEnabledPtr((bool *)&g_Config.iTiltInputType); //<- dirty int-to-bool cast
#endif
	controlsSettings->Add(new ItemHeader(c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, c->T("OnScreen", "On-Screen Touch Controls")));
	layoutEditorChoice_ = controlsSettings->Add(new Choice(c->T("Custom layout...")));
	layoutEditorChoice_->OnClick.Handle(this, &GameSettingsScreen::OnTouchControlLayout);
	layoutEditorChoice_->SetEnabledPtr(&g_Config.bShowTouchControls);

	// On systems that aren't Symbian, iOS, and Meego Harmattan, offer to let the user see this button.
	// Some Windows touch devices don't have a back button or other button to call up the menu.
#if !defined(__SYMBIAN32__) && !defined(IOS) && !defined(MEEGO_EDITION_HARMATTAN)
	CheckBox *enablePauseBtn = controlsSettings->Add(new CheckBox(&g_Config.bShowTouchPause, c->T("Show Touch Pause Menu Button")));

	// Don't allow the user to disable it once in-game, so they can't lock themselves out of the menu.
	enablePauseBtn->SetEnabled(!PSP_IsInited());
#endif

	CheckBox *disableDiags = controlsSettings->Add(new CheckBox(&g_Config.bDisableDpadDiagonals, c->T("Disable D-Pad diagonals (4-way touch)")));
	disableDiags->SetEnabledPtr(&g_Config.bShowTouchControls);
	View *opacity = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 100, c->T("Button Opacity"), screenManager()));
	opacity->SetEnabledPtr(&g_Config.bShowTouchControls);
	static const char *touchControlStyles[] = {"Classic", "Thin borders"};
	View *style = controlsSettings->Add(new PopupMultiChoice(&g_Config.iTouchButtonStyle, c->T("Button style"), touchControlStyles, 0, ARRAY_SIZE(touchControlStyles), c, screenManager()));
	style->SetEnabledPtr(&g_Config.bShowTouchControls);

#if defined(USING_WIN_UI)
	controlsSettings->Add(new ItemHeader(c->T("Keyboard", "Keyboard Control Settings")));
	controlsSettings->Add(new CheckBox(&g_Config.bIgnoreWindowsKey, c->T("Ignore Windows Key")));
#endif

	// System
	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettings->SetSpacing(0);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab(ms->T("System"), systemSettingsScroll);

	systemSettings->Add(new ItemHeader(s->T("UI Language")));
	systemSettings->Add(new Choice(dev->T("Language", "Language")))->OnClick.Handle(this, &GameSettingsScreen::OnLanguage);

	systemSettings->Add(new ItemHeader(s->T("Emulation")));
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (Unstable)")));

	systemSettings->Add(new CheckBox(&g_Config.bSeparateCPUThread, s->T("Multithreaded (experimental)")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new CheckBox(&g_Config.bSeparateIOThread, s->T("I/O on thread (experimental)")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, s->T("Change CPU Clock", "Change CPU Clock (0 = default) (unstable)"), screenManager()));
#ifndef MOBILE_DEVICE
	systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindFlipFrequency, 0, 1800, s->T("Rewind Snapshot Frequency", "Rewind Snapshot Frequency (0 = off, mem hog)"), screenManager()));
#endif

	systemSettings->Add(new CheckBox(&g_Config.bAtomicAudioLocks, s->T("Atomic Audio locks (experimental)")))->SetEnabled(!PSP_IsInited());

	systemSettings->Add(new ItemHeader(s->T("Developer Tools")));
	systemSettings->Add(new Choice(s->T("Developer Tools")))->OnClick.Handle(this, &GameSettingsScreen::OnDeveloperTools);

	systemSettings->Add(new ItemHeader(s->T("General")));

#ifdef ANDROID
	static const char *screenRotation[] = {"Auto", "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed"};
	PopupMultiChoice *rot = systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenRotation, c->T("Screen Rotation"), screenRotation, 0, ARRAY_SIZE(screenRotation), c, screenManager()));
	rot->OnChoice.Handle(this, &GameSettingsScreen::OnScreenRotation);
#endif

	systemSettings->Add(new CheckBox(&g_Config.bCheckForNewVersion, s->T("VersionCheck", "Check for new versions of PPSSPP")));
	systemSettings->Add(new Choice(s->T("Clear Recent Games List")))->OnClick.Handle(this, &GameSettingsScreen::OnClearRecents);
	systemSettings->Add(new Choice(s->T("Restore Default Settings")))->OnClick.Handle(this, &GameSettingsScreen::OnRestoreDefaultSettings);
	systemSettings->Add(new CheckBox(&g_Config.bEnableAutoLoad, s->T("Auto Load Newest Savestate")));

	enableReports_ = Reporting::IsEnabled();
	enableReportsCheckbox_ = new CheckBox(&enableReports_, s->T("Enable Compatibility Server Reports"));
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	systemSettings->Add(enableReportsCheckbox_);

	systemSettings->Add(new ItemHeader(s->T("Networking")));
	systemSettings->Add(new CheckBox(&g_Config.bEnableWlan, s->T("Enable networking", "Enable networking/wlan (beta)")));

//#ifndef ANDROID
	systemSettings->Add(new ItemHeader(s->T("Cheats", "Cheats (experimental, see forums)")));
	systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, s->T("Enable Cheats")));
//#endif
	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	systemSettings->SetSpacing(0);

	systemSettings->Add(new ItemHeader(s->T("PSP Settings")));
	static const char *models[] = {"PSP-1000" , "PSP-2000/3000"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iPSPModel, s->T("PSP Model"), models, 0, ARRAY_SIZE(models), s, screenManager()));
	// TODO: Come up with a way to display a keyboard for mobile users,
	// so until then, this is Windows/Desktop only.
#if defined(_WIN32) || defined(USING_QT_UI)
	systemSettings->Add(new Choice(s->T("Change Nickname")))->OnClick.Handle(this, &GameSettingsScreen::OnChangeNickname);
#endif
#if defined(_WIN32) || (defined(USING_QT_UI) && !defined(MOBILE_DEVICE))
	// Screenshot functionality is not yet available on non-Windows/non-Qt
	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, s->T("Screenshots as PNG")));
#endif
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, s->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, s->T("Date Format"), dateFormat, 1, 3, s, screenManager()));
	static const char *timeFormat[] = { "12HR", "24HR"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, s->T("Time Format"), timeFormat, 1, 2, s, screenManager()));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, s->T("Confirmation Button"), buttonPref, 0, 2, s, screenManager()));
}

UI::EventReturn GameSettingsScreen::OnScreenRotation(UI::EventParams &e) {
	System_SendMessage("rotate", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_SendMessage("immersive", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingMode(UI::EventParams &e) {
	// We do not want to report when rendering mode is Framebuffer to memory - so many issues
	// are caused by that (framebuffer copies overwriting display lists, etc).
	Reporting::UpdateConfig();
	enableReports_ = Reporting::IsEnabled();
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());

	postProcChoice_->SetEnabled(g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionChoice_->SetEnabled(g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFrameSkipChange(UI::EventParams &e) {
	frameSkipAuto_->SetEnabled(g_Config.iFrameSkip != 0);

	return UI::EVENT_DONE;
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

UI::EventReturn GameSettingsScreen::OnFullscreenChange(UI::EventParams &e) {
	host->GoFullscreen(g_Config.bFullScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnResolutionChange(UI::EventParams &e) {
	if (gpu) {
		gpu->Resized();
	}
	Reporting::UpdateConfig();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnShaderChange(UI::EventParams &e) {
	if (gpu) {
		gpu->ClearShaderCache();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDumpNextFrameToLog(UI::EventParams &e) {
	if (gpu) {
		gpu->DumpNextFrame();
	}
	return UI::EVENT_DONE;
}

void GameSettingsScreen::update(InputState &input) {
	UIScreen::update(input);
	g_Config.iForceMaxEmulatedFPS = cap60FPS_ ? 60 : 0;
	g_Config.iFpsLimit = alternateSpeedTable[iAlternateSpeedPercent_];
	if (g_Config.bShowDebugStats != showDebugStats_) {
		// This affects the jit.
		NativeMessageReceived("clear jit", "");
		g_Config.bShowDebugStats = showDebugStats_;
	}
}

void GameSettingsScreen::sendMessage(const char *message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIDialogScreenWithBackground::sendMessage(message, value);

	if (!strcmp(message, "control mapping")) {
		UpdateUIState(UISTATE_MENU);
		screenManager()->push(new ControlMappingScreen());
	}
}

void GameSettingsScreen::onFinish(DialogResult result) {
	if (g_Config.bEnableSound) {
		if (PSP_IsInited() && !IsAudioInitialised())
			Audio_Init();
	}

	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	g_Config.Save();

	host->UpdateUI();

	KeyMap::UpdateConfirmCancelKeys();
}

/*
void GlobalSettingsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableReports_ = Reporting::IsEnabled();
}*/

UI::EventReturn GameSettingsScreen::OnChangeNickname(UI::EventParams &e) {
#if defined(_WIN32) || defined(USING_QT_UI)
	const size_t name_len = 256;

	char name[name_len];
	memset(name, 0, sizeof(name));

	if (System_InputBoxGetString("Enter a new PSP nickname", g_Config.sNickName.c_str(), name, name_len)) {
		g_Config.sNickName = name;
	}
#endif

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
	screenManager()->RecreateAllViews();

	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnPostProcShader(UI::EventParams &e) {
	I18NCategory *g = GetI18NCategory("Graphics");
	auto procScreen = new PostProcScreen(g->T("Postprocessing Shader"));
	procScreen->OnChoice.Handle(this, &GameSettingsScreen::OnPostProcShaderChange);
	screenManager()->push(procScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnPostProcShaderChange(UI::EventParams &e) {
	if (gpu) {
		gpu->Resized();
	}
	Reporting::UpdateConfig();
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

UI::EventReturn GameSettingsScreen::OnTouchControlLayout(UI::EventParams &e) {
	screenManager()->push(new TouchControlLayoutScreen());
	return UI::EVENT_DONE;
};

//when the tilt event type is modified, we need to reset all tilt settings.
//refer to the ResetTiltEvents() function for a detailed explanation.
UI::EventReturn GameSettingsScreen::OnTiltTypeChange(UI::EventParams &e){
	TiltEventProcessor::ResetTiltEvents();
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnTiltCuztomize(UI::EventParams &e){
	screenManager()->push(new TiltAnalogSettingsScreen());
	return UI::EVENT_DONE;
};

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *de = GetI18NCategory("Developer");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *s = GetI18NCategory("System");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->SetSpacing(0);
	list->Add(new ItemHeader(s->T("General")));

#ifdef IOS
	if (!iosCanUseJit) {
		list->Add(new TextView(s->T("DynarecisJailed", "Dynarec (JIT) - (Not jailbroken - JIT not available)")));
	} else {
		list->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
	}
#else
	list->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
#endif

	list->Add(new Choice(de->T("System Information")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSysInfo);
	list->Add(new CheckBox(&g_Config.bShowDeveloperMenu, de->T("Show Developer Menu")));
	list->Add(new CheckBox(&g_Config.bDumpDecryptedEboot, de->T("Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game")));

	Choice *cpuTests = new Choice(de->T("Run CPU Tests"));
	list->Add(cpuTests)->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);
#ifdef IOS
	const std::string testDirectory = g_Config.flash0Directory + "../";
#else
	const std::string testDirectory = g_Config.memCardDirectory;
#endif
	if (!File::Exists(testDirectory + "pspautotests/tests/")) {
		cpuTests->SetEnabled(false);
	}

	list->Add(new CheckBox(&g_Config.bEnableLogging, de->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	list->Add(new Choice(de->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new ItemHeader(de->T("Language")));
	list->Add(new Choice(de->T("Load language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoadLanguageIni);
	list->Add(new Choice(de->T("Save language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSaveLanguageIni);
	list->Add(new ItemHeader(""));
	list->Add(new Choice(d->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	g_Config.Save();
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes)
		g_Config.RestoreDefaults();
	host->UpdateUI();
}

UI::EventReturn GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	I18NCategory *de = GetI18NCategory("Developer");
	I18NCategory *d = GetI18NCategory("Dialog");
	screenManager()->push(
		new PromptScreen(de->T("RestoreDefaultSettings", "Are you sure you want to restore all settings(except control mapping)\nback to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings."), d->T("OK"), d->T("Cancel"),
	std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, placeholder::_1)));

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	host->ToggleDebugConsoleVisibility();
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
	i18nrepo.SaveIni(g_Config.sLanguageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoadLanguageIni(UI::EventParams &e) {
	i18nrepo.LoadIni(g_Config.sLanguageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}
