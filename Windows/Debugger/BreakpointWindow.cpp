#include <cstdio>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Math/expression_parser.h"

#include "BreakpointWindow.h"
#include "../resource.h"

INT_PTR CALLBACK BreakpointWindow::StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
	BreakpointWindow *thiz;
	if (iMsg == WM_INITDIALOG) {
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lParam);
		thiz = (BreakpointWindow *)lParam;
	}
	else {
		thiz = (BreakpointWindow *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}

	if (!thiz)
		return FALSE;
	return thiz->DlgFunc(hWnd, iMsg, wParam, lParam);
}

INT_PTR BreakpointWindow::DlgFunc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char str[128];

	switch (iMsg)
	{
	case WM_INITDIALOG:
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_EXECUTE), BM_SETCHECK, memory ? BST_UNCHECKED : BST_CHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_MEMORY), BM_SETCHECK, memory ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_READ), BM_SETCHECK, read ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_WRITE), BM_SETCHECK, write ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_ONCHANGE), BM_SETCHECK, onChange ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_ENABLED), BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG), BM_SETCHECK, log ? BST_CHECKED : BST_UNCHECKED, 0);

		EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_READ), memory);
		EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_WRITE), memory);
		EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_ONCHANGE), memory);
		EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE), memory);
		EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), log);

		if (address != -1) {
			snprintf(str, sizeof(str), "0x%08X", address);
			SetWindowTextA(GetDlgItem(hwnd, IDC_BREAKPOINT_ADDRESS), str);
		}

		snprintf(str, sizeof(str), "0x%08X", size);
		SetWindowTextA(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE), str);

		SetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_CONDITION), ConvertUTF8ToWString(condition).c_str());
		SetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), ConvertUTF8ToWString(logFormat).c_str());
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BREAKPOINT_EXECUTE:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				memory = false;
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_READ), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_WRITE), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_ONCHANGE), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE), memory);
				break;
			}
			break;
		case IDC_BREAKPOINT_MEMORY:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				memory = true;
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_READ), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_WRITE), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_ONCHANGE), memory);
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE), memory);
				break;
			}
			break;
		case IDC_BREAKPOINT_LOG:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), GetCheckState(hwnd, IDC_BREAKPOINT_LOG));
				break;
			}
			break;
		case IDC_BREAKPOINT_OK:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				if (fetchDialogData(hwnd)) {
					EndDialog(hwnd, true);
				}
				break;
			};
			break;
		case IDC_BREAKPOINT_CANCEL:
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

bool BreakpointWindow::fetchDialogData(HWND hwnd)
{
	char str[256], errorMessage[512];
	PostfixExpression exp;

	memory = GetCheckState(hwnd, IDC_BREAKPOINT_MEMORY);
	read = GetCheckState(hwnd, IDC_BREAKPOINT_READ);
	write = GetCheckState(hwnd, IDC_BREAKPOINT_WRITE);
	enabled = GetCheckState(hwnd, IDC_BREAKPOINT_ENABLED);
	log = GetCheckState(hwnd, IDC_BREAKPOINT_LOG);
	onChange = GetCheckState(hwnd, IDC_BREAKPOINT_ONCHANGE);

	// parse address
	GetWindowTextA(GetDlgItem(hwnd, IDC_BREAKPOINT_ADDRESS), str, 256);
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

	if (memory)
	{
		// parse size
		GetWindowTextA(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE), str, 256);
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
	}

	// condition
	wchar_t tempCond[512];
	GetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_CONDITION), tempCond, 512);
	condition = ConvertWStringToUTF8(tempCond);
	compiledCondition.clear();
	if (!condition.empty())
	{
		if (cpu->initExpression(condition.c_str(), compiledCondition) == false)
		{
			snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\": %s", condition.c_str(), getExpressionError());
			MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
			return false;
		}
	}

	wchar_t tempLogFormat[512];
	GetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), tempLogFormat, 512);
	logFormat = ConvertWStringToUTF8(tempLogFormat);
	if (!CBreakPoints::ValidateLogFormat(cpu, logFormat)) {
		snprintf(errorMessage, sizeof(errorMessage), "Invalid log format (example: \"{a1}\").");
		MessageBoxA(hwnd, errorMessage, "Error", MB_OK);
		return false;
	}

	return true;
}

bool BreakpointWindow::GetCheckState(HWND hwnd, int dlgItem) {
	return SendMessage(GetDlgItem(hwnd, dlgItem), BM_GETCHECK, 0, 0) != 0;
}

bool BreakpointWindow::exec() {
	return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_BREAKPOINT), parentHwnd, StaticDlgFunc, (LPARAM)this) != 0;
}

void BreakpointWindow::addBreakpoint() {
	BreakAction result = BREAK_ACTION_IGNORE;
	if (log)
		result |= BREAK_ACTION_LOG;
	if (enabled)
		result |= BREAK_ACTION_PAUSE;

	if (memory) {
		// add memcheck
		int cond = 0;
		if (read)
			cond |= MEMCHECK_READ;
		if (write)
			cond |= MEMCHECK_WRITE;
		if (onChange)
			cond |= MEMCHECK_WRITE_ONCHANGE;

		CBreakPoints::AddMemCheck(address, address + size, (MemCheckCondition)cond, result);

		if (!condition.empty()) {
			BreakPointCond cond;
			cond.debug = cpu;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			CBreakPoints::ChangeMemCheckAddCond(address, address + size, cond);
		}

		CBreakPoints::ChangeMemCheckLogFormat(address, address + size, logFormat);
	}
	else {
		// add breakpoint
		CBreakPoints::AddBreakPoint(address, false);

		if (!condition.empty()) {
			BreakPointCond cond;
			cond.debug = cpu;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			CBreakPoints::ChangeBreakPointAddCond(address, cond);
		}

		CBreakPoints::ChangeBreakPoint(address, result);
		CBreakPoints::ChangeBreakPointLogFormat(address, logFormat);
	}
}

void BreakpointWindow::loadFromMemcheck(const MemCheck &memcheck) {
	memory = true;

	read = (memcheck.cond & MEMCHECK_READ) != 0;
	write = (memcheck.cond & MEMCHECK_WRITE) != 0;
	onChange = (memcheck.cond & MEMCHECK_WRITE_ONCHANGE) != 0;

	log = (memcheck.result & BREAK_ACTION_LOG) != 0;
	enabled = (memcheck.result & BREAK_ACTION_PAUSE) != 0;

	address = memcheck.start;
	size = memcheck.end - address;

	if (memcheck.hasCondition) {
		condition = memcheck.condition.expressionString;
	}
	else {
		condition.clear();
	}

	logFormat = memcheck.logFormat;
}

void BreakpointWindow::loadFromBreakpoint(const BreakPoint& breakpoint) {
	memory = false;

	log = (breakpoint.result & BREAK_ACTION_LOG) != 0;
	enabled = (breakpoint.result & BREAK_ACTION_PAUSE) != 0;
	address = breakpoint.addr;
	size = 1;

	if (breakpoint.hasCond) {
		condition = breakpoint.cond.expressionString;
	}
	else {
		condition.clear();
	}

	logFormat = breakpoint.logFormat;
}

void BreakpointWindow::initBreakpoint(u32 _address) {
	memory = false;
	enabled = true;
	address = _address;
	size = 1;
	condition.clear();
}
