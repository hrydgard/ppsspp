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

namespace http {
	class Request;
	class RequestManager;
}

struct UrlEncoder;
struct ConfigPrivate;

class Section;

class PlayTimeTracker {
public:
	struct PlayTime {
		int totalTimePlayed;
		double startTime;  // time_now_d() time
		uint64_t lastTimePlayed;  // UTC Unix time for portability.
	};

	// It's OK to call these redundantly.
	void Start(const std::string &gameId);
	void Stop(const std::string &gameId);

	void Load(const Section *section);
	void Save(Section *section);

	bool GetPlayedTimeString(const std::string &gameId, std::string *str) const;

private:
	std::map<std::string, PlayTime> tracker_;
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

	bool bPauseExitsEmulator;
	bool bPauseMenuExitsEmulator;

	bool bRunBehindPauseMenu;

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

	bool bDisableHTTPS;

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
	std::string sRemoteISOSharedDir;
	int iRemoteISOShareType;
	bool bRemoteDebuggerOnStartup;
	bool bRemoteTab;
	bool bMemStickInserted;
	int iMemStickSizeGB;
	bool bLoadPlugins;

	int iScreenRotation;  // The rotation angle of the PPSSPP UI. Only supported on Android and possibly other mobile platforms.
	int iInternalScreenRotation;  // The internal screen rotation angle. Useful for vertical SHMUPs and similar.

	std::string sReportHost;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;

	std::string sIgnoreCompatSettings;

	bool bDiscordPresence;  // Enables setting the Discord presence to the current game (or menu)

	// GFX
	int iGPUBackend;
	std::string sCustomDriver;
	std::string sFailedGPUBackends;
	std::string sDisabledGPUBackends;
	// We have separate device parameters for each backend so it doesn't get erased if you switch backends.
	// If not set, will use the "best" device.
	std::string sVulkanDevice;
	std::string sD3D11Device;  // Windows only
	std::string sCameraDevice;
	std::string sMicDevice;
	bool bCameraMirrorHorizontal;
	int iDisplayFramerateMode;  // enum DisplayFramerateMode. Android-only.
	int iDisplayRefreshRate = 60;

	bool bSoftwareRendering;
	bool bSoftwareRenderingJit;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;
	bool bVendorBugChecksEnabled;
	bool bUseGeometryShader;

	// Speedhacks (more will be moved here):
	bool bSkipBufferEffects;
	bool bDisableRangeCulling;

	int iTexFiltering; // 1 = auto , 2 = nearest , 3 = linear , 4 = auto max quality
	bool bSmart2DTexFiltering;

	bool bDisplayStretch;  // Automatically matches the aspect ratio of the window.
	int iDisplayFilter;    // 1 = linear, 2 = nearest
	float fDisplayOffsetX;
	float fDisplayOffsetY;
	float fDisplayScale;   // Relative to the most constraining axis (x or y).
	bool bDisplayIntegerScale;  // Snaps scaling to integer scale factors in raw pixels.
	bool bDisplayCropTo16x9;  // Crops to 16:9 if the resolution is very close.
	float fDisplayAspectRatio;  // Stored relative to the PSP's native ratio, so 1.0 is the normal pixel aspect ratio.

	bool bImmersiveMode;  // Mode on Android Kitkat 4.4 and later that hides the back button etc.
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
	bool bShowMenuBar;  // Windows-only

	float fUITint;
	float fUISaturation;

	bool bTextureBackoffCache;
	bool bVertexDecoderJit;
	int iAppSwitchMode;
	bool bFullScreen;
	bool bFullScreenMulti;
	int iForceFullScreen = -1; // -1 = nope, 0 = force off, 1 = force on (not saved.)
	int iInternalResolution;  // 0 = Auto (native), 1 = 1x (480x272), 2 = 2x, 3 = 3x, 4 = 4x and so on.
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	int iMultiSampleLevel;
	int bHighQualityDepth;
	bool bReplaceTextures;
	bool bSaveNewTextures;
	bool bIgnoreTextureFilenames;
	int iTexScalingLevel; // 0 = auto, 1 = off, 2 = 2x, ..., 5 = 5x
	int iTexScalingType; // 0 = xBRZ, 1 = Hybrid
	bool bTexDeposterize;
	bool bTexHardwareScaling;
	int iFpsLimit1;
	int iFpsLimit2;
	int iAnalogFpsLimit;
	int iMaxRecent;
	int iCurrentStateSlot;
	int iRewindSnapshotInterval;
	bool bUISound;
	bool bEnableStateUndo;
	std::string sStateLoadUndoGame;
	std::string sStateUndoLastSaveGame;
	int iStateUndoLastSaveSlot;
	int iAutoLoadSaveState; // 0 = off, 1 = oldest, 2 = newest, >2 = slot number + 3
	bool bEnableCheats;
	bool bReloadCheats;
	bool bEnablePlugins;
	int iCwCheatRefreshIntervalMs;
	float fCwCheatScrollPosition;
	float fGameListScrollPosition;
	int iBloomHack; //0 = off, 1 = safe, 2 = balanced, 3 = aggressive
	int iSkipGPUReadbackMode;  // 0 = off, 1 = skip, 2 = to texture
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	bool bHardwareTessellation;
	bool bShaderCache;  // Hidden ini-only setting, useful for debugging shader compile times.
	bool bUberShaderVertex;
	bool bUberShaderFragment;

