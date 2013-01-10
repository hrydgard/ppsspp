// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.


#define programname "PPSSPP v0.5"


#include <windows.h>
#include <tchar.h>
#include "../globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "../Core/Debugger/SymbolMap.h"
#include "OpenGLBase.h"
#include "Debugger/Debugger_Disasm.h"
#include "Debugger/Debugger_MemoryDlg.h"
#include "main.h"

#include "../Core/Core.h"
#include "../Core/MemMap.h"
#include "../Core/SaveState.h"
#include "../Core/System.h"
#include "EmuThread.h"

#include "resource.h"

#include "WndMainWindow.h"
#include "LogManager.h"
#include "ConsoleListener.h"
#include "W32Util/DialogManager.h"
#include "W32Util/ShellUtil.h"
#include "W32Util/Misc.h"
#include "../Core/Config.h"
#include "../GPU/GPUInterface.h"
#include "../GPU/GPUState.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

BOOL g_bFullScreen = FALSE;
RECT g_normalRC = {0};

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	HMENU menu;
	BOOL skinMode = FALSE;
	CoreState nextState = CORE_POWERDOWN;

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
	LRESULT CALLBACK Controls(HWND, UINT, WPARAM, LPARAM);

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
		wcex.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
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

	void GetWindowRectAtZoom(int zoom, RECT &rcInner, RECT &rcOuter) {
		rcInner.left=20;
		rcInner.top=100;

		rcInner.right=480*zoom + rcInner.left;//+client edge
		rcInner.bottom=272*zoom + rcInner.top; //+client edge

		rcOuter=rcInner;
		AdjustWindowRect(&rcOuter, WS_OVERLAPPEDWINDOW, TRUE);
	}

	void SetZoom(float zoom) {
		if (zoom < 5)
			g_Config.iWindowZoom = (int) zoom;
		RECT rc, rcOuter;
		GetWindowRectAtZoom((int) zoom, rc, rcOuter);
		MoveWindow(hwndMain, rcOuter.left, rcOuter.top, rcOuter.right - rcOuter.left, rcOuter.bottom - rcOuter.top, TRUE);
		MoveWindow(hwndDisplay, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
		PSP_CoreParameter().pixelWidth = (int) (480 * zoom);
		PSP_CoreParameter().pixelHeight = (int) (272 * zoom);
		GL_Resized();
	}

	BOOL Show(HINSTANCE hInstance, int nCmdShow)
	{
		hInst = hInstance; // Store instance handle in our global variable

		int zoom = g_Config.iWindowZoom;
		if (zoom < 1) zoom = 1;
		if (zoom > 4) zoom = 4;
		
		RECT rc,rcOrig;
		GetWindowRectAtZoom(zoom, rcOrig, rc);

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
		for (int i = 0; i < GetMenuItemCount(menu); i++)
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
			break;

		case WM_MOVE:
			break;

		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
			case ID_FILE_LOAD:
				BrowseAndBoot();
				break;

			case ID_FILE_REFRESHGAMELIST:
				break;

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

			case ID_FILE_LOADSTATEFILE:
				if (g_State.bEmuThreadStarted)
				{
					nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				}
				if (W32Util::BrowseForFileName(true, hWnd, "Load state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SaveState::Load(fn, SaveStateActionFinished);
				}
				break;

			case ID_FILE_SAVESTATEFILE:
				if (g_State.bEmuThreadStarted)
				{
					nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				}
				if (W32Util::BrowseForFileName(false, hWnd, "Save state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SaveState::Save(fn, SaveStateActionFinished);
				}
				break;

			// TODO: Add UI for multiple slots

			case ID_FILE_QUICKLOADSTATE:
				if (g_State.bEmuThreadStarted)
				{
					nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				}
				SetCursor(LoadCursor(0,IDC_WAIT));
				SaveState::LoadSlot(0, SaveStateActionFinished);
				break;

			case ID_FILE_QUICKSAVESTATE:
				if (g_State.bEmuThreadStarted)
				{
					nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				}
				SetCursor(LoadCursor(0,IDC_WAIT));
				SaveState::SaveSlot(0, SaveStateActionFinished);
				break;

			case ID_OPTIONS_SCREEN1X:
				SetZoom(1);
				UpdateMenus();
				break;
			case ID_OPTIONS_SCREEN2X:
				SetZoom(2);
				UpdateMenus();
				break;
			case ID_OPTIONS_SCREEN3X:
				SetZoom(3);
				UpdateMenus();
				break;
			case ID_OPTIONS_SCREEN4X:
				SetZoom(4);
				UpdateMenus();
				break;

			case ID_OPTIONS_BUFFEREDRENDERING:
				g_Config.bBufferedRendering = !g_Config.bBufferedRendering;
				UpdateMenus();
				break;

			case ID_OPTIONS_SHOWDEBUGSTATISTICS:
				g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
				UpdateMenus();
				break;

			case ID_OPTIONS_HARDWARETRANSFORM:
				g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
				UpdateMenus();
				break;

			case ID_FILE_EXIT:
				DestroyWindow(hWnd);
				break;

			case ID_CPU_DYNAREC:
				g_Config.iCpuCore = CPU_JIT;
				UpdateMenus();
				break;			
			case ID_CPU_INTERPRETER:
				g_Config.iCpuCore = CPU_INTERPRETER;
				UpdateMenus();
				break;
			case ID_CPU_FASTINTERPRETER:
				g_Config.iCpuCore = CPU_FASTINTERPRETER;
				UpdateMenus();
				break;

			case ID_EMULATION_RUNONLOAD:
				g_Config.bAutoRun = !g_Config.bAutoRun;
				UpdateMenus();
				break;

			case ID_DEBUG_DUMPNEXTFRAME:
				gpu->DumpNextFrame();
				break;

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

			case ID_OPTIONS_IGNOREILLEGALREADS:
				g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
				UpdateMenus();
				break;

			case ID_OPTIONS_FULLSCREEN:
				if(g_bFullScreen) {
					_ViewNormal(hWnd); 
					SetZoom(1); //restore window to original size
				}
				else {
					int cx = ::GetSystemMetrics(SM_CXSCREEN);
					float screenfactor = cx / 480.0f;
					SetZoom(screenfactor);
					_ViewFullScreen(hWnd);
				}
				break;

			case ID_OPTIONS_WIREFRAME:
				g_Config.bDrawWireframe = !g_Config.bDrawWireframe;
				UpdateMenus();
				break;

			case ID_OPTIONS_DISPLAYRAWFRAMEBUFFER:
				g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
				UpdateMenus();
				break;
			case ID_OPTIONS_FASTMEMORY:
				g_Config.bFastMemory = !g_Config.bFastMemory;
				UpdateMenus();
				break;
			case ID_OPTIONS_LINEARFILTERING:
				g_Config.bLinearFiltering = !g_Config.bLinearFiltering;
				UpdateMenus();
				break;
			case ID_OPTIONS_SIMPLE2XSSAA:
				g_Config.SSAntiAlaising = !g_Config.SSAntiAlaising;
				UpdateMenus();
				break;
			case ID_OPTIONS_CONTROLS:
				DialogManager::EnableAll(FALSE);
				DialogBox(hInst, (LPCTSTR)IDD_CONTROLS, hWnd, (DLGPROC)Controls);
				DialogManager::EnableAll(TRUE);
				break;

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
			disasmWindow[0]->Show(g_Config.bShowDebuggerOnLoad);
			memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(memoryWindow[0]);
			if (disasmWindow[0])
				disasmWindow[0]->NotifyMapLoaded();
			if (memoryWindow[0])
				memoryWindow[0]->NotifyMapLoaded();

			if (nextState == CORE_RUNNING)
				PostMessage(hwndMain, WM_COMMAND, ID_EMULATION_RUN, 0);
			UpdateMenus();
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
		CHECKITEM(ID_CPU_INTERPRETER,g_Config.iCpuCore == CPU_INTERPRETER);
		CHECKITEM(ID_CPU_FASTINTERPRETER,g_Config.iCpuCore == CPU_FASTINTERPRETER);
		CHECKITEM(ID_CPU_DYNAREC,g_Config.iCpuCore == CPU_JIT);
		CHECKITEM(ID_OPTIONS_BUFFEREDRENDERING, g_Config.bBufferedRendering);
		CHECKITEM(ID_OPTIONS_SHOWDEBUGSTATISTICS, g_Config.bShowDebugStats);
		CHECKITEM(ID_OPTIONS_WIREFRAME, g_Config.bDrawWireframe);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_OPTIONS_FASTMEMORY, g_Config.bFastMemory);
		CHECKITEM(ID_OPTIONS_LINEARFILTERING, g_Config.bLinearFiltering);
		CHECKITEM(ID_OPTIONS_SIMPLE2XSSAA, g_Config.SSAntiAlaising);
		CHECKITEM(ID_EMULATION_RUNONLOAD, g_Config.bAutoRun);

		UINT enable = !Core_IsStepping() ? MF_GRAYED : MF_ENABLED;
		EnableMenuItem(menu,ID_EMULATION_RUN, g_State.bEmuThreadStarted ? enable : MF_GRAYED);
		EnableMenuItem(menu,ID_EMULATION_PAUSE, g_State.bEmuThreadStarted ? !enable : MF_GRAYED);
		EnableMenuItem(menu,ID_EMULATION_RESET, g_State.bEmuThreadStarted ? MF_ENABLED : MF_GRAYED);

		enable = g_State.bEmuThreadStarted ? MF_GRAYED : MF_ENABLED;
		EnableMenuItem(menu,ID_FILE_LOAD,enable);
		EnableMenuItem(menu,ID_FILE_SAVESTATEFILE,!enable);
		EnableMenuItem(menu,ID_FILE_LOADSTATEFILE,!enable);
		EnableMenuItem(menu,ID_FILE_QUICKSAVESTATE,!enable);
		EnableMenuItem(menu,ID_FILE_QUICKLOADSTATE,!enable);
		EnableMenuItem(menu,ID_CPU_DYNAREC,enable);
		EnableMenuItem(menu,ID_CPU_INTERPRETER,enable);
		EnableMenuItem(menu,ID_CPU_FASTINTERPRETER,enable);
		EnableMenuItem(menu,ID_DVD_INSERTISO,enable);
		EnableMenuItem(menu,ID_FILE_BOOTBIOS,enable);
		EnableMenuItem(menu,ID_EMULATION_STOP,!enable);
		EnableMenuItem(menu,ID_OPTIONS_SETTINGS,enable);
		EnableMenuItem(menu,ID_PLUGINS_CHOOSEPLUGINS,enable);
		EnableMenuItem(menu,ID_OPTIONS_SIMPLE2XSSAA,enable);

		const int zoomitems[4] = {
			ID_OPTIONS_SCREEN1X,
			ID_OPTIONS_SCREEN2X,
			ID_OPTIONS_SCREEN3X,
			ID_OPTIONS_SCREEN4X,
		};
		for (int i = 0; i < 4; i++) {
			CheckMenuItem(menu, zoomitems[i], MF_BYCOMMAND | ((i == g_Config.iWindowZoom - 1) ? MF_CHECKED : MF_UNCHECKED));
		}
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

	const char *controllist[] = {
		"Start\tSpace",
		"Select\tV",
		"Square\tA",
		"Triangle\tS",
		"Circle\tX",
		"Cross\tZ",
		"Left Trigger\tQ",
		"Right Trigger\tW",
		"Up\tArrow Up",
		"Down\tArrow Down",
		"Left\tArrow Left",
		"Right\tArrow Right",
		"Analog Up\tI",
		"Analog Down\tK",
		"Analog Left\tJ",
		"Analog Right\tL",
	};
	// Message handler for about box.
	LRESULT CALLBACK Controls(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
			W32Util::CenterWindow(hDlg);
			{
				// TODO: connect to keyboard device instead
				HWND list = GetDlgItem(hDlg, IDC_LISTCONTROLS);
				int stops[1] = {80};
				SendMessage(list, LB_SETTABSTOPS, 1, (LPARAM)stops);
				for (int i = 0; i < sizeof(controllist)/sizeof(controllist[0]); i++) {
					SendMessage(list, LB_INSERTSTRING, -1, (LPARAM)controllist[i]);
				}
			}
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
	void _ViewNormal(HWND hWnd)
	{
		// put caption and border styles back
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle | WS_CAPTION | WS_THICKFRAME;
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// put back the menu bar
		::SetMenu(hWnd, menu);

		// resize to normal view
		// NOTE: use SWP_FRAMECHANGED to force redraw non-client
		const int x = g_normalRC.left;
		const int y = g_normalRC.top;
		const int cx = g_normalRC.right - g_normalRC.left;
		const int cy = g_normalRC.bottom - g_normalRC.top;
		::SetWindowPos(hWnd, HWND_NOTOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// reset full screen indicator
		g_bFullScreen = FALSE;
	}

	void _ViewFullScreen(HWND hWnd)
	{
		// keep in mind normal window rectangle
		::GetWindowRect(hWnd, &g_normalRC);

		// remove caption and border styles
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME);
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// remove the menu bar
		::SetMenu(hWnd, NULL);

		// resize to full screen view
		// NOTE: use SWP_FRAMECHANGED to force redraw non-client
		const int x = 0;
		const int y = 0;
		const int cx = ::GetSystemMetrics(SM_CXSCREEN);
		const int cy = ::GetSystemMetrics(SM_CYSCREEN);
		::SetWindowPos(hWnd, HWND_TOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// set full screen indicator
		g_bFullScreen = TRUE;
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

	void SaveStateActionFinished(bool result, void *userdata)
	{
		// TODO: Improve messaging?
		if (!result)
			MessageBox(0, "Savestate failure.  Please try again later.", "Sorry", MB_OK);
		SetCursor(LoadCursor(0, IDC_ARROW));

		if (g_State.bEmuThreadStarted && nextState == CORE_RUNNING)
		{
			for (int i=0; i<numCPUs; i++)
				if (disasmWindow[i])
					SendMessage(disasmWindow[i]->GetDlgHandle(), WM_COMMAND, IDC_GO, 0);
		}
	}

	void SetNextState(CoreState state)
	{
		nextState = state;
	}

	HINSTANCE GetHInstance()
	{
		return hInst;
	}
}

