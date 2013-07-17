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

#include "CommonTypes.h"

extern const char *PPSSPP_GIT_VERSION;

struct Config
{
public:
	Config();
	~Config();

	// Whether to save the config on close.
	bool bSaveSettings;

	bool bFirstRun;

	// These are broken
	bool bAutoLoadLast;
	bool bSpeedLimit;
	bool bConfirmOnQuit;
	bool bAutoRun;  // start immediately
	bool bBrowse;
#ifdef _WIN32
	bool bTopMost;
#endif

	// General
	bool bNewUI;  // "Hidden" setting, does not get saved to ini file.
	int iNumWorkerThreads;
	bool bScreenshotsAsPNG;

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	bool bJit;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::string languageIni;

	// GFX
	bool bDisplayFramebuffer;
	bool bHardwareTransform;
	bool bBufferedRendering;
	int iTexFiltering; // 1 = off , 2 = nearest , 3 = linear , 4 = linear(CG)
	bool bUseVBO;
#ifdef BLACKBERRY
	bool bPartialStretch;
#endif
	bool bStretchToDisplay;
	int iVSyncInterval;
	int iFrameSkip;

	int iWindowX;
	int iWindowY;
	int iWindowZoom;  // for Windows
	bool SSAntiAliasing; // for Windows, too
	bool bVertexCache;
	bool bFullScreen;
#ifdef _WIN32
	bool bFullScreenOnLaunch;
#endif
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	bool bTrueColor;
	bool bFramebuffersToMem;
	bool bFramebuffersCPUConvert; // for OpenGL devices
	bool bMipMap;
	int iTexScalingLevel; // 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	int iFpsLimit;
	int iForceMaxEmulatedFPS;
	int iMaxRecent;
	bool bEnableCheats;
	bool bReloadCheats;

	// Sound
	bool bEnableSound;
	bool bEnableAtrac3plus;
	int iSEVolume;
	int iBGMVolume;

	// UI
	bool bShowTouchControls;
	bool bShowDebuggerOnLoad;
	bool bShowAnalogStick;
	int iShowFPSCounter;
	bool bShowDebugStats;
	bool bLargeControls;
	bool bAccelerometerToAnalogHoriz;

	// Control
	int iTouchButtonOpacity;
	float fButtonScale;

	// SystemParam
	std::string sNickName;
	int ilanguage;
	int iTimeFormat;
	int iDateFormat;
	int iTimeZone;
	bool bDayLightSavings;
	int iButtonPreference;
	int iLockParentalLevel;
	bool bEncryptSave;
	int iWlanAdhocChannel;
	bool bWlanPowerSave;

	// Debugger
	int iDisasmWindowX;
	int iDisasmWindowY;
	int iDisasmWindowW;
	int iDisasmWindowH;
	int iConsoleWindowX;
	int iConsoleWindowY;
	int iFontWidth;
	int iFontHeight;

	std::string currentDirectory;
	std::string externalDirectory; 
	std::string memCardDirectory;
	std::string flashDirectory;
	std::string internalDataDirectory;

	void Load(const char *iniFileName = "ppsspp.ini");
	void Save();

	// Utility functions for "recent" management
	void AddRecent(const std::string &file);
	void CleanRecent();

private:
	std::string iniFilename_;
};

extern Config g_Config;
