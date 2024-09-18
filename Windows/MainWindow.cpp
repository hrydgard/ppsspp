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

// TODO: Get rid of the internal window.
// Tried before but Intel drivers screw up when minimizing, or something ?

#include "stdafx.h"

#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include "Common/OSVersion.h"

#include <Windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <dwmapi.h>

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Instance.h"
#include "Core/KeyMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/Reporting.h"
#include "Windows/InputBox.h"
#include "Windows/InputDevice.h"
#if PPSSPP_API(ANY_GL)
#include "Windows/GPU/WindowsGLContext.h"
#include "Windows/GEDebugger/GEDebugger.h"
#endif
#include "Windows/W32Util/DarkMode.h"
#include "Windows/W32Util/UAHMenuBar.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"

#include "Common/GraphicsContext.h"

#include "Windows/main.h"
#ifndef _M_ARM
#include "Windows/DinputDevice.h"
#endif
#include "Windows/EmuThread.h"
#include "Windows/resource.h"

#include "Windows/MainWindow.h"
#include "Common/Log/LogManager.h"
#include "Common/Log/ConsoleListener.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/RawInput.h"
#include "Windows/CaptureDevice.h"
#include "Windows/TouchInputHandler.h"
#include "Windows/MainWindowMenu.h"
#include "GPU/GPUInterface.h"
#include "UI/OnScreenDisplay.h"
#include "UI/GameSettingsScreen.h"

#define MOUSEEVENTF_FROMTOUCH_NOPEN 0xFF515780 //http://msdn.microsoft.com/en-us/library/windows/desktop/ms703320(v=vs.85).aspx
#define MOUSEEVENTF_MASK_PLUS_PENTOUCH 0xFFFFFF80

// See https://github.com/unknownbrackets/verysleepy/commit/fc1b1b3bd6081fae3566cdb542d896e413238b71
int verysleepy__useSendMessage = 1;

const UINT WM_VERYSLEEPY_MSG = WM_APP + 0x3117;
const UINT WM_USER_GET_BASE_POINTER = WM_APP + 0x3118;  // 0xB118
const UINT WM_USER_GET_EMULATION_STATE = WM_APP + 0x3119;  // 0xB119

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

static std::wstring windowTitle;

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
	static int g_WindowState;
	static bool g_IgnoreWM_SIZE = false;
	static bool inFullscreenResize = false;
	static bool inResizeMove = false;
	static bool hasFocus = true;
	static bool g_isFullscreen = false;
	static bool g_keepScreenBright = false;

	static bool disasmMapLoadPending = false;
	static bool memoryMapLoadPending = false;

	// gross hack
	bool noFocusPause = false;	// TOGGLE_PAUSE state to override pause on lost focus
	bool trapMouse = true; // Handles some special cases(alt+tab, win menu) when game is running and mouse is confined

