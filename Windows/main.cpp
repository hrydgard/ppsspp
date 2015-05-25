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
#include <Wbemidl.h>

#include "Common/CommonWindows.h"

#include "file/vfs.h"
#include "file/zip_read.h"
#include "base/NativeApp.h"
#include "profiler/profiler.h"
#include "thread/threadutil.h"
#include "util/text/utf8.h"

#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Windows/EmuThread.h"
#include "Windows/DSoundStream.h"
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
static std::string gpuDriverVersion;

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

// Adapted mostly as-is from http://www.gamedev.net/topic/495075-how-to-retrieve-info-about-videocard/?view=findpost&p=4229170
// so credit goes to that post's author, and in turn, the author of the site mentioned in that post (which seems to be down?).
std::string GetVideoCardDriverVersion() {
	std::string retvalue = "";

	HRESULT hr;
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		return retvalue;
	}

	IWbemLocator *pIWbemLocator = NULL;
	hr = CoCreateInstance(__uuidof(WbemLocator), NULL, CLSCTX_INPROC_SERVER, 
		__uuidof(IWbemLocator), (LPVOID *)&pIWbemLocator);
	if (FAILED(hr)) {
		CoUninitialize();
		return retvalue;
	}

	BSTR bstrServer = SysAllocString(L"\\\\.\\root\\cimv2");
	IWbemServices *pIWbemServices;
	hr = pIWbemLocator->ConnectServer(bstrServer, NULL, NULL, 0L, 0L, NULL,	NULL, &pIWbemServices);
	if (FAILED(hr)) {
		pIWbemLocator->Release();
		SysFreeString(bstrServer);
		CoUninitialize();
		return retvalue;
	}

	hr = CoSetProxyBlanket(pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, 
		NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,EOAC_DEFAULT);
	
	BSTR bstrWQL = SysAllocString(L"WQL");
	BSTR bstrPath = SysAllocString(L"select * from Win32_VideoController");
	IEnumWbemClassObject* pEnum;
	hr = pIWbemServices->ExecQuery(bstrWQL, bstrPath, WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);

	ULONG uReturned;
	VARIANT var;
	IWbemClassObject* pObj = NULL;
	if (!FAILED(hr)) {
		hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturned);
	}

	if (!FAILED(hr) && uReturned) {
		hr = pObj->Get(L"DriverVersion", 0, &var, NULL, NULL);
		if (SUCCEEDED(hr)) {
			char str[MAX_PATH];
			WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, str, sizeof(str), NULL, NULL);
			retvalue = str;
		}
	}

	pEnum->Release();
	SysFreeString(bstrPath);
	SysFreeString(bstrWQL);
	pIWbemServices->Release();
	pIWbemLocator->Release();
	SysFreeString(bstrServer);
	CoUninitialize();
	return retvalue;
}

std::string System_GetProperty(SystemProperty prop) {
	static bool hasCheckedGPUDriverVersion = false;
	switch (prop) {
	case SYSPROP_NAME:
		return osName;
	case SYSPROP_LANGREGION:
		return langRegion;
	case SYSPROP_CLIPBOARD_TEXT:
		{
			std::string retval;
			if (OpenClipboard(MainWindow::GetDisplayHWND())) {
				HANDLE handle = GetClipboardData(CF_UNICODETEXT);
				const wchar_t *wstr = (const wchar_t*)GlobalLock(handle);
				if (wstr)
					retval = ConvertWStringToUTF8(wstr);
				else
					retval = "";
				GlobalUnlock(handle);
				CloseClipboard();
			}
			return retval;
		}
	case SYSPROP_GPUDRIVER_VERSION:
		if (!hasCheckedGPUDriverVersion) {
			hasCheckedGPUDriverVersion = true;
			gpuDriverVersion = GetVideoCardDriverVersion();
		}
		return gpuDriverVersion;
	default:
		return "";
	}
}

// Ugly!
extern WindowsAudioBackend *winAudioBackend;

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return winAudioBackend ? winAudioBackend->GetSampleRate() : -1;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	default:
		return -1;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		PostMessage(MainWindow::GetHWND(), WM_CLOSE, 0, 0);
	} else if (!strcmp(command, "setclipboardtext")) {
		if (OpenClipboard(MainWindow::GetDisplayHWND())) {
			std::wstring data = ConvertUTF8ToWString(parameter);
			HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, (data.size() + 1) * sizeof(wchar_t));
			wchar_t *wstr = (wchar_t *)GlobalLock(handle);
			memcpy(wstr, data.c_str(), (data.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(wstr);
			SetClipboardData(CF_UNICODETEXT, handle);
			GlobalFree(handle);
			CloseClipboard();
		}
	}
}

