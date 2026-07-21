#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)

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

#include "Windows/W32Util/Misc.h"
#include "Windows/MainWindow.h"
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

static std::thread emuThread;
static std::atomic<EmuThreadState> g_emuThreadState(EmuThreadState::DISABLED);

static std::string g_error_message;
static bool g_inLoop;

class GraphicsContext;

void MainThreadFunc(GraphicsContext *graphicsContext);

bool MainThread_Ready() {
	return g_inLoop;
}

static void EmuThreadFunc(GraphicsContext *graphicsContext) {
	SetCurrentThreadName("EmuThread");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	g_emuThreadState = EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

	while (g_emuThreadState != EmuThreadState::QUIT_REQUESTED) {
		// We're here again, so the game quit.  Restart Run() which controls the UI.
		// This way they can load a new game.
		if (!Core_IsActive()) {
			UpdateUIState(UISTATE_MENU);
		}

		Core_StateProcessed();
		NativeFrame(graphicsContext);

		if (GetUIState() == UISTATE_EXIT) {
			g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
		}
	}

	g_emuThreadState = EmuThreadState::STOPPED;

	NativeShutdownGraphics();
}

static void EmuThreadStart(GraphicsContext *graphicsContext) {
	g_emuThreadState = EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc, graphicsContext);
}

static void EmuThreadStop() {
	const EmuThreadState state = g_emuThreadState;
	if (state != EmuThreadState::QUIT_REQUESTED &&
		state != EmuThreadState::STOPPED) {
		g_emuThreadState = EmuThreadState::QUIT_REQUESTED;
	}
}

static void EmuThreadJoin() {
	emuThread.join();
	INFO_LOG(Log::System, "EmuThreadJoin - joined");
}

void MainThreadFunc(GraphicsContext *graphicsContext) {
	const bool useEmuThread = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	SetCurrentThreadName(useEmuThread ? "RenderThread" : "EmuThread");

	std::string error_string;
	bool success = graphicsContext->InitFromRenderThread(&error_string);

	if (!useEmuThread) {
		NativeInitGraphics(graphicsContext);
		NativeResized();
	}

	DEBUG_LOG(Log::Boot, "Done.");

	g_inLoop = true;

	if (useEmuThread) {
		EmuThreadStart(graphicsContext);
	}
	graphicsContext->ThreadStart();

	if (useEmuThread) {
		// Again, this thread becomes the render thread.
		while (true) {
			if (equals_any(g_emuThreadState, EmuThreadState::QUIT_REQUESTED, EmuThreadState::STOPPED)) {
				break;
			}
			graphicsContext->ThreadFrame(true);
			if (GetUIState() == UISTATE_EXIT) {
				break;
			}
		}
	} else {
		// Same contents as EmuThread.
		while (GetUIState() != UISTATE_EXIT) {  //  && GetUIState() != UISTATE_EXCEPTION
			// We're here again, so the game quit.  Restart Run() which controls the UI.
			// This way they can load a new game.
			if (!(Core_IsActive() || Core_IsStepping()))
				UpdateUIState(UISTATE_MENU);
			Core_StateProcessed();
			NativeFrame(graphicsContext);
		}
	}
	Core_Stop();
	if (!useEmuThread) {
		// Process the shutdown.  Without this, non-GL delays 800ms on shutdown.
		Core_StateProcessed();
		NativeFrame(graphicsContext);
	}

	g_inLoop = false;

	if (useEmuThread) {
		EmuThreadStop();
		graphicsContext->ThreadFrameUntilCondition([] {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			return g_emuThreadState == EmuThreadState::STOPPED;
		});
		EmuThreadJoin();
	}

	if (!useEmuThread) {
		NativeShutdownGraphics();
	}

	graphicsContext->ThreadEnd();
	graphicsContext->ShutdownFromRenderThread();
	graphicsContext->Shutdown();
}

#endif
