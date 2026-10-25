
// Copyright (c) 2013- PPSSPP Project.

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

#include "ppsspp_config.h"

#include <algorithm>
#include <set>

#include "Common/Net/Resolve.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/UI/Notice.h"
#include "Common/UI/ScreenManager.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/BitSet.h"
#include "Common/System/Display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/UI/PopupScreens.h"
#include "UI/EmuScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/Settings/MainSettingsPages.h"
#include "UI/SettingsPage.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DeveloperSettingsPages.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/FirmwareScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/SystemInfoScreen.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/MemStickScreen.h"
#include "UI/Theme.h"
#include "UI/RetroAchievementScreens.h"
#include "UI/OnScreenDisplay.h"
#include "UI/DiscordIntegration.h"
#include "UI/Background.h"
#include "UI/BackgroundAudio.h"
#include "UI/MiscViews.h"
#include "UI/AdhocServerScreen.h"

#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/KeyMap.h"
#include "Core/TiltEventProcessor.h"
#include "Core/Instance.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Util/PortManager.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GPU.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "Core/Util/DarwinFileSystemServices.h"
#endif

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "Windows/MainWindow.h"
#include <shlobj.h>
#include "Windows/W32Util/ShellUtil.h"
#endif

#if PPSSPP_PLATFORM(ANDROID)

#include "android/jni/AndroidAudio.h"
#include "Common/File/AndroidStorage.h"

extern AndroidAudioState *g_audioState;

#endif

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
void SetMemStickDirDarwin(int requesterToken) {
	auto initialPath = g_Config.memStickDirectory;
	INFO_LOG(Log::System, "Current path: %s", initialPath.c_str());
	System_BrowseForFolder(requesterToken, "", initialPath, [](std::string_view value, int) {
		INFO_LOG(Log::System, "Selected path: %.*s", STR_VIEW(value));
		DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path(value));
	});
}
#endif

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: SettingsPagesDialogScreen(gamePath, &g_Config.iSettingsCurrentTab, TabDialogFlags::HorizontalOnlyIcons | TabDialogFlags::VerticalShowIcons), gameID_(gameID), editGameSpecificThenRestore_(editThenRestore) {
	if (editGameSpecificThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.LoadGameConfig(gameID_);
	}

	ReadConfigTemporaries();
}

// Also called after restoring defaults, so the old values aren't written back over them.
void GameSettingsScreen::ReadConfigTemporaries() {
	// Written back on exit, so it must be read even if the System tab is never opened.
	enableReports_ = Reporting::IsEnabled();
}

GameSettingsScreen::~GameSettingsScreen() {
	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		std::string message = "Failed to save settings!\n";
#ifdef MOBILE_DEVICE
		message += "Check permissions, or try to restart the device.";
#else
		message += "Make sure this folder isn't read-only:\n";
		message += g_Config.memStickDirectory.ToVisualString();
#endif
		System_Toast(message);
	}

	if (editGameSpecificThenRestore_) {
		// We already saved above. Just unload the game config.
		// Note that we are leaving the screen here in practice, we don't need to reset the bool.
		g_Config.UnloadGameConfig();
	}

	System_Notify(SystemNotification::UI);

	// Wipe some caches after potentially changing settings.
	// Let's not send resize messages here, handled elsewhere.
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::OpenDeveloperTools() {
	OpenPage("GameSettingsTools", "Developer Tools");
}

