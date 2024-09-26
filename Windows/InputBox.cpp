#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/CommonWindows.h"
#include "Windows/InputBox.h"
#include "Windows/resource.h"
#include "Windows/W32Util/Misc.h"
#include "Common/Data/Encoding/Utf8.h"

struct DialogBoxParams {
	std::wstring textBoxContents;
	std::wstring passwordContents;
	std::wstring out;
	std::wstring windowTitle;
	bool defaultSelected;
	bool passwordMasking;
	std::string userName;
	std::string passWord;
};

static DialogBoxParams g_params;

static INT_PTR CALLBACK InputBoxFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
	{
		HWND hwndTextBox = GetDlgItem(hDlg, IDC_INPUTBOX);
		SetWindowText(hwndTextBox, g_params.textBoxContents.c_str());
		SetWindowText(hDlg, g_params.windowTitle.c_str());
		if (!g_params.defaultSelected) {
			PostMessage(hwndTextBox, EM_SETSEL, -1, -1);
		}
		if (g_params.passwordMasking) {
			LONG_PTR style = GetWindowLongPtr(hwndTextBox, GWL_STYLE);
			SetWindowLongPtr(hwndTextBox, GWL_STYLE, style | ES_PASSWORD);
			SendMessage(hwndTextBox, EM_SETPASSWORDCHAR, (WPARAM)'*', 0);
		}
		W32Util::CenterWindow(hDlg);
		return TRUE;
	}
	case WM_COMMAND:
		switch (wParam) {
		case IDOK:
			{
				wchar_t temp[512];
				GetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), temp, ARRAY_SIZE(temp));
				g_params.out = temp;
				EndDialog(hDlg, IDOK);
				return TRUE;
			}
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		default:
			return FALSE;
		}
	default:
		return FALSE;
	}
}

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultValue, std::string &outvalue, InputBoxFlags flags) {
	const wchar_t *defaultTitle = L"Input value";

	g_params.defaultSelected = flags & InputBoxFlags::Selected;
	g_params.passwordMasking = flags & InputBoxFlags::PasswordMasking;
	if (defaultValue.size() < 255) {
		g_params.textBoxContents = ConvertUTF8ToWString(defaultValue);
	} else {
		g_params.textBoxContents.clear();
	}

	if (title && wcslen(title) <= 0) {
		g_params.windowTitle = defaultTitle;
	} else if (title && wcslen(title) < 255) {
		g_params.windowTitle = title;
	} else {
		g_params.windowTitle = defaultTitle;
	}

	if (IDOK == DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc)) {
		outvalue = ConvertWStringToUTF8(g_params.out);
		return true;
	} else {
		return false;
	}
}

bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, const wchar_t* title, u32 defaultvalue, u32 &outvalue) {
	const wchar_t *defaultTitle = L"Input value";
	wchar_t temp[256];
	wsprintf(temp, L"%08x", defaultvalue);
	g_params.textBoxContents = temp;

	if (title && wcslen(title) <= 0)
		g_params.windowTitle = defaultTitle;
	else if (title && wcslen(title) < 255)
		g_params.windowTitle = title;
	else
		g_params.windowTitle = defaultTitle;

	INT_PTR value = DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc);

	if (value == IDOK) {
		if (swscanf(g_params.out.c_str(), L"0x%08x", &outvalue) == 1)
			return true;
		if (swscanf(g_params.out.c_str(), L"%08x", &outvalue) == 1)
			return true;
		return false;
	} else {
		outvalue = 0;
		return false;
	}
}

static INT_PTR CALLBACK UserPasswordBoxFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), L"");
		SetWindowText(GetDlgItem(hDlg, IDC_PASSWORDBOX), L"");
		SetWindowText(hDlg, g_params.windowTitle.c_str());
		PostMessage(GetDlgItem(hDlg, IDC_INPUTBOX), EM_SETSEL, -1, -1);
		PostMessage(GetDlgItem(hDlg, IDC_PASSWORDBOX), EM_SETSEL, -1, -1);
		W32Util::CenterWindow(hDlg);
		return TRUE;
	case WM_COMMAND:
		switch (wParam)
		{
		case IDOK:
		{
			wchar_t temp[256];
			GetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), temp, sizeof(temp));
			g_params.userName = ConvertWStringToUTF8(temp);
			GetWindowText(GetDlgItem(hDlg, IDC_PASSWORDBOX), temp, sizeof(temp));
			g_params.passWord = ConvertWStringToUTF8(temp);
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		default:
			return FALSE;
		}
	default:
		return FALSE;
	}
}

bool UserPasswordBox_GetStrings(HINSTANCE hInst, HWND hParent, const wchar_t *title, std::string *username, std::string *password) {
	g_params.windowTitle = title;
	INT_PTR value = DialogBox(hInst, (LPCWSTR)IDD_USERPASSWORDBOX, hParent, UserPasswordBoxFunc);

	if (value == IDOK) {
		*username = g_params.userName;
		*password = g_params.passWord;
		return true;
	} else {
		return false;
	}
}
