// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.
// It's improving slowly, though. :)

#include "Common/CommonWindows.h"

#include <map>
#include <string>

#include "base/NativeApp.h"
#include "Globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "i18n/i18n.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "util/text/utf8.h"

#include "Core/Debugger/SymbolMap.h"
#include "Windows/InputBox.h"
#include "Windows/OpenGLBase.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "main.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Windows/EmuThread.h"

#include "resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/WindowsHost.h"
#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/RawInput.h"
#include "Windows/TouchInputHandler.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "gfx_es2/gpu_features.h"
#include "GPU/GLES/TextureScaler.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "ControlMapping.h"
#include "UI/OnScreenDisplay.h"
#include "GPU/Common/PostShader.h"

#include "Core/HLE/sceUmd.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

#define MOUSEEVENTF_FROMTOUCH 0xFF515700

static const int numCPUs = 1;

int verysleepy__useSendMessage = 1;

const UINT WM_VERYSLEEPY_MSG = WM_APP + 0x3117;
// Respond TRUE to a message with this param value to indicate support.
const WPARAM VERYSLEEPY_WPARAM_SUPPORTED = 0;
// Respond TRUE to a message wit this param value after filling in the addr name.
const WPARAM VERYSLEEPY_WPARAM_GETADDRINFO = 1;

struct VerySleepy_AddrInfo
{
	// Always zero for now.
	int flags;
	// This is the pointer (always passed as 64 bits.)
	unsigned long long addr;
	// Write the name here.
	wchar_t name[256];
};

extern std::map<int, int> windowsTransTable;
static RECT g_normalRC = {0};
static std::wstring windowTitle;
extern bool g_TakeScreenshot;
extern InputState input_state;

#define TIMER_CURSORUPDATE 1
#define TIMER_CURSORMOVEUPDATE 2
#define CURSORUPDATE_INTERVAL_MS 1000
#define CURSORUPDATE_MOVE_TIMESPAN_MS 500

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	TouchInputHandler touchHandler;
	static HMENU menu;

	static HINSTANCE hInst;
	static int cursorCounter = 0;
	static int prevCursorX = -1;
	static int prevCursorY = -1;
	static bool mouseButtonDown = false;
	static bool hideCursor = false;
	static std::map<int, std::string> initialMenuKeys;
	static std::vector<std::string> countryCodes;
	static std::vector<std::string> availableShaders;
	static W32Util::AsyncBrowseDialog *browseDialog;
	static bool browsePauseAfter;
