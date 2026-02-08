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
#include <string_view>
#include <map>
#include <vector>

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Common/Math/geom2d.h"
#include "Core/ConfigValues.h"

extern const char *PPSSPP_GIT_VERSION;

namespace http {
	class Request;
	class RequestManager;
}

struct UrlEncoder;

class Section;
class IniFile;

class PlayTimeTracker {
public:
	struct PlayTime {
		int totalTimePlayed;
		double startTime;  // time_now_d() time
		uint64_t lastTimePlayed;  // UTC Unix time for portability.
	};

	// It's OK to call these redundantly.
	void Start(std::string_view gameId);
	void Stop(std::string_view gameId);
	void Reset(std::string_view gameId);

	void Load(const Section *section);
	void Save(Section *section);

	bool GetPlayedTimeString(std::string_view, std::string *str) const;

private:
	std::map<std::string, PlayTime, std::less<>> tracker_;
};

struct ConfigSetting;

struct ConfigSectionMeta {
	ConfigBlock *configBlock;
	const ConfigSetting *settings;
	size_t settingsCount;
	std::string_view section;
	std::string_view fallbackSectionName;  // used if section is not found (useful when moving settings into a struct from Config).
};

struct DisplayLayoutConfig : public ConfigBlock {
	int iDisplayFilter = SCALE_LINEAR;    // 1 = linear, 2 = nearest
	bool bDisplayStretch = false;  // Automatically matches the aspect ratio of the window.
	float fDisplayOffsetX = 0.5f;
	float fDisplayOffsetY = 0.5f;
	float fDisplayScale = 1.0f;   // Relative to the most constraining axis (x or y).
	bool bDisplayIntegerScale = false;  // Snaps scaling to integer scale factors in raw pixels.
	float fDisplayAspectRatio = 1.0f;  // Stored relative to the PSP's native ratio, so 1.0 is the normal pixel aspect ratio.
	int iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL;  // The internal screen rotation angle. Useful for vertical SHMUPs and similar.
	bool bRotateControlsWithScreen = true;  // Rotate gamepad controls along with the internal screen rotation.
	bool bIgnoreScreenInsets = true;  // Android: Center screen disregarding insets if this is enabled.

	// Deprecated
	bool bEnableCardboardVR = false; // Cardboard Master Switch
	int iCardboardScreenSize = 50; // Screen Size (in %)
	int iCardboardXShift = 0; // X-Shift of Screen (in %)
	int iCardboardYShift = 0; // Y-Shift of Screen (in %)
	bool bImmersiveMode = false;  // Mode on Android Kitkat 4.4 and later that hides the back button etc.

	bool InternalRotationIsPortrait() const;
	bool CanResetToDefault() const override { return true; }
	bool ResetToDefault(std::string_view blockName) override;
	size_t Size() const override { return sizeof(DisplayLayoutConfig); }  // For sanity checks
};

struct TouchControlConfig : public ConfigBlock {
	constexpr TouchControlConfig() {
		// Hide all extras and custom buttons by default.
		touchRightAnalogStick.show = false;
		for (size_t i = 0; i < CUSTOM_BUTTON_COUNT; i++) {
			touchCustom[i].show = false;
		}
	}
	// the PSP button's center (triangle, circle, square, cross)
	ConfigTouchPos touchActionButtonCenter;
	// space between those PSP buttons
	float fActionButtonSpacing = 1.0f;
	// the D-pad (PSP cross) position
	ConfigTouchPos touchDpad;
	// And its spacing.
	float fDpadSpacing = 1.0f;

	ConfigTouchPos touchStartKey;
	ConfigTouchPos touchSelectKey;
	ConfigTouchPos touchFastForwardKey;
	ConfigTouchPos touchLKey;
	ConfigTouchPos touchRKey;
	ConfigTouchPos touchAnalogStick;
	ConfigTouchPos touchRightAnalogStick;
	ConfigTouchPos touchPauseKey;

