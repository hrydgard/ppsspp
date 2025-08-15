#include "Common/CommonWindows.h"

#include <Uxtheme.h>
#include <vsstyle.h>

#include "Windows/W32Util/UAHMenuBar.h"
#include "Windows/W32Util/DarkMode.h"

static HTHEME g_menuTheme = nullptr;

// processes messages related to UAH / custom menubar drawing.
// return true if handled, false to continue with normal processing in your wndproc
bool UAHDarkModeWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT *lr)
{
	if (!IsDarkModeEnabled() && message != WM_THEMECHANGED) {
		return false;
	}

	switch (message)
	{
	case WM_UAHDRAWMENU:
	{
		UAHMENU *pUDM = (UAHMENU *)lParam;
		RECT rc = { 0 };

		// get the menubar rect
		{
			MENUBARINFO mbi = { sizeof(mbi) };
			GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);

			RECT rcWindow;
			GetWindowRect(hWnd, &rcWindow);

			// the rcBar is offset by the window rect
			rc = mbi.rcBar;
			OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

			rc.top -= 1;
		}

		if (!g_menuTheme) {
			g_menuTheme = OpenThemeData(hWnd, L"Menu");
		}

		DrawThemeBackground(g_menuTheme, pUDM->hdc, MENU_POPUPITEM, MPI_NORMAL, &rc, nullptr);
		return true;
	}
	case WM_UAHDRAWMENUITEM:
	{
		UAHDRAWMENUITEM *pUDMI = (UAHDRAWMENUITEM *)lParam;

		// get the menu item string
		wchar_t menuString[256] = { 0 };
		MENUITEMINFO mii = { sizeof(mii), MIIM_STRING };
		{
			mii.dwTypeData = menuString;
			mii.cch = (sizeof(menuString) / 2) - 1;

			GetMenuItemInfo(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);
		}

		// get the item state for drawing

		DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;

		int iTextStateID = 0;
		int iBackgroundStateID = 0;
		{
			if ((pUDMI->dis.itemState & ODS_INACTIVE) | (pUDMI->dis.itemState & ODS_DEFAULT)) {
				// normal display
				iTextStateID = MPI_NORMAL;
				iBackgroundStateID = MPI_NORMAL;
			}
			if (pUDMI->dis.itemState & ODS_HOTLIGHT) {
				// hot tracking
				iTextStateID = MPI_HOT;
				iBackgroundStateID = MPI_HOT;
			}
			if (pUDMI->dis.itemState & ODS_SELECTED) {
				// clicked -- MENU_POPUPITEM has no state for this, though MENU_BARITEM does
				iTextStateID = MPI_HOT;
				iBackgroundStateID = MPI_HOT;
			}
			if ((pUDMI->dis.itemState & ODS_GRAYED) || (pUDMI->dis.itemState & ODS_DISABLED)) {
				// disabled / grey text
				iTextStateID = MPI_DISABLED;
				iBackgroundStateID = MPI_DISABLED;
			}
			if (pUDMI->dis.itemState & ODS_NOACCEL) {
				dwFlags |= DT_HIDEPREFIX;
			}
		}

		if (!g_menuTheme) {
			g_menuTheme = OpenThemeData(hWnd, L"Menu");
		}

		DrawThemeBackground(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iBackgroundStateID, &pUDMI->dis.rcItem, nullptr);
		DrawThemeText(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iTextStateID, menuString, mii.cch, dwFlags, 0, &pUDMI->dis.rcItem);

		return true;
	}
	case WM_THEMECHANGED:
	{
		if (g_menuTheme) {
			CloseThemeData(g_menuTheme);
			g_menuTheme = nullptr;
		}
		// continue processing in main wndproc
		return false;
	}
	default:
		return false;
	}
}
