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

#include "android/app-android.h"
#include "base/logging.h"

#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/fbo.h"

#include "input/input_state.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "i18n/i18n.h"

#include "Common/KeyMap.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/Debugger/SymbolMap.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "UI/GamepadEmu.h"
#include "UI/UIShader.h"

#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/DevScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"


EmuScreen::EmuScreen(const std::string &filename)
	: booted_(false), gamePath_(filename), invalid_(true), pauseTrigger_(false) {
}

void EmuScreen::bootGame(const std::string &filename) {
	booted_ = true;
	std::string fileToStart = filename;
	// This is probably where we should start up the emulated PSP.
	INFO_LOG(BOOT, "Starting up hardware.");

	CoreParameter coreParam;
	coreParam.cpuCore = g_Config.bJit ? CPU_JIT : CPU_INTERPRETER;
	coreParam.gpuCore = g_Config.bSoftwareRendering ? GPU_SOFTWARE : GPU_GLES;
	coreParam.enableSound = g_Config.bEnableSound;
	coreParam.fileToStart = fileToStart;
	coreParam.mountIso = "";
	coreParam.startPaused = false;
	coreParam.enableDebugging = false;
	coreParam.printfEmuLog = false;
	coreParam.headLess = false;
#ifndef _WIN32
	if (g_Config.iWindowZoom < 1 || g_Config.iWindowZoom > 2)
		g_Config.iWindowZoom = 1;
#endif
	coreParam.renderWidth = 480 * g_Config.iWindowZoom;
	coreParam.renderHeight = 272 * g_Config.iWindowZoom;
	coreParam.outputWidth = dp_xres;
	coreParam.outputHeight = dp_yres;
	coreParam.pixelWidth = pixel_xres;
	coreParam.pixelHeight = pixel_yres;
	if (g_Config.bAntiAliasing) {
		coreParam.renderWidth *= 2;
		coreParam.renderHeight *= 2;
	}
	std::string error_string;
	if (PSP_Init(coreParam, &error_string)) {
		invalid_ = false;
	} else {
		invalid_ = true;
		errorMessage_ = error_string;
		ERROR_LOG(BOOT, "%s", errorMessage_.c_str());
		return;
	}

	globalUIState = UISTATE_INGAME;
	host->BootDone();
	host->UpdateDisassembly();

	g_gameInfoCache.FlushBGs();

	NOTICE_LOG(BOOT, "Loading %s...", fileToStart.c_str());
	I18NCategory *s = GetI18NCategory("Screen"); 

#ifdef _WIN32
	if (g_Config.bFirstRun) {
		osm.Show(s->T("PressESC", "Press ESC to open the pause menu"), 3.0f);
	}
#endif
	memset(virtKeys, 0, sizeof(virtKeys));
}

EmuScreen::~EmuScreen() {
	if (!invalid_) {
		// If we were invalid, it would already be shutdown.
		PSP_Shutdown();
	}
}

void EmuScreen::dialogFinished(const Screen *dialog, DialogResult result) {

	// TODO: improve the way with which we got commands from PauseMenu.
	// DR_CANCEL means clicked on "continue", DR_OK means clicked on "back to menu",
	// DR_YES means a message sent to PauseMenu by NativeMessageReceived.
	if (result == DR_OK) {
		screenManager()->switchScreen(new MainScreen());
	}
	RecreateViews();
}

void EmuScreen::sendMessage(const char *message, const char *value) {
	// External commands, like from the Windows UI.
	if (!strcmp(message, "pause")) {
		screenManager()->push(new GamePauseScreen(gamePath_));
	} else if (!strcmp(message, "stop")) {
		screenManager()->switchScreen(new MainScreen());
	} else if (!strcmp(message, "reset")) {
		PSP_Shutdown();
		std::string resetError;
		if (!PSP_Init(PSP_CoreParameter(), &resetError)) {
			ELOG("Error resetting: %s", resetError.c_str());
			screenManager()->switchScreen(new MainScreen());
			return;
		}
		host->BootDone();
		host->UpdateDisassembly();
#ifdef _WIN32
		if (g_Config.bAutoRun) {
			Core_EnableStepping(false);
		} else {
			Core_EnableStepping(true);
		}
#endif
	}
	else if (!strcmp(message, "boot")) {
		PSP_Shutdown();
		bootGame(value);
	}
	else if (!strcmp(message, "control mapping")) {
		screenManager()->push(new ControlMappingScreen());
	}
	else if (!strcmp(message, "settings")) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	}
}

