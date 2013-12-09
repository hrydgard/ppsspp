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

#include "base/display.h"
#include "base/NativeApp.h"
#include "ext/vjson/json.h"
#include "file/ini_file.h"
#include "i18n/i18n.h"
#include "gfx_es2/gpu_features.h"
#include "net/http_client.h"
#include "util/text/parsers.h"

#include "Common/CPUDetect.h"
#include "Common/KeyMap.h"
#include "Common/FileUtil.h"
#include "Common/StringUtils.h"
#include "Config.h"
#include "HLE/sceUtility.h"

#ifndef USING_QT_UI
extern const char *PPSSPP_GIT_VERSION; 
#endif

// TODO: Find a better place for this.
http::Downloader g_DownloadManager;

Config g_Config;

#ifdef IOS
extern bool iosCanUseJit;
#endif

Config::Config() { }
Config::~Config() { }

void Config::Load(const char *iniFileName, const char *controllerIniFilename) {
	iniFilename_ = FindConfigFile(iniFileName != NULL ? iniFileName : "ppsspp.ini");
	controllerIniFilename_ = FindConfigFile(controllerIniFilename != NULL ? controllerIniFilename : "controls.ini");

	INFO_LOG(LOADER, "Loading config: %s", iniFilename_.c_str());
	bSaveSettings = true;

	IniFile iniFile;
	if (!iniFile.Load(iniFilename_)) {
		ERROR_LOG(LOADER, "Failed to read %s. Setting config to default.", iniFilename_.c_str());
		// Continue anyway to initialize the config.
	}

	IniFile::Section *general = iniFile.GetOrCreateSection("General");

	general->Get("FirstRun", &bFirstRun, true);
	general->Get("RunCount", &iRunCount, 0);
	iRunCount++;
	general->Get("Enable Logging", &bEnableLogging, true);
	general->Get("AutoRun", &bAutoRun, true);
	general->Get("Browse", &bBrowse, false);
	general->Get("IgnoreBadMemAccess", &bIgnoreBadMemAccess, true);
	general->Get("CurrentDirectory", &currentDirectory, "");
	general->Get("ShowDebuggerOnLoad", &bShowDebuggerOnLoad, false);
	general->Get("HomebrewStore", &bHomebrewStore, false);

	if (!File::Exists(currentDirectory))
		currentDirectory = "";

	std::string defaultLangRegion = "en_US";
	if (bFirstRun) {
		std::string langRegion = System_GetProperty(SYSPROP_LANGREGION);
		if (i18nrepo.IniExists(langRegion))
			defaultLangRegion = langRegion;
		// TODO: Be smart about same language, different country
	}

	general->Get("Language", &sLanguageIni, defaultLangRegion.c_str());
	general->Get("NumWorkerThreads", &iNumWorkerThreads, cpu_info.num_cores);
	general->Get("EnableAutoLoad", &bEnableAutoLoad, false);
	general->Get("EnableCheats", &bEnableCheats, false);
	general->Get("ScreenshotsAsPNG", &bScreenshotsAsPNG, false);
	general->Get("StateSlot", &iCurrentStateSlot, 0);
	general->Get("RewindFlipFrequency", &iRewindFlipFrequency, 0);
	general->Get("GridView1", &bGridView1, true);
	general->Get("GridView2", &bGridView2, true);
	general->Get("GridView3", &bGridView3, false);

	// "default" means let emulator decide, "" means disable.
	general->Get("ReportingHost", &sReportHost, "default");
	general->Get("Recent", recentIsos);
	general->Get("AutoSaveSymbolMap", &bAutoSaveSymbolMap, false);
#ifdef _WIN32
	general->Get("TopMost", &bTopMost);
	general->Get("WindowX", &iWindowX, -1); // -1 tells us to center the window.
	general->Get("WindowY", &iWindowY, -1);
	general->Get("WindowWidth", &iWindowWidth, 0);   // 0 will be automatically reset later (need to do the AdjustWindowRect dance).
	general->Get("WindowHeight", &iWindowHeight, 0);
	general->Get("PauseOnLostFocus", &bPauseOnLostFocus, false);
#endif

	IniFile::Section *recent = iniFile.GetOrCreateSection("Recent");
	recent->Get("MaxRecent", &iMaxRecent, 30);

	// Fix issue from switching from uint (hex in .ini) to int (dec)
	if (iMaxRecent == 0)
		iMaxRecent = 30;

	recentIsos.clear();
	for (int i = 0; i < iMaxRecent; i++)
	{
		char keyName[64];
		std::string fileName;

		sprintf(keyName,"FileName%d",i);
		if (!recent->Get(keyName,&fileName,"") || fileName.length() == 0) {
			// just skip it to get the next key
		}
		else {
			recentIsos.push_back(fileName);
		}
	}

	IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
#ifdef IOS
	cpu->Get("Jit", &bJit, iosCanUseJit);
#else
	cpu->Get("Jit", &bJit, true);
#endif
	cpu->Get("SeparateCPUThread", &bSeparateCPUThread, false);
	cpu->Get("AtomicAudioLocks", &bAtomicAudioLocks, false);

	cpu->Get("SeparateIOThread", &bSeparateIOThread, true);
	cpu->Get("FastMemoryAccess", &bFastMemory, true);
	cpu->Get("CPUSpeed", &iLockedCPUSpeed, 0);

	IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
	graphics->Get("ShowFPSCounter", &iShowFPSCounter, false);

	int renderingModeDefault = 1;  // Buffered
	if (System_GetProperty(SYSPROP_NAME) == "samsung:GT-S5360") {
		renderingModeDefault = 0;  // Non-buffered
	}

	graphics->Get("RenderingMode", &iRenderingMode, renderingModeDefault);
	graphics->Get("SoftwareRendering", &bSoftwareRendering, false);
	graphics->Get("HardwareTransform", &bHardwareTransform, true);
	graphics->Get("SoftwareSkinning", &bSoftwareSkinning, true);
	graphics->Get("TextureFiltering", &iTexFiltering, 1);
	// Auto on Windows, 2x on large screens, 1x elsewhere.
#if defined(_WIN32) && !defined(USING_QT_UI)
	graphics->Get("InternalResolution", &iInternalResolution, 0);
#else
	graphics->Get("InternalResolution", &iInternalResolution, pixel_xres >= 1024 ? 2 : 1);
#endif

	graphics->Get("FrameSkip", &iFrameSkip, 0);
	graphics->Get("FrameRate", &iFpsLimit, 0);
#ifdef _WIN32
	graphics->Get("FrameSkipUnthrottle", &bFrameSkipUnthrottle, false);
#else
	graphics->Get("FrameSkipUnthrottle", &bFrameSkipUnthrottle, true);
#endif
	graphics->Get("ForceMaxEmulatedFPS", &iForceMaxEmulatedFPS, 60);
#ifdef USING_GLES2
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 0);
#else
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 8);
#endif
	if (iAnisotropyLevel > 4) {
		iAnisotropyLevel = 4;
	}
	graphics->Get("VertexCache", &bVertexCache, true);
