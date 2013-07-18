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
#include "Common/KeyMap.h"
#include "Common/FileUtil.h"
#include "Config.h"
#include "file/ini_file.h"
#include "HLE/sceUtility.h"
#include "Common/CPUDetect.h"

Config g_Config;

#ifdef IOS
extern bool isJailed;
#endif

Config::Config() { }
Config::~Config() { }

void Config::Load(const char *iniFileName)
{
	iniFilename_ = iniFileName;
	INFO_LOG(LOADER, "Loading config: %s", iniFileName);
	bSaveSettings = true;

	IniFile iniFile;
	if (!iniFile.Load(iniFileName)) {
		ERROR_LOG(LOADER, "Failed to read %s. Setting config to default.", iniFileName);
		// Continue anyway to initialize the config.
	}

	IniFile::Section *general = iniFile.GetOrCreateSection("General");

	bSpeedLimit = false;
	general->Get("FirstRun", &bFirstRun, true);
	general->Get("NewUI", &bNewUI, false);
	general->Get("AutoLoadLast", &bAutoLoadLast, false);
	general->Get("AutoRun", &bAutoRun, true);
	general->Get("Browse", &bBrowse, false);
	general->Get("ConfirmOnQuit", &bConfirmOnQuit, false);
	general->Get("IgnoreBadMemAccess", &bIgnoreBadMemAccess, true);
	general->Get("CurrentDirectory", &currentDirectory, "");
	general->Get("ShowDebuggerOnLoad", &bShowDebuggerOnLoad, false);
	general->Get("Language", &languageIni, "en_US");
	general->Get("NumWorkerThreads", &iNumWorkerThreads, cpu_info.num_cores);
	general->Get("EnableCheats", &bEnableCheats, false);
	general->Get("MaxRecent", &iMaxRecent, 12);
	general->Get("ScreenshotsAsPNG", &bScreenshotsAsPNG, false);

	// Fix issue from switching from uint (hex in .ini) to int (dec)
	if (iMaxRecent == 0)
		iMaxRecent = 12;

	// "default" means let emulator decide, "" means disable.
	general->Get("ReportHost", &sReportHost, "default");
	general->Get("Recent", recentIsos);
	general->Get("AutoSaveSymbolMap", &bAutoSaveSymbolMap, false);
#ifdef _WIN32
	general->Get("TopMost", &bTopMost);
	general->Get("WindowX", &iWindowX, 40);
	general->Get("WindowY", &iWindowY, 100);
#endif

	if ((int)recentIsos.size() > iMaxRecent)
		recentIsos.resize(iMaxRecent);

	IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
#ifdef IOS
	cpu->Get("Jit", &bJit, !isJailed);
#else
	cpu->Get("Jit", &bJit, true);
#endif
	cpu->Get("FastMemory", &bFastMemory, false);
	cpu->Get("CPUSpeed", &iLockedCPUSpeed, false);

	IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
	graphics->Get("ShowFPSCounter", &iShowFPSCounter, false);
	graphics->Get("DisplayFramebuffer", &bDisplayFramebuffer, false);
#ifdef _WIN32
	graphics->Get("ResolutionScale", &iWindowZoom, 2);
#else
	graphics->Get("ResolutionScale", &iWindowZoom, 1);
#endif
	graphics->Get("BufferedRendering", &bBufferedRendering, true);
	graphics->Get("HardwareTransform", &bHardwareTransform, true);
	graphics->Get("TextureFiltering", &iTexFiltering, 1);
	graphics->Get("SSAA", &SSAntiAliasing, 0);
	graphics->Get("VBO", &bUseVBO, false);
	graphics->Get("FrameSkip", &iFrameSkip, 0);
	graphics->Get("FrameRate", &iFpsLimit, 0);
	graphics->Get("ForceMaxEmulatedFPS", &iForceMaxEmulatedFPS, 0);
#ifdef USING_GLES2
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 0);
#else
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 8);
#endif
	if (iAnisotropyLevel > 4) {
		iAnisotropyLevel = 4;
	}
	graphics->Get("VertexCache", &bVertexCache, true);
#ifdef _WIN32
	graphics->Get("FullScreen", &bFullScreen, false);
	graphics->Get("FullScreenOnLaunch", &bFullScreenOnLaunch, false);
#endif
#ifdef BLACKBERRY
	graphics->Get("PartialStretch", &bPartialStretch, pixel_xres == pixel_yres);
