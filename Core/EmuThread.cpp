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
#include "Common/System/Display.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Common/VR/PPSSPPVR.h"

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

void RunGraphicsLoop(GraphicsContext *graphicsContext, Application *application, std::function<bool(GraphicsContext *)> frame, std::function<bool()> shouldExit) {
	if (!graphicsContext->NeedsSeparateEmuThread()) {
		// The backend spawns its own render thread, so this thread is simply where emulation runs.
		SetCurrentThreadName("EmuThread");
		RunMainLoop(graphicsContext, application, frame);
		return;
	}

	// OpenGL wants its API calls on the thread its context is current on, which is this one. So
	// this thread becomes the render thread - all it does is execute the GPU commands the emu
	// thread queues up - and emulation moves to a thread we spawn here.
	SetCurrentThreadName("RenderThread");

	g_inLoop = true;

	// EmuThread_Start calls ThreadStart() for us, and EmuThread_Join calls ThreadEnd(), so neither
	// is called directly here.
	std::thread emuThread = EmuThread_Start(graphicsContext, application, frame);

	if (IsVREnabled()) {
		static bool vrFirstStart = true;
		EnterVR(vrFirstStart);
		vrFirstStart = false;
	}

	// Normally ThreadFrame() returning false is what gets us out of here - the frame callback
	// decides emulation is done, and the emu thread's NotifyEmuThreadExit then queues the exit.
	// That isn't enough in VR: while the session is idle we skip ThreadFrame() entirely, and going
	// to the background is precisely when the session goes idle, so the exit would never be seen
	// and whoever is waiting to join this thread would wait forever. Hence shouldExit.
	while (!(shouldExit && shouldExit())) {
		if (IsVREnabled() && !StartVRRender()) {
			// The session isn't active, so there's no frame to render into. Nothing to do until it
			// comes back - wait out a frame rather than spinning on it.
			sleep_ms(16, "vr-session-idle");
			continue;
		}
		if (!graphicsContext->ThreadFrame()) {
			break;
		}
		if (IsVREnabled()) {
			UpdateVRInput(g_Config.bHapticFeedback, g_display.dpi_scale_x, g_display.dpi_scale_y);
			FinishVRRender();
		}
	}

	// Also drains whatever the emu thread still had queued, so it can't get stuck waiting on us.
	EmuThread_Join(graphicsContext, emuThread);

	g_inLoop = false;

	INFO_LOG(Log::System, "RenderThread - joined");
}

// Call InitAPI and ShutdownAPI outside this!
bool MainThreadFunc(GraphicsContext *graphicsContext, Application *application, const WindowDesc &windowDesc, std::function<bool(GraphicsContext *)> frame, std::string *errorMessage) {
	if (!graphicsContext->InitSurface(windowDesc.winsys, windowDesc.data1, windowDesc.data2, errorMessage)) {
		ERROR_LOG(Log::G3D, "MainThreadFunc: InitSurface failed: %s", errorMessage->c_str());
		delete application;
		return false;
	}
	RunGraphicsLoop(graphicsContext, application, frame);
	graphicsContext->ShutdownSurface();
	return true;
}
