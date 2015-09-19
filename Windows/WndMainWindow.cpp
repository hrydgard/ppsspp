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

// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.
// It's improving slowly, though. :)

#include "Common/CommonWindows.h"
#include "Common/KeyMap.h"
#include <Windowsx.h>

#include <map>
#include <string>

#include "base/NativeApp.h"
#include "base/timeutil.h"
#include "Globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "i18n/i18n.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "thread/threadutil.h"
#include "util/text/utf8.h"

#include "Core/Debugger/SymbolMap.h"
#include "Windows/InputBox.h"
#include "Windows/OpenGLBase.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "main.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/MIPS/JitCommon/NativeJit.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Windows/EmuThread.h"

#include "resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/WindowsHost.h"
#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/RawInput.h"
#include "Windows/TouchInputHandler.h"
#include "GPU/GPUInterface.h"
#include "UI/OnScreenDisplay.h"
#include "Windows/MainWindowMenu.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

#define MOUSEEVENTF_FROMTOUCH_NOPEN 0xFF515780 //http://msdn.microsoft.com/en-us/library/windows/desktop/ms703320(v=vs.85).aspx
#define MOUSEEVENTF_MASK_PLUS_PENTOUCH 0xFFFFFF80

int verysleepy__useSendMessage = 1;

const UINT WM_VERYSLEEPY_MSG = WM_APP + 0x3117;
// Respond TRUE to a message with this param value to indicate support.
const WPARAM VERYSLEEPY_WPARAM_SUPPORTED = 0;
// Respond TRUE to a message wit this param value after filling in the addr name.
const WPARAM VERYSLEEPY_WPARAM_GETADDRINFO = 1;

struct VerySleepy_AddrInfo {
	// Always zero for now.
	int flags;
	// This is the pointer (always passed as 64 bits.)
	unsigned long long addr;
	// Write the name here.
	wchar_t name[256];
};

static RECT g_normalRC = {0};
static std::wstring windowTitle;
extern InputState input_state;

#define TIMER_CURSORUPDATE 1
#define TIMER_CURSORMOVEUPDATE 2
#define CURSORUPDATE_INTERVAL_MS 1000
#define CURSORUPDATE_MOVE_TIMESPAN_MS 500

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	TouchInputHandler touchHandler;
	static HMENU menu;

	HINSTANCE hInst;
	static int cursorCounter = 0;
	static int prevCursorX = -1;
	static int prevCursorY = -1;
	static bool mouseButtonDown = false;
	static bool hideCursor = false;
	static W32Util::AsyncBrowseDialog *browseDialog;
	static bool browsePauseAfter;
	static bool g_inModeSwitch; // when true, don't react to WM_SIZE
	static int g_WindowState;

	// gross hack
	bool noFocusPause = false;	// TOGGLE_PAUSE state to override pause on lost focus

