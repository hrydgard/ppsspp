// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once

//////////////////////////////////////////////////////////////////////////
//CtrlDisAsmView
// CtrlDisAsmView.cpp
//////////////////////////////////////////////////////////////////////////
//This Win32 control is made to be flexible and usable with
//every kind of CPU architechture that has fixed width instruction words.
//Just supply it an instance of a class derived from Debugger, with all methods
//overridden for full functionality. Look at the ppc one for an example.
//
//To add to a dialog box, just draw a User Control in the dialog editor,
//and set classname to "CtrlDisAsmView". you also need to call CtrlDisAsmView::init()
//before opening this dialog, to register the window class.
//
//To get a class instance to be able to access it, just use 
//  CtrlDisAsmView::getFrom(GetDlgItem(yourdialog, IDC_yourid)).

#include "../../Core/Debugger/DebugInterface.h"

#include "Common/CommonWindows.h"
#include <vector>
#include <algorithm>

using std::min;
using std::max;


class CtrlDisAsmView
{
	HWND wnd;
	HFONT font;
	HFONT boldfont;
	RECT rect;

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
	int instructionSize;
	bool whiteBackground;
	bool displaySymbols;
	u32 branchTarget;
	int branchRegister;

	struct {
		int addressStart;
		int opcodeStart;
		int argumentsStart;
		int arrowsStart;
	} pixelPositions;

	std::vector<u32> jumpStack;

	bool controlHeld;
	std::string searchQuery;
	int matchAddress;
	bool searching;
	bool dontRedraw;
	bool keyTaken;

	void assembleOpcode(u32 address, std::string defaultText);
	void disassembleToFile();
	void search(bool continueSearch);
	void followBranch();
	void calculatePixelPositions();
	bool getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels);
	void parseDisasm(const char* disasm, char* opcode, char* arguments);
	void updateStatusBarText();
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
	
	void getOpcodeText(u32 address, char* dest);
	u32 yToAddress(int y);

	void setDontRedraw(bool b) { dontRedraw = b; };
	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		curAddress=debugger->getPC();
		instructionSize=debugger->getInstructionSize(0);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}
	void gotoAddr(unsigned int addr)
	{
		u32 windowEnd = windowStart+visibleRows*instructionSize;
		u32 newAddress = addr&(~(instructionSize-1));

		if (newAddress < windowStart || newAddress >= windowEnd)
		{
			windowStart = newAddress-visibleRows/2*instructionSize;
		}

		setCurAddress(newAddress);
		redraw();
	}
	void gotoPC()
	{
		gotoAddr(debugger->getPC()&(~(instructionSize-1)));
	}
	u32 getSelection()
	{
		return curAddress;
	}

	void setShowMode(bool s)
	{
		showHex=s;
	}

	void toggleBreakpoint();

	void scrollWindow(int lines)
	{
		windowStart += lines*instructionSize;
		redraw();
	}

	void setCurAddress(u32 newAddress, bool extend = false)
	{
		u32 after = newAddress + instructionSize;
		curAddress = newAddress;
		selectRangeStart = extend ? std::min(selectRangeStart, newAddress) : newAddress;
		selectRangeEnd = extend ? std::max(selectRangeEnd, after) : after;
		updateStatusBarText();
	}
};