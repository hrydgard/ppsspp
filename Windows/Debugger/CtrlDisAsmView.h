#pragma once

// CtrlDisAsmView
//  
// This Win32 control is made to be flexible and usable with
// every kind of CPU architecture that has fixed width instruction words.
// Just supply it an instance of a class derived from Debugger, with all methods
// overridden for full functionality. Look at the ppc one for an example.
// 
// To add to a dialog box, just draw a User Control in the dialog editor,
// and set classname to "CtrlDisAsmView". you also need to call CtrlDisAsmView::init()
// before opening this dialog, to register the window class.
// 
// To get a class instance to be able to access it, just use 
//   CtrlDisAsmView::getFrom(GetDlgItem(yourdialog, IDC_yourid)).

#include <vector>
#include <algorithm>

#include "Common/CommonWindows.h"
#include "Common/Log.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"

class CtrlDisAsmView
{
	HWND wnd;
	HFONT font;
	HFONT boldfont;
	RECT rect;

	DisassemblyManager manager;
	u32 curAddress;
	u32 selectRangeStart;
	u32 selectRangeEnd;
	int rowHeight;
	int charWidth;

	bool hasFocus;
	bool showHex;
	DebugInterface *debugger;
	static TCHAR szClassName[];

	u32 windowStart;
	int visibleRows;
	bool whiteBackground;
	bool displaySymbols;

	struct {
		int addressStart;
		int opcodeStart;
		int argumentsStart;
		int arrowsStart;
	} pixelPositions;

	std::vector<u32> jumpStack;

	std::string searchQuery;
	int matchAddress;
	bool searching;
	bool dontRedraw;
	bool keyTaken;

	enum class CopyInstructionsMode {
		OPCODES,
		DISASM,
		ADDRESSES,
	};

	void assembleOpcode(u32 address, const std::string &defaultText);
	std::string disassembleRange(u32 start, u32 size);
	void disassembleToFile();
	void search(bool continueSearch);
	void followBranch();
	void calculatePixelPositions();
	bool getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels, bool showData);
	void updateStatusBarText();
	void drawBranchLine(HDC hdc, std::map<u32, int> &addressPositions, const BranchLine &line);
	void CopyInstructions(u32 startAddr, u32 endAddr, CopyInstructionsMode mode);
	void NopInstructions(u32 startAddr, u32 endAddr);
	std::set<std::string> getSelectedLineArguments();
	void drawArguments(HDC hdc, const DisassemblyLineInfo &line, int x, int y, int textColor, const std::set<std::string> &currentArguments);

public:
	CtrlDisAsmView(HWND _wnd);
	~CtrlDisAsmView();
	static void init();
	static void deinit();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlDisAsmView * getFrom(HWND wnd);
	
	void onChar(WPARAM wParam, LPARAM lParam);
	void onPaint(WPARAM wParam, LPARAM lParam);
	void onVScroll(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onKeyUp(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void scrollAddressIntoView();
	bool curAddressIsVisible();
	void redraw();
	void scanFunctions();
	void clearFunctions() { manager.clear(); };

	void getOpcodeText(u32 address, char* dest, int bufsize);
	int getRowHeight() { return rowHeight; };
	u32 yToAddress(int y);

	void setDontRedraw(bool b) { dontRedraw = b; };
	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		curAddress=debugger->getPC();
		manager.setCpu(deb);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}

	void scrollStepping(u32 newPc);
	u32 getInstructionSizeAt(u32 address);

	void gotoAddr(unsigned int addr)
	{
		if (positionLocked_ != 0)
			return;
		u32 windowEnd = manager.getNthNextAddress(windowStart,visibleRows);
		u32 newAddress = manager.getStartAddress(addr);

		if (newAddress < windowStart || newAddress >= windowEnd)
		{
			windowStart = manager.getNthPreviousAddress(newAddress,visibleRows/2);
		}

		setCurAddress(newAddress);
		scanFunctions();
		redraw();
	}
	void gotoPC()
	{
		gotoAddr(debugger->getPC());
	}
	u32 getSelection()
	{
		return curAddress;
	}

	void setShowMode(bool s)
	{
		showHex=s;
	}

	void toggleBreakpoint(bool toggleEnabled = false);
	void editBreakpoint();

	void scrollWindow(int lines)
	{
		if (lines < 0)
			windowStart = manager.getNthPreviousAddress(windowStart,abs(lines));
		else
			windowStart = manager.getNthNextAddress(windowStart,lines);

		scanFunctions();
		redraw();
	}

	void setCurAddress(u32 newAddress, bool extend = false)
	{
		newAddress = manager.getStartAddress(newAddress);
		u32 after = manager.getNthNextAddress(newAddress,1);
		curAddress = newAddress;
		selectRangeStart = extend ? std::min(selectRangeStart, newAddress) : newAddress;
		selectRangeEnd = extend ? std::max(selectRangeEnd, after) : after;
		updateStatusBarText();
	}

	void LockPosition() {
		positionLocked_++;
	}
	void UnlockPosition() {
		positionLocked_--;
		_assert_(positionLocked_ >= 0);
	}

private:
	bool redrawScheduled_ = false;
	int positionLocked_ = 0;
};