#define MAX_LOADSTRING 100
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

	void SetKeepScreenBright(bool keepBright) {
		g_keepScreenBright = keepBright;
	}

	void Init(HINSTANCE hInstance) {
		// Register classes - Main Window
		WNDCLASSEX wcex;
		memset(&wcex, 0, sizeof(wcex));
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = 0;  // Show in taskbar
		wcex.lpfnWndProc = (WNDPROC)WndProc;
		wcex.hInstance = hInstance;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = NULL;  // Always covered by display window
		wcex.lpszMenuName	= (LPCWSTR)IDR_MENU1;
		wcex.lpszClassName = szWindowClass;
		wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP);
		wcex.hIconSm = (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16, 16, LR_SHARED);
		RegisterClassEx(&wcex);

		WNDCLASSEX wcdisp;
		memset(&wcdisp, 0, sizeof(wcdisp));
		// Display Window (contained in main window)
		wcdisp.cbSize = sizeof(WNDCLASSEX);
		wcdisp.style = CS_HREDRAW | CS_VREDRAW;
		wcdisp.lpfnWndProc = (WNDPROC)DisplayProc;
		wcdisp.hInstance = hInstance;
		wcdisp.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcdisp.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcdisp.lpszMenuName = 0;
		wcdisp.lpszClassName = szDisplayClass;
		wcdisp.hIcon = 0;
		wcdisp.hIconSm = 0;
		RegisterClassEx(&wcdisp);
	}

	void SavePosition() {
		if (g_Config.UseFullScreen() || inFullscreenResize)
			return;

		WINDOWPLACEMENT placement{};
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

	static void GetWindowSizeAtResolution(int xres, int yres, int *windowWidth, int *windowHeight) {
		RECT rc{};
		rc.right = xres;
		rc.bottom = yres;
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
		*windowWidth = rc.right - rc.left;
		*windowHeight = rc.bottom - rc.top;
	}

	void SetWindowSize(int zoom) {
		AssertCurrentThreadName("Main");
		// Actually, auto mode should be more granular...
		int width, height;
		if (g_Config.IsPortrait()) {
			GetWindowSizeAtResolution(272 * (int)zoom, 480 * (int)zoom, &width, &height);
		} else {
			GetWindowSizeAtResolution(480 * (int)zoom, 272 * (int)zoom, &width, &height);
		}
		g_Config.iWindowWidth = width;
		g_Config.iWindowHeight = height;
		MoveWindow(hwndMain, g_Config.iWindowX, g_Config.iWindowY, width, height, TRUE);
	}

	void SetInternalResolution(int res) {
		if (res >= 0 && res <= RESOLUTION_MAX)
			g_Config.iInternalResolution = res;
		else {
			if (++g_Config.iInternalResolution > RESOLUTION_MAX)
				g_Config.iInternalResolution = 0;
		}

		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	}

	void CorrectCursor() {
		bool autoHide = ((g_Config.UseFullScreen() && !mouseButtonDown) || (g_Config.bMouseControl && trapMouse)) && GetUIState() == UISTATE_INGAME;
		if (autoHide && (hideCursor || g_Config.bMouseControl)) {
			while (cursorCounter >= 0) {
				cursorCounter = ShowCursor(FALSE);
			}
			if (g_Config.bMouseConfine) {
				RECT rc;
				GetClientRect(hwndDisplay, &rc);
				ClientToScreen(hwndDisplay, reinterpret_cast<POINT*>(&rc.left));
				ClientToScreen(hwndDisplay, reinterpret_cast<POINT*>(&rc.right));
				ClipCursor(&rc);
			}
		} else {
			hideCursor = !autoHide;
			if (cursorCounter < 0) {
				cursorCounter = ShowCursor(TRUE);
				SetCursor(LoadCursor(NULL, IDC_ARROW));
				ClipCursor(NULL);
			}
		}
	}

	static void HandleSizeChange(int newSizingType) {
		SavePosition();
		Core_NotifyWindowHidden(false);
		if (!g_Config.bPauseWhenMinimized) {
			System_PostUIMessage(UIMessage::WINDOW_MINIMIZED, "false");
		}

		int width, height;
		W32Util::GetWindowRes(hwndMain, &width, &height);

		// Moves the internal display window to match the inner size of the main window.
		MoveWindow(hwndDisplay, 0, 0, width, height, TRUE);

		// Setting pixelWidth to be too small could have odd consequences.
		if (width >= 4 && height >= 4) {
			// The framebuffer manager reads these once per frame, hopefully safe enough.. should really use a mutex or some
			// much better mechanism.
			PSP_CoreParameter().pixelWidth = width;
			PSP_CoreParameter().pixelHeight = height;
		}

		DEBUG_LOG(Log::System, "Pixel width/height: %dx%d", PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

		if (UpdateScreenScale(width, height)) {
			System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		}

		// Don't save the window state if fullscreen.
		if (!g_Config.UseFullScreen()) {
			g_WindowState = newSizingType;
		}
	}

	void ToggleFullscreen(HWND hWnd, bool goingFullscreen) {
		GraphicsContext *graphicsContext = PSP_CoreParameter().graphicsContext;
		// Make sure no rendering is happening during the switch.
		if (graphicsContext) {
			graphicsContext->Pause();
		}

		WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
		GetWindowPlacement(hwndMain, &placement);

		int oldWindowState = g_WindowState;
		inFullscreenResize = true;
		g_IgnoreWM_SIZE = true;

		DWORD dwStyle;

		if (!goingFullscreen) {
			dwStyle = ::GetWindowLong(hWnd, GWL_STYLE);

			// Remove popup
			dwStyle &= ~WS_POPUP;
			// Re-add caption and border styles.
			dwStyle |= WS_OVERLAPPEDWINDOW;
		} else {
			// If the window was maximized before going fullscreen, make sure to restore first
			// in order not to have the taskbar show up on top of PPSSPP.
			if (oldWindowState == SIZE_MAXIMIZED || placement.showCmd == SW_SHOWMAXIMIZED) {
				ShowWindow(hwndMain, SW_RESTORE);
			}

			// Remove caption and border styles.
			dwStyle = ::GetWindowLong(hWnd, GWL_STYLE);
			dwStyle &= ~WS_OVERLAPPEDWINDOW;
			// Add Popup
			dwStyle |= WS_POPUP;
		}

		::SetWindowLong(hWnd, GWL_STYLE, dwStyle);

		// Remove the menu bar. This can trigger WM_SIZE because the contents change size.
		::SetMenu(hWnd, goingFullscreen || !g_Config.bShowMenuBar ? NULL : menu);

		if (g_Config.UseFullScreen() != goingFullscreen) {
			g_Config.bFullScreen = goingFullscreen;
			g_Config.iForceFullScreen = -1;
		}
		g_isFullscreen = goingFullscreen;

		g_IgnoreWM_SIZE = false;

		// Resize to the appropriate view.
		// If we're returning to window mode, re-apply the appropriate size setting.
		if (goingFullscreen) {
			if (g_Config.bFullScreenMulti) {
				// Maximize isn't enough to display on all monitors.
				// Remember that negative coordinates may be valid.
				int totalX = GetSystemMetrics(SM_XVIRTUALSCREEN);
				int totalY = GetSystemMetrics(SM_YVIRTUALSCREEN);
				int totalWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
				int totalHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
				MoveWindow(hwndMain, totalX, totalY, totalWidth, totalHeight, TRUE);
				HandleSizeChange(oldWindowState);
				ShowWindow(hwndMain, SW_SHOW);
			} else {
				ShowWindow(hwndMain, SW_MAXIMIZE);
			}
		} else {
			ShowWindow(hwndMain, oldWindowState == SIZE_MAXIMIZED ? SW_MAXIMIZE : SW_RESTORE);
			if (g_Config.bFullScreenMulti && oldWindowState != SIZE_MAXIMIZED) {
				// Return the screen to where it was.
				MoveWindow(hwndMain, g_Config.iWindowX, g_Config.iWindowY, g_Config.iWindowWidth, g_Config.iWindowHeight, TRUE);
			}
			if (oldWindowState == SIZE_MAXIMIZED) {
				// WM_SIZE wasn't sent, since the size didn't change (it was full screen before and after.)
				HandleSizeChange(oldWindowState);
			}
		}

		inFullscreenResize = false;
		CorrectCursor();

		ShowOwnedPopups(hwndMain, goingFullscreen ? FALSE : TRUE);
		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		WindowsRawInput::NotifyMenu();

		if (graphicsContext) {
			graphicsContext->Resume();
		}
	}

	void Minimize() {
		ShowWindow(hwndMain, SW_MINIMIZE);
		InputDevice::LoseFocus();
	}

	RECT DetermineWindowRectangle() {
		const int virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		const int virtualScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int virtualScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int currentScreenWidth = GetSystemMetrics(SM_CXSCREEN);
		const int currentScreenHeight = GetSystemMetrics(SM_CYSCREEN);

		bool resetPositionX = true;
		bool resetPositionY = true;

		if (g_Config.iWindowWidth > 0 && g_Config.iWindowHeight > 0 && !g_Config.UseFullScreen()) {
			bool visibleHorizontally = ((g_Config.iWindowX + g_Config.iWindowWidth) >= virtualScreenX) &&
				((g_Config.iWindowX + g_Config.iWindowWidth) < (virtualScreenWidth + g_Config.iWindowWidth));

			bool visibleVertically = ((g_Config.iWindowY + g_Config.iWindowHeight) >= virtualScreenY) &&
				((g_Config.iWindowY + g_Config.iWindowHeight) < (virtualScreenHeight + g_Config.iWindowHeight));

			if (visibleHorizontally)
				resetPositionX = false;
			if (visibleVertically)
				resetPositionY = false;
		}

		// Try to workaround #9563.
		if (!resetPositionY && g_Config.iWindowY < 0) {
			g_Config.iWindowY = 0;
		}

		int windowWidth = g_Config.iWindowWidth;
		int windowHeight = g_Config.iWindowHeight;

		// First, get the w/h right.
		if (windowWidth <= 0 || windowHeight <= 0) {
			bool portrait = g_Config.IsPortrait();

			// We want to adjust for DPI but still get an integer pixel scaling ratio.
			double dpi_scale = 96.0 / System_GetPropertyFloat(SYSPROP_DISPLAY_DPI);
			int scale = (int)ceil(2.0 / dpi_scale);

			GetWindowSizeAtResolution(scale * (portrait ? 272 : 480), scale * (portrait ? 480 : 272), &windowWidth, &windowHeight);
		}

		// Then center if necessary. One dimension at a time.
		// Max is to make sure that if we end up making the window bigger than the screen (which is not ideal), the top left
		// corner, and thus the menu etc, will be visible. Also potential workaround for #9563.
		int x = g_Config.iWindowX;
		int y = g_Config.iWindowY;
		if (resetPositionX) {
			x = std::max(0, (currentScreenWidth - windowWidth) / 2);
		}
		if (resetPositionY) {
			y = std::max(0, (currentScreenHeight - windowHeight) / 2);
		}

		RECT rc;
		rc.left = x;
		rc.right = rc.left + windowWidth;
		rc.top = y;
		rc.bottom = rc.top + windowHeight;
		return rc;
	}

	void UpdateWindowTitle() {
		std::wstring title = windowTitle;
		if (PPSSPP_ID >= 1 && GetInstancePeerCount() > 1) {
			title.append(ConvertUTF8ToWString(StringFromFormat(" (instance: %d)", (int)PPSSPP_ID)));
		}
		SetWindowText(hwndMain, title.c_str());
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

		SetWindowLong(hwndMain, GWL_EXSTYLE, WS_EX_APPWINDOW);


		const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DONOTROUND;
		DwmSetWindowAttribute(hwndMain, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

		RECT rcClient;
		GetClientRect(hwndMain, &rcClient);

		hwndDisplay = CreateWindowEx(0, szDisplayClass, L"", WS_CHILD | WS_VISIBLE,
			0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hwndMain, 0, hInstance, 0);
		if (!hwndDisplay)
			return FALSE;

		menu = GetMenu(hwndMain);

		MENUINFO info;
		ZeroMemory(&info,sizeof(MENUINFO));
		info.cbSize = sizeof(MENUINFO);
		info.cyMax = 0;
		info.dwStyle = MNS_CHECKORBMP;
		info.fMask = MIM_STYLE;
		for (int i = 0; i < GetMenuItemCount(menu); i++) {
			SetMenuInfo(GetSubMenu(menu,i), &info);
		}

		// Always translate first: translating resets the menu.
		TranslateMenus(hwndMain, menu);
		UpdateMenus();

		// Accept dragged files.
		DragAcceptFiles(hwndMain, TRUE);

		hideCursor = true;
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);

		ToggleFullscreen(hwndMain, g_Config.UseFullScreen());

		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		touchHandler.registerTouchWindow(hwndDisplay);

		WindowsRawInput::Init();

		SetFocus(hwndMain);

		return TRUE;
	}

	void CreateDisasmWindow() {
		if (!disasmWindow) {
			disasmWindow = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(disasmWindow);
		}
		if (disasmMapLoadPending)
			disasmWindow->NotifyMapLoaded();
		disasmMapLoadPending = false;
	}

	void CreateGeDebuggerWindow() {
#if PPSSPP_API(ANY_GL)
		if (!geDebuggerWindow) {
			geDebuggerWindow = new CGEDebugger(MainWindow::GetHInstance(), MainWindow::GetHWND());
			DialogManager::AddDlg(geDebuggerWindow);
		}
#endif
	}

	void CreateMemoryWindow() {
		if (!memoryWindow) {
			memoryWindow = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(memoryWindow);
		}
		if (memoryMapLoadPending)
			memoryWindow->NotifyMapLoaded();
		memoryMapLoadPending = false;
	}

	void CreateVFPUWindow() {
		if (!vfpudlg) {
			vfpudlg = new CVFPUDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(vfpudlg);
		}
	}

	void NotifyDebuggerMapLoaded() {
		disasmMapLoadPending = disasmWindow == nullptr;
		memoryMapLoadPending = memoryWindow == nullptr;
		if (!disasmMapLoadPending)
			disasmWindow->NotifyMapLoaded();
		if (!memoryMapLoadPending)
			memoryWindow->NotifyMapLoaded();
	}

	void DestroyDebugWindows() {
		DialogManager::RemoveDlg(disasmWindow);
		delete disasmWindow;
		disasmWindow = nullptr;

#if PPSSPP_API(ANY_GL)
		DialogManager::RemoveDlg(geDebuggerWindow);
		delete geDebuggerWindow;
		geDebuggerWindow = nullptr;
#endif

		DialogManager::RemoveDlg(memoryWindow);
		delete memoryWindow;
		memoryWindow = nullptr;

		DialogManager::RemoveDlg(vfpudlg);
		delete vfpudlg;
		vfpudlg = nullptr;
	}

	LRESULT CALLBACK DisplayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
		static bool firstErase = true;

		switch (message) {
		case WM_SIZE:
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

		// Mouse input. We send asynchronous touch events for minimal latency.
		case WM_LBUTTONDOWN:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = true;

				float x = GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
				float y = GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_DOWN;
				touch.x = x;
				touch.y = y;
				NativeTouch(touch);
				SetCapture(hWnd);

				// Simulate doubleclick, doesn't work with RawInput enabled
				static double lastMouseDown;
				static float lastMouseDownX = -1.0f;
				static float lastMouseDownY = -1.0f;
				double now = time_now_d();
				if ((now - lastMouseDown) < 0.001 * GetDoubleClickTime()) {
					float dx = lastMouseDownX - x;
					float dy = lastMouseDownY - y;
					float distSq = dx * dx + dy * dy;
					if (distSq < 3.0f*3.0f && !g_Config.bShowTouchControls && !g_Config.bMouseControl && GetUIState() == UISTATE_INGAME && g_Config.bFullscreenOnDoubleclick) {
						SendToggleFullscreen(!g_Config.UseFullScreen());
					}
					lastMouseDown = 0.0;
				} else {
					lastMouseDown = now;
				}
				lastMouseDownX = x;
				lastMouseDownY = y;
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

				float x = (float)cursorX * g_display.dpi_scale_x;
				float y = (float)cursorY * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				if (wParam & MK_LBUTTON) {
					TouchInput touch;
					touch.id = 0;
					touch.flags = TOUCH_MOVE;
					touch.x = x;
					touch.y = y;
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

				float x = (float)GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
				float y = (float)GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_UP;
				touch.x = x;
				touch.y = y;
				NativeTouch(touch);
				ReleaseCapture();
			}
			break;

		case WM_TOUCH:
			touchHandler.handleTouchEvent(hWnd, message, wParam, lParam);
			return 0;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	RECT MapRectFromClientToWndCoords(HWND hwnd, const RECT & r)
	{
		RECT wnd_coords = r;

		// map to screen
		MapWindowPoints(hwnd, NULL, reinterpret_cast<POINT *>(&wnd_coords), 2);

		RECT scr_coords;
		GetWindowRect(hwnd, &scr_coords);

		// map to window coords by substracting the window coord origin in
		// screen coords.
		OffsetRect(&wnd_coords, -scr_coords.left, -scr_coords.top);

		return wnd_coords;
	}

	RECT GetNonclientMenuBorderRect(HWND hwnd)
	{
		RECT r;
		GetClientRect(hwnd, &r);
		r = MapRectFromClientToWndCoords(hwnd, r);
		int y = r.top - 1;
		return {
			r.left,
			y,
			r.right,
			y + 1
		};
	}

	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)	{
		LRESULT darkResult = 0;
		if (UAHDarkModeWndProc(hWnd, message, wParam, lParam, &darkResult)) {
			return darkResult;
		}

		switch (message) {
		case WM_CREATE:
			if (!IsVistaOrHigher()) {
				// Remove the D3D11 choice on versions below XP
				RemoveMenu(GetMenu(hWnd), ID_OPTIONS_DIRECT3D11, MF_BYCOMMAND);
			}
			if (g_darkModeSupported) {
				SendMessageW(hWnd, WM_THEMECHANGED, 0, 0);
			}
			break;

		case WM_USER_RUN_CALLBACK:
		{
			auto callback = reinterpret_cast<void (*)(void *window, void *userdata)>(wParam);
			void *userdata = reinterpret_cast<void *>(lParam);
			callback(hWnd, userdata);
			break;
		}
		case WM_USER_GET_BASE_POINTER:
			Reporting::NotifyDebugger();
			switch (lParam) {
			case 0: return (u32)(u64)Memory::base;
			case 1: return (u32)((u64)Memory::base >> 32);
			case 2: return (u32)(u64)(&Memory::base);
			case 3: return (u32)((u64)(&Memory::base) >> 32);
			default:
				return 0;
			}
			break;

		case WM_USER_GET_EMULATION_STATE:
			return (u32)(Core_IsActive() && GetUIState() == UISTATE_INGAME);

		// Hack to kill the white line underneath the menubar.
		// From https://stackoverflow.com/questions/57177310/how-to-paint-over-white-line-between-menu-bar-and-client-area-of-window
		case WM_NCPAINT:
		case WM_NCACTIVATE:
		{
			if (!IsDarkModeEnabled() || IsIconic(hWnd)) {
				return DefWindowProc(hWnd, message, wParam, lParam);
			}

			auto result = DefWindowProc(hWnd, message, wParam, lParam);
			// Paint over the line with pure black. Could also try to figure out the dark theme color.
			HDC hdc = GetWindowDC(hWnd);
			RECT r = GetNonclientMenuBorderRect(hWnd);
			HBRUSH red = CreateSolidBrush(RGB(0, 0, 0));
			FillRect(hdc, &r, red);
			DeleteObject(red);
			ReleaseDC(hWnd, hdc);
			return result;
		}

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
				UpdateWindowTitle();
				bool pause = true;
				if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
					WindowsRawInput::GainFocus();
					if (!IsIconic(GetHWND())) {
						InputDevice::GainFocus();
					}
					g_activeWindow = WINDOW_MAINWINDOW;
					pause = false;
				} else {
					g_activeWindow = WINDOW_OTHER;
				}
				if (!noFocusPause && g_Config.bPauseOnLostFocus && GetUIState() == UISTATE_INGAME) {
					if (pause != Core_IsStepping()) {
						if (disasmWindow)
							SendMessage(disasmWindow->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						else
							Core_EnableStepping(pause, "ui.lost_focus", 0);
					}
				}

				if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
					System_PostUIMessage(UIMessage::GOT_FOCUS);
					hasFocus = true;
					trapMouse = true;
				}
				if (wParam == WA_INACTIVE) {
					System_PostUIMessage(UIMessage::LOST_FOCUS);
					WindowsRawInput::LoseFocus();
					InputDevice::LoseFocus();
					hasFocus = false;
					trapMouse = false;
				}
			}
			break;

		case WM_SETFOCUS:
			UpdateWindowTitle();
			break;

		case WM_ERASEBKGND:
			// This window is always covered by DisplayWindow. No reason to erase.
			return 0;

		case WM_MOVE:
			SavePosition();
			break;

		case WM_ENTERSIZEMOVE:
			inResizeMove = true;
			break;

		case WM_EXITSIZEMOVE:
			inResizeMove = false;
			HandleSizeChange(SIZE_RESTORED);
			break;

		case WM_SIZE:
			switch (wParam) {
			case SIZE_RESTORED:
			case SIZE_MAXIMIZED:
				if (g_IgnoreWM_SIZE) {
					return DefWindowProc(hWnd, message, wParam, lParam);
				} else if (!inResizeMove) {
					HandleSizeChange(wParam);
				}
				if (hasFocus) {
					InputDevice::GainFocus();
				}
				break;

			case SIZE_MINIMIZED:
				Core_NotifyWindowHidden(true);
				if (!g_Config.bPauseWhenMinimized) {
					System_PostUIMessage(UIMessage::WINDOW_MINIMIZED, "true");
				}
				InputDevice::LoseFocus();
				break;
			default:
				break;
			}
			break;

		// Wheel events have to stay in WndProc for compatibility with older Windows(7). See #12156
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
				// There's no release event, but we simulate it in NativeKey/NativeFrame.
				key.flags = KEY_DOWN | KEY_HASWHEELDELTA | (wheelDelta << 16);
				NativeKey(key);
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

		case WM_COMMAND:
			{
				if (!MainThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				MainWindowMenu_Process(hWnd, wParam);
			}
			break;

		case WM_USER_TOGGLE_FULLSCREEN:
			ToggleFullscreen(hwndMain, wParam ? true : false);
			break;

		case WM_INPUT:
			return WindowsRawInput::Process(hWnd, wParam, lParam);

		// TODO: Could do something useful with WM_INPUT_DEVICE_CHANGE?

		// Not sure why we are actually getting WM_CHAR even though we use RawInput, but alright..
		case WM_CHAR:
			return WindowsRawInput::ProcessChar(hWnd, wParam, lParam);

		case WM_DEVICECHANGE:
#ifndef _M_ARM
			DinputDevice::CheckDevices();
#endif
			if (winCamera)
				winCamera->CheckDevices();
			if (winMic)
				winMic->CheckDevices();
			return DefWindowProc(hWnd, message, wParam, lParam);

		case WM_VERYSLEEPY_MSG:
			switch (wParam) {
			case VERYSLEEPY_WPARAM_SUPPORTED:
				return TRUE;

			case VERYSLEEPY_WPARAM_GETADDRINFO:
				{
					VerySleepy_AddrInfo *info = (VerySleepy_AddrInfo *)lParam;
					const u8 *ptr = (const u8 *)info->addr;
					std::string name;

					std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
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
				if (!MainThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				HDROP hdrop = (HDROP)wParam;
				int count = DragQueryFile(hdrop, 0xFFFFFFFF, 0, 0);
				if (count != 1) {
					// TODO: Translate? Or just not bother?
					MessageBox(hwndMain, L"You can only load one file at a time", L"Error", MB_ICONINFORMATION);
				} else {
					TCHAR filename[1024];
					if (DragQueryFile(hdrop, 0, filename, ARRAY_SIZE(filename)) != 0) {
						const std::string utf8_filename = ReplaceAll(ConvertWStringToUTF8(filename), "\\", "/");
						System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, utf8_filename);
						Core_EnableStepping(false);
					}
				}
				DragFinish(hdrop);
			}
			break;

		case WM_CLOSE:
			InputDevice::StopPolling();
			MainThread_Stop();
			WindowsRawInput::Shutdown();
			return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_DESTROY:
			KillTimer(hWnd, TIMER_CURSORUPDATE);
			KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
			// Main window is gone, this tells the message loop to exit.
			PostQuitMessage(0);
			return 0;

		case WM_USER + 1:
			NotifyDebuggerMapLoaded();
			if (disasmWindow)
				disasmWindow->UpdateDialog();
			break;

		case WM_USER_SAVESTATE_FINISH:
			SetCursor(LoadCursor(0, IDC_ARROW));
			break;

		case WM_USER_UPDATE_UI:
			TranslateMenus(hwndMain, menu);
			// Update checked status immediately for accelerators.
			UpdateMenus();
			break;

		case WM_USER_WINDOW_TITLE_CHANGED:
			UpdateWindowTitle();
			break;

		case WM_USER_RESTART_EMUTHREAD:
			NativeSetRestarting();
			InputDevice::StopPolling();
			MainThread_Stop();
			coreState = CORE_POWERUP;
			UpdateUIState(UISTATE_MENU);
			MainThread_Start(g_Config.iGPUBackend == (int)GPUBackend::OPENGL);
			InputDevice::BeginPolling();
			break;

		case WM_USER_SWITCHUMD_UPDATED:
			UpdateSwitchUMD();
			break;

		case WM_MENUSELECT:
			// Called when a menu is opened. Also when an item is selected, but meh.
			UpdateMenus(true);
			WindowsRawInput::NotifyMenu();
			trapMouse = false;
			break;

		case WM_EXITMENULOOP:
			// Called when menu is closed.
			trapMouse = true;
			break;

		// Turn off the screensaver if in-game.
		// Note that if there's a screensaver password, this simple method
		// doesn't work on Vista or higher.
		case WM_SYSCOMMAND:
			// Disable Alt key for menu if it's been mapped.
			if (wParam == SC_KEYMENU && (lParam >> 16) <= 0) {
				if (KeyMap::IsKeyMapped(DEVICE_ID_KEYBOARD, NKCODE_ALT_LEFT) || KeyMap::IsKeyMapped(DEVICE_ID_KEYBOARD, NKCODE_ALT_RIGHT)) {
					return 0;
				}
			}
			if (g_keepScreenBright) {
				switch (wParam) {
				case SC_SCREENSAVE:
					return 0;
				case SC_MONITORPOWER:
					if (lParam == 1 || lParam == 2) {
						return 0;
					} else {
						break;
					}
				default:
					// fall down to DefWindowProc
					break;
				}
			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		case WM_SETTINGCHANGE:
			{
				if (g_darkModeSupported && IsColorSchemeChangeMessage(lParam))
					SendMessageW(hWnd, WM_THEMECHANGED, 0, 0);
			}
			return DefWindowProc(hWnd, message, wParam, lParam);

		case WM_THEMECHANGED:
		{
			if (g_darkModeSupported)
			{
				_AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
				RefreshTitleBarThemeColor(hWnd);
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

	void SendToggleFullscreen(bool fullscreen) {
		PostMessage(hwndMain, WM_USER_TOGGLE_FULLSCREEN, fullscreen, 0);
	}

	bool IsFullscreen() {
		return g_isFullscreen;
	}

	void RunCallbackInWndProc(void (*callback)(void *, void *), void *userdata) {
		PostMessage(hwndMain, WM_USER_RUN_CALLBACK, reinterpret_cast<WPARAM>(callback), reinterpret_cast<LPARAM>(userdata));
	}

}  // namespace
