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

#include "ppsspp_config.h"

#include <functional>

using namespace std::placeholders;

#include "Common/Render/TextureAtlas.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log/LogManager.h"
#include "Common/UI/Root.h"
#include "Common/UI/UI.h"
#include "Common/UI/Context.h"
#include "Common/UI/Tween.h"
#include "Common/UI/View.h"
#include "Common/UI/AsyncImageFileView.h"
#include "Common/VR/PPSSPPVR.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Math/curves.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#ifndef MOBILE_DEVICE
#include "Core/AVIDump.h"
#endif
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Core.h"
#include "Core/KeyMap.h"
#include "Core/MemFault.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/Common/PresentationCommon.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "GPU/GPUState.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#if !PPSSPP_PLATFORM(UWP)
#include "GPU/Vulkan/DebugVisVulkan.h"
#endif
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceSas.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/RetroAchievements.h"
#include "Core/SaveState.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "Core/HLE/__sceAudio.h"
// #include "Core/HLE/proAdhoc.h"
#include "Core/HW/Display.h"

#include "UI/BackgroundAudio.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GamepadEmu.h"
#include "UI/PauseScreen.h"
#include "UI/MainScreen.h"
#include "UI/Background.h"
#include "UI/EmuScreen.h"
#include "UI/DevScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/BaseScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ProfilerDraw.h"
#include "UI/DiscordIntegration.h"
#include "UI/ChatScreen.h"
#include "UI/DebugOverlay.h"

#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#include "ext/imgui/imgui_impl_platform.h"

#include "Core/Reporting.h"

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
#include "Windows/MainWindow.h"
#endif

#ifndef MOBILE_DEVICE
static AVIDump avi;
#endif

extern bool g_TakeScreenshot;

static void AssertCancelCallback(const char *message, void *userdata) {
	NOTICE_LOG(Log::CPU, "Broke after assert: %s", message);
	Core_Break(BreakReason::AssertChoice);
	g_Config.bShowImDebugger = true;

	EmuScreen *emuScreen = (EmuScreen *)userdata;
	emuScreen->SendImDebuggerCommand(ImCommand{ ImCmd::SHOW_IN_CPU_DISASM, currentMIPS->pc });
}

// Handles control rotation due to internal screen rotation.
static void SetPSPAnalog(int iInternalScreenRotation, int stick, float x, float y) {
	switch (iInternalScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL:
		// Standard rotation. No change.
		break;
	case ROTATION_LOCKED_HORIZONTAL180:
		x = -x;
		y = -y;
		break;
	case ROTATION_LOCKED_VERTICAL:
	{
		float new_y = y;
		x = -y;
		y = new_y;
		break;
	}
	case ROTATION_LOCKED_VERTICAL180:
	{
		float new_y = -x;
		x = y;
		y = new_y;
		break;
	}
	default:
		break;
	}
	__CtrlSetAnalogXY(stick, x, y);
}

EmuScreen::EmuScreen(const Path &filename)
	: gamePath_(filename) {
	saveStateSlot_ = SaveState::GetCurrentSlot();
	controlMapper_.SetCallbacks(
		std::bind(&EmuScreen::onVKey, this, _1, _2),
		std::bind(&EmuScreen::onVKeyAnalog, this, _1, _2),
		[](uint32_t bitsToSet, uint32_t bitsToClear) {
			__CtrlUpdateButtons(bitsToSet, bitsToClear);
		},
		&SetPSPAnalog,
		nullptr);

	_dbg_assert_(coreState == CORE_POWERDOWN);

	OnDevMenu.Handle(this, &EmuScreen::OnDevTools);
	OnChatMenu.Handle(this, &EmuScreen::OnChat);

	// Usually, we don't want focus movement enabled on this screen, so disable on start.
	// Only if you open chat or dev tools do we want it to start working.
	UI::EnableFocusMovement(false);
}

bool EmuScreen::bootAllowStorage(const Path &filename) {
	// No permissions needed.  The easy life.
	if (filename.Type() == PathType::HTTP)
		return true;

	if (!System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS))
		return true;

	PermissionStatus status = System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE);
	switch (status) {
	case PERMISSION_STATUS_UNKNOWN:
		System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
		return false;

	case PERMISSION_STATUS_DENIED:
		if (!bootPending_) {
			screenManager()->switchScreen(new MainScreen());
		}
		return false;

	case PERMISSION_STATUS_PENDING:
		// Keep waiting.
		return false;

	case PERMISSION_STATUS_GRANTED:
		return true;
	}

	_assert_(false);
	return false;
}

void EmuScreen::ProcessGameBoot(const Path &filename) {
	if (!bootPending_ && !readyToFinishBoot_) {
		// Nothing to do.
		return;
	}

	if (!root_) {
		// Views not created yet, wait until they are. Not sure if this can actually happen
		// but crash reports seem to indicate it.
		return;
	}

	// Check permission status first, in case we came from a shortcut.
	if (!bootAllowStorage(filename)) {
		return;
	}

	if (Achievements::IsBlockingExecution()) {
		// Keep waiting.
		return;
	}

	if (readyToFinishBoot_) {
		// Finish booting.
		bootComplete();
		readyToFinishBoot_ = false;
		return;
	}

	std::string error_string = "(unknown error)";
	const BootState state = PSP_InitUpdate(&error_string);

	if (state == BootState::Off && screenManager()->topScreen() != this) {
		// Don't kick off a new boot if we're not on top.
		return;
	}

	switch (state) {
	case BootState::Booting:
		// Keep trying.
		return;
	case BootState::Failed:
		// Failure.
		_dbg_assert_(!error_string.empty());
		g_BackgroundAudio.SetGame(Path());
		bootPending_ = false;
		errorMessage_ = error_string;
		ERROR_LOG(Log::Boot, "Boot failed: %s", errorMessage_.c_str());
		return;
	case BootState::Complete:
		// Done booting!
		g_BackgroundAudio.SetGame(Path());
		bootPending_ = false;
		errorMessage_.clear();

		if (PSP_CoreParameter().startBreak) {
			coreState = CORE_STEPPING_CPU;
			System_Notify(SystemNotification::DEBUG_MODE_CHANGE);
		} else {
			coreState = CORE_RUNNING_CPU;
		}

		Achievements::Initialize();

		readyToFinishBoot_ = true;
		return;
	case BootState::Off:
		// Gotta start the boot process! Continue below.
		break;
	}

	SetAssertCancelCallback(&AssertCancelCallback, this);

	if (!g_Config.bShaderCache) {
		// Only developers should ever see this.
		g_OSD.Show(OSDType::MESSAGE_WARNING, "Shader cache is disabled (developer)");
	}

	if (g_Config.bTiltInputEnabled && g_Config.iTiltInputType != 0) {
		auto co = GetI18NCategory(I18NCat::CONTROLS);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions("%1: %2", co->T("Tilt control"), di->T("Enabled")), "", "I_CONTROLLER", 2.5f, "tilt");
	}

	currentMIPS = &mipsr4k;

	CoreParameter coreParam{};
	coreParam.cpuCore = (CPUCore)g_Config.iCpuCore;
	coreParam.gpuCore = GPUCORE_GLES;
	switch (GetGPUBackend()) {
	case GPUBackend::DIRECT3D11:
		coreParam.gpuCore = GPUCORE_DIRECTX11;
		break;
#if !PPSSPP_PLATFORM(UWP)
#if PPSSPP_API(ANY_GL)
	case GPUBackend::OPENGL:
		coreParam.gpuCore = GPUCORE_GLES;
		break;
#endif
	case GPUBackend::VULKAN:
		coreParam.gpuCore = GPUCORE_VULKAN;
		break;
#endif
	}

	// Preserve the existing graphics context.
	coreParam.graphicsContext = PSP_CoreParameter().graphicsContext;
	coreParam.enableSound = g_Config.bEnableSound;
	coreParam.fileToStart = filename;
	coreParam.mountIso.clear();
	coreParam.mountRoot = g_Config.mountRoot;
	coreParam.startBreak = !g_Config.bAutoRun;
	coreParam.headLess = false;

	if (g_Config.iInternalResolution == 0) {
		coreParam.renderWidth = g_display.pixel_xres;
		coreParam.renderHeight = g_display.pixel_yres;
	} else {
		if (g_Config.iInternalResolution < 0)
			g_Config.iInternalResolution = 1;
		coreParam.renderWidth = 480 * g_Config.iInternalResolution;
		coreParam.renderHeight = 272 * g_Config.iInternalResolution;
	}
	coreParam.pixelWidth = g_display.pixel_xres;
	coreParam.pixelHeight = g_display.pixel_yres;

	// PSP_InitStart can't really fail anymore, unless it's called at the wrong time. It just starts the loader thread.
	if (!PSP_InitStart(coreParam)) {
		bootPending_ = false;
		ERROR_LOG(Log::Boot, "InitStart ProcessGameBoot error: %s", errorMessage_.c_str());
		return;
	}

	_dbg_assert_(loadingViewVisible_);
	_dbg_assert_(loadingViewColor_);

	if (loadingViewColor_)
		loadingViewColor_->Divert(0xFFFFFFFF, 0.75f);
	if (loadingViewVisible_)
		loadingViewVisible_->Divert(UI::V_VISIBLE, 0.75f);

	screenManager()->getDrawContext()->ResetStats();

	System_PostUIMessage(UIMessage::GAME_SELECTED, filename.c_str());
}