#endif
	graphics->Get("StretchToDisplay", &bStretchToDisplay, false);
	graphics->Get("TrueColor", &bTrueColor, true);
	graphics->Get("FramebuffersToMem", &bFramebuffersToMem, false);
	graphics->Get("FramebuffersCPUConvert", &bFramebuffersCPUConvert, false);
	graphics->Get("MipMap", &bMipMap, true);
	graphics->Get("TexScalingLevel", &iTexScalingLevel, 1);
	graphics->Get("TexScalingType", &iTexScalingType, 0);
	graphics->Get("TexDeposterize", &bTexDeposterize, false);
	graphics->Get("VSyncInterval", &iVSyncInterval, 0);

	IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
	sound->Get("Enable", &bEnableSound, true);
	sound->Get("EnableAtrac3plus", &bEnableAtrac3plus, true);
	sound->Get("BGMVolume", &iBGMVolume, 5);
	sound->Get("SEVolume", &iSEVolume, 5);
	
	IniFile::Section *control = iniFile.GetOrCreateSection("Control");
	control->Get("ShowStick", &bShowAnalogStick, false);
#ifdef BLACKBERRY
	control->Get("ShowTouchControls", &bShowTouchControls, pixel_xres != pixel_yres);
#elif defined(USING_GLES2)
	control->Get("ShowTouchControls", &bShowTouchControls, true);
#else
	control->Get("ShowTouchControls", &bShowTouchControls, false);
#endif
	control->Get("LargeControls", &bLargeControls, false);
	// control->Get("KeyMapping",iMappingMap);
	control->Get("AccelerometerToAnalogHoriz", &bAccelerometerToAnalogHoriz, false);
	control->Get("TouchButtonOpacity", &iTouchButtonOpacity, 65);
	control->Get("ButtonScale", &fButtonScale, 1.15);

	IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
	pspConfig->Get("NickName", &sNickName, "PPSSPP");
	pspConfig->Get("Language", &ilanguage, PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	pspConfig->Get("TimeFormat", &iTimeFormat, PSP_SYSTEMPARAM_TIME_FORMAT_24HR);
	pspConfig->Get("DateFormat", &iDateFormat, PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD);
	pspConfig->Get("TimeZone", &iTimeZone, 0);
	pspConfig->Get("DayLightSavings", &bDayLightSavings, PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD);
	pspConfig->Get("ButtonPreference", &iButtonPreference, PSP_SYSTEMPARAM_BUTTON_CROSS);
	pspConfig->Get("LockParentalLevel", &iLockParentalLevel, 0);
	pspConfig->Get("WlanAdhocChannel", &iWlanAdhocChannel, PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC);
	pspConfig->Get("WlanPowerSave", &bWlanPowerSave, PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF);
	pspConfig->Get("EncryptSave", &bEncryptSave, true);

	IniFile::Section *debugConfig = iniFile.GetOrCreateSection("Debugger");
	debugConfig->Get("DisasmWindowX", &iDisasmWindowX, -1);
	debugConfig->Get("DisasmWindowY", &iDisasmWindowY, -1);
	debugConfig->Get("DisasmWindowW", &iDisasmWindowW, -1);
	debugConfig->Get("DisasmWindowH", &iDisasmWindowH, -1);
	debugConfig->Get("ConsoleWindowX", &iConsoleWindowX, -1);
	debugConfig->Get("ConsoleWindowY", &iConsoleWindowY, -1);
	debugConfig->Get("FontWidth", &iFontWidth, 8);
	debugConfig->Get("FontHeight", &iFontHeight, 12);

	KeyMap::LoadFromIni(iniFile);

	CleanRecent();
}

