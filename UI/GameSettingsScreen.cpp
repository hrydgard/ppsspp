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

void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, GAMEINFO_WANTBG | GAMEINFO_WANTSIZE);

	cap60FPS_ = g_Config.iForceMaxEmulatedFPS == 60;

	iAlternateSpeedPercent_ = (g_Config.iFpsLimit * 100) / 60;

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
	PopupMultiChoice *renderingModeChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, ARRAY_SIZE(renderingMode), gs, screenManager()));
	renderingModeChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingMode);
	renderModeEnable = !g_Config.bSoftwareRendering;
	renderingModeChoice->SetEnabledPtr(&renderModeEnable);

	CheckBox *blockTransfer = graphicsSettings->Add(new CheckBox(&g_Config.bBlockTransferGPU, gs->T("Simulate Block Transfer", "Simulate Block Transfer (unfinished)")));
	blockTransferEnable = !g_Config.bSoftwareRendering;
	blockTransfer->SetEnabledPtr(&blockTransferEnable);

	graphicsSettings->Add(new ItemHeader(gs->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gs->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), gs, screenManager()))->OnChoice.Handle(this, &GameSettingsScreen::OnFrameSkipChange);
	frameSkipAuto_ = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gs->T("Auto FrameSkip")));
	frameSkipAuto_->SetEnabled(g_Config.iFrameSkip != 0);
	graphicsSettings->Add(new CheckBox(&cap60FPS_, gs->T("Force max 60 FPS (helps GoW)")));

	graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent_, 0, 600, gs->T("Alternative Speed", "Alternative Speed (in %, 0 = unlimited)"), 5, screenManager()));

	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	postProcChoice_ = graphicsSettings->Add(new Choice(gs->T("Postprocessing Shader")));
	postProcChoice_->OnClick.Handle(this, &GameSettingsScreen::OnPostProcShader);
	postProcEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	postProcChoice_->SetEnabledPtr(&postProcEnable);

#if !defined(MOBILE_DEVICE)
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
	resolutionEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionChoice_->SetEnabledPtr(&resolutionEnable);

