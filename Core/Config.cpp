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
#include "Common/KeyMap.h"
#include "Common/FileUtil.h"
#include "Config.h"
#include "file/ini_file.h"
#include "i18n/i18n.h"
#include "HLE/sceUtility.h"
#include "Common/CPUDetect.h"

Config g_Config;

#ifdef IOS
extern bool isJailed;
#endif

Config::Config() { }
Config::~Config() { }

void Config::Load(const char *iniFileName, const char *controllerIniFilename)
{
	iniFilename_ = iniFileName;
	controllerIniFilename_ = controllerIniFilename;
	INFO_LOG(LOADER, "Loading config: %s", iniFileName);
	bSaveSettings = true;

	IniFile iniFile;
	if (!iniFile.Load(iniFileName)) {
		ERROR_LOG(LOADER, "Failed to read %s. Setting config to default.", iniFileName);
		// Continue anyway to initialize the config.
	}

	IniFile::Section *general = iniFile.GetOrCreateSection("General");

	general->Get("FirstRun", &bFirstRun, true);
	general->Get("Enable Logging", &bEnableLogging, true);
	general->Get("AutoRun", &bAutoRun, true);
	general->Get("Browse", &bBrowse, false);
	general->Get("IgnoreBadMemAccess", &bIgnoreBadMemAccess, true);
	general->Get("CurrentDirectory", &currentDirectory, "");
	general->Get("ShowDebuggerOnLoad", &bShowDebuggerOnLoad, false);

	std::string defaultLangRegion = "en_US";
	if (bFirstRun) {
		std::string langRegion = System_GetProperty(SYSPROP_LANGREGION);
		if (i18nrepo.IniExists(langRegion))
			defaultLangRegion = langRegion;
		// TODO: Be smart about same language, different country
	}

	general->Get("Language", &languageIni, defaultLangRegion.c_str());
	general->Get("NumWorkerThreads", &iNumWorkerThreads, cpu_info.num_cores);
	general->Get("EnableCheats", &bEnableCheats, false);
	general->Get("ScreenshotsAsPNG", &bScreenshotsAsPNG, false);
	general->Get("StateSlot", &iCurrentStateSlot, 0);
	general->Get("GridView1", &bGridView1, true);
	general->Get("GridView2", &bGridView2, true);
	general->Get("GridView3", &bGridView3, true);


	// "default" means let emulator decide, "" means disable.
	general->Get("ReportingHost", &sReportHost, "default");
	general->Get("Recent", recentIsos);
	general->Get("AutoSaveSymbolMap", &bAutoSaveSymbolMap, false);
#ifdef _WIN32
	general->Get("TopMost", &bTopMost);
	general->Get("WindowX", &iWindowX, 40);
	general->Get("WindowY", &iWindowY, 100);
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
	cpu->Get("Jit", &bJit, !isJailed);
#else
	cpu->Get("Jit", &bJit, true);
#endif
	cpu->Get("SeparateCPUThread", &bSeparateCPUThread, false);
#ifdef __SYMBIAN32__
	cpu->Get("SeparateIOThread", &bSeparateIOThread, false);
#else
	cpu->Get("SeparateIOThread", &bSeparateIOThread, true);
#endif
	cpu->Get("FastMemory", &bFastMemory, false);
	cpu->Get("CPUSpeed", &iLockedCPUSpeed, 0);

	IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
	graphics->Get("ShowFPSCounter", &iShowFPSCounter, false);
#ifdef _WIN32
	graphics->Get("ResolutionScale", &iWindowZoom, 2);
#else
	graphics->Get("ResolutionScale", &iWindowZoom, 1);
#endif
	graphics->Get("RenderingMode", &iRenderingMode, 
		// Many ARMv6 devices have serious problems with buffered rendering.
#if defined(ARM) && !defined(ARMV7)
		0
#else
		1
#endif
		); // default is buffered rendering mode
	graphics->Get("SoftwareRendering", &bSoftwareRendering, false);
	graphics->Get("HardwareTransform", &bHardwareTransform, true);
	graphics->Get("TextureFiltering", &iTexFiltering, 1);
	graphics->Get("SSAA", &bAntiAliasing, 0);
	graphics->Get("FrameSkip", &iFrameSkip, 0);
	graphics->Get("FrameRate", &iFpsLimit, 0);
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
#ifdef _WIN32
	graphics->Get("FullScreen", &bFullScreen, false);
	graphics->Get("FullScreenOnLaunch", &bFullScreenOnLaunch, false);
#endif
#ifdef BLACKBERRY
	graphics->Get("PartialStretch", &bPartialStretch, pixel_xres == pixel_yres);
#endif
	graphics->Get("StretchToDisplay", &bStretchToDisplay, false);
	graphics->Get("TrueColor", &bTrueColor, true);
	graphics->Get("MipMap", &bMipMap, true);
	graphics->Get("TexScalingLevel", &iTexScalingLevel, 1);
	graphics->Get("TexScalingType", &iTexScalingType, 0);
	graphics->Get("TexDeposterize", &bTexDeposterize, false);
	graphics->Get("VSyncInterval", &bVSync, false);
	graphics->Get("DisableStencilTest", &bDisableStencilTest, false);
	graphics->Get("AlwaysDepthWrite", &bAlwaysDepthWrite, false);

	IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
	sound->Get("Enable", &bEnableSound, true);
	sound->Get("EnableAtrac3plus", &bEnableAtrac3plus, true);
	sound->Get("VolumeBGM", &iBGMVolume, 7);
	sound->Get("VolumeSFX", &iSFXVolume, 7);
	sound->Get("LowLatency", &bLowLatencyAudio, false);

	IniFile::Section *control = iniFile.GetOrCreateSection("Control");
	control->Get("ShowAnalogStick", &bShowAnalogStick, true);
#ifdef BLACKBERRY
	control->Get("ShowTouchControls", &bShowTouchControls, pixel_xres != pixel_yres);
#elif defined(USING_GLES2)
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
	debugConfig->Get("ConsoleWindowX", &iConsoleWindowX, -1);
	debugConfig->Get("ConsoleWindowY", &iConsoleWindowY, -1);
	debugConfig->Get("FontWidth", &iFontWidth, 8);
	debugConfig->Get("FontHeight", &iFontHeight, 12);
	debugConfig->Get("DisplayStatusBar", &bDisplayStatusBar, true);
	debugConfig->Get("ShowDeveloperMenu", &bShowDeveloperMenu, false);

	IniFile::Section *gleshacks = iniFile.GetOrCreateSection("GLESHacks");
	gleshacks->Get("PrescaleUV", &bPrescaleUV, false);

	INFO_LOG(LOADER, "Loading controller config: %s", controllerIniFilename);
	bSaveSettings = true;

	IniFile controllerIniFile;
	if (!controllerIniFile.Load(controllerIniFilename)) {
		ERROR_LOG(LOADER, "Failed to read %s. Setting controller config to default.", controllerIniFilename);
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
#endif
		general->Set("Language", languageIni);
		general->Set("NumWorkerThreads", iNumWorkerThreads);
		general->Set("EnableCheats", bEnableCheats);
		general->Set("ScreenshotsAsPNG", bScreenshotsAsPNG);
		general->Set("StateSlot", iCurrentStateSlot);
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
		cpu->Set("SeparateIOThread", bSeparateIOThread);
		cpu->Set("FastMemory", bFastMemory);
		cpu->Set("CPUSpeed", iLockedCPUSpeed);

		IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
		graphics->Set("ShowFPSCounter", iShowFPSCounter);
		graphics->Set("ResolutionScale", iWindowZoom);
		graphics->Set("RenderingMode", iRenderingMode);
		graphics->Set("SoftwareRendering", bSoftwareRendering);
		graphics->Set("HardwareTransform", bHardwareTransform);
		graphics->Set("TextureFiltering", iTexFiltering);
		graphics->Set("SSAA", bAntiAliasing);
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
		graphics->Set("MipMap", bMipMap);
		graphics->Set("TexScalingLevel", iTexScalingLevel);
		graphics->Set("TexScalingType", iTexScalingType);
		graphics->Set("TexDeposterize", bTexDeposterize);
		graphics->Set("VSyncInterval", bVSync);
		graphics->Set("DisableStencilTest", bDisableStencilTest);
		graphics->Set("AlwaysDepthWrite", bAlwaysDepthWrite);

		IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
		sound->Set("Enable", bEnableSound);
		sound->Set("EnableAtrac3plus", bEnableAtrac3plus);
		sound->Set("VolumeBGM", iBGMVolume);
		sound->Set("VolumeSFX", iSFXVolume);
		sound->Set("LowLatency", bLowLatencyAudio);

		IniFile::Section *control = iniFile.GetOrCreateSection("Control");
		control->Set("ShowAnalogStick", bShowAnalogStick);
		control->Set("ShowTouchControls", bShowTouchControls);
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
#ifdef _WIN32
		pspConfig->Set("BypassOSKWithKeyboard", bBypassOSKWithKeyboard);
#endif

		IniFile::Section *debugConfig = iniFile.GetOrCreateSection("Debugger");
		debugConfig->Set("DisasmWindowX", iDisasmWindowX);
		debugConfig->Set("DisasmWindowY", iDisasmWindowY);
		debugConfig->Set("DisasmWindowW", iDisasmWindowW);
		debugConfig->Set("DisasmWindowH", iDisasmWindowH);
		debugConfig->Set("ConsoleWindowX", iConsoleWindowX);
		debugConfig->Set("ConsoleWindowY", iConsoleWindowY);
		debugConfig->Set("FontWidth", iFontWidth);
		debugConfig->Set("FontHeight", iFontHeight);
		debugConfig->Set("DisplayStatusBar", bDisplayStatusBar);
		debugConfig->Set("ShowDeveloperMenu", bShowDeveloperMenu);

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
		if (File::Exists(recentIsos[i])){
			// clean the redundant recent games' list.
			if (cleanedRecent.size()==0){ // add first one
					cleanedRecent.push_back(recentIsos[i]);
			}
			for (size_t j=0; j<cleanedRecent.size();j++){
				if (cleanedRecent[j]==recentIsos[i])
					break; // skip if found redundant
				if (j==cleanedRecent.size()-1){ // add if no redundant found
					cleanedRecent.push_back(recentIsos[i]);
				}
			}
		}
	}
	recentIsos = cleanedRecent;
}

void Config::RestoreDefaults() {
	if(File::Exists("ppsspp.ini"))
		File::Delete("ppsspp.ini");
	recentIsos.clear();
	currentDirectory = "";
	Load();
}
