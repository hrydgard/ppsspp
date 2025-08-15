#pragma once

#include "Common/CommonWindows.h"
#include <commctrl.h>
#include <Uxtheme.h>
#include <WindowsX.h>
#include <Vssym32.h>

#include "IatHook.h"

enum IMMERSIVE_HC_CACHE_MODE
{
	IHCM_USE_CACHED_VALUE,
	IHCM_REFRESH
};

// 1903 18362
enum PreferredAppMode
{
	Default,
	AllowDark,
	ForceDark,
	ForceLight,
	Max
};

enum WINDOWCOMPOSITIONATTRIB
{
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA
{
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
};

using fnRtlGetNtVersionNumbers = void (WINAPI *)(LPDWORD major, LPDWORD minor, LPDWORD build);
using fnSetWindowCompositionAttribute = BOOL (WINAPI *)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);
// 1809 17763
using fnShouldAppsUseDarkMode = bool (WINAPI *)(); // ordinal 132
using fnAllowDarkModeForWindow = bool (WINAPI *)(HWND hWnd, bool allow); // ordinal 133
using fnAllowDarkModeForApp = bool (WINAPI *)(bool allow); // ordinal 135, in 1809
using fnFlushMenuThemes = void (WINAPI *)(); // ordinal 136
using fnRefreshImmersiveColorPolicyState = void (WINAPI *)(); // ordinal 104
using fnIsDarkModeAllowedForWindow = bool (WINAPI *)(HWND hWnd); // ordinal 137
using fnGetIsImmersiveColorUsingHighContrast = bool (WINAPI *)(IMMERSIVE_HC_CACHE_MODE mode); // ordinal 106
using fnOpenNcThemeData = HTHEME(WINAPI *)(HWND hWnd, LPCWSTR pszClassList); // ordinal 49
// 1903 18362
using fnShouldSystemUseDarkMode = bool (WINAPI *)(); // ordinal 138
using fnSetPreferredAppMode = PreferredAppMode (WINAPI *)(PreferredAppMode appMode); // ordinal 135, in 1903
using fnIsDarkModeAllowedForApp = bool (WINAPI *)(); // ordinal 139
using fnSetWindowTheme = void (WINAPI*)(HWND, LPCWSTR, LPCWSTR); 
//---------------------------------------------------------------------------

extern fnSetWindowCompositionAttribute _SetWindowCompositionAttribute;
extern fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode;
extern fnAllowDarkModeForWindow _AllowDarkModeForWindow;
extern fnAllowDarkModeForApp _AllowDarkModeForApp;
extern fnFlushMenuThemes _FlushMenuThemes;
extern fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState;
extern fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow;
extern fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast;
extern fnOpenNcThemeData _OpenNcThemeData;
// 1903 18362
extern fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode;
extern fnSetPreferredAppMode _SetPreferredAppMode;
extern fnSetWindowTheme _SetWindowTheme;

extern bool g_darkModeSupported;
extern bool g_darkModeEnabled;

void InitDarkMode();
bool AllowDarkModeForWindow(HWND hWnd, bool allow);
void RefreshTitleBarThemeColor(HWND hWnd);
bool IsColorSchemeChangeMessage(LPARAM lParam);
bool IsDarkModeEnabled();

void DarkModeInitDialog(HWND hDlg);
LRESULT DarkModeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
