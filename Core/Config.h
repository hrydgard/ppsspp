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

#pragma once

#include <string>


struct SState
{
	bool bEmuThreadStarted;	// is anything loaded into the emulator?
	bool bBooted;
};

struct CConfig
{
public:
	CConfig();
	~CConfig();

	// Whether to save the config on close.
	bool bSaveSettings;

	// These are broken
	bool bAutoLoadLast;
	bool bFirstRun;
	bool bSpeedLimit;
	bool bConfirmOnQuit;
	bool bAutoRun;  // start immediately

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	int iCpuCore;

	// GFX
	bool bDisplayFramebuffer;
	bool bHardwareTransform;
	bool bBufferedRendering;
	bool bDrawWireframe;
	bool bLinearFiltering;
	bool bUseVBO;
	int iWindowZoom;  // for Windows
	bool SSAntiAlaising; //for Windows, too

	// Sound
	bool bEnableSound;

	// UI
	bool bShowTouchControls;
	bool bShowDebuggerOnLoad;
	bool bShowAnalogStick;
	bool bShowFPSCounter;
	bool bShowDebugStats;

	std::string currentDirectory;
	std::string memCardDirectory;
	std::string flashDirectory;

	void Load(const char *iniFileName = "ppsspp.ini");
	void Save();
private:
	std::string iniFilename_;
};

extern SState g_State;
extern CConfig g_Config;