void EnableCrashingOnCrashes() {
	typedef BOOL (WINAPI *tGetPolicy)(LPDWORD lpFlags);
	typedef BOOL (WINAPI *tSetPolicy)(DWORD dwFlags);
	const DWORD EXCEPTION_SWALLOWING = 0x1;

	HMODULE kernel32 = LoadLibrary(L"kernel32.dll");
	tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress(kernel32,
		"GetProcessUserModeExceptionPolicy");
	tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress(kernel32,
		"SetProcessUserModeExceptionPolicy");
	if (pGetPolicy && pSetPolicy) {
		DWORD dwFlags;
		if (pGetPolicy(&dwFlags)) {
			// Turn off the filter.
			pSetPolicy(dwFlags & ~EXCEPTION_SWALLOWING);
		}
	}
	FreeLibrary(kernel32);
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


std::vector<std::wstring> GetWideCmdLine() {
	wchar_t **wargv;
	int wargc = -1;
	wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

	std::vector<std::wstring> wideArgs(wargv, wargv + wargc);

	return wideArgs;
}

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	setCurrentThreadName("Main");

	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	PROFILE_INIT();

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

	char configFilename[MAX_PATH] = { 0 };
	const std::wstring configOption = L"--config=";

	char controlsConfigFilename[MAX_PATH] = { 0 };
	const std::wstring controlsOption = L"--controlconfig=";

	std::vector<std::wstring> wideArgs = GetWideCmdLine();

	for (size_t i = 1; i < wideArgs.size(); ++i) {
		if (wideArgs[i][0] == L'\0')
			continue;
		if (wideArgs[i][0] == L'-') {
			if (wideArgs[i].find(configOption) != std::wstring::npos && wideArgs[i].size() > configOption.size()) {
				const std::wstring tempWide = wideArgs[i].substr(configOption.size());
				const std::string tempStr = ConvertWStringToUTF8(tempWide);
				std::strncpy(configFilename, tempStr.c_str(), MAX_PATH);
			}

			if (wideArgs[i].find(controlsOption) != std::wstring::npos && wideArgs[i].size() > controlsOption.size()) {
				const std::wstring tempWide = wideArgs[i].substr(controlsOption.size());
				const std::string tempStr = ConvertWStringToUTF8(tempWide);
				std::strncpy(controlsConfigFilename, tempStr.c_str(), MAX_PATH);
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

	const std::wstring gpuBackend = L"--graphics=";

	// The rest is handled in NativeInit().
	for (size_t i = 1; i < wideArgs.size(); ++i) {
		if (wideArgs[i][0] == L'\0')
			continue;

		if (wideArgs[i][0] == L'-') {
			switch (wideArgs[i][1]) {
			case L'l':
				showLog = true;
				g_Config.bEnableLogging = true;
				break;
			case L's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			case L'd':
				debugLogLevel = true;
				break;
			}

			if (wideArgs[i] == L"--fullscreen")
				g_Config.bFullScreen = true;

			if (wideArgs[i] == L"--windowed")
				g_Config.bFullScreen = false;

			if (wideArgs[i].find(gpuBackend) != std::wstring::npos && wideArgs[i].size() > gpuBackend.size()) {
				const std::wstring restOfOption = wideArgs[i].substr(gpuBackend.size());

				// Force software rendering off, as picking directx9 or gles implies HW acceleration.
				// Once software rendering supports Direct3D9/11, we can add more options for software,
				// such as "software-gles", "software-d3d9", and "software-d3d11", or something similar.
				// For now, software rendering force-activates OpenGL.
				if (restOfOption == L"directx9") {
					g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D9;
					g_Config.bSoftwareRendering = false;
				}
				else if (restOfOption == L"gles") {
					g_Config.iGPUBackend = GPU_BACKEND_OPENGL;
					g_Config.bSoftwareRendering = false;
				}
				
				else if (restOfOption == L"software") {
					g_Config.iGPUBackend = GPU_BACKEND_OPENGL;
					g_Config.bSoftwareRendering = true;
				}
			}
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

	MainWindow::Show(_hInstance);

	HWND hwndMain = MainWindow::GetHWND();
	HWND hwndDisplay = MainWindow::GetDisplayHWND();
	
	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();
	CGEDebugger::Init();

	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));

	host = new WindowsHost(hwndMain, hwndDisplay);
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

	// Is there a safer place to do this?
	// Doing this in Config::Save requires knowing if the UI state is UISTATE_EXIT,
	// but that causes UnitTest to fail linking with 400 errors if System.h is included..
	if (g_Config.iTempGPUBackend != g_Config.iGPUBackend) {
		g_Config.iGPUBackend = g_Config.iTempGPUBackend;

		// For now, turn off software rendering too, similar to the command-line.
		g_Config.bSoftwareRendering = false;
	}

	g_Config.Save();
	LogManager::Shutdown();

	if (g_Config.bRestartRequired) {
		W32Util::ExitAndRestart();
	}
	CoUninitialize();
	return 0;
}
