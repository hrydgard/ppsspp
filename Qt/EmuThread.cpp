#include "EmuThread.h"

#include <QThread>
#include <QElapsedTimer>

#include "Core/HLE/sceCtrl.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Host.h"
#include "Core/Core.h"
#include "math/lin/matrix4x4.h"
#include "native/ui/ui.h"
#include "android/jni/UIShader.h"
#include "android/jni/GamepadEmu.h"
#include "base/NativeApp.h"
#include "base/threadutil.h"
#include "gfx_es2/fbo.h"
#include "gfx_es2/gl_state.h"
#include "GPU/GPUState.h"

#include "qtemugl.h"
#include "QtHost.h"

void EmuThread_Start(QString filename, QtEmuGL* w)
{
	//_dbg_clear_();
	fileToStart = filename;
	glWindow = w;
	glWindow->start_rendering();
}

void EmuThread_Stop()
{
//	DSound_UpdateSound();
	Core_Stop();
	glWindow->stop_rendering();
	host->UpdateUI();
}

void EmuThread::init(InputState *inputState)
{
	input_state = inputState;
}

void EmuThread::run()
{
	running = true;
	setCurrentThreadName("EmuThread");

	g_State.bEmuThreadStarted = true;

	host->UpdateUI();
	host->InitGL();

	glWindow->makeCurrent();

#ifndef USING_GLES2
	glewInit();
#endif
	NativeInitGraphics();

	INFO_LOG(BOOT, "Starting up hardware.");

	CoreParameter coreParameter;
	coreParameter.fileToStart = fileToStart.toStdString();
	coreParameter.enableSound = true;
	coreParameter.gpuCore = GPU_GLES;
	coreParameter.cpuCore = (CPUCore)g_Config.iCpuCore;
	coreParameter.enableDebugging = true;
	coreParameter.printfEmuLog = false;
	coreParameter.headLess = false;
	coreParameter.renderWidth = 480 * g_Config.iWindowZoom;
	coreParameter.renderHeight = 272 * g_Config.iWindowZoom;
	coreParameter.outputWidth = dp_xres;
	coreParameter.outputHeight = dp_yres;
	coreParameter.pixelWidth = pixel_xres;
	coreParameter.pixelHeight = pixel_yres;
	coreParameter.startPaused = !g_Config.bAutoRun;

	std::string error_string;
	if (!PSP_Init(coreParameter, &error_string))
	{
		ERROR_LOG(BOOT, "Error loading: %s", error_string.c_str());
		FinalShutdown();
		return;
	}

	LayoutGamepad(dp_xres, dp_yres);

	_dbg_update_();

	host->UpdateDisassembly();
	Core_EnableStepping(coreParameter.startPaused ? TRUE : FALSE);

	g_State.bBooted = true;
#ifdef _DEBUG
	host->UpdateMemView();
#endif
	host->BootDone();

	QElapsedTimer timer;

	while(running) {
		//UpdateGamepad(*input_state);
		timer.start();

		UpdateInputState(input_state);

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
            if (input_state->pad_buttons_down & mapping[i][0]) {
				__CtrlButtonDown(mapping[i][1]);
			}
            if (input_state->pad_buttons_up & mapping[i][0]) {
				__CtrlButtonUp(mapping[i][1]);
			}
		}
		__CtrlSetAnalog(input_state->pad_lstick_x, input_state->pad_lstick_y);

		EndInputState(input_state);

		glstate.Restore();
		glViewport(0, 0, pixel_xres, pixel_yres);
		Matrix4x4 ortho;
		ortho.setOrtho(0.0f, dp_xres, dp_yres, 0.0f, -1.0f, 1.0f);
		glsl_bind(UIShader_Get());
		glUniformMatrix4fv(UIShader_Get()->u_worldviewproj, 1, GL_FALSE, ortho.getReadPtr());


		ReapplyGfxState();

		Core_Run();

		// Hopefully coreState is now CORE_NEXTFRAME
		if (coreState == CORE_NEXTFRAME) {
			// set back to running for the next frame
			coreState = CORE_RUNNING;

			qint64 time = timer.elapsed();
			const int frameTime = (1.0f/60.0f) * 1000;
			if(time < frameTime)
			{
				msleep(frameTime-time);
			}
		}
		timer.start();

		fbo_unbind();

		UIShader_Prepare();

		uiTexture->Bind(0);

		glViewport(0, 0, pixel_xres, pixel_yres);

		ui_draw2d.Begin(DBMODE_NORMAL);

		//if (g_Config.bShowTouchControls)
		//	DrawGamepad(ui_draw2d);

		glsl_bind(UIShader_Get());
		ui_draw2d.End();
		ui_draw2d.Flush(UIShader_Get());


		// Tiled renderers like PowerVR should benefit greatly from this. However - seems I can't call it?
#if defined(USING_GLES2)
		bool hasDiscard = false;  // TODO
		if (hasDiscard) {
			//glDiscardFramebuffer(GL_COLOR_EXT | GL_DEPTH_EXT | GL_STENCIL_EXT);
		}
#endif

		glWindow->swapBuffers();
	}
	glWindow->doneCurrent();
}

void EmuThread::Shutdown()
{
	host->PrepareShutdown();
	PSP_Shutdown();
	FinalShutdown();
}
void EmuThread::FinalShutdown()
{

	host->ShutdownGL();


	delete uiTexture;
	uiTexture = NULL;

	UIShader_Shutdown();

	gl_lost_manager_shutdown();

	// TODO
	//The CPU should return when a game is stopped and cleanup should be done here, 
	//so we can restart the plugins (or load new ones) for the next game
	g_State.bEmuThreadStarted = false;
	//_endthreadex(0);
	//return 0;
}

void EmuThread::setRunning(bool value)
{
	running = false;
}


