// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "base/mutex.h"
#include "i18n/i18n.h"
#include "util/text/utf8.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "../Globals.h"
#include "Windows/EmuThread.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/WndMainWindow.h"
#include "Windows/resource.h"
#include "Core/Reporting.h"
#include "Core/MemMap.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "thread/threadutil.h"

#include <tchar.h>
#include <process.h>
#include <intrin.h>
#pragma intrinsic(_InterlockedExchange)

static recursive_mutex emuThreadLock;
static HANDLE emuThread;
static volatile long emuThreadReady;

extern std::vector<std::wstring> GetWideCmdLine();

enum EmuThreadStatus : long
{
	THREAD_NONE = 0,
	THREAD_INIT,
	THREAD_CORE_LOOP,
	THREAD_SHUTDOWN,
	THREAD_END,
};

HANDLE EmuThread_GetThreadHandle()
{
	lock_guard guard(emuThreadLock);
	return emuThread;
}

unsigned int WINAPI TheThread(void *);

void EmuThread_Start()
{
	lock_guard guard(emuThreadLock);
	emuThread = (HANDLE)_beginthreadex(0, 0, &TheThread, 0, 0, 0);
}

void EmuThread_Stop()
{
	// Already stopped?
	{
		lock_guard guard(emuThreadLock);
		if (emuThread == NULL || emuThreadReady == THREAD_END)
			return;
	}

	UpdateUIState(UISTATE_EXIT);
	Core_Stop();
	Core_WaitInactive(800);
	if (WAIT_TIMEOUT == WaitForSingleObject(emuThread, 800))
	{
		_dbg_assert_msg_(COMMON, false, "Wait for EmuThread timed out.");
	}
	{
		lock_guard guard(emuThreadLock);
		CloseHandle(emuThread);
		emuThread = 0;
	}
	host->UpdateUI();
}

bool EmuThread_Ready()
{
	return emuThreadReady == THREAD_CORE_LOOP;
}

unsigned int WINAPI TheThread(void *)
{
	_InterlockedExchange(&emuThreadReady, THREAD_INIT);

	setCurrentThreadName("Emu");  // And graphics...

	// Native overwrites host. Can't allow that.

	Host *oldHost = host;

	// Convert the command-line arguments to Unicode, then to proper UTF-8 
	// (the benefit being that we don't have to pollute the UI project with win32 ifdefs and lots of Convert<whatever>To<whatever>).
	// This avoids issues with PPSSPP inadvertently destroying paths with Unicode glyphs 
	// (using the ANSI args resulted in Japanese/Chinese glyphs being turned into question marks, at least for me..).
	// -TheDax
	std::vector<std::wstring> wideArgs = GetWideCmdLine();
	std::vector<std::string> argsUTF8;
	for (auto& string : wideArgs) {
		argsUTF8.push_back(ConvertWStringToUTF8(string));
	}

	std::vector<const char *> args;

	for (auto& string : argsUTF8) {
		args.push_back(string.c_str());
	}

	NativeInit(static_cast<int>(args.size()), &args[0], "1234", "1234", "1234");

	Host *nativeHost = host;
	host = oldHost;

	host->UpdateUI();
	
	//Check Colour depth
	HDC dc = GetDC(NULL);
	u32 colour_depth = GetDeviceCaps(dc, BITSPIXEL);
	ReleaseDC(NULL, dc);
	if (colour_depth != 32){
		MessageBox(0, L"Please switch your display to 32-bit colour mode", L"OpenGL Error", MB_OK);
		ExitProcess(1);
	}

	std::string error_string;
	if (!host->InitGraphics(&error_string)) {
		I18NCategory *err = GetI18NCategory("Error");
		Reporting::ReportMessage("Graphics init error: %s", error_string.c_str());

		const char *defaultErrorOpenGL = "Failed initializing graphics. Try upgrading your graphics drivers.\n\nWould you like to try switching to DirectX 9?\n\nError message:";
		const char *defaultErrorDirect3D9 = "Failed initializing graphics. Try upgrading your graphics drivers and directx 9 runtime.\n\nWould you like to try switching to OpenGL?\n\nError message:";
		const char *genericError;
		int nextBackend = GPU_BACKEND_DIRECT3D9;
		switch (g_Config.iGPUBackend) {
		case GPU_BACKEND_DIRECT3D9:
			nextBackend = GPU_BACKEND_OPENGL;
			genericError = err->T("GenericDirect3D9Error", defaultErrorDirect3D9);
			break;
		case GPU_BACKEND_OPENGL:
		default:
			nextBackend = GPU_BACKEND_DIRECT3D9;
			genericError = err->T("GenericOpenGLError", defaultErrorOpenGL);
			break;
		}
		std::string full_error = StringFromFormat("%s\n\n%s", genericError, error_string.c_str());
		std::wstring title = ConvertUTF8ToWString(err->T("GenericGraphicsError", "Graphics Error"));
		bool yes = IDYES == MessageBox(0, ConvertUTF8ToWString(full_error).c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);
		ERROR_LOG(BOOT, full_error.c_str());

		if (yes) {
			// Change the config to the alternative and restart.
			g_Config.iGPUBackend = nextBackend;
			g_Config.Save();

			W32Util::ExitAndRestart();
		}

		// No safe way out without graphics.
		ExitProcess(1);
	}

	NativeInitGraphics();
	NativeResized();

	INFO_LOG(BOOT, "Done.");
	_dbg_update_();

	if (coreState == CORE_POWERDOWN) {
		INFO_LOG(BOOT, "Exit before core loop.");
		goto shutdown;
	}

	_InterlockedExchange(&emuThreadReady, THREAD_CORE_LOOP);

	if (g_Config.bBrowse)
		PostMessage(MainWindow::GetHWND(), WM_COMMAND, ID_FILE_LOAD, 0);

	Core_EnableStepping(FALSE);

	while (GetUIState() != UISTATE_EXIT)
	{
		// We're here again, so the game quit.  Restart Core_Run() which controls the UI.
		// This way they can load a new game.
		if (!Core_IsActive())
			UpdateUIState(UISTATE_MENU);

		Core_Run();
	}

shutdown:
	_InterlockedExchange(&emuThreadReady, THREAD_SHUTDOWN);

	NativeShutdownGraphics();

	host->ShutdownSound();
	host = nativeHost;
	NativeShutdown();
	host = oldHost;
	host->ShutdownGraphics();
	
	_InterlockedExchange(&emuThreadReady, THREAD_END);

	return 0;
}


