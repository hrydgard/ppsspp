#pragma once
#include <string>
#include "Common/CommonWindows.h"
#include "Common/CommonTypes.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/Breakpoints.h"

class BreakpointWindow
{
	HWND parentHwnd;
	DebugInterface* cpu;

	bool memory;
	bool read;
	bool write;
	bool enabled;
	bool log;
	bool onChange;
	u32 address;
	u32 size;
	std::string condition;
	std::string logFormat;
	PostfixExpression compiledCondition;

	bool fetchDialogData(HWND hwnd);
	bool GetCheckState(HWND hwnd, int dlgItem);

	static INT_PTR CALLBACK StaticDlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	INT_PTR DlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

public:
	BreakpointWindow(HWND parent, DebugInterface* cpu): cpu(cpu)
	{
		parentHwnd = parent;
		memory = true;
		onChange = false;
		read = write = true;
		enabled = log = true;
		address = -1;
		size = 1;
	};

	bool exec();
	bool isMemoryBreakpoint() { return memory; };

	void addBreakpoint();
	void loadFromMemcheck(const MemCheck &memcheck);
	void loadFromBreakpoint(const BreakPoint &bp);
	void initBreakpoint(u32 address);
};
