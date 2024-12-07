#pragma once

#include <vector>
#include <string>
#include <set>

#include "ext/imgui/imgui.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/System/Request.h"

#include "Core/Core.h"

#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/DebugInterface.h"

#include "UI/ImDebugger/ImDisasmView.h"
#include "UI/ImDebugger/ImStructViewer.h"
#include "UI/ImDebugger/ImGe.h"

// This is the main state container of the whole Dear ImGUI-based in-game cross-platform debugger.
//
// Code conventions for ImGUI in PPSSPP
// * If windows/objects need state, prefix the class name with Im and just store straight in parent struct

class MIPSDebugInterface;
class GPUDebugInterface;
struct ImConfig;

// Corresponds to the CDisasm dialog
class ImDisasmWindow {
public:
	void Draw(MIPSDebugInterface *mipsDebug, ImConfig &cfg, CoreState coreState);
	ImDisasmView &View() {
		return disasmView_;
	}
	void DirtySymbolMap() {
		symsDirty_ = true;
	}

private:
	// We just keep the state directly in the window. Can refactor later.

	enum {
		INVALID_ADDR = 0xFFFFFFFF,
	};

	u32 gotoAddr_ = 0x1000;

	// Symbol cache
	std::vector<SymbolEntry> symCache_;
	bool symsDirty_ = true;
	int selectedSymbol_ = -1;
	char selectedSymbolName_[128];

	ImDisasmView disasmView_;
	char searchTerm_[64]{};
};

struct ImConfig {
	// Defaults for saved settings are set in SyncConfig.

	bool disasmOpen;
	bool demoOpen;
	bool regsOpen;
	bool threadsOpen;
	bool callstackOpen;
	bool breakpointsOpen;
	bool modulesOpen;
	bool hleModulesOpen;
	bool audioDecodersOpen;
	bool structViewerOpen;
	bool framebuffersOpen;
	bool texturesOpen;
	bool displayOpen;
	bool styleEditorOpen;
	bool filesystemBrowserOpen;
	bool kernelObjectsOpen;
	bool audioChannelsOpen;
	bool debugStatsOpen;
	bool geDebuggerOpen;
	bool geStateOpen;
	bool schedulerOpen;

	// HLE explorer settings
	// bool filterByUsed = true;

	// Various selections
	int selectedModule = 0;
	int selectedThread = 0;
	int selectedFramebuffer = -1;
	int selectedBreakpoint = -1;
	int selectedMemCheck = -1;
	uint64_t selectedTexAddr = 0;

	bool displayLatched = false;

	// We use a separate ini file from the main PPSSPP config.
	void LoadConfig(const Path &iniFile);
	void SaveConfig(const Path &iniFile);

	void SyncConfig(IniFile *ini, bool save);
};

enum ImUiCmd {
	TRIGGER_FIND_POPUP = 0,
};

struct ImUiCommand {
	ImUiCmd cmd;
};

class ImDebugger {
public:
	ImDebugger();
	~ImDebugger();

	void Frame(MIPSDebugInterface *mipsDebug, GPUDebugInterface *gpuDebug);

private:
	Path ConfigPath();

	RequesterToken reqToken_;

	ImDisasmWindow disasm_;
	ImGeDebuggerWindow geDebugger_;
	ImStructViewer structViewer_;

	// Open variables.
	ImConfig cfg_{};
};
