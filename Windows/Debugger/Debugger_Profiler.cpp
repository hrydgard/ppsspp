// __________________________________________________________________________________________________
//
//			$Archive:  $
//			$Revision: 1.42 $
//			$Author: tronic $ 
//			$Modtime:  $  
//
/////////////////////////////////////////////////////////////////////////////////////////////////////
// M O D U L E  B E G I N ///////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <windowsx.h>
#include <stdio.h>
#include "../resource.h"
#include "../../Globals.h"

#include "../../Core/ARM/ARM.h"

#include "Debugger_Registers.h"
#ifdef THEMES
#include "../XPTheme.h"
#include "../W32Util/TabControl.h"
#endif

extern void SetDlgItemText_Hex(HWND _hDialog, int _iResource, u32 _iValue);
extern void SetDlgItemText_Float(HWND _hDialog, int _iResource, u32 _iReg);

/////////////////////////////////////////////////////////////////////////////////////////////////////
// I M P L E M E N T A T I O N //////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

HINSTANCE		CRegisters::m_hInstance = NULL;
HWND			CRegisters::m_hParent = NULL;
HWND			CRegisters::m_hDlg = NULL;
W32Util::TabControl * CRegisters::m_pWinTabCtrl= NULL;

HWND		CRegisters::m_pWinDialog_GPR = NULL;
HWND		CRegisters::m_pWinDialog_FPU = NULL;
HWND		CRegisters::m_pWinDialog_DMA = NULL;
HWND		CRegisters::m_pWinDialog_TIMER = NULL;

BOOL			CRegisters::m_bPaired = FALSE;

/////////////////////////////////////////////////////////////////////////////////////////////////////
// I M P L E M E N T A T I O N //////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

// __________________________________________________________________________________________________
// constructor 
//
void CRegisters::Init(HINSTANCE _hInstance, HWND _hParent)
{
	m_hInstance = _hInstance;
	m_hParent = _hParent;

	//
	// --- build dialog ---
	//

	m_hDlg = CreateDialog(_hInstance, (LPCSTR)IDD_REGISTERS, _hParent, RegistersDlgProc);
	ShowWindow(m_hDlg,SW_HIDE);

	m_pWinTabCtrl = new W32Util::TabControl(_hInstance, GetDlgItem(m_hDlg,IDC_REGISTER_TAB), RegistersDlgProc);

	m_pWinDialog_GPR   = m_pWinTabCtrl->AddItem("Main", IDD_REGISTERS_GPR,   RegistersDlgProc);	
	m_pWinDialog_FPU   = m_pWinTabCtrl->AddItem("FPU",  IDD_REGISTERS_FPU1,  RegistersDlgProc);	
	m_pWinDialog_DMA   = m_pWinTabCtrl->AddItem("Misc",  IDD_REGISTERS_DMA,   RegistersDlgProc);	
	
#ifdef THEMES
	if (WTL::CTheme::IsThemingSupported())
	{
		EnableThemeDialogTexture(m_pWinDialog_GPR,ETDT_ENABLETAB);
		EnableThemeDialogTexture(m_pWinDialog_FPU,ETDT_ENABLETAB);
		EnableThemeDialogTexture(m_pWinDialog_DMA,ETDT_ENABLETAB);
	}
#endif
//	m_pWinDialog_TIMER = m_pWinTabCtrl->AddItem("Timer",IDD_REGISTERS_TIMER, RegistersDlgProc);	
}

// __________________________________________________________________________________________________
// destructor 
//
void CRegisters::DeInit(void)
{
	DestroyWindow(m_hDlg);
	DestroyWindow(m_pWinDialog_GPR);
	DestroyWindow(m_pWinDialog_FPU);
	DestroyWindow(m_pWinDialog_DMA);
}

