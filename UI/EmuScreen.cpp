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

#include <algorithm>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "profiler/profiler.h"

#include "gfx_es2/gpu_features.h"
#include "gfx_es2/draw_text.h"

#include "input/input_state.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "i18n/i18n.h"

#include "Common/KeyMap.h"

#ifndef MOBILE_DEVICE
#include "Core/AVIDump.h"
#endif
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceSas.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/SaveState.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/proAdhoc.h"

#include "UI/ui_atlas.h"
#include "UI/BackgroundAudio.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GamepadEmu.h"
#include "UI/PauseScreen.h"
#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/DevScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/InstallZipScreen.h"
#include "UI/ProfilerDraw.h"
#include "UI/ChatScreen.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#include "Windows/MainWindow.h"
#endif
#if !PPSSPP_PLATFORM(UWP)
#include "gfx/gl_common.h"
#endif

#ifndef MOBILE_DEVICE
AVIDump avi;
#endif

static bool frameStep_;
static int lastNumFlips;
static bool startDumping;
static void __EmuScreenVblank()
{
	I18NCategory *sy = GetI18NCategory("System");

	if (frameStep_ && lastNumFlips != gpuStats.numFlips)
	{
		frameStep_ = false;
		Core_EnableStepping(true);
		lastNumFlips = gpuStats.numFlips;
	}
#ifndef MOBILE_DEVICE
	if (g_Config.bDumpFrames && !startDumping)
	{
		avi.Start(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
		osm.Show(sy->T("AVI Dump started."), 3.0f);
		startDumping = true;
	}
	if (g_Config.bDumpFrames && startDumping)
	{
		avi.AddFrame();
	}
	else if (!g_Config.bDumpFrames && startDumping)
	{
		avi.Stop();
		osm.Show(sy->T("AVI Dump stopped."), 3.0f);
		startDumping = false;
	}
#endif
}

EmuScreen::EmuScreen(const std::string &filename)
	: bootPending_(true), gamePath_(filename), invalid_(true), quit_(false), pauseTrigger_(false), saveStatePreviewShownTime_(0.0), saveStatePreview_(nullptr) {
	memset(axisState_, 0, sizeof(axisState_));
	saveStateSlot_ = SaveState::GetCurrentSlot();
	__DisplayListenVblank(__EmuScreenVblank);
	frameStep_ = false;
	lastNumFlips = gpuStats.numFlips;
	startDumping = false;

	OnDevMenu.Handle(this, &EmuScreen::OnDevTools);
}

void EmuScreen::bootGame(const std::string &filename) {
	if (PSP_IsIniting()) {
		std::string error_string;
		bootPending_ = !PSP_InitUpdate(&error_string);
		if (!bootPending_) {
			invalid_ = !PSP_IsInited();
			if (invalid_) {
				errorMessage_ = error_string;
				ERROR_LOG(BOOT, "%s", errorMessage_.c_str());
				System_SendMessage("event", "failstartgame");
				return;
			}
			bootComplete();
		}
		return;
	}

	SetBackgroundAudioGame("");

	//pre-emptive loading of game specific config if possible, to get all the settings
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, filename, 0);
	if (info && !info->id.empty()) {
		g_Config.loadGameConfig(info->id);
	}

	invalid_ = true;

	CoreParameter coreParam{};
	coreParam.cpuCore = (CPUCore)g_Config.iCpuCore;
	coreParam.gpuCore = GPUCORE_GLES;
	switch (GetGPUBackend()) {
	case GPUBackend::DIRECT3D11:
		coreParam.gpuCore = GPUCORE_DIRECTX11;
		break;
#if !PPSSPP_PLATFORM(UWP)
	case GPUBackend::OPENGL:
		coreParam.gpuCore = GPUCORE_GLES;
		break;
	case GPUBackend::DIRECT3D9:
		coreParam.gpuCore = GPUCORE_DIRECTX9;
		break;
	case GPUBackend::VULKAN:
		coreParam.gpuCore = GPUCORE_VULKAN;
		break;
#endif
	}
	if (g_Config.bSoftwareRendering) {
		coreParam.gpuCore = GPUCORE_SOFTWARE;
	}
	// Preserve the existing graphics context.
	coreParam.graphicsContext = PSP_CoreParameter().graphicsContext;
	coreParam.thin3d = screenManager()->getDrawContext();
	coreParam.enableSound = g_Config.bEnableSound;
	coreParam.fileToStart = filename;
	coreParam.mountIso = "";
	coreParam.mountRoot = "";
	coreParam.startPaused = false;
	coreParam.printfEmuLog = false;
	coreParam.headLess = false;

	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	if (g_Config.iInternalResolution == 0) {
		coreParam.renderWidth = pixel_xres;
		coreParam.renderHeight = pixel_yres;
	} else {
		if (g_Config.iInternalResolution < 0)
			g_Config.iInternalResolution = 1;
		coreParam.renderWidth = 480 * g_Config.iInternalResolution;
		coreParam.renderHeight = 272 * g_Config.iInternalResolution;
	}
	coreParam.pixelWidth = pixel_xres;
	coreParam.pixelHeight = pixel_yres;

	std::string error_string;
	if (!PSP_InitStart(coreParam, &error_string)) {
		bootPending_ = false;
		invalid_ = true;
		errorMessage_ = error_string;
		ERROR_LOG(BOOT, "%s", errorMessage_.c_str());
		System_SendMessage("event", "failstartgame");
	}

	if (PSP_CoreParameter().compat.flags().RequireBufferedRendering && g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		I18NCategory *gr = GetI18NCategory("Graphics");
		host->NotifyUserMessage(gr->T("BufferedRenderingRequired", "Warning: This game requires Rendering Mode to be set to Buffered."), 15.0f);
	}

	if (PSP_CoreParameter().compat.flags().RequireBlockTransfer && g_Config.bBlockTransferGPU == false) {
		I18NCategory *gr = GetI18NCategory("Graphics");
		host->NotifyUserMessage(gr->T("BlockTransferRequired", "Warning: This game requires Simulate Block Transfer Mode to be set to On."), 15.0f);
	}
}

