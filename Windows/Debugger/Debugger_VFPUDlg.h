// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once
#include "../W32Util/DialogManager.h"

#include "../../Core/MemMap.h"
#include "../../Core/CPU.h"

#include "../../Core/Debugger/DebugInterface.h"

class CVFPUDlg : public Dialog
{
private:
	DebugInterface *cpu;
	HFONT font;
	int mode;
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
public:
	int index; //helper 

	CVFPUDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu_);
	~CVFPUDlg();
	
	void Goto(u32 addr);
	void Update();	
	void Size();
};


extern CVFPUDlg *vfpudlg;
