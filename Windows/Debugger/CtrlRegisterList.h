// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once

//////////////////////////////////////////////////////////////////////////
//CtrlRegisterList
// CtrlRegisterList.cpp
//////////////////////////////////////////////////////////////////////////
//This Win32 control is made to be flexible and usable with
//every kind of CPU architechture that has fixed width instruction words.
//Just supply it an instance of a class derived from Debugger, with all methods
//overridden for full functionality. Look at the ppc one for an example.
//
//To add to a dialog box, just draw a User Control in the dialog editor,
//and set classname to "CtrlRegisterList". you also need to call CtrlRegisterList::init()
//before opening this dialog, to register the window class.
//
//To get a class instance to be able to access it, just use 
//  CtrlRegisterList::getFrom(GetDlgItem(yourdialog, IDC_yourid)).

#include "../../Core/Debugger/DebugInterface.h"

class CtrlRegisterList
{
	HWND wnd;
	HFONT font;
	RECT rect;

	int rowHeight;
	int selection;
	int marker;
	int category;

	int oldSelection;
	
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;
	DebugInterface *cpu;
	static TCHAR szClassName[];

	u32 lastPC;
	u32 *lastCat0Values;
	bool *changedCat0Regs;

public:
	CtrlRegisterList(HWND _wnd);
	~CtrlRegisterList();
	static void init();
	static void deinit();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlRegisterList * getFrom(HWND wnd);
	
	void onPaint(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void redraw();

	int yToIndex(int y);

	void setCPU(DebugInterface *deb)
	{
		cpu = deb;

		int regs = cpu->GetNumRegsInCategory(0);
		lastCat0Values = new u32[regs];
		changedCat0Regs = new bool[regs];
		memset(lastCat0Values, 0, regs * sizeof(u32));
		memset(changedCat0Regs, 0, regs * sizeof(bool));
	}
	DebugInterface *getCPU()
	{
		return cpu;
	}
};