// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <WinNls.h>
#include <math.h>

#include "Common/CommonWindows.h"

#include "file/vfs.h"
#include "file/zip_read.h"
#include "base/NativeApp.h"
#include "util/text/utf8.h"

#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Windows/EmuThread.h"
#include "ext/disarm.h"

#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"

#include "Commctrl.h"

#include "Windows/resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"
#include "Windows/GEDebugger/GEDebugger.h"

#include "Windows/W32Util/DialogManager.h"

#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/CtrlRegisterList.h"
#include "Windows/InputBox.h"

#include "Windows/WindowsHost.h"
#include "Windows/main.h"

// Nvidia drivers >= v302 will check if the application exports a global
// variable named NvOptimusEnablement to know if it should run the app in high
// performance graphics mode or using the IGP.
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 1;
}

CDisasm *disasmWindow[MAX_CPUCOUNT] = {0};
CGEDebugger *geDebuggerWindow = 0;
CMemoryDlg *memoryWindow[MAX_CPUCOUNT] = {0};

static std::string langRegion;
static std::string osName;

typedef BOOL(WINAPI *isProcessDPIAwareProc)();
typedef BOOL(WINAPI *setProcessDPIAwareProc)();

void LaunchBrowser(const char *url) {
	ShellExecute(NULL, L"open", ConvertUTF8ToWString(url).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

bool DoesVersionMatchWindows(const u32 major, const u32 minor, const u32 spMajor = 0, const u32 spMinor = 0) {
	u64 conditionMask = 0;
	OSVERSIONINFOEX osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));

	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = major;
	osvi.dwMinorVersion = minor;
	osvi.wServicePackMajor = spMajor;
	osvi.wServicePackMinor = spMinor;
	u32 op = VER_EQUAL;

	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMAJOR, op);
	VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMINOR, op);

	const u32 typeMask = VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR;

	return VerifyVersionInfo(&osvi, typeMask, conditionMask) != FALSE;
}

std::string GetWindowsVersion() {
	const bool IsWindowsXPSP2 = DoesVersionMatchWindows(5, 1, 2, 0);
	const bool IsWindowsXPSP3 = DoesVersionMatchWindows(5, 1, 3, 0);
	const bool IsWindowsVista = DoesVersionMatchWindows(6, 0);
	const bool IsWindowsVistaSP1 = DoesVersionMatchWindows(6, 0, 1, 0);
	const bool IsWindowsVistaSP2 = DoesVersionMatchWindows(6, 0, 2, 0);
	const bool IsWindows7 = DoesVersionMatchWindows(6, 1);
	const bool IsWindows7SP1 = DoesVersionMatchWindows(6, 1, 1, 0);
	const bool IsWindows8 = DoesVersionMatchWindows(6, 2);
	const bool IsWindows8_1 = DoesVersionMatchWindows(6, 3);

	if (IsWindowsXPSP2)
		return "Microsoft Windows XP, Service Pack 2";

	if (IsWindowsXPSP3)
		return "Microsoft Windows XP, Service Pack 3";

	if (IsWindowsVista)
		return "Microsoft Windows Vista";

	if (IsWindowsVistaSP1)
		return "Microsoft Windows Vista, Service Pack 1";

	if (IsWindowsVistaSP2)
		return "Microsoft Windows Vista, Service Pack 2";

	if (IsWindows7)
		return "Microsoft Windows 7";

	if (IsWindows7SP1)
		return "Microsoft Windows 7, Service Pack 1";

	if (IsWindows8)
		return "Microsoft Windows 8";

	if (IsWindows8_1)
		return "Microsoft Windows 8.1";

	return "Unsupported version of Microsoft Windows.";
}

std::string GetWindowsSystemArchitecture() {
	SYSTEM_INFO sysinfo;
	ZeroMemory(&sysinfo, sizeof(SYSTEM_INFO));
	GetNativeSystemInfo(&sysinfo);

	if (sysinfo.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_AMD64)
		return "(x64)";
	// Need to check for equality here, since ANDing with 0 is always 0.
	else if (sysinfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
		return "(x86)";
	else if (sysinfo.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_ARM)
		return "(ARM)";
	else
		return "(Unknown)";
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return osName;
	case SYSPROP_LANGREGION:
		return langRegion;
	default:
		return "";
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		PostMessage(MainWindow::GetHWND(), WM_CLOSE, 0, 0);
	}
}

