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

#include "base/logging.h"

#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "gfx_es2/fbo.h"

#include "input/input_state.h"
#include "ui/ui.h"
#include "i18n/i18n.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/Debugger/SymbolMap.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "UI/GamepadEmu.h"
#include "UI/UIShader.h"

#include "UI/MenuScreens.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"

EmuScreen::EmuScreen(const std::string &filename) : invalid_(true) {
	CheckGLExtensions();
	std::string fileToStart = filename;
	// This is probably where we should start up the emulated PSP.
	INFO_LOG(BOOT, "Starting up hardware.");

	CoreParameter coreParam;
	coreParam.cpuCore = g_Config.bJit ? CPU_JIT : CPU_INTERPRETER;
	coreParam.gpuCore = GPU_GLES;
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
	if (g_Config.SSAntiAliasing) {
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

	LayoutGamepad(dp_xres, dp_yres);

	g_gameInfoCache.FlushBGs();

	NOTICE_LOG(BOOT, "Loading %s...", fileToStart.c_str());
	I18NCategory *s = GetI18NCategory("Screen"); 

#ifdef _WIN32
	if (g_Config.bFirstRun) {
		osm.Show(s->T("PressESC", "Press ESC to open the pause menu"), 3.0f);
	}
#endif
}

EmuScreen::~EmuScreen() {
	if (!invalid_) {
		// If we were invalid, it would already be shutdown.
		PSP_Shutdown();
	}
}

void EmuScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_OK) {
		screenManager()->switchScreen(new MenuScreen());
	}
}

