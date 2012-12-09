#pragma once

#include <windows.h>

namespace MainWindow
{
	void Init(HINSTANCE hInstance);
	BOOL Show(HINSTANCE hInstance, int nCmdShow);
	void Close();
	void UpdateMenus();
	void Update();
	void Redraw();
	HWND GetHWND();
	HINSTANCE GetHInstance();
	HWND GetDisplayHWND();
	void SetPlaying(const char*text);
	void _ViewFullScreen(HWND hWnd);
	void _ViewNormal(HWND hWnd);
}
