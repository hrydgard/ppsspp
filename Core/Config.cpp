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
	NOTICE_LOG(LOADER, "Loading config: %s", iniFileName);
	bSaveSettings = true;

	IniFile iniFile;
	iniFile.Load(iniFileName);

	IniFile::Section *general = iniFile.GetOrCreateSection("General");

	bSpeedLimit = false;
	general->Get("FirstRun", &bFirstRun, true);
	general->Get("AutoLoadLast", &bAutoLoadLast, false);
	general->Get("AutoRun", &bAutoRun, false);
	general->Get("Jit", &bJIT, false);
	general->Get("ConfirmOnQuit", &bConfirmOnQuit, false);
	general->Get("IgnoreBadMemAccess", &bIgnoreBadMemAccess, true);
	general->Get("DisplayFramebuffer", &bDisplayFramebuffer, false);
	general->Get("CurrentDirectory", &currentDirectory, "");
	general->Get("ShowFPSCounter", &bShowFPSCounter, false);
	general->Get("WindowZoom", &iWindowZoom, 1);

	IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
	sound->Get("Enable", &bEnableSound, true);

	IniFile::Section *control = iniFile.GetOrCreateSection("Control");
	control->Get("ShowStick", &bShowAnalogStick, false);
}

void CConfig::Save()
{
	if (g_Config.bSaveSettings && iniFilename_.size())
	{
		IniFile iniFile;
		iniFile.Load(iniFilename_.c_str());

		IniFile::Section *general = iniFile.GetOrCreateSection("General");
		general->Set("FirstRun", bFirstRun);
		general->Set("AutoLoadLast", bAutoLoadLast);
		general->Set("AutoRun", bAutoRun);
		general->Set("Jit", bJIT);
		general->Set("ConfirmOnQuit", bConfirmOnQuit);
		general->Set("IgnoreBadMemAccess", bIgnoreBadMemAccess);
		general->Set("DisplayFramebuffer", bDisplayFramebuffer);
		general->Set("CurrentDirectory", currentDirectory);
		general->Set("ShowFPSCounter", bShowFPSCounter);
		general->Set("WindowZoom", iWindowZoom);

		IniFile::Section *sound = iniFile.GetOrCreateSection("Sound");
		sound->Set("Enable", bEnableSound);

		IniFile::Section *control = iniFile.GetOrCreateSection("Control");
		control->Set("ShowStick", bShowAnalogStick);

		iniFile.Save(iniFilename_.c_str());
		NOTICE_LOG(LOADER, "Config saved: %s", iniFilename_.c_str());
	} else {
		NOTICE_LOG(LOADER, "Error saving config: %s", iniFilename_.c_str());
	}
}
