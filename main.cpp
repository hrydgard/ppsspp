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

#include <windows.h>

#include "file/vfs.h"
#include "file/zip_read.h"

#include "../Core/Config.h"

#include "LogManager.h"
#include "ConsoleListener.h"
#include "Thread.h"

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

CDisasm *disasmWindow[MAX_CPUCOUNT];
CMemoryDlg *memoryWindow[MAX_CPUCOUNT];

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	Common::EnableCrashingOnCrashes();

	char *token = szCmdLine;
	char fileToLoad[256] = "";

	token = strtok(szCmdLine," ");

	g_Config.Load();
	VFSRegister("", new DirectoryAssetReader("shaders"));

	while (token)
	{
		if (strcmp(token,"-run"))
		{
			//run immediately
		}

		token = strtok(NULL," ");
	}

	//Windows, API init stuff
	INITCOMMONCONTROLSEX comm;
	comm.dwSize = sizeof(comm);
	comm.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
	InitCommonControlsEx(&comm);

	MainWindow::Init(_hInstance);
	host = new WindowsHost(MainWindow::GetDisplayHWND());

	HACCEL hAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_ACCELS);
	g_hPopupMenus = LoadMenu(_hInstance, (LPCSTR)IDR_POPUPMENUS);

	MainWindow::Show(_hInstance, iCmdShow);

	HWND hwndMain = MainWindow::GetHWND();
	HMENU menu = GetMenu(hwndMain);

	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();

	DialogManager::AddDlg(memoryWindow[0] = new CMemoryDlg(_hInstance, hwndMain, currentDebugMIPS));
	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));

	MainWindow::Update();
	MainWindow::UpdateMenus();

	LogManager::Init();
	bool hidden = false;
#ifndef _DEBUG
	hidden = true;
#endif
	LogManager::GetInstance()->GetConsoleListener()->Open(hidden, 150, 120, "PPSSPP Debug Console");
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LNOTICE);
	if (strlen(fileToLoad))
	{
		// TODO: load the thing
	}

	//so.. we're at the message pump of the GUI thread
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))	//while no quit
	{
		//DSound_UpdateSound();

		//hack to make it possible to get to main window from floating windows with Esc
		if (msg.hwnd != hwndMain && msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE)
			BringWindowToTop(hwndMain);

		//Translate accelerators and dialog messages...
		if (!TranslateAccelerator(hwndMain, hAccelTable, &msg))
		{
			if (!DialogManager::IsDialogMessage(&msg))
			{
				//and finally translate and dispatch
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	LogManager::Shutdown();
	DialogManager::DestroyAll();
	g_Config.Save();
	delete host;
	return 0;
}