	enum { CUSTOM_BUTTON_COUNT = 20 };

	ConfigTouchPos touchCustom[CUSTOM_BUTTON_COUNT];

	float fLeftStickHeadScale = 1.0f;
	float fRightStickHeadScale = 1.0f;

	bool bHideStickBackground = false;

	bool bShowTouchCircle = true;
	bool bShowTouchCross = true;
	bool bShowTouchTriangle = true;
	bool bShowTouchSquare = true;

	void ResetLayout();

	bool CanResetToDefault() const override { return true; }
	bool ResetToDefault(std::string_view blockName) override;
	size_t Size() const override { return sizeof(TouchControlConfig); }  // For sanity checks
};

struct GestureControlConfig : public ConfigBlock {
	// Motion gesture controller
	bool bGestureControlEnabled = false;
	int iSwipeUp = 0;
	int iSwipeDown = 0;
	int iSwipeLeft = 0;
	int iSwipeRight = 0;
	float fSwipeSensitivity = 1.0f;
	float fSwipeSmoothing = 0.5f;
	int iDoubleTapGesture = 0;
	bool bAnalogGesture = false;
	float fAnalogGestureSensitivity = 1.0f;

	bool CanResetToDefault() const override { return true; }
	bool ResetToDefault(std::string_view blockName) override;
	size_t Size() const override { return sizeof(GestureControlConfig); }  // For sanity checks
};

struct Config : public ConfigBlock {
public:
	~Config();

	void Init();

	size_t Size() const override { return sizeof(Config); }

	// Whether to save the config on close.
	bool bSaveSettings;
	bool bFirstRun;
	bool bUpdatedInstanceCounter = false;

	int iRunCount; // To be used to for example check for updates every 10 runs and things like that.

	// Debugger
	bool bAutoRun;  // start immediately
	bool bBreakOnFrameTimeout;  // not saved

	// General
	bool bScreenshotsAsPNG;
	bool bUseFFV1;
	bool bDumpFrames;
	bool bDumpVideoOutput;
	bool bDumpAudio;
	bool bSaveLoadResetsAVdumping;
	bool bEnableLogging;
	bool bEnableFileLogging;
	int iLogOutputTypes;  // enum class LogOutput
	int iDumpFileTypes;  // DumpFileType bitflag enum
	bool bFullscreenOnDoubleclick;

	// These four are Win UI only
	bool bPauseOnLostFocus;
	bool bTopMost;
	bool bIgnoreWindowsKey;
	bool bRestartRequired;

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
	uint32_t uJitDisableFlags;

	bool bDisableHTTPS;

	bool bShrinkIfWindowSmall;
	bool bSeparateSASThread;
	int iIOTimingMethod;
	int iLockedCPUSpeed;
	bool bAutoSaveSymbolMap;
	bool bCompressSymbols;
	bool bCacheFullIsoInRam;
	int iRemoteISOPort; // Also used for serving a local remote debugger.
	std::string sLastRemoteISOServer;
	int iLastRemoteISOPort;
	bool bRemoteISOManual;
	bool bRemoteShareOnStartup;
	std::string sRemoteISOSubdir;
	std::string sRemoteISOSharedDir;
	int iRemoteISOShareType;
	bool bRemoteDebuggerOnStartup;
	bool bRemoteDebuggerLocal;
	bool bRemoteTab;
	bool bMemStickInserted;
	int iMemStickSizeGB;
	bool bLoadPlugins;
	int iAskForExitConfirmationAfterSeconds;
	int iUIScaleFactor;  // In 8ths of powers of two.
	int iDisableHLE;
	int iForceEnableHLE;  // This is the opposite of DisableHLE but can force on HLE even when we've made it permanently off. Only used in tests, not hooked up to the ini file yet.

	int iScreenRotation;  // Screen rotation lock. Only supported on Android and possibly other mobile platforms.