	std::vector<std::string> vPostShaderNames; // Off for chain end (only Off for no shader)
	std::map<std::string, float> mPostShaderSetting;

	// Note that this is separate from VR stereo, though it'll share some code paths.
	bool bStereoRendering;
	// There can only be one, unlike regular post shaders.
	std::string sStereoToMonoShader;

	bool bShaderChainRequires60FPS;
	std::string sTextureShaderName;
	bool bGfxDebugOutput;
	int iInflightFrames;
	bool bRenderDuplicateFrames;
	bool bRenderMultiThreading;

	// HW debug
	bool bShowGPOLEDs;

	// Sound
	bool bEnableSound;
	int iAudioBackend;
	int iGlobalVolume;
	int iReverbVolume;
	int iAltSpeedVolume;
	int iAchievementSoundVolume;
	bool bExtraAudioBuffering;  // For bluetooth
	std::string sAudioDevice;
	bool bAutoAudioDevice;
	bool bUseNewAtrac;

	// iOS only for now
	bool bAudioMixWithOthers;
	bool bAudioRespectSilentMode;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowStatusFlags;
	bool bShowRegionOnGameIcon;
	bool bShowIDOnGameIcon;
	float fGameGridScale;
	bool bShowOnScreenMessages;
	int iBackgroundAnimation;  // enum BackgroundAnimation
	bool bTransparentBackground;

	std::string sThemeName;

	// These aren't saved, just for instant debugging.
	bool bLogFrameDrops;

	// Analog stick tilting
	// This is the held base angle (from the horizon), that we compute the tilt relative from.
	float fTiltBaseAngleY;
	// Inverts the direction of the x axes and y axes for the purposes of tilt input.
	bool bInvertTiltX;
	bool bInvertTiltY;
	// The sensitivity of the tilt in the X and Y directions, separately.
	int iTiltSensitivityX;
	int iTiltSensitivityY;
	// The deadzone radius of the tilt. Only used in the analog mapping.
	float fTiltAnalogDeadzoneRadius;
	float fTiltInverseDeadzone;  // An inverse deadzone for the output, counteracting excessive deadzones applied by games. See #17483.
	bool bTiltCircularDeadzone;
	// Type of tilt input currently selected: Defined in TiltEventProcessor.h
	// 0 - no tilt, 1 - analog stick, 2 - D-Pad, 3 - Action Buttons (Tri, Cross, Square, Circle)
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
	bool bAnalogGesture;
	float fAnalogGestureSensibility;

	// Disable diagonals
	bool bDisableDpadDiagonals;
	bool bGamepadOnlyFocused;
	// Control Style
	int iTouchButtonStyle;
	int iTouchButtonOpacity;
	int iTouchButtonHideSeconds;

	// Snap touch control position
	bool bTouchSnapToGrid;
	int iTouchSnapGridSize;

	// Floating analog stick (recenters on thumb on press).
	bool bAutoCenterTouchAnalog;

	// Sticky D-pad (can't glide off it)
	bool bStickyTouchDPad;

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

	enum { CUSTOM_BUTTON_COUNT = 20 };

	ConfigTouchPos touchCustom[CUSTOM_BUTTON_COUNT];

	float fLeftStickHeadScale;
	float fRightStickHeadScale;
	bool bHideStickBackground;

	// Controls Visibility
	bool bShowTouchControls;

	bool bShowTouchCircle;
	bool bShowTouchCross;
	bool bShowTouchTriangle;
	bool bShowTouchSquare;

	ConfigCustomButton CustomButton[CUSTOM_BUTTON_COUNT];

	// Ignored on iOS and other platforms that lack pause.
	bool bShowTouchPause;

	bool bHapticFeedback;

	// We also use the XInput settings as analog settings on other platforms like Android.
	float fAnalogDeadzone;
	float fAnalogInverseDeadzone;
	float fAnalogSensitivity;
	// convert analog stick circle to square
	bool bAnalogIsCircular;
	// Auto rotation speed
	float fAnalogAutoRotSpeed;

	// Sets up how much the analog limiter button restricts digital->analog input.
	float fAnalogLimiterDeadzone;

	// Trigger configuration
	float fAnalogTriggerThreshold;

	// Sets whether combo mapping is enabled.
	bool bAllowMappingCombos;
	bool bStrictComboOrder;

