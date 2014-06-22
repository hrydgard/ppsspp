// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "base/mutex.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "../Globals.h"
#include "Windows/EmuThread.h"
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

	setCurrentThreadName("EmuThread");

	// Native overwrites host. Can't allow that.

	Host *oldHost = host;

	NativeInit(__argc, (const char **)__argv, "1234", "1234", "1234");
	Host *nativeHost = host;
	host = oldHost;

	host->UpdateUI();
	
	//Check Colour depth
	HDC dc = GetDC(NULL);
	u32 colour_depth = GetDeviceCaps(dc, BITSPIXEL);
	ReleaseDC(NULL, dc);
	if (colour_depth != 32){
		MessageBoxA(0, "Please switch your display to 32-bit colour mode", "OpenGL Error", MB_OK);
		ExitProcess(1);
	}

	std::string error_string;
	if (!host->InitGL(&error_string)) {
		Reporting::ReportMessage("OpenGL init error: %s", error_string.c_str());
		std::string full_error = StringFromFormat( "Failed initializing OpenGL. Try upgrading your graphics drivers.\n\nError message:\n\n%s", error_string.c_str());
		MessageBoxA(0, full_error.c_str(), "OpenGL Error", MB_OK | MB_ICONERROR);
		ERROR_LOG(BOOT, full_error.c_str());

		// No safe way out without OpenGL.
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
	host->ShutdownGL();
	
	_InterlockedExchange(&emuThreadReady, THREAD_END);

	return 0;
}