#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gs->T("VSync")));
#endif
	CheckBox *mipmapping = graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	mipmapEnable = !g_Config.bSoftwareRendering;
	mipmapping->SetEnabledPtr(&mipmapEnable);

	CheckBox *hwTransform = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gs->T("Hardware Transform")));
	hwTransform->OnClick.Handle(this, &GameSettingsScreen::OnHardwareTransform);
	hwTransformEnable = !g_Config.bSoftwareRendering;
	hwTransform->SetEnabledPtr(&hwTransformEnable);

	CheckBox *swSkin = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareSkinning, gs->T("Software Skinning")));
	swSkinningEnable = !PSP_IsInited() && !g_Config.bSoftwareRendering;
	swSkin->SetEnabledPtr(&swSkinningEnable);

	CheckBox *vtxCache = graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	vtxCacheEnable = !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	vtxCache->SetEnabledPtr(&vtxCacheEnable);

	CheckBox *texBackoff = graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gs->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	texBackoffEnable = !g_Config.bSoftwareRendering;
	texBackoff->SetEnabledPtr(&texBackoffEnable);

	CheckBox *texSecondary_ = graphicsSettings->Add(new CheckBox(&g_Config.bTextureSecondaryCache, gs->T("Retain changed textures", "Retain changed textures (speedup, mem hog)")));
	texSecondaryEnable = !g_Config.bSoftwareRendering;
	texSecondary_->SetEnabledPtr(&texSecondaryEnable);

	// Seems solid, so we hide the setting.
	// CheckBox *vtxJit = graphicsSettings->Add(new CheckBox(&g_Config.bVertexDecoderJit, gs->T("Vertex Decoder JIT")));

	// if (PSP_IsInited()) {
		// vtxJit->SetEnabled(false);
	// }

	static const char *quality[] = { "Low", "Medium", "High"};
	PopupMultiChoice *beziersChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSplineBezierQuality, gs->T("LowCurves", "Spline/Bezier curves quality"), quality, 0, ARRAY_SIZE(quality), gs, screenManager()));
	beziersEnable = !g_Config.bSoftwareRendering;
	beziersChoice->SetEnabledPtr(&beziersEnable);
	
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
	PopupMultiChoice *texScalingChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 0, numTexScaleLevels, gs, screenManager()));
	texScalingEnable = !g_Config.bSoftwareRendering;
	texScalingChoice->SetEnabledPtr(&texScalingEnable);

	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	PopupMultiChoice *texScalingType = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale Type"), texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos), gs, screenManager()));
	texScalingEnable = !g_Config.bSoftwareRendering;
	texScalingType->SetEnabledPtr(&texScalingEnable);

	CheckBox *deposterize = graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gs->T("Deposterize")));
	desposterizeEnable = !g_Config.bSoftwareRendering;
	deposterize->SetEnabledPtr(&desposterizeEnable);

	graphicsSettings->Add(new ItemHeader(gs->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	PopupMultiChoice *anisoFiltering = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Anisotropic Filtering"), anisoLevels, 0, ARRAY_SIZE(anisoLevels), gs, screenManager()));
	anisotropicEnable = !g_Config.bSoftwareRendering;
	anisoFiltering->SetEnabledPtr(&anisotropicEnable);

	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Linear on FMV", };
	PopupMultiChoice *texFilter = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Texture Filter"), texFilters, 1, ARRAY_SIZE(texFilters), gs, screenManager()));
	texFilteringEnable = !g_Config.bSoftwareRendering;
	texFilter->SetEnabledPtr(&texFilteringEnable);

	graphicsSettings->Add(new ItemHeader(gs->T("Hack Settings", "Hack Settings (these WILL cause glitches)")));
	graphicsSettings->Add(new CheckBox(&g_Config.bTimerHack, gs->T("Timer Hack")));
	CheckBox *alphaHack = graphicsSettings->Add(new CheckBox(&g_Config.bDisableAlphaTest, gs->T("Disable Alpha Test (PowerVR speedup)")));
	alphaHack->OnClick.Handle(this, &GameSettingsScreen::OnShaderChange);
	alphaHackEnable = !g_Config.bSoftwareRendering;
	alphaHack->SetEnabledPtr(&alphaHackEnable);

	CheckBox *stencilTest = graphicsSettings->Add(new CheckBox(&g_Config.bDisableStencilTest, gs->T("Disable Stencil Test")));
	stencilTestEnable = !g_Config.bSoftwareRendering;
	stencilTest->SetEnabledPtr(&stencilTestEnable);

	CheckBox *depthWrite = graphicsSettings->Add(new CheckBox(&g_Config.bAlwaysDepthWrite, gs->T("Always Depth Write")));
	depthWriteEnable = !g_Config.bSoftwareRendering;
	depthWrite->SetEnabledPtr(&depthWriteEnable);

	CheckBox *prescale = graphicsSettings->Add(new CheckBox(&g_Config.bPrescaleUV, gs->T("Texture Coord Speedhack")));
	prescaleEnable = !PSP_IsInited() && !g_Config.bSoftwareRendering;
	prescale->SetEnabledPtr(&prescaleEnable);

	graphicsSettings->Add(new ItemHeader(gs->T("Overlay Information")));
	static const char *fpsChoices[] = {
		"None", "Speed", "FPS", "Both"
#ifdef BLACKBERRY
     , "Statistics"
#endif
	};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS Counter"), fpsChoices, 0, ARRAY_SIZE(fpsChoices), gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gs->T("Show Debug Statistics")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);

	// Developer tools are not accessible ingame, so it goes here.
	graphicsSettings->Add(new ItemHeader(gs->T("Debugging")));
	Choice *dump = graphicsSettings->Add(new Choice(gs->T("Dump next frame to log")));
	dump->OnClick.Handle(this, &GameSettingsScreen::OnDumpNextFrameToLog);
	if (!PSP_IsInited())
		dump->SetEnabled(false);

	// We normally use software rendering to debug so put it in debugging.
	CheckBox *softwareGPU = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareRendering, gs->T("Software Rendering", "Software Rendering (experimental)")));
	softwareGPU->OnClick.Handle(this, &GameSettingsScreen::OnSoftwareRendering);
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
	static const char *latency[] = { "Low", "Medium", "High" };
	PopupMultiChoice *lowAudio = audioSettings->Add(new PopupMultiChoice(&g_Config.IaudioLatency, a->T("Audio Latency"), latency, 0, ARRAY_SIZE(latency), gs, screenManager()));
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

	// On systems that aren't Symbian, iOS, and Maemo, offer to let the user see this button.
	// Some Windows touch devices don't have a back button or other button to call up the menu.
