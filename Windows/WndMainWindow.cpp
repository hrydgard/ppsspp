// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <windows.h>
#include <tchar.h>

#include <map>

#include "base/NativeApp.h"
#include "Globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "input/input_state.h"
#include "input/keycodes.h"
#include "Core/Debugger/SymbolMap.h"
#include "Windows/OpenGLBase.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "main.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Windows/EmuThread.h"

#include "resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/WindowsHost.h"
#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "native/image/png_load.h"
#include "GPU/GLES/TextureScaler.h"
#include "ControlMapping.h"
#include "UI/OnScreenDisplay.h"
#include "i18n/i18n.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

#define ENABLE_TOUCH 0

extern std::map<int, int> windowsTransTable;
BOOL g_bFullScreen = FALSE;
static RECT g_normalRC = {0};
extern bool g_TakeScreenshot;
extern InputState input_state;

#define TIMER_CURSORUPDATE 1
#define TIMER_CURSORMOVEUPDATE 2
#define CURSORUPDATE_INTERVAL_MS 50
#define CURSORUPDATE_MOVE_TIMESPAN_MS 500

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	static HMENU menu;

	static HINSTANCE hInst;
	static int cursorCounter = 0;
	static int prevCursorX = -1;
	static int prevCursorY = -1;
	static bool mouseButtonDown = false;
	static bool hideCursor = false;
	static void *rawInputBuffer;
	static size_t rawInputBufferSize;
	static int currentSavestateSlot = 0;

	//W32Util::LayeredWindow *layer;
