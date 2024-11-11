#pragma once

#include <vector>
#include <string>
#include <set>

#include "ext/imgui/imgui.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/Core.h"

#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/DebugInterface.h"

#include "UI/ImDebugger/ImDisasmView.h"
#include "UI/ImDebugger/ImStructViewer.h"

// This is the main state container of the whole Dear ImGUI-based in-game cross-platform debugger.
//
// Code conventions for ImGUI in PPSSPP
// * If windows/objects need state, prefix the class name with Im and just store straight in parent struct

class MIPSDebugInterface;


// Corresponds to the CDisasm dialog
class ImDisasmWindow {
public:
	void Draw(MIPSDebugInterface *mipsDebug, bool *open, CoreState coreState);

private:
	// We just keep the state directly in the window. Can refactor later.

	enum {
		INVALID_ADDR = 0xFFFFFFFF,
	};

	u32 gotoAddr_ = 0x1000;

	// Symbol cache
	std::vector<SymbolEntry> symCache_;

	ImDisasmView disasmView_;
};

class ImLuaConsole {
public:
	// Stub
};

struct ImConfig {
	bool disasmOpen = true;
	bool demoOpen  = false;
	bool regsOpen = true;
	bool threadsOpen = true;
	bool callstackOpen = true;
	bool modulesOpen = true;
	bool hleModulesOpen = false;
	bool atracOpen = true;
	bool structViewerOpen = false;

	// HLE explorer settings
	// bool filterByUsed = true;

	// Various selections
	int selectedModule = 0;
	int selectedThread = 0;
};

struct ImDebugger {
	void Frame(MIPSDebugInterface *mipsDebug);

	ImDisasmWindow disasm_;
	ImLuaConsole luaConsole_;
	ImStructViewer structViewer_;

	// Open variables.
	ImConfig cfg_;
};
