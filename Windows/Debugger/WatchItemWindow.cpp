// Copyright (c) 2023- PPSSPP Project.

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

#include "Common/Common.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Windows/Debugger/WatchItemWindow.h"
#include "Windows/resource.h"

INT_PTR CALLBACK WatchItemWindow::StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	WatchItemWindow *thiz;
	if (iMsg == WM_INITDIALOG) {
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lParam);
		thiz = (WatchItemWindow *)lParam;
	} else {
		thiz = (WatchItemWindow *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}

	if (!thiz)
		return FALSE;
	return thiz->DlgFunc(hWnd, iMsg, wParam, lParam);
}

INT_PTR WatchItemWindow::DlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	switch (iMsg) {
	case WM_INITDIALOG:
		SetWindowTextW(GetDlgItem(hWnd, IDC_BREAKPOINT_ADDRESS), ConvertUTF8ToWString(name_).c_str());
		SetWindowTextW(GetDlgItem(hWnd, IDC_BREAKPOINT_CONDITION), ConvertUTF8ToWString(expression_).c_str());

		// We only need to set one state on dialog init.
		if (format_ == WatchFormat::HEX)
			SendMessage(GetDlgItem(hWnd, IDC_DISASM_FMT_HEX), BM_SETCHECK, BST_CHECKED, 0);
		else if (format_ == WatchFormat::INT)
			SendMessage(GetDlgItem(hWnd, IDC_DISASM_FMT_INT), BM_SETCHECK, BST_CHECKED, 0);
		else if (format_ == WatchFormat::FLOAT)
			SendMessage(GetDlgItem(hWnd, IDC_DISASM_FMT_FLOAT), BM_SETCHECK, BST_CHECKED, 0);
		else if (format_ == WatchFormat::STR)
			SendMessage(GetDlgItem(hWnd, IDC_DISASM_FMT_STR), BM_SETCHECK, BST_CHECKED, 0);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BREAKPOINT_OK:
			switch (HIWORD(wParam)) {
			case BN_CLICKED:
				if (FetchDialogData(hWnd)) {
					EndDialog(hWnd, true);
					return TRUE;
				}
				break;
			};
			break;
		case IDC_BREAKPOINT_CANCEL:
			switch (HIWORD(wParam)) {
			case BN_CLICKED:
				EndDialog(hWnd, false);
				return TRUE;
			};
			break;
		case IDOK:
			if (FetchDialogData(hWnd)) {
				EndDialog(hWnd, true);
				return TRUE;
			}
			break;
		case IDCANCEL:
			EndDialog(hWnd, false);
			return TRUE;
		}
		break;

	default:
		break;
	}

	return FALSE;
}

bool WatchItemWindow::Exec() {
	return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_CPUWATCH), parentHwnd_, StaticDlgFunc, (LPARAM)this) != 0;
}

static bool IsControlChecked(HWND hWnd, int id) {
	return SendMessage(GetDlgItem(hWnd, id), BM_GETCHECK, 0, 0) != 0;
}

bool WatchItemWindow::FetchDialogData(HWND hwnd) {
	wchar_t textValue[512];

	GetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_ADDRESS), textValue, ARRAY_SIZE(textValue));
	name_ = ConvertWStringToUTF8(textValue);

	GetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_CONDITION), textValue, ARRAY_SIZE(textValue));
	expression_ = ConvertWStringToUTF8(textValue);
	PostfixExpression compiled;
	if (!cpu_->initExpression(expression_.c_str(), compiled)) {
		char errorMessage[512];
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", expression_.c_str(), getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	if (IsControlChecked(hwnd, IDC_DISASM_FMT_HEX))
		format_ = WatchFormat::HEX;
	else if (IsControlChecked(hwnd, IDC_DISASM_FMT_INT))
		format_ = WatchFormat::INT;
	else if (IsControlChecked(hwnd, IDC_DISASM_FMT_FLOAT))
		format_ = WatchFormat::FLOAT;
	else if (IsControlChecked(hwnd, IDC_DISASM_FMT_STR))
		format_ = WatchFormat::STR;

	return true;
}