// Only call this on successful boot.
void EmuScreen::bootComplete() {
	__DisplayListenFlip([](void *userdata) {
		EmuScreen *scr = (EmuScreen *)userdata;
		scr->HandleFlip();
	}, (void *)this);

	// Initialize retroachievements, now that we're on the right thread.
	if (g_Config.bAchievementsEnable) {
		std::string errorString;
		Achievements::SetGame(PSP_CoreParameter().fileToStart, PSP_CoreParameter().fileType, PSP_LoadedFile());
	}

	// We don't want to boot with the wrong game specific config, so wait until info is ready.
	// TODO: Actually, we read this info again during bootup, so this is not really necessary.
	auto sc = GetI18NCategory(I18NCat::SCREEN);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	if (g_paramSFO.IsValid()) {
		g_Discord.SetPresenceGame(SanitizeString(g_paramSFO.GetValueString("TITLE"), StringRestriction::NoLineBreaksOrSpecials));
		std::string gameTitle = SanitizeString(g_paramSFO.GetValueString("TITLE"), StringRestriction::NoLineBreaksOrSpecials, 0, 32);
		std::string id = g_paramSFO.GetValueString("DISC_ID");
		extraAssertInfoStr_ = id + " " + gameTitle;
		SetExtraAssertInfo(extraAssertInfoStr_.c_str());
		SaveState::Rescan(SaveState::GetGamePrefix(g_paramSFO));
	} else {
		g_Discord.SetPresenceGame(sc->T("Untitled PSP game"));
	}

	UpdateUIState(UISTATE_INGAME);
	System_Notify(SystemNotification::BOOT_DONE);
	System_Notify(SystemNotification::DISASSEMBLY);

	NOTICE_LOG(Log::Boot, "Booted %s...", PSP_CoreParameter().fileToStart.c_str());
	if (!Achievements::HardcoreModeActive() && !bootIsReset_) {
		// Don't auto-load savestates in hardcore mode.
		AutoLoadSaveState();
	}

#ifndef MOBILE_DEVICE
	if (g_Config.bFirstRun) {
		g_OSD.Show(OSDType::MESSAGE_INFO, sc->T("PressESC", "Press ESC to open the pause menu"));
	}
#endif

#if !PPSSPP_PLATFORM(UWP)
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		const char *renderer = gl_extensions.model;
		if (strstr(renderer, "Chainfire3D") != 0) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, sc->T("Chainfire3DWarning", "WARNING: Chainfire3D detected, may cause problems"), 10.0f);
		} else if (strstr(renderer, "GLTools") != 0) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, sc->T("GLToolsWarning", "WARNING: GLTools detected, may cause problems"), 10.0f);
		}

		if (g_Config.bGfxDebugOutput) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, "WARNING: GfxDebugOutput is enabled via ppsspp.ini. Things may be slow.", 10.0f);
		}
	}
#endif

	if (Core_GetPowerSaving()) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
#ifdef __ANDROID__
		g_OSD.Show(OSDType::MESSAGE_WARNING, sy->T("WARNING: Android battery save mode is on"), 2.0f, "core_powerSaving");
#else
		g_OSD.Show(OSDType::MESSAGE_WARNING, sy->T("WARNING: Battery save mode is on"), 2.0f, "core_powerSaving");
#endif
	}

	if (g_Config.bStereoRendering) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		// Stereo rendering is experimental, so let's notify the user it's being used.
		// Carefully reuse translations for this rare warning.
		g_OSD.Show(OSDType::MESSAGE_WARNING, std::string(gr->T("Stereo rendering")) + ": " + std::string(di->T("Enabled")));
	}

	saveStateSlot_ = SaveState::GetCurrentSlot();

	if (loadingViewColor_)
		loadingViewColor_->Divert(0x00FFFFFF, 0.2f);
	if (loadingViewVisible_)
		loadingViewVisible_->Divert(UI::V_INVISIBLE, 0.2f);

	std::string gameID = g_paramSFO.GetValueString("DISC_ID");
	g_Config.TimeTracker().Start(gameID);

	bootIsReset_ = false;

	// Reset views in case controls are in a different place.
	RecreateViews();
}

EmuScreen::~EmuScreen() {
	if (imguiInited_) {
		ImGui_ImplThin3d_Shutdown();
		ImGui::DestroyContext(ctx_);
	}

	std::string gameID = g_paramSFO.GetValueString("DISC_ID");
	g_Config.TimeTracker().Stop(gameID);

	// Should not be able to quit during boot, as boot can't be cancelled.
	_dbg_assert_(!bootPending_);
	if (!bootPending_) {
		Achievements::UnloadGame();
		PSP_Shutdown(true);
	}

	// If achievements are disabled in the global config, let's shut it down here.
	if (!g_Config.bAchievementsEnable) {
		Achievements::Shutdown();
	}

	_dbg_assert_(coreState == CORE_POWERDOWN);

	System_PostUIMessage(UIMessage::GAME_SELECTED, "");

	g_OSD.ClearAchievementStuff();

	SetExtraAssertInfo(nullptr);
	SetAssertCancelCallback(nullptr, nullptr);

	g_logManager.DisableOutput(LogOutput::RingBuffer);

#ifndef MOBILE_DEVICE
	if (g_Config.bDumpFrames && startDumping_)
	{
		avi.Stop();
		g_OSD.Show(OSDType::MESSAGE_INFO, "AVI Dump stopped.", 2.0f);
		startDumping_ = false;
	}
#endif

	if (GetUIState() == UISTATE_EXIT)
		g_Discord.ClearPresence();
	else
		g_Discord.SetPresenceMenu();
}

void EmuScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	std::string_view tag = dialog->tag();
	if (tag == "TextEditPopup") {
		// Chat message finished.
		return;
	}

	// Returning to the PauseScreen, unless we're stepping, means we should go back to controls.
	if (Core_IsActive()) {
		UI::EnableFocusMovement(false);
	}

	// TODO: improve the way with which we got commands from PauseMenu.
	// DR_CANCEL/DR_BACK means clicked on "continue", DR_OK means clicked on "back to menu",
	// DR_YES means a message sent to PauseMenu by System_PostUIMessage.
	if ((result == DR_OK || quit_) && !bootPending_) {
		screenManager()->switchScreen(new MainScreen());
		quit_ = false;
	} else {
		RecreateViews();
	}

	SetExtraAssertInfo(extraAssertInfoStr_.c_str());

	// Make sure we re-enable keyboard mode if it was disabled by the dialog, and if needed.
	lastImguiEnabled_ = false;
}

static void AfterSaveStateAction(SaveState::Status status, std::string_view message) {
	if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
		g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR, message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
	}
}

void EmuScreen::focusChanged(ScreenFocusChange focusChange) {
	Screen::focusChanged(focusChange);

	std::string gameID = g_paramSFO.GetValueString("DISC_ID");
	if (gameID.empty()) {
		// startup or shutdown
		return;
	}
	switch (focusChange) {
	case ScreenFocusChange::FOCUS_LOST_TOP:
		g_Config.TimeTracker().Stop(gameID);
		controlMapper_.ReleaseAll();
		break;
	case ScreenFocusChange::FOCUS_BECAME_TOP:
		g_Config.TimeTracker().Start(gameID);
		break;
	}
}

void EmuScreen::sendMessage(UIMessage message, const char *value) {
	// External commands, like from the Windows UI.
	// This happens on the main thread.
	if (message == UIMessage::REQUEST_GAME_PAUSE && screenManager()->topScreen() == this) {
		screenManager()->push(new GamePauseScreen(gamePath_, bootPending_));
	} else if (message == UIMessage::REQUEST_GAME_STOP) {
		// We will push MainScreen in update().
		if (bootPending_) {
			WARN_LOG(Log::Loader, "Can't stop during a pending boot");
			return;
		}
		// The destructor will take care of shutting down.
		screenManager()->switchScreen(new MainScreen());
	} else if (message == UIMessage::REQUEST_GAME_RESET) {
		if (bootPending_) {
			WARN_LOG(Log::Loader, "Can't reset during a pending boot");
			return;
		}
		Achievements::UnloadGame();
		PSP_Shutdown(true);

		// Restart the boot process
		bootPending_ = true;
		bootIsReset_ = true;
		RecreateViews();
		_dbg_assert_(coreState == CORE_POWERDOWN);
		if (!PSP_InitStart(PSP_CoreParameter())) {
			bootPending_ = false;
			WARN_LOG(Log::Loader, "Error resetting");
			screenManager()->switchScreen(new MainScreen());
			return;
		}
	} else if (message == UIMessage::REQUEST_GAME_BOOT) {
		INFO_LOG(Log::Loader, "EmuScreen received REQUEST_GAME_BOOT: %s", value);

		if (bootPending_) {
			ERROR_LOG(Log::Loader, "Can't boot a new game during a pending boot");
			return;
		}
		// TODO: Ignore or not if it's the same game that's already running?
		if (gamePath_ == Path(value)) {
			WARN_LOG(Log::Loader, "Game already running, ignoring");
			return;
		}

		// TODO: Create a path first and
		Path newGamePath(value);

		if (newGamePath.GetFileExtension() == ".ppst") {
			// TODO: Should verify that it's for the correct game....
			INFO_LOG(Log::Loader, "New game is a save state - just load it.");
			SaveState::Load(newGamePath, -1, [](SaveState::Status status, std::string_view message) {
				Core_Resume();
				System_Notify(SystemNotification::DISASSEMBLY);
			});
		} else {
			Achievements::UnloadGame();
			PSP_Shutdown(true);

			// OK, now pop any open settings screens and stuff that are running above us.
			// Otherwise, we can get strange results with game-specific settings.
			screenManager()->cancelScreensAbove(this);

			bootPending_ = true;
			bootIsReset_ = false;
			gamePath_ = newGamePath;
		}
	} else if (message == UIMessage::CONFIG_LOADED) {
		// In case we need to position touch controls differently.
		RecreateViews();
	} else if (message == UIMessage::SHOW_CONTROL_MAPPING && screenManager()->topScreen() == this) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new ControlMappingScreen(gamePath_));
	} else if (message == UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR && screenManager()->topScreen() == this) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new DisplayLayoutScreen(gamePath_));
	} else if (message == UIMessage::SHOW_SETTINGS && screenManager()->topScreen() == this) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else if (message == UIMessage::REQUEST_GPU_DUMP_NEXT_FRAME) {
		if (gpu)
			gpu->DumpNextFrame();
	} else if (message == UIMessage::REQUEST_CLEAR_JIT) {
		if (!bootPending_) {
			currentMIPS->ClearJitCache();
			if (PSP_IsInited()) {
				currentMIPS->UpdateCore((CPUCore)g_Config.iCpuCore);
			}
		}
	} else if (message == UIMessage::WINDOW_MINIMIZED) {
		if (!strcmp(value, "true")) {
			gstate_c.skipDrawReason |= SKIPDRAW_WINDOW_MINIMIZED;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_WINDOW_MINIMIZED;
		}
	} else if (message == UIMessage::SHOW_CHAT_SCREEN) {
		if (g_Config.bEnableNetworkChat && !g_Config.bShowImDebugger) {
			if (!chatButton_)
				RecreateViews();

			if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
				// temporary workaround for hotkey its freeze the ui when open chat screen using hotkey and native keyboard is enable
				if (g_Config.bBypassOSKWithKeyboard) {
					// TODO: Make translatable.
					g_OSD.Show(OSDType::MESSAGE_INFO, "Disable \"Use system native keyboard\" to use ctrl + c hotkey", 2.0f);
				} else {
					UI::EventParams e{};
					OnChatMenu.Trigger(e);
				}
			} else {
				UI::EventParams e{};
				OnChatMenu.Trigger(e);
			}
		} else if (!g_Config.bEnableNetworkChat) {
			if (chatButton_) {
				RecreateViews();
			}
		}
	} else if (message == UIMessage::APP_RESUMED && screenManager()->topScreen() == this) {
		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
			if (!KeyMap::IsKeyMapped(DEVICE_ID_PAD_0, VIRTKEY_PAUSE) || !KeyMap::IsKeyMapped(DEVICE_ID_PAD_1, VIRTKEY_PAUSE)) {
				// If it's a TV (so no built-in back button), and there's no back button mapped to a pad,
				// use this as the fallback way to get into the menu.
				// Don't do it on the first resume though, in case we launch directly into emuscreen, like from a frontend - see #18926
				if (!equals(value, "first")) {
					screenManager()->push(new GamePauseScreen(gamePath_, bootPending_));
				}
			}
		}
	} else if (message == UIMessage::REQUEST_PLAY_SOUND) {
		if (g_Config.bAchievementsSoundEffects && g_Config.bEnableSound) {
			float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
			// TODO: Handle this some nicer way.
			if (!strcmp(value, "achievement_unlocked")) {
				g_BackgroundAudio.SFX().Play(UI::UISound::ACHIEVEMENT_UNLOCKED, achievementVolume);
			}
			if (!strcmp(value, "leaderboard_submitted")) {
				g_BackgroundAudio.SFX().Play(UI::UISound::LEADERBOARD_SUBMITTED, achievementVolume);
			}
		}
	}
}

