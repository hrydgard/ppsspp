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

enum OffsetSpacing {
	offsetSpace = 3, // the number of blank lines that should be left to make space for the offsets
	offsetLine  = 1, // the line on which the offsets should be written
};

enum OffsetToggles {
	On,
	Off,
};

class CtrlMemView
{
	HWND wnd;
	HFONT font;
	HFONT underlineFont;
	RECT rect;

	unsigned int curAddress;
	unsigned int windowStart;
	int rowHeight;
	int rowSize;
	int offsetPositionY;

	int addressStart;
	int charWidth;
	int hexStart;
	int asciiStart;
	bool asciiSelected;
	int selectedNibble;

	bool displayOffsetScale = false;

	int visibleRows;
	
	std::string searchQuery;
	int matchAddress;
	bool searching;

	bool hasFocus;
	static wchar_t szClassName[];
	DebugInterface *debugger;
	void updateStatusBarText();
	void search(bool continueSearch);
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
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}

	void onPaint(WPARAM wParam, LPARAM lParam);
	void onVScroll(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onChar(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void redraw();

	void gotoPoint(int x, int y);
	void gotoAddr(unsigned int addr);
	void scrollWindow(int lines);
	void scrollCursor(int bytes);

	void drawOffsetScale(HDC hdc);
	void toggleOffsetScale(OffsetToggles toggle);
};