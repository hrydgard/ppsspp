#pragma once

#include "Common/CommonWindows.h"
#include <string>

#include "Core/System.h"

namespace MainWindow
{
	enum {
		WM_USER_SAVESTATE_FINISH = WM_USER + 100,
		WM_USER_UPDATE_UI = WM_USER + 101,
		WM_USER_UPDATE_SCREEN = WM_USER + 102,
		WM_USER_WINDOW_TITLE_CHANGED = WM_USER + 103,
		WM_USER_BROWSE_BOOT_DONE = WM_USER + 104,
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
	BOOL Show(HINSTANCE hInstance, int nCmdShow);
	void CreateDebugWindows();
	void DestroyDebugWindows();
	void Close();
	void UpdateMenus();
	void UpdateCommands();
	void SetWindowTitle(const wchar_t *title);
	void Update();
	void Redraw();
	HWND GetHWND();
	HINSTANCE GetHInstance();
	void BrowseAndBoot(std::string defaultPath, bool browseDirectory = false);
	void SaveStateActionFinished(bool result, void *userdata);
	void _ViewFullScreen(HWND hWnd);
	void _ViewNormal(HWND hWnd);
	void ToggleDebugConsoleVisibility();
	void TranslateMenus();
	void setTexScalingMultiplier(int level);
	void UmdSwitchAction();
}