	std::string sReportHost;
	std::vector<std::string> vPinnedPaths;
	std::string sLanguageIni;

	std::string sIgnoreCompatSettings;

	bool bDiscordRichPresence;  // Enables setting the Discord presence to the current game (or menu)

	// GFX
	int iGPUBackend;
	std::string sCustomDriver;
	std::string sFailedGPUBackends;  // NOT stored in ppsspp.ini anymore!
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

	// These two combined choose the presentation mode.
	// vsync = false: Immediate
	// vsync = true, low latency present = true: Mailbox
	// vsync = true, low latency present = false: FIFO
	bool bVSync;
	bool bLowLatencyPresent;

	bool bSoftwareRendering;
	bool bSoftwareRenderingJit;
	bool bHardwareTransform; // only used in the GLES backend
	bool bSoftwareSkinning;
	bool bVendorBugChecksEnabled;
	bool bUseGeometryShader;

	// Speedhacks (more will be moved here):
	bool bSkipBufferEffects;
	bool bDisableRangeCulling;
	int iDepthRasterMode;

	int iTexFiltering; // 1 = auto , 2 = nearest , 3 = linear , 4 = auto max quality
	bool bSmart2DTexFiltering;

	// We'll carry over the old single layout into landscape for now.
	DisplayLayoutConfig displayLayoutLandscape;
	DisplayLayoutConfig displayLayoutPortrait;

	bool bDisplayCropTo16x9;  // Crops to 16:9 if the resolution is very close.

	bool bSustainedPerformanceMode;  // Android: Slows clocks down to avoid overheating/speed fluctuations.

	bool bShowImDebugger;

	int iFrameSkip;
	bool bAutoFrameSkip;

	int iWindowX;
	int iWindowY;
	int iWindowWidth;  // Windows and other windowed environments
	int iWindowHeight;
	int iWindowSizeState;  // WindowSizeState enum

	bool bShowMenuBar;  // Windows-only

	float fUITint;
	float fUISaturation;

	bool bTextureBackoffCache;
	bool bVertexDecoderJit;
	int iAppSwitchMode;
	bool bFullScreen;
	bool bFullScreenMulti;
	int iInternalResolution;  // 0 = Auto (native), 1 = 1x (480x272), 2 = 2x, 3 = 3x, 4 = 4x and so on.
	int iAnisotropyLevel;  // 0 - 5, powers of 2: 0 = 1x = no aniso
	int iMultiSampleLevel;
	int bHighQualityDepth;
	bool bReplaceTextures;
	bool bSaveNewTextures;
	int iReplacementTextureLoadSpeed;
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
	int iAutoLoadSaveState; // 0 = off, 1 = oldest (deprecated), 2 = newest, 3+ = slot number + 3 (up to 5)
	int iSaveStateSlotCount;
	bool bEnableCheats;
	bool bReloadCheats;
	bool bEnablePlugins;
	int iCwCheatRefreshIntervalMs;
	float fCwCheatScrollPosition;
	float fGameListScrollPosition;
	float fHomebrewScrollPosition;
	float fRemoteScrollPosition;
	int iBloomHack; //0 = off, 1 = safe, 2 = balanced, 3 = aggressive
	int iSkipGPUReadbackMode;  // 0 = off, 1 = skip, 2 = to texture
	int iSplineBezierQuality; // 0 = low , 1 = Intermediate , 2 = High
	bool bHardwareTessellation;
	bool bShaderCache;  // Hidden ini-only setting, useful for debugging shader compile times.
	bool bUberShaderVertex;
	bool bUberShaderFragment;
	int iDefaultTab;
	int iScreenshotMode;
	bool bVulkanDisableImplicitLayers;
	bool bForceFfmpegForAudioDec;

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
	int iSDLAudioBufferSize;
	int iAudioBufferSize;
	bool bFillAudioGaps;
	int iAudioPlaybackMode;

