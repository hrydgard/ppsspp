#pragma once

#include "../../Core/Debugger/DebugInterface.h"
#include "../../Core/HLE/sceKernelThread.h"
#include "../../Core/Debugger/Breakpoints.h"
#include "../../Core/MIPS/MIPSStackWalk.h"
#include "Windows/W32Util/Misc.h"

class CtrlThreadList: public GenericListControl
{
public:
	CtrlThreadList(HWND hwnd);
	void reloadThreads();
	void showMenu(int itemIndex, const POINT &pt);
	const char* getCurrentThreadName();
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue);
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return (int) threads.size(); };
	virtual void OnDoubleClick(int itemIndex, int column);
	virtual void OnRightClick(int itemIndex, int column, const POINT& point);
private:
	std::vector<DebugThreadInfo> threads;
};

class CtrlDisAsmView;

class CtrlBreakpointList
{
	HWND wnd;
	WNDPROC oldProc;
	std::vector<BreakPoint> displayedBreakPoints_;
	std::vector<MemCheck> displayedMemChecks_;
	std::wstring breakpointText;
	DebugInterface* cpu;
	CtrlDisAsmView* disasm;

	void editBreakpoint(int itemIndex);
	void gotoBreakpointAddress(int itemIndex);
	void removeBreakpoint(int itemIndex);
	int getTotalBreakpointCount();
	int getBreakpointIndex(int itemIndex, bool& isMemory);
	void showBreakpointMenu(int itemIndex, const POINT &pt);
	void toggleEnabled(int itemIndex);
public:
	void setCpu(DebugInterface* cpu)
	{
		this->cpu = cpu;
	};
	void setDisasm(CtrlDisAsmView* disasm)
	{
		this->disasm = disasm;
	};
	void update();
	void setDialogItem(HWND hwnd);
	void handleNotify(LPARAM lParam);
	void showMenu(int itemIndex, const POINT &pt);
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

class CtrlStackTraceView
{
	HWND wnd;
	WNDPROC oldProc;
	std::vector<MIPSStackWalk::StackFrame> frames;
	DebugInterface* cpu;
	CtrlDisAsmView* disasm;
	wchar_t stringBuffer[256];

public:
	void setCpu(DebugInterface* cpu)
	{
		this->cpu = cpu;
	};
	void setDisasm(CtrlDisAsmView* disasm)
	{
		this->disasm = disasm;
	};
	void setDialogItem(HWND hwnd);
	void loadStackTrace();
	void handleNotify(LPARAM lParam);
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};