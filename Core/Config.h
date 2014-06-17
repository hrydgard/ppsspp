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
#include <map>
#include <vector>

#include "CommonTypes.h"

#if !defined(USING_QT_UI)
extern const char *PPSSPP_GIT_VERSION;
#endif

const int MAX_CONFIG_VOLUME = 8;
const int PSP_MODEL_FAT = 0;
const int PSP_MODEL_SLIM = 1;
const int PSP_DEFAULT_FIRMWARE = 150;

enum {
	ROTATION_AUTO = 0,
	ROTATION_LOCKED_HORIZONTAL = 1,
	ROTATION_LOCKED_VERTICAL = 2,
	ROTATION_LOCKED_HORIZONTAL180 = 3,
	ROTATION_LOCKED_VERTICAL180 = 4,
};

namespace http {
	class Download;
	class Downloader;
}

struct UrlEncoder;

struct Config {
public:
	Config();
	~Config();

	// Whether to save the config on close.
	bool bSaveSettings;
	bool bFirstRun;

	int iRunCount; // To be used to for example check for updates every 10 runs and things like that.

	bool bAutoRun;  // start immediately
	bool bBrowse; // when opening the emulator, immediately show a file browser
	bool bHomebrewStore;

	// General
	int iNumWorkerThreads;
	bool bScreenshotsAsPNG;
	bool bEnableLogging;
	bool bDumpDecryptedEboot;
#if defined(USING_WIN_UI)
	bool bPauseOnLostFocus;
	bool bTopMost;
	std::string sFont;
	bool bIgnoreWindowsKey;
#endif

#if !defined(MOBILE_DEVICE)
	bool bPauseExitsEmulator;
#endif

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	bool bJit;
	bool bCheckForNewVersion;
	bool bForceLagSync;
	bool bFuncReplacements;

	// Definitely cannot be changed while game is running.
	bool bSeparateCPUThread;
	bool bSeparateIOThread;
	bool bAtomicAudioLocks;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	int iScreenRotation;

	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;


	// GFX
	bool bSoftwareRendering;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;  // may speed up some games

	int iRenderingMode; // 0 = non-buffered rendering 1 = buffered rendering 2 = Read Framebuffer to memory (CPU) 3 = Read Framebuffer to memory (GPU)
	int iTexFiltering; // 1 = off , 2 = nearest , 3 = linear , 4 = linear(CG)
	bool bPartialStretch;
	bool bStretchToDisplay;
	bool bSmallDisplay;  // Useful on large tablets with touch controls to not overlap the image. Temporary setting - will be replaced by more comprehensive display size settings.
	bool bImmersiveMode;  // Mode on Android Kitkat 4.4 that hides the back button etc.
	bool bVSync;
	int iFrameSkip;
	bool bAutoFrameSkip;
	bool bFrameSkipUnthrottle;

	int iWindowX;
	int iWindowY;
	int iWindowWidth;  // Windows and other windowed environments
	int iWindowHeight;

	bool bVertexCache;
	bool bTextureBackoffCache;
	bool bTextureSecondaryCache;
	bool bVertexDecoderJit;
	bool bFullScreen;
	int iInternalResolution;  // 0 = Auto (native), 1 = 1x (480x272), 2 = 2x, 3 = 3x, 4 = 4x and so on.
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
	int iRewindFlipFrequency;
	bool bEnableAutoLoad;
	bool bEnableCheats;
	bool bReloadCheats;
	bool bDisableStencilTest;
	bool bAlwaysDepthWrite;
	bool bTimerHack;
	bool bAlphaMaskHack;
	bool bBlockTransferGPU;
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	std::string sPostShaderName;  // Off for off.

	// Sound
	bool bEnableSound;
	int IaudioLatency; // 0 = low , 1 = medium(default) , 2 = high
	int iSFXVolume;
	int iBGMVolume;

	// Audio Hack
	bool bSoundSpeedHack;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowFPSCounter;
	bool bShowDebugStats;

	//Analog stick tilting
	//the base x and y tilt. this inclination is treated as (0,0) and the tilt input
	//considers this orientation to be equal to no movement of the analog stick.
	float fTiltBaseX, fTiltBaseY;
	//whether the x axes and y axes should invert directions (left becomes right, top becomes bottom.)
	bool bInvertTiltX, bInvertTiltY; 
	//the sensitivity of the tilt in the x direction
	int iTiltSensitivityX;
	//the sensitivity of the tilt in the Y direction
	int iTiltSensitivityY;
	//the deadzone radius of the tilt
	float fDeadzoneRadius;
	//type of tilt input currently selected: Defined in TiltEventProcessor.h
	//0 - no tilt, 1 - analog stick, 2 - D-Pad, 3 - Action Buttons (Tri, Cross, Square, Circle)
	int iTiltInputType;

	// The three tabs.
	bool bGridView1;
	bool bGridView2;
	bool bGridView3;