void EnableCrashingOnCrashes() 
{ 
  typedef BOOL (WINAPI *tGetPolicy)(LPDWORD lpFlags); 
  typedef BOOL (WINAPI *tSetPolicy)(DWORD dwFlags); 
  const DWORD EXCEPTION_SWALLOWING = 0x1;

  HMODULE kernel32 = LoadLibraryA("kernel32.dll"); 
  tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress(kernel32, 
    "GetProcessUserModeExceptionPolicy"); 
  tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress(kernel32, 
    "SetProcessUserModeExceptionPolicy"); 
  if (pGetPolicy && pSetPolicy) 
  { 
    DWORD dwFlags; 
    if (pGetPolicy(&dwFlags)) 
    { 
      // Turn off the filter 
      pSetPolicy(dwFlags & ~EXCEPTION_SWALLOWING); 
    } 
  } 
}

bool System_InputBoxGetString(const char *title, const char *defaultValue, char *outValue, size_t outLength)
{
	std::string out;
	if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), ConvertUTF8ToWString(title).c_str(), defaultValue, out)) {
		strcpy(outValue, out.c_str());
		return true;
	} else {
		return false;
	}
}

bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue)
{
	if (InputBox_GetWString(MainWindow::GetHInstance(), MainWindow::GetHWND(), title, defaultvalue, outvalue)) {
		return true;
	} else {
		return false;
	}
}

void MakePPSSPPDPIAware()
{
	isProcessDPIAwareProc isDPIAwareProc = (isProcessDPIAwareProc) 
		GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "IsProcessDPIAware");

	setProcessDPIAwareProc setDPIAwareProc = (setProcessDPIAwareProc)
		GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "SetProcessDPIAware");

	// If we're not DPI aware, make it so, but do it safely.
	if (isDPIAwareProc != nullptr) {
		if (!isDPIAwareProc()) {
			if (setDPIAwareProc != nullptr)
				setDPIAwareProc();
		}
	}
}

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	// Windows Vista and above: alert Windows that PPSSPP is DPI aware,
	// so that we don't flicker in fullscreen on some PCs.
	MakePPSSPPDPIAware();

	// FMA3 support in the 2013 CRT is broken on Vista and Windows 7 RTM (fixed in SP1). Just disable it.
#ifdef _M_X64
	_set_FMA3_enable(0);
#endif

	EnableCrashingOnCrashes();

	wchar_t modulePath[MAX_PATH];
	GetModuleFileName(NULL, modulePath, MAX_PATH);
	for (size_t i = wcslen(modulePath) - 1; i > 0; i--) {
		if (modulePath[i] == '\\') {
			modulePath[i] = 0;
			break;
		}
	}
	SetCurrentDirectory(modulePath);
	// GetCurrentDirectory(MAX_PATH, modulePath);  // for checking in the debugger

#ifndef _DEBUG
	bool showLog = false;
#else
	bool showLog = false;
