// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#ifndef _DISASM_H
#define _DISASM_H

#include "../W32Util/DialogManager.h"
#include "CtrlDisasmView.h"
#include "../../Core/MIPS/MIPSDebugInterface.h"
#include "CPURegsInterface.h"
#include "../../Globals.h"
#include "../../Core/CPU.h"

#include <windows.h>

class CDisasm : public Dialog
{
private:
	RECT minRect;
	RECT regRect;
	RECT disRect;
	
	DebugInterface *cpu;
	
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

public:
	int index; //helper 

	CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu);
	~CDisasm();
	//
	// --- tools ---
	//
	// Update Dialog
	void UpdateDialog(bool _bComplete = false);
	// SetDebugMode 
	void SetDebugMode(bool _bDebug);
	// show dialog
	void Goto(u32 addr);
	void NotifyMapLoaded();
};

#endif