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

enum BufferFilter {
	SCALE_LINEAR = 1,
	SCALE_NEAREST = 2,
};

// Software is not among these because it will have one of these perform the blit to display.
enum {
	GPU_BACKEND_OPENGL = 0,
	GPU_BACKEND_DIRECT3D9 = 1,
};

enum AudioBackendType {
	AUDIO_BACKEND_AUTO,
	AUDIO_BACKEND_DSOUND,
	AUDIO_BACKEND_WASAPI,
};

// For iIOTimingMethod.
enum IOTimingMethods {
	IOTIMING_FAST = 0,
	IOTIMING_HOST = 1,
	IOTIMING_REALISTIC = 2,
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
	bool bGameSpecific;

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

	// Used for switching the GPU backend in GameSettingsScreen.
	// Without this, PPSSPP instantly crashes if we edit iGPUBackend directly...
	int iTempGPUBackend;

	bool bRestartRequired;
#endif

	bool bPauseWhenMinimized;

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
	bool bSetRoundingMode;
	bool bForceFlushToZero;

	// Definitely cannot be changed while game is running.
	bool bSeparateCPUThread;
	int iIOTimingMethod;
	bool bSeparateIOThread;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	bool bCacheFullIsoInRam;

	int iScreenRotation;  // The rotation angle of the PPSSPP UI. Only supported on Android and possibly other mobile platforms.
	int iInternalScreenRotation;  // The internal screen rotation angle. Useful for vertical SHMUPs and similar.

	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;

	// GFX
	int iGPUBackend;
	bool bSoftwareRendering;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;  // may speed up some games

	int iRenderingMode; // 0 = non-buffered rendering 1 = buffered rendering 2 = Read Framebuffer to memory (CPU) 3 = Read Framebuffer to memory (GPU)
	int iTexFiltering; // 1 = off , 2 = nearest , 3 = linear , 4 = linear(CG)
	int iBufFilter; // 1 = linear, 2 = nearest
	bool bPartialStretch;
	bool bStretchToDisplay;
	bool bSmallDisplay;  // Useful on large tablets with touch controls to not overlap the image. Temporary setting - will be replaced by more comprehensive display size settings.
	bool bImmersiveMode;  // Mode on Android Kitkat 4.4 that hides the back button etc.
	bool bVSync;
	int iFrameSkip;
	bool bAutoFrameSkip;
	bool bFrameSkipUnthrottle;

	bool bEnableCardboard; // Cardboard Master Switch
	int iCardboardScreenSize; // Screen Size (in %)
	int iCardboardXShift; // X-Shift of Screen (in %)
	int iCardboardYShift; // Y-Shift of Screen (in %)

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
	int iCwCheatRefreshRate;
	bool bDisableStencilTest;
	bool bAlwaysDepthWrite;
	bool bDepthRangeHack;
	int iBloomHack; //0 = off, 1 = safe, 2 = balanced, 3 = aggressive
	bool bTimerHack;
	bool bAlphaMaskHack;
	bool bBlockTransferGPU;
	bool bDisableSlowFramebufEffects;
	bool bFragmentTestCache;
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	std::string sPostShaderName;  // Off for off.

	// Sound
	bool bEnableSound;
	int iAudioLatency; // 0 = low , 1 = medium(default) , 2 = high
	int iAudioBackend;

	// Audio Hack
	bool bSoundSpeedHack;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowFPSCounter;

	bool bShowDebugStats;
	bool bShowAudioDebug;
	bool bAudioResampler;

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
	bool bGamepadOnlyFocused;
	// Control Style
	int iTouchButtonStyle;
	// Control Positions
	int iTouchButtonOpacity;
	// Floating analog stick (recenters on thumb on press).
	bool bAutoCenterTouchAnalog;

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

	float fDInputAnalogDeadzone;
	int iDInputAnalogInverseMode;
	float fDInputAnalogInverseDeadzone;
	float fDInputAnalogSensitivity;

	float fXInputAnalogDeadzone;
	int iXInputAnalogInverseMode;
	float fXInputAnalogInverseDeadzone;
	float fXInputAnalogSensitivity;

	float fAnalogLimiterDeadzone;
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

	// Use the hardware scaler to scale up the image to save fillrate. Similar to Windows' window size, really.
	int iAndroidHwScale;  // 0 = device resolution. 1 = 480x272 (extended to correct aspect), 2 = 960x544 etc.

	// Risky JIT optimizations
	bool bDiscardRegsOnJRRA;

	// SystemParam
	std::string sNickName;
	std::string proAdhocServer;
	std::string sMACAddress;
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
	bool bEnableAdhocServer;
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
	std::string memStickDirectory;
	std::string flash0Directory;
	std::string internalDataDirectory;

	// Data for upgrade prompt
	std::string upgradeMessage;  // The actual message from the server is currently not used, need a translation mechanism. So this just acts as a flag.
	std::string upgradeVersion;
	std::string dismissedVersion;

	void Load(const char *iniFileName = nullptr, const char *controllerIniFilename = nullptr);
	void Save();
	void RestoreDefaults();
	
	//per game config managment, should maybe be in it's own class
	void changeGameSpecific(const std::string &gameId = "");
	bool createGameConfig(const std::string &game_id);
	bool deleteGameConfig(const std::string& pGameId);
	bool loadGameConfig(const std::string &game_id);
	bool saveGameConfig(const std::string &pGameId);
	void unloadGameConfig();
	std::string getGameConfigFile(const std::string &gameId);
	bool hasGameConfig(const std::string &game_id);

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
	std::string gameId_;
	std::string iniFilename_;
	std::string controllerIniFilename_;
	std::vector<std::string> searchPath_;
	std::string defaultPath_;
	std::string createdPath_;
};

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping();
const char *CreateRandMAC();

// TODO: Find a better place for this.
extern http::Downloader g_DownloadManager;
extern Config g_Config;

