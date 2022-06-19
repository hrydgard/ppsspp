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

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Core/ConfigValues.h"

extern const char *PPSSPP_GIT_VERSION;

extern bool jitForcedOff;

enum ChatPositions {
	BOTTOM_LEFT = 0,
	BOTTOM_CENTER = 1,
	BOTOM_RIGHT = 2,
	TOP_LEFT = 3,
	TOP_CENTER = 4,
	TOP_RIGHT = 5,
	CENTER_LEFT = 6,
	CENTER_RIGHT = 7,
};

namespace http {
	class Download;
	class Downloader;
}

struct UrlEncoder;

struct ConfigTouchPos {
	float x;
	float y;
	float scale;
	// Note: Show is not used for all settings.
	bool show;
};

struct ConfigCustomButton {
	uint64_t key;
	int image;
	int shape;
	bool toggle;
	bool repeat;
};

struct Config {
public:
	Config();
	~Config();

	// Whether to save the config on close.
	bool bSaveSettings;
	bool bFirstRun;
	bool bGameSpecific = false;
	bool bUpdatedInstanceCounter = false;

	int iRunCount; // To be used to for example check for updates every 10 runs and things like that.

	bool bAutoRun;  // start immediately
	bool bBrowse; // when opening the emulator, immediately show a file browser

	// General
	bool bScreenshotsAsPNG;
	bool bUseFFV1;
	bool bDumpFrames;
	bool bDumpVideoOutput;
	bool bDumpAudio;
	bool bSaveLoadResetsAVdumping;
	bool bEnableLogging;
	bool bDumpDecryptedEboot;
	bool bFullscreenOnDoubleclick;

	// These four are Win UI only
	bool bPauseOnLostFocus;
	bool bTopMost;
	bool bIgnoreWindowsKey;
	bool bRestartRequired;

	std::string sFont;

	bool bPauseWhenMinimized;

	// Not used on mobile devices.
	bool bPauseExitsEmulator;

	bool bPauseMenuExitsEmulator;

	// Core
	bool bIgnoreBadMemAccess;

	bool bFastMemory;
	int iCpuCore;
	bool bCheckForNewVersion;
	bool bForceLagSync;
	bool bFuncReplacements;
	bool bHideSlowWarnings;
	bool bHideStateWarnings;
	bool bPreloadFunctions;
	uint32_t uJitDisableFlags;

	bool bSeparateSASThread;
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
	bool bRemoteDebuggerOnStartup;
	bool bMemStickInserted;
	int iMemStickSizeGB;
	bool bLoadPlugins;

	int iScreenRotation;  // The rotation angle of the PPSSPP UI. Only supported on Android and possibly other mobile platforms.
	int iInternalScreenRotation;  // The internal screen rotation angle. Useful for vertical SHMUPs and similar.

	std::string sReportHost;
	std::vector<std::string> recentIsos;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;

	bool bDiscordPresence;  // Enables setting the Discord presence to the current game (or menu)

	// GFX
	int iGPUBackend;
	std::string sFailedGPUBackends;
	std::string sDisabledGPUBackends;
	// We have separate device parameters for each backend so it doesn't get erased if you switch backends.
	// If not set, will use the "best" device.
	std::string sVulkanDevice;
	std::string sD3D11Device;  // Windows only
	std::string sCameraDevice;
	std::string sMicDevice;

	bool bSoftwareRendering;
	bool bSoftwareRenderingJit;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;  // may speed up some games
	bool bVendorBugChecksEnabled;

	int iRenderingMode; // 0 = non-buffered rendering 1 = buffered rendering
	int iTexFiltering; // 1 = auto , 2 = nearest , 3 = linear , 4 = auto max quality
	int iBufFilter; // 1 = linear, 2 = nearest
	int iSmallDisplayZoomType;  // Used to fit display into screen 0 = stretch, 1 = partial stretch, 2 = auto scaling, 3 = manual scaling.
	float fSmallDisplayOffsetX; // Along with Y it goes from 0.0 to 1.0, XY (0.5, 0.5) = center of the screen
	float fSmallDisplayOffsetY;
	float fSmallDisplayZoomLevel; //This is used for zoom values, both in and out.
	bool bImmersiveMode;  // Mode on Android Kitkat 4.4 that hides the back button etc.
	bool bSustainedPerformanceMode;  // Android: Slows clocks down to avoid overheating/speed fluctuations.
	bool bIgnoreScreenInsets;  // Android: Center screen disregarding insets if this is enabled.
	bool bVSync;
	int iFrameSkip;
	int iFrameSkipType;
	int iFastForwardMode; // See FastForwardMode in ConfigValues.h.
	bool bAutoFrameSkip;