	// Disable diagonals
	bool bDisableDpadDiagonals;
	// Control Style
	int iTouchButtonStyle;
	// Control Positions
	int iTouchButtonOpacity;
	//space between PSP buttons
	//the PSP button's center (triangle, circle, square, cross)
	float fActionButtonCenterX, fActionButtonCenterY;
	float fActionButtonScale;
	float fActionButtonSpacing;
	//radius of the D-pad (PSP cross)
	// int iDpadRadius;
	//the D-pad (PSP cross) position
	float fDpadX, fDpadY;
	float fDpadScale;
	float fDpadSpacing;
	//the start key position
	float fStartKeyX, fStartKeyY;
	float fStartKeyScale;
	//the select key position;
	float fSelectKeyX, fSelectKeyY;
	float fSelectKeyScale;

	float fUnthrottleKeyX, fUnthrottleKeyY;
	float fUnthrottleKeyScale;

	float fLKeyX, fLKeyY;
	float fLKeyScale;

	float fRKeyX, fRKeyY;
	float fRKeyScale;

	//position of the analog stick
	float fAnalogStickX, fAnalogStickY;
	float fAnalogStickScale;

	// Controls Visibility
	bool bShowTouchControls;

	bool bShowTouchCircle;
	bool bShowTouchCross;
	bool bShowTouchTriangle;
	bool bShowTouchSquare;

	bool bShowTouchStart;
	bool bShowTouchSelect;
	bool bShowTouchUnthrottle;

	bool bShowTouchLTrigger;
	bool bShowTouchRTrigger;

	bool bShowTouchAnalogStick;
	bool bShowTouchDpad;

#if !defined(__SYMBIAN32__) && !defined(IOS) && !defined(MAEMO)
	bool bShowTouchPause;
#endif

	bool bHapticFeedback;

	// GLES backend-specific hacks. Not saved to the ini file, do not add checkboxes. Will be made into
	// proper options when good enough.
	// PrescaleUV:
	//   * Applies UV scale/offset when decoding verts. Get rid of some work in the vertex shader,
	//     saves a uniform upload and is a prerequisite for future optimized hybrid 
	//     (SW skinning, HW transform) skinning.
	//   * Still has major problems so off by default - need to store tex scale/offset per DeferredDrawCall, 
	//     which currently isn't done so if texscale/offset isn't static (like in Tekken 6) things go wrong.
	bool bPrescaleUV;
	bool bDisableAlphaTest;  // Helps PowerVR immensely, breaks some graphics
	// End GLES hacks.

	// Risky JIT optimizations
	bool bDiscardRegsOnJRRA;

	// SystemParam
	std::string sNickName;
	std::string proAdhocServer;
	std::string localMacAddress;
	int iLanguage;
	int iTimeFormat;
	int iDateFormat;
	int iTimeZone;
	bool bDayLightSavings;
	int iButtonPreference;
	int iLockParentalLevel;
	bool bEncryptSave;

	// Networking
	bool bEnableWlan;
	int iWlanAdhocChannel;
	bool bWlanPowerSave;

	int iPSPModel;
	int iFirmwareVersion;
	// TODO: Make this work with your platform, too!
#if defined(USING_WIN_UI)
	bool bBypassOSKWithKeyboard;
#endif

	// Debugger
	int iDisasmWindowX;
	int iDisasmWindowY;
	int iDisasmWindowW;
	int iDisasmWindowH;
	int iGEWindowX;
	int iGEWindowY;
	int iGEWindowW;
	int iGEWindowH;
	int iConsoleWindowX;
	int iConsoleWindowY;
	int iFontWidth;
	int iFontHeight;
	bool bDisplayStatusBar;
	bool bShowBottomTabTitles;
	bool bShowDeveloperMenu;
	// Double edged sword: much easier debugging, but not accurate.
	bool bSkipDeadbeefFilling;
	bool bFuncHashMap;

	std::string currentDirectory;
	std::string externalDirectory; 
	std::string memCardDirectory;
	std::string flash0Directory;
	std::string internalDataDirectory;

	// Data for upgrade prompt
	std::string upgradeMessage;  // The actual message from the server is currently not used, need a translation mechanism. So this just acts as a flag.
	std::string upgradeVersion;
	std::string dismissedVersion;

	void Load(const char *iniFileName = "ppsspp.ini", const char *controllerIniFilename = "controls.ini");
	void Save();
	void RestoreDefaults();

	// Used when the file is not found in the search path.  Trailing slash.
	void SetDefaultPath(const std::string &defaultPath);
	// Use a trailing slash.
	void AddSearchPath(const std::string &path);
	const std::string FindConfigFile(const std::string &baseFilename);

	// Utility functions for "recent" management
	void AddRecent(const std::string &file);
	void CleanRecent();

	static void DownloadCompletedCallback(http::Download &download);
	void DismissUpgrade();

	void ResetControlLayout();

	void GetReportingInfo(UrlEncoder &data);

private:
	std::string iniFilename_;
	std::string controllerIniFilename_;
	std::vector<std::string> searchPath_;
	std::string defaultPath_;
};

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping();

// TODO: Find a better place for this.
extern http::Downloader g_DownloadManager;
extern Config g_Config;