void Config::Save()
{
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
		general->Set("NewUI", bNewUI);

		general->Set("AutoLoadLast", bAutoLoadLast);
		general->Set("AutoRun", bAutoRun);
		general->Set("Browse", bBrowse);
		general->Set("ConfirmOnQuit", bConfirmOnQuit);
		general->Set("IgnoreBadMemAccess", bIgnoreBadMemAccess);
		general->Set("CurrentDirectory", currentDirectory);
		general->Set("ShowDebuggerOnLoad", bShowDebuggerOnLoad);
		general->Set("ReportHost", sReportHost);
		general->Set("Recent", recentIsos);
		general->Set("AutoSaveSymbolMap", bAutoSaveSymbolMap);
#ifdef _WIN32
		general->Set("TopMost", bTopMost);
		general->Set("WindowX", iWindowX);
		general->Set("WindowY", iWindowY);
#endif
		general->Set("Language", languageIni);
		general->Set("NumWorkerThreads", iNumWorkerThreads);
		general->Set("MaxRecent", iMaxRecent);
		general->Set("EnableCheats", bEnableCheats);
		general->Set("ScreenshotsAsPNG", bScreenshotsAsPNG);

		IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
		cpu->Set("Jit", bJit);
		cpu->Set("FastMemory", bFastMemory);
		cpu->Set("CPUSpeed", iLockedCPUSpeed);

		IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
		graphics->Set("ShowFPSCounter", iShowFPSCounter);
		graphics->Set("DisplayFramebuffer", bDisplayFramebuffer);
		graphics->Set("ResolutionScale", iWindowZoom);
		graphics->Set("BufferedRendering", bBufferedRendering);
		graphics->Set("HardwareTransform", bHardwareTransform);
		graphics->Set("TextureFiltering", iTexFiltering);
		graphics->Set("SSAA", SSAntiAliasing);
		graphics->Set("VBO", bUseVBO);
		graphics->Set("FrameSkip", iFrameSkip);
		graphics->Set("FrameRate", iFpsLimit);
		graphics->Set("ForceMaxEmulatedFPS", iForceMaxEmulatedFPS);
		graphics->Set("AnisotropyLevel", iAnisotropyLevel);
		graphics->Set("VertexCache", bVertexCache);
#ifdef _WIN32
		graphics->Set("FullScreen", bFullScreen);
		graphics->Set("FullScreenOnLaunch", bFullScreenOnLaunch);
#endif		
#ifdef BLACKBERRY
		graphics->Set("PartialStretch", bPartialStretch);
#endif
		graphics->Set("StretchToDisplay", bStretchToDisplay);
		graphics->Set("TrueColor", bTrueColor);
		graphics->Set("FramebuffersToMem", bFramebuffersToMem);
		graphics->Set("FramebuffersCPUConvert", bFramebuffersCPUConvert);
		graphics->Set("MipMap", bMipMap);
		graphics->Set("TexScalingLevel", iTexScalingLevel);
		graphics->Set("TexScalingType", iTexScalingType);
		graphics->Set("TexDeposterize", bTexDeposterize);
		graphics->Set("VSyncInterval", iVSyncInterval);

		IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
		sound->Set("Enable", bEnableSound);
		sound->Set("EnableAtrac3plus", bEnableAtrac3plus);
		sound->Set("BGMVolume", iBGMVolume);
		sound->Set("SEVolume", iSEVolume);

		IniFile::Section *control = iniFile.GetOrCreateSection("Control");
		control->Set("ShowStick", bShowAnalogStick);
		control->Set("ShowTouchControls", bShowTouchControls);
		control->Set("LargeControls", bLargeControls);
		// control->Set("KeyMapping",iMappingMap);
		control->Set("AccelerometerToAnalogHoriz", bAccelerometerToAnalogHoriz);
		control->Set("TouchButtonOpacity", iTouchButtonOpacity);
		control->Set("ButtonScale", fButtonScale);

		IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
		pspConfig->Set("NickName", sNickName.c_str());
		pspConfig->Set("Language", ilanguage);
		pspConfig->Set("TimeFormat", iTimeFormat);
		pspConfig->Set("DateFormat", iDateFormat);
		pspConfig->Set("TimeZone", iTimeZone);
		pspConfig->Set("DayLightSavings", bDayLightSavings);
		pspConfig->Set("ButtonPreference", iButtonPreference);
		pspConfig->Set("LockParentalLevel", iLockParentalLevel);
		pspConfig->Set("WlanAdhocChannel", iWlanAdhocChannel);
		pspConfig->Set("WlanPowerSave", bWlanPowerSave);
		pspConfig->Set("EncryptSave", bEncryptSave);

		IniFile::Section *debugConfig = iniFile.GetOrCreateSection("Debugger");
		debugConfig->Set("DisasmWindowX", iDisasmWindowX);
		debugConfig->Set("DisasmWindowY", iDisasmWindowY);
		debugConfig->Set("DisasmWindowW", iDisasmWindowW);
		debugConfig->Set("DisasmWindowH", iDisasmWindowH);
		debugConfig->Set("ConsoleWindowX", iConsoleWindowX);
		debugConfig->Set("ConsoleWindowY", iConsoleWindowY);
		debugConfig->Set("FontWidth", iFontWidth);
		debugConfig->Set("FontHeight", iFontHeight);

		KeyMap::SaveToIni(iniFile);

		if (!iniFile.Save(iniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't write ini %s", iniFilename_.c_str());
			return;
		}
		INFO_LOG(LOADER, "Config saved: %s", iniFilename_.c_str());
	} else {
		INFO_LOG(LOADER, "Not saving config");
	}
}

void Config::AddRecent(const std::string &file) {
	for (auto str = recentIsos.begin(); str != recentIsos.end(); str++) {
		if (*str == file) {
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
		if (File::Exists(recentIsos[i]))
			cleanedRecent.push_back(recentIsos[i]);
	}
	recentIsos = cleanedRecent;
}