void GameSettingsScreen::PreCreateViews() {
	if (RecreatingForPageChange()) {
		return;
	}
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	CheckMemstickInstallState();
#endif
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
// Works out where the memstick is (installed.txt next to the exe means My Documents or a custom
// folder), and which of the two checkboxes can be changed.
void GameSettingsScreen::CheckMemstickInstallState() {
	const Path &PPSSPPpath = File::GetExeDirectory();
	const Path installedFile = PPSSPPpath / "installed.txt";
	const bool myDocsExists = W32Util::UserDocumentsPath().size() != 0;

	installed_ = File::Exists(installedFile);
	otherinstalled_ = false;
	memstickMyDocEnabled_ = !PSP_IsInited();
	memstickOtherEnabled_ = false;
	if (!installed_ && myDocsExists) {
		// Disable the setting if we can't create & delete a file next to the exe.
		if (File::CreateEmptyFile(PPSSPPpath / "installedTEMP.txt")) {
			if (!(File::Delete(PPSSPPpath / "installedTEMP.txt")))
				memstickMyDocEnabled_ = false;
			else
				memstickOtherEnabled_ = !PSP_IsInited();
		} else
			memstickMyDocEnabled_ = false;
	} else {
		if (installed_ && myDocsExists) {
			FILE *testInstalled = File::OpenCFile(installedFile, "rt");
			if (testInstalled) {
				char temp[2048];
				char *tempStr = fgets(temp, sizeof(temp), testInstalled);
				// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
				if (tempStr && strncmp(tempStr, "\xEF\xBB\xBF", 3) == 0) {
					tempStr += 3;
				}
				memstickOtherEnabled_ = !PSP_IsInited();
				if (tempStr && strlen(tempStr) != 0 && strcmp(tempStr, "\n") != 0) {
					installed_ = false;
					otherinstalled_ = true;
				}
				fclose(testInstalled);
			}
		} else if (!myDocsExists) {
			memstickMyDocEnabled_ = false;
		}
	}
}
#endif

const std::vector<GameSettingsScreen::PageDef> &GameSettingsScreen::PageDefs() {
	static const std::vector<PageDef> pages = {
		{ "GameSettingsGraphics", "Graphics", "I_DISPLAY", "graphics", I18NCat::GRAPHICS, &BuildGraphicsSettings },
		{ "GameSettingsControls", "Controls", "I_CONTROLLER", "controls", I18NCat::CONTROLS, &BuildControlsSettings },
		{ "GameSettingsAudio", "Audio", "I_SPEAKER_MAX", "audio", I18NCat::AUDIO, &BuildAudioSettings },
		{ "GameSettingsNetworking", "Networking", "I_WIFI", "network", I18NCat::NETWORKING, &BuildNetworkingSettings },
		{ "GameSettingsTools", "Tools", "I_TOOLS", "tools", I18NCat::MAINSETTINGS, &BuildToolsSettings },
		{ "GameSettingsSystem", "System", "I_PSP", "system", I18NCat::SYSTEM, nullptr },
		{ "GameSettingsVR", "VR", nullptr, nullptr, I18NCat::VR, &BuildVRSettings },
	};
	return pages;
}

void GameSettingsScreen::CreateTabs() {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);

	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	bool showVR = deviceType == DEVICE_TYPE_VR || g_Config.bForceVR;

	for (const PageDef &def : PageDefs()) {
		if (def.build == &BuildVRSettings && !showVR) {
			continue;
		}
		std::function<void(SettingsPage *)> build = def.build;
		if (!build) {
			build = [this](SettingsPage *page) {
				BuildSystemSettings(page);
			};
		}
		AddSettingsTab(def.tag, ms->T(def.title), def.icon ? ImageID(def.icon) : ImageID::invalid(), def.cat, build, def.helpTag);
	}
}

