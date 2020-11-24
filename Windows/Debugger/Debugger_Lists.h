#pragma once

#include "../../Core/Debugger/DebugInterface.h"
#include "../../Core/HLE/sceKernelThread.h"
#include "../../Core/Debugger/Breakpoints.h"
#include "../../Core/Debugger/SymbolMap.h"
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

class CtrlBreakpointList: public GenericListControl
{
public:
	CtrlBreakpointList(HWND hwnd, DebugInterface* cpu, CtrlDisAsmView* disasm);
	void reloadBreakpoints();
	void showMenu(int itemIndex, const POINT &pt);
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue);
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return getTotalBreakpointCount(); };
	virtual void OnDoubleClick(int itemIndex, int column);
	virtual void OnRightClick(int itemIndex, int column, const POINT& point);
	virtual void OnToggle(int item, bool newValue);
private:
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
};

class CtrlStackTraceView: public GenericListControl
{
public:
	CtrlStackTraceView(HWND hwnd, DebugInterface* cpu, CtrlDisAsmView* disasm);
	void loadStackTrace();
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue);
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return (int)frames.size(); };
	virtual void OnDoubleClick(int itemIndex, int column);
private:
	std::vector<MIPSStackWalk::StackFrame> frames;
	DebugInterface* cpu;
	CtrlDisAsmView* disasm;
};

class CtrlModuleList: public GenericListControl
{
public:
	CtrlModuleList(HWND hwnd, DebugInterface* cpu);
	void loadModules();
protected:
	virtual bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& returnValue);
	virtual void GetColumnText(wchar_t* dest, int row, int col);
	virtual int GetRowCount() { return (int)modules.size(); };
	virtual void OnDoubleClick(int itemIndex, int column);
private:
	std::vector<LoadedModuleInfo> modules;
	DebugInterface* cpu;
};