	bool bEnableCardboardVR; // Cardboard Master Switch
	int iCardboardScreenSize; // Screen Size (in %)
	int iCardboardXShift; // X-Shift of Screen (in %)
	int iCardboardYShift; // Y-Shift of Screen (in %)

	int iWindowX;
	int iWindowY;
	int iWindowWidth;  // Windows and other windowed environments
	int iWindowHeight;

	float fUITint;
	float fUISaturation;

	bool bVertexCache;
	bool bTextureBackoffCache;
	bool bTextureSecondaryCache;
	bool bVertexDecoderJit;
	bool bFullScreen;
	bool bFullScreenMulti;
	int iForceFullScreen = -1; // -1 = nope, 0 = force off, 1 = force on (not saved.)
	int iInternalResolution;  // 0 = Auto (native), 1 = 1x (480x272), 2 = 2x, 3 = 3x, 4 = 4x and so on.
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	int bHighQualityDepth;
	bool bReplaceTextures;
	bool bSaveNewTextures;
	bool bIgnoreTextureFilenames;
	bool bReplaceTexturesAllowLate;
	int iTexScalingLevel; // 0 = auto, 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	bool bTexHardwareScaling;
	int iFpsLimit1;
	int iFpsLimit2;
	int iMaxRecent;
	int iCurrentStateSlot;
	int iRewindFlipFrequency;
	bool bUISound;
	bool bEnableStateUndo;
	std::string sStateLoadUndoGame;
	std::string sStateUndoLastSaveGame;
	int iStateUndoLastSaveSlot;
	int iAutoLoadSaveState; // 0 = off, 1 = oldest, 2 = newest, >2 = slot number + 3
	bool bEnableCheats;
	bool bReloadCheats;
	int iCwCheatRefreshRate;
	float fCwCheatScrollPosition;
	float fGameListScrollPosition;
	int iBloomHack; //0 = off, 1 = safe, 2 = balanced, 3 = aggressive
	bool bBlockTransferGPU;
	bool bDisableSlowFramebufEffects;
	bool bFragmentTestCache;
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	bool bHardwareTessellation;
	bool bShaderCache;  // Hidden ini-only setting, useful for debugging shader compile times.

	std::vector<std::string> vPostShaderNames; // Off for chain end (only Off for no shader)
	std::map<std::string, float> mPostShaderSetting;
	bool bShaderChainRequires60FPS;
	std::string sTextureShaderName;
	bool bGfxDebugOutput;
	bool bGfxDebugSplitSubmit;
	int iInflightFrames;
	bool bRenderDuplicateFrames;

	// Sound
	bool bEnableSound;
	int iAudioBackend;
	int iGlobalVolume;
	int iReverbVolume;
	int iAltSpeedVolume;
	bool bExtraAudioBuffering;  // For bluetooth
	std::string sAudioDevice;
	bool bAutoAudioDevice;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowFPSCounter;
	bool bShowRegionOnGameIcon;
	bool bShowIDOnGameIcon;
	float fGameGridScale;
	bool bShowOnScreenMessages;
	int iBackgroundAnimation;  // enum BackgroundAnimation

	std::string sThemeName;

	bool bLogFrameDrops;
	bool bShowDebugStats;
	bool bShowAudioDebug;
	bool bShowGpuProfile;