	bool bMouseControl;
	bool bMapMouse; // Workaround for mapping screen:|
	bool bMouseConfine; // Trap inside the window.
	float fMouseSensitivity;
	float fMouseSmoothing;
	int iMouseWheelUpDelayMs;

	bool bSystemControls;
	int iRapidFireInterval;

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

	// Virtual reality
	bool bEnableVR;
	bool bEnable6DoF;
	bool bEnableStereo;
	bool bEnableImmersiveVR;
	bool bForce72Hz;
	bool bForceVR;
	bool bManualForceVR;
	bool bPassthrough;
	bool bRescaleHUD;
	float fCameraDistance;
	float fCameraHeight;
	float fCameraSide;
	float fCameraPitch;
	float fCanvasDistance;
	float fCanvas3DDistance;
	float fFieldOfViewPercentage;
	float fHeadUpDisplayScale;

	// Debugger
	int iDisasmWindowX;
	int iDisasmWindowY;
	int iDisasmWindowW;
	int iDisasmWindowH;
	int iGEWindowX;
	int iGEWindowY;
	int iGEWindowW;
	int iGEWindowH;
	uint32_t uGETabsLeft;
	uint32_t uGETabsRight;
	uint32_t uGETabsTopRight;
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
	std::string sSkipFuncHashMap;
	bool bDebugMemInfoDetailed;

	// Volatile development settings
	// Overlays
	int iDebugOverlay;

	bool bGpuLogProfiler; // Controls the Vulkan logging profiler (profiles textures uploads etc).

	// Retro Achievement settings
	// Copied from Duckstation, we might want to remove some.
	bool bAchievementsEnable;
	bool bAchievementsHardcoreMode;
	bool bAchievementsEncoreMode;
	bool bAchievementsUnofficial;
	bool bAchievementsSoundEffects;
	bool bAchievementsLogBadMemReads;
	bool bAchievementsSaveStateInHardcoreMode;
	bool bAchievementsEnableRAIntegration;

	// Positioning of the various notifications
	int iAchievementsLeaderboardTrackerPos;
	int iAchievementsLeaderboardStartedOrFailedPos;
	int iAchievementsLeaderboardSubmittedPos;
	int iAchievementsProgressPos;
	int iAchievementsChallengePos;
	int iAchievementsUnlockedPos;

	// Customizations
	std::string sAchievementsUnlockAudioFile;
	std::string sAchievementsLeaderboardSubmitAudioFile;

	// Achivements login info. Note that password is NOT stored, only a login token.
	// Still, we may wanna store it more securely than in PPSSPP.ini, especially on Android.
	std::string sAchievementsUserName;
	std::string sAchievementsToken;  // Not saved, to be used if you want to manually make your RA login persistent. See Native_SaveSecret for the normal case.

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
	void RestoreDefaults(RestoreSettingsBits whatToRestore);

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

	static void DownloadCompletedCallback(http::Request &download);
	void DismissUpgrade();

	void ResetControlLayout();

	void GetReportingInfo(UrlEncoder &data);

	bool IsPortrait() const;
	int NextValidBackend();
	bool IsBackendEnabled(GPUBackend backend);

	bool UseFullScreen() const {
		if (iForceFullScreen != -1)
			return iForceFullScreen == 1;
		return bFullScreen;
	}

	std::vector<std::string> RecentIsos() const;
	bool HasRecentIsos() const;
	void ClearRecentIsos();

	const std::map<std::string, std::pair<std::string, int>, std::less<>> &GetLangValuesMapping();
	bool LoadAppendedConfig();
	void SetAppendedConfigIni(const Path &path);
	void UpdateAfterSettingAutoFrameSkip();
	void NotifyUpdatedCpuCore();

	// Applies the Auto setting if set. Returns an enum value from PSP_SYSTEMPARAM_LANGUAGE_*.
	int GetPSPLanguage();

	PlayTimeTracker &TimeTracker() { return playTimeTracker_; }

protected:
	void LoadStandardControllerIni();
	void LoadLangValuesMapping();

	void PostLoadCleanup(bool gameSpecific);
	void PreSaveCleanup(bool gameSpecific);
	void PostSaveCleanup(bool gameSpecific);

private:
	bool reload_ = false;
	std::string gameId_;
	std::string gameIdTitle_;
	std::vector<std::string> recentIsos;
	std::map<std::string, std::pair<std::string, int>, std::less<>> langValuesMapping_;
	PlayTimeTracker playTimeTracker_;
	Path iniFilename_;
	Path controllerIniFilename_;
	Path searchPath_;
	Path appendedConfigFileName_;
	// A set make more sense, but won't have many entry, and I dont want to include the whole std::set header here
	std::vector<std::string> appendedConfigUpdatedGames_;
	ConfigPrivate *private_ = nullptr;
};

std::string CreateRandMAC();

// TODO: Find a better place for this.
extern http::RequestManager g_DownloadManager;
extern Config g_Config;

