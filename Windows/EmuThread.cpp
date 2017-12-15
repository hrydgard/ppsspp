#include <mutex>
#include <atomic>
#include <thread>

#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "i18n/i18n.h"
#include "input/input_state.h"
#include "util/text/utf8.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GraphicsContext.h"
#include "Windows/EmuThread.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/MainWindow.h"
#include "Windows/resource.h"
#include "Windows/WindowsHost.h"
#include "Core/Reporting.h"
#include "Core/MemMap.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "thread/threadutil.h"

static std::mutex emuThreadLock;
static std::thread emuThread;
static std::atomic<int> emuThreadState;

static std::mutex renderThreadLock;
static std::thread renderThread;
static std::atomic<int> renderThreadReady;

static bool useRenderThread;

extern std::vector<std::wstring> GetWideCmdLine();

class GraphicsContext;

static GraphicsContext *g_graphicsContext;

enum EmuThreadStatus : int {
	THREAD_NONE = 0,
	THREAD_INIT,
	THREAD_CORE_LOOP,
	THREAD_SHUTDOWN,
	THREAD_END,
};

void EmuThreadFunc();
void RenderThreadFunc();

void EmuThread_Start(bool separateRenderThread) {
	std::lock_guard<std::mutex> guard(emuThreadLock);
	emuThread = std::thread(&EmuThreadFunc);
	useRenderThread = separateRenderThread;
	if (useRenderThread) {
		renderThread = std::thread(&RenderThreadFunc);
	}
}

void EmuThread_Stop() {
	// Already stopped?
	{
		std::lock_guard<std::mutex> guard(emuThreadLock);
		if (emuThreadState == THREAD_END)
			return;
	}

	UpdateUIState(UISTATE_EXIT);
	Core_Stop();
	Core_WaitInactive(800);
	emuThread.join();
  if (useRenderThread) {
    renderThread.join();
  }

	PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_UPDATE_UI, 0, 0);
}

bool EmuThread_Ready() {
	return emuThreadState == THREAD_CORE_LOOP;
}

void RenderThreadFunc() {
	setCurrentThreadName("Render");
	while (!g_graphicsContext) {
		sleep_ms(50);
		continue;
	}
	g_graphicsContext->InitFromThread();
	while (true) {
		g_graphicsContext->ThreadFrame();
		break;
	}
}

void EmuThreadFunc() {
	emuThreadState = THREAD_INIT;

	setCurrentThreadName("Emu");

	host = new WindowsHost(MainWindow::GetHInstance(), MainWindow::GetHWND(), MainWindow::GetDisplayHWND());
	host->SetWindowTitle(nullptr);

	// We convert command line arguments to UTF-8 immediately.
	std::vector<std::wstring> wideArgs = GetWideCmdLine();
	std::vector<std::string> argsUTF8;
	for (auto& string : wideArgs) {
		argsUTF8.push_back(ConvertWStringToUTF8(string));
	}
	std::vector<const char *> args;
	for (auto& string : argsUTF8) {
		args.push_back(string.c_str());
	}
	bool performingRestart = NativeIsRestarting();
	NativeInit(static_cast<int>(args.size()), &args[0], "1234", "1234", nullptr);

	host->UpdateUI();

	GraphicsContext *graphicsContext = nullptr;

	std::string error_string;
	if (!host->InitGraphics(&error_string, &graphicsContext)) {
		// Before anything: are we restarting right now?
		if (performingRestart) {
			// Okay, switching graphics didn't work out.  Probably a driver bug - fallback to restart.
			// This happens on NVIDIA when switching OpenGL -> Vulkan.
			g_Config.Save();
			W32Util::ExitAndRestart();
		}

		I18NCategory *err = GetI18NCategory("Error");
		Reporting::ReportMessage("Graphics init error: %s", error_string.c_str());

		const char *defaultErrorVulkan = "Failed initializing graphics. Try upgrading your graphics drivers.\n\nWould you like to try switching to OpenGL?\n\nError message:";
		const char *defaultErrorOpenGL = "Failed initializing graphics. Try upgrading your graphics drivers.\n\nWould you like to try switching to DirectX 9?\n\nError message:";
		const char *defaultErrorDirect3D9 = "Failed initializing graphics. Try upgrading your graphics drivers and directx 9 runtime.\n\nWould you like to try switching to OpenGL?\n\nError message:";
		const char *genericError;
		GPUBackend nextBackend = GPUBackend::DIRECT3D9;
		switch (g_Config.iGPUBackend) {
		case (int)GPUBackend::DIRECT3D9:
			nextBackend = GPUBackend::OPENGL;
			genericError = err->T("GenericDirect3D9Error", defaultErrorDirect3D9);
			break;
		case (int)GPUBackend::VULKAN:
			nextBackend = GPUBackend::OPENGL;
			genericError = err->T("GenericVulkanError", defaultErrorVulkan);
			break;
		case (int)GPUBackend::OPENGL:
		default:
			nextBackend = GPUBackend::DIRECT3D9;
			genericError = err->T("GenericOpenGLError", defaultErrorOpenGL);
			break;
		}
		std::string full_error = StringFromFormat("%s\n\n%s", genericError, error_string.c_str());
		std::wstring title = ConvertUTF8ToWString(err->T("GenericGraphicsError", "Graphics Error"));
		bool yes = IDYES == MessageBox(0, ConvertUTF8ToWString(full_error).c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);
		ERROR_LOG(BOOT, full_error.c_str());

		if (yes) {
			// Change the config to the alternative and restart.
			g_Config.iGPUBackend = (int)nextBackend;
			g_Config.Save();

			W32Util::ExitAndRestart();
		}

		// No safe way out without graphics.
		exit(1);
	}

	NativeInitGraphics(graphicsContext);
	NativeResized();

	INFO_LOG(BOOT, "Done.");
	_dbg_update_();

	if (coreState == CORE_POWERDOWN) {
		INFO_LOG(BOOT, "Exit before core loop.");
		goto shutdown;
	}

	emuThreadState = THREAD_CORE_LOOP;

	if (g_Config.bBrowse)
		PostMessage(MainWindow::GetHWND(), WM_COMMAND, ID_FILE_LOAD, 0);

	Core_EnableStepping(FALSE);

	while (GetUIState() != UISTATE_EXIT) {
		// We're here again, so the game quit.  Restart Core_Run() which controls the UI.
		// This way they can load a new game.
		if (!Core_IsActive())
			UpdateUIState(UISTATE_MENU);
		Core_Run(graphicsContext);
	}

shutdown:
	emuThreadState = THREAD_SHUTDOWN;

	NativeShutdownGraphics();

	// NativeShutdown deletes the graphics context through host->ShutdownGraphics().
	NativeShutdown();
	emuThreadState = THREAD_END;
}

