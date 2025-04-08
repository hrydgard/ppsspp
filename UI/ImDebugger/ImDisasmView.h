#pragma once

#include <vector>
#include <string>
#include <set>
#include <algorithm>

#include "ext/imgui/imgui.h"

#include "Common/CommonTypes.h"
#include "Common/Math/geom2d.h"

#include "Core/Debugger/DisassemblyManager.h"
#include "Core/MIPS/MIPSDebugInterface.h"

struct ImConfig;
struct ImControl;

// Corresponds to CtrlDisAsmView
// TODO: Fold out common code.
class ImDisasmView {
public:
	ImDisasmView();
	~ImDisasmView();

	// Public variables bounds to imgui checkboxes
	bool followPC_ = true;

	void Draw(ImDrawList *drawList, ImControl &control);

	void PopupMenu(ImControl &control);
	void NotifyStep();

	void ScrollRelative(int amount);

	void onChar(int c);
	void onMouseDown(float x, float y, int button);
	void onMouseUp(float x, float y, int button);
	void onMouseMove(float x, float y, int button);
	void scrollAddressIntoView();
	bool curAddressIsVisible();
	void ScanVisibleFunctions();
	void clearFunctions() { g_disassemblyManager.clear(); };

	void getOpcodeText(u32 address, char *dest, int bufsize);
	u32 yToAddress(float y);

	void setDebugger(MIPSDebugInterface *deb) {
		if (debugger_ != deb) {
			debugger_ = deb;
			curAddress_ = debugger_->GetPC();
			g_disassemblyManager.setCpu(deb);
		}
	}

	MIPSDebugInterface *getDebugger() {
		return debugger_;
	}

	void scrollStepping(u32 newPc);
	u32 getInstructionSizeAt(u32 address);  // not const because it might have to analyze.

	void gotoAddr(unsigned int addr) {
		if (positionLocked_ != 0)
			return;
		u32 windowEnd = g_disassemblyManager.getNthNextAddress(windowStart_, visibleRows_);
		u32 newAddress = g_disassemblyManager.getStartAddress(addr);

		if (newAddress < windowStart_ || newAddress >= windowEnd) {
			windowStart_ = g_disassemblyManager.getNthPreviousAddress(newAddress, visibleRows_ / 2);
		}

		setCurAddress(newAddress);
		ScanVisibleFunctions();
	}
	void GotoPC() {
		gotoAddr(debugger_->GetPC());
	}
	void GotoRA() {
		gotoAddr(debugger_->GetRA());
	}
	u32 getSelection() {
		return curAddress_;
	}
	void setShowMode(bool s) {
		showHex_ = s;
	}
	void toggleBreakpoint(bool toggleEnabled = false);
	void editBreakpoint(ImConfig &cfg);

	void setCurAddress(u32 newAddress, bool extend = false) {
		newAddress = g_disassemblyManager.getStartAddress(newAddress);
		const u32 after = g_disassemblyManager.getNthNextAddress(newAddress, 1);
		curAddress_ = newAddress;
		selectRangeStart_ = extend ? std::min(selectRangeStart_, newAddress) : newAddress;
		selectRangeEnd_ = extend ? std::max(selectRangeEnd_, after) : after;
		updateStatusBarText();
	}

	void LockPosition() {
		positionLocked_++;
	}
	void UnlockPosition() {
		positionLocked_--;
		_assert_(positionLocked_ >= 0);
	}
	void Search(std::string_view needle);
	void SearchNext(bool forward);

	// Check these every frame!
	const std::string &StatusBarText() const {
		return statusBarText_;
	}
	bool SymbolMapReloaded() {
		bool retval = mapReloaded_;
		mapReloaded_ = false;
		return retval;
	}

private:
	enum class CopyInstructionsMode {
		OPCODES,
		DISASM,
		ADDRESSES,
	};

	void ProcessKeyboardShortcuts(bool focused);
	void assembleOpcode(u32 address, const std::string &defaultText);
	std::string disassembleRange(u32 start, u32 size);
	void disassembleToFile();
	void FollowBranch();
	void calculatePixelPositions();
	bool getDisasmAddressText(u32 address, char* dest, bool abbreviateLabels, bool showData);
	void updateStatusBarText();
	void drawBranchLine(ImDrawList *list, Bounds rc, std::map<u32, float> &addressPositions, const BranchLine &line);
	void CopyInstructions(u32 startAddr, u32 endAddr, CopyInstructionsMode mode);
	std::set<std::string> getSelectedLineArguments();
	void drawArguments(ImDrawList *list, Bounds rc, const DisassemblyLineInfo &line, float x, float y, ImColor textColor, const std::set<std::string> &currentArguments);

	u32 curAddress_ = 0;
	u32 selectRangeStart_ = 0;
	u32 selectRangeEnd_ = 0;
	float rowHeight_ = 0.f;
	float charWidth_ = 0.f;

	bool bpPopup_ = false;
	bool hasFocus_ = true;
	bool showHex_ = false;

	MIPSDebugInterface *debugger_ = nullptr;

	u32 windowStart_ = 0;
	int visibleRows_ = 1;
	bool displaySymbols_ = true;

	struct {
		int addressStart;
		int opcodeStart;
		int argumentsStart;
		int arrowsStart;
	} pixelPositions_{};

	std::vector<u32> jumpStack_;

	std::string searchQuery_;
	int matchAddress_;
	bool searching_ = false;
	bool keyTaken = false;
	bool mapReloaded_ = false;

	int positionLocked_ = 0;

	std::string statusBarText_;
	u32 funcBegin_ = 0;
	char funcNameTemp_[128]{};
};

// Corresponds to the CDisasm dialog
class ImDisasmWindow {
public:
	void Draw(MIPSDebugInterface *mipsDebug, ImConfig &cfg, ImControl &control, CoreState coreState);
	ImDisasmView &View() {
		return disasmView_;
	}
	void NotifyStep() {
		disasmView_.NotifyStep();
	}
	void DirtySymbolMap() {
		symsDirty_ = true;
	}
	const char *Title() const {
		return "CPU Debugger";
	}

private:
	// We just keep the state directly in the window. Can refactor later.

	enum {
		INVALID_ADDR = 0xFFFFFFFF,
	};

	u32 gotoAddr_ = 0x08800000;

	// Symbol cache
	std::vector<SymbolEntry> symCache_;
	bool symsDirty_ = true;
	int selectedSymbol_ = -1;
	char selectedSymbolName_[128];

	ImDisasmView disasmView_;
	char searchTerm_[64]{};
};
