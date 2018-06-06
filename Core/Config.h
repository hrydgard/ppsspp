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

#include "Common/CommonTypes.h"

extern const char *PPSSPP_GIT_VERSION;

const int PSP_MODEL_FAT = 0;
const int PSP_MODEL_SLIM = 1;
const int PSP_DEFAULT_FIRMWARE = 660;
static const s8 VOLUME_OFF = 0;
static const s8 VOLUME_MAX = 10;

enum class CPUCore {
	INTERPRETER = 0,
	JIT = 1,
	IR_JIT = 2,
};

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
enum class GPUBackend {
	OPENGL = 0,
	DIRECT3D9 = 1,
	DIRECT3D11 = 2,
	VULKAN = 3,
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

	// General
	int iNumWorkerThreads;
	bool bScreenshotsAsPNG;
	bool bUseFFV1;
	bool bDumpFrames;
	bool bDumpAudio;
	bool bSaveLoadResetsAVdumping;
	bool bEnableLogging;
	bool bDumpDecryptedEboot;
	bool bFullscreenOnDoubleclick;
#if defined(USING_WIN_UI)
	bool bPauseOnLostFocus;
	bool bTopMost;
	std::string sFont;
	bool bIgnoreWindowsKey;
	bool bRestartRequired;
#endif

	bool bPauseWhenMinimized;

#if !defined(MOBILE_DEVICE)
	bool bPauseExitsEmulator;
#endif
	bool bPauseMenuExitsEmulator;
	bool bPS3Controller;

	// Core
	bool bIgnoreBadMemAccess;
	bool bFastMemory;
	int iCpuCore;
	bool bCheckForNewVersion;
	bool bForceLagSync;
	bool bFuncReplacements;
	bool bHideSlowWarnings;
	bool bPreloadFunctions;

	bool bSeparateSASThread;
	bool bSeparateIOThread;
	int iIOTimingMethod;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	bool bCacheFullIsoInRam;
	int iRemoteISOPort;
	std::string sLastRemoteISOServer;
	int iLastRemoteISOPort;
	bool bRemoteISOManual;
	bool bRemoteShareOnStartup;
	std::string sRemoteISOSubdir;
	bool bMemStickInserted;

	int iScreenRotation;  // The rotation angle of the PPSSPP UI. Only supported on Android and possibly other mobile platforms.
	int iInternalScreenRotation;  // The internal screen rotation angle. Useful for vertical SHMUPs and similar.

	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;

	// GFX
	int iGPUBackend;
	// We have separate device parameters for each backend so it doesn't get erased if you switch backends.
	// If not set, will use the "best" device.
	std::string sVulkanDevice;
#ifdef _WIN32
	std::string sD3D11Device;
#endif
	bool bSoftwareRendering;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;  // may speed up some games

	int iRenderingMode; // 0 = non-buffered rendering 1 = buffered rendering
	int iTexFiltering; // 1 = off , 2 = nearest , 3 = linear , 4 = linear(CG)
	int iBufFilter; // 1 = linear, 2 = nearest
	int iSmallDisplayZoomType;  // Used to fit display into screen 0 = stretch, 1 = partial stretch, 2 = auto scaling, 3 = manual scaling.
	float fSmallDisplayOffsetX; // Along with Y it goes from 0.0 to 1.0, XY (0.5, 0.5) = center of the screen
	float fSmallDisplayOffsetY;
	float fSmallDisplayZoomLevel; //This is used for zoom values, both in and out.
	bool bImmersiveMode;  // Mode on Android Kitkat 4.4 that hides the back button etc.
	bool bSustainedPerformanceMode;  // Android: Slows clocks down to avoid overheating/speed fluctuations.
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
	bool bFullScreenMulti;
	int iInternalResolution;  // 0 = Auto (native), 1 = 1x (480x272), 2 = 2x, 3 = 3x, 4 = 4x and so on.
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	int bHighQualityDepth;
	bool bTrueColor;
	bool bReplaceTextures;
	bool bSaveNewTextures;
	int iTexScalingLevel; // 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	int iFpsLimit;
	int iForceMaxEmulatedFPS;
	int iMaxRecent;
	int iCurrentStateSlot;
	int iRewindFlipFrequency;
	bool bEnableStateUndo;
	bool bEnableAutoLoad;
	bool bEnableCheats;
	bool bReloadCheats;
	int iCwCheatRefreshRate;
	bool bDisableStencilTest;
	int iBloomHack; //0 = off, 1 = safe, 2 = balanced, 3 = aggressive
	bool bTimerHack;
	bool bBlockTransferGPU;
	bool bDisableSlowFramebufEffects;
	bool bFragmentTestCache;
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	bool bHardwareTessellation;
	std::string sPostShaderName;  // Off for off.
	bool bGfxDebugOutput;
	bool bGfxDebugSplitSubmit;

	// Sound
	bool bEnableSound;
	int iAudioLatency; // 0 = low , 1 = medium(default) , 2 = high
	int iAudioBackend;
	int iGlobalVolume;
	bool bExtraAudioBuffering;  // For bluetooth