	//Analog stick tilting
	//the base x and y tilt. this inclination is treated as (0,0) and the tilt input
	//considers this orientation to be equal to no movement of the analog stick.
	float fTiltBaseX, fTiltBaseY;
	int iTiltOrientation;
	//whether the x axes and y axes should invert directions (left becomes right, top becomes bottom.)
	bool bInvertTiltX, bInvertTiltY;
	//the sensitivity of the tilt in the x direction
	int iTiltSensitivityX;
	//the sensitivity of the tilt in the Y direction
	int iTiltSensitivityY;
	//the deadzone radius of the tilt
	float fDeadzoneRadius;
	// deadzone skip
	float fTiltDeadzoneSkip;
	//type of tilt input currently selected: Defined in TiltEventProcessor.h
	//0 - no tilt, 1 - analog stick, 2 - D-Pad, 3 - Action Buttons (Tri, Cross, Square, Circle)
	int iTiltInputType;

	// The three tabs.
	bool bGridView1;
	bool bGridView2;
	bool bGridView3;

	// Right analog binding
	int iRightAnalogUp;
	int iRightAnalogDown;
	int iRightAnalogLeft;
	int iRightAnalogRight;
	int iRightAnalogPress;
	bool bRightAnalogCustom;
	bool bRightAnalogDisableDiagonal;

	// Motion gesture controller
	bool bGestureControlEnabled;
	int iSwipeUp;
	int iSwipeDown;
	int iSwipeLeft;
	int iSwipeRight;
	float fSwipeSensitivity;
	float fSwipeSmoothing;
	int iDoubleTapGesture;

	// Disable diagonals
	bool bDisableDpadDiagonals;
	bool bGamepadOnlyFocused;
	// Control Style
	int iTouchButtonStyle;
	int iTouchButtonOpacity;
	int iTouchButtonHideSeconds;
	// Auto rotation speed
	float fAnalogAutoRotSpeed;

	// Snap touch control position
	bool bTouchSnapToGrid;
	int iTouchSnapGridSize;

	// Floating analog stick (recenters on thumb on press).
	bool bAutoCenterTouchAnalog;

	//space between PSP buttons
	//the PSP button's center (triangle, circle, square, cross)
	ConfigTouchPos touchActionButtonCenter;
	float fActionButtonSpacing;
	//radius of the D-pad (PSP cross)
	// int iDpadRadius;
	//the D-pad (PSP cross) position
	ConfigTouchPos touchDpad;
	float fDpadSpacing;
	ConfigTouchPos touchStartKey;
	ConfigTouchPos touchSelectKey;
	ConfigTouchPos touchFastForwardKey;
	ConfigTouchPos touchLKey;
	ConfigTouchPos touchRKey;
	ConfigTouchPos touchAnalogStick;
	ConfigTouchPos touchRightAnalogStick;

	ConfigTouchPos touchCombo0;
	ConfigTouchPos touchCombo1;
	ConfigTouchPos touchCombo2;
	ConfigTouchPos touchCombo3;
	ConfigTouchPos touchCombo4;
	ConfigTouchPos touchCombo5;
	ConfigTouchPos touchCombo6;
	ConfigTouchPos touchCombo7;
	ConfigTouchPos touchCombo8;
	ConfigTouchPos touchCombo9;

	float fLeftStickHeadScale;
	float fRightStickHeadScale;
	bool bHideStickBackground;

	// Controls Visibility
	bool bShowTouchControls;

	bool bShowTouchCircle;
	bool bShowTouchCross;
	bool bShowTouchTriangle;
	bool bShowTouchSquare;

	ConfigCustomButton CustomKey0;
	ConfigCustomButton CustomKey1;
	ConfigCustomButton CustomKey2;
	ConfigCustomButton CustomKey3;
	ConfigCustomButton CustomKey4;
	ConfigCustomButton CustomKey5;
	ConfigCustomButton CustomKey6;
	ConfigCustomButton CustomKey7;
	ConfigCustomButton CustomKey8;
	ConfigCustomButton CustomKey9;	

	// Ignored on iOS and other platforms that lack pause.
	bool bShowTouchPause;

	bool bHapticFeedback;

	// We also use the XInput settings as analog settings on other platforms like Android.
	float fAnalogDeadzone;
	float fAnalogInverseDeadzone;
	float fAnalogSensitivity;
	// convert analog stick circle to square
	bool bAnalogIsCircular;


	float fAnalogLimiterDeadzone;

