#pragma once

#include <algorithm>
#include "Common/CommonWindows.h"
#include "GPU/Common/GPUDebugInterface.h"

class CtrlDisplayListView
{
	HWND wnd;
	RECT rect;
	static LPCTSTR windowClass;
	DisplayList list;
	
	HFONT font;
	HFONT boldfont;
	u32 windowStart;
	u32 curAddress;
	u32 selectRangeStart;
	u32 selectRangeEnd;

	int visibleRows;
	int rowHeight;
	int instructionSize;
	bool hasFocus;
	bool validDisplayList;

	struct {
		int addressStart;
		int opcodeStart;
	} pixelPositions;

	void toggleBreakpoint();
	void PromptBreakpointCond();

public:
	CtrlDisplayListView(HWND _wnd);
	~CtrlDisplayListView();
	static void registerClass();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlDisplayListView * getFrom(HWND wnd);
	
	HWND GetHWND() {
		return wnd;
	}

	void onPaint(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onVScroll(WPARAM wParam, LPARAM lParam);

	void redraw();
	void setDisplayList(DisplayList& displayList)
	{
		validDisplayList = true;
		list = displayList;
		gotoAddr(list.pc);
	}
	void clearDisplayList()
	{
		validDisplayList = false;
	}

	void scrollWindow(int lines)
	{
		windowStart += lines*instructionSize;
		redraw();
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

	void setCurAddress(u32 newAddress, bool extend = false)
	{
		u32 after = newAddress + instructionSize;
		curAddress = newAddress;
		selectRangeStart = extend ? std::min(selectRangeStart, newAddress) : newAddress;
		selectRangeEnd = extend ? std::max(selectRangeEnd, after) : after;
	}

	void scrollAddressIntoView();
	bool curAddressIsVisible();
};
