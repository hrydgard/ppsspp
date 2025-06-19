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

enum MemorySearchStatus {SEARCH_PSP_NOT_INIT=-1, SEARCH_INITIAL, SEARCH_OK, SEARCH_NOTFOUND, SEARCH_CANCEL};
enum MemorySearchType {BITS_8, BITS_16, BITS_32, BITS_64, FLOAT_32, STRING, STRING_16,  BYTE_SEQ};
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

	void toggleEditableMemory(bool toggle);
	void toggleDrawZeroDark(bool toggle);
	void initSearch(const char* str, MemorySearchType type);
	void continueSearch();
	MemorySearchStatus SearchStatus();
	bool SearchEmpty();
	uint32_t SearchMatchAddress();

	const std::string &StatusMessage() const {
		return statusMessage_;
	}

private:
	void ProcessKeyboardShortcuts(bool focused);

	bool ParseSearchString(const char* query, MemorySearchType mode);
	void updateStatusBarText();
	MemorySearchStatus search(bool continueSearch);

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

	void EditMemory(int i);
	struct MemorySearch{
		// keep search related variables grouped
		std::vector<u8> data;
		uint8_t* fast_data;
		size_t fast_size;
		u32 searchAddress;
		u32 matchAddress;
		u32 segmentStart;
		u32 segmentEnd;
		bool searching;
		MemorySearchStatus status;
	} memSearch_;

	std::vector<u8> byteClipboard_;
	void CopyToByteClipboard();
	void PasteFromByteClipboard();

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
	bool editableMemory_ = false;

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
	void ProcessKeyboardShortcuts();
	// Symbol cache
	std::vector<SymbolEntry> symCache_;
	bool symsDirty_ = true;
	int selectedSymbol_ = -1;
	char selectedSymbolName_[128];

	bool drawZeroDark_ = false;
	bool editableMemory_ = false;

	int searchFormFlags_=0;
	bool focusSearchValueInput_ = false;
	MemorySearchType selectedSearchType_ = BITS_8;
	char searchStr_[512];
	// store the state of the search form
	ImMemView memView_;
	char searchTerm_[64]{};

	u32 gotoAddr_ = 0x08800000;
};
