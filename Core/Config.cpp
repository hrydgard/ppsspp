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


#include "Config.h"
#include "IniFile.h"
#include "HLE/sceUtility.h"

SState g_State;
CConfig g_Config;

CConfig::CConfig()
{
}

CConfig::~CConfig()
{
}

void CConfig::Load(const char *iniFileName)
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
	general->Get("AutoLoadLast", &bAutoLoadLast, false);
	general->Get("AutoRun", &bAutoRun, true);
	general->Get("Browse", &bBrowse, false);
	general->Get("ConfirmOnQuit", &bConfirmOnQuit, false);
	general->Get("IgnoreBadMemAccess", &bIgnoreBadMemAccess, true);
	general->Get("CurrentDirectory", &currentDirectory, "");
	general->Get("ShowDebuggerOnLoad", &bShowDebuggerOnLoad, false);

	IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
	cpu->Get("Jit", &bJit, true);
	cpu->Get("FastMemory", &bFastMemory, true);

	IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
	graphics->Get("ShowFPSCounter", &bShowFPSCounter, false);
	graphics->Get("DisplayFramebuffer", &bDisplayFramebuffer, false);
	graphics->Get("WindowZoom", &iWindowZoom, 1);
	graphics->Get("BufferedRendering", &bBufferedRendering, true);
	graphics->Get("HardwareTransform", &bHardwareTransform, true);
	graphics->Get("LinearFiltering", &bLinearFiltering, false);
	graphics->Get("SSAA", &SSAntiAliasing, 0);
	graphics->Get("VBO", &bUseVBO, false);
	graphics->Get("FrameSkip", &iFrameSkip, 0);
#ifdef USING_GLES2
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 0);
#else
	graphics->Get("AnisotropyLevel", &iAnisotropyLevel, 8);
#endif
	graphics->Get("DisableG3DLog", &bDisableG3DLog, false);
	graphics->Get("VertexCache", &bVertexCache, true);
	graphics->Get("FullScreen", &bFullScreen, false);	
	graphics->Get("StretchToDisplay", &bStretchToDisplay, false);
	graphics->Get("TrueColor", &bTrueColor, true);

	IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
	sound->Get("Enable", &bEnableSound, true);

	IniFile::Section *control = iniFile.GetOrCreateSection("Control");
	control->Get("ShowStick", &bShowAnalogStick, false);
	control->Get("ShowTouchControls", &bShowTouchControls,
#ifdef USING_GLES2
		true);
#else
		false);
#endif
	control->Get("LargeControls", &bLargeControls, false);
	control->Get("KeyMapping",iMappingMap);

	IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
	pspConfig->Get("Language", &ilanguage, PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	pspConfig->Get("TimeFormat", &itimeformat, PSP_SYSTEMPARAM_TIME_FORMAT_24HR);
	pspConfig->Get("EncryptSave", &bEncryptSave, true);

	// Ephemeral settings
	bDrawWireframe = false;
}

void CConfig::Save()
{
	if (iniFilename_.size() && g_Config.bSaveSettings) {
		IniFile iniFile;
		if (!iniFile.Load(iniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't read ini %s", iniFilename_.c_str());
		}

		IniFile::Section *general = iniFile.GetOrCreateSection("General");
		general->Set("FirstRun", bFirstRun);
		general->Set("AutoLoadLast", bAutoLoadLast);
		general->Set("AutoRun", bAutoRun);
		general->Set("Browse", bBrowse);
		general->Set("ConfirmOnQuit", bConfirmOnQuit);
		general->Set("IgnoreBadMemAccess", bIgnoreBadMemAccess);
		general->Set("CurrentDirectory", currentDirectory);
		general->Set("ShowDebuggerOnLoad", bShowDebuggerOnLoad);
		IniFile::Section *cpu = iniFile.GetOrCreateSection("CPU");
		cpu->Set("Jit", bJit);
		cpu->Set("FastMemory", bFastMemory);

		IniFile::Section *graphics = iniFile.GetOrCreateSection("Graphics");
		graphics->Set("ShowFPSCounter", bShowFPSCounter);
		graphics->Set("DisplayFramebuffer", bDisplayFramebuffer);
		graphics->Set("WindowZoom", iWindowZoom);
		graphics->Set("BufferedRendering", bBufferedRendering);
		graphics->Set("HardwareTransform", bHardwareTransform);
		graphics->Set("LinearFiltering", bLinearFiltering);
		graphics->Set("SSAA", SSAntiAliasing);
		graphics->Set("VBO", bUseVBO);
		graphics->Set("FrameSkip", iFrameSkip);
		graphics->Set("AnisotropyLevel", iAnisotropyLevel);
		graphics->Set("DisableG3DLog", bDisableG3DLog);
		graphics->Set("VertexCache", bVertexCache);
		graphics->Set("FullScreen", bFullScreen);
		graphics->Set("StretchToDisplay", bStretchToDisplay);
		graphics->Set("TrueColor", bTrueColor);

		IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
		sound->Set("Enable", bEnableSound);

		IniFile::Section *control = iniFile.GetOrCreateSection("Control");
		control->Set("ShowStick", bShowAnalogStick);
		control->Set("ShowTouchControls", bShowTouchControls);
		control->Set("LargeControls", bLargeControls);
		control->Set("KeyMapping",iMappingMap);

		IniFile::Section *pspConfig = iniFile.GetOrCreateSection("SystemParam");
		pspConfig->Set("Language", ilanguage);
		pspConfig->Set("TimeFormat", itimeformat);
		pspConfig->Set("EncryptSave", bEncryptSave);

		if (!iniFile.Save(iniFilename_.c_str())) {
			ERROR_LOG(LOADER, "Error saving config - can't write ini %s", iniFilename_.c_str());
			return;
		}
		INFO_LOG(LOADER, "Config saved: %s", iniFilename_.c_str());
	} else {
		INFO_LOG(LOADER, "Not saving config");
	}
}