#define MAX_LOADSTRING 100
	const TCHAR *szTitle = TEXT("PPSSPP");
	const TCHAR *szWindowClass = TEXT("PPSSPPWnd");
	const TCHAR *szDisplayClass = TEXT("PPSSPPDisplay");

	// Forward declarations of functions included in this code module:
	LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);

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
		wcex.style = CS_PARENTDC;
		wcex.lpfnWndProc	= (WNDPROC)WndProc;
		wcex.cbClsExtra		= 0;
		wcex.cbWndExtra		= 0;
		wcex.hInstance		= hInstance;
		wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP); 
		wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName	= (LPCWSTR)IDR_MENU1;
		wcex.lpszClassName	= szWindowClass;
		wcex.hIconSm		= (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16, 16, LR_SHARED);
		RegisterClassEx(&wcex);

		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = (WNDPROC)DisplayProc;
		wcex.hIcon = 0;
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = szDisplayClass;
		wcex.hIconSm = 0;
		RegisterClassEx(&wcex);
	}

	void SavePosition() {
		if (g_Config.bFullScreen)
			return;

		WINDOWPLACEMENT placement;
		GetWindowPlacement(hwndMain, &placement);
		if (placement.showCmd == SW_SHOWNORMAL) {
			RECT rc;
			GetWindowRect(hwndMain, &rc);
			g_Config.iWindowX = rc.left;
			g_Config.iWindowY = rc.top;
			g_Config.iWindowWidth = rc.right - rc.left;
			g_Config.iWindowHeight = rc.bottom - rc.top;
		}
	}

	static void GetWindowRectAtResolution(int xres, int yres, RECT &rcInner, RECT &rcOuter) {
		rcInner.left = 0;
		rcInner.top = 0;

		rcInner.right = xres;
		rcInner.bottom = yres;

		rcOuter = rcInner;
		AdjustWindowRect(&rcOuter, WS_OVERLAPPEDWINDOW, TRUE);
		rcOuter.right += g_Config.iWindowX - rcOuter.left;
		rcOuter.bottom += g_Config.iWindowY - rcOuter.top;
		rcOuter.left = g_Config.iWindowX;
		rcOuter.top = g_Config.iWindowY;
	}

	static void ShowScreenResolution() {
		I18NCategory *gr = GetI18NCategory("Graphics");

		std::ostringstream messageStream;
		messageStream << gr->T("Internal Resolution") << ": ";
		messageStream << PSP_CoreParameter().renderWidth << "x" << PSP_CoreParameter().renderHeight << " ";
		messageStream << gr->T("Window Size") << ": ";
		messageStream << PSP_CoreParameter().pixelWidth << "x" << PSP_CoreParameter().pixelHeight;

		osm.Show(messageStream.str(), 2.0f);
	}

	static void UpdateRenderResolution() {
		RECT rc;
		GetClientRect(hwndMain, &rc);

		// Actually, auto mode should be more granular...
		// Round up to a zoom factor for the render size.
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) { // auto mode
			// Use the longest dimension
			if (g_Config.IsPortrait()) {
				zoom = (rc.bottom - rc.top + 479) / 480;
			} else {
				zoom = (rc.right - rc.left + 479) / 480;
			}
		}
		if (zoom <= 1)
			zoom = 1;

		if (g_Config.IsPortrait()) {
			PSP_CoreParameter().renderWidth = 272 * zoom;
			PSP_CoreParameter().renderHeight = 480 * zoom;
		} else {
			PSP_CoreParameter().renderWidth = 480 * zoom;
			PSP_CoreParameter().renderHeight = 272 * zoom;
		}
	}

	static bool IsWindowSmall() {
		// Can't take this from config as it will not be set if windows is maximized.
		RECT rc;
		GetWindowRect(hwndMain, &rc);
		int width = rc.right - rc.left;
		int height = rc.bottom - rc.top;
		return g_Config.IsPortrait() ? (height < 480 + 80) : (width < 480 + 80);
	} 

	static void ResizeDisplay(bool noWindowMovement = false) {
		AssertCurrentThreadName("Main");
		int width = 0, height = 0;
		RECT rc;
		GetClientRect(hwndMain, &rc);
		if (!noWindowMovement) {
			width = rc.right - rc.left;
			height = rc.bottom - rc.top;
			// Moves the internal window, not the frame. TODO: Get rid of the internal window. Tried before but Intel drivers screw up when minimizing, or something?
			MoveWindow(hwndDisplay, 0, 0, width, height, TRUE);
			// This is taken care of anyway later, but makes sure that ShowScreenResolution gets the right numbers.
			// Need to clean all of this up...
			PSP_CoreParameter().pixelWidth = width;
			PSP_CoreParameter().pixelHeight = height;
		}

		UpdateRenderResolution();
		
		if (!noWindowMovement) {
			if (UpdateScreenScale(width, height, IsWindowSmall())) {
				NativeMessageReceived("gpu resized", "");
			}
		}
	}

	void SetWindowSize(int zoom) {
		AssertCurrentThreadName("Main");
		RECT rc, rcOuter;

		// Actually, auto mode should be more granular...
		if (g_Config.IsPortrait()) {
			GetWindowRectAtResolution(272 * (int)zoom, 480 * (int)zoom, rc, rcOuter);
		} else {
			GetWindowRectAtResolution(480 * (int)zoom, 272 * (int)zoom, rc, rcOuter);
		}
		MoveWindow(hwndMain, rcOuter.left, rcOuter.top, rcOuter.right - rcOuter.left, rcOuter.bottom - rcOuter.top, TRUE);
		ResizeDisplay(false);
		ShowScreenResolution();
	}

	void SetInternalResolution(int res) {
		if (res >= 0 && res <= RESOLUTION_MAX)
			g_Config.iInternalResolution = res;
		else {
			if (++g_Config.iInternalResolution > RESOLUTION_MAX)
				g_Config.iInternalResolution = 0;
		}
		
		// Taking auto-texture scaling into account
		if (g_Config.iTexScalingLevel == TEXSCALING_AUTO)
			setTexScalingMultiplier(0);

		if (gpu)
			gpu->Resized();

		UpdateRenderResolution();
		ShowScreenResolution();
	}

	void CorrectCursor() {
		bool autoHide = g_Config.bFullScreen && !mouseButtonDown && GetUIState() == UISTATE_INGAME;
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

	void ToggleFullscreen(HWND hWnd, bool goingFullscreen) {
		// Make sure no rendering is happening during the switch.
		Core_NotifyWindowHidden(true);
		g_inModeSwitch = true;  // Make sure WM_SIZE doesn't call Core_NotifyWindowHidden(false)...

		DWORD dwOldStyle;
		DWORD dwNewStyle;

		if (!goingFullscreen) {
			// Put caption and border styles back.
			dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);

			dwOldStyle &= ~WS_POPUP;

			dwNewStyle = dwOldStyle | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
			
			// Put back the menu bar.
			::SetMenu(hWnd, menu);
		} else {
			// If the window was maximized before going fullscreen, make sure to restore first
			// in order not to have the taskbar show up on top of PPSSPP.
			if (g_WindowState == SIZE_MAXIMIZED) {
				ShowWindow(hwndMain, SW_RESTORE);
			}
			// Remember the normal window rectangle.
			::GetWindowRect(hWnd, &g_normalRC);

			// Remove caption and border styles.
			dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
			dwNewStyle = dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);

			// Add WS_POPUP
			dwNewStyle |= WS_POPUP;
		}

		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// Remove the menu bar.
		::SetMenu(hWnd, goingFullscreen ? NULL : menu);

		// Resize to the appropriate view.
		// If we're returning to window mode, re-apply the appropriate size setting.
		if (goingFullscreen) {
			ShowWindow(hwndMain, SW_MAXIMIZE);
		} else {
			ShowWindow(hwndMain, g_WindowState == SIZE_MAXIMIZED ? SW_MAXIMIZE : SW_RESTORE);
		}

		g_Config.bFullScreen = goingFullscreen;
		CorrectCursor();

		bool showOSM = (g_Config.iInternalResolution == RESOLUTION_AUTO);
		ResizeDisplay(false);
		if (showOSM) {
			ShowScreenResolution();
		}
		ShowOwnedPopups(hwndMain, goingFullscreen ? FALSE : TRUE);
		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		g_inModeSwitch = false;
		Core_NotifyWindowHidden(false);
		WindowsRawInput::NotifyMenu();
	}

	RECT DetermineWindowRectangle() {
		RECT rc;

		const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

		if (!g_Config.bFullScreen) {
			bool visibleHorizontally = ((g_Config.iWindowX + g_Config.iWindowWidth) >= screenX) &&
				((g_Config.iWindowX + g_Config.iWindowWidth) < (screenWidth + g_Config.iWindowWidth));

			bool visibleVertically = ((g_Config.iWindowY + g_Config.iWindowHeight) >= screenY) &&
				((g_Config.iWindowY + g_Config.iWindowHeight) < (screenHeight + g_Config.iWindowHeight));

			if (!visibleHorizontally)
				g_Config.iWindowX = -1;

			if (!visibleVertically)
				g_Config.iWindowY = -1;
		}

		rc.left = g_Config.iWindowX;
		rc.top = g_Config.iWindowY;

		// First, get the w/h right.
		if (g_Config.iWindowWidth <= 0 || g_Config.iWindowHeight <= 0) {
			RECT rcInner = rc, rcOuter;
			bool portrait = g_Config.IsPortrait();
			GetWindowRectAtResolution(2 * (portrait ? 272 : 480), 2 * (portrait ? 480 : 272), rcInner, rcOuter);
			rc.right = rc.left + (rcOuter.right - rcOuter.left);
			rc.bottom = rc.top + (rcOuter.bottom - rcOuter.top);
			g_Config.iWindowWidth = rc.right - rc.left;
			g_Config.iWindowHeight = rc.bottom - rc.top;
		} else {
			rc.right = rc.left + g_Config.iWindowWidth;
			rc.bottom = rc.top + g_Config.iWindowHeight;
		}

		// Then center if necessary.
		if (g_Config.iWindowX == -1 && g_Config.iWindowY == -1) {
			// Center the window.
			const int primaryScreenWidth = GetSystemMetrics(SM_CXSCREEN);
			const int primaryScreenHeight = GetSystemMetrics(SM_CYSCREEN);
			g_Config.iWindowX = (primaryScreenWidth - g_Config.iWindowWidth) / 2;
			g_Config.iWindowY = (primaryScreenHeight - g_Config.iWindowHeight) / 2;
			rc.left = g_Config.iWindowX;
			rc.top = g_Config.iWindowY;
			rc.right = rc.left + g_Config.iWindowWidth;
			rc.bottom = rc.top + g_Config.iWindowHeight;
		}

		return rc;
	}

	void UpdateWindowTitle() {
		// Seems to be fine to call now since we use a UNICODE build...
		SetWindowText(hwndMain, windowTitle.c_str());
	}

	void SetWindowTitle(const wchar_t *title) {
		windowTitle = title;
	}

	BOOL Show(HINSTANCE hInstance) {
		hInst = hInstance; // Store instance handle in our global variable.
		RECT rc = DetermineWindowRectangle();

		u32 style = WS_OVERLAPPEDWINDOW;

		hwndMain = CreateWindowEx(0,szWindowClass, L"", style,
			rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
		if (!hwndMain)
			return FALSE;

		RECT rcClient;
		GetClientRect(hwndMain, &rcClient);

		hwndDisplay = CreateWindowEx(0, szDisplayClass, L"", WS_CHILD | WS_VISIBLE,
			0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hwndMain, 0, hInstance, 0);
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
		for (int i = 0; i < GetMenuItemCount(menu); i++) {
			SetMenuInfo(GetSubMenu(menu,i), &info);
		}
		UpdateMenus();

		// Accept dragged files.
		DragAcceptFiles(hwndMain, TRUE);

		hideCursor = true;
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);

		ToggleFullscreen(hwndMain, g_Config.bFullScreen);

		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		touchHandler.registerTouchWindow(hwndDisplay);

		WindowsRawInput::Init();

		SetFocus(hwndMain);

		return TRUE;
	}

	void CreateDebugWindows() {
		disasmWindow[0] = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
		DialogManager::AddDlg(disasmWindow[0]);
		disasmWindow[0]->Show(g_Config.bShowDebuggerOnLoad);

		geDebuggerWindow = new CGEDebugger(MainWindow::GetHInstance(), MainWindow::GetHWND());
		DialogManager::AddDlg(geDebuggerWindow);

		memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
		DialogManager::AddDlg(memoryWindow[0]);
	}

	void DestroyDebugWindows() {
		DialogManager::RemoveDlg(disasmWindow[0]);
		if (disasmWindow[0])
			delete disasmWindow[0];
		disasmWindow[0] = 0;
		
		DialogManager::RemoveDlg(geDebuggerWindow);
		if (geDebuggerWindow)
			delete geDebuggerWindow;
		geDebuggerWindow = 0;
		
		DialogManager::RemoveDlg(memoryWindow[0]);
		if (memoryWindow[0])
			delete memoryWindow[0];
		memoryWindow[0] = 0;
	}

	void BrowseAndBoot(std::string defaultPath, bool browseDirectory) {
		static std::wstring filter = L"All supported file types (*.iso *.cso *.pbp *.elf *.prx *.zip)|*.pbp;*.elf;*.iso;*.cso;*.prx;*.zip|PSP ROMs (*.iso *.cso *.pbp *.elf *.prx)|*.pbp;*.elf;*.iso;*.cso;*.prx|Homebrew/Demos installers (*.zip)|*.zip|All files (*.*)|*.*||";
		for (int i = 0; i < (int)filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		browsePauseAfter = false;
		if (GetUIState() == UISTATE_INGAME) {
			browsePauseAfter = Core_IsStepping();
			if (!browsePauseAfter)
				Core_EnableStepping(true);
		}

		W32Util::MakeTopMost(GetHWND(), false);
		if (browseDirectory) {
			browseDialog = new W32Util::AsyncBrowseDialog(GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"Choose directory");
		} else {
			browseDialog = new W32Util::AsyncBrowseDialog(W32Util::AsyncBrowseDialog::OPEN, GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"LoadFile", ConvertUTF8ToWString(defaultPath), filter, L"*.pbp;*.elf;*.iso;*.cso;");
		}
	}

	void BrowseAndBootDone() {
		std::string filename;
		if (!browseDialog->GetResult(filename)) {
			if (!browsePauseAfter) {
				Core_EnableStepping(false);
			}
		} else {
			if (GetUIState() == UISTATE_INGAME || GetUIState() == UISTATE_PAUSEMENU) {
				Core_EnableStepping(false);
			}

			// TODO: What is this for / what does it fix?
			if (browseDialog->GetType() != W32Util::AsyncBrowseDialog::DIR) {
				// Decode the filename with fullpath.
				char drive[MAX_PATH];
				char dir[MAX_PATH];
				char fname[MAX_PATH];
				char ext[MAX_PATH];
				_splitpath(filename.c_str(), drive, dir, fname, ext);

				filename = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			}

			filename = ReplaceAll(filename, "\\", "/");
			NativeMessageReceived("boot", filename.c_str());
		}

		W32Util::MakeTopMost(GetHWND(), g_Config.bTopMost);

		delete browseDialog;
		browseDialog = 0;
	}


	LRESULT CALLBACK DisplayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
		// Only apply a factor > 1 in windowed mode.
		int factor = !IsZoomed(GetHWND()) && !g_Config.bFullScreen && IsWindowSmall() ? 2 : 1;
		static bool firstErase = true;

		switch (message) {
		case WM_ACTIVATE:
			if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
				g_activeWindow = WINDOW_MAINWINDOW;
			}
			break;

		case WM_SIZE:
			ILOG("WM_SIZE inner (%d)", (int)wParam);
			break;

		case WM_SETFOCUS:
			break;

		case WM_ERASEBKGND:
			if (firstErase) {
				firstErase = false;
				// Paint black on first erase while OpenGL stuff is loading
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			// Then never erase, let the OpenGL drawing take care of everything.
			return 1;

		// Poor man's touch - mouse input. We send the data both as an input_state pointer,
		// and as asynchronous touch events for minimal latency.
		case WM_LBUTTONDOWN:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = true;
				{
					lock_guard guard(input_state.lock);
					input_state.mouse_valid = true;
					input_state.pointer_down[0] = true;

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

				// Simulate doubleclick, doesn't work with RawInput enabled
				static double lastMouseDown;
				double now = real_time_now();
				if ((now - lastMouseDown) < 0.001 * GetDoubleClickTime()) {
					if (!g_Config.bShowTouchControls && GetUIState() == UISTATE_INGAME) {
						PostMessage(hwndMain, WM_USER_TOGGLE_FULLSCREEN, 0, 0);
					}
					lastMouseDown = 0.0;
				} else {
					lastMouseDown = real_time_now();
				}
			}
			break;

		case WM_MOUSEMOVE:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
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
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
			{
				// Hack: Take the opportunity to hide the cursor.
				mouseButtonDown = false;
				{
					lock_guard guard(input_state.lock);
					input_state.pointer_down[0] = false;
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
			}
			break;

		case WM_TOUCH:
			{
				touchHandler.handleTouchEvent(hWnd, message, wParam, lParam);
				return 0;
			}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}
	
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)	{
		switch (message) {
		case WM_CREATE:
			break;
			
		case WM_GETMINMAXINFO:
			{
				MINMAXINFO *minmax = reinterpret_cast<MINMAXINFO *>(lParam);
				RECT rc = { 0 };
				bool portrait = g_Config.IsPortrait();
				rc.right = portrait ? 272 : 480;
				rc.bottom = portrait ? 480 : 272;
				AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
				minmax->ptMinTrackSize.x = rc.right - rc.left;
				minmax->ptMinTrackSize.y = rc.bottom - rc.top;
			}
			return 0;

		case WM_ACTIVATE:
			{
				bool pause = true;
				if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
					WindowsRawInput::GainFocus();
					InputDevice::GainFocus();
					g_activeWindow = WINDOW_MAINWINDOW;
					pause = false;
				}
				if (!noFocusPause && g_Config.bPauseOnLostFocus && GetUIState() == UISTATE_INGAME) {
					if (pause != Core_IsStepping()) {	// != is xor for bools
						if (disasmWindow[0])
							SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						else
							Core_EnableStepping(pause);
					}
				}

				if (wParam == WA_ACTIVE) {
					NativeMessageReceived("got_focus", "");
				}
				if (wParam == WA_INACTIVE) {
					NativeMessageReceived("lost_focus", "");
					WindowsRawInput::LoseFocus();
					InputDevice::LoseFocus();
				}
			}
			break;

    case WM_ERASEBKGND:
      // This window is always covered by DisplayWindow. No reason to erase.
			return 1;

		case WM_MOVE:
			SavePosition();
			break;

		case WM_SIZE:
			if (!g_inModeSwitch) {
				ILOG("WM_SIZE (%d)", (int)wParam);
				switch (wParam) {
				case SIZE_MAXIMIZED:
				case SIZE_RESTORED:
					Core_NotifyWindowHidden(false);
					if (!g_Config.bPauseWhenMinimized) {
						NativeMessageReceived("window minimized", "false");
					}
					SavePosition();
					ResizeDisplay();
					g_WindowState = wParam;
					break;
				case SIZE_MINIMIZED:
					Core_NotifyWindowHidden(true);
					if (!g_Config.bPauseWhenMinimized) {
						NativeMessageReceived("window minimized", "true");
					}
					break;
				default:
					break;
				}
			} else {
				ILOG("WM_SIZE (%d) (ignored)", (int)wParam);
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;

    case WM_TIMER:
			// Hack: Take the opportunity to also show/hide the mouse cursor in fullscreen mode.
			switch (wParam) {
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
					key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
					wheelDelta = -wheelDelta;
				} else {
					key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
				}
				// There's no separate keyup event for mousewheel events, let's pass them both together.
				// This also means it really won't work great for key mapping :( Need to build a 1 frame delay or something.
				key.flags = KEY_DOWN | KEY_UP | KEY_HASWHEELDELTA | (wheelDelta << 16);
				NativeKey(key);
			}
			break;

		case WM_COMMAND:
			{
				if (!EmuThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				MainWindowMenu_Process(hWnd, wParam);
			}
			break;

		case WM_USER_TOGGLE_FULLSCREEN:
			ToggleFullscreen(hwndMain, !g_Config.bFullScreen);
			break;

		case WM_INPUT:
			return WindowsRawInput::Process(hWnd, wParam, lParam);

		// TODO: Could do something useful with WM_INPUT_DEVICE_CHANGE?

		// Not sure why we are actually getting WM_CHAR even though we use RawInput, but alright..
		case WM_CHAR:
			return WindowsRawInput::ProcessChar(hWnd, wParam, lParam);

		case WM_VERYSLEEPY_MSG:
			switch (wParam) {
			case VERYSLEEPY_WPARAM_SUPPORTED:
				return TRUE;

			case VERYSLEEPY_WPARAM_GETADDRINFO:
				{
					VerySleepy_AddrInfo *info = (VerySleepy_AddrInfo *)lParam;
					const u8 *ptr = (const u8 *)info->addr;
					std::string name;

					if (MIPSComp::jit && MIPSComp::jit->DescribeCodePtr(ptr, name)) {
						swprintf_s(info->name, L"Jit::%S", name.c_str());
						return TRUE;
					}
					if (gpu && gpu->DescribeCodePtr(ptr, name)) {
						swprintf_s(info->name, L"GPU::%S", name.c_str());
						return TRUE;
					}
				}
				return FALSE;

			default:
				return FALSE;
			}
			break;

		case WM_DROPFILES:
			{
				if (!EmuThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				HDROP hdrop = (HDROP)wParam;
				int count = DragQueryFile(hdrop,0xFFFFFFFF,0,0);
				if (count != 1) {
					MessageBox(hwndMain,L"You can only load one file at a time",L"Error",MB_ICONINFORMATION);
				}
				else
				{
					TCHAR filename[512];
					DragQueryFile(hdrop,0,filename,512);
					TCHAR *type = filename+_tcslen(filename)-3;
					
					NativeMessageReceived("boot", ConvertWStringToUTF8(filename).c_str());
					Core_EnableStepping(false);
				}
			}
			break;

		case WM_CLOSE:
			EmuThread_Stop();
			InputDevice::StopPolling();
			WindowsRawInput::Shutdown();

			return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_DESTROY:
			KillTimer(hWnd, TIMER_CURSORUPDATE);
			KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
			PostQuitMessage(0);
			break;

		case WM_USER + 1:
			if (disasmWindow[0])
				disasmWindow[0]->NotifyMapLoaded();
			if (memoryWindow[0])
				memoryWindow[0]->NotifyMapLoaded();

			if (disasmWindow[0])
				disasmWindow[0]->UpdateDialog();

			SetForegroundWindow(hwndMain);
			break;

		case WM_USER_SAVESTATE_FINISH:
			SetCursor(LoadCursor(0, IDC_ARROW));
			break;

		case WM_USER_UPDATE_UI:
			TranslateMenus(hwndMain, menu);
			break;

		case WM_USER_UPDATE_SCREEN:
			ResizeDisplay(true);
			ShowScreenResolution();
			break;

		case WM_USER_WINDOW_TITLE_CHANGED:
			UpdateWindowTitle();
			break;

		case WM_USER_BROWSE_BOOT_DONE:
			BrowseAndBootDone();
			break;

		case WM_MENUSELECT:
			// Unfortunately, accelerate keys (hotkeys) shares the same enabled/disabled states
			// with corresponding menu items.
			UpdateMenus();
			WindowsRawInput::NotifyMenu();
			break;

		// Turn off the screensaver.
		// Note that if there's a screensaver password, this simple method
		// doesn't work on Vista or higher.
		case WM_SYSCOMMAND:
			{
				switch (wParam) {
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
	
	void Redraw() {
		InvalidateRect(hwndDisplay,0,0);
	}

	HINSTANCE GetHInstance() {
		return hInst;
	}

	void ToggleDebugConsoleVisibility() {
		if (!g_Config.bEnableLogging) {
			LogManager::GetInstance()->GetConsoleListener()->Show(false);
			EnableMenuItem(menu, ID_DEBUG_LOG, MF_GRAYED);
		}
		else {
			LogManager::GetInstance()->GetConsoleListener()->Show(true);
			EnableMenuItem(menu, ID_DEBUG_LOG, MF_ENABLED);
		}
	}

}  // namespace