#ifdef IOS
	graphics->Get("VertexDecJit", &bVertexDecoderJit, iosCanUseJit);
#else
	graphics->Get("VertexDecJit", &bVertexDecoderJit, true);
#endif

#ifdef _WIN32
	graphics->Get("FullScreen", &bFullScreen, false);
#endif
	bool partialStretchDefault = false;
#ifdef BLACKBERRY
	partialStretchDefault = pixel_xres < 1.3 * pixel_yres;
#endif
	graphics->Get("PartialStretch", &bPartialStretch, partialStretchDefault);
	graphics->Get("StretchToDisplay", &bStretchToDisplay, false);
	graphics->Get("TrueColor", &bTrueColor, true);

	graphics->Get("MipMap", &bMipMap, false);

	graphics->Get("TexScalingLevel", &iTexScalingLevel, 1);
	graphics->Get("TexScalingType", &iTexScalingType, 0);
	graphics->Get("TexDeposterize", &bTexDeposterize, false);
	graphics->Get("VSyncInterval", &bVSync, false);
	graphics->Get("DisableStencilTest", &bDisableStencilTest, false);
	graphics->Get("AlwaysDepthWrite", &bAlwaysDepthWrite, false);
// Has been in use on Symbian since v0.7. Preferred option.
#ifdef __SYMBIAN32__
	graphics->Get("TimerHack", &bTimerHack, true);
#else
	graphics->Get("TimerHack", &bTimerHack, false);
