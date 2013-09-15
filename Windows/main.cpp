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
#include "Common/Thread.h"

#include "Commctrl.h"

#include "Windows/resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"

#include "Windows/W32Util/DialogManager.h"

#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/CtrlRegisterList.h"

#include "Windows/WindowsHost.h"
#include "Windows/main.h"

CDisasm *disasmWindow[MAX_CPUCOUNT] = {0};
CMemoryDlg *memoryWindow[MAX_CPUCOUNT] = {0};

static std::string langRegion;

void LaunchBrowser(const char *url) {
	ShellExecute(NULL, L"open", ConvertUTF8ToWString(url).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return "PC:Windows";
	case SYSPROP_LANGREGION:
		return langRegion;
	default:
		return "";
	}
}

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	Common::EnableCrashingOnCrashes();

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
	bool hideLog = true;
#else
	bool hideLog = false;
#endif

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));

	wchar_t lcCountry[256];

	// LOCALE_SNAME is only available in WinVista+
	// Really should find a way to do this in XP too :/
	if (0 != GetLocaleInfo(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256)) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (int i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	} else {
		langRegion = "en_US";
	}

	std::string configFilename;
	const char *configOption = "--config=";

	std::string controlsConfigFilename;
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

	if(configFilename.empty())
		configFilename = "ppsspp.ini";

	if(controlsConfigFilename.empty())
		controlsConfigFilename = "controls.ini";

	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.Load(configFilename.c_str(), controlsConfigFilename.c_str());

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
				hideLog = false;
				break;
			case 's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			}

			if (!strncmp(__argv[i], "--fullscreen", strlen("--fullscreen")))
				g_Config.bFullScreen = true;

			if (!strncmp(__argv[i], "--windowed", strlen("--windowed")))
				g_Config.bFullScreen = false;
		}
	}

	LogManager::Init();
	LogManager::GetInstance()->GetConsoleListener()->Open(hideLog, 150, 120, "PPSSPP Debug Console");


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
	HWND hwndDisplay = MainWindow::GetDisplayHWND();
	
	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();

	DialogManager::AddDlg(memoryWindow[0] = new CMemoryDlg(_hInstance, hwndMain, currentDebugMIPS));
	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));

	host = new WindowsHost(hwndMain, hwndDisplay);
	host->SetWindowTitle(0);

	MainWindow::CreateDebugWindows();

	// Emu thread is always running!
	EmuThread_Start();

	HACCEL hAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_ACCELS);
	HACCEL hDebugAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_DEBUGACCELS);

	//so.. we're at the message pump of the GUI thread
	for (MSG msg; GetMessage(&msg, NULL, 0, 0); )	// for no quit
	{
		//DSound_UpdateSound();

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
		if (g_debuggerActive) {
			wnd = disasmWindow[0]->GetDlgHandle();
			accel = hDebugAccelTable;
		} else {
			wnd = hwndMain;
			accel = hAccelTable;
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

	EmuThread_Stop();

	DialogManager::DestroyAll();
	timeEndPeriod(1);
	delete host;
	g_Config.Save();
	LogManager::Shutdown();
	return 0;
}
