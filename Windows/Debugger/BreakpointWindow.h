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

	static BreakpointWindow* bp;
	bool fetchDialogData(HWND hwnd);
	bool GetCheckState(HWND hwnd, int dlgItem);

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


	static INT_PTR CALLBACK dlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	bool exec();
	bool isMemoryBreakpoint() { return memory; };

	void addBreakpoint();
	void loadFromMemcheck(MemCheck& memcheck);
	void loadFromBreakpoint(BreakPoint& memcheck);
	void initBreakpoint(u32 address);
};
