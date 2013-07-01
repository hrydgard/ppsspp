#pragma once
#include <windows.h>
#include "Common/CommonTypes.h"
#include "../../Core/Debugger/DebugInterface.h"
#include "Core/Debugger/Breakpoints.h"

class BreakpointWindow
{
	HWND parentHwnd;
	DebugInterface* cpu;
	bool ctrlDown;

	bool memory;
	bool read;
	bool write;
	bool enabled;
	bool log;
	u32 address;
	u32 size;
	char condition[128];
	PostfixExpression compiledCondition;

	static BreakpointWindow* bp;
	bool fetchDialogData(HWND hwnd);
public:
	BreakpointWindow(HWND parent, DebugInterface* cpu): cpu(cpu)
	{
		parentHwnd = parent;
		memory = false;
		read = write = true;
		enabled = log = true;
		address = -1;
		size = 1;
		condition[0] = 0;

		ctrlDown = false;
	};


	static INT_PTR CALLBACK dlgFunc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	bool exec();
	bool isMemoryBreakpoint() { return memory; };

	void addMemcheck();
	void addBreakpoint();
	void editMemcheck(MemCheck& memcheck);
	void editBreakpoint(BreakPoint& memcheck);
	void loadFromMemcheck(MemCheck& memcheck);
	void loadFromBreakpoint(BreakPoint& memcheck);

};