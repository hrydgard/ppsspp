// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once
#include "../W32Util/DialogManager.h"

#include "../../Core/MemMap.h"
#include "../../Core/CPU.h"

#include "../../Core/Debugger/DebugInterface.h"

#include <windows.h>

class CMemoryDlg : public Dialog
{
private:
	DebugInterface *cpu;
	static RECT slRect;

	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
public:
	int index; //helper 

	// constructor
	CMemoryDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu);
	
	// destructor
	~CMemoryDlg(void);
	
	void Goto(u32 addr);
	void Update(void);	
	void NotifyMapLoaded();

	void Size(void);
};


