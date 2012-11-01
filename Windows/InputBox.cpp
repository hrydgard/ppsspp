#include <windows.h>
#include "InputBox.h"
#include "Resource.h"

static TCHAR textBoxContents[256];
static TCHAR out[256];

static INT_PTR CALLBACK InputBoxFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetWindowText(GetDlgItem(hDlg,IDC_INPUTBOX),textBoxContents);
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

bool InputBox_GetString(HINSTANCE hInst, HWND hParent, TCHAR *title, TCHAR *defaultvalue, TCHAR *outvalue)
{
	if (defaultvalue && strlen(defaultvalue)<255)
		strcpy(textBoxContents,defaultvalue);
	else
		strcpy(textBoxContents,"");

	if (IDOK==DialogBox(hInst,(LPCSTR)IDD_INPUTBOX,hParent,InputBoxFunc))
	{
		strcpy(outvalue,out);
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