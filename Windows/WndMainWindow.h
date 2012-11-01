#pragma once


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
	void RequestWindowSize(int w, int h);
}
