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
#include <vector>
#include <map>

extern const char *PPSSPP_GIT_VERSION;

struct Config
{
public:
	Config();
	~Config();

	// Whether to save the config on close.
	bool bSaveSettings;

	// These are broken
	bool bAutoLoadLast;
	bool bFirstRun;
	bool bSpeedLimit;
	bool bConfirmOnQuit;
	bool bAutoRun;  // start immediately
	bool bBrowse;

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	bool bJit;
	bool bAutoSaveSymbolMap;
	std::string sReportHost;
	std::vector<std::string> recentIsos;

	// GFX
	bool bDisplayFramebuffer;
	bool bHardwareTransform;
	bool bBufferedRendering;
	bool bDrawWireframe;
	bool bLinearFiltering;
	bool bUseVBO;
	bool bStretchToDisplay;
	int iFrameSkip;  // 0 = off;  1 = auto;  (future:  2 = skip every 2nd frame;  3 = skip every 3rd frame etc).
	bool bUseMediaEngine;

	int iWindowX;
	int iWindowY;
	int iWindowZoom;  // for Windows
	bool SSAntiAliasing; //for Windows, too
	bool bVertexCache;
	bool bFullScreen;
	int iAnisotropyLevel;
	bool bTrueColor;
	bool bMipMap;

	// Sound
	bool bEnableSound;

	// UI
	bool bShowTouchControls;
	bool bShowDebuggerOnLoad;
	bool bShowAnalogStick;
	bool bShowFPSCounter;
	bool bShowDebugStats;
	bool bLargeControls;
	bool bAccelerometerToAnalogHoriz;

	// Control
	std::map<int,int> iMappingMap; // Can be used differently depending on systems

	// SystemParam
	int ilanguage;
	int itimeformat;
	bool bEncryptSave;

	std::string currentDirectory;
	std::string memCardDirectory;
	std::string flashDirectory;

	void Load(const char *iniFileName = "ppsspp.ini");
	void Save();

	// Utility functions for "recent" management
	void AddRecent(const std::string &file);
	void CleanRecent();

private:
	std::string iniFilename_;
};

extern Config g_Config;