	// Audio Hack
	bool bSoundSpeedHack;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowFPSCounter;

	// TODO: Maybe move to a separate theme system.
	uint32_t uItemStyleFg;
	uint32_t uItemStyleBg;
	uint32_t uItemFocusedStyleFg;
	uint32_t uItemFocusedStyleBg;
	uint32_t uItemDownStyleFg;
	uint32_t uItemDownStyleBg;
	uint32_t uItemDisabledStyleFg;
	uint32_t uItemDisabledStyleBg;
	uint32_t uItemHighlightedStyleFg;
	uint32_t uItemHighlightedStyleBg;

	uint32_t uButtonStyleFg;
	uint32_t uButtonStyleBg;
	uint32_t uButtonFocusedStyleFg;
	uint32_t uButtonFocusedStyleBg;
	uint32_t uButtonDownStyleFg;
	uint32_t uButtonDownStyleBg;
	uint32_t uButtonDisabledStyleFg;
	uint32_t uButtonDisabledStyleBg;
	uint32_t uButtonHighlightedStyleFg;
	uint32_t uButtonHighlightedStyleBg;

	uint32_t uHeaderStyleFg;
	uint32_t uInfoStyleFg;
	uint32_t uInfoStyleBg;
	uint32_t uPopupTitleStyleFg;
	uint32_t uPopupStyleFg;
	uint32_t uPopupStyleBg;

	bool bLogFrameDrops;
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
	//Combo key screen flag
	int iComboMode;

	// Disable diagonals
	bool bDisableDpadDiagonals;
	bool bGamepadOnlyFocused;
	// Control Style
	int iTouchButtonStyle;
	int iTouchButtonOpacity;
	int iTouchButtonHideSeconds;
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

	//the Combo Button position
	float fcombo0X, fcombo0Y;
	float fcomboScale0;
	float fcombo1X, fcombo1Y;
	float fcomboScale1;
	float fcombo2X, fcombo2Y;
	float fcomboScale2;
	float fcombo3X, fcombo3Y;
	float fcomboScale3;
	float fcombo4X, fcombo4Y;
	float fcomboScale4;

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

	//Combo Button Visibility
	bool bShowComboKey0;
	bool bShowComboKey1;
	bool bShowComboKey2;
	bool bShowComboKey3;
	bool bShowComboKey4;

	// Combo_key mapping. These are bitfields.
	int iCombokey0;
	int iCombokey1;
	int iCombokey2;
	int iCombokey3;
	int iCombokey4;

	// Ignored on iOS and other platforms that lack pause.
	bool bShowTouchPause;

	bool bHapticFeedback;

	float fDInputAnalogDeadzone;
	int iDInputAnalogInverseMode;
	float fDInputAnalogInverseDeadzone;
	float fDInputAnalogSensitivity;

	// We also use the XInput settings as analog settings on other platforms like Android.
	float fXInputAnalogDeadzone;
	int iXInputAnalogInverseMode;
	float fXInputAnalogInverseDeadzone;
	float fXInputAnalogSensitivity;

	float fAnalogLimiterDeadzone;

	bool bMouseControl;
	bool bMapMouse; // Workaround for mapping screen:|
	bool bMouseConfine; // Trap inside the window.
	float fMouseSensitivity;
	float fMouseSmoothing;

	// Use the hardware scaler to scale up the image to save fillrate. Similar to Windows' window size, really.
	int iAndroidHwScale;  // 0 = device resolution. 1 = 480x272 (extended to correct aspect), 2 = 960x544 etc.

	// Risky JIT optimizations
	bool bDiscardRegsOnJRRA;

	// SystemParam
	std::string sNickName;
	std::string proAdhocServer;
	std::string sMACAddress;
	int iPortOffset;
	int iLanguage;
	int iTimeFormat;
	int iDateFormat;
	int iTimeZone;
	bool bDayLightSavings;
	int iButtonPreference;
	int iLockParentalLevel;
	bool bEncryptSave;
	bool bSavedataUpgrade;

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
	bool bShowAllocatorDebug;
	// Double edged sword: much easier debugging, but not accurate.
	bool bSkipDeadbeefFilling;
	bool bFuncHashMap;

	// Volatile development settings
	bool bShowFrameProfiler;

	std::string currentDirectory;
	std::string externalDirectory;
	std::string memStickDirectory;
	std::string flash0Directory;
	std::string internalDataDirectory;
	std::string appCacheDirectory;

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

	bool IsPortrait() const {
		return (iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180) && iRenderingMode != 0;
	}

protected:
	void LoadStandardControllerIni();

private:
	std::string gameId_;
	std::string iniFilename_;
	std::string controllerIniFilename_;
	std::vector<std::string> searchPath_;
	std::string defaultPath_;
	std::string createdPath_;
};

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping();
std::string CreateRandMAC();

// TODO: Find a better place for this.
extern http::Downloader g_DownloadManager;
extern Config g_Config;