bool EmuScreen::UnsyncTouch(const TouchInput &touch) {
	System_Notify(SystemNotification::ACTIVITY);

	bool ignoreGamepad = false;

	if (chatMenu_ && chatMenu_->GetVisibility() == UI::V_VISIBLE) {
		// Avoid pressing touch button behind the chat
		if (chatMenu_->Contains(touch.x, touch.y)) {
			ignoreGamepad = true;
		}
	}

	if (touch.flags & TouchInputFlags::DOWN) {
		if (!(g_Config.bShowImDebugger && imguiInited_) && !ignoreGamepad) {
			// This just prevents the gamepad from timing out.
			GamepadTouch();
		}
	}

	if (root_) {
		UIScreen::UnsyncTouch(touch);
	}
	return true;
}

// TODO: We should replace the "fpsLimit" system with a speed factor.
static void ShowFpsLimitNotice() {
	int fpsLimit = 60;

	switch (PSP_CoreParameter().fpsLimit) {
	case FPSLimit::CUSTOM1:
		fpsLimit = g_Config.iFpsLimit1;
		break;
	case FPSLimit::CUSTOM2:
		fpsLimit = g_Config.iFpsLimit2;
		break;
	default:
		break;
	}

	// Now display it.

	char temp[51];
	snprintf(temp, sizeof(temp), "%d%%", (int)((float)fpsLimit / 60.0f * 100.0f));
	g_OSD.Show(OSDType::STATUS_ICON, temp, "", "I_FAST_FORWARD", 1.5f, "altspeed");
	g_OSD.SetFlags("altspeed", OSDMessageFlags::Transparent);
}

void EmuScreen::onVKey(VirtKey virtualKeyCode, bool down) {
	auto sc = GetI18NCategory(I18NCat::SCREEN);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	switch (virtualKeyCode) {
	case VIRTKEY_TOGGLE_DEBUGGER:
		if (down) {
			g_Config.bShowImDebugger = !g_Config.bShowImDebugger;
		}
		break;
	case VIRTKEY_TOGGLE_TILT:
		if (down) {
			g_Config.bTiltInputEnabled = !g_Config.bTiltInputEnabled;
			if (!g_Config.bTiltInputEnabled) {
				// Reset whatever got tilted.
				switch (g_Config.iTiltInputType) {
				case TILT_ANALOG:
					__CtrlSetAnalogXY(0, 0, 0);
					break;
				case TILT_ACTION_BUTTON:
					__CtrlUpdateButtons(0, CTRL_CROSS | CTRL_CIRCLE | CTRL_SQUARE | CTRL_TRIANGLE);
					break;
				case TILT_DPAD:
					__CtrlUpdateButtons(0, CTRL_UP | CTRL_DOWN | CTRL_LEFT | CTRL_RIGHT);
					break;
				case TILT_TRIGGER_BUTTONS:
					__CtrlUpdateButtons(0, CTRL_LTRIGGER | CTRL_RTRIGGER);
					break;
				}
			}
		}
		break;
	case VIRTKEY_FASTFORWARD:
		if (down && !NetworkWarnUserIfOnlineAndCantSpeed() && !bootPending_) {
			/*
			// This seems like strange behavior. Commented it out.
			if (coreState == CORE_STEPPING_CPU) {
				Core_Resume();
			}
			*/
			PSP_CoreParameter().fastForward = true;
		} else {
			PSP_CoreParameter().fastForward = false;
		}
		break;

	case VIRTKEY_SPEED_TOGGLE:
		if (down && !NetworkWarnUserIfOnlineAndCantSpeed()) {
			// Cycle through enabled speeds.
			if (PSP_CoreParameter().fpsLimit == FPSLimit::NORMAL && g_Config.iFpsLimit1 >= 0) {
				PSP_CoreParameter().fpsLimit = FPSLimit::CUSTOM1;
			} else if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM1 && g_Config.iFpsLimit2 >= 0) {
				PSP_CoreParameter().fpsLimit = FPSLimit::CUSTOM2;
			} else if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM1 || PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM2) {
				PSP_CoreParameter().fpsLimit = FPSLimit::NORMAL;
			}

			ShowFpsLimitNotice();
		}
		break;

	case VIRTKEY_SPEED_CUSTOM1:
		if (down && !NetworkWarnUserIfOnlineAndCantSpeed()) {
			if (PSP_CoreParameter().fpsLimit == FPSLimit::NORMAL) {
				PSP_CoreParameter().fpsLimit = FPSLimit::CUSTOM1;
				ShowFpsLimitNotice();
			}
		} else {
			if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM1) {
				PSP_CoreParameter().fpsLimit = FPSLimit::NORMAL;
				ShowFpsLimitNotice();
			}
		}
		break;
	case VIRTKEY_SPEED_CUSTOM2:
		if (down && !NetworkWarnUserIfOnlineAndCantSpeed()) {
			if (PSP_CoreParameter().fpsLimit == FPSLimit::NORMAL) {
				PSP_CoreParameter().fpsLimit = FPSLimit::CUSTOM2;
				ShowFpsLimitNotice();
			}
		} else {
			if (PSP_CoreParameter().fpsLimit == FPSLimit::CUSTOM2) {
				PSP_CoreParameter().fpsLimit = FPSLimit::NORMAL;
				ShowFpsLimitNotice();
			}
		}
		break;

	case VIRTKEY_PAUSE:
		if (down) {
			// Note: We don't check NetworkWarnUserIfOnlineAndCantSpeed, because we can keep
			// running in the background of the menu.
			pauseTrigger_ = true;
			controlMapper_.ForceReleaseVKey(virtualKeyCode);
		}
		break;

	case VIRTKEY_RESET_EMULATION:
		if (down) {
			System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
		}
		break;

#ifndef MOBILE_DEVICE
	case VIRTKEY_RECORD:
		if (down) {
			if (g_Config.bDumpFrames == g_Config.bDumpAudio) {
				g_Config.bDumpFrames = !g_Config.bDumpFrames;
				g_Config.bDumpAudio = !g_Config.bDumpAudio;
			} else {
				// This hotkey should always toggle both audio and video together.
				// So let's make sure that's the only outcome even if video OR audio was already being dumped.
				if (g_Config.bDumpFrames) {
					AVIDump::Stop();
					AVIDump::Start(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
					g_Config.bDumpAudio = true;
				} else {
					WAVDump::Reset();
					g_Config.bDumpFrames = true;
				}
			}
		}
		break;
#endif

	case VIRTKEY_SAVE_STATE:
		if (down && !Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate() && !bootPending_) {
			SaveState::SaveSlot(SaveState::GetGamePrefix(g_paramSFO), g_Config.iCurrentStateSlot, &AfterSaveStateAction);
		}
		break;
	case VIRTKEY_LOAD_STATE:
		if (down && !Achievements::WarnUserIfHardcoreModeActive(false) && !NetworkWarnUserIfOnlineAndCantSavestate() && !bootPending_) {
			SaveState::LoadSlot(SaveState::GetGamePrefix(g_paramSFO), g_Config.iCurrentStateSlot, &AfterSaveStateAction);
		}
		break;
	case VIRTKEY_PREVIOUS_SLOT:
		if (down && !Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
			SaveState::PrevSlot();
			System_PostUIMessage(UIMessage::SAVESTATE_DISPLAY_SLOT);
		}
		break;
	case VIRTKEY_NEXT_SLOT:
		if (down && !Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
			SaveState::NextSlot();
			System_PostUIMessage(UIMessage::SAVESTATE_DISPLAY_SLOT);
		}
		break;
	case VIRTKEY_SCREENSHOT:
		if (down)
			g_TakeScreenshot = true;
		break;
	case VIRTKEY_RAPID_FIRE:
		__CtrlSetRapidFire(down, g_Config.iRapidFireInterval);
		break;

	case VIRTKEY_SWAP_LAYOUT:
		if (down) {
			const DeviceOrientation orientation = GetDeviceOrientation();
			g_Config.SwapTouchControlsLayouts(orientation);
			
			auto co = GetI18NCategory(I18NCat::CONTROLS);
			g_OSD.Show(OSDType::MESSAGE_INFO, co->T("Touch layout switched"));
		
			// Reset all touch input to prevent buttons stuck in pressed state
			TouchInput releaseAll{};
			releaseAll.flags = TouchInputFlags::RELEASE_ALL;
			if (root_) root_->Touch(releaseAll);
			
			// Post async message to recreate UI views in UI thread instead of from emulation context
			System_PostUIMessage(UIMessage::RECREATE_VIEWS);
		}
		break;

	default:
		// To make sure we're not in an async context.
		if (down) {
			queuedVirtKeys_.push_back(virtualKeyCode);
		}
		break;
	}
}

