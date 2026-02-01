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

#include "Core/MIPS/MIPSDebugInterface.h"

class CtrlRegisterList {
	HWND wnd;
	HFONT font;
	RECT rect;

	int rowHeight;
	int selection = 0;
	int category = 0;

	int oldSelection = 0;
	
	bool selecting = false;
	bool hasFocus = false;
	MIPSDebugInterface *cpu = nullptr;

	u32 lastPC = 0;
	u32 *lastCat0Values = nullptr;
	bool *changedCat0Regs = nullptr;
	bool ctrlDown = false;

	u32 getSelectedRegValue(char *out, size_t size);
	void copyRegisterValue();
	void editRegisterValue();
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

	void setCPU(MIPSDebugInterface *deb) {
		cpu = deb;
		constexpr int regs = MIPSDebugInterface::GetNumRegsInCategory(0);
		lastCat0Values = new u32[regs+3];
		changedCat0Regs = new bool[regs+3];
		memset(lastCat0Values, 0, (regs+3) * sizeof(u32));
		memset(changedCat0Regs, 0, (regs+3) * sizeof(bool));
	}
	MIPSDebugInterface *getCPU()
	{
		return cpu;
	}

private:
	bool redrawScheduled_ = false;
};
