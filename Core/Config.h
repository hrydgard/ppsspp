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
	int iNumWorkerThreads;

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	bool bJit;
	bool bAutoSaveSymbolMap;
	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::string languageIni;

	// GFX
	bool bDisplayFramebuffer;
	bool bHardwareTransform;
	bool bBufferedRendering;
	bool bNearestFiltering;
	bool bLinearFiltering;
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
	int iAnisotropyLevel;
	bool bTrueColor;
	bool bMipMap;
	int iTexScalingLevel; // 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	int iFpsLimit;
	int iMaxRecent;
	bool bEnableCheats;
	bool bReloadCheats;

	// Sound
	bool bEnableSound;
	bool bEnableAtrac3plus;

	// UI
	bool bShowTouchControls;
	bool bShowDebuggerOnLoad;
	bool bShowAnalogStick;
	int iShowFPSCounter;
	bool bShowDebugStats;
	bool bLargeControls;
	bool bAccelerometerToAnalogHoriz;
	// Temporary until control mapping rewrite
	// 0 = none
	// 1 = arrow buttons
	// 2 = face buttons
	// 3 = L/R
	// 4 = L/R + triangle/cross
	int iRightStickBind;

	// Control
	std::map<int,int> iMappingMap; // Can be used differently depending on systems
	int iForceInputDevice;
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
