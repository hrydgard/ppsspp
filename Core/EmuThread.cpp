#include "ppsspp_config.h"

#include <mutex>
#include <atomic>
#include <thread>

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/Application.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/Thread/ThreadUtil.h"

#include "Core/EmuThread.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"

enum class EmuThreadState {
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

static std::atomic<EmuThreadState> g_emuThreadState(EmuThreadState::STOPPED);
static std::atomic<bool> g_inLoop;

class GraphicsContext;

bool MainThread_Ready() {
	return g_inLoop;
}

static void EmuThreadFunc(GraphicsContext *graphicsContext, Application *application, std::function<bool (GraphicsContext *)> frame) {
	INFO_LOG(Log::G3D, "Entering separate emu thread");
	SetCurrentThreadName("EmuThread");

	g_emuThreadState = EmuThreadState::RUNNING;

	AndroidJNIThreadContext context;

	// This normally calls NativeInitGraphics()
	if (!application->InitGraphics(graphicsContext)) {
		_assert_msg_(false, "NativeInitGraphics failed, might as well bail");
		// If this fails, which it normally shouldn't, let's bail.
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	} else {
		INFO_LOG(Log::G3D, "EmuThread: Entering loop");
	}

	while (g_emuThreadState != EmuThreadState::QUIT_REQUESTED) {
		// We're here again, so the game quit.  Restart Run() which controls the UI.
		// This way they can load a new game.
		// This normally calls NativeFrame()
		if (!frame(graphicsContext)) {
			g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
		}
	}

	INFO_LOG(Log::System, "emuThreadState was set to QUIT_REQUESTED, left EmuThreadFunc loop. Setting state to STOPPED.");

	// This normally calls NativeShutdownGraphics()
	application->ShutdownGraphics(graphicsContext);
	delete application;

	INFO_LOG(Log::System, "Leaving separate emu thread");

	g_emuThreadState = EmuThreadState::STOPPED;
}

std::thread EmuThread_Start(GraphicsContext *graphicsContext, Application *application, std::function<bool(GraphicsContext *)> frame) {
	INFO_LOG(Log::System, "EmuTread_Start");
	_dbg_assert_(g_emuThreadState == EmuThreadState::STOPPED);
	std::thread emuThread = std::thread(&EmuThreadFunc, graphicsContext, application, frame);
	graphicsContext->ThreadStart();
	return emuThread;
}

// This is useful when the render thread is in control.
void EmuThread_RequestExit() {
	INFO_LOG(Log::System, "EmuTread_RequestExit");
	if (g_emuThreadState == EmuThreadState::RUNNING) {
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	} else {
		INFO_LOG(Log::System, "EmuTread_RequestExit: g_emuThreadState was not RUNNING, so not requesting exit.");
	}
}

void EmuThread_Join(GraphicsContext *graphicsContext, std::thread &emuThread) {
	INFO_LOG(Log::System, "EmuTread_Join");
	if (graphicsContext->NeedsSeparateEmuThread()) {
		EmuThread_RequestExit();
		while (graphicsContext->ThreadFrame()) {}
	}
	emuThread.join();
	emuThread = std::thread();
	graphicsContext->ThreadEnd();
}

bool RunMainLoop(GraphicsContext *graphicsContext, Application *application, std::function<bool(GraphicsContext *)> frame) {
	// This is the main loop for graphics context that handle their own threading.
	// InitFromRenderThread/ShutdownFromRenderThread are not used.

	application->InitGraphics(graphicsContext);

	g_inLoop = true;

	while (frame(graphicsContext)) {}

	// NOTE: Don't call stuff like Core_Stop here. On Android, we fully shut down graphics when you switch away from the app,
	// then boot it up again when returning. That means stopping this thread and restarting it.

	// Process the shutdown.  Without this, non-GL delays 800ms on shutdown. TODO: is this still an issue?
	Core_StateProcessed();

	g_inLoop = false;

	application->ShutdownGraphics(graphicsContext);
	delete application;
	return true;
}

// Call InitAPI and ShutdownAPI outside this!
bool MainThreadFunc(GraphicsContext *graphicsContext, Application *application, const WindowDesc &windowDesc, std::function<bool(GraphicsContext *)> frame, std::string *errorMessage) {
	// This is now the render thread, and will spawn the emu thread below.
	if (!graphicsContext->InitSurface(windowDesc.winsys, windowDesc.data1, windowDesc.data2, errorMessage)) {
		ERROR_LOG(Log::G3D, "MainThreadFunc: InitSurface failed: %s", errorMessage->c_str());
		delete application;
		return false;
	}
	if (graphicsContext->NeedsSeparateEmuThread()) {
		SetCurrentThreadName("RenderThread");

		g_inLoop = true;
		std::thread emuThread = EmuThread_Start(graphicsContext, application, frame);
		graphicsContext->ThreadStart();
		// This thread becomes the render thread. EmuThread will tell it when to quit by sending a message.
		while (graphicsContext->ThreadFrame()) {}
		EmuThread_Join(graphicsContext, emuThread);
		g_inLoop = false;

		graphicsContext->ThreadEnd();

		INFO_LOG(Log::System, "RenderThread - joined");

	} else {
		SetCurrentThreadName("MainThread");

		RunMainLoop(graphicsContext, application, frame);
	}
	graphicsContext->ShutdownSurface();
	return true;
}
