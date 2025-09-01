#pragma once

#include "Common/CommonWindows.h"
#include <Windowsx.h>

#include "Common/System/Request.h"
#include "Core/System.h"

namespace MainWindow {
	void MainMenuInit(HWND hWndMain, HMENU hMenu);
	void MainWindowMenu_Process(HWND hWnd, WPARAM wParam);
	void TranslateMenus(HWND hWnd, HMENU menu);
	void BrowseAndBoot(RequesterToken token, std::string defaultPath, bool browseDirectory = false);
	void setTexScalingMultiplier(int level);
	void SetIngameMenuItemStates(HMENU menu, const GlobalUIState state);
	void HideDebugWindows();
}
