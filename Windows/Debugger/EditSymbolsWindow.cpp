#include "EditSymbolsWindow.h"
#include "../resource.h"


bool EditSymbolsWindow::GetCheckState(HWND hwnd, int dlgItem) {
	return SendMessage(GetDlgItem(hwnd, dlgItem), BM_GETCHECK, 0, 0) != 0;
}

bool EditSymbolsWindow::fetchDialogData(HWND hwnd)
{
	char str[256], errorMessage[512];
	PostfixExpression exp;

	scan_ = GetCheckState(hwnd, IDC_EDITSYMBOLS_SCAN);

	// Parse the address
	GetWindowTextA(GetDlgItem(hwnd, IDC_EDITSYMBOLS_ADDRESS), str, 256);

	if (cpu->initExpression(str, exp) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}
	if (cpu->parseExpression(exp, address_) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	// Parse the size
	GetWindowTextA(GetDlgItem(hwnd, IDC_EDITSYMBOLS_SIZE), str, 256);

	if (cpu->initExpression(str, exp) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}
	if (cpu->parseExpression(exp, size_) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", str, getExpressionError());
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	// Now let's validate the range
	if (!Memory::IsValidRange(address_, size_)) {
		MessageBoxA(hwnd, "Invalid range", "Error", MB_OK);
		return false;
	}

	return true;
}

INT_PTR CALLBACK EditSymbolsWindow::StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	EditSymbolsWindow *thiz;
	if (iMsg == WM_INITDIALOG) {
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lParam);
		thiz = (EditSymbolsWindow *)lParam;
	}
	else {
		thiz = (EditSymbolsWindow *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}

	if (!thiz)
		return FALSE;
	return thiz->DlgFunc(hWnd, iMsg, wParam, lParam);
}

INT_PTR EditSymbolsWindow::DlgFunc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char str[128];

	switch (iMsg)
	{
	case WM_INITDIALOG:

		// Set the radiobutton values
		SendMessage(GetDlgItem(hwnd, IDC_EDITSYMBOLS_SCAN), BM_SETCHECK, scan_ ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_EDITSYMBOLS_REMOVE), BM_SETCHECK, scan_ ? BST_UNCHECKED : BST_CHECKED, 0);

		// Set the text in the textboxes
		if (address_ != -1) {
			snprintf(str, sizeof(str), "0x%08X", address_);
			SetWindowTextA(GetDlgItem(hwnd, IDC_EDITSYMBOLS_ADDRESS), str);
		}
		snprintf(str, sizeof(str), "0x%08X", size_);
		SetWindowTextA(GetDlgItem(hwnd, IDC_EDITSYMBOLS_SIZE), str);

		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_EDITSYMBOLS_SCAN:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				scan_ = true;
				break;
			}
			break;
		case IDC_EDITSYMBOLS_REMOVE:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				scan_ = false;
				break;
			}
			break;
		case IDC_EDITSYMBOLS_OK:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				if (fetchDialogData(hwnd)) {
					EndDialog(hwnd, true);
				}
				break;
			};
			break;
		case IDC_EDITSYMBOLS_CANCEL:
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

bool EditSymbolsWindow::exec() {
	return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_EDITSYMBOLS), parentHwnd, StaticDlgFunc, (LPARAM)this) != 0;
}

void EditSymbolsWindow::Scan() {
	bool insertSymbols = MIPSAnalyst::ScanForFunctions(address_, address_ + size_ - 1, true);
	MIPSAnalyst::FinalizeScan(insertSymbols);
}

void EditSymbolsWindow::Remove() {
	u32 func_address = g_symbolMap->GetFunctionStart(address_);
	if (func_address == SymbolMap::INVALID_ADDRESS) {
		func_address = g_symbolMap->GetNextSymbolAddress(address_, SymbolType::ST_FUNCTION);
	}

	u32 counter = 0;
	while (func_address < address_ + size_ && func_address != SymbolMap::INVALID_ADDRESS) {
		g_symbolMap->RemoveFunction(func_address, true);
		++counter;
		func_address = g_symbolMap->GetNextSymbolAddress(address_, SymbolType::ST_FUNCTION);
	}

	if (counter) {
		MIPSAnalyst::ForgetFunctions(address_, address_ + size_ - 1);

		// The following was copied from hle.func.remove:
		g_symbolMap->SortSymbols();

		MIPSAnalyst::UpdateHashMap();
		MIPSAnalyst::ApplyHashMap();

		if (g_Config.bFuncReplacements) {
			MIPSAnalyst::ReplaceFunctions();
		}

		// Clear cache for branch lines and such.
		DisassemblyManager manager;
		manager.clear();
	}
}

void EditSymbolsWindow::eval() {
	if (scan_) {
		Scan();
	}
	else {
		Remove();
	}
}
