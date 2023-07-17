#pragma once
#include <string>
#include "Common/CommonWindows.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/MemMap.h"

class EditSymbolsWindow {
	HWND parentHwnd;
	DebugInterface* cpu;

	bool scan_;
	u32 address_;
	u32 size_;

	void Scan();
	void Remove();

	bool GetCheckState(HWND hwnd, int dlgItem);
	bool fetchDialogData(HWND hwnd);

	static INT_PTR CALLBACK StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	INT_PTR DlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

public:
	EditSymbolsWindow(HWND parent, DebugInterface* cpu) : cpu(cpu) {
		parentHwnd = parent;
		scan_ = true;
		address_ = -1;
		size_ = 1;
	}

	bool exec();
	void eval();
};