void EmuScreen::bootComplete() {
	UpdateUIState(UISTATE_INGAME);
	host->BootDone();
	host->UpdateDisassembly();

	g_gameInfoCache->FlushBGs();

	NOTICE_LOG(BOOT, "Loading %s...", PSP_CoreParameter().fileToStart.c_str());
	autoLoad();

	I18NCategory *sc = GetI18NCategory("Screen");

#ifndef MOBILE_DEVICE
	if (g_Config.bFirstRun) {
		osm.Show(sc->T("PressESC", "Press ESC to open the pause menu"), 3.0f);
	}
#endif
	memset(virtKeys, 0, sizeof(virtKeys));

#if !PPSSPP_PLATFORM(UWP)
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		const char *renderer = (const char*)glGetString(GL_RENDERER);
		if (strstr(renderer, "Chainfire3D") != 0) {
			osm.Show(sc->T("Chainfire3DWarning", "WARNING: Chainfire3D detected, may cause problems"), 10.0f, 0xFF30a0FF, -1, true);
		} else if (strstr(renderer, "GLTools") != 0) {
			osm.Show(sc->T("GLToolsWarning", "WARNING: GLTools detected, may cause problems"), 10.0f, 0xFF30a0FF, -1, true);
		}

		if (g_Config.bGfxDebugOutput) {
			osm.Show("WARNING: GfxDebugOutput is enabled via ppsspp.ini. Things may be slow.", 10.0f, 0xFF30a0FF, -1, true);
		}
	}
#endif

	if (Core_GetPowerSaving()) {
		I18NCategory *sy = GetI18NCategory("System");
#ifdef __ANDROID__
		osm.Show(sy->T("WARNING: Android battery save mode is on"), 2.0f, 0xFFFFFF, -1, true, "core_powerSaving");
#else
		osm.Show(sy->T("WARNING: Battery save mode is on"), 2.0f, 0xFFFFFF, -1, true, "core_powerSaving");
#endif
	}

	System_SendMessage("event", "startgame");

	saveStateSlot_ = SaveState::GetCurrentSlot();
}

EmuScreen::~EmuScreen() {
	if (!invalid_) {
		// If we were invalid, it would already be shutdown.
		PSP_Shutdown();
	}
#ifndef MOBILE_DEVICE
	if (g_Config.bDumpFrames && startDumping)
	{
		avi.Stop();
		osm.Show("AVI Dump stopped.", 3.0f);
		startDumping = false;
	}
#endif
}

void EmuScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// TODO: improve the way with which we got commands from PauseMenu.
	// DR_CANCEL/DR_BACK means clicked on "continue", DR_OK means clicked on "back to menu",
	// DR_YES means a message sent to PauseMenu by NativeMessageReceived.
	if (result == DR_OK || quit_) {
		screenManager()->switchScreen(new MainScreen());
		System_SendMessage("event", "exitgame");
		quit_ = false;
	}
	RecreateViews();
}

static void AfterSaveStateAction(bool success, const std::string &message, void *) {
	if (!message.empty()) {
		osm.Show(message, 2.0);
	}
}

static void AfterStateBoot(bool success, const std::string &message, void *ignored) {
	AfterSaveStateAction(success, message, ignored);
	Core_EnableStepping(false);
	host->UpdateDisassembly();
}