// __________________________________________________________________________________________________
// Show 
//
void 
CRegisters::Show(bool _bShow)
{
	Update();
	ShowWindow(m_hDlg,_bShow?SW_NORMAL:SW_HIDE);
	if (_bShow)
		BringWindowToTop(m_hDlg);
}

// __________________________________________________________________________________________________
// Update 
//
void 
CRegisters::Update(void)
{
	UpdateGPR();
	UpdateFPU();
	UpdateDMA();
//	UpdateTIMER();
}

bool inChange = false;
void CRegisters::changeReg(DWORD control, int reg)
{
	inChange=true;
	HWND ctrl = GetDlgItem(m_pWinDialog_GPR,control);
	char temp[256];
	GetWindowText(ctrl,temp,256);
	DWORD val;
	sscanf(temp,"%08x",&val);
	currentARM->r[reg] = val;
	inChange=false;
}


// __________________________________________________________________________________________________
// RegistersDlgProc 
//
BOOL CALLBACK 
CRegisters::RegistersDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (m_pWinTabCtrl != NULL)
	{
		m_pWinTabCtrl->MessageHandler (message,wParam,lParam);
	}
	
	switch(message)
	{
	case WM_INITDIALOG:
		{
			return TRUE;
		}
		break;
	
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
			/*
		case IDC_PAIRED:
			m_bPaired = Button_GetCheck(GetDlgItem(m_pWinDialog_FPU, IDC_PAIRED));
			CRegisters::UpdateFPU();
			break;*/
			
			case IDC_GPR_R0: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R0,0); break;
			case IDC_GPR_R1: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R1,1); break;
			case IDC_GPR_R2: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R2,2); break;
			case IDC_GPR_R3: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R3,3); break;
			case IDC_GPR_R4: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R4,4); break;
			case IDC_GPR_R5: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R5,5); break;
			case IDC_GPR_R6: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R6,6); break;
			case IDC_GPR_R7: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R7,7); break;
			case IDC_GPR_R8: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R8,8); break;
			case IDC_GPR_R9: if (HIWORD(wParam)==EN_CHANGE)  changeReg(IDC_GPR_R9,9); break;
			case IDC_GPR_R10: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R10,10); break;
			case IDC_GPR_R11: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R11,11); break;
			case IDC_GPR_R12: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R12,12); break;
			case IDC_GPR_R13: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R13,13); break;
			case IDC_GPR_R14: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R14,14); break;
			case IDC_GPR_R15: if (HIWORD(wParam)==EN_CHANGE) changeReg(IDC_GPR_R15,15); break;
			//case IDC_UPDATEMISC: UpdateDMA(); break;
		}
		break;
	case WM_CLOSE:
		Show(false);
		break;
	}
		
	return FALSE;
}

// __________________________________________________________________________________________________
// UpdateGPR 
//
void 
CRegisters::UpdateGPR(void)
{
	HWND hWnd = m_pWinDialog_GPR;

	SetDlgItemText_Hex(hWnd, IDC_GPR_R0, currentARM->r[0]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R1, currentARM->r[1]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R2, currentARM->r[2]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R3, currentARM->r[3]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R4, currentARM->r[4]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R5, currentARM->r[5]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R6, currentARM->r[6]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R7, currentARM->r[7]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R8, currentARM->r[8]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R9, currentARM->r[9]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R10, currentARM->r[10]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R11, currentARM->r[11]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R12, currentARM->r[12]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R13, currentARM->r[13]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R14, currentARM->r[14]);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R15, currentARM->r[15]);

	SetDlgItemText_Hex(hWnd, IDC_GPR_R13_SVC, currentARM->r13_svc);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R14_SVC, currentARM->r14_svc);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R13_USR, currentARM->r13_usr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R14_USR, currentARM->r14_usr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R13_IRQ, currentARM->r13_irq);
	SetDlgItemText_Hex(hWnd, IDC_GPR_R14_IRQ, currentARM->r14_irq);

	SetDlgItemText_Hex(hWnd, IDC_GPR_CPSR,	currentARM->GetCPSR());
	SetDlgItemText_Hex(hWnd, IDC_GPR_SPSR,	currentARM->spsr);
