// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "stdafx.h"
#include <windowsx.h>
#include "..\resource.h"

#include "util/text/utf8.h"

#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSDebugInterface.h" //	BAD

#include "Debugger_MemoryDlg.h"
#include "CtrlMemView.h"
#include "DebuggerShared.h"


RECT CMemoryDlg::slRect;

FAR WNDPROC DefAddressEditProc;
HWND AddressEditParentHwnd = 0;

LRESULT CALLBACK AddressEditProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_KEYDOWN:
		if( wParam == VK_RETURN )
		{
			if (AddressEditParentHwnd != 0)
				SendMessage(AddressEditParentHwnd,WM_DEB_GOTOADDRESSEDIT,0,0);
			return 0;
		}
		break;
	case WM_KEYUP:
		if( wParam == VK_RETURN ) return 0;
		break;
	case WM_CHAR:
		if( wParam == VK_RETURN ) return 0;
		break;
	case WM_GETDLGCODE:
		if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN)
		{
			if (wParam == VK_RETURN) return DLGC_WANTMESSAGE;
		}
		break;
	};

	return (LRESULT)CallWindowProc((WNDPROC)DefAddressEditProc,hDlg,message,wParam,lParam);
}


CMemoryDlg::CMemoryDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu) : Dialog((LPCSTR)IDD_MEMORY, _hInstance,_hParent)
{
	cpu = _cpu;
	wchar_t temp[256];
	wsprintf(temp,L"Memory Viewer - %S",cpu->GetName());
	SetWindowText(m_hDlg,temp);

	ShowWindow(m_hDlg,SW_HIDE);
	CtrlMemView *ptr = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
	ptr->setDebugger(_cpu);

	Button_SetCheck(GetDlgItem(m_hDlg,IDC_RAM), TRUE);
	Button_SetCheck(GetDlgItem(m_hDlg,IDC_MODESYMBOLS), TRUE);

	GetWindowRect(GetDlgItem(m_hDlg,IDC_SYMBOLS),&slRect);

	// subclass the edit box
	HWND editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
	DefAddressEditProc = (WNDPROC)GetWindowLongPtr(editWnd,GWLP_WNDPROC);
	SetWindowLongPtr(editWnd,GWLP_WNDPROC,(LONG_PTR)AddressEditProc); 
	AddressEditParentHwnd = m_hDlg;

	Size();
}


CMemoryDlg::~CMemoryDlg(void)
{
}

void CMemoryDlg::Update(void)
{
	if (m_hDlg != NULL)
	{
		CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
		if (mv != NULL)
			mv->redraw();
	}	
}

void CMemoryDlg::NotifyMapLoaded()
{
	if (m_hDlg)
	{
		HWND list = GetDlgItem(m_hDlg,IDC_SYMBOLS);
		symbolMap.FillSymbolListBox(list,ST_DATA);	
		HWND lb = GetDlgItem(m_hDlg,IDC_REGIONS);
		int sel = ComboBox_GetCurSel(lb);
		ComboBox_ResetContent(lb);
    /*
		for (int i = 0; i < cpu->getMemMap()->numRegions; i++)
		{
			// TODO: wchar_t
			int n = ComboBox_AddString(lb,cpu->getMemMap()->regions[i].name);
			ComboBox_SetItemData(lb,n,cpu->getMemMap()->regions[i].start);
		}*/
		ComboBox_SetCurSel(lb,sel>=0?sel:0);
	}
	Update(); 
}


BOOL CMemoryDlg::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{

	switch(message)
	{
	case WM_COMMAND:
		{
			CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
			switch (LOWORD(wParam))
			{
			case IDC_REGIONS:
				switch (HIWORD(wParam)) 
				{ 
				case LBN_DBLCLK:
					{
						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ComboBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ComboBox_GetItemData(lb,n);
							mv->gotoAddr(addr);
						}
					}
					break;
				};
				break;
			case IDC_SYMBOLS: 
				switch (HIWORD(wParam)) 
				{ 
				case LBN_DBLCLK:
					{

						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ListBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ListBox_GetItemData(lb,n);
							mv->gotoAddr(addr);
						}
					}
					break;
				};
				break;
			}
		}
		break;
	case WM_DEB_MAPLOADED:
		NotifyMapLoaded();
		break;
	case WM_DEB_GOTOADDRESSEDIT:
	{
		CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
		wchar_t temp[256];
		u32 addr;
		GetWindowText(GetDlgItem(m_hDlg,IDC_ADDRESS),temp,255);

		if (parseExpression(ConvertWStringToUTF8(temp).c_str(),cpu,addr) == false) {
			displayExpressionError(m_hDlg);
		} else {
			mv->gotoAddr(addr);
			SetFocus(GetDlgItem(m_hDlg,IDC_MEMVIEW));
		}
		break;
	}

	case WM_DEB_UPDATE:
		Update();
		return TRUE;

	case WM_INITDIALOG:
		{
			return TRUE;
		}
		break;
	case WM_SIZE:
		Size();
		break;
	case WM_CLOSE:
		Show(false);
		break;
	}
		
	return FALSE;
}

void CMemoryDlg::Goto(u32 addr)
{
	Show(true);
	CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(CMemoryDlg::m_hDlg,IDC_MEMVIEW));
	mv->gotoAddr(addr);
	SetFocus(GetDlgItem(CMemoryDlg::m_hDlg,IDC_MEMVIEW));
}


void CMemoryDlg::Size()
{
	RECT rc;
	GetClientRect(m_hDlg,&rc);
	int dw=rc.right-rc.left;
	int dh=rc.bottom-rc.top;
	HWND memView = GetDlgItem(m_hDlg, IDC_MEMVIEW);
	HWND symList = GetDlgItem(m_hDlg, IDC_SYMBOLS);
	int wf = slRect.right-slRect.left;
	int w = dw-3-wf;
	int top = 48;
	MoveWindow(symList,0,top,wf,dh-top,TRUE);
	MoveWindow(memView,wf+4,top,w,dh-top,TRUE);
}
