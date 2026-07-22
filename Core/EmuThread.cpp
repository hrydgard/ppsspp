#include "ppsspp_config.h"

#include <mutex>
#include <atomic>
#include <thread>

#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GraphicsContext.h"
#include "Common/Thread/ThreadUtil.h"

#include "Core/EmuThread.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

static std::atomic<EmuThreadState> g_emuThreadState(EmuThreadState::DISABLED);
static std::atomic<bool> g_inLoop;

class GraphicsContext;

void MainThreadFunc(GraphicsContext *graphicsContext);

bool MainThread_Ready() {
	return g_inLoop;
}

static void EmuThreadFunc(GraphicsContext *graphicsContext, std::function<void()> postFrame) {
	INFO_LOG(Log::G3D, "Entering emu thread");
	SetCurrentThreadName("EmuThread");

	AndroidJNIThreadContext context;

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	g_emuThreadState = EmuThreadState::RUNNING;

	if (!NativeInitGraphics(graphicsContext)) {
		_assert_msg_(false, "NativeInitGraphics failed, might as well bail");
		// If this fails, which it normally shouldn't, let's bail.
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	} else {
		INFO_LOG(Log::G3D, "EmuThread: Entering loop");
	}

	while (g_emuThreadState != EmuThreadState::QUIT_REQUESTED) {
		// We're here again, so the game quit.  Restart Run() which controls the UI.
		// This way they can load a new game.
		NativeFrame(graphicsContext);
		if (postFrame) {
			postFrame();
		}

		if (GetUIState() == UISTATE_EXIT) {
			g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
		}
	}

	INFO_LOG(Log::System, "emuThreadState was set to QUIT_REQUESTED, left EmuThreadFunc loop. Setting state to STOPPED.");

	g_emuThreadState = EmuThreadState::STOPPED;

	NativeShutdownGraphics();
	INFO_LOG(Log::System, "Leaving emu thread");
}

std::thread EmuThread_Start(GraphicsContext *graphicsContext, std::function<void()> postFrame) {
	g_emuThreadState = EmuThreadState::START_REQUESTED;
	std::thread emuThread = std::thread(&EmuThreadFunc, graphicsContext, postFrame);
	return emuThread;
}

void EmuThread_Join(GraphicsContext *graphicsContext, std::thread &emuThread) {
	const EmuThreadState state = g_emuThreadState;
	if (state != EmuThreadState::QUIT_REQUESTED &&
		state != EmuThreadState::STOPPED) {
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	}
	_dbg_assert_(emuThread.joinable());
	if (graphicsContext->NeedsRenderThread()) {
		graphicsContext->ThreadFrameUntilCondition([] {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			return g_emuThreadState == EmuThreadState::STOPPED;
		});
	}
	emuThread.join();
	emuThread = std::thread();
}

void MainThreadFunc(GraphicsContext *graphicsContext, std::function<void()> postFrame) {
	if (graphicsContext->NeedsRenderThread()) {
		SetCurrentThreadName("RenderThread");
		// This is now the render thread, and will spawn the emu thread below.

		std::string error_string;
		bool success = graphicsContext->InitFromRenderThread(&error_string);

		DEBUG_LOG(Log::Boot, "Done.");

		g_inLoop = true;

		std::thread emuThread = EmuThread_Start(graphicsContext, postFrame);

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
		graphicsContext->ShutdownFromRenderThread();

		INFO_LOG(Log::System, "EmuThreadJoin - joined");

	} else {
		SetCurrentThreadName("EmuThread");
		// This is the emu thread. the graphics contexts will spawn and handle its own threads if needed.

		std::string error_string;
		bool success = graphicsContext->InitFromRenderThread(&error_string);

		NativeInitGraphics(graphicsContext);
		NativeResized();

		DEBUG_LOG(Log::Boot, "Done.");

		g_inLoop = true;

		graphicsContext->ThreadStart();
		while (GetUIState() != UISTATE_EXIT) {
			// We're here again, so the game quit.  Restart Run() which controls the UI.
			// This way they can load a new game.
			NativeFrame(graphicsContext);
			postFrame();
		}
		Core_Stop();

		// Process the shutdown.  Without this, non-GL delays 800ms on shutdown.
		Core_StateProcessed();
		NativeFrame(graphicsContext);

		g_inLoop = false;

		NativeShutdownGraphics();

		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();
	}

	graphicsContext->Shutdown();
}
