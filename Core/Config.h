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

struct Config {
public:
	Config();
	~Config();

	// Whether to save the config on close.
	bool bSaveSettings;
	bool bFirstRun;

	bool bAutoRun;  // start immediately
	bool bBrowse; // when opening the emulator, immediately show a file browser


	// General
	int iNumWorkerThreads;
	bool bScreenshotsAsPNG;
	bool bEnableLogging;
#ifdef _WIN32
	bool bTopMost;
	std::string sFont;
#endif
	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	bool bJit;
	// Definitely cannot be changed while game is running.
	bool bSeparateCPUThread;
	bool bSeparateIOThread;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::string languageIni;

	// GFX
	bool bSoftwareRendering;
	bool bHardwareTransform; // only used in the GLES backend
	int iRenderingMode; // 0 = non-buffered rendering 1 = buffered rendering 2 = Read Framebuffer to memory (CPU) 3 = Read Framebuffer to memory (GPU)
	int iTexFiltering; // 1 = off , 2 = nearest , 3 = linear , 4 = linear(CG)
#ifdef BLACKBERRY
	bool bPartialStretch;
#endif
	bool bStretchToDisplay;
	bool bVSync;
	int iFrameSkip;

	int iWindowX;
	int iWindowY;
	int iWindowZoom;  // for Windows
	bool bAntiAliasing; 
	bool bVertexCache;
	bool bFullScreen;
#ifdef _WIN32
	bool bFullScreenOnLaunch;
#endif
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	bool bTrueColor;
	bool bMipMap;
	int iTexScalingLevel; // 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	int iFpsLimit;
	int iForceMaxEmulatedFPS;
	int iMaxRecent;
	int iCurrentStateSlot;
	bool bEnableCheats;
	bool bReloadCheats;
	bool bDisableStencilTest;
	bool bAlwaysDepthWrite;

	// Sound
	bool bEnableSound;
	bool bEnableAtrac3plus;
	bool bLowLatencyAudio;
	int iSFXVolume;
	int iBGMVolume;

	// UI
	bool bShowTouchControls;
	bool bShowDebuggerOnLoad;
	bool bShowAnalogStick;
	int iShowFPSCounter;
	bool bShowDebugStats;
	bool bAccelerometerToAnalogHoriz;

	// The three tabs.
	bool bGridView1;
	bool bGridView2;
	bool bGridView3;

	// Control
	int iTouchButtonOpacity;
	float fButtonScale;


	// GLES backend-specific hacks. Not saved to the ini file, do not add checkboxes. Will be made into
	// proper options when good enough.
	// PrescaleUV:
	//   * Applies UV scale/offset when decoding verts. Get rid of some work in the vertex shader,
	//     saves a uniform upload and is a prerequisite for future optimized hybrid 
	//     (SW skinning, HW transform) skinning.
	//   * Still has major problems so off by default - need to store tex scale/offset per DeferredDrawCall, 
	//     which currently isn't done so if texscale/offset isn't static (like in Tekken 6) things go wrong.
	bool bPrescaleUV;

	// End GLES hacks.

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
	// TODO: Make this work with your platform, too!
#ifdef _WIN32
	bool bBypassOSKWithKeyboard;
#endif

	// Debugger
	int iDisasmWindowX;
	int iDisasmWindowY;
	int iDisasmWindowW;
	int iDisasmWindowH;
	int iConsoleWindowX;
	int iConsoleWindowY;
	int iFontWidth;
	int iFontHeight;
	bool bDisplayStatusBar;
	bool bShowDeveloperMenu;

	std::string currentDirectory;
	std::string externalDirectory; 
	std::string memCardDirectory;
	std::string flashDirectory;
	std::string internalDataDirectory;

	void Load(const char *iniFileName = "ppsspp.ini", const char *controllerIniFilename = "controls.ini");
	void Save();
	void RestoreDefaults();

	// Utility functions for "recent" management
	void AddRecent(const std::string &file);
	void CleanRecent();

private:
	std::string iniFilename_;
	std::string controllerIniFilename_;
};

extern Config g_Config;