	// Legacy volume settings, 0-10. These get auto-upgraded and should not be used.
	int iLegacyGameVolume;
	int iLegacyReverbVolume;
	int iLegacyAchievementVolume;

	// Newer volume settings, 0-100
	int iGameVolume;
	int iReverbVolume;
	int iUIVolume;
	int iGamePreviewVolume;  // Volume for the game preview sound in the game grid.
	int iAchievementVolume;
	int iAltSpeedVolume;

	bool bExtraAudioBuffering;  // For bluetooth
	std::string sAudioDevice;
	bool bAutoAudioDevice;
	bool bUseOldAtrac;

	// iOS only for now
	bool bAudioMixWithOthers;
	bool bAudioRespectSilentMode;

	// UI
	bool bShowDebuggerOnLoad;
	int iShowStatusFlags;
	bool bShowRegionOnGameIcon;
	bool bShowIDOnGameIcon;
	float fGameGridScale;
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
	bool bTiltInputEnabled;

	// The four tabs (including Remote last)
	bool bGridView1;
	bool bGridView2;
	bool bGridView3;
	bool bGridView4;

	// Right analog binding
	int iRightAnalogUp;
	int iRightAnalogDown;
	int iRightAnalogLeft;
	int iRightAnalogRight;
	int iRightAnalogPress;
	bool bRightAnalogCustom;
	bool bRightAnalogDisableDiagonal;

	// 0 for left, 1 for right
	GestureControlConfig gestureControls[2];

	// Controls Visibility
	bool bShowTouchControls = false;

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

	// Touch gliding (see #14490)
	bool bTouchGliding;

	TouchControlConfig touchControlsLandscape;
	TouchControlConfig touchControlsPortrait;

	// These are shared between portrait and landscape, just the positions aren't.
	ConfigCustomButton CustomButton[TouchControlConfig::CUSTOM_BUTTON_COUNT];

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
	std::string sNickName;  // AdHoc and system nickname
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
	bool bEnableAdhocServer;
	std::string sProAdhocServer;
	bool bUseServerRelay;
	std::vector<std::string> proAdhocServerList;
	std::string sInfrastructureDNSServer;
	std::string sInfrastructureUsername;  // Username used for Infrastructure play. Different restrictions.
	bool bInfrastructureAutoDNS;
	bool bAllowSavestateWhileConnected;  // Developer option, ini-only. No normal users need this, it's always wrong to save/load state when online.
	bool bAllowSpeedControlWhileConnected;  // Useful in some games but not recommended.

	bool bEnableWlan;
	std::map<std::string, std::string> mHostToAlias;  // Local DNS database stored in ini file
	bool bEnableUPnP;
	bool bUPnPUseOriginalPort;
	bool bForcedFirstConnect;
	int iPortOffset;
	int iMinTimeout;
	int iWlanAdhocChannel;
	bool bWlanPowerSave;
	bool bEnableNetworkChat;
	bool bDontDownloadInfraJson;
	int iChatButtonPosition;
	int iChatScreenPosition;

	bool bEnableQuickChat;
	std::string sQuickChat[5];

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
	int iNotificationPos;
	int iAchievementsLeaderboardTrackerPos;
	int iAchievementsLeaderboardStartedOrFailedPos;
	int iAchievementsLeaderboardSubmittedPos;
	int iAchievementsProgressPos;
	int iAchievementsChallengePos;
	int iAchievementsUnlockedPos;

	// Customizations
	std::string sAchievementsUnlockAudioFile;
	std::string sAchievementsLeaderboardSubmitAudioFile;

	// Achievements login info. Note that password is NOT stored, only a login token.
	// Still, we may wanna store it more securely than in PPSSPP.ini, especially on Android.
	std::string sAchievementsUserName;
	std::string sAchievementsToken;  // Not saved, to be used if you want to manually make your RA login persistent. See Native_SaveSecret for the normal case.
	std::string sAchievementsHost;  // Optional custom host for debugging against alternate RA servers.

