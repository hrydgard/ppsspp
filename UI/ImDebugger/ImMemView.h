#pragma once


#include <cstdint>
#include <vector>
#include "ext/imgui/imgui.h"

#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/MemBlockInfo.h"

enum OffsetSpacing {
	offsetSpace = 3, // the number of blank lines that should be left to make space for the offsets
	offsetLine = 1, // the line on which the offsets should be written
};

enum CommonToggles {
	On,
	Off,
};

class ImMemView {
public:
	ImMemView();
	~ImMemView();

	void setDebugger(MIPSDebugInterface *deb) {
		debugger_ = deb;
	}
	MIPSDebugInterface *getDebugger() {
		return debugger_;
	}
	std::vector<u32> searchString(const std::string &searchQuery);
	void Draw(ImDrawList *drawList);
	// void onVScroll(WPARAM wParam, LPARAM lParam);
	// void onKeyDown(WPARAM wParam, LPARAM lParam);
	void onChar(int c);
	void onMouseDown(float x, float y, int button);
	void onMouseUp(float x, float y, int button);
	void onMouseMove(float x, float y, int button);
	void gotoAddr(unsigned int addr);

	void drawOffsetScale(ImDrawList *drawList);
	void toggleOffsetScale(CommonToggles toggle);
	void setHighlightType(MemBlockFlags flags);

	void toggleDrawZeroDark(bool toggle);

	const std::string &StatusMessage() const {
		return statusMessage_;
	}

private:
	void ProcessKeyboardShortcuts(bool focused);

	bool ParseSearchString(const std::string &query, bool asHex, std::vector<uint8_t> &data);
	void updateStatusBarText();
	void search(bool continueSearch);

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
	void PopupMenu();

	static wchar_t szClassName[];
	MIPSDebugInterface *debugger_ = nullptr;

	MemBlockFlags highlightFlags_ = MemBlockFlags::ALLOC;

	// Current cursor position.
	uint32_t curAddress_ = 0x08800000;
	// Selected range, which should always be around the cursor.
	uint32_t selectRangeStart_ = 0;
	uint32_t selectRangeEnd_ = 0;
	// Last select reset position, for selecting ranges.
	uint32_t lastSelectReset_ = 0;
	// Address of the first displayed byte.
	uint32_t windowStart_ = 0x08800000;
	// Number of bytes displayed per row.
	int rowSize_ = 16;

	// Width of one monospace character (to maintain grid.)
	int charWidth_ = 0;
	// Height of one monospace character (to maintain grid.)
	int charHeight_ = 0;
	// Height of one row of bytes.
	int rowHeight_ = 0;
	// Y position of offset header (at top.)
	int offsetPositionY_ = 0;
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

	bool drawZeroDark_ = false;

	// Last used search query, used when continuing a search.
	std::string searchQuery_;
	// Address of last match when continuing search.
	uint32_t matchAddress_ = 0xFFFFFFFF;
	// Whether a search is in progress.
	bool searching_ = false;

	std::string statusMessage_;
};

enum class MemDumpMode {
	Raw = 0,
	Disassembly = 1,
};

class ImMemDumpWindow {
public:
	ImMemDumpWindow() {
		filename_[0] = 0;
		address_ = 0x08800000;
		size_ = 0x01800000;
	}
	static const char *Title() {
		return "Memory Dumper";
	}
	void Draw(ImConfig &cfg, MIPSDebugInterface *debug);
	void SetRange(uint32_t addr, uint32_t size, MemDumpMode mode) {
		address_ = addr;
		size_ = size;
		mode_ = mode;
	}

private:
	uint32_t address_;
	uint32_t size_;
	MemDumpMode mode_ = MemDumpMode::Raw;
	char filename_[1024];
	std::string errorMsg_;
};

// Corresponds to the CMemView dialog
class ImMemWindow {
public:
	void Draw(MIPSDebugInterface *mipsDebug, ImConfig &cfg, ImControl &control, int index);
	ImMemView &View() {
		return memView_;
	}
	void DirtySymbolMap() {
		symsDirty_ = true;
	}
	void GotoAddr(u32 addr) {
		gotoAddr_ = addr;
		memView_.gotoAddr(addr);
	}
	static const char *Title(int index);

private:
	// We just keep the state directly in the window. Can refactor later.
	enum {
		INVALID_ADDR = 0xFFFFFFFF,
	};

	// Symbol cache
	std::vector<SymbolEntry> symCache_;
	bool symsDirty_ = true;
	int selectedSymbol_ = -1;
	char selectedSymbolName_[128];

	bool drawZeroDark_ = false;

	ImMemView memView_;
	char searchTerm_[64]{};

	u32 gotoAddr_ = 0x08800000;
};
