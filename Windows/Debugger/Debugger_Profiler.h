// __________________________________________________________________________________________________
//
//			$Archive:  $
//			$Revision: 1.7 $
//			$Author: tronic $ 
//			$Modtime:  $  
//
/////////////////////////////////////////////////////////////////////////////////////////////////////
// M O D U L E  B E G I N ///////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _REGISTERS_H
#define _REGISTERS_H

#include "../W32Util/TabControl.h"


class CRegisters
{
private:
	
	static HINSTANCE		m_hInstance;
	static HWND				m_hParent;
	
	static HWND m_hDlg;
	static W32Util::TabControl *m_pWinTabCtrl;

	static HWND		m_pWinDialog_GPR;
	static HWND		m_pWinDialog_DMA;
	static HWND		m_pWinDialog_FPU;
	static HWND		m_pWinDialog_TIMER;

		
	static BOOL CALLBACK RegistersDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	
	// update regs
	static void UpdateGPR(void);

	// update dma regs
	static void UpdateDMA(void);

	// update dma regs
	static void UpdateFPU(void);
	
	static void changeReg(DWORD control, int reg);
public:
	static BOOL				m_bPaired;
	
	// constructor
	static void Init(HINSTANCE _hInstance, HWND _hParent);
	
	// destructor
	static void DeInit(void);
	
	//
	// --- tools ---
	//
		
	// show
	static void Show(bool _bShow);

	// show
	static void Update(void);	
};


#endif