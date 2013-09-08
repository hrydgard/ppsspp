#pragma once

#include "Common/CommonWindows.h"
#include <string>

#include "Core/System.h"

namespace MainWindow
{
	enum {
		WM_USER_SAVESTATE_FINISH = WM_USER + 100,
		WM_USER_LOG_STATUS_CHANGED = WM_USER + 101,
		WM_USER_ATRAC_STATUS_CHANGED = WM_USER + 102,
		WM_USER_UPDATE_UI = WM_USER + 103,
		WM_USER_UPDATE_SCREEN = WM_USER + 104,
	};

	enum {
		FRAMESKIP_OFF = 0,
		FRAMESKIP_AUTO = 1,
		FRAMESKIP_1 = 2,
		FRAMESKIP_2 = 3,
		FRAMESKIP_3 = 4,
		FRAMESKIP_4 = 5,
		FRAMESKIP_5 = 6,
		FRAMESKIP_6 = 7,
		FRAMESKIP_7 = 8,
		FRAMESKIP_8 = 9,
		FRAMESKIP_MAX = FRAMESKIP_8,

		ZOOM_NATIVE = 1,
		ZOOM_2X = 2,
		ZOOM_3X = 3,
		ZOOM_4X = 4,
		ZOOM_MAX = ZOOM_4X,

		TEXSCALING_OFF = 1,
		TEXSCALING_2X = 2,
		TEXSCALING_3X = 3,
		TEXSCALING_4X = 4,
		TEXSCALING_5X = 5,
		TEXSCALING_MAX = TEXSCALING_5X,
	};

	void Init(HINSTANCE hInstance);
	BOOL Show(HINSTANCE hInstance, int nCmdShow);
	void CreateDebugWindows();
	void Close();
	void UpdateMenus();
	void UpdateCommands();
	void Update();
	void Redraw();
	HWND GetHWND();
	HINSTANCE GetHInstance();
	HWND GetDisplayHWND();
	void BrowseAndBoot(std::string defaultPath, bool browseDirectory = false);
	void SaveStateActionFinished(bool result, void *userdata);
	void _ViewFullScreen(HWND hWnd);
	void _ViewNormal(HWND hWnd);
	void TranslateMenus();
}
