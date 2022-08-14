#pragma once

#include <vector>

#include "CommonWindows.h"

class Dialog;

class TabControl
{
public:
	TabControl(HWND handle, bool noDisplayArea = false);
	void HandleNotify(LPARAM lParam);
	int HitTest(const POINT &screenPos);
	HWND AddTabWindow(const wchar_t* className, const wchar_t* title, DWORD style = 0);
	void AddTabDialog(Dialog* dialog, const wchar_t* title);
	void AddTab(HWND hwnd, const wchar_t* title);
	HWND RemoveTab(int index);
	void ShowTab(int index, bool setControlIndex = true);
	void ShowTab(HWND pageHandle);
	void NextTab(bool cycle);
	void PreviousTab(bool cycle);
	int CurrentTabIndex() { return currentTab; }
	HWND CurrentTabHandle() {
		if (currentTab < 0 || currentTab >= (int)tabs.size()) {
			return NULL;
		}
		return tabs[currentTab].pageHandle;
	}
	void SetShowTabTitles(bool enabled);
	void SetIgnoreBottomMargin(bool enabled) { ignoreBottomMargin = enabled; }
	bool GetShowTabTitles() { return showTabTitles; }
	void SetMinTabWidth(int w);

	int Count() {
		return (int)tabs.size();
	}

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void OnResize();
	int AppendPageToControl(const wchar_t* title);

	struct TabInfo
	{
		bool hasBorder;
		bool hasClientEdge;
		HWND lastFocus;
		HWND pageHandle;
		wchar_t title[128];
	};

	HWND hwnd;
	WNDPROC oldProc;
	std::vector<TabInfo> tabs;
	bool showTabTitles = true;
	bool ignoreBottomMargin = false;
	int currentTab = 0;
	bool hasButtons;
	bool noDisplayArea_;
};
