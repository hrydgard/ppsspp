#include "Common/CommonWindows.h"
#include "Windows/InputBox.h"
#include "Windows/resource.h"

static TCHAR textBoxContents[256];
static TCHAR out[256];
static TCHAR windowTitle[256];
static bool defaultSelected;

static INT_PTR CALLBACK InputBoxFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetWindowText(GetDlgItem(hDlg,IDC_INPUTBOX),textBoxContents);
		SetWindowText(hDlg, windowTitle);
		if (defaultSelected == false) PostMessage(GetDlgItem(hDlg,IDC_INPUTBOX),EM_SETSEL,-1,-1);
		return TRUE;
	case WM_COMMAND:
		switch (wParam)
		{
		case IDOK:
			GetWindowText(GetDlgItem(hDlg,IDC_INPUTBOX),out,255);
			EndDialog(hDlg,IDOK);
			return TRUE;
		case IDCANCEL:
			EndDialog(hDlg,IDCANCEL);
			return TRUE;
		}
	default:
		return FALSE;
	}
}

template <bool hex> 
void InputBoxFunc()
{

}

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, TCHAR *defaultvalue, TCHAR *outvalue, bool selected)
{
	defaultSelected = selected;
	if (defaultvalue && strlen(defaultvalue)<255)
		strcpy(textBoxContents,defaultvalue);
	else
		strcpy(textBoxContents,"");

	if (title != NULL)
		strcpy(windowTitle,title);
	else
		strcpy(windowTitle,"");

	if (IDOK==DialogBox(hInst,(LPCSTR)IDD_INPUTBOX,hParent,InputBoxFunc))
	{
		strcpy(outvalue,out);
		return true;
	}
	else 
		return false;
}

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, const TCHAR *defaultvalue, TCHAR *outvalue, size_t outlength)
{
	const char *defaultTitle = "Input value";
	defaultSelected = true;

	if (defaultvalue && strlen(defaultvalue)<255)
		strcpy(textBoxContents,defaultvalue);
	else
		strcpy(textBoxContents,"");


	if(title && strlen(title) <= 0)
		strcpy(windowTitle, defaultTitle);
	else if(title && strlen(title) < 255)
		strcpy(windowTitle, title);
	else
		strcpy(windowTitle, defaultTitle);

	if (IDOK==DialogBox(hInst,(LPCSTR)IDD_INPUTBOX,hParent,InputBoxFunc))
	{
		strncpy(outvalue, out, outlength);
		return true;
	}
	else 
		return false;
}

bool InputBox_GetHex(HINSTANCE hInst, HWND hParent, TCHAR *title, u32 defaultvalue, u32 &outvalue)
{
	sprintf(textBoxContents,"%08x",defaultvalue);
	INT_PTR value = DialogBox(hInst,(LPCSTR)IDD_INPUTBOX,hParent,InputBoxFunc);

	if (value == IDOK)
	{
		sscanf(out,"%08x",&outvalue);
		return true;
	}
	else 
	{
		out[0]=0;
		return false;
	}
}