#if !defined(__SYMBIAN32__) && !defined(IOS) && !defined(MAEMO)
	CheckBox *enablePauseBtn = controlsSettings->Add(new CheckBox(&g_Config.bShowTouchPause, c->T("Show Touch Pause Menu Button")));

	// Don't allow the user to disable it once in-game, so they can't lock themselves out of the menu.
	if (!PSP_IsInited()) {
		enablePauseBtn->SetEnabledPtr(&g_Config.bShowTouchControls);
	} else {
		enablePauseBtn->SetEnabled(false);
	}
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
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (Unstable)")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);

	systemSettings->Add(new CheckBox(&g_Config.bSeparateCPUThread, s->T("Multithreaded (experimental)")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new CheckBox(&g_Config.bSeparateIOThread, s->T("I/O on thread (experimental)")))->SetEnabled(!PSP_IsInited());
	systemSettings->Add(new CheckBox(&g_Config.bForceLagSync, s->T("Force real clock sync (slower, less lag)")));
	systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, s->T("Change CPU Clock", "Change CPU Clock (0 = default) (unstable)"), screenManager()));
#ifndef MOBILE_DEVICE
	systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindFlipFrequency, 0, 1800, s->T("Rewind Snapshot Frequency", "Rewind Snapshot Frequency (0 = off, mem hog)"), screenManager()));
#endif

	systemSettings->Add(new CheckBox(&g_Config.bAtomicAudioLocks, s->T("Atomic Audio locks (experimental)")))->SetEnabled(!PSP_IsInited());
#if defined(USING_WIN_UI)
	systemSettings->Add(new CheckBox(&g_Config.bBypassOSKWithKeyboard, s->T("Enable Windows native keyboard", "Enable Windows native keyboard")));
#endif

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
	systemSettings->Add(new Choice(s->T("Change proAdhocServer Address")))->OnClick.Handle(this, &GameSettingsScreen::OnChangeproAdhocServerAddress);
	systemSettings->Add(new Choice(s->T("Change Mac Address")))->OnClick.Handle(this, &GameSettingsScreen::OnChangeMacAddress);

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

