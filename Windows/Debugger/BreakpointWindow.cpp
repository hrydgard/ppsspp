#include <stdio.h>

#include "base/compat.h"

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
		EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG),bp->memory);

		
		if (bp->address != -1)
		{
			snprintf(str, sizeof(str), "0x%08X", bp->address);
			SetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_ADDRESS),str);
		}

		snprintf(str, sizeof(str), "0x%08X", bp->size);
		SetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_SIZE),str);
		
		SetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_CONDITION),bp->condition);
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
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG),bp->memory);
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
				EnableWindow(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG),bp->memory);
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

	memory = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_MEMORY),BM_GETCHECK,0,0) != 0;
	read = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_READ),BM_GETCHECK,0,0) != 0;
	write = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_WRITE),BM_GETCHECK,0,0) != 0;
	enabled = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_ENABLED),BM_GETCHECK,0,0) != 0;
	log = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_LOG),BM_GETCHECK,0,0) != 0;
	onChange = SendMessage(GetDlgItem(hwnd,IDC_BREAKPOINT_ONCHANGE),BM_GETCHECK,0,0) != 0;

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
	GetWindowTextA(GetDlgItem(hwnd,IDC_BREAKPOINT_CONDITION),condition,128);
	compiledCondition.clear();
	if (condition[0] != 0)
	{
		if (cpu->initExpression(condition,compiledCondition) == false)
		{
			snprintf(errorMessage, sizeof(errorMessage), "Invalid expression \"%s\".",str);
			MessageBoxA(hwnd,errorMessage,"Error",MB_OK);
			return false;
		}
	}

	return true;
}

bool BreakpointWindow::exec()
{
	bp = this;
	bool result = DialogBoxParam(GetModuleHandle(0),MAKEINTRESOURCE(IDD_BREAKPOINT),parentHwnd,dlgFunc,(LPARAM)this) != 0;
	return result;
}

void BreakpointWindow::addBreakpoint()
{
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

		MemCheckResult result;
		if (log && enabled) result = MEMCHECK_BOTH;
		else if (log) result = MEMCHECK_LOG;
		else if (enabled) result = MEMCHECK_BREAK;
		else result = MEMCHECK_IGNORE;

		CBreakPoints::AddMemCheck(address, address + size, (MemCheckCondition)cond, result);
	} else {
		// add breakpoint
		CBreakPoints::AddBreakPoint(address,false);

		if (condition[0] != 0)
		{
			BreakPointCond cond;
			cond.debug = cpu;
			strcpy(cond.expressionString,condition);
			cond.expression = compiledCondition;
			CBreakPoints::ChangeBreakPointAddCond(address,cond);
		}

		if (enabled == false)
		{
			CBreakPoints::ChangeBreakPoint(address,false);
		}
	}
}

void BreakpointWindow::loadFromMemcheck(MemCheck& memcheck)
{
	memory = true;

	read = (memcheck.cond & MEMCHECK_READ) != 0;
	write = (memcheck.cond & MEMCHECK_WRITE) != 0;
	onChange = (memcheck.cond & MEMCHECK_WRITE_ONCHANGE) != 0;

	switch (memcheck.result)
	{
	case MEMCHECK_BOTH:
		log = enabled = true;
		break;
	case MEMCHECK_LOG:
		log = true;
		enabled = false;
		break;
	case MEMCHECK_BREAK:
		log = false;
		enabled = true;
		break;
	case MEMCHECK_IGNORE:
		log = enabled = false;
		break;
	}

	address = memcheck.start;
	size = memcheck.end-address;
}

void BreakpointWindow::loadFromBreakpoint(BreakPoint& breakpoint)
{
	memory = false;

	enabled = breakpoint.enabled;
	address = breakpoint.addr;
	size = 1;

	if (breakpoint.hasCond)
	{
		strcpy(condition,breakpoint.cond.expressionString);
	} else {
		condition[0] = 0;
	}
}

void BreakpointWindow::initBreakpoint(u32 _address)
{
	memory = false;
	enabled = true;
	address = _address;
	size = 1;
	condition[0] = 0;
}