void EmuScreen::sendMessage(const char *message, const char *value) {
	// External commands, like from the Windows UI.
	if (!strcmp(message, "pause")) {
		releaseButtons();
		screenManager()->push(new GamePauseScreen(gamePath_));
	} else if (!strcmp(message, "lost_focus")) {
		releaseButtons();
	} else if (!strcmp(message, "stop")) {
		// We will push MainScreen in update().
		PSP_Shutdown();
		bootPending_ = false;
		invalid_ = true;
		host->UpdateDisassembly();
	} else if (!strcmp(message, "reset")) {
		PSP_Shutdown();
		bootPending_ = true;
		invalid_ = true;
		host->UpdateDisassembly();

		std::string resetError;
		if (!PSP_InitStart(PSP_CoreParameter(), &resetError)) {
			ELOG("Error resetting: %s", resetError.c_str());
			screenManager()->switchScreen(new MainScreen());
			System_SendMessage("event", "failstartgame");
			return;
		}
	} else if (!strcmp(message, "boot")) {
		const char *ext = strrchr(value, '.');
		if (ext != nullptr && !strcmp(ext, ".ppst")) {
			SaveState::Load(value, &AfterStateBoot);
		} else {
			PSP_Shutdown();
			bootPending_ = true;
			bootGame(value);
		}
	} else if (!strcmp(message, "control mapping")) {
		UpdateUIState(UISTATE_MENU);
		releaseButtons();
		screenManager()->push(new ControlMappingScreen());
	} else if (!strcmp(message, "display layout editor")) {
		UpdateUIState(UISTATE_MENU);
		releaseButtons();
		screenManager()->push(new DisplayLayoutScreen());
	} else if (!strcmp(message, "settings")) {
		UpdateUIState(UISTATE_MENU);
		releaseButtons();
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else if (!strcmp(message, "gpu dump next frame")) {
		if (gpu)
			gpu->DumpNextFrame();
	} else if (!strcmp(message, "clear jit")) {
		currentMIPS->ClearJitCache();
		if (PSP_IsInited()) {
			currentMIPS->UpdateCore((CPUCore)g_Config.iCpuCore);
		}
	} else if (!strcmp(message, "window minimized")) {
		if (!strcmp(value, "true")) {
			gstate_c.skipDrawReason |= SKIPDRAW_WINDOW_MINIMIZED;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_WINDOW_MINIMIZED;
		}
	}
}

//tiltInputCurve implements a smooth deadzone as described here:
//http://www.gamasutra.com/blogs/JoshSutphin/20130416/190541/Doing_Thumbstick_Dead_Zones_Right.php
inline float tiltInputCurve(float x) {
	const float deadzone = g_Config.fDeadzoneRadius;
	const float factor = 1.0f / (1.0f - deadzone);

	if (x > deadzone) {
		return (x - deadzone) * (x - deadzone) * factor;
	} else if (x < -deadzone) {
		return -(x + deadzone) * (x + deadzone) * factor;
	} else {
		return 0.0f;
	}
}

inline float clamp1(float x) {
	if (x > 1.0f) return 1.0f;
	if (x < -1.0f) return -1.0f;
	return x;
}

bool EmuScreen::touch(const TouchInput &touch) {
	Core_NotifyActivity();

	if (root_) {
		root_->Touch(touch);
		return true;
	} else {
		return false;
	}
}

void EmuScreen::onVKeyDown(int virtualKeyCode) {
	I18NCategory *sc = GetI18NCategory("Screen");

	switch (virtualKeyCode) {
	case VIRTKEY_UNTHROTTLE:
		PSP_CoreParameter().unthrottle = true;
		break;

	case VIRTKEY_SPEED_TOGGLE:
		if (PSP_CoreParameter().fpsLimit == 0) {
			PSP_CoreParameter().fpsLimit = 1;
			osm.Show(sc->T("fixed", "Speed: alternate"), 1.0);
		}
		else if (PSP_CoreParameter().fpsLimit == 1) {
			PSP_CoreParameter().fpsLimit = 0;
			osm.Show(sc->T("standard", "Speed: standard"), 1.0);
		}
		break;

	case VIRTKEY_PAUSE:
		pauseTrigger_ = true;
		break;

	case VIRTKEY_FRAME_ADVANCE:
		// If game is running, pause emulation immediately. Otherwise, advance a single frame.
		if (Core_IsStepping())
		{
			frameStep_ = true;
			Core_EnableStepping(false);
		}
		else if (!frameStep_)
		{
			Core_EnableStepping(true);
		}
		break;

	case VIRTKEY_AXIS_SWAP:
		KeyMap::SwapAxis();
		break;

	case VIRTKEY_DEVMENU:
	{
		UI::EventParams e{};
		OnDevMenu.Trigger(e);
		break;
	}

	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
		setVKeyAnalogX(CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
		break;
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
		setVKeyAnalogY(CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
		break;

	case VIRTKEY_AXIS_RIGHT_X_MIN:
	case VIRTKEY_AXIS_RIGHT_X_MAX:
		setVKeyAnalogX(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
		break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		setVKeyAnalogY(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
		break;

	case VIRTKEY_ANALOG_LIGHTLY:
		setVKeyAnalogX(CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
		setVKeyAnalogY(CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
		setVKeyAnalogX(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
		setVKeyAnalogY(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
		break;

	case VIRTKEY_REWIND:
		if (SaveState::CanRewind()) {
			SaveState::Rewind(&AfterSaveStateAction);
		} else {
			osm.Show(sc->T("norewind", "No rewind save states available"), 2.0);
		}
		break;
	case VIRTKEY_SAVE_STATE:
		SaveState::SaveSlot(gamePath_, g_Config.iCurrentStateSlot, &AfterSaveStateAction);
		break;
	case VIRTKEY_LOAD_STATE:
		SaveState::LoadSlot(gamePath_, g_Config.iCurrentStateSlot, &AfterSaveStateAction);
		break;
	case VIRTKEY_NEXT_SLOT:
		SaveState::NextSlot();
		NativeMessageReceived("savestate_displayslot", "");
		break;
	case VIRTKEY_TOGGLE_FULLSCREEN:
		System_SendMessage("toggle_fullscreen", "");
		break;
	}
}

void EmuScreen::onVKeyUp(int virtualKeyCode) {
	switch (virtualKeyCode) {
	case VIRTKEY_UNTHROTTLE:
		PSP_CoreParameter().unthrottle = false;
		break;

	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
		setVKeyAnalogX(CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
		break;
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
		setVKeyAnalogY(CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
		break;

	case VIRTKEY_AXIS_RIGHT_X_MIN:
	case VIRTKEY_AXIS_RIGHT_X_MAX:
		setVKeyAnalogX(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
		break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		setVKeyAnalogY(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
		break;

	case VIRTKEY_ANALOG_LIGHTLY:
		setVKeyAnalogX(CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
		setVKeyAnalogY(CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
		setVKeyAnalogX(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
		setVKeyAnalogY(CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
		break;

	default:
		break;
	}
}

// Handles control rotation due to internal screen rotation.
static void SetPSPAxis(char axis, float value, int stick) {
	switch (g_Config.iInternalScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL:
		// Standard rotation.
		break;
	case ROTATION_LOCKED_HORIZONTAL180:
		value = -value;
		break;
	case ROTATION_LOCKED_VERTICAL:
		value = axis == 'Y' ? value : -value;
		axis = (axis == 'X') ? 'Y' : 'X';
		break;
	case ROTATION_LOCKED_VERTICAL180:
		value = axis == 'Y' ? -value : value;
		axis = (axis == 'X') ? 'Y' : 'X';
		break;
	default:
		break;
	}
	if (axis == 'X')
		__CtrlSetAnalogX(value, stick);
	else if (axis == 'Y')
		__CtrlSetAnalogY(value, stick);
}

inline void EmuScreen::setVKeyAnalogX(int stick, int virtualKeyMin, int virtualKeyMax) {
	const float value = virtKeys[VIRTKEY_ANALOG_LIGHTLY - VIRTKEY_FIRST] ? g_Config.fAnalogLimiterDeadzone : 1.0f;
	float axis = 0.0f;
	// The down events can repeat, so just trust the virtKeys array.
	if (virtKeys[virtualKeyMin - VIRTKEY_FIRST])
		axis -= value;
	if (virtKeys[virtualKeyMax - VIRTKEY_FIRST])
		axis += value;
	SetPSPAxis('X', axis, stick);
}

inline void EmuScreen::setVKeyAnalogY(int stick, int virtualKeyMin, int virtualKeyMax) {
	const float value = virtKeys[VIRTKEY_ANALOG_LIGHTLY - VIRTKEY_FIRST] ? g_Config.fAnalogLimiterDeadzone : 1.0f;
	float axis = 0.0f;
	if (virtKeys[virtualKeyMin - VIRTKEY_FIRST])
		axis -= value;
	if (virtKeys[virtualKeyMax - VIRTKEY_FIRST])
		axis += value;
	SetPSPAxis('Y', axis, stick);
}

bool EmuScreen::key(const KeyInput &key) {
	Core_NotifyActivity();

	std::vector<int> pspKeys;
	KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &pspKeys);

	if (pspKeys.size() && (key.flags & KEY_IS_REPEAT)) {
		// Claim that we handled this. Prevents volume key repeats from popping up the volume control on Android.
		return true;
	}

	for (size_t i = 0; i < pspKeys.size(); i++) {
		pspKey(pspKeys[i], key.flags);
	}

	if (!pspKeys.size() || key.deviceId == DEVICE_ID_DEFAULT) {
		if ((key.flags & KEY_DOWN) && key.keyCode == NKCODE_BACK) {
			pauseTrigger_ = true;
			return true;
		}
	}

	return pspKeys.size() > 0;
}

static int RotatePSPKeyCode(int x) {
	switch (x) {
	case CTRL_UP: return CTRL_RIGHT;
	case CTRL_RIGHT: return CTRL_DOWN;
	case CTRL_DOWN: return CTRL_LEFT;
	case CTRL_LEFT: return CTRL_UP;
	default:
		return x;
	}
}

void EmuScreen::pspKey(int pspKeyCode, int flags) {
	int rotations = 0;
	switch (g_Config.iInternalScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL180:
		rotations = 2;
		break;
	case ROTATION_LOCKED_VERTICAL:
		rotations = 1;
		break;
	case ROTATION_LOCKED_VERTICAL180:
		rotations = 3;
		break;
	}

	for (int i = 0; i < rotations; i++) {
		pspKeyCode = RotatePSPKeyCode(pspKeyCode);
	}

	if (pspKeyCode >= VIRTKEY_FIRST) {
		int vk = pspKeyCode - VIRTKEY_FIRST;
		if (flags & KEY_DOWN) {
			virtKeys[vk] = true;
			onVKeyDown(pspKeyCode);
		}
		if (flags & KEY_UP) {
			virtKeys[vk] = false;
			onVKeyUp(pspKeyCode);
		}
	} else {
		// ILOG("pspKey %i %i", pspKeyCode, flags);
		if (flags & KEY_DOWN)
			__CtrlButtonDown(pspKeyCode);
		if (flags & KEY_UP)
			__CtrlButtonUp(pspKeyCode);
	}
}

bool EmuScreen::axis(const AxisInput &axis) {
	Core_NotifyActivity();

	if (axis.value > 0) {
		processAxis(axis, 1);
		return true;
	} else if (axis.value < 0) {
		processAxis(axis, -1);
		return true;
	} else if (axis.value == 0) {
		// Both directions! Prevents sticking for digital input devices that are axises (like HAT)
		processAxis(axis, 1);
		processAxis(axis, -1);
		return true;
	}
	return false;
}

inline bool IsAnalogStickKey(int key) {
	switch (key) {
	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
	case VIRTKEY_AXIS_RIGHT_X_MIN:
	case VIRTKEY_AXIS_RIGHT_X_MAX:
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		return true;
	default:
		return false;
	}
}

void EmuScreen::processAxis(const AxisInput &axis, int direction) {
	// Sanity check
	if (axis.axisId < 0 || axis.axisId >= JOYSTICK_AXIS_MAX) {
		return;
	}

	std::vector<int> results;
	KeyMap::AxisToPspButton(axis.deviceId, axis.axisId, direction, &results);

	for (size_t i = 0; i < results.size(); i++) {
		int result = results[i];
		switch (result) {
		case VIRTKEY_AXIS_X_MIN:
			SetPSPAxis('X', -fabs(axis.value), CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_X_MAX:
			SetPSPAxis('X', fabs(axis.value), CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_Y_MIN:
			SetPSPAxis('Y', -fabs(axis.value), CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_Y_MAX:
			SetPSPAxis('Y', fabs(axis.value), CTRL_STICK_LEFT);
			break;

		case VIRTKEY_AXIS_RIGHT_X_MIN:
			SetPSPAxis('X', -fabs(axis.value), CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_X_MAX:
			SetPSPAxis('X', fabs(axis.value), CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_Y_MIN:
			SetPSPAxis('Y', -fabs(axis.value), CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_Y_MAX:
			SetPSPAxis('Y', fabs(axis.value), CTRL_STICK_RIGHT);
			break;
		}
	}

	std::vector<int> resultsOpposite;
	KeyMap::AxisToPspButton(axis.deviceId, axis.axisId, -direction, &resultsOpposite);

	int axisState = 0;
	if ((direction == 1 && axis.value >= AXIS_BIND_THRESHOLD)) {
		axisState = 1;
	} else if (direction == -1 && axis.value <= -AXIS_BIND_THRESHOLD) {
		axisState = -1;
	} else {
		axisState = 0;
	}

	if (axisState != axisState_[axis.axisId]) {
		axisState_[axis.axisId] = axisState;
		if (axisState != 0) {
			for (size_t i = 0; i < results.size(); i++) {
				if (!IsAnalogStickKey(results[i]))
					pspKey(results[i], KEY_DOWN);
			}
			// Also unpress the other direction (unless both directions press the same key.)
			for (size_t i = 0; i < resultsOpposite.size(); i++) {
				if (!IsAnalogStickKey(resultsOpposite[i]) && std::find(results.begin(), results.end(), resultsOpposite[i]) == results.end())
					pspKey(resultsOpposite[i], KEY_UP);
			}
		} else if (axisState == 0) {
			// Release both directions, trying to deal with some erratic controllers that can cause it to stick.
			for (size_t i = 0; i < results.size(); i++) {
				if (!IsAnalogStickKey(results[i]))
					pspKey(results[i], KEY_UP);
			}
			for (size_t i = 0; i < resultsOpposite.size(); i++) {
				if (!IsAnalogStickKey(resultsOpposite[i]))
					pspKey(resultsOpposite[i], KEY_UP);
			}
		}
	}
}

void EmuScreen::CreateViews() {
	using namespace UI;
	I18NCategory *sc = GetI18NCategory("Screen");
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);
	root_ = CreatePadLayout(bounds.w, bounds.h, &pauseTrigger_);
	if (g_Config.bShowDeveloperMenu) {
		root_->Add(new Button("DevMenu"))->OnClick.Handle(this, &EmuScreen::OnDevTools);
	}
	if (g_Config.bEnableNetworkChat) {
		//root_->Add(new Button(sc->T("Chat"), new AnchorLayoutParams(50, NONE, NONE, 50, true)))->OnClick.Handle(this, &EmuScreen::OnChat);
		root_->Add(new ChoiceWithValueDisplay(&g_Config.iNewChat, sc->T("Chat"), new AnchorLayoutParams(130,WRAP_CONTENT,80, NONE, NONE, 50, true)))->OnClick.Handle(this, &EmuScreen::OnChat);
	}
	saveStatePreview_ = new AsyncImageFileView("", IS_FIXED, nullptr, new AnchorLayoutParams(bounds.centerX(), 100, NONE, NONE, true));
	saveStatePreview_->SetFixedSize(160, 90);
	saveStatePreview_->SetColor(0x90FFFFFF);
	saveStatePreview_->SetVisibility(V_GONE);
	saveStatePreview_->SetCanBeFocused(false);
	root_->Add(saveStatePreview_);
	root_->Add(new OnScreenMessagesView(new AnchorLayoutParams((Size)bounds.w, (Size)bounds.h)));
}

UI::EventReturn EmuScreen::OnDevTools(UI::EventParams &params) {
	releaseButtons();
	DevMenu *devMenu = new DevMenu();
	if (params.v)
		devMenu->SetPopupOrigin(params.v);
	screenManager()->push(devMenu);
	return UI::EVENT_DONE;
}

UI::EventReturn EmuScreen::OnChat(UI::EventParams &params) {
	releaseButtons();
	ChatMenu * ch = new ChatMenu(); // memory leak or not?
	setChatPointer(ch);
	screenManager()->push(ch);
	return UI::EVENT_DONE;
}

void EmuScreen::update() {

	if (bootPending_)
		bootGame(gamePath_);

	UIScreen::update();

	// Simply forcibly update to the current screen size every frame. Doesn't cost much.
	// If bounds is set to be smaller than the actual pixel resolution of the display, respect that.
	// TODO: Should be able to use g_dpi_scale here instead. Might want to store the dpi scale in the UI context too.

#ifndef _WIN32
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	PSP_CoreParameter().pixelWidth = pixel_xres * bounds.w / dp_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres * bounds.h / dp_yres;
#endif

	if (!invalid_) {
		UpdateUIState(UISTATE_INGAME);
	}

	if (errorMessage_.size()) {
		// Special handling for ZIP files. It's not very robust to check an error message but meh,
		// at least it's pre-translation.
		if (errorMessage_.find("ZIP") != std::string::npos) {
			screenManager()->push(new InstallZipScreen(gamePath_));
			errorMessage_ = "";
			quit_ = true;
			return;
		}
		I18NCategory *err = GetI18NCategory("Error");
		std::string errLoadingFile = err->T("Error loading file", "Could not load game");

		errLoadingFile.append(" ");
		errLoadingFile.append(err->T(errorMessage_.c_str()));

		screenManager()->push(new PromptScreen(errLoadingFile, "OK", ""));
		errorMessage_ = "";
		quit_ = true;
		return;
	}

	if (invalid_)
		return;

	// Virtual keys.
	__CtrlSetRapidFire(virtKeys[VIRTKEY_RAPID_FIRE - VIRTKEY_FIRST]);

	// Make sure fpsLimit starts at 0
	if (PSP_CoreParameter().fpsLimit != 0 && PSP_CoreParameter().fpsLimit != 1) {
		PSP_CoreParameter().fpsLimit = 0;
	}

	// This is here to support the iOS on screen back button.
	if (pauseTrigger_) {
		pauseTrigger_ = false;
		releaseButtons();
		screenManager()->push(new GamePauseScreen(gamePath_));
	}

	if (saveStatePreview_ && !bootPending_) {
		int currentSlot = SaveState::GetCurrentSlot();
		if (saveStateSlot_ != currentSlot) {
			saveStateSlot_ = currentSlot;

			std::string fn;
			if (SaveState::HasSaveInSlot(gamePath_, currentSlot)) {
				fn = SaveState::GenerateSaveSlotFilename(gamePath_, currentSlot, SaveState::SCREENSHOT_EXTENSION);
			}

			saveStatePreview_->SetFilename(fn);
			if (!fn.empty()) {
				saveStatePreview_->SetVisibility(UI::V_VISIBLE);
				saveStatePreviewShownTime_ = time_now_d();
			} else {
				saveStatePreview_->SetVisibility(UI::V_GONE);
			}
		}

		if (saveStatePreview_->GetVisibility() == UI::V_VISIBLE) {
			double endTime = saveStatePreviewShownTime_ + 2.0;
			float alpha = clamp_value((endTime - time_now_d()) * 4.0, 0.0, 1.0);
			saveStatePreview_->SetColor(colorAlpha(0x00FFFFFF, alpha));

			if (time_now_d() - saveStatePreviewShownTime_ > 2) {
				saveStatePreview_->SetVisibility(UI::V_GONE);
			}
		}
	}
}

void EmuScreen::checkPowerDown() {
	if (coreState == CORE_POWERDOWN && !PSP_IsIniting()) {
		if (PSP_IsInited()) {
			PSP_Shutdown();
		}
		ILOG("SELF-POWERDOWN!");
		screenManager()->switchScreen(new MainScreen());
		bootPending_ = false;
		invalid_ = true;
	}
}

static void DrawDebugStats(DrawBuffer *draw2d) {
	char statbuf[4096];
	__DisplayGetDebugStats(statbuf, sizeof(statbuf));
	draw2d->SetFontScale(.7f, .7f);
	draw2d->DrawText(UBUNTU24, statbuf, 11, 31, 0xc0000000, FLAG_DYNAMIC_ASCII);
	draw2d->DrawText(UBUNTU24, statbuf, 10, 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

	__SasGetDebugStats(statbuf, sizeof(statbuf));
	draw2d->DrawText(UBUNTU24, statbuf, PSP_CoreParameter().pixelWidth / 2 + 11, 31, 0xc0000000, FLAG_DYNAMIC_ASCII);
	draw2d->DrawText(UBUNTU24, statbuf, PSP_CoreParameter().pixelWidth / 2 + 10, 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	draw2d->SetFontScale(1.0f, 1.0f);
}

static void DrawAudioDebugStats(DrawBuffer *draw2d) {
	char statbuf[1024] = { 0 };
	const AudioDebugStats *stats = __AudioGetDebugStats();
	snprintf(statbuf, sizeof(statbuf),
		"Audio buffer: %d/%d (low watermark: %d)\n"
		"Underruns: %d\n"
		"Overruns: %d\n"
		"Sample rate: %d\n"
		"Push size: %d\n",
		stats->buffered, stats->bufsize, stats->watermark,
		stats->underrunCount,
		stats->overrunCount,
		stats->instantSampleRate,
		stats->lastPushSize);
	draw2d->SetFontScale(0.7f, 0.7f);
	draw2d->DrawText(UBUNTU24, statbuf, 11, 31, 0xc0000000, FLAG_DYNAMIC_ASCII);
	draw2d->DrawText(UBUNTU24, statbuf, 10, 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	draw2d->SetFontScale(1.0f, 1.0f);
}

static void DrawFPS(DrawBuffer *draw2d, const Bounds &bounds) {
	float vps, fps, actual_fps;
	__DisplayGetFPS(&vps, &fps, &actual_fps);
	char fpsbuf[256];
	switch (g_Config.iShowFPSCounter) {
	case 1:
		snprintf(fpsbuf, sizeof(fpsbuf), "Speed: %0.1f%%", vps / (59.94f / 100.0f)); break;
	case 2:
		snprintf(fpsbuf, sizeof(fpsbuf), "FPS: %0.1f", actual_fps); break;
	case 3:
		snprintf(fpsbuf, sizeof(fpsbuf), "%0.0f/%0.0f (%0.1f%%)", actual_fps, fps, vps / (59.94f / 100.0f)); break;
	default:
		return;
	}

	draw2d->SetFontScale(0.7f, 0.7f);
	draw2d->DrawText(UBUNTU24, fpsbuf, bounds.x2() - 8, 12, 0xc0000000, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
	draw2d->DrawText(UBUNTU24, fpsbuf, bounds.x2() - 10, 10, 0xFF3fFF3f, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
	draw2d->SetFontScale(1.0f, 1.0f);
}

void EmuScreen::preRender() {
	using namespace Draw;
	DrawContext *draw = screenManager()->getDrawContext();
	draw->BeginFrame();
	// Here we do NOT bind the backbuffer or clear the screen, unless non-buffered.
	// The emuscreen is different than the others - we really want to allow the game to render to framebuffers
	// before we ever bind the backbuffer for rendering. On mobile GPUs, switching back and forth between render
	// targets is a mortal sin so it's very important that we don't bind the backbuffer unnecessarily here.
	// We only bind it in FramebufferManager::CopyDisplayToOutput (unless non-buffered)...
	// We do, however, start the frame in other ways.

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if ((!useBufferedRendering && !g_Config.bSoftwareRendering) || Core_IsStepping()) {
		// We need to clear here already so that drawing during the frame is done on a clean slate.
		DrawContext *draw = screenManager()->getDrawContext();
		draw->BindFramebufferAsRenderTarget(nullptr, { RPAction::CLEAR, RPAction::CLEAR, 0xFF000000 });

		Viewport viewport;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = pixel_xres;
		viewport.Height = pixel_yres;
		viewport.MaxDepth = 1.0;
		viewport.MinDepth = 0.0;
		draw->SetViewports(1, &viewport);
		draw->SetTargetSize(pixel_xres, pixel_yres);
	}
}

void EmuScreen::postRender() {
	Draw::DrawContext *draw = screenManager()->getDrawContext();
	if (!draw)
		return;
	draw->EndFrame();
}

void EmuScreen::render() {
	using namespace Draw;

	if (invalid_) {
		// It's possible this might be set outside PSP_RunLoopFor().
		// In this case, we need to double check it here.
		checkPowerDown();
		return;
	}

	if (PSP_CoreParameter().freezeNext) {
		PSP_CoreParameter().frozen = true;
		PSP_CoreParameter().freezeNext = false;
		SaveState::SaveToRam(freezeState_);
	} else if (PSP_CoreParameter().frozen) {
		if (CChunkFileReader::ERROR_NONE != SaveState::LoadFromRam(freezeState_)) {
			ERROR_LOG(SAVESTATE, "Failed to load freeze state. Unfreezing.");
			PSP_CoreParameter().frozen = false;
		}
	}

	PSP_BeginHostFrame();

	// We just run the CPU until we get to vblank. This will quickly sync up pretty nicely.
	// The actual number of cycles doesn't matter so much here as we will break due to CORE_NEXTFRAME, most of the time hopefully...
	int blockTicks = usToCycles(1000000 / 10);

	// Run until CORE_NEXTFRAME
	while (coreState == CORE_RUNNING) {
		PSP_RunLoopFor(blockTicks);
	}

	// Hopefully coreState is now CORE_NEXTFRAME
	if (coreState == CORE_NEXTFRAME) {
		// set back to running for the next frame
		coreState = CORE_RUNNING;
	}
	checkPowerDown();

	PSP_EndHostFrame();

	if (invalid_)
		return;
	
	// Here the backbuffer will always be bound.

	if (!osm.IsEmpty() || g_Config.bShowDebugStats || g_Config.iShowFPSCounter || g_Config.bShowTouchControls || g_Config.bShowDeveloperMenu || g_Config.bShowAudioDebug || saveStatePreview_->GetVisibility() != UI::V_GONE || g_Config.bShowFrameProfiler) {
		DrawContext *thin3d = screenManager()->getDrawContext();

		// This sets up some important states but not the viewport.
		screenManager()->getUIContext()->Begin();

		Viewport viewport;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = pixel_xres;
		viewport.Height = pixel_yres;
		viewport.MaxDepth = 1.0;
		viewport.MinDepth = 0.0;
		thin3d->SetViewports(1, &viewport);

		DrawBuffer *draw2d = screenManager()->getUIContext()->Draw();

		if (root_) {
			UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);
			root_->Draw(*screenManager()->getUIContext());
		}

		if (g_Config.bShowDebugStats) {
			DrawDebugStats(draw2d);
		}

		if (g_Config.bShowAudioDebug) {
			DrawAudioDebugStats(draw2d);
		}

		if (g_Config.iShowFPSCounter) {
			DrawFPS(draw2d, screenManager()->getUIContext()->GetBounds());
		}

#ifdef USE_PROFILER
		if (g_Config.bShowFrameProfiler) {
			DrawProfile(*screenManager()->getUIContext());
		}
#endif

		screenManager()->getUIContext()->End();
	}

	// We have no use for backbuffer depth or stencil, so let tiled renderers discard them after tiling.
	/*
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		GLenum attachments[2] = { GL_DEPTH, GL_STENCIL };
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
	} else if (!gl_extensions.GLES3) {
#ifdef USING_GLES2
		// Tiled renderers like PowerVR should benefit greatly from this. However - seems I can't call it?
		bool hasDiscard = gl_extensions.EXT_discard_framebuffer;  // TODO
		if (hasDiscard) {
			//const GLenum targets[3] = { GL_COLOR_EXT, GL_DEPTH_EXT, GL_STENCIL_EXT };
			//glDiscardFramebufferEXT(GL_FRAMEBUFFER, 3, targets);
		}
#endif
	}
	*/
}

void EmuScreen::deviceLost() {
	ILOG("EmuScreen::deviceLost()");
	if (gpu)
		gpu->DeviceLost();
}

void EmuScreen::deviceRestore() {
	ILOG("EmuScreen::deviceRestore()");
	if (gpu)
		gpu->DeviceRestore();

	RecreateViews();
}

void EmuScreen::autoLoad() {
	//check if save state has save, if so, load
	int lastSlot = SaveState::GetNewestSlot(gamePath_);
	if (g_Config.bEnableAutoLoad && lastSlot != -1) {
		SaveState::LoadSlot(gamePath_, lastSlot, &AfterSaveStateAction);
		g_Config.iCurrentStateSlot = lastSlot;
	}
}

// TODO: Add generic loss-of-focus handling for Screens, use this.
void EmuScreen::releaseButtons() {
	TouchInput input;
	input.flags = TOUCH_RELEASE_ALL;
	input.timestamp = time_now_d();
	input.id = 0;
	touch(input);
}

void EmuScreen::resized() {
	RecreateViews();
}
