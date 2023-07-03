
#include "ScanRemoveWindow.h"
#include "../resource.h"



bool ScanRemoveWindow::GetCheckState(HWND hwnd, int dlgItem) {
	return SendMessage(GetDlgItem(hwnd, dlgItem), BM_GETCHECK, 0, 0) != 0;
}

bool ScanRemoveWindow::fetchDialogData(HWND hwnd)
{
	char str[256], errorMessage[512];
	PostfixExpression exp;

	scan = GetCheckState(hwnd, IDC_SCANREMOVE_SCAN);

	// Parse the address
	GetWindowTextA(GetDlgItem(hwnd, IDC_SCANREMOVE_ADDRESS), str, 256);

	if (cpu->initExpression(str, exp) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}
	if (cpu->parseExpression(exp, address) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	// Parse the size
	GetWindowTextA(GetDlgItem(hwnd, IDC_SCANREMOVE_SIZE), str, 256);

	if (cpu->initExpression(str, exp) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}
	if (cpu->parseExpression(exp, size) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	return true;
}

INT_PTR CALLBACK ScanRemoveWindow::StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	ScanRemoveWindow *thiz;
	if (iMsg == WM_INITDIALOG) {
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lParam);
		thiz = (ScanRemoveWindow *)lParam;
	}
	else {
		thiz = (ScanRemoveWindow *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}

	if (!thiz)
		return FALSE;
	return thiz->DlgFunc(hWnd, iMsg, wParam, lParam);
}

INT_PTR ScanRemoveWindow::DlgFunc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char str[128];

	switch (iMsg)
	{
	case WM_INITDIALOG:

		// Set the radiobutton values
		SendMessage(GetDlgItem(hwnd, IDC_SCANREMOVE_SCAN), BM_SETCHECK, scan ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_SCANREMOVE_REMOVE), BM_SETCHECK, scan ? BST_UNCHECKED : BST_CHECKED, 0);

		// Set the text in the textboxes
		if (address != -1) {
			snprintf(str, sizeof(str), "0x%08X", address);
			SetWindowTextA(GetDlgItem(hwnd, IDC_SCANREMOVE_ADDRESS), str);
		}
		snprintf(str, sizeof(str), "0x%08X", size);
		SetWindowTextA(GetDlgItem(hwnd, IDC_SCANREMOVE_SIZE), str);

		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_SCANREMOVE_SCAN:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				scan = true;
				break;
			}
			break;
		case IDC_SCANREMOVE_REMOVE:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				scan = false;
				break;
			}
			break;
		case IDC_SCANREMOVE_OK:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				if (fetchDialogData(hwnd)) {
					EndDialog(hwnd, true);
				}
				break;
			};
			break;
		case IDC_SCANREMOVE_CANCEL:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				EndDialog(hwnd, false);
				break;
			};
			break;
		case IDOK:
			if (fetchDialogData(hwnd)) {
				EndDialog(hwnd, true);
			}
			break;
		case IDCANCEL:
			EndDialog(hwnd, false);
			break;
		}
	}

	return FALSE;
}

bool ScanRemoveWindow::exec() {
	return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_BREAKPOINT), parentHwnd, StaticDlgFunc, (LPARAM)this) != 0;
}