void GameSettingsScreen::BuildSystemSettings(SettingsPage *page) {
	using namespace UI;

	page->Header("UI");

	page->Custom("Language", [this](UI::ViewGroup *parent) -> UI::View * {
		auto langCodeToName = [](std::string_view value) -> std::string {
			auto &mapping = GetLangValuesMapping();
			auto iter = mapping.find(value);
			if (iter != mapping.end()) {
				return iter->second.first;
			}
			return std::string(value);
		};

		Choice *language = parent->Add(new ChoiceWithValueDisplay(&g_Config.sLanguageIni, T(I18NCat::SYSTEM, "Language"), langCodeToName));
		language->OnClick.Add([this](UI::EventParams &e) {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			auto langScreen = new NewLanguageScreen(sy->T("Language"));
			// The actual switching is handled in OnCompleted in NewLanguageScreen.
			if (e.v)
				langScreen->SetPopupOrigin(e.v);
			screenManager()->push(langScreen);
		});
		return language;
	});

#if PPSSPP_PLATFORM(IOS)
	static const char *indicator[] = {
		"Swipe once to switch app (indicator auto-hides)",
		"Swipe twice to switch app (indicator stays visible)"
	};
	page->Setting(&g_Config.iAppSwitchMode);

	{
		// Note: On iPhone, iOS hides the status bar in landscape no matter what this is set to.
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([](EventParams &e) {
			System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
		});
	}
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		page->Setting(&config.bImmersiveMode).OnChange([this](EventParams &e) {
			OnImmersiveModeChange(e);
		});
	}
#endif

	UIContext *ctx = screenManager()->getUIContext();
	page->Setting(&g_Config.iUIScaleFactor).OnChange([ctx](UI::EventParams &e) {
		const float dpiMul = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
		g_display.Recalculate(-1, -1, -1, -1, dpiMul);
		ctx->InvalidateAtlas();
		NativeResized();
	});

	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		page->Choice("Clear UI background").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER) || System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		page->Choice("Set UI background...").OnClick([this](UI::EventParams &e) {
			OnChangeBackground(e);
		});
	}

	page->Setting(&g_Config.bTransparentBackground);

	// Shared with achievements.

	page->Setting(&g_Config.iNotificationPos);

	page->Setting(&g_Config.iBackgroundAnimation);

	page->Custom("Theme", [this](UI::ViewGroup *parent) -> UI::View * {
		PopupMultiChoiceDynamic *theme = parent->Add(new PopupMultiChoiceDynamic(&g_Config.sThemeName, T(I18NCat::SYSTEM, "Theme"), GetThemeInfoNames(), I18NCat::THEMES, screenManager()));
		theme->OnChoice.Add([](EventParams &e) {
			UpdateTheme();
			// Reset the tint/saturation if the theme changed.
			if (e.b) {
				g_Config.fUITint = 0.0f;
				g_Config.fUISaturation = 1.0f;
			}
		});
		return theme;
	});

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	if (!draw->GetBugs().Has(Draw::Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		// We use shaders without tint capability on hardware with this driver bug.
		auto noDropShadow = [](View *view) {
			static_cast<PopupSliderChoiceFloat *>(view)->SetHasDropShadow(false);
		};
		page->Setting(&g_Config.fUITint).LiveUpdate().Setup(noDropShadow);
		page->Setting(&g_Config.fUISaturation).LiveUpdate().Setup(noDropShadow);
	}

	page->Header("PSP Memory Stick");

	if (System_GetPropertyBool(SYSPROP_HAS_OPEN_DIRECTORY)) {
		page->Choice("Show Memory Stick folder").OnClick([](UI::EventParams &p) {
			System_LaunchUrl(LaunchUrlType::LOCAL_FOLDER, g_Config.memStickDirectory.ToString());
		});
	}

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	bool showItHere = true;
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	if (g_Config.memStickDirectory == DarwinFileSystemServices::defaultMemoryStickPath()) {
		// We still keep a way to access it on the developer tools screen.
		showItHere = false;
	}
#endif
	if (showItHere) {
		page->Choice("Set Memory Stick folder").OnClick([this](UI::EventParams &) {
			SetMemStickDirDarwin(GetRequesterToken());
		});
	}
#endif

	auto addMemstickPath = [this, &page]() {
		page->Custom("Memory Stick folder", [this](UI::ViewGroup *parent) -> UI::View * {
			Choice *memstickPath = parent->Add(new ChoiceWithCallbackValueDisplay(T(I18NCat::SYSTEM, "Memory Stick folder"), []() {
				return g_Config.memStickDirectory.ToVisualString();
			}));
			memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnShowMemstickScreen);
			return memstickPath;
		}).EnabledFunc([] {
			return !PSP_IsInited();
		});
	};

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR) {
		addMemstickPath();
	}
