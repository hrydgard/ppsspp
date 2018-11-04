#pragma once

#include "ppsspp_config.h"

#if !PPSSPP_PLATFORM(UWP)

#include "Common/CommonWindows.h"
#include <string>

#include "Core/System.h"
#include "MainWindowMenu.h"


namespace MainWindow
{
	enum {
		WM_USER_SAVESTATE_FINISH = WM_USER + 100,
		WM_USER_UPDATE_UI = WM_USER + 101,
		WM_USER_BROWSE_BG_DONE = WM_USER + 102,
		WM_USER_WINDOW_TITLE_CHANGED = WM_USER + 103,
		WM_USER_BROWSE_BOOT_DONE = WM_USER + 104,
		WM_USER_TOGGLE_FULLSCREEN = WM_USER + 105,
		WM_USER_RESTART_EMUTHREAD = WM_USER + 106,
	};

	enum {
		FRAMESKIP_OFF = 0,
		FRAMESKIP_1 = 1,
		FRAMESKIP_2 = 2,
		FRAMESKIP_3 = 3,
		FRAMESKIP_4 = 4,
		FRAMESKIP_5 = 5,
		FRAMESKIP_6 = 6,
		FRAMESKIP_7 = 7,
		FRAMESKIP_8 = 8,
		FRAMESKIP_MAX = FRAMESKIP_8,

		FRAMESKIPTYPE_COUNT = 0,
		FRAMESKIPTYPE_PRCNT = 1,

		RESOLUTION_AUTO = 0,
		RESOLUTION_NATIVE = 1,
		RESOLUTION_2X = 2,
		RESOLUTION_3X = 3,
		RESOLUTION_4X = 4,
		RESOLUTION_5X = 5,
		RESOLUTION_6X = 6,
		RESOLUTION_7X = 7,
		RESOLUTION_8X = 8,
		RESOLUTION_9X = 9,
		RESOLUTION_MAX = 10,
		
		TEXSCALING_AUTO = 0,
		TEXSCALING_OFF = 1,
		TEXSCALING_2X = 2,
		TEXSCALING_3X = 3,
		TEXSCALING_4X = 4,
		TEXSCALING_5X = 5,
		TEXSCALING_MAX = TEXSCALING_5X,
	};

	void Init(HINSTANCE hInstance);
	BOOL Show(HINSTANCE hInstance);
	void CreateDebugWindows();
	void DestroyDebugWindows();
	void Close();
	void UpdateMenus(bool isMenuSelect = false);
	void UpdateCommands();
	void SetWindowTitle(const wchar_t *title);
	void Redraw();
	HWND GetHWND();
	HINSTANCE GetHInstance();
	HWND GetDisplayHWND();
	void ToggleFullscreen(HWND hWnd, bool goingFullscreen);
	void Minimize();
	void SendToggleFullscreen(bool fullscreen);  // To be used off-thread
	void ToggleDebugConsoleVisibility();
	void SetInternalResolution(int res = -1);
	void SetWindowSize(int zoom);
}

#endif
