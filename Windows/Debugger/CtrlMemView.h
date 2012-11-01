// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once

//////////////////////////////////////////////////////////////////////////
//CtrlDisAsmView
// CtrlDisAsmView.cpp
//////////////////////////////////////////////////////////////////////////
//This Win32 control is made to be flexible and usable with
//every kind of CPU architecture that has fixed width instruction words.
//Just supply it an instance of a class derived from Debugger, with all methods
//overridden for full functionality.
//
//To add to a dialog box, just draw a User Control in the dialog editor,
//and set classname to "CtrlDisAsmView". you also need to call CtrlDisAsmView::init()
//before opening this dialog, to register the window class.
//
//To get a class instance to be able to access it, just use getFrom(HWND wnd).

#include "../../Core/Debugger/DebugInterface.h"

enum MemViewMode
{
	MV_NORMAL,
	MV_SYMBOLS,
	MV_MAX
};

class CtrlMemView
{
	HWND wnd;
	HFONT font;
	RECT rect;

	int curAddress;
	int align;
	int alignMul;
	int rowHeight;

	int selection;
	int oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	static TCHAR szClassName[];
	DebugInterface *debugger;
	MemViewMode mode;
public:
	CtrlMemView(HWND _wnd);
	~CtrlMemView();
	static void init();
	static void deinit();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlMemView * getFrom(HWND wnd);

	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		if (debugger)
			align=debugger->getInstructionSize(0);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}

	void onPaint(WPARAM wParam, LPARAM lParam);
	void onVScroll(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void redraw();

	void setMode(MemViewMode m)
	{
		mode=m;
		switch(mode) {
		case MV_NORMAL:
			alignMul=4;
			break;
		case MV_SYMBOLS:
			alignMul=1;
			break;
		default:
			break;
		}
		redraw();
	}
	void setAlign(int l)
	{
		align=l;
	}
	int yToAddress(int y);
	void gotoAddr(unsigned int addr)
	{
		curAddress=addr&(~(align-1));
		redraw();
	}
	unsigned int getSelection()
	{
		return curAddress;
	}
};