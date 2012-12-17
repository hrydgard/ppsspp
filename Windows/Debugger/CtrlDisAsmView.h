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

#include <windows.h>

class CtrlDisAsmView
{
	HWND wnd;
	HFONT font;
	HFONT boldfont;
	RECT rect;

	int curAddress;
	int align;
	int rowHeight;

	int selection;
	int marker;
	int oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;
	DebugInterface *debugger;
	static TCHAR szClassName[];

public:
	CtrlDisAsmView(HWND _wnd);
	~CtrlDisAsmView();
	static void init();
	static void deinit();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlDisAsmView * getFrom(HWND wnd);
	
	void onPaint(WPARAM wParam, LPARAM lParam);
	void onVScroll(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void redraw();

	void setAlign(int l)
	{
		align=l;
	}
	int yToAddress(int y);

	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		curAddress=debugger->getPC();
		align=debugger->getInstructionSize(0);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}
	void gotoAddr(unsigned int addr)
	{
		curAddress=addr&(~(align-1));
		redraw();
	}
	void gotoPC()
	{
		curAddress=debugger->getPC()&(~(align-1));
		redraw();
	}
	unsigned int getSelection()
	{
		return curAddress;
	}

	void setShowMode(bool s)
	{
		showHex=s;
	}

	void toggleBreakpoint()
	{
		debugger->toggleBreakpoint(curAddress);
		redraw();
	}
};