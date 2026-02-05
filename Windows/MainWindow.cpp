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
#include <shellapi.h>  // For drag/drop functionality
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
#include "ext/imgui/imgui_impl_platform.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Instance.h"
#include "Core/KeyMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/Reporting.h"
#include "Windows/InputDevice.h"
#if PPSSPP_API(ANY_GL)
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
#include "Windows/W32Util/Misc.h"
#include "Windows/RawInput.h"
#include "Windows/CaptureDevice.h"
#include "Windows/TouchInputHandler.h"
#include "Windows/MainWindowMenu.h"
#include "GPU/GPUCommon.h"
#include "UI/PauseScreen.h"

#define MOUSEEVENTF_FROMTOUCH_NOPEN 0xFF515780 //http://msdn.microsoft.com/en-us/library/windows/desktop/ms703320(v=vs.85).aspx
#define MOUSEEVENTF_MASK_PLUS_PENTOUCH 0xFFFFFF80

// See https://github.com/unknownbrackets/verysleepy/commit/fc1b1b3bd6081fae3566cdb542d896e413238b71
int verysleepy__useSendMessage = 1;

const UINT WM_VERYSLEEPY_MSG = WM_APP + 0x3117;
const UINT WM_USER_GET_BASE_POINTER = WM_APP + 0x3118;  // 0xB118
const UINT WM_USER_GET_EMULATION_STATE = WM_APP + 0x3119;  // 0xB119
const UINT WM_USER_GET_CURRENT_GAMEID = WM_APP + 0x311A;  // 0xB11A
const UINT WM_USER_GET_MODULE_INFO = WM_APP + 0x311B;  // 0xB11B

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

static std::mutex g_windowTitleLock;
static std::wstring g_windowTitle;

#define TIMER_CURSORUPDATE 1
#define TIMER_CURSORMOVEUPDATE 2
#define CURSORUPDATE_INTERVAL_MS 1000
#define CURSORUPDATE_MOVE_TIMESPAN_MS 500

inline WindowSizeState ShowCmdToWindowSizeState(const int showCmd) {
	switch (showCmd) {
	case SW_SHOWMAXIMIZED:
		return WindowSizeState::Maximized;
	case SW_SHOWMINIMIZED:
		return WindowSizeState::Minimized;
	case SW_SHOWNORMAL:
	default:
		return WindowSizeState::Normal;
	}
}

inline int WindowSizeStateToShowCmd(const WindowSizeState windowSizeState) {
	switch (windowSizeState) {
	case WindowSizeState::Maximized:
		return SW_SHOWMAXIMIZED;
	case WindowSizeState::Minimized:
		return SW_SHOWMINIMIZED;
	default:
		return SW_SHOWNORMAL;
	}
}

static const char *WindowSizeStateToString(const WindowSizeState state) {
	switch (state) {
	case WindowSizeState::Normal:
		return "Normal";
	case WindowSizeState::Minimized:
		return "Minimized";
	case WindowSizeState::Maximized:
		return "Maximized";
	default:
		return "Unknown";
	}
}

namespace MainWindow {
	static HWND hwndMain;
	static TouchInputHandler touchHandler;

	static HMENU g_hMenu;

	HINSTANCE hInst;
	static int cursorCounter = 0;
	static int prevCursorX = -1;
	static int prevCursorY = -1;

	static bool mouseButtonDown = false;
	static bool hideCursor = false;
	static bool inResizeMove = false;
	static bool hasFocus = true;
	static bool g_keepScreenBright = false;

	static bool disasmMapLoadPending = false;
	static bool memoryMapLoadPending = false;
	static bool g_wasMinimized = false;
	static bool g_inForcedResize = false;


	// gross hack
	bool noFocusPause = false;	// TOGGLE_PAUSE state to override pause on lost focus

	static bool trapMouse = true; // Handles some special cases(alt+tab, win menu) when game is running and mouse is confined

	static constexpr const wchar_t *szWindowClass = L"PPSSPPWnd";

	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWND() { return hwndMain; }
	HINSTANCE GetHInstance() { return hInst; }