#elif defined(_WIN32)
#if PPSSPP_PLATFORM(UWP)
	addMemstickPath();
#else
	page->Bool(&installed_, "Memory Stick in My Documents").Enabled(&memstickMyDocEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickMyDoc(e);
	});
	page->Bool(&otherinstalled_, "Memory Stick in installed.txt").Enabled(&memstickOtherEnabled_).OnChange([this](UI::EventParams &e) {
		OnMemoryStickOther(e);
	});

	if (!System_GetPropertyBool(SYSPROP_CAN_GET_FREE_SPACE_FAST)) {
		// It might be slow, so use an option.
		page->Setting(&g_Config.bReportAccurateFreeStorageSpace);
	}
#endif
#endif
	page->Setting(&g_Config.bMemStickInserted);
	page->Setting(&g_Config.iMemStickSizeGB);

	page->Header("Help the PPSSPP team");
	page->Bool(&enableReports_, "Enable Compatibility Server Reports").EnabledFunc([]() {
		return Reporting::IsSupported();
	});

	page->Header("Emulation");

	page->Setting(&g_Config.bFastMemory).OnChange([this](UI::EventParams &e) {
		OnJitAffectingSetting(e);
	});
	page->Setting(&g_Config.bIgnoreBadMemAccess);

	page->Setting(&g_Config.iIOTimingMethod);
	page->Setting(&g_Config.bForceLagSync).Disabled(&g_Config.bAutoFrameSkip);
	page->Setting(&g_Config.iLockedCPUSpeed);

	page->Header({ I18NCat::SAVEDATA, "Save states" });  // Borrow this string from the savedata manager

	page->Setting(&g_Config.bEnableStateUndo);
	page->Setting(&g_Config.bConfirmLoadState);

	page->Setting(&g_Config.iSaveStateSlotCount);

	// NOTE: We will soon support more states, but we'll keep this niche feature limited to the first five.
	page->Setting(&g_Config.iAutoLoadSaveState).Setup([](View *view) {
		if (g_Config.iAutoLoadSaveState != 1) {
			static_cast<PopupMultiChoice *>(view)->HideChoice(1);  // Hide "Oldest Save" if not using that mode. It doesn't make sense.
		}
	});

	page->Setting(&g_Config.iRewindSnapshotInterval);

	page->Header("General");

	page->Setting(&g_Config.iAskForExitConfirmationAfterSeconds);

	if (System_GetPropertyBool(SYSPROP_CAN_RESTRICT_ORIENTATION)) {
		// Display rotation
		page->Custom({ I18NCat::CONTROLS, "Screen Rotation" }, [this](UI::ViewGroup *parent) -> UI::View * {
			AddRotationPicker(screenManager(), parent, true);
			return parent->GetViewByIndex(parent->GetNumSubviews() - 1);
		});
	}

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
		page->Setting(&g_Config.bSustainedPerformanceMode).OnChange([this](UI::EventParams &e) {
			OnSustainedPerformanceModeChange(e);
		});
	}

	page->Choice("Restore Default Settings").OnClick([this](UI::EventParams &e) {
		OnRestoreDefaultSettings(e);
	});

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD))
		page->Setting(&g_Config.bBypassOSKWithKeyboard);

	if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
		page->Setting(&g_Config.bCacheFullIsoInRam).EnabledFunc([] {
			return !PSP_IsInited();
		});
	}

	page->Setting(&g_Config.bCheckForNewVersion);
	page->Setting(&g_Config.bScreenshotsAsPNG);
	page->Setting(&g_Config.iScreenshotMode);
	// TODO: Make this setting available on Mac too.
#if PPSSPP_PLATFORM(WINDOWS) || (defined (SDL) && !defined(MOBILE_DEVICE))
	page->Setting(&g_Config.bPauseOnLostFocus);