#define MAX_LOADSTRING 100
	const TCHAR *szTitle = TEXT("PPSSPP");
	const TCHAR *szWindowClass = TEXT("PPSSPPWnd");
	const TCHAR *szDisplayClass = TEXT("PPSSPPDisplay");

	// Forward declarations of functions included in this code module:
	LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWND() {
		return hwndMain;
	}

	HWND GetDisplayHWND() {
		return hwndDisplay;
	}

	void Init(HINSTANCE hInstance) {
#ifdef THEMES
		WTL::CTheme::IsThemingSupported();
#endif
		//Register classes
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX); 
		wcex.style			= CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc	= (WNDPROC)WndProc;
		wcex.cbClsExtra		= 0;
		wcex.cbWndExtra		= 0;
		wcex.hInstance		= hInstance;
		wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP); 
		wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName	= (LPCWSTR)IDR_MENU1;
		wcex.lpszClassName	= szWindowClass;
		wcex.hIconSm		= (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16,16,LR_SHARED);
		RegisterClassEx(&wcex);

		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = (WNDPROC)DisplayProc;
		wcex.hIcon = 0;
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = szDisplayClass;
		wcex.hIconSm = 0;
		RegisterClassEx(&wcex);
	}

	void SavePosition() {
		if (g_Config.bFullScreen) return;

		WINDOWPLACEMENT placement;
		GetWindowPlacement(hwndMain, &placement);
		if (placement.showCmd == SW_SHOWNORMAL) {
			RECT rc;
			GetWindowRect(hwndMain, &rc);
			g_Config.iWindowX = rc.left;
			g_Config.iWindowY = rc.top;
			g_Config.iWindowWidth = rc.right - rc.left;
			g_Config.iWindowHeight = rc.bottom - rc.top;
		}
	}

	void GetWindowRectAtResolution(int xres, int yres, RECT &rcInner, RECT &rcOuter) {
		rcInner.left = 0;
		rcInner.top = 0;

		rcInner.right = xres;
		rcInner.bottom = yres;

		rcOuter = rcInner;
		AdjustWindowRect(&rcOuter, WS_OVERLAPPEDWINDOW, TRUE);
		rcOuter.right += g_Config.iWindowX - rcOuter.left;
		rcOuter.bottom += g_Config.iWindowY - rcOuter.top;
		rcOuter.left = g_Config.iWindowX;
		rcOuter.top = g_Config.iWindowY;
	}

	void ResizeDisplay(bool displayOSM = true, bool noWindowMovement = false) {
		RECT rc;
		GetClientRect(hwndMain, &rc);
		if (!noWindowMovement) {
			if ((rc.right - rc.left) == PSP_CoreParameter().pixelWidth &&
				(rc.bottom - rc.top) == PSP_CoreParameter().pixelHeight)
				return;

			PSP_CoreParameter().pixelWidth = rc.right - rc.left;
			PSP_CoreParameter().pixelHeight = rc.bottom - rc.top;
			MoveWindow(hwndDisplay, 0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, TRUE);
		}

		// Round up to a zoom factor for the render size.
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) // auto mode
		{
			zoom = (rc.right - rc.left + 479) / 480;
			PSP_CoreParameter().renderWidth = 480 * zoom;
			PSP_CoreParameter().renderHeight = 272 * zoom;
			PSP_CoreParameter().outputWidth = 480 * zoom;
			PSP_CoreParameter().outputHeight = 272 * zoom;
		}
		
		if (displayOSM) {
			I18NCategory *g = GetI18NCategory("Graphics");
			char message[256];
			sprintf(message, "%s: %ix%i  %s: %ix%i",
				g->T("Internal Resolution"), PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight,
				g->T("Window Size"), PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			osm.Show(g->T(message), 2.0f);
		}

		NativeMessageReceived("gpu resized", "");
	}

	void SetWindowSize(int zoom) {
		RECT rc, rcOuter;
		GetWindowRectAtResolution(480 * (int)zoom, 272 * (int)zoom, rc, rcOuter);
		MoveWindow(hwndMain, rcOuter.left, rcOuter.top, rcOuter.right - rcOuter.left, rcOuter.bottom - rcOuter.top, TRUE);
		ResizeDisplay(true, true);
	}

	void SetInternalResolution(int res = -1) {
		if (res >= 0 && res <= RESOLUTION_MAX)
			g_Config.iInternalResolution = res;
		else {
			if (++g_Config.iInternalResolution > RESOLUTION_MAX)
				g_Config.iInternalResolution = 0;
		}
		
		// Taking auto-texture scaling into account
		if (g_Config.iTexScalingLevel == TEXSCALING_AUTO)
			setTexScalingMultiplier(0);

		ResizeDisplay(true, true);
	}

	void CorrectCursor() {
		bool autoHide = g_Config.bFullScreen && !mouseButtonDown && globalUIState == UISTATE_INGAME;
		if (autoHide && hideCursor) {
			while (cursorCounter >= 0) {
				cursorCounter = ShowCursor(FALSE);
			}
		} else {
			hideCursor = !autoHide;
			if (cursorCounter < 0) {
				cursorCounter = ShowCursor(TRUE);
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
		}
	}

	void _ViewNormal(HWND hWnd) {
		// Put caption and border styles back.
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU;
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// Put back the menu bar.
		::SetMenu(hWnd, menu);

		// Resize to normal view.
		// NOTE: Use SWP_FRAMECHANGED to force redraw non-client.
		const int x = g_normalRC.left;
		const int y = g_normalRC.top;
		const int cx = g_normalRC.right - g_normalRC.left;
		const int cy = g_normalRC.bottom - g_normalRC.top;
		::SetWindowPos(hWnd, HWND_NOTOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// Reset full screen indicator.
		g_Config.bFullScreen = false;
		CorrectCursor();

		bool showOSM = (g_Config.iInternalResolution == RESOLUTION_AUTO);
		ResizeDisplay(showOSM, true);
		ShowOwnedPopups(hwndMain, TRUE);
		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);
	}

	void _ViewFullScreen(HWND hWnd) {
		// Keep in mind normal window rectangle.
		::GetWindowRect(hWnd, &g_normalRC);

		// Remove caption and border styles.
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// Remove the menu bar.
		::SetMenu(hWnd, NULL);

		// Resize to full screen view.
		// NOTE: Use SWP_FRAMECHANGED to force redraw non-client.
		const int x = 0;
		const int y = 0;
		const int cx = ::GetSystemMetrics(SM_CXSCREEN);
		const int cy = ::GetSystemMetrics(SM_CYSCREEN);
		::SetWindowPos(hWnd, HWND_TOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// Set full screen indicator.
		g_Config.bFullScreen = true;
		CorrectCursor();

		bool showOSM = (g_Config.iInternalResolution == RESOLUTION_AUTO);
		ResizeDisplay(showOSM, true);

		ShowOwnedPopups(hwndMain, FALSE);
		UpdateScreenScale();
	}

	RECT DetermineWindowRectangle() {
		RECT rc;

		const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

		if (!g_Config.bFullScreen) {
			bool visibleHorizontally = ((g_Config.iWindowX + g_Config.iWindowWidth) >= screenX) &&
				((g_Config.iWindowX + g_Config.iWindowWidth) < (screenWidth + g_Config.iWindowWidth));

			bool visibleVertically = ((g_Config.iWindowY + g_Config.iWindowHeight) >= screenY) &&
				((g_Config.iWindowY + g_Config.iWindowHeight) < (screenHeight + g_Config.iWindowHeight));

			if (!visibleHorizontally)
				g_Config.iWindowX = -1;

			if (!visibleVertically)
				g_Config.iWindowY = -1;
		}

		rc.left = g_Config.iWindowX;
		rc.top = g_Config.iWindowY;

		// First, get the w/h right.
		if (g_Config.iWindowWidth <= 0 || g_Config.iWindowHeight <= 0) {
			RECT rcInner = rc, rcOuter;
			GetWindowRectAtResolution(2 * 480, 2 * 272, rcInner, rcOuter);
			rc.right = rc.left + (rcOuter.right - rcOuter.left);
			rc.bottom = rc.top + (rcOuter.bottom - rcOuter.top);
			g_Config.iWindowWidth = rc.right - rc.left;
			g_Config.iWindowHeight = rc.bottom - rc.top;
		} else {
			rc.right = rc.left + g_Config.iWindowWidth;
			rc.bottom = rc.top + g_Config.iWindowHeight;
		}

		// Then center if necessary.
		if (g_Config.iWindowX == -1 && g_Config.iWindowY == -1) {
			// Center the window.
			const int primaryScreenWidth = GetSystemMetrics(SM_CXSCREEN);
			const int primaryScreenHeight = GetSystemMetrics(SM_CYSCREEN);
			g_Config.iWindowX = (primaryScreenWidth - g_Config.iWindowWidth) / 2;
			g_Config.iWindowY = (primaryScreenHeight - g_Config.iWindowHeight) / 2;
			rc.left = g_Config.iWindowX;
			rc.top = g_Config.iWindowY;
			rc.right = rc.left + g_Config.iWindowWidth;
			rc.bottom = rc.top + g_Config.iWindowHeight;
		}

		return rc;
	}

	void SetIngameMenuItemStates(const GlobalUIState state) {
		UINT menuEnable = state == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED;
		UINT umdSwitchEnable = state == UISTATE_INGAME && getUMDReplacePermit()? MF_ENABLED : MF_GRAYED;

		EnableMenuItem(menu, ID_FILE_SAVESTATEFILE, menuEnable);
		EnableMenuItem(menu, ID_FILE_LOADSTATEFILE, menuEnable);
		EnableMenuItem(menu, ID_FILE_QUICKSAVESTATE, menuEnable);
		EnableMenuItem(menu, ID_FILE_QUICKLOADSTATE, menuEnable);
		EnableMenuItem(menu, ID_TOGGLE_PAUSE, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_STOP, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_RESET, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_SWITCH_UMD, umdSwitchEnable);
	}

	// These are used as an offset
	// to determine which menu item to change.
	// Make sure to count(from 0) the separators too, when dealing with submenus!!
	enum MenuItemPosition {
		// Main menus
		MENU_FILE = 0,
		MENU_EMULATION = 1,
		MENU_DEBUG = 2,
		MENU_OPTIONS = 3,
		MENU_HELP = 4,

		// File submenus
		SUBMENU_FILE_SAVESTATE_SLOT = 6,

		// Game Settings submenus
		SUBMENU_CUSTOM_SHADERS = 10,
		SUBMENU_RENDERING_RESOLUTION = 11,
		SUBMENU_WINDOW_SIZE = 12,
		SUBMENU_RENDERING_MODE = 13,
		SUBMENU_FRAME_SKIPPING = 14,
		SUBMENU_TEXTURE_FILTERING = 15,
		SUBMENU_TEXTURE_SCALING = 16,
	};

	std::string GetMenuItemText(int menuID) {
		MENUITEMINFO menuInfo;
		memset(&menuInfo, 0, sizeof(menuInfo));
		menuInfo.cbSize = sizeof(MENUITEMINFO);
		menuInfo.fMask = MIIM_STRING;
		menuInfo.dwTypeData = 0;

		std::string retVal;
		if (GetMenuItemInfo(menu, menuID, MF_BYCOMMAND, &menuInfo) != FALSE) {
			wchar_t *buffer = new wchar_t[++menuInfo.cch];
			menuInfo.dwTypeData = buffer;
			GetMenuItemInfo(menu, menuID, MF_BYCOMMAND, &menuInfo);
			retVal = ConvertWStringToUTF8(menuInfo.dwTypeData);
			delete [] buffer;
		}

		return retVal;
	}

	const std::string &GetMenuItemInitialText(const int menuID) {
		if (initialMenuKeys.find(menuID) == initialMenuKeys.end()) {
			initialMenuKeys[menuID] = GetMenuItemText(menuID);
		}
		return initialMenuKeys[menuID];
	}

	void CreateHelpMenu() {
		I18NCategory *des = GetI18NCategory("DesktopUI");

		const std::wstring help = ConvertUTF8ToWString(des->T("Help"));
		const std::wstring visitMainWebsite = ConvertUTF8ToWString(des->T("www.ppsspp.org"));
		const std::wstring visitForum = ConvertUTF8ToWString(des->T("PPSSPP Forums"));
		const std::wstring buyGold = ConvertUTF8ToWString(des->T("Buy Gold"));
		const std::wstring aboutPPSSPP = ConvertUTF8ToWString(des->T("About PPSSPP..."));

		// Simply remove the old help menu and create a new one.
		RemoveMenu(menu, MENU_HELP, MF_BYPOSITION);

		HMENU helpMenu = CreatePopupMenu();
		InsertMenu(menu, MENU_HELP, MF_POPUP | MF_STRING | MF_BYPOSITION, (UINT_PTR)helpMenu, help.c_str());

		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENWEBSITE, visitMainWebsite.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENFORUM, visitForum.c_str());
		// Repeat the process for other languages, if necessary.
		if(g_Config.sLanguageIni == "zh_CN" || g_Config.sLanguageIni == "zh_TW") {
			const std::wstring visitChineseForum = ConvertUTF8ToWString(des->T("PPSSPP Chinese Forum"));
			AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_CHINESE_FORUM, visitChineseForum.c_str());
		}
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_BUYGOLD, buyGold.c_str());
		AppendMenu(helpMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_ABOUT, aboutPPSSPP.c_str());
	}

	void UpdateDynamicMenuCheckmarks() {
		int item = ID_SHADERS_BASE + 1;

		for (size_t i = 0; i < availableShaders.size(); i++)
			CheckMenuItem(menu, item++, ((g_Config.sPostShaderName == availableShaders[i]) ? MF_CHECKED : MF_UNCHECKED));
	}

	void CreateShadersSubmenu() {
		I18NCategory *des = GetI18NCategory("DesktopUI");
		I18NCategory *ps = GetI18NCategory("PostShaders");
		const std::wstring key = ConvertUTF8ToWString(des->T("Postprocessing Shader"));

		HMENU optionsMenu = GetSubMenu(menu, MENU_OPTIONS);
		
		HMENU shaderMenu = CreatePopupMenu();

		RemoveMenu(optionsMenu, SUBMENU_CUSTOM_SHADERS, MF_BYPOSITION);
		InsertMenu(optionsMenu, SUBMENU_CUSTOM_SHADERS, MF_POPUP | MF_STRING | MF_BYPOSITION, (UINT_PTR)shaderMenu, key.c_str());

		std::vector<ShaderInfo> info = GetAllPostShaderInfo();
		availableShaders.clear();

		int item = ID_SHADERS_BASE + 1;
		int checkedStatus = -1;

		const char *translatedShaderName = nullptr;

		for (auto i = info.begin(); i != info.end(); ++i) {
			checkedStatus = MF_UNCHECKED;
			availableShaders.push_back(i->section);
			if (g_Config.sPostShaderName == i->section) {
				checkedStatus = MF_CHECKED;
			}

			translatedShaderName = ps->T(i->section.c_str());

			AppendMenu(shaderMenu, MF_STRING | MF_BYPOSITION | checkedStatus, item++, ConvertUTF8ToWString(translatedShaderName).c_str());
		}
	}

	void _TranslateMenuItem(const int menuIDOrPosition, const char *key, bool byCommand = false, const std::wstring& accelerator = L"", const HMENU hMenu = menu) {
		I18NCategory *des = GetI18NCategory("DesktopUI");

		std::wstring translated = ConvertUTF8ToWString(des->T(key));
		translated.append(accelerator);

		u32 flags = MF_STRING | (byCommand ? MF_BYCOMMAND : MF_BYPOSITION);

		ModifyMenu(hMenu, menuIDOrPosition, flags, menuIDOrPosition, translated.c_str());
	}

	void TranslateMenuItem(const int menuID, const std::wstring& accelerator = L"", const char *key = "", const HMENU hMenu = menu) {
		if(key == nullptr || !strcmp(key, ""))
			_TranslateMenuItem(menuID, GetMenuItemInitialText(menuID).c_str(), true, accelerator, hMenu);
		else
			_TranslateMenuItem(menuID, key, true, accelerator, hMenu);
	}

	void TranslateMenu(const char *key, const MenuItemPosition mainMenuPosition, const std::wstring& accelerator = L"") {
		_TranslateMenuItem(mainMenuPosition, key, false, accelerator);
	}

	void TranslateSubMenu(const char *key, const MenuItemPosition mainMenuItem, const MenuItemPosition subMenuItem, const std::wstring& accelerator = L"") {
		_TranslateMenuItem(subMenuItem, key, false, accelerator, GetSubMenu(menu, mainMenuItem));
	}

	void TranslateMenus() {
		// Menu headers and submenu headers don't have resource IDs,
		// So we have to hardcode strings here, unfortunately.
		TranslateMenu("File", MENU_FILE);
		TranslateMenu("Emulation", MENU_EMULATION);
		TranslateMenu("Debugging", MENU_DEBUG);
		TranslateMenu("Game Settings", MENU_OPTIONS);
		TranslateMenu("Help", MENU_HELP);

		CreateShadersSubmenu();

		// File menu
		TranslateMenuItem(ID_FILE_LOAD);
		TranslateMenuItem(ID_FILE_LOAD_DIR);
		TranslateMenuItem(ID_FILE_LOAD_MEMSTICK);
		TranslateMenuItem(ID_FILE_MEMSTICK);
		TranslateSubMenu("Savestate Slot", MENU_FILE, SUBMENU_FILE_SAVESTATE_SLOT, L"\tF3");
		TranslateMenuItem(ID_FILE_QUICKLOADSTATE, L"\tF4");
		TranslateMenuItem(ID_FILE_QUICKSAVESTATE, L"\tF2");
		TranslateMenuItem(ID_FILE_LOADSTATEFILE);
		TranslateMenuItem(ID_FILE_SAVESTATEFILE);
		TranslateMenuItem(ID_FILE_EXIT, L"\tAlt+F4");

		// Emulation menu
		TranslateMenuItem(ID_TOGGLE_PAUSE, L"\tF8", "Pause");
		TranslateMenuItem(ID_EMULATION_STOP,  L"\tCtrl+W");
		TranslateMenuItem(ID_EMULATION_RESET, L"\tCtrl+B");
		TranslateMenuItem(ID_EMULATION_SWITCH_UMD, L"\tCtrl+U", "Switch UMD");
		
		// Debug menu
		TranslateMenuItem(ID_DEBUG_LOADMAPFILE);
		TranslateMenuItem(ID_DEBUG_SAVEMAPFILE);
		TranslateMenuItem(ID_DEBUG_RESETSYMBOLTABLE);
		TranslateMenuItem(ID_DEBUG_DUMPNEXTFRAME);
		TranslateMenuItem(ID_DEBUG_TAKESCREENSHOT,  L"\tF12");
		TranslateMenuItem(ID_DEBUG_SHOWDEBUGSTATISTICS);
		TranslateMenuItem(ID_DEBUG_IGNOREILLEGALREADS);
		TranslateMenuItem(ID_DEBUG_RUNONLOAD);
		TranslateMenuItem(ID_DEBUG_DISASSEMBLY, L"\tCtrl+D");
		TranslateMenuItem(ID_DEBUG_GEDEBUGGER,L"\tCtrl+G");
		TranslateMenuItem(ID_DEBUG_EXTRACTFILE);
		TranslateMenuItem(ID_DEBUG_LOG, L"\tCtrl+L");
		TranslateMenuItem(ID_DEBUG_MEMORYVIEW, L"\tCtrl+M");

		// Options menu
		TranslateMenuItem(ID_OPTIONS_LANGUAGE);
		TranslateMenuItem(ID_OPTIONS_TOPMOST);
		TranslateMenuItem(ID_OPTIONS_PAUSE_FOCUS);
		TranslateMenuItem(ID_OPTIONS_IGNOREWINKEY);
		TranslateMenuItem(ID_OPTIONS_MORE_SETTINGS);
		TranslateMenuItem(ID_OPTIONS_CONTROLS);
		TranslateMenuItem(ID_OPTIONS_STRETCHDISPLAY);
		TranslateMenuItem(ID_OPTIONS_FULLSCREEN, L"\tAlt+Return, F11");
		TranslateMenuItem(ID_OPTIONS_VSYNC);
		TranslateSubMenu("Postprocessing Shader", MENU_OPTIONS, SUBMENU_CUSTOM_SHADERS);
		TranslateSubMenu("Rendering Resolution", MENU_OPTIONS, SUBMENU_RENDERING_RESOLUTION, L"\tCtrl+1");
		TranslateMenuItem(ID_OPTIONS_SCREENAUTO);
		// Skip rendering resolution 2x-5x..
		TranslateSubMenu("Window Size", MENU_OPTIONS, SUBMENU_WINDOW_SIZE);
		// Skip window size 1x-4x..
		TranslateSubMenu("Rendering Mode", MENU_OPTIONS, SUBMENU_RENDERING_MODE, L"\tF5");
		TranslateMenuItem(ID_OPTIONS_NONBUFFEREDRENDERING);
		TranslateMenuItem(ID_OPTIONS_BUFFEREDRENDERING);
		TranslateMenuItem(ID_OPTIONS_READFBOTOMEMORYCPU);
		TranslateMenuItem(ID_OPTIONS_READFBOTOMEMORYGPU);
		TranslateSubMenu("Frame Skipping", MENU_OPTIONS, SUBMENU_FRAME_SKIPPING, L"\tF7");
		TranslateMenuItem(ID_OPTIONS_FRAMESKIP_0);
		TranslateMenuItem(ID_OPTIONS_FRAMESKIP_AUTO);
		// Skip frameskipping 1-8..
		TranslateSubMenu("Texture Filtering", MENU_OPTIONS, SUBMENU_TEXTURE_FILTERING);
		TranslateMenuItem(ID_OPTIONS_TEXTUREFILTERING_AUTO);
		TranslateMenuItem(ID_OPTIONS_NEARESTFILTERING);
		TranslateMenuItem(ID_OPTIONS_LINEARFILTERING);
		TranslateMenuItem(ID_OPTIONS_LINEARFILTERING_CG);
		TranslateSubMenu("Texture Scaling", MENU_OPTIONS, SUBMENU_TEXTURE_SCALING);
		TranslateMenuItem(ID_TEXTURESCALING_OFF);
		// Skip texture scaling 2x-5x...
		TranslateMenuItem(ID_TEXTURESCALING_XBRZ);
		TranslateMenuItem(ID_TEXTURESCALING_HYBRID);
		TranslateMenuItem(ID_TEXTURESCALING_BICUBIC);
		TranslateMenuItem(ID_TEXTURESCALING_HYBRID_BICUBIC);
		TranslateMenuItem(ID_TEXTURESCALING_DEPOSTERIZE);
		TranslateMenuItem(ID_OPTIONS_HARDWARETRANSFORM, L"\tF6");
		TranslateMenuItem(ID_OPTIONS_VERTEXCACHE);	
		TranslateMenuItem(ID_OPTIONS_SHOWFPS);
		TranslateMenuItem(ID_EMULATION_SOUND);
		TranslateMenuItem(ID_EMULATION_CHEATS, L"\tCtrl+T");

		// Help menu: it's translated in CreateHelpMenu.
		CreateHelpMenu();

		// TODO: Urgh! Why do we need this here?
		// The menu is supposed to enable/disable this stuff directly afterward.
		SetIngameMenuItemStates(globalUIState);

		DrawMenuBar(hwndMain);
		UpdateMenus();
	}

	void setTexScalingMultiplier(int level) {
		g_Config.iTexScalingLevel = level;
		NativeMessageReceived("gpu clear cache", "");
	}

	void setTexFiltering(int type) {
		g_Config.iTexFiltering = type;
	}

	void setTexScalingType(int type) {
		g_Config.iTexScalingType = type;
		NativeMessageReceived("gpu clear cache", "");
	}

	void setRenderingMode(int mode = -1) {
		if (mode >= FB_NON_BUFFERED_MODE)
			g_Config.iRenderingMode = mode;
		else {
			if (++g_Config.iRenderingMode > FB_READFBOMEMORY_GPU)
				g_Config.iRenderingMode = FB_NON_BUFFERED_MODE;
		}

		I18NCategory *g = GetI18NCategory("Graphics");

		switch(g_Config.iRenderingMode) {
		case FB_NON_BUFFERED_MODE:
			osm.Show(g->T("Non-Buffered Rendering"));
			break;

		case FB_BUFFERED_MODE:
			osm.Show(g->T("Buffered Rendering"));
			break;

		case FB_READFBOMEMORY_CPU:
			osm.Show(g->T("Read Framebuffer to Memory (CPU)"));
			break;

		case FB_READFBOMEMORY_GPU:
			osm.Show(g->T("Read Framebuffer to Memory (GPU)"));
			break;
		}

		NativeMessageReceived("gpu resized", "");
	}

	void setFpsLimit(int fps) {
		g_Config.iFpsLimit = fps;
	}

	void setFrameSkipping(int framesToSkip = -1) {
		if (framesToSkip >= FRAMESKIP_OFF)
			g_Config.iFrameSkip = framesToSkip;
		else {
			if (++g_Config.iFrameSkip > FRAMESKIP_MAX)
				g_Config.iFrameSkip = FRAMESKIP_OFF;
		}

		I18NCategory *g = GetI18NCategory("Graphics");
		const char *frameskipStr = g->T("Frame Skipping");
		const char *offStr = g->T("Off");
		const char *autoStr = g->T("Auto");

		char message[256];
		memset(message, 0, sizeof(message));

		switch(g_Config.iFrameSkip) {
		case FRAMESKIP_OFF:
			sprintf(message, "%s: %s", frameskipStr, offStr);
			break;
		case FRAMESKIP_AUTO:
			sprintf(message, "%s: %s", frameskipStr, autoStr);
			break;
		default:
			//1 means auto, 2 means 1, 3 means 2...
			sprintf(message, "%s: %d", frameskipStr, g_Config.iFrameSkip - 1);
			break;
		}

		osm.Show(message); 
	} 

	void enableCheats(bool cheats) {
		g_Config.bEnableCheats = cheats;
	}

	void UpdateWindowTitle() {
		// Seems to be fine to call now since we use a UNICODE build...
		SetWindowText(hwndMain, windowTitle.c_str());
	}

	void SetWindowTitle(const wchar_t *title) {
		windowTitle = title;
	}

	BOOL Show(HINSTANCE hInstance, int nCmdShow) {
		hInst = hInstance; // Store instance handle in our global variable.
		RECT rc = DetermineWindowRectangle();

		u32 style = WS_OVERLAPPEDWINDOW;

		hwndMain = CreateWindowEx(0,szWindowClass, L"", style,
			rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
		if (!hwndMain)
			return FALSE;

		RECT rcClient;
		GetClientRect(hwndMain, &rcClient);

		hwndDisplay = CreateWindowEx(0, szDisplayClass, L"", WS_CHILD | WS_VISIBLE,
			0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, hwndMain, 0, hInstance, 0);
		if (!hwndDisplay)
			return FALSE;

		menu = GetMenu(hwndMain);

#ifdef FINAL
		RemoveMenu(menu,2,MF_BYPOSITION);
		RemoveMenu(menu,2,MF_BYPOSITION);
#endif
		MENUINFO info;
		ZeroMemory(&info,sizeof(MENUINFO));
		info.cbSize = sizeof(MENUINFO);
		info.cyMax = 0;
		info.dwStyle = MNS_CHECKORBMP;
		info.fMask = MIM_STYLE;
		for (int i = 0; i < GetMenuItemCount(menu); i++) {
			SetMenuInfo(GetSubMenu(menu,i),&info);
		}
		UpdateMenus();

		// Accept dragged files.
		DragAcceptFiles(hwndMain, TRUE);

		hideCursor = true;
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);
		
		if(g_Config.bFullScreen)
			_ViewFullScreen(hwndMain);
		
		ShowWindow(hwndMain, nCmdShow);

		W32Util::MakeTopMost(hwndMain, g_Config.bTopMost);

		touchHandler.registerTouchWindow(hwndDisplay);

		WindowsRawInput::Init();

		SetFocus(hwndMain);
		return TRUE;
	}

	void CreateDebugWindows() {
		disasmWindow[0] = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
		DialogManager::AddDlg(disasmWindow[0]);
		disasmWindow[0]->Show(g_Config.bShowDebuggerOnLoad);

		geDebuggerWindow = new CGEDebugger(MainWindow::GetHInstance(), MainWindow::GetHWND());
		DialogManager::AddDlg(geDebuggerWindow);

		memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
		DialogManager::AddDlg(memoryWindow[0]);
	}

	void BrowseAndBoot(std::string defaultPath, bool browseDirectory) {
		static std::wstring filter = L"All supported file types (*.iso *.cso *.pbp *.elf *.prx *.zip)|*.pbp;*.elf;*.iso;*.cso;*.prx;*.zip|PSP ROMs (*.iso *.cso *.pbp *.elf *.prx)|*.pbp;*.elf;*.iso;*.cso;*.prx|Homebrew/Demos installers (*.zip)|*.zip|All files (*.*)|*.*||";
		for (int i = 0; i < (int)filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		browsePauseAfter = false;
		if (globalUIState == UISTATE_INGAME) {
			browsePauseAfter = Core_IsStepping();
			if (!browsePauseAfter)
				Core_EnableStepping(true);
		}

		W32Util::MakeTopMost(GetHWND(), false);
		if (browseDirectory) {
			browseDialog = new W32Util::AsyncBrowseDialog(GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"Choose directory");
		} else {
			browseDialog = new W32Util::AsyncBrowseDialog(W32Util::AsyncBrowseDialog::OPEN, GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"LoadFile", ConvertUTF8ToWString(defaultPath), filter, L"*.pbp;*.elf;*.iso;*.cso;");
		}
	}

	void BrowseAndBootDone() {
		std::string filename;
		if (!browseDialog->GetResult(filename)) {
			if (!browsePauseAfter) {
				Core_EnableStepping(false);
			}
		} else {
			if (globalUIState == UISTATE_INGAME || globalUIState == UISTATE_PAUSEMENU) {
				Core_EnableStepping(false);
			}

			// TODO: What is this for / what does it fix?
			if (browseDialog->GetType() != W32Util::AsyncBrowseDialog::DIR) {
				// Decode the filename with fullpath.
				char drive[MAX_PATH];
				char dir[MAX_PATH];
				char fname[MAX_PATH];
				char ext[MAX_PATH];
				_splitpath(filename.c_str(), drive, dir, fname, ext);

				filename = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			}

			filename = ReplaceAll(filename, "\\", "/");
			NativeMessageReceived("boot", filename.c_str());
		}

		W32Util::MakeTopMost(GetHWND(), g_Config.bTopMost);

		delete browseDialog;
		browseDialog = 0;
	}

	void UmdSwitchAction() {
		std::string fn;
		std::string filter = "PSP ROMs (*.iso *.cso *.pbp *.elf)|*.pbp;*.elf;*.iso;*.cso;*.prx|All files (*.*)|*.*||";
		
		for (int i=0; i<(int)filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		if (W32Util::BrowseForFileName(true, GetHWND(), L"Switch Umd", 0, ConvertUTF8ToWString(filter).c_str(), L"*.pbp;*.elf;*.iso;*.cso;",fn)) {
			fn = ReplaceAll(fn, "\\", "/");
			__UmdReplace(fn);
		}
	}

	LRESULT CALLBACK DisplayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
		// Only apply a factor > 1 in windowed mode.
		int factor = !IsZoomed(GetHWND()) && !g_Config.bFullScreen && g_Config.iWindowWidth < (480 + 80) ? 2 : 1;

		switch (message) {
		case WM_ACTIVATE:
			if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
				g_activeWindow = WINDOW_MAINWINDOW;
			}
			break;

		case WM_SETFOCUS:
			break;

		case WM_SIZE:
			break;

		case WM_ERASEBKGND:
			return DefWindowProc(hWnd, message, wParam, lParam);

		// Poor man's touch - mouse input. We send the data both as an input_state pointer,
		// and as asynchronous touch events for minimal latency.

		case WM_LBUTTONDOWN:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_FROMTOUCH) != MOUSEEVENTF_FROMTOUCH ) 
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = true;
				{
					lock_guard guard(input_state.lock);
					input_state.mouse_valid = true;
					input_state.pointer_down[0] = true;

					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}

				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_DOWN;
				touch.x = input_state.pointer_x[0];
				touch.y = input_state.pointer_y[0];
				NativeTouch(touch);
				SetCapture(hWnd);
				
			}
			break;

		case WM_MOUSEMOVE:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_FROMTOUCH) != MOUSEEVENTF_FROMTOUCH)
			{
				// Hack: Take the opportunity to show the cursor.
				mouseButtonDown = (wParam & MK_LBUTTON) != 0;
				int cursorX = GET_X_LPARAM(lParam);
				int cursorY = GET_Y_LPARAM(lParam);
				if (abs(cursorX - prevCursorX) > 1 || abs(cursorY - prevCursorY) > 1) {
					hideCursor = false;
					SetTimer(hwndMain, TIMER_CURSORMOVEUPDATE, CURSORUPDATE_MOVE_TIMESPAN_MS, 0);
				}
				prevCursorX = cursorX;
				prevCursorY = cursorY;

				{
					lock_guard guard(input_state.lock);
					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}

				if (wParam & MK_LBUTTON) {
					TouchInput touch;
					touch.id = 0;
					touch.flags = TOUCH_MOVE;
					touch.x = input_state.pointer_x[0];
					touch.y = input_state.pointer_y[0];
					NativeTouch(touch);
				}
			}
			break;

		case WM_LBUTTONUP:
			if (!touchHandler.hasTouch() ||
				(GetMessageExtraInfo() & MOUSEEVENTF_FROMTOUCH) != MOUSEEVENTF_FROMTOUCH)
			{
				// Hack: Take the opportunity to hide the cursor.
				mouseButtonDown = false;
				{
					lock_guard guard(input_state.lock);
					input_state.pointer_down[0] = false;
					input_state.pointer_x[0] = GET_X_LPARAM(lParam) * factor; 
					input_state.pointer_y[0] = GET_Y_LPARAM(lParam) * factor;
				}
				TouchInput touch;
				touch.id = 0;
				touch.flags = TOUCH_UP;
				touch.x = input_state.pointer_x[0];
				touch.y = input_state.pointer_y[0];
				NativeTouch(touch);
				ReleaseCapture();
			}
			break;

		case WM_TOUCH:
			{
				touchHandler.handleTouchEvent(hWnd, message, wParam, lParam);
				return DefWindowProc(hWnd, message, wParam, lParam);
			}

		case WM_PAINT:
			return DefWindowProc(hWnd, message, wParam, lParam);

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}
	
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)	{
		int wmId, wmEvent;
		std::string fn;
		static bool noFocusPause = false;	// TOGGLE_PAUSE state to override pause on lost focus

		switch (message) {
		case WM_CREATE:
			break;
			
		case WM_ACTIVATE:
			{
				bool pause = true;
				if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE) {
					g_activeWindow = WINDOW_MAINWINDOW;
					pause = false;
				}
				if (!noFocusPause && g_Config.bPauseOnLostFocus && globalUIState == UISTATE_INGAME) {
					if (pause != Core_IsStepping()) {	// != is xor for bools
						if (disasmWindow[0])
							SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						else
							Core_EnableStepping(pause);
					}
				}

				if (wParam == WA_INACTIVE) {
					WindowsRawInput::LoseFocus();
				}
			}
			break;

		case WM_MOVE:
			SavePosition();
			ResizeDisplay(false);
			break;

		case WM_SIZE:
			SavePosition();
			ResizeDisplay(false);
			break;

		case WM_TIMER:
			// Hack: Take the opportunity to also show/hide the mouse cursor in fullscreen mode.
			switch (wParam) {
			case TIMER_CURSORUPDATE:
				CorrectCursor();
				return 0;

			case TIMER_CURSORMOVEUPDATE:
				hideCursor = true;
				KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
				return 0;
			}
			break;

		// For some reason, need to catch this here rather than in DisplayProc.
		case WM_MOUSEWHEEL:
			{
				int wheelDelta = (short)(wParam >> 16);
				KeyInput key;
				key.deviceId = DEVICE_ID_MOUSE;

				if (wheelDelta < 0) {
					key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
					wheelDelta = -wheelDelta;
				} else {
					key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
				}
				// There's no separate keyup event for mousewheel events, let's pass them both together.
				// This also means it really won't work great for key mapping :( Need to build a 1 frame delay or something.
				key.flags = KEY_DOWN | KEY_UP | KEY_HASWHEELDELTA | (wheelDelta << 16);
				NativeKey(key);
			}
			break;

		case WM_COMMAND:
			{
				if (!EmuThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);
				I18NCategory *g = GetI18NCategory("Graphics");

				wmId    = LOWORD(wParam); 
				wmEvent = HIWORD(wParam); 
				// Parse the menu selections:
				switch (wmId) {
				case ID_FILE_LOAD:
					BrowseAndBoot("");
					break;

				case ID_FILE_LOAD_DIR:
					BrowseAndBoot("",true);
					break;

				case ID_FILE_LOAD_MEMSTICK:
					BrowseAndBoot(GetSysDirectory(DIRECTORY_GAME));
					break;

				case ID_FILE_MEMSTICK:
					ShellExecute(NULL, L"open", ConvertUTF8ToWString(g_Config.memCardDirectory).c_str(), 0, 0, SW_SHOW);
					break;

				case ID_TOGGLE_PAUSE:
					if (globalUIState == UISTATE_PAUSEMENU) {
						// Causes hang
						//NativeMessageReceived("run", "");

						if (disasmWindow[0])
							SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
					}
					else if (Core_IsStepping()) { // It is paused, then continue to run.
						if (disasmWindow[0])
							SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						else
							Core_EnableStepping(false);
					} else {
						if (disasmWindow[0])
							SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
						else
							Core_EnableStepping(true);
					}
					noFocusPause = !noFocusPause;	// If we pause, override pause on lost focus
					break;

				case ID_EMULATION_STOP:
					if (Core_IsStepping())
						Core_EnableStepping(false);

					Core_Stop();
					NativeMessageReceived("stop", "");
					Core_WaitInactive();
					Update();
					break;

				case ID_EMULATION_RESET:
					NativeMessageReceived("reset", "");
					Core_EnableStepping(false);
					break;
				case ID_EMULATION_SWITCH_UMD:
					UmdSwitchAction();
					break;

				case ID_EMULATION_CHEATS:
					g_Config.bEnableCheats = !g_Config.bEnableCheats;
					osm.ShowOnOff(g->T("Cheats"), g_Config.bEnableCheats);
					break;

				case ID_FILE_LOADSTATEFILE:
					if (W32Util::BrowseForFileName(true, hWnd, L"Load state",0,L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0",L"ppst",fn)) {
						SetCursor(LoadCursor(0, IDC_WAIT));
						SaveState::Load(fn, SaveStateActionFinished);
					}
					break;

				case ID_FILE_SAVESTATEFILE:
					if (W32Util::BrowseForFileName(false, hWnd, L"Save state",0,L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0",L"ppst",fn)) {
						SetCursor(LoadCursor(0, IDC_WAIT));
						SaveState::Save(fn, SaveStateActionFinished);
					}
					break;

				case ID_FILE_SAVESTATE_NEXT_SLOT:
				{
					SaveState::NextSlot();
					break;
				}

				case ID_FILE_SAVESTATE_SLOT_1: g_Config.iCurrentStateSlot = 0; break;
				case ID_FILE_SAVESTATE_SLOT_2: g_Config.iCurrentStateSlot = 1; break;
				case ID_FILE_SAVESTATE_SLOT_3: g_Config.iCurrentStateSlot = 2; break;
				case ID_FILE_SAVESTATE_SLOT_4: g_Config.iCurrentStateSlot = 3; break;
				case ID_FILE_SAVESTATE_SLOT_5: g_Config.iCurrentStateSlot = 4; break;

				case ID_FILE_QUICKLOADSTATE:
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::LoadSlot(g_Config.iCurrentStateSlot, SaveStateActionFinished);
					break;

				case ID_FILE_QUICKSAVESTATE:
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::SaveSlot(g_Config.iCurrentStateSlot, SaveStateActionFinished);
					break;

				case ID_OPTIONS_LANGUAGE:
					NativeMessageReceived("language screen", "");
					break;

				case ID_OPTIONS_IGNOREWINKEY:
					g_Config.bIgnoreWindowsKey = !g_Config.bIgnoreWindowsKey;
					break;

				case ID_OPTIONS_SCREENAUTO: SetInternalResolution(RESOLUTION_AUTO); break;
				case ID_OPTIONS_SCREEN1X:   SetInternalResolution(RESOLUTION_NATIVE); break;
				case ID_OPTIONS_SCREEN2X:   SetInternalResolution(RESOLUTION_2X); break;
				case ID_OPTIONS_SCREEN3X:   SetInternalResolution(RESOLUTION_3X); break;
				case ID_OPTIONS_SCREEN4X:   SetInternalResolution(RESOLUTION_4X); break;
				case ID_OPTIONS_SCREEN5X:   SetInternalResolution(RESOLUTION_5X); break;
				case ID_OPTIONS_SCREEN6X:   SetInternalResolution(RESOLUTION_6X); break;
				case ID_OPTIONS_SCREEN7X:   SetInternalResolution(RESOLUTION_7X); break;
				case ID_OPTIONS_SCREEN8X:   SetInternalResolution(RESOLUTION_8X); break;
				case ID_OPTIONS_SCREEN9X:   SetInternalResolution(RESOLUTION_9X); break;
				case ID_OPTIONS_SCREEN10X:   SetInternalResolution(RESOLUTION_MAX); break;

				case ID_OPTIONS_WINDOW1X:   SetWindowSize(1); break;
				case ID_OPTIONS_WINDOW2X:   SetWindowSize(2); break;
				case ID_OPTIONS_WINDOW3X:   SetWindowSize(3); break;
				case ID_OPTIONS_WINDOW4X:   SetWindowSize(4); break;
					
				case ID_OPTIONS_RESOLUTIONDUMMY:
					{
						SetInternalResolution();
						break;
					}

				case ID_OPTIONS_VSYNC:
					g_Config.bVSync = !g_Config.bVSync;
					break;

				case ID_TEXTURESCALING_AUTO: setTexScalingMultiplier(TEXSCALING_AUTO); break;
				case ID_TEXTURESCALING_OFF: setTexScalingMultiplier(TEXSCALING_OFF); break;
				case ID_TEXTURESCALING_2X:  setTexScalingMultiplier(TEXSCALING_2X); break;
				case ID_TEXTURESCALING_3X:  setTexScalingMultiplier(TEXSCALING_3X); break;
				case ID_TEXTURESCALING_4X:  setTexScalingMultiplier(TEXSCALING_4X); break;
				case ID_TEXTURESCALING_5X:  setTexScalingMultiplier(TEXSCALING_MAX); break;

				case ID_TEXTURESCALING_XBRZ:            setTexScalingType(TextureScaler::XBRZ); break;
				case ID_TEXTURESCALING_HYBRID:          setTexScalingType(TextureScaler::HYBRID); break;
				case ID_TEXTURESCALING_BICUBIC:         setTexScalingType(TextureScaler::BICUBIC); break;
				case ID_TEXTURESCALING_HYBRID_BICUBIC:  setTexScalingType(TextureScaler::HYBRID_BICUBIC); break;

				case ID_TEXTURESCALING_DEPOSTERIZE:
					g_Config.bTexDeposterize = !g_Config.bTexDeposterize;
					NativeMessageReceived("gpu clear cache", "");
					break;

				case ID_OPTIONS_NONBUFFEREDRENDERING:   setRenderingMode(FB_NON_BUFFERED_MODE); break;
				case ID_OPTIONS_BUFFEREDRENDERING:      setRenderingMode(FB_BUFFERED_MODE); break;
				case ID_OPTIONS_READFBOTOMEMORYCPU:     setRenderingMode(FB_READFBOMEMORY_CPU); break;
				case ID_OPTIONS_READFBOTOMEMORYGPU:     setRenderingMode(FB_READFBOMEMORY_GPU); break;

				// Dummy option to let the buffered rendering hotkey cycle through all the options.
				case ID_OPTIONS_BUFFEREDRENDERINGDUMMY:
					setRenderingMode();
					break;

				case ID_DEBUG_SHOWDEBUGSTATISTICS:
					g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
					NativeMessageReceived("clear jit", "");
					break;

				case ID_OPTIONS_HARDWARETRANSFORM:
					g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
					osm.ShowOnOff(g->T("Hardware Transform"), g_Config.bHardwareTransform);
					break;

				case ID_OPTIONS_STRETCHDISPLAY:
					g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
					NativeMessageReceived("gpu resized", "");
					break;

				case ID_OPTIONS_FRAMESKIP_0:    setFrameSkipping(FRAMESKIP_OFF); break;
				case ID_OPTIONS_FRAMESKIP_AUTO: setFrameSkipping(FRAMESKIP_AUTO); break;
				case ID_OPTIONS_FRAMESKIP_1:    setFrameSkipping(FRAMESKIP_1); break;
				case ID_OPTIONS_FRAMESKIP_2:    setFrameSkipping(FRAMESKIP_2); break;
				case ID_OPTIONS_FRAMESKIP_3:    setFrameSkipping(FRAMESKIP_3); break;
				case ID_OPTIONS_FRAMESKIP_4:    setFrameSkipping(FRAMESKIP_4); break;
				case ID_OPTIONS_FRAMESKIP_5:    setFrameSkipping(FRAMESKIP_5); break;
				case ID_OPTIONS_FRAMESKIP_6:    setFrameSkipping(FRAMESKIP_6); break;
				case ID_OPTIONS_FRAMESKIP_7:    setFrameSkipping(FRAMESKIP_7); break;
				case ID_OPTIONS_FRAMESKIP_8:    setFrameSkipping(FRAMESKIP_MAX); break;

				case ID_OPTIONS_FRAMESKIPDUMMY:
					setFrameSkipping();
					break;

				case ID_FILE_EXIT:
					DestroyWindow(hWnd);
					break;

				case ID_DEBUG_RUNONLOAD:
					g_Config.bAutoRun = !g_Config.bAutoRun;
					break;

				case ID_DEBUG_DUMPNEXTFRAME:
					NativeMessageReceived("gpu dump next frame", "");
					break;

				case ID_DEBUG_LOADMAPFILE:
					if (W32Util::BrowseForFileName(true, hWnd, L"Load .MAP",0,L"Maps\0*.map\0All files\0*.*\0\0",L"map",fn)) {
						symbolMap.LoadSymbolMap(fn.c_str());

						if (disasmWindow[0])
							disasmWindow[0]->NotifyMapLoaded();

						if (memoryWindow[0])
							memoryWindow[0]->NotifyMapLoaded();
					}
					break;

				case ID_DEBUG_SAVEMAPFILE:
					if (W32Util::BrowseForFileName(false, hWnd, L"Save .MAP",0,L"Maps\0*.map\0All files\0*.*\0\0",L"map",fn))
						symbolMap.SaveSymbolMap(fn.c_str());
					break;

				case ID_DEBUG_RESETSYMBOLTABLE:
					symbolMap.Clear();

					for (int i=0; i<numCPUs; i++)
						if (disasmWindow[i])
							disasmWindow[i]->NotifyMapLoaded();

					for (int i=0; i<numCPUs; i++)
						if (memoryWindow[i])
							memoryWindow[i]->NotifyMapLoaded();
					break;

				case ID_DEBUG_DISASSEMBLY:
					disasmWindow[0]->Show(true);
					break;

				case ID_DEBUG_GEDEBUGGER:
					geDebuggerWindow->Show(true);
					break;

				case ID_DEBUG_MEMORYVIEW:
					memoryWindow[0]->Show(true);
					break;

				case ID_DEBUG_EXTRACTFILE:
					{
						std::string filename;
						if (!InputBox_GetString(hInst, hwndMain, L"Disc filename", filename, filename)) {
							break;
						}

						const char *lastSlash = strrchr(filename.c_str(), '/');
						if (lastSlash) {
							fn = lastSlash + 1;
						} else {
							fn = "";
						}

						PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
						if (!info.exists) {
							MessageBox(hwndMain, L"File does not exist.", L"Sorry",0);
						} else if (info.type == FILETYPE_DIRECTORY) {
							MessageBox(hwndMain, L"Cannot extract directories.", L"Sorry",0);
						} else if (W32Util::BrowseForFileName(false, hWnd, L"Save file as...", 0, L"0All files\0*.*\0\0", L"", fn)) {
							FILE *fp = fopen(fn.c_str(), "wb");
							u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ, "");
							u8 buffer[4096];
							while (pspFileSystem.ReadFile(handle, buffer, sizeof(buffer)) > 0) {
								fwrite(buffer, sizeof(buffer), 1, fp);
							}
							pspFileSystem.CloseFile(handle);
							fclose(fp);
						}
					}
					break;

				case ID_DEBUG_LOG:
					LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
					break;

				case ID_DEBUG_IGNOREILLEGALREADS:
					g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
					break;

				case ID_OPTIONS_FULLSCREEN:
					g_Config.bFullScreen = !g_Config.bFullScreen;

					if (g_Config.bFullScreen)
						_ViewFullScreen(hWnd);
					else
						_ViewNormal(hWnd);

					break;

				case ID_OPTIONS_VERTEXCACHE:
					g_Config.bVertexCache = !g_Config.bVertexCache;
					break;

				case ID_OPTIONS_SHOWFPS:
					g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter;
					break;

				case ID_OPTIONS_TEXTUREFILTERING_AUTO: setTexFiltering(AUTO); break;
				case ID_OPTIONS_NEARESTFILTERING:      setTexFiltering(NEAREST); break;
				case ID_OPTIONS_LINEARFILTERING:       setTexFiltering(LINEAR); break;
				case ID_OPTIONS_LINEARFILTERING_CG:    setTexFiltering(LINEARFMV); break;

				case ID_OPTIONS_TOPMOST:
					g_Config.bTopMost = !g_Config.bTopMost;
					W32Util::MakeTopMost(hWnd, g_Config.bTopMost);
					break;

				case ID_OPTIONS_PAUSE_FOCUS:
					g_Config.bPauseOnLostFocus = !g_Config.bPauseOnLostFocus;
					break;

				case ID_OPTIONS_CONTROLS:
					NativeMessageReceived("control mapping", "");
					break;

				case ID_OPTIONS_MORE_SETTINGS:
					NativeMessageReceived("settings", "");
					break;

				case ID_EMULATION_SOUND:
					g_Config.bEnableSound = !g_Config.bEnableSound;
					if (g_Config.bEnableSound) {
						if (PSP_IsInited() && !IsAudioInitialised())
							Audio_Init();
					}
					break;

				case ID_HELP_OPENWEBSITE:
					ShellExecute(NULL, L"open", L"http://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
					break;

				case ID_HELP_BUYGOLD:
					ShellExecute(NULL, L"open", L"http://central.ppsspp.org/buygold", NULL, NULL, SW_SHOWNORMAL);
					break;

				case ID_HELP_OPENFORUM:
					ShellExecute(NULL, L"open", L"http://forums.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
					break;

				case ID_HELP_CHINESE_FORUM:
					ShellExecute(NULL, L"open", L"http://tieba.baidu.com/f?ie=utf-8&kw=ppsspp", NULL, NULL, SW_SHOWNORMAL);
					break;

				case ID_HELP_ABOUT:
					DialogManager::EnableAll(FALSE);
					DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
					DialogManager::EnableAll(TRUE);
					break;

				case ID_DEBUG_TAKESCREENSHOT:
					g_TakeScreenshot = true;
					break;

				default:
					{
						// Handle the dynamic shader switching here.
						// The Menu ID is contained in wParam, so subtract
						// ID_SHADERS_BASE and an additional 1 off it.
						u32 index = (wParam - ID_SHADERS_BASE - 1);
						if (index >= 0 && index < availableShaders.size()) {
							g_Config.sPostShaderName = availableShaders[index];

							NativeMessageReceived("gpu resized", "");

							break;
						}

						MessageBox(hwndMain, L"Unimplemented", L"Sorry",0);
					}
					break;
				}
			}
			break;

		case WM_INPUT:
			return WindowsRawInput::Process(hWnd, wParam, lParam);

		// TODO: Could do something useful with WM_INPUT_DEVICE_CHANGE?

		case WM_VERYSLEEPY_MSG:
			switch (wParam) {
			case VERYSLEEPY_WPARAM_SUPPORTED:
				return TRUE;

			case VERYSLEEPY_WPARAM_GETADDRINFO:
				{
					VerySleepy_AddrInfo *info = (VerySleepy_AddrInfo *)lParam;
					const u8 *ptr = (const u8 *)info->addr;
					std::string name;

					if (MIPSComp::jit && MIPSComp::jit->DescribeCodePtr(ptr, name)) {
						swprintf_s(info->name, L"Jit::%S", name.c_str());
						return TRUE;
					}
					if (gpu && gpu->DescribeCodePtr(ptr, name)) {
						swprintf_s(info->name, L"GPU::%S", name.c_str());
						return TRUE;
					}
				}
				return FALSE;

			default:
				return FALSE;
			}
			break;

		case WM_DROPFILES:
			{
				if (!EmuThread_Ready())
					return DefWindowProc(hWnd, message, wParam, lParam);

				HDROP hdrop = (HDROP)wParam;
				int count = DragQueryFile(hdrop,0xFFFFFFFF,0,0);
				if (count != 1) {
					MessageBox(hwndMain,L"You can only load one file at a time",L"Error",MB_ICONINFORMATION);
				}
				else
				{
					TCHAR filename[512];
					DragQueryFile(hdrop,0,filename,512);
					TCHAR *type = filename+_tcslen(filename)-3;
					
					Update();

					NativeMessageReceived("boot", ConvertWStringToUTF8(filename).c_str());
					Core_EnableStepping(false);
				}
			}
			break;

		case WM_CLOSE:
			EmuThread_Stop();
			WindowsRawInput::Shutdown();

			return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_DESTROY:
			KillTimer(hWnd, TIMER_CURSORUPDATE);
			KillTimer(hWnd, TIMER_CURSORMOVEUPDATE);
			PostQuitMessage(0);
			break;

		case WM_USER + 1:
			disasmWindow[0]->NotifyMapLoaded();
			memoryWindow[0]->NotifyMapLoaded();

			disasmWindow[0]->UpdateDialog();

			SetForegroundWindow(hwndMain);
			break;

		case WM_USER_SAVESTATE_FINISH:
			SetCursor(LoadCursor(0, IDC_ARROW));
			break;

		case WM_USER_UPDATE_UI:
			TranslateMenus();
			Update();
			break;

		case WM_USER_UPDATE_SCREEN:
			ResizeDisplay(true, true);
			break;

		case WM_USER_WINDOW_TITLE_CHANGED:
			UpdateWindowTitle();
			break;

		case WM_USER_BROWSE_BOOT_DONE:
			BrowseAndBootDone();
			break;

		case WM_MENUSELECT:
			// Unfortunately, accelerate keys (hotkeys) shares the same enabled/disabled states
			// with corresponding menu items.
			UpdateMenus();
			WindowsRawInput::NotifyMenu();
			break;

		// Turn off the screensaver.
		// Note that if there's a screensaver password, this simple method
		// doesn't work on Vista or higher.
		case WM_SYSCOMMAND:
			{
				switch (wParam) {
				case SC_SCREENSAVE:  
					return 0;
				case SC_MONITORPOWER:
					return 0;      
				}
				return DefWindowProc(hWnd, message, wParam, lParam);
			}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	void UpdateMenus() {
		HMENU menu = GetMenu(GetHWND());
#define CHECKITEM(item,value) 	CheckMenuItem(menu,item,MF_BYCOMMAND | ((value) ? MF_CHECKED : MF_UNCHECKED));
		CHECKITEM(ID_DEBUG_IGNOREILLEGALREADS, g_Config.bIgnoreBadMemAccess);
		CHECKITEM(ID_DEBUG_SHOWDEBUGSTATISTICS, g_Config.bShowDebugStats);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_OPTIONS_STRETCHDISPLAY, g_Config.bStretchToDisplay);
		CHECKITEM(ID_DEBUG_RUNONLOAD, g_Config.bAutoRun);
		CHECKITEM(ID_OPTIONS_VERTEXCACHE, g_Config.bVertexCache);
		CHECKITEM(ID_OPTIONS_SHOWFPS, g_Config.iShowFPSCounter);
		CHECKITEM(ID_OPTIONS_FRAMESKIP, g_Config.iFrameSkip != 0);
		CHECKITEM(ID_OPTIONS_VSYNC, g_Config.bVSync);
		CHECKITEM(ID_OPTIONS_TOPMOST, g_Config.bTopMost);
		CHECKITEM(ID_OPTIONS_PAUSE_FOCUS, g_Config.bPauseOnLostFocus);
		CHECKITEM(ID_EMULATION_SOUND, g_Config.bEnableSound);
		CHECKITEM(ID_TEXTURESCALING_DEPOSTERIZE, g_Config.bTexDeposterize);
		CHECKITEM(ID_EMULATION_CHEATS, g_Config.bEnableCheats);
		CHECKITEM(ID_OPTIONS_IGNOREWINKEY, g_Config.bIgnoreWindowsKey);

		static const int zoomitems[11] = {
			ID_OPTIONS_SCREENAUTO,
			ID_OPTIONS_SCREEN1X,
			ID_OPTIONS_SCREEN2X,
			ID_OPTIONS_SCREEN3X,
			ID_OPTIONS_SCREEN4X,
			ID_OPTIONS_SCREEN5X,
			ID_OPTIONS_SCREEN6X,
			ID_OPTIONS_SCREEN7X,
			ID_OPTIONS_SCREEN8X,
			ID_OPTIONS_SCREEN9X,
			ID_OPTIONS_SCREEN10X,
		};
		if (g_Config.iInternalResolution < RESOLUTION_AUTO)
			g_Config.iInternalResolution = RESOLUTION_AUTO;

		else if (g_Config.iInternalResolution > RESOLUTION_MAX)
			g_Config.iInternalResolution = RESOLUTION_MAX;

		for (int i = 0; i < ARRAY_SIZE(zoomitems); i++) {
			CheckMenuItem(menu, zoomitems[i], MF_BYCOMMAND | ((i == g_Config.iInternalResolution) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int windowSizeItems[4] = {
			ID_OPTIONS_WINDOW1X,
			ID_OPTIONS_WINDOW2X,
			ID_OPTIONS_WINDOW3X,
			ID_OPTIONS_WINDOW4X,
		};

		RECT rc;
		GetClientRect(GetHWND(), &rc);

		for (int i = 0; i < ARRAY_SIZE(windowSizeItems); i++) {
			bool check = (i + 1) * 480 == rc.right - rc.left || (i + 1) * 272 == rc.bottom - rc.top;
			CheckMenuItem(menu, windowSizeItems[i], MF_BYCOMMAND | (check ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingitems[] = {
			ID_TEXTURESCALING_AUTO,
			ID_TEXTURESCALING_OFF,
			ID_TEXTURESCALING_2X,
			ID_TEXTURESCALING_3X,
			ID_TEXTURESCALING_4X,
			ID_TEXTURESCALING_5X,
		};
		if(g_Config.iTexScalingLevel < TEXSCALING_AUTO)
			g_Config.iTexScalingLevel = TEXSCALING_AUTO;

		else if(g_Config.iTexScalingLevel > TEXSCALING_MAX)
			g_Config.iTexScalingLevel = TEXSCALING_MAX;

		for (int i = 0; i < ARRAY_SIZE(texscalingitems); i++) {
			CheckMenuItem(menu, texscalingitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingLevel) ? MF_CHECKED : MF_UNCHECKED));
		}

		if (!gl_extensions.OES_texture_npot) {
			EnableMenuItem(menu, ID_TEXTURESCALING_3X, MF_GRAYED);
			EnableMenuItem(menu, ID_TEXTURESCALING_5X, MF_GRAYED);
		} else {
			EnableMenuItem(menu, ID_TEXTURESCALING_3X, MF_ENABLED);
			EnableMenuItem(menu, ID_TEXTURESCALING_5X, MF_ENABLED);
		}

		static const int texscalingtypeitems[] = {
			ID_TEXTURESCALING_XBRZ,
			ID_TEXTURESCALING_HYBRID,
			ID_TEXTURESCALING_BICUBIC,
			ID_TEXTURESCALING_HYBRID_BICUBIC,
		};
		if(g_Config.iTexScalingType < TextureScaler::XBRZ)
			g_Config.iTexScalingType = TextureScaler::XBRZ;

		else if(g_Config.iTexScalingType > TextureScaler::HYBRID_BICUBIC)
			g_Config.iTexScalingType = TextureScaler::HYBRID_BICUBIC;

		for (int i = 0; i < ARRAY_SIZE(texscalingtypeitems); i++) {
			CheckMenuItem(menu, texscalingtypeitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingType) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texfilteringitems[] = {
			ID_OPTIONS_TEXTUREFILTERING_AUTO,
			ID_OPTIONS_NEARESTFILTERING,
			ID_OPTIONS_LINEARFILTERING,
			ID_OPTIONS_LINEARFILTERING_CG,
		};
		if(g_Config.iTexFiltering < AUTO)
			g_Config.iTexFiltering = AUTO;

		else if(g_Config.iTexFiltering > LINEARFMV)
			g_Config.iTexFiltering = LINEARFMV;

		for (int i = 0; i < ARRAY_SIZE(texfilteringitems); i++) {
			CheckMenuItem(menu, texfilteringitems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iTexFiltering ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int renderingmode[] = {
			ID_OPTIONS_NONBUFFEREDRENDERING,
			ID_OPTIONS_BUFFEREDRENDERING,
			ID_OPTIONS_READFBOTOMEMORYCPU,
			ID_OPTIONS_READFBOTOMEMORYGPU,
		};
		if(g_Config.iRenderingMode < FB_NON_BUFFERED_MODE)
			g_Config.iRenderingMode = FB_NON_BUFFERED_MODE;

		else if(g_Config.iRenderingMode > FB_READFBOMEMORY_GPU)
			g_Config.iRenderingMode = FB_READFBOMEMORY_GPU;

		for (int i = 0; i < ARRAY_SIZE(renderingmode); i++) {
			CheckMenuItem(menu, renderingmode[i], MF_BYCOMMAND | (( i == g_Config.iRenderingMode )? MF_CHECKED : MF_UNCHECKED));
		}

		static const int frameskipping[] = {
			ID_OPTIONS_FRAMESKIP_0,
			ID_OPTIONS_FRAMESKIP_AUTO,
			ID_OPTIONS_FRAMESKIP_1,
			ID_OPTIONS_FRAMESKIP_2,
			ID_OPTIONS_FRAMESKIP_3,
			ID_OPTIONS_FRAMESKIP_4,
			ID_OPTIONS_FRAMESKIP_5,
			ID_OPTIONS_FRAMESKIP_6,
			ID_OPTIONS_FRAMESKIP_7,
			ID_OPTIONS_FRAMESKIP_8,
		};
		if(g_Config.iFrameSkip < FRAMESKIP_OFF)
			g_Config.iFrameSkip = FRAMESKIP_OFF;

		else if(g_Config.iFrameSkip > FRAMESKIP_MAX)
			g_Config.iFrameSkip = FRAMESKIP_MAX;

		for (int i = 0; i < ARRAY_SIZE(frameskipping); i++) {
			CheckMenuItem(menu, frameskipping[i], MF_BYCOMMAND | (( i == g_Config.iFrameSkip )? MF_CHECKED : MF_UNCHECKED));
		}

		static const int savestateSlot[] = {
			ID_FILE_SAVESTATE_SLOT_1,
			ID_FILE_SAVESTATE_SLOT_2,
			ID_FILE_SAVESTATE_SLOT_3,
			ID_FILE_SAVESTATE_SLOT_4,
			ID_FILE_SAVESTATE_SLOT_5,
		};

		if(g_Config.iCurrentStateSlot < 0)
			g_Config.iCurrentStateSlot = 0;

		else if(g_Config.iCurrentStateSlot >= SaveState::SAVESTATESLOTS)
			g_Config.iCurrentStateSlot = SaveState::SAVESTATESLOTS - 1;

		for (int i = 0; i < ARRAY_SIZE(savestateSlot); i++) {
			CheckMenuItem(menu, savestateSlot[i], MF_BYCOMMAND | (( i == g_Config.iCurrentStateSlot )? MF_CHECKED : MF_UNCHECKED));
		}

		UpdateDynamicMenuCheckmarks();
		UpdateCommands();
	}
	
	void UpdateCommands() {
		static GlobalUIState lastGlobalUIState = UISTATE_PAUSEMENU;
		static CoreState lastCoreState = CORE_ERROR;

		if (lastGlobalUIState == globalUIState && lastCoreState == coreState)
			return;

		lastCoreState = coreState;
		lastGlobalUIState = globalUIState;

		HMENU menu = GetMenu(GetHWND());

		bool isPaused = Core_IsStepping() && globalUIState == UISTATE_INGAME;
		TranslateMenuItem(ID_TOGGLE_PAUSE, L"\tF8", isPaused ? "Run" : "Pause");

		SetIngameMenuItemStates(globalUIState);
		EnableMenuItem(menu, ID_DEBUG_LOG, !g_Config.bEnableLogging);
	}

	// Message handler for about box.
	LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
		case WM_INITDIALOG:
			{
				W32Util::CenterWindow(hDlg);
				HWND versionBox = GetDlgItem(hDlg, IDC_VERSION);
				char temp[256];
				sprintf(temp, "PPSSPP %s", PPSSPP_GIT_VERSION);
				SetWindowTextA(versionBox, temp);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	void Update() {
		InvalidateRect(hwndDisplay,0,0);
		UpdateWindow(hwndDisplay);
		SendMessage(hwndMain,WM_SIZE,0,0);
	}

	void Redraw() {
		InvalidateRect(hwndDisplay,0,0);
	}

	void SaveStateActionFinished(bool result, void *userdata) {
		PostMessage(hwndMain, WM_USER_SAVESTATE_FINISH, 0, 0);
	}

	HINSTANCE GetHInstance() {
		return hInst;
	}

	void ToggleDebugConsoleVisibility() {
		if (!g_Config.bEnableLogging) {
			LogManager::GetInstance()->GetConsoleListener()->Show(false);
			EnableMenuItem(menu, ID_DEBUG_LOG, MF_GRAYED);
		}
		else {
			LogManager::GetInstance()->GetConsoleListener()->Show(true);
			EnableMenuItem(menu, ID_DEBUG_LOG, MF_ENABLED);
		}
	}
}