#endif
	graphics->Get("LowQualitySplineBezier", &bLowQualitySplineBezier, false);
	graphics->Get("PostShader", &sPostShaderName, "Off");

	IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
	sound->Get("Enable", &bEnableSound, true);
	sound->Get("VolumeBGM", &iBGMVolume, 7);
	sound->Get("VolumeSFX", &iSFXVolume, 7);
	sound->Get("LowLatency", &bLowLatencyAudio, false);

	IniFile::Section *control = iniFile.GetOrCreateSection("Control");
	control->Get("HapticFeedback", &bHapticFeedback, true);
	control->Get("ShowAnalogStick", &bShowTouchAnalogStick, true);
	control->Get("ShowTouchCross", &bShowTouchCross, true);
	control->Get("ShowTouchCircle", &bShowTouchCircle, true);
	control->Get("ShowTouchSquare", &bShowTouchSquare, true);
	control->Get("ShowTouchTriangle", &bShowTouchTriangle, true);
	control->Get("ShowTouchStart", &bShowTouchStart, true);
	control->Get("ShowTouchSelect", &bShowTouchSelect, true);
	control->Get("ShowTouchLTrigger", &bShowTouchLTrigger, true);
	control->Get("ShowTouchRTrigger", &bShowTouchRTrigger, true);
	control->Get("ShowAnalogStick", &bShowTouchAnalogStick, true);
	control->Get("ShowTouchDpad", &bShowTouchDpad, true);
	control->Get("ShowTouchUnthrottle", &bShowTouchUnthrottle, true);

#if defined(USING_GLES2)
	std::string name = System_GetProperty(SYSPROP_NAME);
	if (KeyMap::HasBuiltinController(name)) {
		control->Get("ShowTouchControls", &bShowTouchControls, false);
	} else {
		control->Get("ShowTouchControls", &bShowTouchControls, true);
	}
#else
	control->Get("ShowTouchControls", &bShowTouchControls, false);
#endif
	// control->Get("KeyMapping",iMappingMap);
#ifdef USING_GLES2
	control->Get("AccelerometerToAnalogHoriz", &bAccelerometerToAnalogHoriz, false);

	control->Get("TiltBaseX", &fTiltBaseX, 0);
	control->Get("TiltBaseY", &fTiltBaseY, 0);
	control->Get("InvertTiltX", &bInvertTiltX, false);
	control->Get("InvertTiltY", &bInvertTiltY, true);
	control->Get("TiltSensitivityX", &iTiltSensitivityX, 100);
	control->Get("TiltSensitivityY", &iTiltSensitivityY, 100);
	control->Get("DeadzoneRadius", &fDeadzoneRadius, 0.35);

#endif
	control->Get("DisableDpadDiagonals", &bDisableDpadDiagonals, false);
	control->Get("TouchButtonOpacity", &iTouchButtonOpacity, 65);
	//set these to -1 if not initialized. initializing these
	//requires pixel coordinates which is not known right now.
	//will be initialized in GamepadEmu::CreatePadLayout
	float defaultScale = 1.15f;
	control->Get("ActionButtonSpacing2", &fActionButtonSpacing, 1.0f);
	control->Get("ActionButtonCenterX", &fActionButtonCenterX, -1.0);
	control->Get("ActionButtonCenterY", &fActionButtonCenterY, -1.0);
	control->Get("ActionButtonScale", &fActionButtonScale, defaultScale);
	control->Get("DPadX", &fDpadX, -1.0);
	control->Get("DPadY", &fDpadY, -1.0);
	control->Get("DPadScale", &fDpadScale, defaultScale);
	control->Get("DPadSpacing", &fDpadSpacing, 1.0f);
	control->Get("StartKeyX", &fStartKeyX, -1.0);
	control->Get("StartKeyY", &fStartKeyY, -1.0);
	control->Get("StartKeyScale", &fStartKeyScale, defaultScale);
	control->Get("SelectKeyX", &fSelectKeyX, -1.0);
	control->Get("SelectKeyY", &fSelectKeyY, -1.0);
	control->Get("SelectKeyScale", &fSelectKeyScale, defaultScale);
	control->Get("UnthrottleKeyX", &fUnthrottleKeyX, -1.0);
	control->Get("UnthrottleKeyY", &fUnthrottleKeyY, -1.0);
	control->Get("UnthrottleKeyScale", &fUnthrottleKeyScale, defaultScale);
	control->Get("LKeyX", &fLKeyX, -1.0);
	control->Get("LKeyY", &fLKeyY, -1.0);
	control->Get("LKeyScale", &fLKeyScale, defaultScale);
	control->Get("RKeyX", &fRKeyX, -1.0);
	control->Get("RKeyY", &fRKeyY, -1.0);
	control->Get("RKeyScale", &fRKeyScale, defaultScale);
	control->Get("AnalogStickX", &fAnalogStickX, -1.0);
	control->Get("AnalogStickY", &fAnalogStickY, -1.0);
	control->Get("AnalogStickScale", &fAnalogStickScale, defaultScale);

	// MIGRATION: For users who had the old static touch layout, aren't I nice?
	if (fDpadX > 1.0 || fDpadY > 1.0) // Likely the rest are too!
	{
		fActionButtonCenterX /= dp_xres;
		fActionButtonCenterY /= dp_yres;
		fDpadX /= dp_xres;
		fDpadY /= dp_yres;
		fStartKeyX /= dp_xres;
		fStartKeyY /= dp_yres;
		fSelectKeyX /= dp_xres;
		fSelectKeyY /= dp_yres;
		fUnthrottleKeyX /= dp_xres;
		fUnthrottleKeyY /= dp_yres;
		fLKeyX /= dp_xres;
		fLKeyY /= dp_yres;
		fRKeyX /= dp_xres;
		fRKeyY /= dp_yres;
		fAnalogStickX /= dp_xres;
		fAnalogStickY /= dp_yres;
	}

	IniFile::Section *network = iniFile.GetOrCreateSection("Network");
	network->Get("EnableWlan", &bEnableWlan, false);

	IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