#define MAX_LOADSTRING 100
	const TCHAR *szTitle = TEXT("PPSSPP");
	const TCHAR *szWindowClass = TEXT("PPSSPPWnd");
	const TCHAR *szDisplayClass = TEXT("PPSSPPDisplay");

	// Forward declarations of functions included in this code module:
	LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWND() {
		return hwndMain;
	}

	HWND GetDisplayHWND() {
		return hwndDisplay;
	}

	void Init(HINSTANCE hInstance) {
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
		// GetWindowRect(hwndMain, &rcInner);
		rcInner.left = 0;
		rcInner.top = 0;

		rcInner.right=480*zoom;//+client edge
		rcInner.bottom=272*zoom; //+client edge

		rcOuter=rcInner;
		AdjustWindowRect(&rcOuter, WS_OVERLAPPEDWINDOW, TRUE);
		rcOuter.right += g_Config.iWindowX - rcOuter.left;
		rcOuter.bottom += g_Config.iWindowY - rcOuter.top;
		rcOuter.left = g_Config.iWindowX;
		rcOuter.top = g_Config.iWindowY;
	}

	void SavePosition() {
		WINDOWPLACEMENT placement;
		GetWindowPlacement(hwndMain, &placement);
		if (placement.showCmd == SW_SHOWNORMAL) {
			RECT rc;
			GetWindowRect(hwndMain, &rc);
			g_Config.iWindowX = rc.left;
			g_Config.iWindowY = rc.top;
		}
	}

	void ResizeDisplay(bool noWindowMovement = false) {
		RECT rc;
		GetClientRect(hwndMain, &rc);
		if (!noWindowMovement) {

			if ((rc.right - rc.left) == PSP_CoreParameter().pixelWidth &&
				(rc.bottom - rc.top) == PSP_CoreParameter().pixelHeight)
				return;
			PSP_CoreParameter().pixelWidth = rc.right - rc.left;
			PSP_CoreParameter().pixelHeight = rc.bottom - rc.top;
			MoveWindow(hwndDisplay, 0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, TRUE);
		}

		// round up to a zoom factor for the render size.
		int zoom = (rc.right - rc.left + 479) / 480;
		if (g_Config.SSAntiAliasing) zoom *= 2;
		PSP_CoreParameter().renderWidth = 480 * zoom;
		PSP_CoreParameter().renderHeight = 272 * zoom;
		PSP_CoreParameter().outputWidth = 480 * zoom;
		PSP_CoreParameter().outputHeight = 272 * zoom;

		if (gpu)
			gpu->Resized();
	}

	void SetZoom(float zoom) {
		if (zoom < 5)
			g_Config.iWindowZoom = (int) zoom;
		RECT rc, rcOuter;
		GetWindowRectAtZoom((int) zoom, rc, rcOuter);
		MoveWindow(hwndMain, rcOuter.left, rcOuter.top, rcOuter.right - rcOuter.left, rcOuter.bottom - rcOuter.top, TRUE);
		ResizeDisplay();
	}

	void CorrectCursor() {
		bool autoHide = g_bFullScreen && !mouseButtonDown && globalUIState == UISTATE_INGAME;
		if (autoHide && hideCursor) {
			while (cursorCounter >= 0) {
				cursorCounter = ShowCursor(FALSE);
			}
		} else {
			hideCursor = !autoHide;
			if (cursorCounter < 0) {
				cursorCounter = ShowCursor(TRUE);
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
		}
	}

	void setTexScalingLevel(int num) {
		g_Config.iTexScalingLevel = num;
		if(gpu) gpu->ClearCacheNextFrame();
	}

	void setTexFiltering(int num) {
		g_Config.iTexFiltering = num;
	}

	void setTexScalingType(int num) {
		g_Config.iTexScalingType = num;
		if(gpu) gpu->ClearCacheNextFrame();
	}

	void setFpsLimit(int fps) {
		g_Config.iFpsLimit = fps;
	}

	void enableCheats(bool cheats){
		g_Config.bEnableCheats = cheats;
	}

	BOOL Show(HINSTANCE hInstance, int nCmdShow)
	{
		hInst = hInstance; // Store instance handle in our global variable

		int zoom = g_Config.iWindowZoom;
		if (zoom < 1) zoom = 1;
		if (zoom > 4) zoom = 4;
		
		RECT rc, rcOrig;
		GetWindowRectAtZoom(zoom, rcOrig, rc);

		u32 style = WS_OVERLAPPEDWINDOW;

		hwndMain = CreateWindowEx(0,szWindowClass, "", style,
			rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
		if (!hwndMain)
			return FALSE;

		hwndDisplay = CreateWindowEx(0, szDisplayClass, TEXT(""), WS_CHILD | WS_VISIBLE,
			rcOrig.left, rcOrig.top, rcOrig.right - rcOrig.left, rcOrig.bottom - rcOrig.top, hwndMain, 0, hInstance, 0);
		if (!hwndDisplay)
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
		UpdateMenus();

		//accept dragged files
		DragAcceptFiles(hwndMain, TRUE);

		hideCursor = true;
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);

		Update();
		SetPlaying(0);

		if(g_Config.bFullScreenOnLaunch)
			_ViewFullScreen(hwndMain);
		
		ShowWindow(hwndMain, nCmdShow);

		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

#if ENABLE_TOUCH
		RegisterTouchWindow(hwndDisplay, TWF_WANTPALM);
#endif

		RAWINPUTDEVICE keyboard;
		memset(&keyboard, 0, sizeof(keyboard));
		keyboard.usUsagePage = 1;
		keyboard.usUsage = 6;
		keyboard.dwFlags = 0; // RIDEV_NOLEGACY | ;
		RegisterRawInputDevices(&keyboard, 1, sizeof(RAWINPUTDEVICE));

		SetFocus(hwndDisplay);

		return TRUE;
	}

	void BrowseAndBoot(std::string defaultPath)
	{
		std::string fn;
		std::string filter = "PSP ROMs (*.iso *.cso *.pbp *.elf)|*.pbp;*.elf;*.iso;*.cso;*.prx|All files (*.*)|*.*||";
		
		for (int i=0; i<(int)filter.length(); i++)
		{
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		// pause if a game is being played
		bool isPaused = false;
		if (globalUIState == UISTATE_INGAME)
		{
			isPaused = Core_IsStepping();
			if (!isPaused)
				Core_EnableStepping(true);
		}

		if (W32Util::BrowseForFileName(true, GetHWND(), "Load File", defaultPath.size() ? defaultPath.c_str() : 0, filter.c_str(),"*.pbp;*.elf;*.iso;*.cso;",fn))
		{
			if (globalUIState == UISTATE_INGAME || globalUIState == UISTATE_PAUSEMENU)
			{
				Core_EnableStepping(false);
			}

			// decode the filename with fullpath
			std::string fullpath = fn;
			char drive[MAX_PATH];
			char dir[MAX_PATH];
			char fname[MAX_PATH];
			char ext[MAX_PATH];
			_splitpath(fullpath.c_str(), drive, dir, fname, ext);

			std::string executable = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			NativeMessageReceived("boot", executable.c_str());
		}
		else
		{
			if (!isPaused)
				Core_EnableStepping(false);
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

		// Poor man's touch - mouse input. We send the data both as an input_state pointer,
		// and as asynchronous touch events for minimal latency.

		case WM_LBUTTONDOWN:
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = true;
				{
					lock_guard guard(input_state.lock);
					input_state.mouse_valid = true;
					input_state.pointer_down[0] = true;

					int factor = g_Config.iWindowZoom == 1 ? 2 : 1;
					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}

				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_DOWN;
				touch.x = input_state.pointer_x[0];
				touch.y = input_state.pointer_y[0];
				NativeTouch(touch);
				SetCapture(hWnd);
				break;
			}

		case WM_MOUSEMOVE:
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = (wParam & MK_LBUTTON) != 0;
				int cursorX = GET_X_LPARAM(lParam);
				int cursorY = GET_Y_LPARAM(lParam);
				if (abs(cursorX - prevCursorX) > 1 || abs(cursorY - prevCursorY) > 1) {
					hideCursor = false;
					SetTimer(hwndMain, TIMER_CURSORMOVEUPDATE, CURSORUPDATE_MOVE_TIMESPAN_MS, 0);
				}
				prevCursorX = cursorX;
				prevCursorY = cursorY;

				{
					lock_guard guard(input_state.lock);
					int factor = g_Config.iWindowZoom == 1 ? 2 : 1;
					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}

				if (wParam & MK_LBUTTON) {
					TouchInput touch;
					touch.id = 0;
					touch.flags = TOUCH_MOVE;
					touch.x = input_state.pointer_x[0];
					touch.y = input_state.pointer_y[0];
					NativeTouch(touch);
				}
			}
			break;

		case WM_LBUTTONUP:
			{
				// Hack: Take the opportunity to hide the cursor.
				mouseButtonDown = false;
				{
					lock_guard guard(input_state.lock);
					input_state.pointer_down[0] = false;
					int factor = g_Config.iWindowZoom == 1 ? 2 : 1;
					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}
				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_UP;
				touch.x = input_state.pointer_x[0];
				touch.y = input_state.pointer_y[0];
				NativeTouch(touch);
				ReleaseCapture();
				break;
			}

		// Actual touch! Unfinished

		case WM_TOUCH:
			{
				// TODO: Enabling this section will probably break things on Windows XP.
				// We probably need to manually fetch pointers to GetTouchInputInfo and CloseTouchInputHandle.
#if ENABLE_TOUCH
				UINT inputCount = LOWORD(wParam);
				TOUCHINPUT *inputs = new TOUCHINPUT[inputCount];
				if (GetTouchInputInfo((HTOUCHINPUT)lParam,
					inputCount,
					inputs,
					sizeof(TOUCHINPUT)))
				{
					for (int i = 0; i < inputCount; i++) {
						// TODO: process inputs here!

					}

					if (!CloseTouchInputHandle((HTOUCHINPUT)lParam))
					{
						// error handling
					}
				}
				else
				{
					// GetLastError() and error handling
				}
				delete [] inputs;
				return DefWindowProc(hWnd, message, wParam, lParam);
#endif
			}


		case WM_PAINT:
			return DefWindowProc(hWnd, message, wParam, lParam);
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	static int GetTrueVKey(const RAWKEYBOARD &kb) {
		switch (kb.VKey) {
		case VK_SHIFT:
			return MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
		case VK_CONTROL:
			if (kb.Flags & RI_KEY_E0)
				return VK_RCONTROL;
			else
				return VK_LCONTROL;

		case VK_MENU:
			if (kb.Flags & RI_KEY_E0)
				return VK_RMENU;  // Right Alt / AltGr
			else
				return VK_LMENU;  // Left Alt
		default:
			return kb.VKey;
		}
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
			SavePosition();
			ResizeDisplay();
			break;

		case WM_SIZE:
			SavePosition();
			ResizeDisplay();
			break;

		case WM_TIMER:
			// Hack: Take the opportunity to also show/hide the mouse cursor in fullscreen mode.
			switch (wParam)
			{
			case TIMER_CURSORUPDATE:
				CorrectCursor();
				return 0;
			case TIMER_CURSORMOVEUPDATE:
				hideCursor = true;
				KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
				return 0;
			}
			break;

		// For some reason, need to catch this here rather than in DisplayProc.
		case WM_MOUSEWHEEL:
			{
				int wheelDelta = (short)(wParam >> 16);
				KeyInput key;
				key.deviceId = DEVICE_ID_MOUSE;

				if (wheelDelta < 0) {
					key.keyCode = KEYCODE_EXT_MOUSEWHEEL_DOWN;
					wheelDelta = -wheelDelta;
				} else {
					key.keyCode = KEYCODE_EXT_MOUSEWHEEL_UP;
				}
				// There's no separate keyup event for mousewheel events, let's pass them both together.
				// This also means it really won't work great for key mapping :( Need to build a 1 frame delay or something.
				key.flags = KEY_DOWN | KEY_UP | KEY_HASWHEELDELTA | (wheelDelta << 16);
				NativeKey(key);
				break;
			}

		case WM_COMMAND:
			{
			if (!EmuThread_Ready())
				return DefWindowProc(hWnd, message, wParam, lParam);
			I18NCategory *g = GetI18NCategory("Graphics");

			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
			case ID_FILE_LOAD:
				BrowseAndBoot("");
				break;

			case ID_FILE_LOAD_MEMSTICK:
				{
					std::string memStickDir, flash0dir;
					GetSysDirectories(memStickDir, flash0dir);
					memStickDir += "PSP\\GAME\\";
					BrowseAndBoot(memStickDir);
				}
				break;

			case ID_FILE_REFRESHGAMELIST:
				break;

			case ID_FILE_MEMSTICK:
				{
					std::string memStickDir, flash0dir;
					GetSysDirectories(memStickDir, flash0dir);
					ShellExecuteA(NULL, "open", memStickDir.c_str(), 0, 0, SW_SHOW);
				}
				break;

			case ID_TOGGLE_PAUSE:
				if (globalUIState == UISTATE_PAUSEMENU)
				{
					NativeMessageReceived("run", "");
					if (disasmWindow[0])
						SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_GO, 0);
				}
				else if (Core_IsStepping()) //It is paused, then continue to run
				{
					if (disasmWindow[0])
						SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_GO, 0);
					else
						Core_EnableStepping(false);
				} else {
					if (disasmWindow[0])
						SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
					else
						Core_EnableStepping(true);
				}
				break;

			case ID_EMULATION_STOP:
				if (memoryWindow[0]) {
					SendMessage(memoryWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
				}
				if (disasmWindow[0]) {
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
				}
				if (Core_IsStepping()) {
					Core_EnableStepping(false);
				}
				NativeMessageReceived("stop", "");
				SetPlaying(0);
				Update();
				break;

			case ID_EMULATION_RESET:
				NativeMessageReceived("reset", "");
				break;

			case ID_EMULATION_SPEEDLIMIT:
				g_Config.bSpeedLimit = !g_Config.bSpeedLimit;
				break;

			case ID_FILE_LOADSTATEFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::Load(fn, SaveStateActionFinished);
				}
				break;

			case ID_FILE_SAVESTATEFILE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::Save(fn, SaveStateActionFinished);
				}
				break;

			// TODO: Improve UI for multiple slots
			case ID_FILE_SAVESTATE_NEXT_SLOT:
			{
				currentSavestateSlot = (currentSavestateSlot + 1)%SaveState::SAVESTATESLOTS;
				char msg[30];
				sprintf(msg, "Using save state slot %d.", currentSavestateSlot + 1);
				osm.Show(msg);
				break;
			}

			case ID_FILE_QUICKLOADSTATE:
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::LoadSlot(currentSavestateSlot, SaveStateActionFinished);
				break;

			case ID_FILE_QUICKSAVESTATE:
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::SaveSlot(currentSavestateSlot, SaveStateActionFinished);
				break;

			case ID_OPTIONS_SCREEN1X:
				SetZoom(1);
				break;
			case ID_OPTIONS_SCREEN2X:
				SetZoom(2);
				break;
			case ID_OPTIONS_SCREEN3X:
				SetZoom(3);
				break;
			case ID_OPTIONS_SCREEN4X:
				SetZoom(4);
				break;

			case ID_OPTIONS_MIPMAP:
				g_Config.bMipMap = !g_Config.bMipMap;
				break;

			case ID_OPTIONS_VSYNC:
				g_Config.iVSyncInterval = !g_Config.iVSyncInterval;
				break;

			case ID_TEXTURESCALING_OFF:
				setTexScalingLevel(1);
				break;
			case ID_TEXTURESCALING_2X:
				setTexScalingLevel(2);
				break;
			case ID_TEXTURESCALING_3X:
				setTexScalingLevel(3);
				break;
			case ID_TEXTURESCALING_4X:
				setTexScalingLevel(4);
				break;
			case ID_TEXTURESCALING_5X:
				setTexScalingLevel(5);
				break;

			case ID_TEXTURESCALING_XBRZ:
				setTexScalingType(TextureScaler::XBRZ);
				break;
			case ID_TEXTURESCALING_HYBRID:
				setTexScalingType(TextureScaler::HYBRID);
				break;
			case ID_TEXTURESCALING_BICUBIC:
				setTexScalingType(TextureScaler::BICUBIC);
				break;
			case ID_TEXTURESCALING_HYBRID_BICUBIC:
				setTexScalingType(TextureScaler::HYBRID_BICUBIC);
				break;

			case ID_TEXTURESCALING_DEPOSTERIZE:
				g_Config.bTexDeposterize = !g_Config.bTexDeposterize;
				if(gpu) gpu->ClearCacheNextFrame();
				break;

			case ID_OPTIONS_BUFFEREDRENDERING:
				g_Config.bBufferedRendering = !g_Config.bBufferedRendering;
				osm.ShowOnOff(g->T("Buffered Rendering"), g_Config.bBufferedRendering);
				if (gpu)
					gpu->Resized();  // easy way to force a clear...
				break;

			case ID_OPTIONS_READFBOTOMEMORY:
				g_Config.bFramebuffersToMem = !g_Config.bFramebuffersToMem;
				osm.ShowOnOff(g->T("Read Framebuffers To Memory"), g_Config.bFramebuffersToMem);
				if (gpu)
					gpu->Resized();  // easy way to force a clear...
				break;

			case ID_OPTIONS_SHOWDEBUGSTATISTICS:
				g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
				break;

			case ID_OPTIONS_HARDWARETRANSFORM:
				g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
				osm.ShowOnOff(g->T("Hardware Transform"), g_Config.bHardwareTransform);
				break;

			case ID_OPTIONS_STRETCHDISPLAY:
				g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
				if (gpu)
					gpu->Resized();  // easy way to force a clear...
				break;

			case ID_OPTIONS_FRAMESKIP:
				g_Config.iFrameSkip = g_Config.iFrameSkip == 0 ? 1 : 0;
				osm.ShowOnOff(g->T("Frame Skipping"), g_Config.iFrameSkip != 0);
				break;

			case ID_FILE_EXIT:
				DestroyWindow(hWnd);
				break;

			case ID_CPU_DYNAREC:
				g_Config.bJit = true;
				osm.ShowOnOff(g->T("Dynarec", "Dynarec (JIT)"), g_Config.bJit);
				break;	

			case ID_CPU_INTERPRETER:
				g_Config.bJit = false;
				break;

			case ID_EMULATION_RUNONLOAD:
				g_Config.bAutoRun = !g_Config.bAutoRun;
				break;

			case ID_DEBUG_DUMPNEXTFRAME:
				if (gpu)
					gpu->DumpNextFrame();
				break;

			case ID_DEBUG_LOADMAPFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn)) {
					symbolMap.LoadSymbolMap(fn.c_str());
//					HLE_PatchFunctions();
					if (disasmWindow[0])
						disasmWindow[0]->NotifyMapLoaded();
					if (memoryWindow[0])
						memoryWindow[0]->NotifyMapLoaded();
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
				break;

			case ID_OPTIONS_FULLSCREEN:
				g_Config.bFullScreen = !g_Config.bFullScreen ;
				if(g_bFullScreen) {
					_ViewNormal(hWnd); 
				} else {
					_ViewFullScreen(hWnd);
				}
				break;

			case ID_OPTIONS_VERTEXCACHE:
				g_Config.bVertexCache = !g_Config.bVertexCache;
				break;
			case ID_OPTIONS_SHOWFPS:
				g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter;
				break;
			case ID_OPTIONS_DISPLAYRAWFRAMEBUFFER:
				g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
				break;
			case ID_OPTIONS_FASTMEMORY:
				g_Config.bFastMemory = !g_Config.bFastMemory;
				break;
			case ID_OPTIONS_USEVBO:
				g_Config.bUseVBO = !g_Config.bUseVBO;
				break;
			case ID_OPTIONS_TEXTUREFILTERING_AUTO:
				setTexFiltering(0);
				break;
			case ID_OPTIONS_NEARESTFILTERING:
				setTexFiltering(2) ;
				break;
			case ID_OPTIONS_LINEARFILTERING:
				setTexFiltering(3) ;
				break;
			case ID_OPTIONS_LINEARFILTERING_CG:
				setTexFiltering(4) ;
				break;
			case ID_OPTIONS_TOPMOST:
				g_Config.bTopMost = !g_Config.bTopMost;
				W32Util::MakeTopMost(hWnd, g_Config.bTopMost);
				break;

			case ID_OPTIONS_SIMPLE2XSSAA:
				g_Config.SSAntiAliasing = !g_Config.SSAntiAliasing;
				ResizeDisplay(true);
				break;
			case ID_OPTIONS_CONTROLS:
				MessageBox(hWnd, "Control mapping has been moved to the in-window Settings menu.\n", "Sorry", 0);
				break;

			case ID_EMULATION_SOUND:
				g_Config.bEnableSound = !g_Config.bEnableSound;
				break;

			case ID_HELP_OPENWEBSITE:
				ShellExecute(NULL, "open", "http://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
				break;

			case ID_HELP_OPENFORUM:
				ShellExecute(NULL, "open", "http://forums.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
				break;

			case ID_HELP_ABOUT:
				DialogManager::EnableAll(FALSE);
				DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				DialogManager::EnableAll(TRUE);
				break;
			case ID_DEBUG_TAKESCREENSHOT:
				g_TakeScreenshot = true;
				break;

			default:
				MessageBox(hwndMain,"Unimplemented","Sorry",0);
				break;
			}
			}
			break;

		case WM_INPUT:
			{
				UINT dwSize;
				GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
				if (!rawInputBuffer) {
					rawInputBuffer = malloc(dwSize);
					rawInputBufferSize = dwSize;
				}
				if (dwSize > rawInputBufferSize) {
					rawInputBuffer = realloc(rawInputBuffer, dwSize);
				}
				GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawInputBuffer, &dwSize, sizeof(RAWINPUTHEADER));
				RAWINPUT* raw = (RAWINPUT*)rawInputBuffer;

				if (raw->header.dwType == RIM_TYPEKEYBOARD) {
					KeyInput key;
					key.deviceId = DEVICE_ID_KEYBOARD;
					if (raw->data.keyboard.Message == WM_KEYDOWN || raw->data.keyboard.Message == WM_SYSKEYDOWN) {
						key.flags = KEY_DOWN;
						key.keyCode = windowsTransTable[GetTrueVKey(raw->data.keyboard)];
						if (key.keyCode) {
							NativeKey(key);
						}
					} else if (raw->data.keyboard.Message == WM_KEYUP) {
						key.flags = KEY_UP;
						key.keyCode = windowsTransTable[GetTrueVKey(raw->data.keyboard)];
						if (key.keyCode) {
							NativeKey(key);	
						}
					}
				}
			}
			return 0;

		case WM_DROPFILES:
			{
				if (!EmuThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

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

					SendMessage(hWnd, WM_COMMAND, ID_EMULATION_STOP, 0);
					// Ugly, need to wait for the stop message to process in the EmuThread.
					Sleep(20);
					
					MainWindow::SetPlaying(filename);
					MainWindow::Update();

					NativeMessageReceived("boot", filename);
				}
			}
			break;

		case WM_CLOSE:
			/*
			if (g_Config.bConfirmOnQuit && __KernelIsRunning())
				if (IDYES != MessageBox(hwndMain, "A game is in progress. Are you sure you want to exit?",
					"Are you sure?", MB_YESNO | MB_ICONQUESTION))
					return 0;
			//*/
			EmuThread_Stop();

			return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_DESTROY:
			KillTimer(hWnd, TIMER_CURSORUPDATE);
			KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
			PostQuitMessage(0);
			break;

		case WM_USER+1:
			if (disasmWindow[0])
				SendMessage(disasmWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
			if (memoryWindow[0])
				SendMessage(memoryWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);

			disasmWindow[0] = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(disasmWindow[0]);
			disasmWindow[0]->Show(g_Config.bShowDebuggerOnLoad);
			if (g_Config.bFullScreen)
				_ViewFullScreen(hWnd);
			memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(memoryWindow[0]);
			if (disasmWindow[0])
				disasmWindow[0]->NotifyMapLoaded();
			if (memoryWindow[0])
				memoryWindow[0]->NotifyMapLoaded();

			SetForegroundWindow(hwndMain);
			break;


		case WM_MENUSELECT:
			// Unfortunately, accelerate keys (hotkeys) shares the same enabled/disabled states
			// with corresponding menu items.
			UpdateMenus();
			break;

		// Turn off the screensaver.
		// Note that if there's a screensaver password, this simple method
		// doesn't work on Vista or higher.
		case WM_SYSCOMMAND:
			{
				switch (wParam)
				{
				case SC_SCREENSAVE:  
					return 0;
				case SC_MONITORPOWER:
					return 0;      
				}
				return DefWindowProc(hWnd, message, wParam, lParam);
			}

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
		CHECKITEM(ID_CPU_INTERPRETER,g_Config.bJit == false);
		CHECKITEM(ID_CPU_DYNAREC,g_Config.bJit == true);
		CHECKITEM(ID_OPTIONS_BUFFEREDRENDERING, g_Config.bBufferedRendering);
		CHECKITEM(ID_OPTIONS_READFBOTOMEMORY, g_Config.bFramebuffersToMem);
		CHECKITEM(ID_OPTIONS_SHOWDEBUGSTATISTICS, g_Config.bShowDebugStats);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_OPTIONS_FASTMEMORY, g_Config.bFastMemory);
		CHECKITEM(ID_OPTIONS_SIMPLE2XSSAA, g_Config.SSAntiAliasing);
		CHECKITEM(ID_OPTIONS_STRETCHDISPLAY, g_Config.bStretchToDisplay);
		CHECKITEM(ID_EMULATION_RUNONLOAD, g_Config.bAutoRun);
		CHECKITEM(ID_OPTIONS_USEVBO, g_Config.bUseVBO);
		CHECKITEM(ID_OPTIONS_VERTEXCACHE, g_Config.bVertexCache);
		CHECKITEM(ID_OPTIONS_SHOWFPS, g_Config.iShowFPSCounter);
		CHECKITEM(ID_OPTIONS_FRAMESKIP, g_Config.iFrameSkip != 0);
		CHECKITEM(ID_OPTIONS_MIPMAP, g_Config.bMipMap);
		CHECKITEM(ID_OPTIONS_VSYNC, g_Config.iVSyncInterval != 0);
		CHECKITEM(ID_OPTIONS_TOPMOST, g_Config.bTopMost);
		CHECKITEM(ID_EMULATION_SOUND, g_Config.bEnableSound);
		CHECKITEM(ID_TEXTURESCALING_DEPOSTERIZE, g_Config.bTexDeposterize);
		
		static const int zoomitems[4] = {
			ID_OPTIONS_SCREEN1X,
			ID_OPTIONS_SCREEN2X,
			ID_OPTIONS_SCREEN3X,
			ID_OPTIONS_SCREEN4X,
		};
		for (int i = 0; i < 4; i++) {
			CheckMenuItem(menu, zoomitems[i], MF_BYCOMMAND | ((i == g_Config.iWindowZoom - 1) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingitems[] = {
			ID_TEXTURESCALING_OFF,
			ID_TEXTURESCALING_2X,
			ID_TEXTURESCALING_3X,
			ID_TEXTURESCALING_4X,
			ID_TEXTURESCALING_5X,
		};
		for (int i = 0; i < 5; i++) {
			CheckMenuItem(menu, texscalingitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingLevel-1) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingtypeitems[] = {
			ID_TEXTURESCALING_XBRZ,
			ID_TEXTURESCALING_HYBRID,
			ID_TEXTURESCALING_BICUBIC,
			ID_TEXTURESCALING_HYBRID_BICUBIC,
		};
		for (int i = 0; i < 4; i++) {
			CheckMenuItem(menu, texscalingtypeitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingType) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texfilteringitems[] = {
			ID_OPTIONS_TEXTUREFILTERING_AUTO,
			ID_OPTIONS_NEARESTFILTERING,
			ID_OPTIONS_LINEARFILTERING,
			ID_OPTIONS_LINEARFILTERING_CG,
		};
		for (int i = 0; i < 4; i++) {
			int texFilterLevel = i > 0? (g_Config.iTexFiltering - 1) : g_Config.iTexFiltering;
			CheckMenuItem(menu, texfilteringitems[i], MF_BYCOMMAND | ((i == texFilterLevel) ? MF_CHECKED : MF_UNCHECKED));
		}

		UpdateCommands();
	}

	void UpdateCommands()
	{
		static GlobalUIState lastGlobalUIState = UISTATE_PAUSEMENU;
		static CoreState lastCoreState = CORE_ERROR;

		if (lastGlobalUIState == globalUIState && lastCoreState == coreState)
			return;

		lastCoreState = coreState;
		lastGlobalUIState = globalUIState;

		HMENU menu = GetMenu(GetHWND());

		const char* pauseMenuText =  (Core_IsStepping() || globalUIState != UISTATE_INGAME) ? "Run\tF8" : "Pause\tF8";
		ModifyMenu(menu, ID_TOGGLE_PAUSE, MF_BYCOMMAND | MF_STRING, ID_TOGGLE_PAUSE, pauseMenuText);

		UINT ingameEnable = globalUIState == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED;
		EnableMenuItem(menu,ID_TOGGLE_PAUSE, ingameEnable);
		EnableMenuItem(menu,ID_EMULATION_STOP, ingameEnable);
		EnableMenuItem(menu,ID_EMULATION_RESET, ingameEnable);

		UINT menuEnable = globalUIState == UISTATE_MENU ? MF_ENABLED : MF_GRAYED;
		EnableMenuItem(menu,ID_FILE_SAVESTATEFILE, !menuEnable);
		EnableMenuItem(menu,ID_FILE_LOADSTATEFILE, !menuEnable);
		EnableMenuItem(menu,ID_FILE_QUICKSAVESTATE, !menuEnable);
		EnableMenuItem(menu,ID_FILE_QUICKLOADSTATE, !menuEnable);
		EnableMenuItem(menu,ID_CPU_DYNAREC, menuEnable);
		EnableMenuItem(menu,ID_CPU_INTERPRETER, menuEnable);
		EnableMenuItem(menu,ID_TOGGLE_PAUSE, !menuEnable);
		EnableMenuItem(menu,ID_EMULATION_STOP, !menuEnable);
		EnableMenuItem(menu,ID_EMULATION_RESET, !menuEnable);
	}

	// Message handler for about box.
	LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
			W32Util::CenterWindow(hDlg);
			{
				HWND versionBox = GetDlgItem(hDlg, IDC_VERSION);
				char temp[256];
				sprintf(temp, "PPSSPP %s", PPSSPP_GIT_VERSION);
				SetWindowText(versionBox, temp);
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
		CorrectCursor();
		ResizeDisplay();
		ShowOwnedPopups(hwndMain, TRUE);
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
		CorrectCursor();
		ResizeDisplay();
		ShowOwnedPopups(hwndMain, FALSE);
	}

	void SetPlaying(const char *text)
	{
		char temp[256];
		if (text == 0)
			snprintf(temp, 256, "PPSSPP %s", PPSSPP_GIT_VERSION);
		else
			snprintf(temp, 256, "%s - PPSSPP %s", text, PPSSPP_GIT_VERSION);
		temp[255] = '\0';
		SetWindowText(hwndMain, temp);
	}

	void SaveStateActionFinished(bool result, void *userdata)
	{
		SetCursor(LoadCursor(0, IDC_ARROW));
	}

	HINSTANCE GetHInstance()
	{
		return hInst;
	}
}