#endif

	std::vector<std::string> cameraList = Camera::getDeviceList();
	if (cameraList.size() >= 1) {
		page->Header({ I18NCat::GRAPHICS, "Camera" });
		page->Setting(&g_Config.sCameraDevice, std::move(cameraList));
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		page->Setting(&g_Config.bCameraMirrorHorizontal);
#endif
	}

	page->Header("Cheats");
	page->Setting(&g_Config.bEnableCheats);
	page->Setting(&g_Config.bEnablePlugins);

	page->Header("PSP Settings");

	// The ordering here is simply mapping directly to PSP_SYSTEMPARAM_LANGUAGE_*.
	page->Setting(&g_Config.iLanguage);
	page->Setting(&g_Config.iPSPModel).EnabledFunc([] {
		return !PSP_IsInited();
	});
	page->Custom("Nickname", [this](UI::ViewGroup *parent) {
		return AddNicknameChoice(this, parent);
	});
	page->Setting(&g_Config.bDayLightSavings);
	page->Setting(&g_Config.iDateFormat);
	page->Setting(&g_Config.iTimeFormat);
	page->Setting(&g_Config.iButtonPreference);

#if defined(_WIN32)
	page->Header("Recording");
	page->Setting(&g_Config.bDumpFrames);
	page->Setting(&g_Config.bUseFFV1);
	page->Setting(&g_Config.bDumpVideoOutput);
	page->Setting(&g_Config.bDumpAudio);
	page->Setting(&g_Config.bSaveLoadResetsAVdumping);
#endif
}

void GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		System_RecreateActivity();
	}
}

void GameSettingsScreen::OnSustainedPerformanceModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::SUSTAINED_PERF_CHANGE);
}

void GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
}