void EmuScreen::sendMessage(const char *message, const char *value) {
	// External commands, like from the Windows UI.
	if (!strcmp(message, "pause")) {
		screenManager()->push(new PauseScreen());
	} else if (!strcmp(message, "stop")) {
		screenManager()->switchScreen(new MenuScreen());
	} else if (!strcmp(message, "reset")) {
		PSP_Shutdown();
		std::string resetError;
		if (!PSP_Init(PSP_CoreParameter(), &resetError)) {
			ELOG("Error resetting: %s", resetError.c_str());
			screenManager()->switchScreen(new MenuScreen());
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

void EmuScreen::update(InputState &input) {
	globalUIState = UISTATE_INGAME;
	if (errorMessage_.size()) {
		screenManager()->push(new ErrorScreen(
			"Error loading file",
			errorMessage_));
		errorMessage_ = "";
		return;
	}

	if (invalid_)
		return;

	// First translate touches into native pad input.
	// Do this no matter the value of g_Config.bShowTouchControls, some people
	// like to use invisible controls...
	// Don't force on platforms that likely don't have a touchscreen, like Win32, OSX, and Linux...
	// TODO: What are good ifdefs for OSX and Linux, without breaking other mobile platforms?
#ifdef _WIN32
	if(g_Config.bShowTouchControls) {
#endif
		UpdateGamepad(input);

		UpdateInputState(&input);
#ifdef _WIN32
	}
#endif

	// Then translate pad input into PSP pad input. Also, add in tilt.
	static const int mapping[12][2] = {
		{PAD_BUTTON_A, CTRL_CROSS},
		{PAD_BUTTON_B, CTRL_CIRCLE},
		{PAD_BUTTON_X, CTRL_SQUARE},
		{PAD_BUTTON_Y, CTRL_TRIANGLE},
		{PAD_BUTTON_UP, CTRL_UP},
		{PAD_BUTTON_DOWN, CTRL_DOWN},
		{PAD_BUTTON_LEFT, CTRL_LEFT},
		{PAD_BUTTON_RIGHT, CTRL_RIGHT},
		{PAD_BUTTON_LBUMPER, CTRL_LTRIGGER},
		{PAD_BUTTON_RBUMPER, CTRL_RTRIGGER},
		{PAD_BUTTON_START, CTRL_START},
		{PAD_BUTTON_SELECT, CTRL_SELECT},
	};

	for (int i = 0; i < 12; i++) {
		if (input.pad_buttons_down & mapping[i][0]) {
			__CtrlButtonDown(mapping[i][1]);
		}
		if (input.pad_buttons_up & mapping[i][0]) {
			__CtrlButtonUp(mapping[i][1]);
		}
	}

	float stick_x = input.pad_lstick_x;
	float stick_y = input.pad_lstick_y;
	float rightstick_x = input.pad_rstick_x;
	float rightstick_y = input.pad_rstick_y;
	
	I18NCategory *s = GetI18NCategory("Screen"); 

	// Apply tilt to left stick
	if (g_Config.bAccelerometerToAnalogHoriz) {
		// TODO: Deadzone, etc.
		stick_x += clamp1(curve1(input.acc.y) * 2.0f);
		stick_x = clamp1(stick_x);
	}

	__CtrlSetAnalog(stick_x, stick_y, 0);
	__CtrlSetAnalog(rightstick_x, rightstick_x, 1);

	if (PSP_CoreParameter().fpsLimit != 2) {
		// Don't really need to show these, it's pretty obvious what unthrottle does,
		// in contrast to the three state toggle
		/*
		if (input.pad_buttons_down & PAD_BUTTON_UNTHROTTLE) {
			osm.Show(s->T("unlimited", "Speed: unlimited!"), 1.0, 0x50E0FF);
		}
		if (input.pad_buttons_up & PAD_BUTTON_UNTHROTTLE) {
			osm.Show(s->T("standard", "Speed: standard"), 1.0);
		}*/
	}
	if (input.pad_buttons & PAD_BUTTON_UNTHROTTLE) {
		PSP_CoreParameter().unthrottle = true;
	} else {
		PSP_CoreParameter().unthrottle = false;
	}
	// Make sure fpsLimit starts at 0
	if (PSP_CoreParameter().fpsLimit != 0 && PSP_CoreParameter().fpsLimit != 1 && PSP_CoreParameter().fpsLimit != 2) {
		PSP_CoreParameter().fpsLimit = 0;
	}

	//Toggle between 3 different states of fpsLimit
	if (input.pad_buttons_down & PAD_BUTTON_LEFT_THUMB) {
		if (PSP_CoreParameter().fpsLimit == 0) {
			PSP_CoreParameter().fpsLimit = 1;
			osm.Show(s->T("fixed", "Speed: fixed"), 1.0);
		}
		else if (PSP_CoreParameter().fpsLimit == 1) {
			PSP_CoreParameter().fpsLimit = 2;
			osm.Show(s->T("unlimited", "Speed: unlimited!"), 1.0, 0x50E0FF);
		}
		else if (PSP_CoreParameter().fpsLimit == 2) {
			PSP_CoreParameter().fpsLimit = 0;
			osm.Show(s->T("standard", "Speed: standard"), 1.0);
		}
	}

	if (input.pad_buttons_down & (PAD_BUTTON_MENU | PAD_BUTTON_BACK | PAD_BUTTON_RIGHT_THUMB)) {
		if (g_Config.bBufferedRendering)
			fbo_unbind();
		screenManager()->push(new PauseScreen());
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
		u64 nowTicks = CoreTiming::GetTicks();
		mipsr4k.RunLoopUntil(nowTicks + blockTicks);
	}
	// Hopefully coreState is now CORE_NEXTFRAME
	if (coreState == CORE_NEXTFRAME) {
		// set back to running for the next frame
		coreState = CORE_RUNNING;
	} else if (coreState == CORE_POWERDOWN)	{
		ILOG("SELF-POWERDOWN!");
		screenManager()->switchScreen(new MenuScreen());
	}

	if (invalid_)
		return;

	if (g_Config.bBufferedRendering)
		fbo_unbind();

	UIShader_Prepare();

	uiTexture->Bind(0);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
	glstate.viewport.restore();

	ui_draw2d.Begin(UIShader_Get(), DBMODE_NORMAL);

	float touchOpacity = g_Config.iTouchButtonOpacity / 100.0f;
	if (g_Config.bShowTouchControls)
		DrawGamepad(ui_draw2d, touchOpacity);

	DrawWatermark();

	if (!osm.IsEmpty()) {
		osm.Draw(ui_draw2d);
	}

	if (g_Config.bShowDebugStats) {
		char statbuf[4096] = {0};
		__DisplayGetDebugStats(statbuf);
		if (statbuf[4095])
			ERROR_LOG(HLE, "Statbuf too big");
		ui_draw2d.SetFontScale(.7f, .7f);
		ui_draw2d.DrawText(UBUNTU24, statbuf, 11, 11, 0xc0000000);
		ui_draw2d.DrawText(UBUNTU24, statbuf, 10, 10, 0xFFFFFFFF);
		ui_draw2d.SetFontScale(1.0f, 1.0f);
	}

	if (g_Config.iShowFPSCounter) {
		float vps, fps;
		__DisplayGetFPS(&vps, &fps);
		char fpsbuf[256];
		switch (g_Config.iShowFPSCounter) {
		case 1:
			sprintf(fpsbuf, "VPS: %0.1f", vps); break;
		case 2:
			sprintf(fpsbuf, "FPS: %0.1f", fps); break;
		case 3:
			sprintf(fpsbuf, "VPS: %0.1f\nFPS: %0.1f", vps, fps); break;
		}
		ui_draw2d.DrawText(UBUNTU24, fpsbuf, dp_xres - 8, 12, 0xc0000000, ALIGN_TOPRIGHT);
		ui_draw2d.DrawText(UBUNTU24, fpsbuf, dp_xres - 10, 10, 0xFF3fFF3f, ALIGN_TOPRIGHT);
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
	gpu->DeviceLost();
}