void EmuScreen::ProcessQueuedVKeys() {
	for (auto iter : queuedVirtKeys_) {
		ProcessVKey(iter);
	}
	queuedVirtKeys_.clear();
}

void EmuScreen::ProcessVKey(VirtKey virtKey) {
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
	auto sc = GetI18NCategory(I18NCat::SCREEN);

	switch (virtKey) {
	case VIRTKEY_OPENCHAT:
		if (g_Config.bEnableNetworkChat && !g_Config.bShowImDebugger) {
			UI::EventParams e{};
			OnChatMenu.Trigger(e);
			controlMapper_.ForceReleaseVKey(VIRTKEY_OPENCHAT);
		}
		break;

	case VIRTKEY_AXIS_SWAP:
		controlMapper_.ToggleSwapAxes();
		g_OSD.Show(OSDType::MESSAGE_INFO, mc->T("AxisSwap"));  // best string we have.
		break;

	case VIRTKEY_DEVMENU:
		{
			UI::EventParams e{};
			OnDevMenu.Trigger(e);
		}
		break;

	case VIRTKEY_TOGGLE_MOUSE:
		g_Config.bMouseControl = !g_Config.bMouseControl;
		break;

	case VIRTKEY_TEXTURE_DUMP:
		g_Config.bSaveNewTextures = !g_Config.bSaveNewTextures;
		if (g_Config.bSaveNewTextures) {
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, sc->T("saveNewTextures_true", "Textures will now be saved to your storage"), 2.0, "savetexturechanged");
		} else {
			g_OSD.Show(OSDType::MESSAGE_INFO, sc->T("saveNewTextures_false", "Texture saving was disabled"), 2.0, "savetexturechanged");
		}
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
		break;

	case VIRTKEY_TEXTURE_REPLACE:
		g_Config.bReplaceTextures = !g_Config.bReplaceTextures;
		if (g_Config.bReplaceTextures) {
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, sc->T("replaceTextures_true", "Texture replacement enabled"), 2.0, "replacetexturechanged");
		} else {
			g_OSD.Show(OSDType::MESSAGE_INFO, sc->T("replaceTextures_false", "Textures are no longer being replaced"), 2.0, "replacetexturechanged");
		}
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
		break;

	case VIRTKEY_MUTE_TOGGLE:
		g_Config.bEnableSound = !g_Config.bEnableSound;
		break;

	case VIRTKEY_SCREEN_ROTATION_VERTICAL:
	{
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		config.iInternalScreenRotation = ROTATION_LOCKED_VERTICAL;
		break;
	}
	case VIRTKEY_SCREEN_ROTATION_VERTICAL180:
	{
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		config.iInternalScreenRotation = ROTATION_LOCKED_VERTICAL180;
		break;
	}
	case VIRTKEY_SCREEN_ROTATION_HORIZONTAL:
	{
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		config.iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL;
		break;
	}
	case VIRTKEY_SCREEN_ROTATION_HORIZONTAL180:
	{
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
		config.iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL180;
		break;
	}

	case VIRTKEY_TOGGLE_WLAN:
		// Let's not allow the user to toggle wlan while connected, could get confusing.
		if (!g_netInited) {
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			auto di = GetI18NCategory(I18NCat::DIALOG);
			g_Config.bEnableWlan = !g_Config.bEnableWlan;
			// Try to avoid adding more strings so we piece together a message from existing ones.
			g_OSD.Show(OSDType::MESSAGE_INFO, StringFromFormat(
				"%s: %s", n->T_cstr("Enable networking"), g_Config.bEnableWlan ? di->T_cstr("Enabled") : di->T_cstr("Disabled")), 2.0, "toggle_wlan");
		}
		break;

	case VIRTKEY_TOGGLE_FULLSCREEN:
		// TODO: Limit to platforms that can support fullscreen.
		g_Config.bFullScreen = !g_Config.bFullScreen;
		System_ApplyFullscreenState();
		break;

	case VIRTKEY_TOGGLE_TOUCH_CONTROLS:
		if (g_Config.bShowTouchControls) {
			// This just messes with opacity if enabled, so you can touch the screen again to bring them back.
			if (GamepadGetOpacity() < 0.01f) {
				GamepadTouch();
			} else {
				GamepadResetTouch();
			}
		} else {
			// If touch controls are disabled though, they'll get enabled.
			g_Config.bShowTouchControls = true;
			RecreateViews();
			GamepadTouch();
		}
		break;

	case VIRTKEY_REWIND:
		if (!Achievements::WarnUserIfHardcoreModeActive(false) && !NetworkWarnUserIfOnlineAndCantSavestate() && !bootPending_) {
			if (SaveState::CanRewind()) {
				SaveState::Rewind(&AfterSaveStateAction);
			} else {
				g_OSD.Show(OSDType::MESSAGE_WARNING, sc->T("norewind", "No rewind save states available"), 2.0);
			}
		}
		break;

	case VIRTKEY_PAUSE_NO_MENU:
		if (!NetworkWarnUserIfOnlineAndCantSpeed()) {
			// We re-use debug break/resume to implement pause/resume without a menu.
			if (coreState == CORE_STEPPING_CPU) {  // should we check reason?
				Core_Resume();
			} else {
				Core_Break(BreakReason::UIPause);
			}
		}
		break;

	case VIRTKEY_EXIT_APP:
	{
		if (!bootPending_) {
			std::string confirmExitMessage = GetConfirmExitMessage();
			if (!confirmExitMessage.empty()) {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				auto mm = GetI18NCategory(I18NCat::MAINMENU);
				confirmExitMessage += '\n';
				confirmExitMessage += di->T("Are you sure you want to exit?");
				screenManager()->push(new UI::MessagePopupScreen(mm->T("Exit"), confirmExitMessage, di->T("Yes"), di->T("No"), [=](bool result) {
					if (result) {
						System_ExitApp();
					}
				}));
			} else {
				System_ExitApp();
			}
		}
		break;
	}

	case VIRTKEY_FRAME_ADVANCE:
		// Can't do this reliably in an async fashion, so we just set a variable.
		// Is this used by anyone? There's no user-friendly way to resume, other than PAUSE_NO_MENU or the debugger.
		if (!NetworkWarnUserIfOnlineAndCantSpeed()) {
			if (Core_IsStepping()) {
				Core_Resume();
				frameStep_ = true;
			} else {
				Core_Break(BreakReason::FrameAdvance);
			}
		}
		break;

	default:
		break;
	}
}

void EmuScreen::onVKeyAnalog(VirtKey virtualKeyCode, float value) {
	if (virtualKeyCode != VIRTKEY_SPEED_ANALOG) {
		return;
	}

	// We only handle VIRTKEY_SPEED_ANALOG here.

	// Xbox controllers need a pretty big deadzone here to not leave behind small values
	// on occasion when releasing the trigger. Still feels right.
	static constexpr float DEADZONE_THRESHOLD = 0.2f;
	static constexpr float DEADZONE_SCALE = 1.0f / (1.0f - DEADZONE_THRESHOLD);

	FPSLimit &limitMode = PSP_CoreParameter().fpsLimit;
	// If we're using an alternate speed already, let that win.
	if (limitMode != FPSLimit::NORMAL && limitMode != FPSLimit::ANALOG)
		return;
	// Don't even try if the limit is invalid.
	if (g_Config.iAnalogFpsLimit <= 0)
		return;

	// Apply a small deadzone (against the resting position.)
	value = std::max(0.0f, (value - DEADZONE_THRESHOLD) * DEADZONE_SCALE);

	// If target is above 60, value is how much to speed up over 60.  Otherwise, it's how much slower.
	// So normalize the target.
	int target = g_Config.iAnalogFpsLimit - 60;
	PSP_CoreParameter().analogFpsLimit = 60 + (int)(target * value);

	// If we've reset back to normal, turn it off.
	limitMode = PSP_CoreParameter().analogFpsLimit == 60 ? FPSLimit::NORMAL : FPSLimit::ANALOG;
}

bool EmuScreen::UnsyncKey(const KeyInput &key) {
	System_Notify(SystemNotification::ACTIVITY);

	// Update imgui modifier flags
	if (key.flags & (KeyInputFlags::DOWN | KeyInputFlags::UP)) {
		bool down = (key.flags & KeyInputFlags::DOWN) != 0;
		switch (key.keyCode) {
		case NKCODE_CTRL_LEFT: keyCtrlLeft_ = down; break;
		case NKCODE_CTRL_RIGHT: keyCtrlRight_ = down; break;
		case NKCODE_SHIFT_LEFT: keyShiftLeft_ = down; break;
		case NKCODE_SHIFT_RIGHT: keyShiftRight_ = down; break;
		case NKCODE_ALT_LEFT: keyAltLeft_ = down; break;
		case NKCODE_ALT_RIGHT: keyAltRight_ = down; break;
		default: break;
		}
	}

	const bool chatMenuOpen = chatMenu_ && chatMenu_->GetVisibility() == UI::V_VISIBLE;

	if (chatMenuOpen || (g_Config.bShowImDebugger && imguiInited_)) {
		// Note: Allow some Vkeys through, so we can toggle the imgui for example (since we actually block the control mapper otherwise in imgui mode).
		// We need to manually implement it here :/
		if (g_Config.bShowImDebugger && imguiInited_) {
			if (key.flags & (KeyInputFlags::UP | KeyInputFlags::DOWN)) {
				InputMapping mapping(key.deviceId, key.keyCode);
				std::vector<int> pspButtons;
				bool mappingFound = KeyMap::InputMappingToPspButton(mapping, &pspButtons);
				if (mappingFound) {
					for (auto b : pspButtons) {
						if (b == VIRTKEY_TOGGLE_DEBUGGER || b == VIRTKEY_PAUSE) {
							return controlMapper_.Key(key, &pauseTrigger_);
						}
					}
				}
			}
			UI::EnableFocusMovement(false);
			// Enable gamepad controls while running imgui (but ignore mouse/keyboard).
			switch (key.deviceId) {
			case DEVICE_ID_KEYBOARD:
				if (!ImGui::GetIO().WantCaptureKeyboard) {
					controlMapper_.Key(key, &pauseTrigger_);
				}
				break;
			case DEVICE_ID_MOUSE:
				if (!ImGui::GetIO().WantCaptureMouse) {
					controlMapper_.Key(key, &pauseTrigger_);
				}
				break;
			default:
				controlMapper_.Key(key, &pauseTrigger_);
				break;
			}
		} else {
			// Let up-events through to the controlMapper_ so input doesn't get stuck.
			if (key.flags & KeyInputFlags::UP) {
				controlMapper_.Key(key, &pauseTrigger_);
			}
		}

		return UIScreen::UnsyncKey(key);
	}
	return controlMapper_.Key(key, &pauseTrigger_);
}