	// Various directories. Autoconfigured, not read from ini.
	Path currentDirectory;  // The directory selected in the game browsing window.
	Path defaultCurrentDirectory;  // Platform dependent, initialized at startup.

	Path memStickDirectory;
	Path flash0Directory;
	Path internalDataDirectory;
	Path appCacheDirectory;

	Path mountRoot;  // Actually, mount as host0. keeping consistent with headless args.

	void Load(const char *iniFileName = nullptr, const char *controllerIniFilename = nullptr);
	bool Save(const char *saveReason);
	void Reload();
	void RestoreDefaults(RestoreSettingsBits whatToRestore, bool log = false);

	// Note: This doesn't switch to the config, just creates it.
	bool CreateGameConfig(std::string_view gameId);
	bool DeleteGameConfig(std::string_view gameId);
	bool LoadGameConfig(const std::string &gameId);
	bool SaveGameConfig(const std::string &pGameId, std::string_view titleForComment);
	void UnloadGameConfig();

	bool HasGameConfig(std::string_view gameId);
	bool IsGameSpecific() const { return !gameId_.empty(); }

	void SetSearchPath(const Path &path);

	void UpdateIniLocation(const char *iniFileName = nullptr, const char *controllerIniFilename = nullptr);

	void GetReportingInfo(UrlEncoder &data) const;

	int NextValidBackend();
	bool IsBackendEnabled(GPUBackend backend);

	bool LoadAppendedConfig();
	void SetAppendedConfigIni(const Path &path) { appendedConfigFileName_ = path; }
	void UpdateAfterSettingAutoFrameSkip();
	void NotifyUpdatedCpuCore();

	PlayTimeTracker &TimeTracker() { return playTimeTracker_; }

	const DisplayLayoutConfig &GetDisplayLayoutConfig(DeviceOrientation orientation) const {
		return orientation == DeviceOrientation::Portrait ? displayLayoutPortrait : displayLayoutLandscape;
	}
	DisplayLayoutConfig &GetDisplayLayoutConfig(DeviceOrientation orientation) {
		return orientation == DeviceOrientation::Portrait ? displayLayoutPortrait : displayLayoutLandscape;
	}
	const TouchControlConfig &GetTouchControlsConfig(DeviceOrientation orientation) const {
		return orientation == DeviceOrientation::Portrait ? touchControlsPortrait : touchControlsLandscape;
	}
	TouchControlConfig &GetTouchControlsConfig(DeviceOrientation orientation) {
		return orientation == DeviceOrientation::Portrait ? touchControlsPortrait : touchControlsLandscape;
	}

	static int GetDefaultValueInt(int *configSetting);

	void DoNotSaveSetting(void *configSetting) {
		settingsNotToSave_.push_back(configSetting);
	}

private:
	void LoadStandardControllerIni();

	void PostLoadCleanup();
	void PreSaveCleanup();
	void PostSaveCleanup();

	friend struct ConfigSetting;

	static std::map<const void *, std::pair<const ConfigBlock *, const ConfigSetting *>> &getPtrLUT();

	// Applies defaults for missing settings.
	void ReadAllSettings(const IniFile &iniFile);

	bool inReload_ = false;

	// If not empty, we're using a game-specific config.
	std::string gameId_;

	PlayTimeTracker playTimeTracker_;

	// Always the paths to the main configs, doesn't change with game-specific overlay.
	Path iniFilename_;
	Path controllerIniFilename_;

	Path searchPath_;
	Path appendedConfigFileName_;
	// A set make more sense, but won't have many entry, and I dont want to include the whole std::set header here
	std::vector<std::string> appendedConfigUpdatedGames_;
	std::vector<void *> settingsNotToSave_;

	bool ShouldSaveSetting(const void *configSetting) const;
};

std::string CreateRandMAC();

// TODO: Find a better place for this.
extern http::RequestManager g_DownloadManager;
extern Config g_Config;

