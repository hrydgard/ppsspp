#pragma once

#include <vector>
#include <string>
#include <set>

#include "ext/imgui/imgui.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"

#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/DebugInterface.h"

#include "UI/ImDebugger/ImDisasmView.h"

// This is the main state container of the whole Dear ImGUI-based in-game cross-platform debugger.
//
// Code conventions for ImGUI in PPSSPP
// * If windows/objects need state, prefix the class name with Im and just store straight in parent struct

class MIPSDebugInterface;


// Corresponds to the CDisasm dialog
class ImDisasmWindow {
public:
	void Draw(MIPSDebugInterface *mipsDebug, bool *open);

	void PCChanged() {
		pcChanged_ = true;
	}

private:
	// We just keep the state directly in the window. Can refactor later.

	enum {
		INVALID_ADDR = 0xFFFFFFFF,
	};

	u32 gotoAddr_ = 0x1000;

	bool followPC_ = true;
	bool pcChanged_ = false;

	ImDisasmView disasmView_;
};

class ImLuaConsole {
public:
	// Stub
};

struct ImDebugger {
	void Frame(MIPSDebugInterface *mipsDebug);

	ImDisasmWindow disasm_;
	ImLuaConsole luaConsole_;

	// Open variables.
	bool disasmOpen_ = true;
	bool demoOpen_ = false;
	bool regsOpen_ = true;
};