void EmuScreen::UnsyncAxis(const AxisInput *axes, size_t count) {
	System_Notify(SystemNotification::ACTIVITY);

	if (UI::IsFocusMovementEnabled()) {
		return UIScreen::UnsyncAxis(axes, count);
	}

	return controlMapper_.Axis(axes, count);
}

bool EmuScreen::key(const KeyInput &key) {
	bool retval = UIScreen::key(key);

	if (!retval && g_Config.bShowImDebugger && imguiInited_) {
		ImGui_ImplPlatform_KeyEvent(key);
	}

	if (!retval && (key.flags & KeyInputFlags::DOWN) != 0 && UI::IsEscapeKey(key)) {
		if (chatMenu_)
			chatMenu_->Close();
		if (chatButton_)
			chatButton_->SetVisibility(UI::V_VISIBLE);
		UI::EnableFocusMovement(false);
		return true;
	}

	return retval;
}

void EmuScreen::touch(const TouchInput &touch) {
	if (g_Config.bShowImDebugger && imguiInited_) {
		ImGui_ImplPlatform_TouchEvent(touch);
		if (!ImGui::GetIO().WantCaptureMouse) {
			UIScreen::touch(touch);
		}
	} else if (g_Config.bMouseControl && !(touch.flags & TouchInputFlags::UP) && (touch.flags & TouchInputFlags::MOUSE)) {
		// don't do anything as the mouse pointer is hidden in this case.
		// But we let touch-up events through to avoid getting stuck if the user toggles mouse control.
	} else {
		// Handle closing the chat menu if touched outside it.
		if (chatMenu_ && chatMenu_->GetVisibility() == UI::V_VISIBLE) {
			// Avoid pressing touch button behind the chat
			if (!chatMenu_->Contains(touch.x, touch.y)) {
				if ((touch.flags & TouchInputFlags::DOWN) != 0) {
					chatMenu_->Close();
					if (chatButton_)
						chatButton_->SetVisibility(UI::V_VISIBLE);
					UI::EnableFocusMovement(false);
				}
			}
		}
		UIScreen::touch(touch);
	}
}

class GameInfoBGView : public UI::InertView {
public:
	GameInfoBGView(const Path &gamePath, UI::LayoutParams *layoutParams) : InertView(layoutParams), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override {
		// Should only be called when visible.
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, GameInfoFlags::PIC1);
		dc.Flush();

		// PIC1 is the loading image, so let's only draw if it's available.
		if (ginfo->Ready(GameInfoFlags::PIC1) && ginfo->pic1.texture) {
			Draw::Texture *texture = ginfo->pic1.texture;
			if (texture) {
				const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
				// Similar to presentation, we want to put the game PIC1 in the same region of the screen.
				FRect frame = GetScreenFrame(config.bIgnoreScreenInsets, g_display.pixel_xres, g_display.pixel_yres);
				FRect rc;
				CalculateDisplayOutputRect(config, &rc, texture->Width(), texture->Height(), frame, config.iInternalScreenRotation);

				// Need to adjust for DPI here since we're still in the UI coordinate space here, not the pixel coordinate space used for in-game presentation.
				Bounds bounds(rc.x * g_display.dpi_scale_x, rc.y * g_display.dpi_scale_y, rc.w * g_display.dpi_scale_x, rc.h * g_display.dpi_scale_y);

				dc.GetDrawContext()->BindTexture(0, texture);

				double loadTime = ginfo->pic1.timeLoaded;
				uint32_t color = alphaMul(color_, ease((time_now_d() - loadTime) * 3));
				dc.Draw()->DrawTexRect(bounds, 0, 0, 1, 1, color);
				dc.Flush();
				dc.RebindTexture();
			}
		}
	}

	std::string DescribeText() const override {
		return "";
	}

	void SetColor(uint32_t c) {
		color_ = c;
	}

protected:
	Path gamePath_;
	uint32_t color_ = 0xFFC0C0C0;
};

// TODO: Shouldn't actually need bounds for this, Anchor can center too.
static UI::AnchorLayoutParams *AnchorInCorner(const Bounds &bounds, int corner, float xOffset, float yOffset) {
	using namespace UI;
	switch ((ScreenEdgePosition)g_Config.iChatButtonPosition) {
	case ScreenEdgePosition::BOTTOM_LEFT:   return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, xOffset, NONE, NONE, yOffset, Centering::Both);
	case ScreenEdgePosition::BOTTOM_CENTER: return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, bounds.centerX(), NONE, NONE, yOffset, Centering::Both);
	case ScreenEdgePosition::BOTTOM_RIGHT:  return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, NONE, NONE, xOffset, yOffset, Centering::Both);
	case ScreenEdgePosition::TOP_LEFT:      return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, xOffset, yOffset, NONE, NONE, Centering::Both);
	case ScreenEdgePosition::TOP_CENTER:    return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, bounds.centerX(), yOffset, NONE, NONE, Centering::Both);
	case ScreenEdgePosition::TOP_RIGHT:     return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, NONE, yOffset, xOffset, NONE, Centering::Both);
	case ScreenEdgePosition::CENTER_LEFT:   return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, xOffset, bounds.centerY(), NONE, NONE, Centering::Both);
	case ScreenEdgePosition::CENTER_RIGHT:  return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, NONE, bounds.centerY(), xOffset, NONE, Centering::Both);
	default: return new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, xOffset, NONE, NONE, yOffset, Centering::Both);
	}
}

void EmuScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sc = GetI18NCategory(I18NCat::SCREEN);

	const DeviceOrientation deviceOrientation = GetDeviceOrientation();

	TouchControlConfig &touch = g_Config.GetCurrentTouchControlsConfig(deviceOrientation);

	const Bounds &bounds = screenManager()->getUIContext()->GetLayoutBounds();
	InitPadLayout(&touch, deviceOrientation, bounds.w, bounds.h);

	root_ = CreatePadLayout(touch, bounds.w, bounds.h, &pauseTrigger_, &controlMapper_);
	if (g_Config.bShowDeveloperMenu) {
		root_->Add(new Button(dev->T("DevMenu")))->OnClick.Handle(this, &EmuScreen::OnDevTools);
	}

	LinearLayout *buttons = new LinearLayout(Orientation::ORIENT_HORIZONTAL, new AnchorLayoutParams(bounds.centerX(), NONE, NONE, 60, Centering::Both));
	buttons->SetSpacing(20.0f);
	root_->Add(buttons);

	resumeButton_ = buttons->Add(new Button(dev->T("Resume")));
	resumeButton_->OnClick.Add([](UI::EventParams &) {
		if (coreState == CoreState::CORE_RUNTIME_ERROR) {
			// Force it!
			Memory::MemFault_IgnoreLastCrash();
			coreState = CoreState::CORE_RUNNING_CPU;
		}
	});
	resumeButton_->SetVisibility(V_GONE);

	resetButton_ = buttons->Add(new Button(di->T("Reset")));
	resetButton_->OnClick.Add([](UI::EventParams &) {
		if (coreState == CoreState::CORE_RUNTIME_ERROR) {
			System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
		}
	});
	resetButton_->SetVisibility(V_GONE);

	backButton_ = buttons->Add(new Button(di->T("Back")));
	backButton_->OnClick.Add([this](UI::EventParams &) {
		this->pauseTrigger_ = true;
	});
	backButton_->SetVisibility(V_GONE);

	cardboardDisableButton_ = root_->Add(new Button(sc->T("Cardboard VR OFF"), new AnchorLayoutParams(bounds.centerX(), NONE, NONE, 30, Centering::Both)));
	DeviceOrientation orientation = GetDeviceOrientation();
	cardboardDisableButton_->OnClick.Add([deviceOrientation](UI::EventParams &) {
		DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(deviceOrientation);
		config.bEnableCardboardVR = false;
	});
	cardboardDisableButton_->SetVisibility(V_GONE);
	cardboardDisableButton_->SetScale(0.65f);  // make it smaller - this button can be in the way otherwise.

	chatButton_ = nullptr;
	chatMenu_ = nullptr;
	if (g_Config.bEnableNetworkChat) {
		if (g_Config.iChatButtonPosition != 8) {
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			AnchorLayoutParams *layoutParams = AnchorInCorner(bounds, g_Config.iChatButtonPosition, 80.0f, 50.0f);
			ChoiceWithValueDisplay *btn = new ChoiceWithValueDisplay(&newChatMessages_, n->T("Chat"), layoutParams);
			root_->Add(btn)->OnClick.Handle(this, &EmuScreen::OnChat);
			chatButton_ = btn;
		}
		chatMenu_ = root_->Add(new ChatMenu(GetRequesterToken(), screenManager()->getUIContext()->GetBounds(), screenManager(), new LayoutParams(FILL_PARENT, FILL_PARENT)));
		chatMenu_->SetVisibility(UI::V_GONE);
	}

	saveStatePreview_ = new AsyncImageFileView(Path(), IS_FIXED, new AnchorLayoutParams(bounds.centerX(), 100, NONE, NONE, Centering::Both));
	saveStatePreview_->SetFixedSize(160, 90);
	saveStatePreview_->SetColor(0x90FFFFFF);
	saveStatePreview_->SetVisibility(V_GONE);
	saveStatePreview_->SetCanBeFocused(false);
	root_->Add(saveStatePreview_);

	GameInfoBGView *loadingBG = root_->Add(new GameInfoBGView(gamePath_, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT)));

	static const ImageID symbols[4] = {
		ImageID("I_CROSS"),
		ImageID("I_CIRCLE"),
		ImageID("I_SQUARE"),
		ImageID("I_TRIANGLE"),
	};

	Spinner *loadingSpinner = root_->Add(new Spinner(symbols, ARRAY_SIZE(symbols), new AnchorLayoutParams(NONE, NONE, 45, 45, Centering::Both)));
	loadingSpinner_ = loadingSpinner;

	loadingBG->SetTag("LoadingBG");
	loadingSpinner->SetTag("LoadingSpinner");

	loadingViewColor_ = loadingSpinner->AddTween(new CallbackColorTween(0x00FFFFFF, 0x00FFFFFF, 0.2f, &bezierEaseInOut));
	loadingViewColor_->SetCallback([loadingBG, loadingSpinner](View *v, uint32_t c) {
		loadingBG->SetColor(c & 0xFFC0C0C0);
		loadingSpinner->SetColor(alphaMul(c, 0.7f));
	});
	loadingViewColor_->Persist();

	// We start invisible here, in case of recreated views.
	loadingViewVisible_ = loadingSpinner->AddTween(new VisibilityTween(UI::V_INVISIBLE, UI::V_INVISIBLE, 0.2f, &bezierEaseInOut));
	loadingViewVisible_->Persist();
	loadingViewVisible_->Finish.Add([loadingBG, loadingSpinner](EventParams &p) {
		loadingBG->SetVisibility(p.v->GetVisibility());

		// If we just became invisible, flush BGs since we don't need them anymore.
		// Saves some VRAM for the game, but don't do it before we fade out...
		if (p.v->GetVisibility() == V_INVISIBLE) {
			g_gameInfoCache->FlushBGs();
			// And we can go away too.  This means the tween will never run again.
			loadingBG->SetVisibility(V_GONE);
			loadingSpinner->SetVisibility(V_GONE);
		}
	});
	// Will become visible along with the loadingView.
	loadingBG->SetVisibility(V_INVISIBLE);
}