	bool bMouseControl;
	bool bMapMouse; // Workaround for mapping screen:|
	bool bMouseConfine; // Trap inside the window.
	float fMouseSensitivity;
	float fMouseSmoothing;

	bool bSystemControls;

	// Use the hardware scaler to scale up the image to save fillrate. Similar to Windows' window size, really.
	int iAndroidHwScale;  // 0 = device resolution. 1 = 480x272 (extended to correct aspect), 2 = 960x544 etc.

	// Risky JIT optimizations
	bool bDiscardRegsOnJRRA;

	// SystemParam
	std::string sNickName;
	std::string sMACAddress;
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
	std::string proAdhocServer;
	bool bEnableWlan;
	bool bEnableAdhocServer;
	bool bEnableUPnP;
	bool bUPnPUseOriginalPort;
	bool bForcedFirstConnect;
	int iPortOffset;
	int iMinTimeout;
	int iWlanAdhocChannel;
	bool bWlanPowerSave;
	bool bEnableNetworkChat;
	//for chat position , moveable buttons is better than this 
	int iChatButtonPosition;
	int iChatScreenPosition;

	bool bEnableQuickChat;
	std::string sQuickChat0;
	std::string sQuickChat1;
	std::string sQuickChat2;
	std::string sQuickChat3;
	std::string sQuickChat4;

	int iPSPModel;
	int iFirmwareVersion;
	bool bBypassOSKWithKeyboard;

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
	bool bDebugMemInfoDetailed;
	bool bDrawFrameGraph;

	// Volatile development settings
	bool bShowFrameProfiler;
	bool bGpuLogProfiler; // Controls the Vulkan logging profiler (profiles textures uploads etc).

	// Various directories. Autoconfigured, not read from ini.
	Path currentDirectory;  // The directory selected in the game browsing window.
	Path defaultCurrentDirectory;  // Platform dependent, initialized at startup.

	Path memStickDirectory;
	Path flash0Directory;
	Path internalDataDirectory;
	Path appCacheDirectory;

	// Data for upgrade prompt
	std::string upgradeMessage;  // The actual message from the server is currently not used, need a translation mechanism. So this just acts as a flag.
	std::string upgradeVersion;
	std::string dismissedVersion;

	void Load(const char *iniFileName = nullptr, const char *controllerIniFilename = nullptr);
	bool Save(const char *saveReason);
	void Reload();
	void RestoreDefaults();

	//per game config managment, should maybe be in it's own class
	void changeGameSpecific(const std::string &gameId = "", const std::string &title = "");
	bool createGameConfig(const std::string &game_id);
	bool deleteGameConfig(const std::string& pGameId);
	bool loadGameConfig(const std::string &game_id, const std::string &title);
	bool saveGameConfig(const std::string &pGameId, const std::string &title);
	void unloadGameConfig();
	Path getGameConfigFile(const std::string &gameId);
	bool hasGameConfig(const std::string &game_id);

	void SetSearchPath(const Path &path);
	const Path FindConfigFile(const std::string &baseFilename);

	void UpdateIniLocation(const char *iniFileName = nullptr, const char *controllerIniFilename = nullptr);

	// Utility functions for "recent" management
	void AddRecent(const std::string &file);
	void RemoveRecent(const std::string &file);
	void CleanRecent();

	static void DownloadCompletedCallback(http::Download &download);
	void DismissUpgrade();

	void ResetControlLayout();

	void GetReportingInfo(UrlEncoder &data);

	bool IsPortrait() const;
	int NextValidBackend();
	bool IsBackendEnabled(GPUBackend backend, bool validate = true);

	bool UseFullScreen() const {
		if (iForceFullScreen != -1)
			return iForceFullScreen == 1;
		return bFullScreen;
	}

protected:
	void LoadStandardControllerIni();

private:
	bool reload_ = false;
	std::string gameId_;
	std::string gameIdTitle_;
	Path iniFilename_;
	Path controllerIniFilename_;
	Path searchPath_;
};

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping();
std::string CreateRandMAC();

// TODO: Find a better place for this.
extern http::Downloader g_DownloadManager;
extern Config g_Config;

