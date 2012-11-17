// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.


#define programname "PPSSPP v0.2"


#include <windows.h>
#include "../globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "../Core/Debugger/SymbolMap.h"

#include "Debugger/Debugger_Disasm.h"
#include "Debugger/Debugger_MemoryDlg.h"
#include "main.h"

#include "../Core/Core.h"
#include "../Core/MemMap.h"
#include "EmuThread.h"

#include "resource.h"

#include "WndMainWindow.h"
#include "LogManager.h"
#include "ConsoleListener.h"
#include "W32Util/DialogManager.h"
#include "W32Util/ShellUtil.h"
#include "W32Util/Misc.h"
#include "../Core/Config.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	HMENU menu;
	BOOL skinMode = FALSE;

	HINSTANCE hInst;

	//W32Util::LayeredWindow *layer;
#define MAX_LOADSTRING 100
	TCHAR *szTitle = TEXT("PPSSPP");
	TCHAR *szWindowClass = TEXT("PPSSPPWnd");
	TCHAR *szDisplayClass = TEXT("PPSSPPDisplay");

	// Foward declarations of functions included in this code module:
	LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWND()
	{
		return hwndMain;
	}

	HWND GetDisplayHWND()
	{
		return hwndDisplay;
	}

	void Init(HINSTANCE hInstance)
	{
#ifdef THEMES
		WTL::CTheme::IsThemingSupported();
#endif
		//Register classes
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX); 
		wcex.style			= CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc	= (WNDPROC)WndProc;
		wcex.cbClsExtra		= 0;
		wcex.cbWndExtra		= 0;
		wcex.hInstance		= hInstance;
		wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP); 
		wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground	= (HBRUSH)GetStockObject(NULL_BRUSH);
		wcex.lpszMenuName	= (LPCSTR)IDR_MENU1;
		wcex.lpszClassName	= szWindowClass;
		wcex.hIconSm		= (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16,16,LR_SHARED);
		RegisterClassEx(&wcex);

		wcex.style = CS_HREDRAW | CS_VREDRAW;;
		wcex.lpfnWndProc = (WNDPROC)DisplayProc;
		wcex.hIcon = 0;
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = szDisplayClass;
		wcex.hIconSm = 0;
		RegisterClassEx(&wcex);
	}

	void RequestWindowSize(int w, int h)
	{
		RECT rc;
		rc.left=20;
		rc.top=100;

		rc.right=w+rc.left;//+client edge
		rc.bottom=h+rc.top; //+client edge

		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
		SetWindowPos(hwndMain,0,0,0,rc.right-rc.left,rc.bottom-rc.top,SWP_NOMOVE|SWP_NOZORDER);
	}

	BOOL Show(HINSTANCE hInstance, int nCmdShow)
	{
		hInst = hInstance; // Store instance handle in our global variable

		RECT rc,rcOrig;
		rc.left=20;
		rc.top=100;

		int zoom = g_Config.iZoom;

		rc.right=480*zoom+rc.left;//+client edge
		rc.bottom=272*zoom+rc.top; //+client edge

		rcOrig=rc;
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

		u32 style = skinMode ? WS_POPUP : WS_OVERLAPPEDWINDOW;

		hwndMain = CreateWindowEx(0,szWindowClass, "", style,
			rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, hInstance, NULL);
		SetPlaying(0);
		if (!hwndMain)
			return FALSE;

		menu = GetMenu(hwndMain);
#ifdef FINAL
		RemoveMenu(menu,2,MF_BYPOSITION);
		RemoveMenu(menu,2,MF_BYPOSITION);
#endif
		MENUINFO info;
		ZeroMemory(&info,sizeof(MENUINFO));
		info.cbSize = sizeof(MENUINFO);
		info.cyMax = 0;
		info.dwStyle = MNS_CHECKORBMP;
		info.fMask = MIM_STYLE;
		for (int i=0; i<GetMenuItemCount(menu); i++)
		{
			SetMenuInfo(GetSubMenu(menu,i),&info);
		}

		hwndDisplay = CreateWindowEx(0,szDisplayClass,TEXT(""),
			WS_CHILD|WS_VISIBLE,
			0,0,/*rcOrig.left,rcOrig.top,*/rcOrig.right-rcOrig.left,rcOrig.bottom-rcOrig.top,hwndMain,0,hInstance,0);

		ShowWindow(hwndMain, nCmdShow);
		//accept dragged files
		DragAcceptFiles(hwndMain, TRUE);
		UpdateMenus();

		SetFocus(hwndMain);

		return TRUE;
	}

	void BrowseAndBoot(void)
	{
		std::string fn;
		std::string filter = "";

		filter += "PSP";
		filter += "|";
		filter += "*.pbp;*.elf;*.iso;*.cso;*.prx";
		filter += "|";
		filter += "|";
		for (int i=0; i<(int)filter.length(); i++)
		{
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		if (W32Util::BrowseForFileName(true, GetHWND(), "Load File",0,filter.c_str(),"*.pbp;*.elf;*.iso;*.cso;",fn))
		{
			// decode the filename with fullpath
			std::string fullpath = fn;
			char drive[MAX_PATH];
			char dir[MAX_PATH];
			char fname[MAX_PATH];
			char ext[MAX_PATH];
			_splitpath(fullpath.c_str(), drive, dir, fname, ext);

			// generate the mapfilename
			std::string executable = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			std::string mapfile = std::string(drive) + std::string(dir) + std::string(fname) + std::string(".map");

			EmuThread_Start(executable.c_str());
		}
	}

	LRESULT CALLBACK DisplayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message) 
		{
		case WM_ACTIVATE:
			break;
		case WM_SETFOCUS:
			break;
		case WM_SIZE:
			break;
			
		case WM_ERASEBKGND:
	  	return DefWindowProc(hWnd, message, wParam, lParam);
  
		case WM_LBUTTONDOWN:
//			Update();
			break;

		case WM_LBUTTONDBLCLK:
			MessageBox(0,"Fullscreen isn't implemented yet","Sorry",0);
			break;
		case WM_PAINT:
			return DefWindowProc(hWnd, message, wParam, lParam);
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		int wmId, wmEvent;
		std::string fn;

		switch (message) 
		{
		case WM_CREATE:
			PostMessage(hWnd, WM_COMMAND, ID_FILE_LOAD, 0);
			break;

		case WM_MOVE:
			break;

		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
			//File menu

			case ID_FILE_LOAD:
				BrowseAndBoot();
				break;

			case ID_FILE_REFRESHGAMELIST:
				break;

			//Emulation menu

			case ID_EMULATION_RUN:
				if (g_State.bEmuThreadStarted)
				{
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_GO, 0);
				}
				break;

			case ID_EMULATION_STOP:
				for (int i=0; i<numCPUs; i++)
					if (disasmWindow[i])
						SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);

				Sleep(100);//UGLY wait for event instead

				for (int i=0; i<numCPUs; i++)
					if (disasmWindow[i])
						SendMessage(disasmWindow[i]->GetDlgHandle(), WM_CLOSE, 0, 0);
				for (int i=0; i<numCPUs; i++)
					if (memoryWindow[i])
						SendMessage(memoryWindow[i]->GetDlgHandle(), WM_CLOSE, 0, 0);

				EmuThread_Stop();
				SetPlaying(0);
				Update();
				UpdateMenus();
				break;


			case ID_EMULATION_PAUSE:
				for (int i=0; i<numCPUs; i++)
					if (disasmWindow[i])
						SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				break;

			case ID_EMULATION_SPEEDLIMIT:
				g_Config.bSpeedLimit = !g_Config.bSpeedLimit;
				UpdateMenus();
				break;

			case ID_FILE_LOADSTATE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load state",0,"Save States (*.gcs)\0*.gcs\0All files\0*.*\0\0","gcs",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SetCursor(LoadCursor(0,IDC_ARROW));
				}
				break;

			case ID_FILE_SAVESTATE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save state",0,"Save States (*.gcs)\0*.gcs\0All files\0*.*\0\0","gcs",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SetCursor(LoadCursor(0,IDC_ARROW));
				}
				break;

			case ID_FILE_EXIT:
				DestroyWindow(hWnd);
				break;

			//////////////////////////////////////////////////////////////////////////
			//CPU menu
			//////////////////////////////////////////////////////////////////////////
			case ID_CPU_DYNAREC:
				g_Config.bJIT = true;
				UpdateMenus();
				break;			
			case ID_CPU_INTERPRETER:
				g_Config.bJIT = false;
				UpdateMenus();
				break;

			//case ID_CPU_RESET: 
			//	MessageBox(hwndMain,"Use the controls in the disasm window for now..","Sorry",0);
			//	Update();
			//	break;

			case ID_DEBUG_RUNPOWERPCTEST:
				//doppctest();
				break;
			//////////////////////////////////////////////////////////////////////////
			//Debug menu
			//////////////////////////////////////////////////////////////////////////
				/*
			case ID_DEBUG_LOCATESYMBOLS:
				{
					std::vector<std::string> files=  W32Util::BrowseForFileNameMultiSelect(true,hWnd,"MOJS",0,"BLAH\0*.*",0);
					std::vector<std::string>::iterator iter;
					if (files.size())
					{
						for (iter=files.begin(); iter!=files.end(); iter++)
						{
							LOG(MASTER_LOG,"Loading symbols from %s", iter->c_str());
							LoadSymbolsFromO((*iter).c_str(),0x02000000,1*1024*1024);
						}
						symbolMap.SortSymbols();
//						HLE_PatchFunctions();
					}
					
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							disasmWindow[i]->NotifyMapLoaded();
					for (int i=0; i<numCPUs; i++)
						if (memoryWindow[i])
							memoryWindow[i]->Update();
				}
				break;
*/

			case ID_DEBUG_LOADMAPFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn))
				{
					symbolMap.LoadSymbolMap(fn.c_str());
//					HLE_PatchFunctions();
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							disasmWindow[i]->NotifyMapLoaded();
					for (int i=0; i<numCPUs; i++)
						if (memoryWindow[i])
							memoryWindow[i]->NotifyMapLoaded();
				}
				break;
			case ID_DEBUG_SAVEMAPFILE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn))
					symbolMap.SaveSymbolMap(fn.c_str());
				break;
				/*
			case ID_DEBUG_COMPILESIGNATUREFILE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save signature file",0,"Sigs\0*.sig\0All files\0*.*\0\0","sig",fn))
					symbolMap.CompileFuncSignaturesFile(fn.c_str());
				break;
			case ID_DEBUG_USESIGNATUREFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Use signature file",0,"Sigs\0*.sig\0All files\0*.*\0\0","sig",fn))
				{
					symbolMap.UseFuncSignaturesFile(fn.c_str(),0x80400000);
//					HLE_PatchFunctions();
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							disasmWindow[i]->NotifyMapLoaded();
				}
				break;*/

			case ID_DEBUG_RESETSYMBOLTABLE:
				symbolMap.ResetSymbolMap();
				for (int i=0; i<numCPUs; i++)
					if (disasmWindow[i])
						disasmWindow[i]->NotifyMapLoaded();
				for (int i=0; i<numCPUs; i++)
					if (memoryWindow[i])
						memoryWindow[i]->NotifyMapLoaded();
				break;
			case ID_DEBUG_DISASSEMBLY:
				if (disasmWindow[0])
					disasmWindow[0]->Show(true);
				break;
			case ID_DEBUG_MEMORYVIEW:
				if (memoryWindow[0])
					memoryWindow[0]->Show(true);
				break;
			case ID_DEBUG_LOG:
        LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
				break;

			//////////////////////////////////////////////////////////////////////////
			//Options menu
			//////////////////////////////////////////////////////////////////////////
			case ID_OPTIONS_IGNOREILLEGALREADS:
				g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
				UpdateMenus();
				break;
				//case ID_OPTIONS_FULLSCREEN:
				//	break;

			case ID_OPTIONS_DISPLAYRAWFRAMEBUFFER:
				g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
				UpdateMenus();
				break;

			
			//////////////////////////////////////////////////////////////////////////
			//Help menu
			//////////////////////////////////////////////////////////////////////////
	
      case ID_HELP_OPENWEBSITE:
				ShellExecute(NULL, "open", "http://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
        break;

      case ID_HELP_ABOUT:
				DialogManager::EnableAll(FALSE);
				DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				DialogManager::EnableAll(TRUE);
				break;

			default:
				{
					MessageBox(hwndMain,"Unimplemented","Sorry",0);
				}
				break;
			}
			break;
		case WM_KEYDOWN:
			{
				static int mojs=0;
				mojs ^= 1;
				//SetSkinMode(mojs);
			}
			break;
		case WM_DROPFILES:
			{
				HDROP hdrop = (HDROP)wParam;
				int count = DragQueryFile(hdrop,0xFFFFFFFF,0,0);
				if (count != 1)
				{
					MessageBox(hwndMain,"You can only load one file at a time","Error",MB_ICONINFORMATION);
				}
				else
				{
					TCHAR filename[512];
					DragQueryFile(hdrop,0,filename,512);
					TCHAR *type = filename+_tcslen(filename)-3;
/*
					TBootFileType t;
					if (strcmp(type,"bin")==0)
						t=BOOT_BIN;
					else if (strcmp(type,"elf")==0)
						t=BOOT_ELF;
					else if (strcmp(type,"dol")==0)
						t=BOOT_DOL;
					else
					{
						MessageBox(hwndMain,"Not a runnable Gamecube file","Error",MB_ICONERROR);
						break;
					}
					CCore::Start(0,filename,t);
					*/

					if (g_State.bEmuThreadStarted)
					{
						SendMessage(hWnd, WM_COMMAND, ID_EMULATION_STOP, 0);
					}
					
					MainWindow::SetPlaying(filename);
					MainWindow::Update();
					MainWindow::UpdateMenus();

					EmuThread_Start(filename);
				}
			}
			break;

			//case WM_ERASEBKGND:
			//	return 0;
		case WM_CLOSE:
			Sleep(100);//UGLY wait for event instead
			EmuThread_Stop();

			/*
			if (g_Config.bConfirmOnQuit && CCore::IsRunning())
			{
				if (IDNO==MessageBox(hwndMain,"A game is in progress. Are you sure you want to exit?","Are you sure?",MB_YESNO|MB_ICONQUESTION))
					return 1;//or 1?
				else
					return DefWindowProc(hWnd,message,wParam,lParam);
				break;
			}
			else
			*/
				return DefWindowProc(hWnd,message,wParam,lParam);
