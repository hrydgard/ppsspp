#pragma once

#include "Windows/W32Util/DialogManager.h"
#include "Core/MemMap.h"
#include "Core/Debugger/DebugInterface.h"

class CVFPUDlg : public Dialog {
public:
	CVFPUDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu_);
	~CVFPUDlg();

	void Goto(u32 addr);
	void Update() override;
	void Size();

private:
	DebugInterface *cpu;
	HFONT font;
	int mode;
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;
};