/*	SetDlgItemText_Hex(hWnd, IDC_GPR_LR,	PowerPC::LR);
	SetDlgItemText_Hex(hWnd, IDC_GPR_CR,	PowerPC::ppcState.cr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_TU,	PowerPC::ppcState.TU);
	SetDlgItemText_Hex(hWnd, IDC_GPR_TL,	PowerPC::ppcState.TL);
	SetDlgItemText_Hex(hWnd, IDC_GPR_CTR,	PowerPC::CTR);
	SetDlgItemText_Hex(hWnd, IDC_GPR_DEC,	PowerPC::DEC);
	SetDlgItemText_Hex(hWnd, IDC_GPR_MSR,	PowerPC::ppcState.msr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_SRR0,	PowerPC::SRR0);
	SetDlgItemText_Hex(hWnd, IDC_GPR_SRR1,	PowerPC::SRR1);
	SetDlgItemText_Hex(hWnd, IDC_GPR_EXCEPTIONS, PowerPC::ppcState.Exceptions);
*/
}

void 
CRegisters::UpdateFPU(void)
{
	HWND hWnd = m_pWinDialog_FPU;
/*
	SetDlgItemText_Float(hWnd, IDC_FPU_R0, 0);
	SetDlgItemText_Float(hWnd, IDC_FPU_R1, 1);
	SetDlgItemText_Float(hWnd, IDC_FPU_R2, 2);
	SetDlgItemText_Float(hWnd, IDC_FPU_R3, 3);
	SetDlgItemText_Float(hWnd, IDC_FPU_R4, 4);
	SetDlgItemText_Float(hWnd, IDC_FPU_R5, 5);
	SetDlgItemText_Float(hWnd, IDC_FPU_R6, 6);
	SetDlgItemText_Float(hWnd, IDC_FPU_R7, 7);
	SetDlgItemText_Float(hWnd, IDC_FPU_R8, 8);
	SetDlgItemText_Float(hWnd, IDC_FPU_R9, 9);
	SetDlgItemText_Float(hWnd, IDC_FPU_R10, 10);
	SetDlgItemText_Float(hWnd, IDC_FPU_R11, 11);
	SetDlgItemText_Float(hWnd, IDC_FPU_R12, 12);
	SetDlgItemText_Float(hWnd, IDC_FPU_R13, 13);
	SetDlgItemText_Float(hWnd, IDC_FPU_R14, 14);
	SetDlgItemText_Float(hWnd, IDC_FPU_R15, 15);
	SetDlgItemText_Float(hWnd, IDC_FPU_R16, 16);
	SetDlgItemText_Float(hWnd, IDC_FPU_R17, 17);
	SetDlgItemText_Float(hWnd, IDC_FPU_R18, 18);
	SetDlgItemText_Float(hWnd, IDC_FPU_R19, 19);
	SetDlgItemText_Float(hWnd, IDC_FPU_R20, 20);
	SetDlgItemText_Float(hWnd, IDC_FPU_R21, 21);
	SetDlgItemText_Float(hWnd, IDC_FPU_R22, 22);
	SetDlgItemText_Float(hWnd, IDC_FPU_R23, 23);
	SetDlgItemText_Float(hWnd, IDC_FPU_R24, 24);
	SetDlgItemText_Float(hWnd, IDC_FPU_R25, 25);
	SetDlgItemText_Float(hWnd, IDC_FPU_R26, 26);
	SetDlgItemText_Float(hWnd, IDC_FPU_R27, 27);
	SetDlgItemText_Float(hWnd, IDC_FPU_R28, 28);
	SetDlgItemText_Float(hWnd, IDC_FPU_R29, 29);
	SetDlgItemText_Float(hWnd, IDC_FPU_R30, 30);
	SetDlgItemText_Float(hWnd, IDC_FPU_R31, 31);

	SetDlgItemText_Hex(hWnd, IDC_FPU_FPSCR,	(DWORD)PowerPC::ppcState.fpscr);
	SetDlgItemText_Hex(hWnd, IDC_FPU_HID2,	HID2.Hex);
*/	
	/*
	SetDlgItemText_Hex(hWnd, IDC_GPR_XER,	ppcState.xer);
	SetDlgItemText_Hex(hWnd, IDC_GPR_LR,	ppcState.lr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_CR,	ppcState.cr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_TU,	ppcState.tu);
	SetDlgItemText_Hex(hWnd, IDC_GPR_TL,	ppcState.tl);
	SetDlgItemText_Hex(hWnd, IDC_GPR_CTR,	ppcState.ctr);
	SetDlgItemText_Hex(hWnd, IDC_GPR_DEC,	ppcState.dec);
*/
}

