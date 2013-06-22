// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "base/display.h"
#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "Log.h"
#include "StringUtils.h"
#include "../Globals.h"
#include "EmuThread.h"
#include "WndMainWindow.h"
#include "resource.h"
#include "../Core/Reporting.h"
#include "../Core/MemMap.h"
#include "../Core/Core.h"
#include "../Core/Host.h"
#include "../Core/System.h"
#include "../Core/Config.h"
#include "thread/threadutil.h"

#include <tchar.h>
#include <process.h>
#include <intrin.h>
#pragma intrinsic(_InterlockedExchange)

class EmuThreadLockGuard
{
public:
	EmuThreadLockGuard() { emuThreadCS_.Enter(); }
	~EmuThreadLockGuard() { emuThreadCS_.Leave(); }
private:
	static struct EmuThreadCS
	{
		EmuThreadCS() { InitializeCriticalSection(&TheCS_); }
		~EmuThreadCS() { DeleteCriticalSection(&TheCS_); }
		void Enter() { EnterCriticalSection(&TheCS_); }
		void Leave() { LeaveCriticalSection(&TheCS_); }
		CRITICAL_SECTION TheCS_;
	} emuThreadCS_;
};

EmuThreadLockGuard::EmuThreadCS EmuThreadLockGuard::emuThreadCS_;
static HANDLE emuThread;
static long emuThreadReady;

enum EmuTreadStatus : long
{
	THREAD_NONE = 0,
	THREAD_INIT,
	THREAD_CORE_LOOP,
	THREAD_SHUTDOWN,
	THREAD_END,
};

HANDLE EmuThread_GetThreadHandle()
{
	EmuThreadLockGuard lock;
	return emuThread;
}

unsigned int WINAPI TheThread(void *);

void EmuThread_Start()
{
	EmuThreadLockGuard lock;
	emuThread = (HANDLE)_beginthreadex(0, 0, &TheThread, 0, 0, 0);
}

void EmuThread_Stop()
{
	globalUIState = UISTATE_EXIT;
//	DSound_UpdateSound();
	Core_Stop();
	Core_WaitInactive(800);
	if (WAIT_TIMEOUT == WaitForSingleObject(emuThread, 800))
	{
		MessageBox(MainWindow::GetHWND(),"Wait for emuthread timed out! :(\n"
			"please alert the developer to possible deadlock or infinite loop in emuthread!", 0, 0);
	}
	{
		EmuThreadLockGuard lock;
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

	std::string memstick, flash0;
	GetSysDirectories(memstick, flash0);

	// Native overwrites host. Can't allow that.

	Host *oldHost = host;
	UpdateScreenScale();

	NativeInit(__argc, (const char **)__argv, memstick.c_str(), memstick.c_str(), "1234");
	Host *nativeHost = host;
	host = oldHost;

	host->UpdateUI();
	
	std::string error_string;
	if (!host->InitGL(&error_string)) {
		Reporting::ReportMessage("OpenGL init error: %s", error_string.c_str());
		std::string full_error = StringFromFormat( "Failed initializing OpenGL. Try upgrading your graphics drivers.\n\nError message:\n\n%s", error_string.c_str());
		MessageBoxA(0, full_error.c_str(), "OpenGL Error", MB_OK | MB_ICONERROR);
		ERROR_LOG(BOOT, full_error.c_str());
		goto shutdown;
	}

	NativeInitGraphics();

	INFO_LOG(BOOT, "Done.");
	_dbg_update_();

	if (coreState == CORE_POWERDOWN) {
		INFO_LOG(BOOT, "Exit before core loop.");
		goto shutdown;
	}

	_InterlockedExchange(&emuThreadReady, THREAD_CORE_LOOP);

	if (g_Config.bBrowse)
	{
		PostMessage(MainWindow::GetHWND(), WM_COMMAND, ID_FILE_LOAD, 0);
		//MainWindow::BrowseAndBoot("");
	}

	Core_EnableStepping(FALSE);

	while (globalUIState != UISTATE_EXIT)
	{
		Core_Run();

		// We're here again, so the game quit.  Restart Core_Run() which controls the UI.
		// This way they can load a new game.
		Core_UpdateState(CORE_RUNNING);
	}

shutdown:
	_InterlockedExchange(&emuThreadReady, THREAD_SHUTDOWN);

	NativeShutdownGraphics();
	host = nativeHost;
	NativeShutdown();
	host = oldHost;
	host->ShutdownGL();
	
	_InterlockedExchange(&emuThreadReady, THREAD_END);

	//The CPU should return when a game is stopped and cleanup should be done here, 
	//so we can restart the plugins (or load new ones) for the next game
	return 0;
}


