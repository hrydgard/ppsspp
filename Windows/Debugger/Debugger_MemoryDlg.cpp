// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "stdafx.h"
#include <windowsx.h>
#include "..\resource.h"

#include "base/display.h"
#include "util/text/utf8.h"

#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSDebugInterface.h" //	BAD

#include "Debugger_MemoryDlg.h"
#include "CtrlMemView.h"
#include "DebuggerShared.h"
#include "LogManager.h"
#include "winnt.h"
#include <WindowsX.h>
#include <algorithm>


RECT CMemoryDlg::slRect; //sym list rect

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

	memViewHdl = GetDlgItem(m_hDlg, IDC_MEMVIEW);
	symListHdl = GetDlgItem(m_hDlg, IDC_SYMBOLS);
	searchBoxHdl = GetDlgItem(m_hDlg, IDC_SEARCH_BOX);
	srcListHdl = GetDlgItem(m_hDlg, IDC_SEARCH_RESULTS);

	memView = CtrlMemView::getFrom(memViewHdl);
	memView->setDebugger(_cpu);

	Button_SetCheck(GetDlgItem(m_hDlg,IDC_RAM), TRUE);
	Button_SetCheck(GetDlgItem(m_hDlg,IDC_MODESYMBOLS), TRUE);

	GetWindowRect(symListHdl, &slRect);
	GetWindowRect(srcListHdl, &srRect);


	// subclass the edit box
	editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
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
		if (memView != NULL)
			memView->redraw();
	}	
}

void CMemoryDlg::searchBoxRedraw(std::vector<u32> results) {
	wchar_t temp[256]{};
	SendMessage(srcListHdl, WM_SETREDRAW, FALSE, 0);
	ListBox_ResetContent(srcListHdl);
	for (int i = 0; i < results.size(); i++) {
		wsprintf(temp, L"0x%08X", results[i]);
		int index = (int)ListBox_AddString(srcListHdl, temp);
		ListBox_SetItemData(srcListHdl, index, results[i]);
	}
   	SendMessage(srcListHdl, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(srcListHdl, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}


void CMemoryDlg::NotifyMapLoaded()
{
	if (m_hDlg)
	{
		g_symbolMap->FillSymbolListBox(symListHdl,ST_DATA);
		int sel = ComboBox_GetCurSel(memViewHdl);
		ComboBox_ResetContent(memViewHdl);
    /*
		for (int i = 0; i < cpu->getMemMap()->numRegions; i++)
		{
			// TODO: wchar_t
			int n = ComboBox_AddString(lb,cpu->getMemMap()->regions[i].name);
			ComboBox_SetItemData(lb,n,cpu->getMemMap()->regions[i].start);
		}*/
		ComboBox_SetCurSel(memViewHdl,sel>=0?sel:0);
	}
	Update(); 
}

BOOL CMemoryDlg::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message){
	case WM_COMMAND:{
		HWND lb = GetDlgItem(m_hDlg, LOWORD(wParam));
		switch (LOWORD(wParam)){
		case IDC_REGIONS:
			switch (HIWORD(wParam)) {
				case LBN_DBLCLK:{
					int n = ComboBox_GetCurSel(lb);
					if (n != -1) {
						unsigned int addr = (unsigned int)ComboBox_GetItemData(lb,n);
						memView->gotoAddr(addr);
					}
				}
				break;
		};
		break;
		case IDC_SYMBOLS: 
			switch (HIWORD(wParam)) { 
				case LBN_DBLCLK:{
					int n = ListBox_GetCurSel(lb);
					if (n != -1) {
						unsigned int addr = (unsigned int)ListBox_GetItemData(lb,n);
						memView->gotoAddr(addr);
					}
			}
			break;
		};
		case IDC_SEARCH_RESULTS:
			switch (HIWORD(wParam)) {
				case LBN_DBLCLK: {
					int n = ListBox_GetCurSel(lb);
					if (n != -1) {
						unsigned int addr = (unsigned int)ListBox_GetItemData(lb, n);
						memView->gotoAddr(addr);
					}
			}
			break;
			};
		break;
		case IDC_SHOWOFFSETS:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				if (SendDlgItemMessage(m_hDlg, IDC_SHOWOFFSETS, BM_GETCHECK, 0, 0))
					memView->toggleOffsetScale(On);
				else
					memView->toggleOffsetScale(Off);
				break;
			}
			break;
		case IDC_BUTTON_SEARCH:
			switch (HIWORD(wParam))
			{
			case BN_CLICKED:
				wchar_t temp[256];
				GetWindowText(searchBoxHdl, temp, 255);
				std::vector<u32> results = memView->searchString(ConvertWStringToUTF8(temp).c_str());
				if (results.size() > 0){
					searchBoxRedraw(results);
				}
				break;
			}
		}
		}
		break;
	case WM_DEB_MAPLOADED:
		NotifyMapLoaded();
		break;
	case WM_DEB_GOTOADDRESSEDIT:{
		wchar_t temp[256];
		u32 addr;
		GetWindowText(editWnd,temp,255);

		if (parseExpression(ConvertWStringToUTF8(temp).c_str(),cpu,addr) == false) {
			displayExpressionError(m_hDlg);
		} else {
			memView->gotoAddr(addr);
			SetFocus(memViewHdl);
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
	memView->gotoAddr(addr);
	SetFocus(memViewHdl);
}


void CMemoryDlg::Size()
{
	const float fontScale = 1.0f / g_dpi_scale_real_y;

	GetClientRect(m_hDlg,&winRect);
	int dlg_w = winRect.right - winRect.left;
	int dlg_h = winRect.bottom - winRect.top;


	int wf = slRect.right-slRect.left;
	int w = dlg_w - 3 * fontScale - wf*2;
	int top = 48 * fontScale;
	int height = dlg_h - top;
	//HWND, X, Y, width, height, repaint
	MoveWindow(symListHdl, 0    ,top, wf, height, TRUE);
	MoveWindow(memViewHdl, wf+4 ,top, w, height, TRUE);
	MoveWindow(srcListHdl, wf + 4 + w+ 4, top, wf-4, height, TRUE);
}