// __________________________________________________________________________________________________
// UpdateDMA 
//
void 
CRegisters::UpdateDMA(void)
{
	HWND hWnd = m_pWinDialog_DMA;
/*
	SetDlgItemText_Hex(hWnd, IDC_SOURCE_ADDR_DMA0,	CPeripheralInterface::m_InterruptCause);
	SetDlgItemText_Hex(hWnd, IDC_DEST_ADDR_DMA0,	CPeripheralInterface::m_InterruptMask);

	SetDlgItemText_Hex(hWnd, IDC_SPRG0,				PowerPC::SPRG0);
	SetDlgItemText_Hex(hWnd, IDC_SPRG1,				PowerPC::SPRG1);
	SetDlgItemText_Hex(hWnd, IDC_SPRG2,				PowerPC::SPRG2);
	SetDlgItemText_Hex(hWnd, IDC_SPRG3,				PowerPC::SPRG3);

	SetDlgItemText_Hex(hWnd, IDC_FIFOCPUBASE,     CGPFifo::fifo.cpubegin);
	SetDlgItemText_Hex(hWnd, IDC_FIFOCPUEND,     CGPFifo::fifo.cpuend);
	SetDlgItemText_Hex(hWnd, IDC_FIFOCPUWRITEPTR,     CGPFifo::fifo.writeptr);

	SetDlgItemText_Hex(hWnd, IDC_FIFOGPBASE,     CGPFifo::fifo.gpbegin);
	SetDlgItemText_Hex(hWnd, IDC_FIFOGPEND,     CGPFifo::fifo.gpend);
	SetDlgItemText_Hex(hWnd, IDC_FIFOGPREADPTR,     CGPFifo::fifo.readptr);
	SetDlgItemText_Hex(hWnd, IDC_FIFOGPBREAKPT,     CGPFifo::fifo.breakpt);
	SetDlgItemText_Hex(hWnd, IDC_FIFOGPUNKNOWN,     CGPFifo::fifo.gpunknown);

	CheckDlgButton(hWnd,IDC_BPENABLE,CGPFifo::fifo.bpenable);*/
}

void SetDlgItemText_Hex(HWND _hDialog, int _iResource, u32 _iValue)
{
	char szBuffer[32];
	sprintf(szBuffer, "%08X", _iValue);
	SetDlgItemText(_hDialog, _iResource, szBuffer);
}

void SetDlgItemText_Float(HWND _hDialog, int _iResource, u32 _iReg)
{
	/*
	char szBuffer[128];
	if (CRegisters::m_bPaired)
	{
		sprintf(szBuffer, "%.4f %.4f", (float)PowerPC::ppcState.fpr[_iReg], (float)PowerPC::ppcState.ps1[_iReg]);
	}
	else
	{
		sprintf(szBuffer, "%.6e", *(double*)&PowerPC::ppcState.fpr[_iReg]);
	}
	SetDlgItemText(_hDialog, _iResource, szBuffer);*/
}