void EmuScreen::deviceLost() {
	UIScreen::deviceLost();

	if (imguiInited_) {
		if (imDebugger_) {
			imDebugger_->DeviceLost();
		}
		ImGui_ImplThin3d_DestroyDeviceObjects();
	}
}

void EmuScreen::deviceRestored(Draw::DrawContext *draw) {
	UIScreen::deviceRestored(draw);
	if (imguiInited_) {
		ImGui_ImplThin3d_CreateDeviceObjects(draw);
	}
}

void EmuScreen::OnDevTools(UI::EventParams &params) {
	DevMenuScreen *devMenu = new DevMenuScreen(gamePath_, I18NCat::DEVELOPER);
	if (params.v)
		devMenu->SetPopupOrigin(params.v);
	screenManager()->push(devMenu);
}

void EmuScreen::OnChat(UI::EventParams &params) {
	if (chatButton_ != nullptr && chatButton_->GetVisibility() == UI::V_VISIBLE) {
		chatButton_->SetVisibility(UI::V_GONE);
	}
	if (chatMenu_ != nullptr) {
		chatMenu_->SetVisibility(UI::V_VISIBLE);

#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(SDL)
		UI::EnableFocusMovement(true);
		root_->SetDefaultFocusView(chatMenu_);

		chatMenu_->SetFocus();
		UI::View *focused = UI::GetFocusedView();
		if (focused) {
			root_->SubviewFocused(focused);
		}
#endif
	}
}

// To avoid including proAdhoc.h, which includes a lot of stuff.
int GetChatMessageCount();

void EmuScreen::update() {
	using namespace UI;

	// This is where views are recreated.
	UIScreen::update();

	resumeButton_->SetVisibility(coreState == CoreState::CORE_RUNTIME_ERROR && Memory::MemFault_MayBeResumable() ? V_VISIBLE : V_GONE);
	resetButton_->SetVisibility(coreState == CoreState::CORE_RUNTIME_ERROR ? V_VISIBLE : V_GONE);
	backButton_->SetVisibility(coreState == CoreState::CORE_RUNTIME_ERROR ? V_VISIBLE : V_GONE);

	if (chatButton_ && chatMenu_) {
		if (chatMenu_->GetVisibility() != V_GONE) {
			chatMessages_ = GetChatMessageCount();
			newChatMessages_ = 0;
		} else {
			int diff = GetChatMessageCount() - chatMessages_;
			// Cap the count at 50.
			newChatMessages_ = diff > 50 ? 50 : diff;
		}
	}

	// Simply forcibly update to the current screen size every frame. Doesn't cost much.
	// If bounds is set to be smaller than the actual pixel resolution of the display, respect that.
	// TODO: Should be able to use g_dpi_scale here instead. Might want to store the dpi scale in the UI context too.

#ifndef _WIN32
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	PSP_CoreParameter().pixelWidth = g_display.pixel_xres * bounds.w / g_display.dp_xres;
	PSP_CoreParameter().pixelHeight = g_display.pixel_yres * bounds.h / g_display.dp_yres;
#endif

	if (PSP_IsInited()) {
		UpdateUIState(coreState != CORE_RUNTIME_ERROR ? UISTATE_INGAME : UISTATE_EXCEPTION);
	}

	if (errorMessage_.size()) {
		auto err = GetI18NCategory(I18NCat::ERRORS);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string errLoadingFile = gamePath_.ToVisualString() + "\n\n";
		errLoadingFile.append(err->T("Error loading file", "Could not load game"));
		errLoadingFile.append("\n");
		errLoadingFile.append(errorMessage_);

		screenManager()->push(new PromptScreen(gamePath_, errLoadingFile, di->T("OK"), ""));
		errorMessage_.clear();
		quit_ = true;
		return;
	}

	if (pauseTrigger_) {
		pauseTrigger_ = false;
		screenManager()->push(new GamePauseScreen(gamePath_, bootPending_));
	}

	if (!PSP_IsInited())
		return;

	double now = time_now_d();

	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	controlMapper_.Update(config, now);

	if (saveStatePreview_ && !bootPending_) {
		int currentSlot = SaveState::GetCurrentSlot();
		if (saveStateSlot_ != currentSlot) {
			saveStateSlot_ = currentSlot;

			const std::string gamePrefix = SaveState::GetGamePrefix(g_paramSFO);

			Path fn;
			if (SaveState::HasSaveInSlot(gamePrefix, currentSlot)) {
				fn = SaveState::GenerateSaveSlotPath(gamePrefix, currentSlot, SaveState::SCREENSHOT_EXTENSION);
			}

			saveStatePreview_->SetFilename(fn);
			if (!fn.empty()) {
				saveStatePreview_->SetVisibility(UI::V_VISIBLE);
				saveStatePreviewShownTime_ = now;
			} else {
				saveStatePreview_->SetVisibility(UI::V_GONE);
			}
		}

		if (saveStatePreview_->GetVisibility() == UI::V_VISIBLE) {
			double endTime = saveStatePreviewShownTime_ + 2.0;
			float alpha = clamp_value((endTime - now) * 4.0, 0.0, 1.0);
			saveStatePreview_->SetColor(colorAlpha(0x00FFFFFF, alpha));

			if (now - saveStatePreviewShownTime_ > 2) {
				saveStatePreview_->SetVisibility(UI::V_GONE);
			}
		}
	}
}

bool EmuScreen::checkPowerDown() {
	// This is for handling things like sceKernelExitGame().
	// Also for REQUEST_STOP.
	if (coreState == CORE_POWERDOWN && PSP_GetBootState() == BootState::Complete && !bootPending_) {
		INFO_LOG(Log::System, "SELF-POWERDOWN!");
		screenManager()->switchScreen(new MainScreen());
		return true;
	}
	return false;
}

ScreenRenderRole EmuScreen::renderRole(bool isTop) const {
	auto CanBeBackground = [&]() -> bool {
		if (g_Config.bSkipBufferEffects) {
			return isTop || (g_Config.bTransparentBackground && ShouldRunBehind());
		}

		if (!g_Config.bTransparentBackground && !isTop) {
			if (ShouldRunBehind() || screenManager()->topScreen()->wantBrightBackground())
				return true;
			return false;
		}

		if (!PSP_IsInited() && !bootPending_) {
			return false;
		}

		return true;
	};

	ScreenRenderRole role = ScreenRenderRole::MUST_BE_FIRST;
	if (CanBeBackground()) {
		role |= ScreenRenderRole::CAN_BE_BACKGROUND;
	}
	return role;
}

void EmuScreen::darken() {
	if (!screenManager()->topScreen()->wantBrightBackground()) {
		UIContext &dc = *screenManager()->getUIContext();
		uint32_t color = GetBackgroundColorWithAlpha(dc);
		dc.Begin();
		dc.RebindTexture();
		dc.FillRect(UI::Drawable(color), dc.GetBounds());
		dc.Flush();
	}
}

void EmuScreen::HandleFlip() {
	Achievements::FrameUpdate();

	// This video dumping stuff is bad. Or at least completely broken with frameskip..
#ifndef MOBILE_DEVICE
	if (g_Config.bDumpFrames && !startDumping_) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		avi.Start(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
		g_OSD.Show(OSDType::MESSAGE_INFO, sy->T("AVI Dump started."), 1.0f);
		startDumping_ = true;
	}
	if (g_Config.bDumpFrames && startDumping_) {
		avi.AddFrame();
	} else if (!g_Config.bDumpFrames && startDumping_) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		avi.Stop();
		g_OSD.Show(OSDType::MESSAGE_INFO, sy->T("AVI Dump stopped."), 3.0f, "avi_dump");
		g_OSD.SetClickCallback("avi_dump", [](bool, void *) {
			System_ShowFileInFolder(avi.LastFilename());
		}, nullptr);
		startDumping_ = false;
	}
#endif
}

