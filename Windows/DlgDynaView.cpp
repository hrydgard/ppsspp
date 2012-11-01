
#include "stdafx.h"
#include <windowsx.h>

#include "resource.h"

#include "../Core/HW.h"
#include "../Core/MemMap.h"
//#include "hw\hw.h"
#include "DlgDynaView.h"
//#include "PowerPC/PowerPCDisasm.h"
//#include "PowerPC/DynaRec/DynaCodeCache.h"
//#include "pchw/x86Disasm.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////
// I M P L E M E N T A T I O N //////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

HINSTANCE		CDynaViewDlg::m_hInstance = NULL;
HWND			CDynaViewDlg::m_hParent = NULL;
HWND			CDynaViewDlg::m_hDlg = NULL;
int				CDynaViewDlg::m_iBlock = -1;
HFONT dfont;
//RECT slRect;

// __________________________________________________________________________________________________
// constructor 
//
void CDynaViewDlg::Init(HINSTANCE _hInstance, HWND _hParent)
{
	m_hInstance = _hInstance;
	m_hParent = _hParent;
	dfont = CreateFont(14,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,
		"Courier New");

	//
	// --- build dialog ---
	//
	m_hDlg = CreateDialog(_hInstance, (LPCSTR)IDD_DYNAVIEW, _hParent, DlgProc);
	Size();
	ShowWindow(m_hDlg,SW_HIDE);
#ifdef THEMES
	if (WTL::CTheme::IsThemingSupported())
		EnableThemeDialogTexture(m_hDlg ,ETDT_ENABLETAB);
#endif

	//m_hListView = GetDlgItem(m_hDlg , IDC_SEARCHRESULTS);
//	SetWindowText(GetDlgItem(m_hDlg ,IDC_SEARCHRANGESTART),"80003100");
//	SetWindowText(GetDlgItem(m_hDlg ,IDC_SEARCHRANGEEND),"80400000");

	//GetWindowRect(GetDlgItem(hDlg,IDC_SYMBOLS),&slRect);
	//Size();
}

// __________________________________________________________________________________________________
// destructor 
//
void CDynaViewDlg::DeInit(void)
{
	DeleteObject(dfont);
}

// __________________________________________________________________________________________________
// Show 
//
void 
CDynaViewDlg::Show(bool _bShow)
{
	ShowWindow(m_hDlg, _bShow?SW_NORMAL:SW_HIDE);
	if (_bShow)
		BringWindowToTop(m_hDlg);
}


// __________________________________________________________________________________________________
// RegistersDlgProc 
//
BOOL CALLBACK 
CDynaViewDlg::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{

	switch(message)
	{
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDC_PREVBLOCK: 
				{
					if (m_iBlock>0)
						View(m_iBlock-1);
				}
				break;
			case IDC_NEXTBLOCK:
				{
					if (m_iBlock>=-1)
						View(m_iBlock+1);
				}
				break;
			case IDC_CODEADDRESS:
				{
					
				}
				break;
			}
		}
		break;
	case WM_INITDIALOG:
		{
			SendMessage(GetDlgItem(hDlg,IDC_X86ASM),WM_SETFONT,(WPARAM)dfont,0);
			SendMessage(GetDlgItem(hDlg,IDC_POWERPCASM),WM_SETFONT,(WPARAM)dfont,0);
			View(-1);
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


void CDynaViewDlg::View(int blocknum)
{
	if (!HW_IsInited() || blocknum == -1)
	{
		m_iBlock = -1;
		SetWindowText(GetDlgItem(m_hDlg,IDC_POWERPCASM), "Invalid block");
		SetWindowText(GetDlgItem(m_hDlg,IDC_X86ASM), "Invalid block");
		SetWindowText(GetDlgItem(m_hDlg,IDC_BLOCKNUMBER), "N/A");
		return;
	}
	m_iBlock = blocknum;
	/*
	CDynaCodeCache::Codeblock *block;
	block = CDynaCodeCache::GetCodeBlock(m_iBlock);
	if (block == 0 || block->m_pCode == 0)
	{
		View(-1);
		return;
	}
	char temp[256];
	int addr=block->m_uAddress;
	sprintf(temp,"%08x",addr);
	SetWindowText(GetDlgItem(m_hDlg,IDC_CODEADDRESS),temp);

	char *bigtemp = new char[65536];
	bigtemp[0]=0;
	for (int i=0; i<block->m_uBlockSize; i++, addr+=4)
	{
		u32 op = i==0?block->m_uOriginalOp:CMemory::ReadUncheckedu32(addr);
		strcat(bigtemp,DisassembleGekko(op,addr));
		strcat(bigtemp,"\x0D\x0A");
	}
	SetWindowText(GetDlgItem(m_hDlg,IDC_POWERPCASM),bigtemp);

	bigtemp[0]=0;
	_u8 *x86 = block->m_pCode;
	while (x86<block->m_pCode+block->m_uCodeSize)
	{
		int size;
		strcat(bigtemp,disasmx86((unsigned char*)x86,(int)x86,&size));
		strcat(bigtemp,"\x0D\x0A");
		x86+=size;
	}
    SetWindowText(GetDlgItem(m_hDlg,IDC_X86ASM),bigtemp);

	sprintf(temp,"%i/%i",m_iBlock,CDynaCodeCache::GetNumBlocks());
	SetWindowText(GetDlgItem(m_hDlg,IDC_BLOCKNUMBER), temp);
	delete [] bigtemp;
	*/
}

void CDynaViewDlg::ViewAddr(u32 addr)
{
	/*
	int num = CDynaCodeCache::GetCodeBlockNumber(addr);
	if (num!=-1)
		View(num);*/
}

void CDynaViewDlg::Size()
{
	RECT rc,rc2;
	GetClientRect(m_hDlg,&rc);
	int dw=rc.right-rc.left;
	int dh=rc.bottom-rc.top;

	int space = 6;

	HWND lbox = GetDlgItem(m_hDlg, IDC_POWERPCASM);
	HWND rbox = GetDlgItem(m_hDlg, IDC_X86ASM);
	GetWindowRect(lbox,&rc2);

	int boxw = (dw-space*3)/2;
	int boxtop = 40;
	int boxh = dh-boxtop-space;
	MoveWindow(lbox,space,boxtop,boxw,boxh,TRUE);
	MoveWindow(rbox,space*2+boxw,boxtop,boxw,boxh,TRUE);
	/*
	RECT rc;
	HWND hDlg = m_pWinDialog->GetDialogHandle();
	GetClientRect(hDlg,&rc);
	int dw=rc.right-rc.left;
	int dh=rc.bottom-rc.top;
	HWND memView = GetDlgItem(hDlg, IDC_MEMVIEW);
	HWND symList = GetDlgItem(hDlg, IDC_SYMBOLS);
	int wf = slRect.right-slRect.left;
	int w = dw-3-wf;
	int top = 48;
	MoveWindow(symList,0,top,wf,dh-top,TRUE);
	MoveWindow(memView,wf+4,top,w,dh-top,TRUE);
	*/
}