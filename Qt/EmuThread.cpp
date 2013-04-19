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
#include "UI/UIShader.h"
#include "UI/GamepadEmu.h"
#include "UI/ui_atlas.h"
#include "base/NativeApp.h"
#include "thread/threadutil.h"
#include "gfx_es2/fbo.h"
#include "gfx_es2/gl_state.h"
#include "GPU/GPUState.h"
#include "native/base/timeutil.h"
#include "native/base/colorutil.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

#include "qtemugl.h"
#include "QtHost.h"

namespace
{

	static const int symbols[4] = {
		I_CROSS,
		I_CIRCLE,
		I_SQUARE,
		I_TRIANGLE
	};

	static const uint32_t colors[4] = {
		/*
		0xFF6666FF, // blue
		0xFFFF6666, // red
		0xFFFF66FF, // pink
		0xFF66FF66, // green
		*/
		0xC0FFFFFF,
		0xC0FFFFFF,
		0xC0FFFFFF,
		0xC0FFFFFF,
	};

	static void DrawBackground(float alpha) {
		static float xbase[100] = {0};
		static float ybase[100] = {0};
		static int old_dp_xres = dp_xres;
		// if window was resized, recalculate animation coordinates
		if (xbase[0] == 0.0f || dp_xres != old_dp_xres) {
			old_dp_xres = dp_xres;
			GMRng rng;
			for (int i = 0; i < 100; i++) {
				xbase[i] = rng.F() * dp_xres;
				ybase[i] = rng.F() * dp_yres;
			}
		}
		glClearColor(0.1f,0.2f,0.43f,1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		ui_draw2d.DrawImageStretch(I_BG, 0, 0, dp_xres, dp_yres);
		float t = time_now();
		for (int i = 0; i < 100; i++) {
			float x = xbase[i];
			float y = ybase[i] + 40*cos(i * 7.2 + t * 1.3);
			float angle = sin(i + t);
			int n = i & 3;
			ui_draw2d.DrawImageRotated(symbols[n], x, y, 1.0f, angle, colorAlpha(colors[n], alpha * 0.1f));
		}
	}

}

void EmuThread_Start(QtEmuGL* w)
{
	//_dbg_clear_();
	glWindow = w;
	glWindow->doneCurrent();
	glWindow->start_rendering();
}

void EmuThread_Stop()
{
	if(glWindow)
	{
		glWindow->stop_rendering();
	}
	host->UpdateUI();
}

void EmuThread_StartGame(QString filename)
{
	if(glWindow)
	{
		glWindow->start_game(filename);
	}
}

void EmuThread_StopGame()
{
	if(glWindow)
	{
		glWindow->stop_game();
	}
}

void EmuThread_LockDraw(bool value)
{
	if(glWindow)
	{
		glWindow->LockDraw(value);
	}
}

QString GetCurrentFilename()
{
	return fileToStart;
}


EmuThread::EmuThread()
	: running(false)
	, gameRunning(false)
	, needInitGame(false)
	, frames_(0)
	, gameMutex( new QMutex(QMutex::Recursive))
	, mutexLockNum(0)
{
}

EmuThread::~EmuThread()
{
	delete gameMutex;
}

void EmuThread::init(InputState *inputState)
{
	input_state = inputState;
}

void EmuThread::run()
{
	running = true;
	setCurrentThreadName("EmuThread");

	host->UpdateUI();
	host->InitGL(0);

	EmuThread_LockDraw(true);

#ifndef USING_GLES2
	glewInit();
#endif
	NativeInitGraphics();

	INFO_LOG(BOOT, "Starting up hardware.");

	QElapsedTimer timer;

	EmuThread_LockDraw(false);

	while(running) {
		//UpdateGamepad(*input_state);
		timer.start();

		gameMutex->lock();
		bool gRun = gameRunning;
		gameMutex->unlock();

		if(gRun)
		{
			EmuThread_LockDraw(true);
			if(needInitGame)
			{
				CoreParameter coreParameter;
				coreParameter.fileToStart = fileToStart.toStdString();
				coreParameter.enableSound = true;
				coreParameter.gpuCore = GPU_GLES;
				coreParameter.cpuCore = g_Config.bJit ? CPU_JIT : CPU_INTERPRETER;
				coreParameter.enableDebugging = true;
				coreParameter.printfEmuLog = false;
				coreParameter.headLess = false;
				coreParameter.renderWidth = (480 * g_Config.iWindowZoom) * (g_Config.SSAntiAliasing + 1);
				coreParameter.renderHeight = (272 * g_Config.iWindowZoom) * (g_Config.SSAntiAliasing + 1);
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

				globalUIState = coreParameter.startPaused ? UISTATE_PAUSEMENU : UISTATE_INGAME;
			#ifdef _DEBUG
				host->UpdateMemView();
			#endif
				host->BootDone();
				needInitGame = false;
			}

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
					EmuThread_LockDraw(false);
					msleep(frameTime-time);
					EmuThread_LockDraw(true);
				}
				timer.start();
			}

			fbo_unbind();

			UIShader_Prepare();

			uiTexture->Bind(0);

			glViewport(0, 0, pixel_xres, pixel_yres);

			ui_draw2d.Begin(UIShader_Get(), DBMODE_NORMAL);

			//if (g_Config.bShowTouchControls)
			//	DrawGamepad(ui_draw2d);

			glsl_bind(UIShader_Get());
			ui_draw2d.End();
			ui_draw2d.Flush();


			// Tiled renderers like PowerVR should benefit greatly from this. However - seems I can't call it?
#if defined(USING_GLES2)
			bool hasDiscard = false;  // TODO
			if (hasDiscard) {
				//glDiscardFramebuffer(GL_COLOR_EXT | GL_DEPTH_EXT | GL_STENCIL_EXT);
			}
#endif
			glWindow->swapBuffers();
			EmuThread_LockDraw(false);
		}
		else
		{
			EmuThread_LockDraw(true);
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			time_update();
			float t = (float)frames_ / 60.0f;
			frames_++;

			float alpha = t;
			if (t > 1.0f) alpha = 1.0f;
			float alphaText = alpha;
			//if (t > 2.0f) alphaText = 3.0f - t;

			glstate.Restore();
			glViewport(0, 0, pixel_xres, pixel_yres);
			Matrix4x4 ortho;
			ortho.setOrtho(0.0f, dp_xres, dp_yres, 0.0f, -1.0f, 1.0f);
			glsl_bind(UIShader_Get());
			glUniformMatrix4fv(UIShader_Get()->u_worldviewproj, 1, GL_FALSE, ortho.getReadPtr());


			ReapplyGfxState();

			UIShader_Prepare();
			glViewport(0, 0, pixel_xres, pixel_yres);
			UIBegin(UIShader_Get());
			DrawBackground(alpha);

			ui_draw2d.SetFontScale(1.5f, 1.5f);
			ui_draw2d.DrawText(UBUNTU48, "PPSSPP", dp_xres / 2, dp_yres / 2 - 30, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
			ui_draw2d.SetFontScale(1.0f, 1.0f);
			ui_draw2d.DrawText(UBUNTU24, "Created by Henrik Rydg\u00E5rd", dp_xres / 2, dp_yres / 2 + 40, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
			ui_draw2d.DrawText(UBUNTU24, "Free Software under GPL 2.0", dp_xres / 2, dp_yres / 2 + 70, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
			ui_draw2d.DrawText(UBUNTU24, "www.ppsspp.org", dp_xres / 2, dp_yres / 2 + 130, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);

			UIEnd();
			globalUIState = UISTATE_MENU;

			glsl_bind(UIShader_Get());
			ui_draw2d.Flush();

			glWindow->swapBuffers();
			EmuThread_LockDraw(false);
			qint64 time = timer.elapsed();
			const int frameTime = (1.0f/60.0f) * 1000;
			if(time < frameTime)
			{
				msleep(frameTime-time);
			}
			timer.start();
		}

	}

	if(gameRunning)
	{
		stopGame();
	}

}

void EmuThread::Shutdown()
{
	FinalShutdown();
}
void EmuThread::FinalShutdown()
{

	host->ShutdownGL();

	delete uiTexture;
	uiTexture = NULL;

	UIShader_Shutdown();

	gl_lost_manager_shutdown();

	//_endthreadex(0);
	//return 0;
}

void EmuThread::setRunning(bool value)
{
	running = false;
}

void EmuThread::startGame(QString filename)
{
	gameMutex->lock();
	needInitGame = true;
	gameRunning = true;
	fileToStart = filename;
	gameMutex->unlock();

}

void EmuThread::stopGame()
{
	Core_Stop();
	gameMutex->lock();
	gameRunning = false;

	PSP_Shutdown();

	// TODO
	//The CPU should return when a game is stopped and cleanup should be done here,
	//so we can restart the plugins (or load new ones) for the next game
	frames_ = 0;

	gameMutex->unlock();
}

void EmuThread::LockGL(bool lock)
{
	if(lock)
	{
		gameMutex->lock();
		if(mutexLockNum == 0)
			glWindow->makeCurrent();
		mutexLockNum++;
	}
	else
	{
		mutexLockNum--;
		if(mutexLockNum == 0)
			glWindow->doneCurrent();
		gameMutex->unlock();
	}
}


