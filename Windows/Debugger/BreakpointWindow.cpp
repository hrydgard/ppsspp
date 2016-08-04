#include <stdio.h>

#include "base/compat.h"
#include "util/text/utf8.h"

#include "BreakpointWindow.h"
#include "../resource.h"


BreakpointWindow* BreakpointWindow::bp;

INT_PTR CALLBACK BreakpointWindow::dlgFunc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	char str[128];

	switch (iMsg)
	{
	case WM_INITDIALOG:
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_EXECUTE),BM_SETCHECK,bp->memory ? BST_UNCHECKED : BST_CHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_MEMORY),BM_SETCHECK,bp->memory ? BST_CHECKED : BST_UNCHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_READ),BM_SETCHECK, bp->read ? BST_CHECKED : BST_UNCHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_WRITE),BM_SETCHECK, bp->write ? BST_CHECKED : BST_UNCHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_ONCHANGE),BM_SETCHECK, bp->onChange ? BST_CHECKED : BST_UNCHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_ENABLED),BM_SETCHECK, bp->enabled ? BST_CHECKED : BST_UNCHECKED,0);
		SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG),BM_SETCHECK, bp->log ? BST_CHECKED : BST_UNCHECKED,0);

		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_READ),bp->memory);
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_WRITE),bp->memory);
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_ONCHANGE),bp->memory);
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_SIZE),bp->memory);
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_CONDITION),!bp->memory);
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG_FORMAT), bp->log);
		
		if (bp->address != -1)
		{
			snprintf(str, sizeof(str), "0x%08X", bp->address);
			SetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_ADDRESS),str);
		}

		snprintf(str, sizeof(str), "0x%08X", bp->size);
		SetWindowTextA(GetDlgItem(hwnd, IDC_BREAKPOINT_SIZE),str);
		
		SetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_CONDITION), ConvertUTF8ToWString(bp->condition).c_str());
		SetWindowTextW(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), ConvertUTF8ToWString(bp->logFormat).c_str());
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BREAKPOINT_EXECUTE:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->memory = false;
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_READ),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_WRITE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_ONCHANGE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_SIZE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_CONDITION),!bp->memory);
				break;
			}
			break;
		case IDC_BREAKPOINT_MEMORY:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				bp->memory = true;
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_READ),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_WRITE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_ONCHANGE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_SIZE),bp->memory);
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_CONDITION),!bp->memory);
				break;
			}
			break;
		case IDC_BREAKPOINT_LOG:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				EnableWindow(GetDlgItem(hwnd, IDC_BREAKPOINT_LOG_FORMAT), bp->GetCheckState(hwnd, IDC_BREAKPOINT_LOG));
				break;
			}
			break;
		case IDC_BREAKPOINT_OK:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				if (bp->fetchDialogData(hwnd))
				{
					EndDialog(hwnd,true);
				}
				break;
			};
			break;
		case IDC_BREAKPOINT_CANCEL:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				EndDialog(hwnd,false);
				break;
			};
			break;
		case IDOK:
			if (bp->fetchDialogData(hwnd))
			{
				EndDialog(hwnd,true);
			}
			break;
		case IDCANCEL:
			EndDialog(hwnd,false);
			break;
		}

	case WM_KEYDOWN:

		break;
	}

	return FALSE;
}

bool BreakpointWindow::fetchDialogData(HWND hwnd)
{
	char str[256],errorMessage[512];
	PostfixExpression exp;

	memory = GetCheckState(hwnd, IDC_BREAKPOINT_MEMORY);
	read = GetCheckState(hwnd, IDC_BREAKPOINT_READ);
	write = GetCheckState(hwnd, IDC_BREAKPOINT_WRITE);
	enabled = GetCheckState(hwnd, IDC_BREAKPOINT_ENABLED);
	log = GetCheckState(hwnd, IDC_BREAKPOINT_LOG);
	onChange = GetCheckState(hwnd, IDC_BREAKPOINT_ONCHANGE);

	// parse address
	GetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_ADDRESS),str,256);
	if (cpu->initExpression(str,exp) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	}

	if (cpu->parseExpression(exp,address) == false)
	{
		snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
		MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
		return false;
	}

	if (memory)
	{
		// parse size
		GetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_SIZE),str,256);
		if (cpu->initExpression(str,exp) == false)
		{
			snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
			MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
			return false;
		}

		if (cpu->parseExpression(exp,size) == false)
		{
			snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
			MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
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
			snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
			MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
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

bool BreakpointWindow::exec()
{
	bp = this;
	bool result = DialogBoxParam(GetModuleHandle(0),MAKEINTRESOURCE(IDD_BREAKPOINT),parentHwnd,dlgFunc,(LPARAM)this) != 0;
	return result;
}

void BreakpointWindow::addBreakpoint()
{
	BreakAction result = BREAK_ACTION_IGNORE;
	if (log)
		result |= BREAK_ACTION_LOG;
	if (enabled)
		result |= BREAK_ACTION_PAUSE;

	if (memory)
	{
		// add memcheck
		int cond = 0;
		if (read)
			cond |= MEMCHECK_READ;
		if (write)
			cond |= MEMCHECK_WRITE;
		if (onChange)
			cond |= MEMCHECK_WRITE_ONCHANGE;

		CBreakPoints::AddMemCheck(address, address + size, (MemCheckCondition)cond, result);
		CBreakPoints::ChangeMemCheckLogFormat(address, address + size, logFormat);
	} else {
		// add breakpoint
		CBreakPoints::AddBreakPoint(address,false);

		if (!condition.empty())
		{
			BreakPointCond cond;
			cond.debug = cpu;
			cond.expressionString = condition;
			cond.expression = compiledCondition;
			CBreakPoints::ChangeBreakPointAddCond(address,cond);
		}

		CBreakPoints::ChangeBreakPoint(address, result);
		CBreakPoints::ChangeBreakPointLogFormat(address, logFormat);
	}
}

void BreakpointWindow::loadFromMemcheck(MemCheck& memcheck)
{
	memory = true;

	read = (memcheck.cond & MEMCHECK_READ) != 0;
	write = (memcheck.cond & MEMCHECK_WRITE) != 0;
	onChange = (memcheck.cond & MEMCHECK_WRITE_ONCHANGE) != 0;

	log = (memcheck.result & BREAK_ACTION_LOG) != 0;
	enabled = (memcheck.result & BREAK_ACTION_PAUSE) != 0;

	address = memcheck.start;
	size = memcheck.end-address;

	logFormat = memcheck.logFormat;
}

void BreakpointWindow::loadFromBreakpoint(BreakPoint& breakpoint)
{
	memory = false;

	log = (breakpoint.result & BREAK_ACTION_LOG) != 0;
	enabled = (breakpoint.result & BREAK_ACTION_PAUSE) != 0;
	address = breakpoint.addr;
	size = 1;

	if (breakpoint.hasCond) {
		condition = breakpoint.cond.expressionString;
	} else {
		condition.clear();
	}

	logFormat = breakpoint.logFormat;
}

void BreakpointWindow::initBreakpoint(u32 _address)
{
	memory = false;
	enabled = true;
	address = _address;
	size = 1;
	condition.clear();
}
