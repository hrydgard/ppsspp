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

static void EmuThreadFunc(GraphicsContext *graphicsContext, Application *application, std::function<void()> postFrame) {
	INFO_LOG(Log::G3D, "Entering separate emu thread");
	SetCurrentThreadName("EmuThread");

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
		application->Frame(graphicsContext);
		if (postFrame) {
			postFrame();
		}
		if (GetUIState() == UISTATE_EXIT) {
			g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
		}
	}

	INFO_LOG(Log::System, "emuThreadState was set to QUIT_REQUESTED, left EmuThreadFunc loop. Setting state to STOPPED.");

	g_emuThreadState = EmuThreadState::STOPPED;

	// This normally calls NativeShutdownGraphics()
	application->ShutdownGraphics(graphicsContext);
	delete application;

	INFO_LOG(Log::System, "Leaving separate emu thread");
}

std::thread EmuThread_Start(GraphicsContext *graphicsContext, Application *application, std::function<void()> postFrame) {
	_dbg_assert_(g_emuThreadState == EmuThreadState::STOPPED);
	g_emuThreadState = EmuThreadState::RUNNING;
	std::thread emuThread = std::thread(&EmuThreadFunc, graphicsContext, application, postFrame);
	graphicsContext->ThreadStart();
	return emuThread;
}

void EmuThread_Join(GraphicsContext *graphicsContext, std::thread &emuThread) {
	const EmuThreadState state = g_emuThreadState;
	if (state != EmuThreadState::QUIT_REQUESTED &&
		state != EmuThreadState::STOPPED) {
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	}
	_dbg_assert_(emuThread.joinable());
	if (graphicsContext->NeedsSeparateEmuThread()) {
		graphicsContext->ThreadFrameUntilCondition([] {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			return g_emuThreadState == EmuThreadState::STOPPED;
		});
	}
	graphicsContext->ThreadEnd();
	emuThread.join();
	emuThread = std::thread();
}

bool RunMainLoop(GraphicsContext *graphicsContext, Application *application, std::function<bool()> runCondition, std::function<void()> postFrame) {
	// This is the main thread. the graphics contexts will spawn and handle its own threads if needed.
	// InitFromRenderThread/ShutdownFromRenderThread are not used.

	application->InitGraphics(graphicsContext);
	// NativeResized();

	DEBUG_LOG(Log::Boot, "Done.");

	g_inLoop = true;

	while (runCondition()) {
		// We're here again, so the game quit.  Restart Run() which controls the UI.
		// This way they can load a new game.
		application->Frame(graphicsContext);
		postFrame();
	}
	Core_Stop();

	// Process the shutdown.  Without this, non-GL delays 800ms on shutdown.
	Core_StateProcessed();
	application->Frame(graphicsContext);

	g_inLoop = false;

	application->ShutdownGraphics(graphicsContext);
	delete application;
	return true;
}

// Call InitAPI and ShutdownAPI outside this!
bool MainThreadFunc(GraphicsContext *graphicsContext, Application *application, WindowSystem windowSystem, void *windowData1, void *windowData2, std::function<void()> postFrame) {
	// This is now the render thread, and will spawn the emu thread below.
	std::string error_string;
	bool success = graphicsContext->InitSurface(windowSystem, windowData1, windowData2, &error_string);
	if (!success) {
		return false;
	}

	std::string errorMessage;
	if (graphicsContext->NeedsSeparateEmuThread()) {
		SetCurrentThreadName("RenderThread");

		DEBUG_LOG(Log::Boot, "Done.");

		g_inLoop = true;
		std::thread emuThread = EmuThread_Start(graphicsContext, application, postFrame);

		graphicsContext->ThreadStart();
		// This thread becomes the render thread.
		while (true) {
			if (equals_any(g_emuThreadState, EmuThreadState::QUIT_REQUESTED, EmuThreadState::STOPPED)) {
				break;
			}
			graphicsContext->ThreadFrame(true);
			if (GetUIState() == UISTATE_EXIT) {
				break;
			}
		}
		Core_Stop();
		g_inLoop = false;

		EmuThread_Join(graphicsContext, emuThread);

		graphicsContext->ThreadEnd();

		INFO_LOG(Log::System, "RenderThread - joined");

	} else {
		SetCurrentThreadName("MainThread");

		RunMainLoop(graphicsContext, application, []() { return GetUIState() != UISTATE_EXIT; }, postFrame);
	}

	graphicsContext->ShutdownSurface();
	return true;
}
