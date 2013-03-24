// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "base/threadutil.h"
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

char fileToStart[MAX_PATH];

static HANDLE emuThread;


HANDLE EmuThread_GetThreadHandle()
{
	return emuThread;
}


DWORD TheThread(LPVOID x);

void EmuThread_Start(const char *filename)
{
	// _dbg_clear_();
	_tcsncpy(fileToStart, filename, sizeof(fileToStart) - 1);
	fileToStart[sizeof(fileToStart) - 1] = 0;

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


char *GetCurrentFilename()
{
	return fileToStart;
}

DWORD TheThread(LPVOID x) {
	setCurrentThreadName("EmuThread");

	g_State.bEmuThreadStarted = true;

	CoreParameter coreParameter;

	host->UpdateUI();
	
	std::string error_string;
	if (!host->InitGL(&error_string)) {
		Reporting::ReportMessage("OpenGL init error: %s", error_string.c_str());
		std::string full_error = StringFromFormat( "Failed initializing OpenGL. Try upgrading your graphics drivers.\n\nError message:\n\n%s", error_string.c_str());
		MessageBoxA(0, full_error.c_str(), "OpenGL Error", MB_OK | MB_ICONERROR);
		ERROR_LOG(BOOT, full_error.c_str());
		goto shutdown;
	}

	INFO_LOG(BOOT, "Starting up hardware.");

	coreParameter.fileToStart = fileToStart;
	coreParameter.enableSound = true;
	coreParameter.gpuCore = GPU_GLES;
	coreParameter.cpuCore = g_Config.bJit ? CPU_JIT : CPU_INTERPRETER;
	coreParameter.enableDebugging = true;
	coreParameter.printfEmuLog = false;
	coreParameter.headLess = false;
	coreParameter.renderWidth = (480 * g_Config.iWindowZoom) * (g_Config.SSAntiAliasing + 1);
	coreParameter.renderHeight = (272 * g_Config.iWindowZoom) * (g_Config.SSAntiAliasing + 1);
	coreParameter.outputWidth = 480 * g_Config.iWindowZoom;
	coreParameter.outputHeight = 272 * g_Config.iWindowZoom;
	coreParameter.pixelWidth = 480 * g_Config.iWindowZoom;
	coreParameter.pixelHeight = 272 * g_Config.iWindowZoom;
	coreParameter.startPaused = !g_Config.bAutoRun;
	coreParameter.useMediaEngine = false;

	error_string = "";
	if (!PSP_Init(coreParameter, &error_string))
	{
		ERROR_LOG(BOOT, "Error loading: %s", error_string.c_str());
		goto shutdown;
	}

	INFO_LOG(BOOT, "Done.");
	_dbg_update_();

	host->UpdateDisassembly();
	Core_EnableStepping(coreParameter.startPaused ? TRUE : FALSE);

	g_State.bBooted = true;
#ifdef _DEBUG
	host->UpdateMemView();
#endif

	host->BootDone();
	Core_Run();

	host->PrepareShutdown();


	PSP_Shutdown();

shutdown:

	host->ShutdownGL();
	
	//The CPU should return when a game is stopped and cleanup should be done here, 
	//so we can restart the plugins (or load new ones) for the next game
	g_State.bEmuThreadStarted = false;
	_endthreadex(0);
	return 0;
}