#endif

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));

	wchar_t lcCountry[256];

	// LOCALE_SNAME is only available in WinVista+
	// Really should find a way to do this in XP too :/
	if (0 != GetLocaleInfo(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256)) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (size_t i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	} else {
		langRegion = "en_US";
	}

	osName = GetWindowsVersion() + " " + GetWindowsSystemArchitecture();

	const char *configFilename = NULL;
	const char *configOption = "--config=";

	const char *controlsConfigFilename = NULL;
	const char *controlsOption = "--controlconfig=";

	for (int i = 1; i < __argc; ++i)
	{
		if (__argv[i][0] == '\0')
			continue;
		if (__argv[i][0] == '-')
		{
			if (!strncmp(__argv[i], configOption, strlen(configOption)) && strlen(__argv[i]) > strlen(configOption)) {
				configFilename = __argv[i] + strlen(configOption);
			}
			if (!strncmp(__argv[i], controlsOption, strlen(controlsOption)) && strlen(__argv[i]) > strlen(controlsOption)) {
				controlsConfigFilename = __argv[i] + strlen(controlsOption);
			}
		}
	}

	// On Win32 it makes more sense to initialize the system directories here 
	// because the next place it was called was in the EmuThread, and it's too late by then.
	InitSysDirectories();

	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.AddSearchPath("");
	g_Config.AddSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.SetDefaultPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load(configFilename, controlsConfigFilename);

	bool debugLogLevel = false;

	// The rest is handled in NativeInit().
	for (int i = 1; i < __argc; ++i)
	{
		if (__argv[i][0] == '\0')
			continue;

		if (__argv[i][0] == '-')
		{
			switch (__argv[i][1])
			{
			case 'l':
				showLog = true;
				g_Config.bEnableLogging = true;
				break;
			case 's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			case 'd':
				debugLogLevel = true;
				break;
			}

			if (!strncmp(__argv[i], "--fullscreen", strlen("--fullscreen")))
				g_Config.bFullScreen = true;

			if (!strncmp(__argv[i], "--windowed", strlen("--windowed")))
				g_Config.bFullScreen = false;
		}
	}
#ifdef _DEBUG
	g_Config.bEnableLogging = true;
#endif

	LogManager::Init();
	// Consider at least the following cases before changing this code:
	//   - By default in Release, the console should be hidden by default even if logging is enabled.
	//   - By default in Debug, the console should be shown by default.
	//   - The -l switch is expected to show the log console, REGARDLESS of config settings.
	//   - It should be possible to log to a file without showing the console.
	LogManager::GetInstance()->GetConsoleListener()->Init(showLog, 150, 120, "PPSSPP Debug Console");
	
	if (debugLogLevel)
		LogManager::GetInstance()->SetAllLogLevels(LogTypes::LDEBUG);

	//Windows, API init stuff
	INITCOMMONCONTROLSEX comm;
	comm.dwSize = sizeof(comm);
	comm.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
	InitCommonControlsEx(&comm);
	timeBeginPeriod(1);
	MainWindow::Init(_hInstance);

	g_hPopupMenus = LoadMenu(_hInstance, (LPCWSTR)IDR_POPUPMENUS);

	MainWindow::Show(_hInstance, iCmdShow);

	HWND hwndMain = MainWindow::GetHWND();
	
	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();
	CGEDebugger::Init();

	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));

	host = new WindowsHost(hwndMain);
	host->SetWindowTitle(0);

	MainWindow::CreateDebugWindows();

	// Emu thread is always running!
	EmuThread_Start();
	InputDevice::BeginPolling();

	HACCEL hAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_ACCELS);
	HACCEL hDebugAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_DEBUGACCELS);

	//so.. we're at the message pump of the GUI thread
	for (MSG msg; GetMessage(&msg, NULL, 0, 0); )	// for no quit
	{
		if (msg.message == WM_KEYDOWN)
		{
			//hack to enable/disable menu command accelerate keys
			MainWindow::UpdateCommands();

			//hack to make it possible to get to main window from floating windows with Esc
			if (msg.hwnd != hwndMain && msg.wParam == VK_ESCAPE)
				BringWindowToTop(hwndMain);
		}

		//Translate accelerators and dialog messages...
		HWND wnd;
		HACCEL accel;
		switch (g_activeWindow)
		{
		case WINDOW_MAINWINDOW:
			wnd = hwndMain;
			accel = hAccelTable;
			break;
		case WINDOW_CPUDEBUGGER:
			wnd = disasmWindow[0] ? disasmWindow[0]->GetDlgHandle() : 0;
			accel = hDebugAccelTable;
			break;
		case WINDOW_GEDEBUGGER:
		default:
			wnd = 0;
			accel = 0;
			break;
		}

		if (!TranslateAccelerator(wnd, accel, &msg))
		{
			if (!DialogManager::IsDialogMessage(&msg))
			{
				//and finally translate and dispatch
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	VFSShutdown();

	InputDevice::StopPolling();
	EmuThread_Stop();

	MainWindow::DestroyDebugWindows();
	DialogManager::DestroyAll();
	timeEndPeriod(1);
	delete host;
	g_Config.Save();
	LogManager::Shutdown();
	return 0;
}