#if !defined(ARM)
	pspConfig->Get("PSPModel", &iPSPModel, PSP_MODEL_SLIM);
#else
	pspConfig->Get("PSPModel", &iPSPModel, PSP_MODEL_FAT);
#endif
	pspConfig->Get("NickName", &sNickName, "PPSSPP");
	pspConfig->Get("proAdhocServer", &proAdhocServer, "localhost");
	pspConfig->Get("MacAddress", &localMacAddress, "01:02:03:04:05:06");
	pspConfig->Get("Language", &iLanguage, PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	pspConfig->Get("TimeFormat", &iTimeFormat, PSP_SYSTEMPARAM_TIME_FORMAT_24HR);
	pspConfig->Get("DateFormat", &iDateFormat, PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD);
	pspConfig->Get("TimeZone", &iTimeZone, 0);
	pspConfig->Get("DayLightSavings", &bDayLightSavings, PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD);
	pspConfig->Get("ButtonPreference", &iButtonPreference, PSP_SYSTEMPARAM_BUTTON_CROSS);
	pspConfig->Get("LockParentalLevel", &iLockParentalLevel, 0);
	pspConfig->Get("WlanAdhocChannel", &iWlanAdhocChannel, PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC);
#ifdef _WIN32
	pspConfig->Get("BypassOSKWithKeyboard", &bBypassOSKWithKeyboard, false);
#endif
	pspConfig->Get("WlanPowerSave", &bWlanPowerSave, PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF);
	pspConfig->Get("EncryptSave", &bEncryptSave, true);

	IniFile::Section *debugConfig = iniFile.GetOrCreateSection("Debugger");
	debugConfig->Get("DisasmWindowX", &iDisasmWindowX, -1);
	debugConfig->Get("DisasmWindowY", &iDisasmWindowY, -1);
	debugConfig->Get("DisasmWindowW", &iDisasmWindowW, -1);
	debugConfig->Get("DisasmWindowH", &iDisasmWindowH, -1);
	debugConfig->Get("GEWindowX", &iGEWindowX, -1);
	debugConfig->Get("GEWindowY", &iGEWindowY, -1);
	debugConfig->Get("GEWindowW", &iGEWindowW, -1);
	debugConfig->Get("GEWindowH", &iGEWindowH, -1);
	debugConfig->Get("ConsoleWindowX", &iConsoleWindowX, -1);
	debugConfig->Get("ConsoleWindowY", &iConsoleWindowY, -1);
	debugConfig->Get("FontWidth", &iFontWidth, 8);
	debugConfig->Get("FontHeight", &iFontHeight, 12);
	debugConfig->Get("DisplayStatusBar", &bDisplayStatusBar, true);
	debugConfig->Get("ShowBottomTabTitles",&bShowBottomTabTitles,true);
	debugConfig->Get("ShowDeveloperMenu", &bShowDeveloperMenu, false);
	debugConfig->Get("SkipDeadbeefFilling", &bSkipDeadbeefFilling, false);
	debugConfig->Get("FuncHashMap", &bFuncHashMap, false);

	IniFile::Section *speedhacks = iniFile.GetOrCreateSection("SpeedHacks");
	speedhacks->Get("PrescaleUV", &bPrescaleUV, false);
	speedhacks->Get("DisableAlphaTest", &bDisableAlphaTest, false);

	IniFile::Section *jitConfig = iniFile.GetOrCreateSection("JIT");
	jitConfig->Get("DiscardRegsOnJRRA", &bDiscardRegsOnJRRA, false);

	IniFile::Section *upgrade = iniFile.GetOrCreateSection("Upgrade");
	upgrade->Get("UpgradeMessage", &upgradeMessage, "");
	upgrade->Get("UpgradeVersion", &upgradeVersion, "");
	upgrade->Get("DismissedVersion", &dismissedVersion, "");
	
	if (dismissedVersion == upgradeVersion) {
		upgradeMessage = "";
	}

	// Check for new version on every 5 runs.
	// Sometimes the download may not be finished when the main screen shows (if the user dismisses the
	// splash screen quickly), but then we'll just show the notification next time instead, we store the
	// upgrade number in the ini.
	if (iRunCount % 5 == 0) {
		g_DownloadManager.StartDownloadWithCallback(
			"http://www.ppsspp.org/version.json", "", &DownloadCompletedCallback);
	}

	INFO_LOG(LOADER, "Loading controller config: %s", controllerIniFilename_.c_str());
	bSaveSettings = true;

	IniFile controllerIniFile;
	if (!controllerIniFile.Load(controllerIniFilename_)) {
		ERROR_LOG(LOADER, "Failed to read %s. Setting controller config to default.", controllerIniFilename_.c_str());
		KeyMap::RestoreDefault();
	} else {
		// Continue anyway to initialize the config. It will just restore the defaults.
		KeyMap::LoadFromIni(controllerIniFile);
	}

	CleanRecent();
}