void GameSettingsScreen::OnShowMemstickScreen(UI::EventParams &e) {
	screenManager()->push(new MemStickScreen(false));
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

void GameSettingsScreen::OnMemoryStickMyDoc(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	const Path installedFile = PPSSPPpath / "installed.txt";
	installed_ = File::Exists(installedFile);
	if (otherinstalled_) {
		File::Delete(PPSSPPpath / "installed.txt");
		File::CreateEmptyFile(PPSSPPpath / "installed.txt");
		otherinstalled_ = false;
		const std::string myDocsPath = W32Util::UserDocumentsPath() + "/PPSSPP/";
		g_Config.memStickDirectory = Path(myDocsPath);
	} else if (installed_) {
		File::Delete(PPSSPPpath / "installed.txt");
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath / "memstick";
	} else {
		FILE *f = File::OpenCFile(PPSSPPpath / "installed.txt", "wb");
		if (f) {
			fclose(f);
		}

		const std::string myDocsPath = W32Util::UserDocumentsPath() + "/PPSSPP/";
		g_Config.memStickDirectory = Path(myDocsPath);
		installed_ = true;
	}
}

void GameSettingsScreen::OnMemoryStickOther(UI::EventParams &e) {
	const Path &PPSSPPpath = File::GetExeDirectory();
	if (otherinstalled_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string initialPath = g_Config.memStickDirectory.ToCString();
		std::string folder = W32Util::BrowseForFolder2(MainWindow::GetHWND(), di->T("Choose PPSSPP save folder"), initialPath);
		if (folder.size()) {
			g_Config.memStickDirectory = Path(folder);
			FILE *f = File::OpenCFile(PPSSPPpath / "installed.txt", "wb");
			if (f) {
				std::string utfstring("\xEF\xBB\xBF");
				utfstring.append(folder);
				fwrite(utfstring.c_str(), 1, utfstring.length(), f);
				fclose(f);
			}
			installed_ = false;
		}
		else
			otherinstalled_ = false;
	}
	else {
		File::Delete(PPSSPPpath / "installed.txt");
		memstickMyDocEnabled_ = true;
		otherinstalled_ = false;
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath / "memstick";
	}
}

#endif

void GameSettingsScreen::OnChangeBackground(UI::EventParams &e) {
	const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
	const Path bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) / "background.jpg";

	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		INFO_LOG(Log::UI, "Clearing background image.");
		// The button is in clear mode.
		File::Delete(bgPng);
		File::Delete(bgJpg);
		UIBackgroundShutdown();
		RecreateViews();
		return;
	}

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	System_BrowseForImage(GetRequesterToken(), sy->T("Set UI background..."), bgJpg, [this](std::string_view value, int converted) {
		if (converted == 1) {
			// The platform code converted and saved the file to the desired path already.
			INFO_LOG(Log::UI, "Platform converted the file: %.*s", STR_VIEW(value));
		} else if (!value.empty()) {
			Path path(value);

			// Check the file format. Don't rely on the file extension here due to scoped storage URLs.
			FILE *f = File::OpenCFile(path, "rb");
			uint8_t buffer[8];
			ImageFileType type = ImageFileType::UNKNOWN;
			if (f != nullptr && 8 == fread(buffer, 1, ARRAY_SIZE(buffer), f)) {
				type = DetectImageFileType(buffer, ARRAY_SIZE(buffer));
			}
			if (f) {
				fclose(f);
			}

			std::string filename;
			switch (type) {
			case ImageFileType::JPEG:
				filename = "background.jpg";
				break;
			case ImageFileType::PNG:
				filename = "background.png";
				break;
			default:
				break;
			}

			if (!filename.empty()) {
				Path dest = GetSysDirectory(DIRECTORY_SYSTEM) / filename;
				File::Copy(path, dest);
				if (path.FilePathContainsNoCase("temp_import.jpg")) {
					INFO_LOG(Log::UI, "Deleting temp file: %s", GetFriendlyPath(path).c_str());
					File::Delete(path);
				}
			} else {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				g_OSD.Show(OSDType::MESSAGE_ERROR, sy->T("Only JPG and PNG images are supported"), path.GetFilename(), 5.0);
			}
		}
		// It will init again automatically.  We can't init outside a frame on Vulkan.
		UIBackgroundShutdown();
		RecreateViews();
	});

	// Change to a browse or clear button.
}

void GameSettingsScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (equals(dialog->tag(), "NewLanguage") && result == DR_OK) {
		screenManager()->RecreateAllViews();
		return;
	}

	bool recreate = false;

	if (equals(dialog->tag(), "AdhocServer")) {
		const AdhocServerScreen *scr = dynamic_cast<const AdhocServerScreen *>(dialog);
		if (scr && scr->RecreateParent()) {
			recreate = true;
		}
	}

	if (result == DialogResult::DR_OK) {
		if (equals(dialog->tag(), "RestoreSettingsScreen")) {
			ReadConfigTemporaries();
		}
		recreate = true;
	}

	// The analog alternative speed is only shown when it's mapped.
	if (equals(dialog->tag(), "ControlMapping")) {
		recreate = true;
	}

	if (recreate) {
		RecreateViews();
	}
}

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath) {
	// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
	// the GPU backend code.
	g_Config.Save(why);
	std::string param = "--start-screen=gamesettings";
	if (editThenRestore) {
		// We won't pass the gameID, so don't resume back into settings.
		param.clear();
	} else if (!gamePath.empty()) {
		param += " \"" + ReplaceAll(ReplaceAll(gamePath.ToString(), "\\", "\\\\"), "\"", "\\\"") + "\"";
	}
	// Make sure the new instance is considered the first.
	ShutdownInstanceCounter();
	System_RestartApp(param);
}

// From PauseScreen
std::string GetConfirmExitMessage();

