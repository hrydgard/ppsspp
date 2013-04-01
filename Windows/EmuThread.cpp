// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "base/display.h"
#include "base/timeutil.h"
#include "base/threadutil.h"
#include "base/NativeApp.h"
#include "Log.h"
#include "StringUtil.h"
#include "../Globals.h"
#include "EmuThread.h"
#include "../Core/Reporting.h"
#include "../Core/MemMap.h"
#include "../Core/Core.h"
#include "../Core/Host.h"
#include "../Core/System.h"
#include "../Core/Config.h"

#include <tchar.h>
#include <process.h>

static HANDLE emuThread;

HANDLE EmuThread_GetThreadHandle()
{
	return emuThread;
}

DWORD TheThread(LPVOID x);

void EmuThread_Start()
{
	unsigned int i;
	emuThread = (HANDLE)_beginthreadex(0,0,(unsigned int (__stdcall *)(void *))TheThread,(LPVOID)0,0,&i);
}

void EmuThread_Stop()
{
//	DSound_UpdateSound();
	Core_Stop();
	if (WAIT_TIMEOUT == WaitForSingleObject(EmuThread_GetThreadHandle(),300))
	{
		//MessageBox(0,"Wait for emuthread timed out, please alert the developer to possible deadlock or infinite loop in emuthread :(.",0,0);
	}
	host->UpdateUI();
}

DWORD TheThread(LPVOID x) {
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

	Core_EnableStepping(FALSE);
	Core_Run();

shutdown:
	host = nativeHost;
	NativeShutdownGraphics();
	NativeShutdown();
	host = oldHost;

	host->ShutdownGL();
	
	//The CPU should return when a game is stopped and cleanup should be done here, 
	//so we can restart the plugins (or load new ones) for the next game
	_endthreadex(0);
	return 0;
}