	void SetKeepScreenBright(bool keepBright) {
		g_keepScreenBright = keepBright;
	}

	void Init(HINSTANCE hInstance) {
		// Register classes - Main Window
		WNDCLASSEX wcex{};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = 0;  // Show in taskbar
		wcex.lpfnWndProc = (WNDPROC)WndProc;
		wcex.hInstance = hInstance;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);  // or NULL?
		wcex.lpszMenuName	= (LPCWSTR)IDR_MENU1;
		wcex.lpszClassName = szWindowClass;
		wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP);
		wcex.hIconSm = (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16, 16, LR_SHARED);
		RegisterClassEx(&wcex);
	}

	void SavePosition() {
		if (g_Config.bFullScreen) {
			// Don't save the position of the full screen window. We keep the old saved position around.
			return;
		}
		WINDOWPLACEMENT placement{};
		GetWindowPlacement(hwndMain, &placement);
		WindowSizeState sizeState = ShowCmdToWindowSizeState(placement.showCmd);
		if (sizeState == WindowSizeState::Minimized) {
			// Don't save minimized position.
			sizeState = WindowSizeState::Normal;
		}
		g_Config.iWindowSizeState = (int)sizeState;
		g_Config.iWindowX = placement.rcNormalPosition.left;
		g_Config.iWindowY = placement.rcNormalPosition.top;
		g_Config.iWindowWidth = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
		g_Config.iWindowHeight = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
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

		if (g_Config.bFullScreen) {
			return;
		}

		// Actually, auto mode should be more granular...
		int width, height;
		const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
		if (config.InternalRotationIsPortrait()) {
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
		const bool autoHide = ((g_Config.bFullScreen && !mouseButtonDown) || (g_Config.bMouseControl && trapMouse)) && GetUIState() == UISTATE_INGAME;
		if (autoHide && (hideCursor || g_Config.bMouseControl)) {
			while (cursorCounter >= 0) {
				cursorCounter = ShowCursor(FALSE);
			}
			if (g_Config.bMouseConfine) {
				RECT rc;
				GetClientRect(hwndMain, &rc);
				ClientToScreen(hwndMain, reinterpret_cast<POINT*>(&rc.left));
				ClientToScreen(hwndMain, reinterpret_cast<POINT*>(&rc.right));
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

	static void HandleSizeChange() {
		Native_NotifyWindowHidden(false);
		if (!g_Config.bPauseWhenMinimized) {
			System_PostUIMessage(UIMessage::WINDOW_MINIMIZED, "false");
		}

		int width, height;
		W32Util::GetWindowRes(hwndMain, &width, &height);

		// Setting pixelWidth to be too small could have odd consequences.
		if (width >= 4 && height >= 4) {
			// The framebuffer manager reads these once per frame, hopefully safe enough.. should really use a mutex or some
			// much better mechanism.
			PSP_CoreParameter().pixelWidth = width;
			PSP_CoreParameter().pixelHeight = height;
		}

		DEBUG_LOG(Log::System, "Pixel width/height: %dx%d", PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

		if (Native_UpdateScreenScale(width, height, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor))) {
			System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
			System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		}
	}

	void ApplyFullscreenState(HWND hWnd, bool goFullscreen) {
		GraphicsContext *graphicsContext = PSP_CoreParameter().graphicsContext;
		// Make sure no rendering is happening during the switch.
		if (graphicsContext) {
			graphicsContext->Pause();
		}

		const DWORD prevStyle = GetWindowLong(hWnd, GWL_STYLE);
		const bool isCurrentlyFullscreen = !(prevStyle & WS_OVERLAPPEDWINDOW);

		if (goFullscreen && !isCurrentlyFullscreen) {
			INFO_LOG(Log::System, "ApplyFullscreenState: Entering fullscreen from %s mode at %dx%d+%d+%d",
				WindowSizeStateToString((WindowSizeState)g_Config.iWindowSizeState),
				g_Config.iWindowWidth,  g_Config.iWindowHeight,
				g_Config.iWindowX, g_Config.iWindowY);

			// Transitioning to Fullscreen
			WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
			if (GetWindowPlacement(hWnd, &wp)) {
				g_Config.iWindowX = wp.rcNormalPosition.left;
				g_Config.iWindowY = wp.rcNormalPosition.top;
				g_Config.iWindowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
				g_Config.iWindowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
				wp.showCmd = SW_SHOW;
			}

			if (g_Config.bFullScreenMulti) {
				SetMenu(hWnd, NULL);
				// Strip all decorations
				SetWindowLong(hWnd, GWL_STYLE, prevStyle & ~WS_OVERLAPPEDWINDOW);
				// Maximize isn't enough to display on all monitors.
				// Remember that negative coordinates may be valid.
				const int totalX = GetSystemMetrics(SM_XVIRTUALSCREEN);
				const int totalY = GetSystemMetrics(SM_YVIRTUALSCREEN);
				const int totalWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
				const int totalHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
				SetWindowPos(hWnd, HWND_TOP,
					totalX, totalY,
					totalWidth, totalHeight,
					SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			} else {
				MONITORINFO mi = {sizeof(mi)};
				if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) {
					SetMenu(hWnd, NULL);
					// Strip all decorations
					SetWindowLong(hWnd, GWL_STYLE, prevStyle & ~WS_OVERLAPPEDWINDOW);

					SetWindowPos(hWnd, HWND_TOP,
						mi.rcMonitor.left, mi.rcMonitor.top,
						mi.rcMonitor.right - mi.rcMonitor.left,
						mi.rcMonitor.bottom - mi.rcMonitor.top,
						SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
				}
			}
		} else if (!goFullscreen && isCurrentlyFullscreen) {
			INFO_LOG(Log::System, "ApplyFullscreenState: Exiting fullscreen to %s mode at %dx%d+%d+%d",
				WindowSizeStateToString((WindowSizeState)g_Config.iWindowSizeState),
				g_Config.iWindowWidth,  g_Config.iWindowHeight,
				g_Config.iWindowX, g_Config.iWindowY);

			// Transitioning to Windowed
			SetWindowLong(hWnd, GWL_STYLE, prevStyle | WS_OVERLAPPEDWINDOW);
			SetMenu(hWnd, g_hMenu);

			WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
			wp.showCmd = WindowSizeStateToShowCmd((WindowSizeState)g_Config.iWindowSizeState);
			wp.rcNormalPosition = {g_Config.iWindowX, g_Config.iWindowY, g_Config.iWindowX + g_Config.iWindowWidth, g_Config.iWindowY + g_Config.iWindowHeight};

			if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = SW_SHOWNORMAL;

			SetWindowPlacement(hWnd, &wp);
			SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}

		CorrectCursor();

		ShowOwnedPopups(hwndMain, goFullscreen ? FALSE : TRUE);
		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		WindowsRawInput::NotifyMenu();

		if (graphicsContext) {
			graphicsContext->Resume();
		}
	}

	void Minimize() {
		ShowWindow(hwndMain, SW_MINIMIZE);
		g_InputManager.LoseFocus();
	}

	// TODO: Currently unused.
	RECT DetermineDefaultWindowRectangle() {
		const int virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		const int virtualScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int virtualScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int currentScreenWidth = GetSystemMetrics(SM_CXSCREEN);
		const int currentScreenHeight = GetSystemMetrics(SM_CYSCREEN);

		bool resetPositionX = true;
		bool resetPositionY = true;

		if (g_Config.iWindowWidth > 0 && g_Config.iWindowHeight > 0 && !g_Config.bFullScreen) {
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
			DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
			const bool portrait = config.InternalRotationIsPortrait();

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
		std::wstring title;
		{
			std::lock_guard<std::mutex> lock(g_windowTitleLock);
			title = g_windowTitle;
		}
		if (PPSSPP_ID >= 1 && GetInstancePeerCount() > 1) {
			title.append(ConvertUTF8ToWString(StringFromFormat(" (instance: %d)", (int)PPSSPP_ID)));
		}
		SetWindowText(hwndMain, title.c_str());
	}

	void SetWindowTitle(const wchar_t *title) {
		{
			std::lock_guard<std::mutex> lock(g_windowTitleLock);
			g_windowTitle = title;
		}
		PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
	}

	BOOL Show(HINSTANCE hInstance) {
		hInst = hInstance; // Store instance handle in our global variable.

		hwndMain = CreateWindowEx(0, szWindowClass, L"", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

		if (!hwndMain)
			return FALSE;

		g_inForcedResize = true;

		g_hMenu = GetMenu(hwndMain);

		WINDOWPLACEMENT placement = {sizeof(WINDOWPLACEMENT)};
		placement.showCmd = WindowSizeStateToShowCmd((WindowSizeState)g_Config.iWindowSizeState);
		placement.rcNormalPosition.left = g_Config.iWindowX;
		placement.rcNormalPosition.top = g_Config.iWindowY;
		placement.rcNormalPosition.right = g_Config.iWindowX + g_Config.iWindowWidth;
		placement.rcNormalPosition.bottom = g_Config.iWindowY + g_Config.iWindowHeight;
		SetWindowPlacement(hwndMain, &placement);

		// SetWindowLong(hwndMain, GWL_EXSTYLE, WS_EX_APPWINDOW);

		const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DONOTROUND;
		DwmSetWindowAttribute(hwndMain, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

		MainMenuInit(hwndMain, g_hMenu);

		// Accept dragged files.
		DragAcceptFiles(hwndMain, TRUE);

		hideCursor = true;
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);

		ApplyFullscreenState(hwndMain, g_Config.bFullScreen);

		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		touchHandler.registerTouchWindow(hwndMain);

		WindowsRawInput::Init();

		UpdateWindow(hwndMain);

		SetFocus(hwndMain);

		g_inForcedResize = false;

		return TRUE;
	}

	void CreateDisasmWindow() {
		if (!disasmWindow) {
			disasmWindow = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(disasmWindow);
		}
		if (disasmMapLoadPending) {
			disasmWindow->NotifyMapLoaded();
			disasmMapLoadPending = false;
		}
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
		if (memoryMapLoadPending) {
			memoryWindow->NotifyMapLoaded();
			memoryMapLoadPending = false;
		}
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

	bool ConfirmAction(HWND hWnd, bool actionIsReset) {
		const GlobalUIState state = GetUIState();
		if (state == UISTATE_MENU || state == UISTATE_EXIT) {
			return true;
		}

		std::string confirmExitMessage = GetConfirmExitMessage();
		if (confirmExitMessage.empty()) {
			return true;
		}
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		if (!actionIsReset) {
			confirmExitMessage += '\n';
			confirmExitMessage += di->T("Are you sure you want to exit?");
		} else {
			// Reset is bit rarer, let's just omit the extra message for now.
		}
		return IDYES == MessageBox(hWnd, ConvertUTF8ToWString(confirmExitMessage).c_str(), ConvertUTF8ToWString(mm->T("Exit")).c_str(), MB_YESNO | MB_ICONQUESTION);
	}

	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)	{
		static bool firstErase = true;
		LRESULT darkResult = 0;
		if (UAHDarkModeWndProc(hWnd, message, wParam, lParam, &darkResult)) {
			return darkResult;
		}

		static bool first = true;
		switch (message) {
		case WM_CREATE:
			first = true;
			if (!IsVistaOrHigher()) {
				// Remove the D3D11 choice on versions below XP
				RemoveMenu(GetMenu(hWnd), ID_OPTIONS_DIRECT3D11, MF_BYCOMMAND);
			}
			if (g_darkModeSupported) {
				SendMessageW(hWnd, WM_THEMECHANGED, 0, 0);
			}
			SetAssertDialogParent(hWnd);
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

		case WM_USER_GET_CURRENT_GAMEID:
		{
			// Return game ID as four u32 values
			// wParam: 0-3 = which u32 to return (chars 0-3, 4-7, 8-11, 12-15)
			// Returns: packed u32 with 4 bytes of game ID
			if (!PSP_IsInited()) {
				return 0;
			}
			const std::string gameID = Reporting::CurrentGameID();
			if (gameID.empty()) {
				return 0;
			}
			const size_t offset = (wParam & 0x3) * 4;  // 0, 4, 8, 12
			u32 packed = 0;
			for (size_t i = 0; i < 4; ++i) {
				if (offset + i < gameID.length()) {
					const u8 c = static_cast<u8>(gameID[offset + i]);
					packed |= ((u32)c << (i * 8));
				}
			}
			return packed;
		}
		case WM_USER_GET_MODULE_INFO:
		{
			// Get module information by name
			// wParam: pointer to module name (null-terminated string)
			// lParam: 0 = address, 1 = size, 2 = active flag
			// Returns: u64 packed with module info, or 0 if not found
			if (!PSP_IsInited() || !g_symbolMap)
			{
				return 0;
			}
			const char* moduleName = reinterpret_cast<const char*>(wParam);
			if (!moduleName)
			{
				return 0;
			}
			// Get all modules from symbol map
			auto modules = g_symbolMap->getAllModules();
			for (const auto& module : modules)
			{
				if (module.name == moduleName)
				{
					switch (lParam)
					{
					case 0:
						// Return address as u32 (low 32 bits)
						return (u64)module.address;
					case 1:
						// Return size as u32 (low 32 bits)
						return (u64)module.size;
					case 2:
						// Return active flag in bit 0, padded with zeros
						return (u64)(module.active ? 1 : 0);
					case 3:
						// Return all info packed: address (bits 0-31), size (bits 32-62), active (bit 63)
						return ((u64)module.address) | (((u64)module.size) << 32) | (module.active ? (1ULL << 63) : 0);
					default:
						return 0;
					}
				}
			}
			// Module not found
			return 0;
		}

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
			RECT r = W32Util::GetNonclientMenuBorderRect(hWnd);
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
				const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
				bool portrait = config.InternalRotationIsPortrait();
				rc.right = portrait ? 272 : 480;
				rc.bottom = portrait ? 480 : 272;
				AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
				minmax->ptMinTrackSize.x = rc.right - rc.left;
				minmax->ptMinTrackSize.y = rc.bottom - rc.top;
				return 0;
			}

		case WM_ACTIVATE:
			{
				UpdateWindowTitle();
				bool pause = true;
				if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
					WindowsRawInput::GainFocus();
					if (!IsIconic(GetHWND())) {
						g_InputManager.GainFocus();
					}
					g_activeWindow = WINDOW_MAINWINDOW;
					pause = false;
				} else {
					g_activeWindow = WINDOW_OTHER;
				}

				if (!noFocusPause && g_Config.bPauseOnLostFocus && GetUIState() == UISTATE_INGAME) {
					if (pause != Core_IsStepping()) {
						if (disasmWindow) {
							SendMessage(disasmWindow->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						} else {
							if (pause) {
								Core_Break(BreakReason::UIFocus, 0);
							} else if (Core_BreakReason() == BreakReason::UIFocus) {
								Core_Resume();
							}
						}
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
					g_InputManager.LoseFocus();
					hasFocus = false;
					trapMouse = false;
				}
			}
			break;

		case WM_SETFOCUS:
			UpdateWindowTitle();
			break;

		case WM_ERASEBKGND:
			if (firstErase) {
				firstErase = false;
				// Paint black on first erase while OpenGL stuff is loading
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			// Then never erase, let the OpenGL drawing take care of everything.
			return 1;

		case WM_USER_APPLY_FULLSCREEN:
			ApplyFullscreenState(hwndMain, g_Config.bFullScreen);
			break;

		case WM_DISPLAYCHANGE:
			// If resolution changes while we are fullscreen, re-snap to the new monitor size
			if (g_Config.bFullScreen) {
				MONITORINFO mi = {sizeof(mi)};
				if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) {
					SetWindowPos(hWnd, HWND_TOP,
						mi.rcMonitor.left, mi.rcMonitor.top,
						mi.rcMonitor.right - mi.rcMonitor.left,
						mi.rcMonitor.bottom - mi.rcMonitor.top,
						SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
				}
			}
			break;

		case WM_WINDOWPOSCHANGED:
		{
			// Handling this means that WM_SIZE and WM_MOVE won't be sent, except once during
			// window creation for some reason.
			const WINDOWPOS *pos = reinterpret_cast<WINDOWPOS*>(lParam);
			if (!pos) {
				// Uh?
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			const bool sizeChanged = !(pos->flags & SWP_NOSIZE);

			WINDOWPLACEMENT wp{sizeof(wp)};
			GetWindowPlacement(hWnd, &wp);
			if (!g_Config.bFullScreen) {
				g_Config.iWindowSizeState = (int)ShowCmdToWindowSizeState(wp.showCmd);
			}

			switch (wp.showCmd) {
			case SW_SHOWNORMAL:
			case SW_SHOWMAXIMIZED:
				if (hasFocus) {
					g_InputManager.GainFocus();
				}
				if (g_wasMinimized) {
					System_PostUIMessage(UIMessage::WINDOW_RESTORED, "true");
					g_wasMinimized = false;
				}
				break;
			case SW_SHOWMINIMIZED:
				Native_NotifyWindowHidden(true);
				if (!g_Config.bPauseWhenMinimized) {
					System_PostUIMessage(UIMessage::WINDOW_MINIMIZED, "true");
				}
				g_InputManager.LoseFocus();
				g_wasMinimized = true;
				break;
			default:
				break;
			}

			if (sizeChanged) {
				// Check that we're not in a resize that we ourselves are performing.
				if (g_Config.bFullScreen && !g_inForcedResize) {
					MONITORINFO mi = {sizeof(mi)};
					if (GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) {
						int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
						int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

						// If the new size is no longer the full monitor size, drop FS state (put back the menu
						// and recalculate window decorations without actually changing the size of the window).
						if (pos->cx != monWidth || pos->cy != monHeight) {
							g_Config.bFullScreen = false;
							if (GetMenu(hWnd) == NULL) {
								SetMenu(hWnd, g_hMenu);
							}
							DWORD style = GetWindowLong(hWnd, GWL_STYLE);
							if (!(style & WS_CAPTION)) {
								SetWindowLong(hWnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
								SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
									SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
							}
						}
					}
				}
				if (!inResizeMove) {
					HandleSizeChange();
				}
			}
			return 0;
		}

		case WM_ENTERSIZEMOVE:
			inResizeMove = true;
			break;

		case WM_EXITSIZEMOVE:
			inResizeMove = false;
			HandleSizeChange();
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
				key.flags = (KeyInputFlags)((u32)KeyInputFlags::DOWN | (u32)KeyInputFlags::HAS_WHEEL_DELTA | (wheelDelta << 16));
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

			default:
				break;
			}
			break;

		case WM_COMMAND:
			{
				if (!MainThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				MainWindowMenu_Process(hWnd, wParam);
			}
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
				if (!MainThread_Ready()) {
					return DefWindowProc(hWnd, message, wParam, lParam);
				}
				const HDROP hdrop = (HDROP)wParam;
				const int count = DragQueryFile(hdrop, 0xFFFFFFFF, 0, 0);
				if (count != 1) {
					// TODO: Translate? Or just not bother?
					MessageBox(hwndMain, L"You can only load one file at a time", L"Error", MB_ICONINFORMATION);
				} else {
					wchar_t filename[1024];
					if (DragQueryFile(hdrop, 0, filename, ARRAY_SIZE(filename)) != 0) {
						const std::string utf8_filename = ReplaceAll(ConvertWStringToUTF8(filename), "\\", "/");
						System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, utf8_filename);
					}
				}
				DragFinish(hdrop);
			}
			break;

		case WM_CLOSE:
		{
			if (ConfirmAction(hWnd, false)) {
				DestroyWindow(hWnd);
			}
			return 0;
		}

		case WM_DESTROY:
			SavePosition();
			g_InputManager.StopPolling();
			g_InputManager.Shutdown();
			WindowsRawInput::Shutdown();

			MainThread_Stop();
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
			TranslateMenus(hwndMain, g_hMenu);
			// Update checked status immediately for accelerators.
			UpdateMenus(nullptr);
			break;

		case WM_USER_WINDOW_TITLE_CHANGED:
			UpdateWindowTitle();
			break;

		case WM_USER_RESTART_EMUTHREAD:
			NativeSetRestarting();
			g_InputManager.StopPolling();
			MainThread_Stop();
			UpdateUIState(UISTATE_MENU);
			MainThread_Start(g_Config.iGPUBackend == (int)GPUBackend::OPENGL);
			g_InputManager.BeginPolling();
			break;

		case WM_USER_SWITCHUMD_UPDATED:
			UpdateSwitchUMD();
			break;

		case WM_USER_DESTROY:
			DestroyWindow(hWnd);
			break;

		case WM_INITMENUPOPUP:
			// Called when a menu or submenu is about to be opened.
			UpdateMenus((HMENU)wParam);
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
			if (g_darkModeSupported && IsColorSchemeChangeMessage(lParam)) {
				SendMessageW(hWnd, WM_THEMECHANGED, 0, 0);
			}
			return DefWindowProc(hWnd, message, wParam, lParam);

		case WM_THEMECHANGED:
		{
			if (g_darkModeSupported) {
				_AllowDarkModeForWindow(hWnd, g_darkModeEnabled);
				RefreshTitleBarThemeColor(hWnd);
			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		}

		case WM_SETCURSOR:
			if ((lParam & 0xFFFF) == HTCLIENT && g_Config.bShowImDebugger) {
				LPTSTR win32_cursor = 0;
				if (g_Config.bShowImDebugger) {
					switch (ImGui_ImplPlatform_GetCursor()) {
					case ImGuiMouseCursor_Arrow:        win32_cursor = IDC_ARROW; break;
					case ImGuiMouseCursor_TextInput:    win32_cursor = IDC_IBEAM; break;
					case ImGuiMouseCursor_ResizeAll:    win32_cursor = IDC_SIZEALL; break;
					case ImGuiMouseCursor_ResizeEW:     win32_cursor = IDC_SIZEWE; break;
					case ImGuiMouseCursor_ResizeNS:     win32_cursor = IDC_SIZENS; break;
					case ImGuiMouseCursor_ResizeNESW:   win32_cursor = IDC_SIZENESW; break;
					case ImGuiMouseCursor_ResizeNWSE:   win32_cursor = IDC_SIZENWSE; break;
					case ImGuiMouseCursor_Hand:         win32_cursor = IDC_HAND; break;
					case ImGuiMouseCursor_NotAllowed:   win32_cursor = IDC_NO; break;
					default: break;
					}
				}
				SetCursor(win32_cursor ? ::LoadCursor(nullptr, win32_cursor) : nullptr);
				return TRUE;
			} else {
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;

		// Mouse input. We send asynchronous touch events for minimal latency.
		case WM_LBUTTONDOWN:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = true;

				const float x = GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
				const float y = GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				TouchInput touch{};
				touch.flags = TouchInputFlags::DOWN | TouchInputFlags::MOUSE;
				touch.buttons = 1;
				touch.x = x;
				touch.y = y;
				NativeTouch(touch);
				SetCapture(hWnd);

				// Simulate doubleclick, doesn't work with RawInput enabled
				static double lastMouseDownTime;
				static float lastMouseDownX = -1.0f;
				static float lastMouseDownY = -1.0f;
				const double now = time_now_d();
				if ((now - lastMouseDownTime) < 0.001 * GetDoubleClickTime()) {
					const float dx = lastMouseDownX - x;
					const float dy = lastMouseDownY - y;
					const float distSq = dx * dx + dy * dy;
					if (distSq < 3.0f*3.0f && !g_Config.bShowTouchControls && !g_Config.bShowImDebugger && !g_Config.bMouseControl && GetUIState() == UISTATE_INGAME && g_Config.bFullscreenOnDoubleclick) {
						g_Config.bFullScreen = !g_Config.bFullScreen;
						SendApplyFullscreenState();
					}
					lastMouseDownTime = 0.0;
				} else {
					lastMouseDownTime = now;
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
				// Require at least 2 pixels of movement to reset the hide timer.
				if (abs(cursorX - prevCursorX) > 1 || abs(cursorY - prevCursorY) > 1) {
					hideCursor = false;
					SetTimer(hwndMain, TIMER_CURSORMOVEUPDATE, CURSORUPDATE_MOVE_TIMESPAN_MS, 0);
				}
				prevCursorX = cursorX;
				prevCursorY = cursorY;

				const float x = (float)cursorX * g_display.dpi_scale_x;
				const float y = (float)cursorY * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				// Mouse moves now happen also when no button is pressed.
				TouchInput touch{};
				touch.flags = TouchInputFlags::MOVE | TouchInputFlags::MOUSE;
				if (wParam & MK_LBUTTON) {
					touch.buttons |= 1;
				}
				if (wParam & MK_RBUTTON) {
					touch.buttons |= 2;
				}
				touch.x = x;
				touch.y = y;
				NativeTouch(touch);
			}
			break;

		case WM_LBUTTONUP:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_MASK_PLUS_PENTOUCH) != MOUSEEVENTF_FROMTOUCH_NOPEN)
			{
				// Hack: Take the opportunity to hide the cursor.
				mouseButtonDown = false;

				const float x = (float)GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
				const float y = (float)GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
				WindowsRawInput::SetMousePos(x, y);

				TouchInput touch{};
				touch.buttons = 1;
				touch.flags = TouchInputFlags::UP | TouchInputFlags::MOUSE;
				touch.x = x;
				touch.y = y;
				NativeTouch(touch);
				ReleaseCapture();
			}
			break;

		case WM_TOUCH:
			touchHandler.handleTouchEvent(hWnd, message, wParam, lParam);
			return 0;

		case WM_RBUTTONDOWN:
		{
			TouchInput touch{};
			touch.buttons = 2;
			touch.flags = TouchInputFlags::DOWN | TouchInputFlags::MOUSE;
			touch.x = GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
			touch.y = GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
			NativeTouch(touch);
			break;
		}

		case WM_RBUTTONUP:
		{
			TouchInput touch{};
			touch.buttons = 2;
			touch.flags = TouchInputFlags::UP | TouchInputFlags::MOUSE;
			touch.x = GET_X_LPARAM(lParam) * g_display.dpi_scale_x;
			touch.y = GET_Y_LPARAM(lParam) * g_display.dpi_scale_y;
			NativeTouch(touch);
			break;
		}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	void ToggleDebugConsoleVisibility() {
		if (!g_Config.bEnableLogging) {
			g_logManager.GetConsoleListener()->Show(false);
			EnableMenuItem(g_hMenu, ID_DEBUG_LOG, MF_GRAYED);
		}
		else {
			g_logManager.GetConsoleListener()->Show(true);
			EnableMenuItem(g_hMenu, ID_DEBUG_LOG, MF_ENABLED);
		}
	}

	void SendApplyFullscreenState() {
		PostMessage(hwndMain, WM_USER_APPLY_FULLSCREEN, 0, 0);
	}

	void RunCallbackInWndProc(void (*callback)(void *, void *), void *userdata) {
		PostMessage(hwndMain, WM_USER_RUN_CALLBACK, reinterpret_cast<WPARAM>(callback), reinterpret_cast<LPARAM>(userdata));
	}

}  // namespace MainWindow
