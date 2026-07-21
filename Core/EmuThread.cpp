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
	SetCurrentThreadName("EmuThread");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	g_emuThreadState = EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

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

	g_emuThreadState = EmuThreadState::STOPPED;

	NativeShutdownGraphics();
}

std::thread EmuThread_Start(GraphicsContext *graphicsContext, std::function<void()> postFrame) {
	g_emuThreadState = EmuThreadState::START_REQUESTED;
	std::thread emuThread = std::thread(&EmuThreadFunc, graphicsContext, postFrame);
	return emuThread;
}

void EmuThread_Join(GraphicsContext *graphicsContext, std::thread &emuThread) {
	_dbg_assert_(emuThread.joinable());
	const EmuThreadState state = g_emuThreadState;
	if (state != EmuThreadState::QUIT_REQUESTED &&
		state != EmuThreadState::STOPPED) {
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	}
	graphicsContext->ThreadFrameUntilCondition([] {
		// Need to keep eating frames to allow the EmuThread to exit correctly.
		return g_emuThreadState == EmuThreadState::STOPPED;
	});
	emuThread.join();
	emuThread = std::thread();
}

void MainThreadFunc(GraphicsContext *graphicsContext, std::function<void()> postFrame) {
	const bool useEmuThread = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;
	if (useEmuThread) {
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