ScreenRenderFlags EmuScreen::render(ScreenRenderMode mode) {
	// Moved from update, because we want it to be possible for booting to happen even when the screen
	// is in the background, like when choosing Reset from the pause menu.

	// If a boot is in progress, update it.
	ProcessGameBoot(gamePath_);

	const Draw::Viewport viewport{0.0f, 0.0f, (float)g_display.pixel_xres, (float)g_display.pixel_yres, 0.0f, 1.0f};
	using namespace Draw;

	DrawContext *draw = screenManager()->getDrawContext();
	if (!draw) {
		return ScreenRenderFlags::NONE;  // shouldn't really happen but I've seen a suspicious stack trace..
	}

	ProcessQueuedVKeys();

	const bool skipBufferEffects = g_Config.bSkipBufferEffects;

	bool framebufferBound = false;

	if (mode & ScreenRenderMode::FIRST) {
		// Actually, always gonna be first when it exists (?)

		// Here we do NOT bind the backbuffer or clear the screen, unless non-buffered.
		// The emuscreen is different than the others - we really want to allow the game to render to framebuffers
		// before we ever bind the backbuffer for rendering. On mobile GPUs, switching back and forth between render
		// targets is a mortal sin so it's very important that we don't bind the backbuffer unnecessarily here.
		// We only bind it in FramebufferManager::CopyDisplayToOutput (unless non-buffered)...
		// We do, however, start the frame in other ways.

		if (skipBufferEffects && !g_Config.bSoftwareRendering) {
			// We need to clear here already so that drawing during the frame is done on a clean slate.
			if (Core_IsStepping() && gpuStats.numFlips != 0) {
				draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::KEEP, RPAction::CLEAR, RPAction::CLEAR}, "EmuScreen_BackBuffer");
			} else {
				draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, 0xFF000000}, "EmuScreen_BackBuffer");
			}

			draw->SetViewport(viewport);
			draw->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
			framebufferBound = true;
		}
		draw->SetTargetSize(g_display.pixel_xres, g_display.pixel_yres);
	} else {
		// Some other screen bound the backbuffer first.
		framebufferBound = true;
	}

	g_OSD.NudgeIngameNotifications();

	const DeviceOrientation orientation = GetDeviceOrientation();
	const DisplayLayoutConfig &displayLayoutConfig = g_Config.GetDisplayLayoutConfig(orientation);
	__DisplaySetDisplayLayoutConfig(displayLayoutConfig);

	if (mode & ScreenRenderMode::TOP) {
		System_Notify(SystemNotification::KEEP_SCREEN_AWAKE);
	} else if (!ShouldRunBehind() && strcmp(screenManager()->topScreen()->tag(), "DevMenu") != 0) {
		// NOTE: The strcmp is != 0 - so all popped-over screens EXCEPT DevMenu
		// Just to make sure.
		if (PSP_IsInited() && !skipBufferEffects) {
			_dbg_assert_(gpu);
			gpu->BeginHostFrame(displayLayoutConfig);
			gpu->SetCurFramebufferDirty(true);
			gpu->CopyDisplayToOutput(displayLayoutConfig);
			gpu->EndHostFrame();
		}
		if (gpu && gpu->PresentedThisFrame()) {
			framebufferBound = true;
		}
		if (!framebufferBound) {
			draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR,}, "EmuScreen_Behind");
		}

		Draw::BackendState state = draw->GetCurrentBackendState();
		if (state.valid) {
			// The below can trigger when switching from skip-buffer-effects. We don't really care anymore...
			// _dbg_assert_msg_(state.passes >= 1, "skipB: %d sw: %d mode: %d back: %d tag: %s behi: %d", (int)skipBufferEffects, (int)g_Config.bSoftwareRendering, (int)mode, (int)g_Config.iGPUBackend, screenManager()->topScreen()->tag(), (int)g_Config.bRunBehindPauseMenu);
			// Workaround any remaining bugs like this.
			if (state.passes == 0) {
				draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR,}, "EmuScreen_SafeFallback");
			}
		}

		// Need to make sure the UI texture is available, for "darken".
		screenManager()->getUIContext()->BeginFrame();
		draw->SetViewport(viewport);
		draw->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
		darken();
		return ScreenRenderFlags::NONE;
	}

	if (!PSP_IsInited() || readyToFinishBoot_) {
		// It's possible this might be set outside PSP_RunLoopFor().
		// In this case, we need to double check it here.
		if (mode & ScreenRenderMode::TOP) {
			checkPowerDown();
		}
		draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR}, "EmuScreen_Invalid");
		// Need to make sure the UI texture is available, for "darken".
		screenManager()->getUIContext()->BeginFrame();
		draw->SetViewport(viewport);
		draw->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
		renderUI();
		return ScreenRenderFlags::NONE;
	}

	// Freeze-frame functionality (loads a savestate on every frame).
	if (PSP_CoreParameter().freezeNext) {
		PSP_CoreParameter().frozen = true;
		PSP_CoreParameter().freezeNext = false;
		SaveState::SaveToRam(freezeState_);
	} else if (PSP_CoreParameter().frozen) {
		std::string errorString;
		if (CChunkFileReader::ERROR_NONE != SaveState::LoadFromRam(freezeState_, &errorString)) {
			ERROR_LOG(Log::SaveState, "Failed to load freeze state (%s). Unfreezing.", errorString.c_str());
			PSP_CoreParameter().frozen = false;
		}
	}

	// Running it early allows things like direct readbacks of buffers, things we can't do
	// when we have started the final render pass. Well, technically we probably could with some manipulation
	// of pass order in the render managers..
	runImDebugger();

	return RunEmulation(mode, framebufferBound, skipBufferEffects);
}

ScreenRenderFlags EmuScreen::RunEmulation(ScreenRenderMode mode, bool framebufferBound, bool skipBufferEffects) {
	using namespace Draw;
	ScreenRenderFlags flags = ScreenRenderFlags::NONE;

	const DeviceOrientation orientation = GetDeviceOrientation();
	const DisplayLayoutConfig &displayLayoutConfig = g_Config.GetDisplayLayoutConfig(orientation);
	DrawContext *draw = screenManager()->getDrawContext();
	const Draw::Viewport viewport{0.0f, 0.0f, (float)g_display.pixel_xres, (float)g_display.pixel_yres, 0.0f, 1.0f};

	PSP_UpdateDebugStats((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS || g_Config.bLogFrameDrops);
	bool blockedExecution = Achievements::IsBlockingExecution();
	uint32_t clearColor = 0;
	if (!blockedExecution) {
		if (gpu) {
			gpu->BeginHostFrame(displayLayoutConfig);
		}
		if (SaveState::Process()) {
			// We might have lost the framebuffer bind if we had one, due to a readback.
			if (framebufferBound) {
				draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, clearColor }, "EmuScreen_SavestateRebind");
			}
		}
		PSP_RunLoopWhileState();

		// Hopefully, after running, coreState is now CORE_NEXTFRAME
		switch (coreState) {
		case CORE_NEXTFRAME:
			// Reached the end of the frame while running at full blast, all good. Set back to running for the next frame
			coreState = frameStep_ ? CORE_STEPPING_CPU : CORE_RUNNING_CPU;
			flags |= ScreenRenderFlags::HANDLED_THROTTLING;
			break;
		case CORE_STEPPING_CPU:
		case CORE_STEPPING_GE:
		case CORE_RUNTIME_ERROR:
		{
			// If there's an exception, display information.
			const MIPSExceptionInfo &info = Core_GetExceptionInfo();
			if (info.type != MIPSExceptionType::NONE) {
				// Clear to blue background screen
				bool dangerousSettings = !Reporting::IsSupported();
				clearColor = dangerousSettings ? 0xFF900050 : 0xFF900000;
				draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, clearColor }, "EmuScreen_RuntimeError");
				framebufferBound = true;
				// The info is drawn later in renderUI
			} else {
				// If we're stepping, it's convenient not to clear the screen entirely, so we copy display to output.
				// This won't work in non-buffered, but that's fine.
				if (!framebufferBound && PSP_IsInited()) {
					// draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, clearColor }, "EmuScreen_Stepping");
					gpu->SetCurFramebufferDirty(true);
					gpu->CopyDisplayToOutput(displayLayoutConfig);
					framebufferBound = true;
				}
			}
			break;
		}
		default:
			// Didn't actually reach the end of the frame, ran out of the blockTicks cycles.
			// In this case we need to bind and wipe the backbuffer, at least.
			// It's possible we never ended up outputted anything - make sure we have the backbuffer cleared
			// So, we don't set framebufferBound here.

			// However, let's not cause a UI sleep in the mainloop.
			flags |= ScreenRenderFlags::HANDLED_THROTTLING;
			break;
		}

		if (gpu) {
			gpu->EndHostFrame();
		}

		if (SaveState::PollRestartNeeded() && !bootPending_) {
			Achievements::UnloadGame();
			PSP_Shutdown(true);

			// Restart the boot process
			bootPending_ = true;
			bootIsReset_ = true;
			RecreateViews();
			_dbg_assert_(coreState == CORE_POWERDOWN);
			if (!PSP_InitStart(PSP_CoreParameter())) {
				bootPending_ = false;
				WARN_LOG(Log::Loader, "Error resetting");
				screenManager()->switchScreen(new MainScreen());
			}
		}
	}

	if (frameStep_) {
		frameStep_ = false;
		if (coreState == CORE_RUNNING_CPU) {
			Core_Break(BreakReason::FrameAdvance, 0);
		}
	}

	if (gpu && gpu->PresentedThisFrame()) {
		framebufferBound = true;
	}

	if (!framebufferBound) {
		draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, clearColor }, "EmuScreen_NoFrame");
		draw->SetViewport(viewport);
		draw->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
	}

	Draw::BackendState state = draw->GetCurrentBackendState();

	// State.valid just states whether the passes parameter has a meaningful value.
	if (state.valid) {
		_dbg_assert_msg_(state.passes >= 1, "skipB: %d sw: %d mode: %d back: %d bound: %d", (int)skipBufferEffects, (int)g_Config.bSoftwareRendering, (int)mode, (int)g_Config.iGPUBackend, (int)framebufferBound);
		if (state.passes == 0) {
			// Workaround any remaining bugs like this.
			draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, }, "EmuScreen_SafeFallback");
		}
	}

	screenManager()->getUIContext()->BeginFrame();

	if (!(mode & ScreenRenderMode::TOP)) {
		renderImDebugger();
		// We're in run-behind mode, but we don't want to draw chat, debug UI and stuff. We do draw the imdebugger though.
		// So, darken and bail here.
		// Reset viewport/scissor to be sure.
		draw->SetViewport(viewport);
		draw->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
		darken();
		return flags;
	}

	// NOTE: We don't check for powerdown if we're not the top screen.
	if (checkPowerDown()) {
		draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR, clearColor }, "EmuScreen_PowerDown");
	}

	if (hasVisibleUI()) {
		draw->SetViewport(viewport);
		cardboardDisableButton_->SetVisibility(displayLayoutConfig.bEnableCardboardVR ? UI::V_VISIBLE : UI::V_GONE);
		renderUI();
	}

	if (chatMenu_ && (chatMenu_->GetVisibility() == UI::V_VISIBLE)) {
		SetVRAppMode(VRAppMode::VR_DIALOG_MODE);
	} else {
		SetVRAppMode(screenManager()->topScreen() == this ? VRAppMode::VR_GAME_MODE : VRAppMode::VR_DIALOG_MODE);
	}

	renderImDebugger();
	return flags;
}