inline float curve1(float x) {
	const float deadzone = 0.15f;
	const float factor = 1.0f / (1.0f - deadzone);
	if (x > deadzone) {
		return (x - deadzone) * (x - deadzone) * factor;
	} else if (x < -0.1f) {
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

void EmuScreen::touch(const TouchInput &touch) {
	if (root_)
		root_->Touch(touch);
}

void EmuScreen::onVKeyDown(int virtualKeyCode) {
	I18NCategory *s = GetI18NCategory("Screen"); 

	switch (virtualKeyCode) {
	case VIRTKEY_UNTHROTTLE:
		PSP_CoreParameter().unthrottle = true;
		break;

	case VIRTKEY_SPEED_TOGGLE:
		if (PSP_CoreParameter().fpsLimit == 0) {
			PSP_CoreParameter().fpsLimit = 1;
			osm.Show(s->T("fixed", "Speed: alternate"), 1.0);
		}
		else if (PSP_CoreParameter().fpsLimit == 1) {
			PSP_CoreParameter().fpsLimit = 0;
			osm.Show(s->T("standard", "Speed: standard"), 1.0);
		}
		break;

	case VIRTKEY_PAUSE:
		screenManager()->push(new GamePauseScreen(gamePath_));
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

	default:
		break;
	}
}

inline void EmuScreen::setVKeyAnalogX(int stick, int virtualKeyMin, int virtualKeyMax) {
	float axis = 0.0f;
	// The down events can repeat, so just trust the virtKeys array.
	if (virtKeys[virtualKeyMin - VIRTKEY_FIRST])
		axis -= 1.0f;
	if (virtKeys[virtualKeyMax - VIRTKEY_FIRST])
		axis += 1.0f;
	__CtrlSetAnalogX(axis, stick);
}

inline void EmuScreen::setVKeyAnalogY(int stick, int virtualKeyMin, int virtualKeyMax) {
	float axis = 0.0f;
	if (virtKeys[virtualKeyMin - VIRTKEY_FIRST])
		axis -= 1.0f;
	if (virtKeys[virtualKeyMax - VIRTKEY_FIRST])
		axis += 1.0f;
	__CtrlSetAnalogY(axis, stick);
}

void EmuScreen::key(const KeyInput &key) {
	if (key.keyCode == NKCODE_BACK)
		pauseTrigger_ = true;

	int result = KeyMap::KeyToPspButton(key.deviceId, key.keyCode);
	if (result == KEYMAP_ERROR_UNKNOWN_KEY)
		return;

	pspKey(result, key.flags);
}

void EmuScreen::pspKey(int pspKeyCode, int flags) {
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

void EmuScreen::axis(const AxisInput &axis) {
	if (axis.value > 0) {
		processAxis(axis, 1);
	} else if (axis.value < 0) {
		processAxis(axis, -1);
	} else if (axis.value == 0) {
		// Both directions! Prevents sticking for digital input devices that are axises (like HAT)
		processAxis(axis, 1);
		processAxis(axis, -1);
	}
}

void EmuScreen::processAxis(const AxisInput &axis, int direction) {
	int result = KeyMap::AxisToPspButton(axis.deviceId, axis.axisId, direction);
	if (result == KEYMAP_ERROR_UNKNOWN_KEY)
		return;

	switch (result) {
	case VIRTKEY_AXIS_X_MIN:
		__CtrlSetAnalogX(-fabs(axis.value), CTRL_STICK_LEFT);
		break;
	case VIRTKEY_AXIS_X_MAX:
		__CtrlSetAnalogX(fabs(axis.value), CTRL_STICK_LEFT);
		break;
	case VIRTKEY_AXIS_Y_MIN:
		__CtrlSetAnalogY(-fabs(axis.value), CTRL_STICK_LEFT);
		break;
	case VIRTKEY_AXIS_Y_MAX:
		__CtrlSetAnalogY(fabs(axis.value), CTRL_STICK_LEFT);
		break;

	case VIRTKEY_AXIS_RIGHT_X_MIN:
		__CtrlSetAnalogX(-fabs(axis.value), CTRL_STICK_RIGHT);
		break;
	case VIRTKEY_AXIS_RIGHT_X_MAX:
		__CtrlSetAnalogX(fabs(axis.value), CTRL_STICK_RIGHT);
		break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
		__CtrlSetAnalogY(-fabs(axis.value), CTRL_STICK_RIGHT);
		break;
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		__CtrlSetAnalogY(fabs(axis.value), CTRL_STICK_RIGHT);
		break;

	default:
		if (axis.value >= AXIS_BIND_THRESHOLD || axis.value <= -AXIS_BIND_THRESHOLD) {
			pspKey(result, KEY_DOWN);

			// Also unpress the other direction.
			result = KeyMap::AxisToPspButton(axis.deviceId, axis.axisId, axis.value >= 0 ? -1 : 1);
			if (result != KEYMAP_ERROR_UNKNOWN_KEY)
				pspKey(result, KEY_UP);
		} else {
			pspKey(result, KEY_UP);
		}
	}
}


// TODO: Get rid of this.
static const struct { int from, to; } legacy_touch_mapping[12] = {
	{PAD_BUTTON_A, CTRL_CROSS},
	{PAD_BUTTON_B, CTRL_CIRCLE},
	{PAD_BUTTON_X, CTRL_SQUARE},
	{PAD_BUTTON_Y, CTRL_TRIANGLE},
	{PAD_BUTTON_START, CTRL_START},
	{PAD_BUTTON_SELECT, CTRL_SELECT},
	{PAD_BUTTON_LBUMPER, CTRL_LTRIGGER},
	{PAD_BUTTON_RBUMPER, CTRL_RTRIGGER},
	{PAD_BUTTON_UP, CTRL_UP},
	{PAD_BUTTON_RIGHT, CTRL_RIGHT},
	{PAD_BUTTON_DOWN, CTRL_DOWN},
	{PAD_BUTTON_LEFT, CTRL_LEFT},
};

void EmuScreen::CreateViews() {
	root_ = CreatePadLayout(&pauseTrigger_);
	if (g_Config.bShowDeveloperMenu) {
		root_->Add(new UI::Button("DevMenu"))->OnClick.Handle(this, &EmuScreen::OnDevTools);
	}
}

UI::EventReturn EmuScreen::OnDevTools(UI::EventParams &params) {
	screenManager()->push(new DevMenu());
	return UI::EVENT_DONE;
}

void EmuScreen::update(InputState &input) {
	if (!booted_)
		bootGame(gamePath_);

	UIScreen::update(input);

	// Simply forcibily update to the current screen size every frame. Doesn't cost much.
	PSP_CoreParameter().outputWidth = dp_xres;
	PSP_CoreParameter().outputHeight = dp_yres;
	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;

	globalUIState = UISTATE_INGAME;
	
	if (errorMessage_.size()) {
		I18NCategory *g = GetI18NCategory("Error");
		std::string errLoadingFile = g->T("Error loading file");
		errLoadingFile.append(" ");
		errLoadingFile.append(g->T(errorMessage_.c_str()));

		screenManager()->push(new PromptScreen(
			errLoadingFile, "OK", ""));
		errorMessage_ = "";
		return;
	}

	if (invalid_)
		return;

	float leftstick_x = 0.0f;
	float leftstick_y = 0.0f;
	float rightstick_x = 0.0f;
	float rightstick_y = 0.0f;

	// Virtual keys.
	__CtrlSetRapidFire(virtKeys[VIRTKEY_RAPID_FIRE - VIRTKEY_FIRST]);

	// Apply tilt to left stick
	if (g_Config.bAccelerometerToAnalogHoriz) {
		// TODO: Deadzone, etc.
		leftstick_x += clamp1(curve1(input.acc.y) * 2.0f);
		__CtrlSetAnalogX(clamp1(leftstick_x), CTRL_STICK_LEFT);
	}

	// Make sure fpsLimit starts at 0
	if (PSP_CoreParameter().fpsLimit != 0 && PSP_CoreParameter().fpsLimit != 1) {
		PSP_CoreParameter().fpsLimit = 0;
	}

	// This is here to support the iOS on screen back button.
	if (pauseTrigger_) {
		pauseTrigger_ = false;
		screenManager()->push(new GamePauseScreen(gamePath_));
	}
}

void EmuScreen::render() {
	if (invalid_)
		return;

	// Reapply the graphics state of the PSP
	ReapplyGfxState();

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
	} else if (coreState == CORE_POWERDOWN)	{
		ILOG("SELF-POWERDOWN!");
		screenManager()->switchScreen(new MainScreen());
	}

	if (invalid_)
		return;

	bool useBufferedRendering = g_Config.iRenderingMode != 0 ? 1 : 0;
	if (useBufferedRendering)
		fbo_unbind();

	UIShader_Prepare();

	uiTexture->Bind(0);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
	glstate.viewport.restore();

	ui_draw2d.Begin(UIShader_Get(), DBMODE_NORMAL);

	float touchOpacity = g_Config.iTouchButtonOpacity / 100.0f;

	if (root_) {
		UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);
		root_->Draw(*screenManager()->getUIContext());
	}

	if (!osm.IsEmpty()) {
		osm.Draw(ui_draw2d);
	}

	if (g_Config.bShowDebugStats) {
		char statbuf[4096] = {0};
		__DisplayGetDebugStats(statbuf);
		if (statbuf[4095]) {
			ELOG("Statbuf too small! :(");
		}
		ui_draw2d.SetFontScale(.7f, .7f);
		ui_draw2d.DrawText(UBUNTU24, statbuf, 11, 11, 0xc0000000, FLAG_DYNAMIC_ASCII);
		ui_draw2d.DrawText(UBUNTU24, statbuf, 10, 10, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
		ui_draw2d.SetFontScale(1.0f, 1.0f);
	}

	if (g_Config.iShowFPSCounter) {
		float vps, fps, actual_fps;
		__DisplayGetFPS(&vps, &fps, &actual_fps);
		char fpsbuf[256];
		switch (g_Config.iShowFPSCounter) {
		case 1:
			sprintf(fpsbuf, "Speed: %0.1f%%", vps / 60.0f * 100.0f); break;
		case 2:
			sprintf(fpsbuf, "FPS: %0.1f", actual_fps); break;
		case 3:
			sprintf(fpsbuf, "%0.0f/%0.0f (%0.1f%%)", actual_fps, fps, vps / 60.0f * 100.0f); break;
		}
		ui_draw2d.SetFontScale(0.7f, 0.7f);
		ui_draw2d.DrawText(UBUNTU24, fpsbuf, dp_xres - 8, 12, 0xc0000000, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
		ui_draw2d.DrawText(UBUNTU24, fpsbuf, dp_xres - 10, 10, 0xFF3fFF3f, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
		ui_draw2d.SetFontScale(1.0f, 1.0f);
	}
	
	glsl_bind(UIShader_Get());
	ui_draw2d.End();
	ui_draw2d.Flush();

	// Tiled renderers like PowerVR should benefit greatly from this. However - seems I can't call it?
#if defined(USING_GLES2)
	bool hasDiscard = gl_extensions.EXT_discard_framebuffer;  // TODO
	if (hasDiscard) {
		//const GLenum targets[3] = { GL_COLOR_EXT, GL_DEPTH_EXT, GL_STENCIL_EXT };
		//glDiscardFramebufferEXT(GL_FRAMEBUFFER, 3, targets);
	}
#endif
}

void EmuScreen::deviceLost() {
	ILOG("EmuScreen::deviceLost()");
	if (gpu)
		gpu->DeviceLost();
}
