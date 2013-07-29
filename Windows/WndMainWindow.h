#pragma once

#include "Common/CommonWindows.h"
#include <string>

#include "Core/System.h"

namespace MainWindow
{
	enum {
		WM_USER_SAVESTATE_FINISH = WM_USER + 100,
		WM_USER_LOG_STATUS_CHANGED = WM_USER + 200,
	};
	void Init(HINSTANCE hInstance);
	BOOL Show(HINSTANCE hInstance, int nCmdShow);
	void Close();
	void UpdateMenus();
	void UpdateCommands();
	void Update();
	void Redraw();
	HWND GetHWND();
	HINSTANCE GetHInstance();
	HWND GetDisplayHWND();
	void SetPlaying(const char*text);
	void BrowseAndBoot(std::string defaultPath, bool browseDirectory = false);
	void SaveStateActionFinished(bool result, void *userdata);
	void _ViewFullScreen(HWND hWnd);
	void _ViewNormal(HWND hWnd);
}