void EmuScreen::runImDebugger() {
	if (!lastImguiEnabled_ && g_Config.bShowImDebugger) {
		System_NotifyUIEvent(UIEventNotification::TEXT_GOTFOCUS);
		VERBOSE_LOG(Log::System, "activating keyboard");
	} else if (lastImguiEnabled_ && !g_Config.bShowImDebugger) {
		System_NotifyUIEvent(UIEventNotification::TEXT_LOSTFOCUS);
		VERBOSE_LOG(Log::System, "deactivating keyboard");
	}
	lastImguiEnabled_ = g_Config.bShowImDebugger;
	if (g_Config.bShowImDebugger) {
		Draw::DrawContext *draw = screenManager()->getDrawContext();
		if (!imguiInited_) {
			// TODO: Do this only on demand.
			IMGUI_CHECKVERSION();
			ctx_ = ImGui::CreateContext();

			ImGui_ImplPlatform_Init(GetSysDirectory(DIRECTORY_SYSTEM) / "imgui.ini");
			imDebugger_ = std::make_unique<ImDebugger>();

			// Read the TTF font
			size_t propSize = 0;
			const uint8_t *propFontData = g_VFS.ReadFile("Roboto_Condensed-Regular.ttf", &propSize);
			size_t fixedSize = 0;
			const uint8_t *fixedFontData = g_VFS.ReadFile("Inconsolata-Regular.ttf", &fixedSize);
			// This call works even if fontData is nullptr, in which case the font just won't get loaded.
			// This takes ownership of the font array.
			ImGui_ImplThin3d_Init(draw, propFontData, propSize, fixedFontData, fixedSize);
			imguiInited_ = true;
		}

		if (PSP_IsInited()) {
			_dbg_assert_(imDebugger_);

			ImGui_ImplPlatform_NewFrame();
			ImGui_ImplThin3d_NewFrame(draw, ui_draw2d.GetDrawMatrix());

			ImGui::NewFrame();

			if (imCmd_.cmd != ImCmd::NONE) {
				imDebugger_->PostCmd(imCmd_);
				imCmd_.cmd = ImCmd::NONE;
			}

			// Update keyboard modifiers.
			auto &io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, keyCtrlLeft_ || keyCtrlRight_);
			io.AddKeyEvent(ImGuiMod_Shift, keyShiftLeft_ || keyShiftRight_);
			io.AddKeyEvent(ImGuiMod_Alt, keyAltLeft_ || keyAltRight_);
			// io.AddKeyEvent(ImGuiMod_Super, e.key.super);

			ImGuiID dockID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);
			ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(dockID);

			// Not elegant! But don't know how else to pass through the bounds, without making a mess.
			Bounds centralNode(node->Pos.x, node->Pos.y, node->Size.x, node->Size.y);
			SetOverrideScreenFrame(&centralNode);

			if (!io.WantCaptureKeyboard) {
				// Draw a focus rectangle to indicate inputs will be passed through.
				ImGui::GetBackgroundDrawList()->AddRect
				(
					node->Pos,
					{ node->Pos.x + node->Size.x, node->Pos.y + node->Size.y },
					IM_COL32(255, 255, 255, 90),
					0.f,
					ImDrawFlags_None,
					1.f
				);
			}
			imDebugger_->Frame(currentDebugMIPS, gpuDebug, draw);

			// Convert to drawlists.
			ImGui::Render();
		}
	}
}

void EmuScreen::renderImDebugger() {
	if (g_Config.bShowImDebugger) {
		Draw::DrawContext *draw = screenManager()->getDrawContext();
		if (PSP_IsInited() && imDebugger_) {
			ImGui_ImplThin3d_RenderDrawData(ImGui::GetDrawData(), draw);
		}
	}
}

bool EmuScreen::hasVisibleUI() {
	// Regular but uncommon UI.
	if (saveStatePreview_->GetVisibility() != UI::V_GONE || loadingSpinner_->GetVisibility() == UI::V_VISIBLE)
		return true;
	if (!g_OSD.IsEmpty() || g_Config.bShowTouchControls || g_Config.iShowStatusFlags != 0)
		return true;
	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(GetDeviceOrientation());
	if (config.bEnableCardboardVR || g_Config.bEnableNetworkChat)
		return true;
	if (g_Config.bShowGPOLEDs)
		return true;
	// Debug UI.
	if ((DebugOverlay)g_Config.iDebugOverlay != DebugOverlay::OFF || g_Config.bShowDeveloperMenu)
		return true;

	// Exception information.
	if (coreState == CORE_RUNTIME_ERROR || coreState == CORE_STEPPING_CPU) {
		return true;
	}

	return false;
}

void EmuScreen::renderUI() {
	using namespace Draw;

	DrawContext *thin3d = screenManager()->getDrawContext();
	UIContext *ctx = screenManager()->getUIContext();
	ctx->BeginFrame();
	// This sets up some important states but not the viewport.
	ctx->Begin();

	Viewport viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = g_display.pixel_xres;
	viewport.Height = g_display.pixel_yres;
	viewport.MaxDepth = 1.0;
	viewport.MinDepth = 0.0;
	thin3d->SetViewport(viewport);

	if (root_) {
		UI::LayoutViewHierarchy(*ctx, RootMargins(), root_, false, false);
		root_->Draw(*ctx);
	}

	if (PSP_IsInited()) {
		if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::CONTROL) {
			DrawControlMapperOverlay(ctx, ctx->GetLayoutBounds(), controlMapper_);
		}
		if (g_Config.iShowStatusFlags) {
			DrawFPS(ctx, ctx->GetLayoutBounds());
		}
	}

#ifdef USE_PROFILER
	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::FRAME_PROFILE && PSP_IsInited()) {
		DrawProfile(*ctx);
	}
#endif

	if (g_Config.bShowGPOLEDs) {
		// Draw a vertical strip of LEDs at the right side of the screen.
		const float ledSize = 24.0f;
		const float spacing = 4.0f;
		const float height = 8 * ledSize + 7 * spacing;
		const float x = ctx->GetBounds().w - spacing - ledSize;
		const float y = (ctx->GetBounds().h - height) * 0.5f;
		ctx->FillRect(UI::Drawable(0xFF000000), Bounds(x - spacing, y - spacing, ledSize + spacing * 2, height + spacing * 2));
		for (int i = 0; i < 8; i++) {
			int bit = (g_GPOBits >> i) & 1;
			uint32_t color = 0xFF30FF30;
			if (!bit) {
				color = darkenColor(darkenColor(color));
			}
			Bounds ledBounds(x, y + (spacing + ledSize) * i, ledSize, ledSize);
			ctx->FillRect(UI::Drawable(color), ledBounds);
		}
		ctx->Flush();
	}

	if (coreState == CORE_RUNTIME_ERROR || coreState == CORE_STEPPING_CPU) {
		const MIPSExceptionInfo &info = Core_GetExceptionInfo();
		if (info.type != MIPSExceptionType::NONE) {
			DrawCrashDump(ctx, gamePath_);
		} else {
			// We're somehow in ERROR or STEPPING without a crash dump. This case is what lead
			// to the bare "Resume" and "Reset" buttons without a crash dump before, in cases
			// where we were unable to ignore memory errors.
		}
	}

	ctx->Flush();
}

void EmuScreen::AutoLoadSaveState() {
	if (autoLoadFailed_) {
		return;
	}

	int autoSlot = -1;

	std::string gamePrefix = SaveState::GetGamePrefix(g_paramSFO);

	//check if save state has save, if so, load
	switch (g_Config.iAutoLoadSaveState) {
	case (int)AutoLoadSaveState::OFF: // "AutoLoad Off"
		return;
	case (int)AutoLoadSaveState::OLDEST: // "Oldest Save"
		autoSlot = SaveState::GetOldestSlot(gamePrefix);
		break;
	case (int)AutoLoadSaveState::NEWEST: // "Newest Save"
		autoSlot = SaveState::GetNewestSlot(gamePrefix);
		break;
	default: // try the specific save state slot specified
		autoSlot = (SaveState::HasSaveInSlot(gamePrefix, g_Config.iAutoLoadSaveState - 3)) ? (g_Config.iAutoLoadSaveState - 3) : -1;
		break;
	}

	if (g_Config.iAutoLoadSaveState && autoSlot != -1) {
		SaveState::LoadSlot(gamePrefix, autoSlot, [this, autoSlot](SaveState::Status status, std::string_view message) {
			AfterSaveStateAction(status, message);
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
			std::string msg = std::string(sy->T("Auto Load Savestate")) + ": " + StringFromFormat("%d", autoSlot);
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, msg);
			if (status == SaveState::Status::FAILURE) {
				autoLoadFailed_ = true;
			}
		});
		g_Config.iCurrentStateSlot = autoSlot;
	}
}

void EmuScreen::resized() {
	RecreateViews();
}

bool MustRunBehind() {
	return IsNetworkConnected();
}

bool ShouldRunBehind() {
	// Enforce run-behind if ad-hoc connected
	return g_Config.bRunBehindPauseMenu || MustRunBehind();
}
