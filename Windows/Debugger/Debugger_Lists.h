#pragma once

#include "../../Core/Debugger/DebugInterface.h"
#include "../../Core/HLE/sceKernelThread.h"
#include "../../Core/Debugger/Breakpoints.h"
#include "../../Core/Debugger/SymbolMap.h"
#include "../../Core/MIPS/MIPSStackWalk.h"
#include "Windows/W32Util/Misc.h"

enum class WatchFormat {
	HEX,
	INT,
	FLOAT,
	STR,
};

class CtrlThreadList: public GenericListControl
{
public:
	CtrlThreadList(HWND hwnd);
	void reloadThreads();
	void showMenu(int itemIndex, const POINT &pt);
	const char* getCurrentThreadName();
protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return (int) threads.size(); }
	void OnDoubleClick(int itemIndex, int column) override;
	void OnRightClick(int itemIndex, int column, const POINT &point) override;
private:
	std::vector<DebugThreadInfo> threads;
};

class CtrlDisAsmView;

class CtrlBreakpointList: public GenericListControl
{
public:
	CtrlBreakpointList(HWND hwnd, DebugInterface* cpu, CtrlDisAsmView* disasm);
	void reloadBreakpoints();
protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return getTotalBreakpointCount(); }
	void OnDoubleClick(int itemIndex, int column) override;
	void OnRightClick(int itemIndex, int column, const POINT &point) override;
	void OnToggle(int item, bool newValue) override;
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
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return (int)frames.size(); }
	void OnDoubleClick(int itemIndex, int column) override;
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
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return (int)modules.size(); }
	void OnDoubleClick(int itemIndex, int column) override;
private:
	std::vector<LoadedModuleInfo> modules;
	DebugInterface* cpu;
};

class CtrlWatchList : public GenericListControl {
public:
	CtrlWatchList(HWND hwnd, DebugInterface *cpu);
	void RefreshValues();

protected:
	bool WindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT &returnValue) override;
	void GetColumnText(wchar_t *dest, int row, int col) override;
	int GetRowCount() override { return (int)watches_.size(); }
	void OnRightClick(int itemIndex, int column, const POINT &point) override;
	bool ListenRowPrePaint() override { return true; }
	bool OnRowPrePaint(int row, LPNMLVCUSTOMDRAW msg) override;

private:
	void AddWatch();
	void EditWatch(int pos);
	void DeleteWatch(int pos);
	bool HasWatchChanged(int pos);

	struct WatchInfo {
		std::string name;
		std::string originalExpression;
		PostfixExpression expression;
		WatchFormat format = WatchFormat::HEX;
		uint32_t currentValue = 0;
		uint32_t lastValue = 0;
		int steppingCounter = -1;
		bool evaluateFailed = false;
	};
	std::vector<WatchInfo> watches_;
	DebugInterface *cpu_;
};
