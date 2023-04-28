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

#include <cstdint>
#include <vector>
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/MemBlockInfo.h"

enum OffsetSpacing {
	offsetSpace = 3, // the number of blank lines that should be left to make space for the offsets
	offsetLine  = 1, // the line on which the offsets should be written
};

enum CommonToggles {
	On,
	Off,
};

class CtrlMemView
{
public:
	CtrlMemView(HWND _wnd);
	~CtrlMemView();
	static void init();
	static void deinit();
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static CtrlMemView *getFrom(HWND wnd);

	void setDebugger(DebugInterface *deb) {
		debugger_ = deb;
	}
	DebugInterface *getDebugger() {
		return debugger_;
	}
	std::vector<u32> searchString(const std::string &searchQuery);
	void onPaint(WPARAM wParam, LPARAM lParam);
	void onVScroll(WPARAM wParam, LPARAM lParam);
	void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onChar(WPARAM wParam, LPARAM lParam);
	void onMouseDown(WPARAM wParam, LPARAM lParam, int button);
	void onMouseUp(WPARAM wParam, LPARAM lParam, int button);
	void onMouseMove(WPARAM wParam, LPARAM lParam, int button);
	void redraw();
	void gotoAddr(unsigned int addr);

	void drawOffsetScale(HDC hdc);
	void toggleOffsetScale(CommonToggles toggle);
	void setHighlightType(MemBlockFlags flags);

private:
	bool ParseSearchString(const std::string &query, bool asHex, std::vector<uint8_t> &data);
	void updateStatusBarText();
	void search(bool continueSearch);
	uint32_t pickTagColor(const std::string &tag);

	enum class GotoMode {
		RESET,
		RESET_IF_OUTSIDE,
		FROM_CUR,
		EXTEND,
	};
	static GotoMode GotoModeFromModifiers(bool isRightClick);
	void UpdateSelectRange(uint32_t target, GotoMode mode);
	void GotoPoint(int x, int y, GotoMode mode);
	void ScrollWindow(int lines, GotoMode mdoe);
	void ScrollCursor(int bytes, GotoMode mdoe);

	static wchar_t szClassName[];
	DebugInterface *debugger_ = nullptr;

	HWND wnd;
	HFONT font;
	HFONT underlineFont;

	bool redrawScheduled_ = false;
	// Whether to draw things using focused styles.
	bool hasFocus_ = false;
	MemBlockFlags highlightFlags_ = MemBlockFlags::ALLOC;

	// Current cursor position.
	uint32_t curAddress_ = 0;
	// Selected range, which should always be around the cursor.
	uint32_t selectRangeStart_ = 0;
	uint32_t selectRangeEnd_ = 0;
	// Last select reset position, for selecting ranges.
	uint32_t lastSelectReset_ = 0;
	// Address of the first displayed byte.
	uint32_t windowStart_ = 0;
	// Number of bytes displayed per row.
	int rowSize_ = 16;

	// Width of one monospace character (to maintain grid.)
	int charWidth_ = 0;
	// Height of one row of bytes.
	int rowHeight_ = 0;
	// Y position of offset header (at top.)
	int offsetPositionY_;
	// X position of addresses (at left.)
	int addressStartX_ = 0;
	// X position of hex display.
	int hexStartX_ = 0;
	// X position of text display.
	int asciiStartX_ = 0;
	// Whether cursor is within text display or hex display.
	bool asciiSelected_ = false;
	// Which nibble is selected, if in hex display.  0 means leftmost, i.e. most significant.
	int selectedNibble_ = 0;

	bool displayOffsetScale_ = false;

	// Number of rows visible as of last redraw.
	int visibleRows_ = 0;
	// Position and size as of last redraw.
	RECT rect_;

	// Last used search query, used when continuing a search.
	std::string searchQuery_;
	// Address of last match when continuing search.
	uint32_t matchAddress_ = 0xFFFFFFFF;
	// Whether a search is in progress.
	bool searching_ = false;
};