void Config::Save() {
	if (iniFilename_.size() && g_Config.bSaveSettings) {
		CleanRecent();
		IniFile iniFile;
		if (!iniFile.Load(iniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't read ini %s", iniFilename_.c_str());
		}

		IniFile::Section *general = iniFile.GetOrCreateSection("General");

		// Need to do this somewhere...
		bFirstRun = false;
		general->Set("FirstRun", bFirstRun);
		general->Set("RunCount", iRunCount);
		general->Set("Enable Logging", bEnableLogging);
		general->Set("AutoRun", bAutoRun);
		general->Set("Browse", bBrowse);
		general->Set("IgnoreBadMemAccess", bIgnoreBadMemAccess);
		general->Set("CurrentDirectory", currentDirectory);
		general->Set("ShowDebuggerOnLoad", bShowDebuggerOnLoad);
		general->Set("ReportingHost", sReportHost);
		general->Set("AutoSaveSymbolMap", bAutoSaveSymbolMap);
#ifdef _WIN32
		general->Set("TopMost", bTopMost);
		general->Set("WindowX", iWindowX);
		general->Set("WindowY", iWindowY);
		general->Set("WindowWidth", iWindowWidth);
		general->Set("WindowHeight", iWindowHeight);
		general->Set("PauseOnLostFocus", bPauseOnLostFocus);
#endif
		general->Set("Language", sLanguageIni);
		general->Set("NumWorkerThreads", iNumWorkerThreads);
		general->Set("EnableAutoLoad", bEnableAutoLoad);
		general->Set("EnableCheats", bEnableCheats);
		general->Set("ScreenshotsAsPNG", bScreenshotsAsPNG);
		general->Set("StateSlot", iCurrentStateSlot);
		general->Set("RewindFlipFrequency", iRewindFlipFrequency);
		general->Set("GridView1", bGridView1);
		general->Set("GridView2", bGridView2);
		general->Set("GridView3", bGridView3);

		IniFile::Section *recent = iniFile.GetOrCreateSection("Recent");
		recent->Set("MaxRecent", iMaxRecent);

		for (int i = 0; i < iMaxRecent; i++) {
			char keyName[64];
			sprintf(keyName,"FileName%d",i);
			if (i < (int)recentIsos.size()) {
				recent->Set(keyName, recentIsos[i]);
			} else {
				recent->Delete(keyName); // delete the nonexisting FileName
			}
		}

		IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
		cpu->Set("Jit", bJit);
		cpu->Set("SeparateCPUThread", bSeparateCPUThread);
		cpu->Set("AtomicAudioLocks", bAtomicAudioLocks);
		cpu->Set("SeparateIOThread", bSeparateIOThread);
		cpu->Set("FastMemoryAccess", bFastMemory);
		cpu->Set("CPUSpeed", iLockedCPUSpeed);

		IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
		graphics->Set("ShowFPSCounter", iShowFPSCounter);
		graphics->Set("RenderingMode", iRenderingMode);
		graphics->Set("SoftwareRendering", bSoftwareRendering);
		graphics->Set("HardwareTransform", bHardwareTransform);
		graphics->Set("SoftwareSkinning", bSoftwareSkinning);
		graphics->Set("TextureFiltering", iTexFiltering);
		graphics->Set("InternalResolution", iInternalResolution);
		graphics->Set("FrameSkip", iFrameSkip);
		graphics->Set("FrameRate", iFpsLimit);
		graphics->Set("FrameSkipUnthrottle", bFrameSkipUnthrottle);
		graphics->Set("ForceMaxEmulatedFPS", iForceMaxEmulatedFPS);
		graphics->Set("AnisotropyLevel", iAnisotropyLevel);
		graphics->Set("VertexCache", bVertexCache);
#ifdef _WIN32
		graphics->Set("FullScreen", bFullScreen);
#endif
		graphics->Set("PartialStretch", bPartialStretch);
		graphics->Set("StretchToDisplay", bStretchToDisplay);
		graphics->Set("TrueColor", bTrueColor);
		graphics->Set("MipMap", bMipMap);
		graphics->Set("TexScalingLevel", iTexScalingLevel);
		graphics->Set("TexScalingType", iTexScalingType);
		graphics->Set("TexDeposterize", bTexDeposterize);
		graphics->Set("VSyncInterval", bVSync);
		graphics->Set("DisableStencilTest", bDisableStencilTest);
		graphics->Set("AlwaysDepthWrite", bAlwaysDepthWrite);
		graphics->Set("TimerHack", bTimerHack);
		graphics->Set("LowQualitySplineBezier", bLowQualitySplineBezier);
		graphics->Set("PostShader", sPostShaderName);

		IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
		sound->Set("Enable", bEnableSound);
		sound->Set("VolumeBGM", iBGMVolume);
		sound->Set("VolumeSFX", iSFXVolume);
		sound->Set("LowLatency", bLowLatencyAudio);

		IniFile::Section *control = iniFile.GetOrCreateSection("Control");
		control->Set("HapticFeedback", bHapticFeedback);
		control->Set("ShowTouchControls", bShowTouchControls);
		control->Set("ShowTouchCross", bShowTouchCross);
		control->Set("ShowTouchCircle", bShowTouchCircle);
		control->Set("ShowTouchSquare", bShowTouchSquare);
		control->Set("ShowTouchTriangle", bShowTouchTriangle);
		control->Set("ShowTouchStart", bShowTouchStart);
		control->Set("ShowTouchSelect", bShowTouchSelect);
		control->Set("ShowTouchLTrigger", bShowTouchLTrigger);
		control->Set("ShowTouchRTrigger", bShowTouchRTrigger);
		control->Set("ShowAnalogStick", bShowTouchAnalogStick);
		control->Set("ShowTouchUnthrottle", bShowTouchUnthrottle);
		control->Set("ShowTouchDpad", bShowTouchDpad);

#ifdef USING_GLES2
		control->Set("AccelerometerToAnalogHoriz", bAccelerometerToAnalogHoriz);
		control->Set("TiltBaseX", fTiltBaseX);
		control->Set("TiltBaseY", fTiltBaseY);
		control->Set("InvertTiltX", bInvertTiltX);
		control->Set("InvertTiltY", bInvertTiltY);
		control->Set("TiltSensitivityX", iTiltSensitivityX);
		control->Set("TiltSensitivityY", iTiltSensitivityY);
		control->Set("DeadzoneRadius", fDeadzoneRadius);
#endif
		control->Set("DisableDpadDiagonals", bDisableDpadDiagonals);

		control->Set("TouchButtonOpacity", iTouchButtonOpacity);
		control->Set("ActionButtonScale", fActionButtonScale);
		control->Set("ActionButtonSpacing2", fActionButtonSpacing);
		control->Set("ActionButtonCenterX", fActionButtonCenterX);
		control->Set("ActionButtonCenterY", fActionButtonCenterY);
		control->Set("DPadX", fDpadX);
		control->Set("DPadY", fDpadY);
		control->Set("DPadScale", fDpadScale);
		control->Set("DPadSpacing", fDpadSpacing);
		control->Set("StartKeyX", fStartKeyX);
		control->Set("StartKeyY", fStartKeyY);
		control->Set("StartKeyScale", fStartKeyScale);
		control->Set("SelectKeyX", fSelectKeyX);
		control->Set("SelectKeyY", fSelectKeyY);
		control->Set("SelectKeyScale", fSelectKeyScale);
		control->Set("UnthrottleKeyX", fUnthrottleKeyX);
		control->Set("UnthrottleKeyY", fUnthrottleKeyY);
		control->Set("UnthrottleKeyScale", fUnthrottleKeyScale);
		control->Set("LKeyX", fLKeyX);
		control->Set("LKeyY", fLKeyY);
		control->Set("LKeyScale", fLKeyScale);
		control->Set("RKeyX", fRKeyX);
		control->Set("RKeyY", fRKeyY);
		control->Set("RKeyScale", fRKeyScale);
		control->Set("AnalogStickX", fAnalogStickX);
		control->Set("AnalogStickY", fAnalogStickY);
		control->Set("AnalogStickScale", fAnalogStickScale);

		IniFile::Section *network = iniFile.GetOrCreateSection("Network");
		network->Set("EnableWlan", bEnableWlan);

		IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
		pspConfig->Set("PSPModel", iPSPModel);
		pspConfig->Set("NickName", sNickName.c_str());
		pspConfig->Set("proAdhocServer", proAdhocServer.c_str());
		pspConfig->Set("MacAddress", localMacAddress.c_str());
		pspConfig->Set("Language", iLanguage);
		pspConfig->Set("TimeFormat", iTimeFormat);
		pspConfig->Set("DateFormat", iDateFormat);
		pspConfig->Set("TimeZone", iTimeZone);
		pspConfig->Set("DayLightSavings", bDayLightSavings);
		pspConfig->Set("ButtonPreference", iButtonPreference);
		pspConfig->Set("LockParentalLevel", iLockParentalLevel);
		pspConfig->Set("WlanAdhocChannel", iWlanAdhocChannel);
		pspConfig->Set("WlanPowerSave", bWlanPowerSave);
		pspConfig->Set("EncryptSave", bEncryptSave);
#ifdef _WIN32
		pspConfig->Set("BypassOSKWithKeyboard", bBypassOSKWithKeyboard);
#endif

		IniFile::Section *debugConfig = iniFile.GetOrCreateSection("Debugger");
		debugConfig->Set("DisasmWindowX", iDisasmWindowX);
		debugConfig->Set("DisasmWindowY", iDisasmWindowY);
		debugConfig->Set("DisasmWindowW", iDisasmWindowW);
		debugConfig->Set("DisasmWindowH", iDisasmWindowH);
		debugConfig->Set("GEWindowX", iGEWindowX);
		debugConfig->Set("GEWindowY", iGEWindowY);
		debugConfig->Set("GEWindowW", iGEWindowW);
		debugConfig->Set("GEWindowH", iGEWindowH);
		debugConfig->Set("ConsoleWindowX", iConsoleWindowX);
		debugConfig->Set("ConsoleWindowY", iConsoleWindowY);
		debugConfig->Set("FontWidth", iFontWidth);
		debugConfig->Set("FontHeight", iFontHeight);
		debugConfig->Set("DisplayStatusBar", bDisplayStatusBar);
		debugConfig->Set("ShowBottomTabTitles",bShowBottomTabTitles);
		debugConfig->Set("ShowDeveloperMenu", bShowDeveloperMenu);
		debugConfig->Set("SkipDeadbeefFilling", bSkipDeadbeefFilling);
		debugConfig->Set("FuncHashMap", bFuncHashMap);

		IniFile::Section *speedhacks = iniFile.GetOrCreateSection("SpeedHacks");
		speedhacks->Set("PrescaleUV", bPrescaleUV);
		speedhacks->Set("DisableAlphaTest", bDisableAlphaTest);

		// Save upgrade check state
		IniFile::Section *upgrade = iniFile.GetOrCreateSection("Upgrade");
		upgrade->Set("UpgradeMessage", upgradeMessage);
		upgrade->Set("UpgradeVersion", upgradeVersion);
		upgrade->Set("DismissedVersion", dismissedVersion);

		if (!iniFile.Save(iniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't write ini %s", iniFilename_.c_str());
			return;
		}
		INFO_LOG(LOADER, "Config saved: %s", iniFilename_.c_str());


		IniFile controllerIniFile;
		if (!controllerIniFile.Load(controllerIniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't read ini %s", controllerIniFilename_.c_str());
		}
		KeyMap::SaveToIni(controllerIniFile);
		if (!controllerIniFile.Save(controllerIniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't write ini %s", controllerIniFilename_.c_str());
			return;
		}
		INFO_LOG(LOADER, "Controller config saved: %s", controllerIniFilename_.c_str());

	} else {
		INFO_LOG(LOADER, "Not saving config");
	}
}

// Use for debugging the version check without messing with the server
#if 0
#define PPSSPP_GIT_VERSION "v0.0.1-gaaaaaaaaa"
#endif

void Config::DownloadCompletedCallback(http::Download &download) {
	if (download.ResultCode() != 200) {
		ERROR_LOG(LOADER, "Failed to download version.json");
		return;
	}
	std::string data;
	download.buffer().TakeAll(&data);
	if (data.empty()) {
		ERROR_LOG(LOADER, "Version check: Empty data from server!");
		return;
	}

	JsonReader reader(data.c_str(), data.size());
	const json_value *root = reader.root();
	std::string version = root->getString("version", "");

	const char *gitVer = PPSSPP_GIT_VERSION;
	Version installed(gitVer);
	Version upgrade(version);
	Version dismissed(g_Config.dismissedVersion);

	if (!installed.IsValid()) {
		ERROR_LOG(LOADER, "Version check: Local version string invalid. Build problems? %s", PPSSPP_GIT_VERSION);
		return;
	}
	if (!upgrade.IsValid()) {
		ERROR_LOG(LOADER, "Version check: Invalid server version: %s", version.c_str());
		return;
	}

	if (installed >= upgrade) {
		INFO_LOG(LOADER, "Version check: Already up to date, erasing any upgrade message");
		g_Config.upgradeMessage = "";
		g_Config.upgradeVersion = upgrade.ToString();
		g_Config.dismissedVersion = "";
		return;
	}

	if (installed < upgrade && dismissed != upgrade) {
		g_Config.upgradeMessage = "New version of PPSSPP available!";
		g_Config.upgradeVersion = upgrade.ToString();
		g_Config.dismissedVersion = "";
	}
}

void Config::DismissUpgrade() {
	g_Config.dismissedVersion = g_Config.upgradeVersion;
}

void Config::AddRecent(const std::string &file) {
	for (auto str = recentIsos.begin(); str != recentIsos.end(); ++str) {
#ifdef _WIN32
		if (!strcmpIgnore((*str).c_str(), file.c_str(), "\\", "/")) {
#else
		if (!strcmp((*str).c_str(), file.c_str())) {
#endif
			recentIsos.erase(str);
			recentIsos.insert(recentIsos.begin(), file);
			if ((int)recentIsos.size() > iMaxRecent)
				recentIsos.resize(iMaxRecent);
			return;
		}
	}
	recentIsos.insert(recentIsos.begin(), file);
	if ((int)recentIsos.size() > iMaxRecent)
		recentIsos.resize(iMaxRecent);
}

void Config::CleanRecent() {
	std::vector<std::string> cleanedRecent;
	for (size_t i = 0; i < recentIsos.size(); i++) {
		if (File::Exists(recentIsos[i])) {
			// clean the redundant recent games' list.
			if (cleanedRecent.size()==0) { // add first one
				cleanedRecent.push_back(recentIsos[i]);
			}
			for (size_t j = 0; j < cleanedRecent.size();j++) {
				if (cleanedRecent[j] == recentIsos[i])
					break; // skip if found redundant
				if (j == cleanedRecent.size() - 1){ // add if no redundant found
					cleanedRecent.push_back(recentIsos[i]);
				}
			}
		}
	}
	recentIsos = cleanedRecent;
}

void Config::SetDefaultPath(const std::string &defaultPath) {
	defaultPath_ = defaultPath;
}

void Config::AddSearchPath(const std::string &path) {
	searchPath_.push_back(path);
}

const std::string Config::FindConfigFile(const std::string &baseFilename) {
	// Don't search for an absolute path.
	if (baseFilename.size() > 1 && baseFilename[0] == '/') {
		return baseFilename;
	}
#ifdef _WIN32
	if (baseFilename.size() > 3 && baseFilename[1] == ':' && (baseFilename[2] == '/' || baseFilename[2] == '\\')) {
		return baseFilename;
	}
#endif

	for (size_t i = 0; i < searchPath_.size(); ++i) {
		std::string filename = searchPath_[i] + baseFilename;
		if (File::Exists(filename)) {
			return filename;
		}
	}

	const std::string filename = defaultPath_.empty() ? baseFilename : defaultPath_ + baseFilename;
	if (!File::Exists(filename)) {
		std::string path;
		SplitPath(filename, &path, NULL, NULL);
		File::CreateFullPath(path);
	}
	return filename;
}

void Config::RestoreDefaults() {
	if(File::Exists(iniFilename_))
		File::Delete(iniFilename_);
	recentIsos.clear();
	currentDirectory = "";
	Load();
}