//		case WM_LBUTTONDOWN:
//			TrackPopupMenu(menu,0,0,0,0,hWnd,0);
//			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_SIZE:
			break;
		case WM_NCHITTEST:
			if (skinMode)
				return HTCAPTION;
			else
				return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_USER+1:
			disasmWindow[0] = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(disasmWindow[0]);
			disasmWindow[0]->Show(TRUE);
			memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(memoryWindow[0]);
			if (disasmWindow[0])
				disasmWindow[0]->NotifyMapLoaded();
			if (memoryWindow[0])
				memoryWindow[0]->NotifyMapLoaded();
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}


	void UpdateMenus()
	{
		HMENU menu = GetMenu(GetHWND());
#define CHECKITEM(item,value) 	CheckMenuItem(menu,item,MF_BYCOMMAND | ((value) ? MF_CHECKED : MF_UNCHECKED));

		CHECKITEM(ID_EMULATION_SPEEDLIMIT,g_Config.bSpeedLimit);
//		CHECK(ID_OPTIONS_ENABLEFRAMEBUFFER,g_Config.bEnableFrameBuffer);
//		CHECK(ID_OPTIONS_EMULATESYSCALL,g_bEmulateSyscall);
		CHECKITEM(ID_OPTIONS_DISPLAYRAWFRAMEBUFFER, g_Config.bDisplayFramebuffer);
		CHECKITEM(ID_OPTIONS_IGNOREILLEGALREADS,g_Config.bIgnoreBadMemAccess);
		CHECKITEM(ID_CPU_INTERPRETER,!g_Config.bJIT);
		CHECKITEM(ID_CPU_DYNAREC,g_Config.bJIT);

		BOOL enable = !Core_IsStepping();
		EnableMenuItem(menu,ID_EMULATION_RUN,enable);
		EnableMenuItem(menu,ID_EMULATION_PAUSE,!enable);

		enable = g_State.bEmuThreadStarted;
		EnableMenuItem(menu,ID_FILE_LOAD,enable);
		//EnableMenuItem(menu,ID_FILE_LOAD_DOL,enable);
		//EnableMenuItem(menu,ID_FILE_LOAD_ELF,enable);
		EnableMenuItem(menu,ID_CPU_DYNAREC,enable);
		EnableMenuItem(menu,ID_CPU_INTERPRETER,enable);
		EnableMenuItem(menu,ID_DVD_INSERTISO,enable);
		EnableMenuItem(menu,ID_FILE_BOOTBIOS,enable);
		EnableMenuItem(menu,ID_EMULATION_STOP,!enable);
		EnableMenuItem(menu,ID_OPTIONS_SETTINGS,enable);
		EnableMenuItem(menu,ID_PLUGINS_CHOOSEPLUGINS,enable);
	}


	// Message handler for about box.
	LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
			W32Util::CenterWindow(hDlg);
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	void Update()
	{
		InvalidateRect(hwndDisplay,0,0);
		UpdateWindow(hwndDisplay);
		SendMessage(hwndMain,WM_SIZE,0,0);
	}

	void Redraw()
	{
		InvalidateRect(hwndDisplay,0,0);
	}

	void SetPlaying(const char *text)
	{
		if (text == 0)
			SetWindowText(hwndMain,programname);
		else
		{
			char temp[256];
			sprintf(temp, "%s - %s", text, programname);
			SetWindowText(hwndMain,temp);
		}
	}

	HINSTANCE GetHInstance()
	{
		return hInst;
	}
}

