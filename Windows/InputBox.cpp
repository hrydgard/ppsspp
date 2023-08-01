#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/CommonWindows.h"
#include "Windows/InputBox.h"
#include "Windows/resource.h"
#include "Windows/W32Util/Misc.h"
#include "Common/Data/Encoding/Utf8.h"

static std::wstring textBoxContents;
static std::wstring passwordContents;
static std::wstring out;
static std::wstring windowTitle;
static bool defaultSelected;
static std::string g_userName;
static std::string g_passWord;

static INT_PTR CALLBACK InputBoxFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), textBoxContents.c_str());
		SetWindowText(hDlg, windowTitle.c_str());
		if (defaultSelected == false)
			PostMessage(GetDlgItem(hDlg,IDC_INPUTBOX),EM_SETSEL,-1,-1);
		W32Util::CenterWindow(hDlg);
		return TRUE;
	case WM_COMMAND:
		switch (wParam) {
		case IDOK:
			{
				wchar_t temp[512];
				GetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), temp, ARRAY_SIZE(temp) - 1);
				out = temp;
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

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultValue, std::string &outvalue, bool selected)
{
	defaultSelected = selected;
	if (defaultValue.size() < 255)
		textBoxContents = ConvertUTF8ToWString(defaultValue);
	else
		textBoxContents.clear();

	if (title != NULL)
		windowTitle = title;
	else
		windowTitle.clear();

	if (IDOK == DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc)) {
		outvalue = ConvertWStringToUTF8(out);
		return true;
	}
	else 
		return false;
}

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::string &defaultValue, std::string &outvalue)
{
	const wchar_t *defaultTitle = L"Input value";
	defaultSelected = true;

	textBoxContents = ConvertUTF8ToWString(defaultValue);

	if (title && wcslen(title) <= 0)
		windowTitle = defaultTitle;
	else if (title && wcslen(title) < 255)
		windowTitle = title;
	else
		windowTitle = defaultTitle;

	if (IDOK == DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc)) {
		outvalue = ConvertWStringToUTF8(out);
		return true;
	}
	else 
		return false;
}

bool InputBox_GetWString(HINSTANCE hInst, HWND hParent, const wchar_t *title, const std::wstring &defaultValue, std::wstring &outvalue)
{
	const wchar_t *defaultTitle = L"Input value";
	defaultSelected = true;

	textBoxContents = defaultValue;

	if (title && wcslen(title) <= 0)
		windowTitle = defaultTitle;
	else if (title && wcslen(title) < 255)
		windowTitle = title;
	else
		windowTitle = defaultTitle;

	if (IDOK == DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc)) {
		outvalue = out;
		return true;
	}
	else 
		return false;
}

bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, const wchar_t* title, u32 defaultvalue, u32& outvalue)
{
	const wchar_t *defaultTitle = L"Input value";
	wchar_t temp[256];
	wsprintf(temp, L"%08x", defaultvalue);
	textBoxContents = temp;

	if (title && wcslen(title) <= 0)
		windowTitle = defaultTitle;
	else if (title && wcslen(title) < 255)
		windowTitle = title;
	else
		windowTitle = defaultTitle;

	INT_PTR value = DialogBox(hInst, (LPCWSTR)IDD_INPUTBOX, hParent, InputBoxFunc);

	if (value == IDOK)
	{
		if (swscanf(out.c_str(), L"0x%08x", &outvalue) == 1)
			return true;
		if (swscanf(out.c_str(), L"%08x", &outvalue) == 1)
			return true;
		return false;
	}
	else
	{
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
		SetWindowText(hDlg, windowTitle.c_str());
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
			GetWindowText(GetDlgItem(hDlg, IDC_INPUTBOX), temp, 255);
			g_userName = ConvertWStringToUTF8(temp);
			GetWindowText(GetDlgItem(hDlg, IDC_PASSWORDBOX), temp, 255);
			g_passWord = ConvertWStringToUTF8(temp);
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
	windowTitle = title;
	INT_PTR value = DialogBox(hInst, (LPCWSTR)IDD_USERPASSWORDBOX, hParent, UserPasswordBoxFunc);

	if (value == IDOK) {
		*username = g_userName;
		*password = g_passWord;
		return true;
	} else {
		return false;
	}
}
