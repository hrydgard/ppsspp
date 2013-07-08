// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#ifndef _DISASM_H
#define _DISASM_H

#include "../W32Util/DialogManager.h"
#include "CtrlDisasmView.h"
#include "Debugger_Lists.h"
#include "CPURegsInterface.h"
#include "Globals.h"
#include "Core/CPU.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/Breakpoints.h"
#include <vector>

#include <windows.h>

class CDisasm : public Dialog
{
private:
	RECT defaultRect;
	RECT defaultBreakpointRect;
	RECT regRect;
	RECT disRect;
	RECT breakpointRect;

	DebugInterface *cpu;
	u64 lastTicks;

	CtrlBreakpointList* breakpointList;
	CtrlThreadList* threadList;
	std::vector<BreakPoint> displayedBreakPoints_;
	std::vector<MemCheck> displayedMemChecks_;

	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
	void UpdateSize(WORD width, WORD height);
	void SavePosition();
	void updateThreadLabel(bool clear);
public:
	int index; //helper 

	CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu);
	~CDisasm();
	//
	// --- tools ---
	//
	
	virtual void Update()
	{
		UpdateDialog(true);
		breakpointList->update();
	};
	void UpdateDialog(bool _bComplete = false);
	// SetDebugMode 
	void SetDebugMode(bool _bDebug);
	// show dialog
	void Goto(u32 addr);
	void NotifyMapLoaded();
};

#endif