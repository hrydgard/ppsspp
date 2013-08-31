#pragma once

#include "Common/CommonWindows.h"
#include <string>

#include "Core/System.h"

namespace MainWindow
{
	enum {
		WM_USER_SAVESTATE_FINISH = WM_USER + 100,
		WM_USER_LOG_STATUS_CHANGED = WM_USER + 200,
		WM_USER_ATRAC_STATUS_CHANGED = WM_USER + 300,
	};

	enum {
		FRAMESKIP_OFF = 0,
		FRAMESKIP_AUTO = 1,
		FRAMESKIP_2 = 2,
		FRAMESKIP_3 = 3,
		FRAMESKIP_4 = 4,
		FRAMESKIP_5 = 5,
		FRAMESKIP_6 = 6,
		FRAMESKIP_7 = 7,
		FRAMESKIP_8 = 8,
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
