#pragma once

#include <vector>

class Dialog;

class TabControl
{
public:
	TabControl(HWND handle);
	void HandleNotify(LPARAM lParam);
	HWND AddTabWindow(wchar_t* className, wchar_t* title, DWORD style = 0);
	void AddTabDialog(Dialog* dialog, wchar_t* title);
	void ShowTab(int index, bool setControlIndex = true);
	void ShowTab(HWND pageHandle);
	void NextTab(bool cycle);
	void PreviousTab(bool cycle);
private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void OnResize();
	HWND hwnd;
	WNDPROC oldProc;
	std::vector<HWND> tabs;
};