void GameSettingsScreen::RestartOrDo(std::function<void()> callback) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::string confirmExitMessage = GetConfirmExitMessage();
	if (!confirmExitMessage.empty()) {
		confirmExitMessage = "\n\n" + confirmExitMessage;
	}
	confirmExitMessage = std::string(di->T("Changing this setting requires PPSSPP to restart.")) + confirmExitMessage;

	screenManager()->push(new UI::MessagePopupScreen(di->T("Restart"), confirmExitMessage, di->T("Restart"), di->T("Cancel"), [=](bool yes) {
		if (yes) {
			TriggerRestart("GameSettingsScreen::RenderingBackendYes", editGameSpecificThenRestore_, gamePath_);
		} else {
			callback();
		}
	}));
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS);
		ReadConfigTemporaries();
	}
	System_Notify(SystemNotification::UI);
}

void GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (g_Config.IsGameSpecific()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		screenManager()->push(
			new UI::MessagePopupScreen(sy->T("Restore Default Settings"), dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"),
				di->T("OK"), di->T("Cancel"), [this](bool yes) { CallbackRestoreDefaults(yes); }));
	} else {
		std::string_view title = sy->T("Restore Default Settings");
		screenManager()->push(new RestoreSettingsScreen(title));
	}
}

void GestureMappingScreen::CreateTabs() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	AddTab("Gesture", di->T("Left side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 0, GetDeviceOrientation() == DeviceOrientation::Portrait); });
	AddTab("Gesture", di->T("Right side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 1, GetDeviceOrientation() == DeviceOrientation::Portrait); });
}

void GestureMappingScreen::CreateGestureTab(UI::LinearLayout *vert, int zoneIndex, bool portrait) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	static const char *gestureButton[ARRAY_SIZE(GestureKey::keyList) + 1];
	gestureButton[0] = "None";
	for (int i = 1; i < ARRAY_SIZE(gestureButton); ++i) {
		gestureButton[i] = KeyMap::GetPspButtonNameCharPointer(GestureKey::keyList[i - 1]);
	}

	GestureControlConfig &zone = g_Config.gestureControls[zoneIndex];

	TopBarFlags flags = TopBarFlags::NoBackButton;
	if (portrait) {
		flags |= TopBarFlags::Portrait;
	}
	vert->Add(new TopBar(*screenManager()->getUIContext(), flags, ApplySafeSubstitutions("%1: %2", co->T("Gesture"), di->T(zoneIndex == 0 ? "Left side" : "Right side"))));

	ChoiceGroupList *list = vert->Add(new ChoiceGroupList());

	list->Add(new CheckBox(&zone.bGestureControlEnabled, co->T("Enable gesture control")))->SetRoundCorners();

	ChoiceGroup *swipeGroup = list->Add(new ChoiceGroup(co->T("Swipe")));

	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSensitivity, 0.01f, 2.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);

	ChoiceGroup *doubleTapGroup = list->Add(new ChoiceGroup(co->T("Double tap")));
	doubleTapGroup->Add(new PopupMultiChoice(&zone.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()));

	ChoiceGroup *analogGroup = list->Add(new ChoiceGroup(co->T("Analog Stick")));
	analogGroup->Add(new CheckBox(&zone.bAnalogGesture, co->T("Enable analog stick gesture")));
	analogGroup->Add(new PopupSliderChoiceFloat(&zone.fAnalogGestureSensitivity, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bAnalogGesture);
}

RestoreSettingsScreen::RestoreSettingsScreen(std::string_view title)
	: PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")) {}

void RestoreSettingsScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	// Carefully re-use various translations.
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	std::string_view text = dev->T(
		"RestoreDefaultSettings",
		"Restore these settings back to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings.");

	TextView *textView = parent->Add(new TextView(text, FLAG_WRAP_TEXT, false));
	textView->SetPadding(Margins(10));

	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::SETTINGS, ga->T("Game Settings")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::CONTROLS, ms->T("Controls")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::RECENT, mm->T("Recent")));
}

void RestoreSettingsScreen::OnCompleted(DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.RestoreDefaults((RestoreSettingsBits)restoreFlags_);
	}
}