UI::EventReturn GameSettingsScreen::OnSoftwareRendering(UI::EventParams &e) {
	prescaleEnable = !PSP_IsInited() && !g_Config.bSoftwareRendering;
	depthWriteEnable = !g_Config.bSoftwareRendering;
	stencilTestEnable = !g_Config.bSoftwareRendering;
	beziersEnable = !g_Config.bSoftwareRendering;
	texSecondaryEnable = !g_Config.bSoftwareRendering;
	swSkinningEnable = !PSP_IsInited() && !g_Config.bSoftwareRendering;
	hwTransformEnable = !g_Config.bSoftwareRendering;
	vtxCacheEnable = hwTransformEnable && g_Config.bHardwareTransform;
	texBackoffEnable = !g_Config.bSoftwareRendering;
	mipmapEnable = !g_Config.bSoftwareRendering;
	texScalingEnable = !g_Config.bSoftwareRendering;
	texScalingTypeEnable = !g_Config.bSoftwareRendering;
	desposterizeEnable = !g_Config.bSoftwareRendering;
	anisotropicEnable = !g_Config.bSoftwareRendering;
	texFilteringEnable = !g_Config.bSoftwareRendering;
	blockTransferEnable = !g_Config.bSoftwareRendering;
	renderModeEnable = !g_Config.bSoftwareRendering;
	alphaHackEnable = !g_Config.bSoftwareRendering;
	postProcEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnHardwareTransform(UI::EventParams &e) {
	hwTransformEnable = !g_Config.bSoftwareRendering;
	vtxCacheEnable = hwTransformEnable && g_Config.bHardwareTransform;
	return UI::EVENT_DONE;
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

	postProcEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionEnable = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
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
#if defined(USING_WIN_UI) || defined(USING_QT_UI)
	host->GoFullscreen(g_Config.bFullScreen);
#else
	// SDL, basically.
	System_SendMessage("toggle_fullscreen", "");
#endif
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

	g_Config.iFpsLimit = (iAlternateSpeedPercent_ * 60) / 100;
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

UI::EventReturn GameSettingsScreen::OnChangeproAdhocServerAddress(UI::EventParams &e) {	
#if defined(_WIN32) || defined(USING_QT_UI)	
	if (!g_Config.bFullScreen) {
		const size_t name_len = 256;

		char name[name_len];
		memset(name, 0, sizeof(name));

		if (System_InputBoxGetString("Enter an IP address", g_Config.proAdhocServer.c_str(), name, name_len)) {
			g_Config.proAdhocServer = name;
		}
	}
	else
		screenManager()->push(new ProAdhocServerScreen);
#else
	screenManager()->push(new ProAdhocServerScreen);
#endif
	
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeMacAddress(UI::EventParams &e) {
#if defined(_WIN32) || defined(USING_QT_UI)
	if (!g_Config.bFullScreen) {
		const size_t name_len = 256;

		char name[name_len];
		memset(name, 0, sizeof(name));

		if (System_InputBoxGetString("Enter a MAC address", g_Config.localMacAddress.c_str(), name, name_len)) {
			g_Config.localMacAddress = name;
		}
	}
	else
		screenManager()->push(new MacAddressScreen);
#else
	screenManager()->push(new MacAddressScreen);
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

	bool canUseJit = true;
#ifdef IOS
	if (!iosCanUseJit) {
		canUseJit = false;
		list->Add(new TextView(s->T("DynarecisJailed", "Dynarec (JIT) - (Not jailbroken - JIT not available)")));
	}
#endif
	if (canUseJit) {
		list->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")))->OnClick.Handle(this, &DeveloperToolsScreen::OnJitAffectingSetting);
	}

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

UI::EventReturn DeveloperToolsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
	return UI::EVENT_DONE;
}

void ProAdhocServerScreen::CreateViews() {
	using namespace UI;	
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *d = GetI18NCategory("Dialog");
	
	tempProAdhocServer = g_Config.proAdhocServer;
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	
	leftColumn->Add(new ItemHeader(s->T("proAdhocServer Address:")));
	addrView_ = new TextView(tempProAdhocServer, ALIGN_LEFT, false);
	leftColumn->Add(addrView_);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(0, 120, 10, NONE, NONE,10));
	rightColumn->Add(new Button("0"))->OnClick.Handle(this, &ProAdhocServerScreen::On0Click);
	rightColumn->Add(new Button("1"))->OnClick.Handle(this, &ProAdhocServerScreen::On1Click);
	rightColumn->Add(new Button("2"))->OnClick.Handle(this, &ProAdhocServerScreen::On2Click);
	rightColumn->Add(new Button("3"))->OnClick.Handle(this, &ProAdhocServerScreen::On3Click);
	rightColumn->Add(new Button("4"))->OnClick.Handle(this, &ProAdhocServerScreen::On4Click);
	rightColumn->Add(new Button("5"))->OnClick.Handle(this, &ProAdhocServerScreen::On5Click);
	rightColumn->Add(new Button("6"))->OnClick.Handle(this, &ProAdhocServerScreen::On6Click);
	rightColumn->Add(new Button("7"))->OnClick.Handle(this, &ProAdhocServerScreen::On7Click);
	rightColumn->Add(new Button("8"))->OnClick.Handle(this, &ProAdhocServerScreen::On8Click);
	rightColumn->Add(new Button("9"))->OnClick.Handle(this, &ProAdhocServerScreen::On9Click);
	rightColumn->Add(new Button("."))->OnClick.Handle(this, &ProAdhocServerScreen::OnPointClick);
	rightColumn->Add(new Button(d->T("Delete")))->OnClick.Handle(this, &ProAdhocServerScreen::OnDeleteClick);
	rightColumn->Add(new Button(d->T("Delete all")))->OnClick.Handle(this, &ProAdhocServerScreen::OnDeleteAllClick);
	rightColumn->Add(new Button(d->T("OK")))->OnClick.Handle(this, &ProAdhocServerScreen::OnOKClick);
	rightColumn->Add(new Button(d->T("Cancel")))->OnClick.Handle(this, &ProAdhocServerScreen::OnCancelClick);
	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

UI::EventReturn ProAdhocServerScreen::On0Click(UI::EventParams &e) {
	if (tempProAdhocServer.length() > 0)
		tempProAdhocServer.append("0");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On1Click(UI::EventParams &e) {
	tempProAdhocServer.append("1");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On2Click(UI::EventParams &e) {
	tempProAdhocServer.append("2");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On3Click(UI::EventParams &e) {
	tempProAdhocServer.append("3");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On4Click(UI::EventParams &e) {
	tempProAdhocServer.append("4");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On5Click(UI::EventParams &e) {
	tempProAdhocServer.append("5");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On6Click(UI::EventParams &e) {
	tempProAdhocServer.append("6");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On7Click(UI::EventParams &e) {
	tempProAdhocServer.append("7");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On8Click(UI::EventParams &e) {
	tempProAdhocServer.append("8");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::On9Click(UI::EventParams &e) {
	tempProAdhocServer.append("9");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}


UI::EventReturn ProAdhocServerScreen::OnPointClick(UI::EventParams &e) {
	if (tempProAdhocServer.length() > 0 && tempProAdhocServer.at(tempProAdhocServer.length() - 1) != '.')
		tempProAdhocServer.append(".");
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::OnDeleteClick(UI::EventParams &e) {
	if (tempProAdhocServer.length() > 0)
		tempProAdhocServer.erase(tempProAdhocServer.length() -1, 1);
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::OnDeleteAllClick(UI::EventParams &e) {
	tempProAdhocServer = "";
	addrView_->SetText(tempProAdhocServer);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::OnOKClick(UI::EventParams &e) {
	g_Config.proAdhocServer = tempProAdhocServer;
	UIScreen::OnBack(e);
	return UI::EVENT_DONE;
}

UI::EventReturn ProAdhocServerScreen::OnCancelClick(UI::EventParams &e) {
	tempProAdhocServer = g_Config.proAdhocServer;
	UIScreen::OnBack(e);
	return UI::EVENT_DONE;
}

void MacAddressScreen::CreateViews() {
	using namespace UI;
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *d = GetI18NCategory("Dialog");

	tempMacAddress = g_Config.localMacAddress;
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	leftColumn->Add(new ItemHeader(s->T("Mac address:")));
	addrView_ = new TextView(tempMacAddress, ALIGN_LEFT, false);
	leftColumn->Add(addrView_);
	LinearLayout *A_F_Column = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(0, 480, 10, 300, NONE, 150));
	A_F_Column->Add(new Button("A"))->OnClick.Handle(this, &MacAddressScreen::OnAClick);
	A_F_Column->Add(new Button("B"))->OnClick.Handle(this, &MacAddressScreen::OnBClick);
	A_F_Column->Add(new Button("C"))->OnClick.Handle(this, &MacAddressScreen::OnCClick);
	A_F_Column->Add(new Button("D"))->OnClick.Handle(this, &MacAddressScreen::OnDClick);
	A_F_Column->Add(new Button("E"))->OnClick.Handle(this, &MacAddressScreen::OnEClick);
	A_F_Column->Add(new Button("F"))->OnClick.Handle(this, &MacAddressScreen::OnFClick);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(0, 120, 10, NONE, NONE, 10));	
	rightColumn->Add(new Button("0"))->OnClick.Handle(this, &MacAddressScreen::On0Click);
	rightColumn->Add(new Button("1"))->OnClick.Handle(this, &MacAddressScreen::On1Click);
	rightColumn->Add(new Button("2"))->OnClick.Handle(this, &MacAddressScreen::On2Click);
	rightColumn->Add(new Button("3"))->OnClick.Handle(this, &MacAddressScreen::On3Click);
	rightColumn->Add(new Button("4"))->OnClick.Handle(this, &MacAddressScreen::On4Click);
	rightColumn->Add(new Button("5"))->OnClick.Handle(this, &MacAddressScreen::On5Click);
	rightColumn->Add(new Button("6"))->OnClick.Handle(this, &MacAddressScreen::On6Click);
	rightColumn->Add(new Button("7"))->OnClick.Handle(this, &MacAddressScreen::On7Click);
	rightColumn->Add(new Button("8"))->OnClick.Handle(this, &MacAddressScreen::On8Click);
	rightColumn->Add(new Button("9"))->OnClick.Handle(this, &MacAddressScreen::On9Click);
	rightColumn->Add(new Button(d->T("Delete")))->OnClick.Handle(this, &MacAddressScreen::OnDeleteClick);
	rightColumn->Add(new Button(d->T("Delete all")))->OnClick.Handle(this, &MacAddressScreen::OnDeleteAllClick);	
	rightColumn->Add(new Button(d->T("OK")))->OnClick.Handle(this, &MacAddressScreen::OnOKClick);
	rightColumn->Add(new Button(d->T("Cancel")))->OnClick.Handle(this, &MacAddressScreen::OnCancelClick);	
	root_->Add(leftColumn);
	root_->Add(A_F_Column);
	root_->Add(rightColumn);
}

UI::EventReturn MacAddressScreen::On0Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("0");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}	
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnAClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("A");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnBClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("B");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnCClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("C");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnDClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("D");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnEClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("E");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnFClick(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("F");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On1Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("1");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On2Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("2");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On3Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("3");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On4Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("4");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On5Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("5");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On6Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("6");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On7Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("7");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On8Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("8");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::On9Click(UI::EventParams &e) {
	if (tempMacAddress.length() < 17) {
		tempMacAddress.append("9");
		if (tempMacAddress.length() > 1 && tempMacAddress.length() < 17 && tempMacAddress.at(tempMacAddress.length() - 2) != ':')
			tempMacAddress.append(":");
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnDeleteClick(UI::EventParams &e) {
	if (tempMacAddress.length() > 0) {
		if (tempMacAddress.at(tempMacAddress.length() - 1) == ':')
			tempMacAddress.erase(tempMacAddress.length() - 1, 1);
		tempMacAddress.erase(tempMacAddress.length() - 1, 1);
		
	}
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnDeleteAllClick(UI::EventParams &e) {
	tempMacAddress = "";
	addrView_->SetText(tempMacAddress);
	return UI::EVENT_DONE;	
}

UI::EventReturn MacAddressScreen::OnOKClick(UI::EventParams &e) {
	if (tempMacAddress.length() == 17) {
		g_Config.localMacAddress = tempMacAddress;
		UIScreen::OnBack(e);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MacAddressScreen::OnCancelClick(UI::EventParams &e) {
	tempMacAddress = g_Config.proAdhocServer;
	UIScreen::OnBack(e);
	return UI::EVENT_DONE;
}
