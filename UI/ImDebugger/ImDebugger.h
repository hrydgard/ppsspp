#pragma once

#include <vector>
#include <string>
#include <set>

#include "ext/imgui/imgui.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/System/Request.h"

#include "Core/Core.h"
#include "Core/HLE/SocketManager.h"

#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/Watch.h"

#include "UI/ImDebugger/ImDisasmView.h"
#include "UI/ImDebugger/ImMemView.h"
#include "UI/ImDebugger/ImStructViewer.h"
#include "UI/ImDebugger/ImJitViewer.h"
#include "UI/ImDebugger/ImGe.h"
#include "UI/ImDebugger/ImConsole.h"
#include "UI/ImDebugger/ImCommand.h"

// This is the main state container of the whole Dear ImGUI-based in-game cross-platform debugger.
//
// Code conventions for ImGUI in PPSSPP
// * If windows/objects need state, prefix the class name with Im and just store straight in parent struct

class MIPSDebugInterface;
class GPUCommon;
struct ImConfig;

// Snapshot of the MIPS CPU and other things we want to show diffs off.
struct ImSnapshotState {
	u32 gpr[32];
	float fpr[32];
	float vpr[128];
	u32 pc;
	u32 lo;
	u32 hi;
	u32 ll;
};

class IniFile;

struct ImConfig {
	// Defaults for saved settings are set in SyncConfig.
	bool disasmOpen;
	bool demoOpen;
	bool gprOpen;
	bool fprOpen;
	bool vfpuOpen;
	bool threadsOpen;
	bool callstackOpen;
	bool breakpointsOpen;
	bool symbolsOpen;
	bool modulesOpen;
	bool hleModulesOpen;
	bool mediaDecodersOpen;
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
	bool geVertsOpen;
	bool schedulerOpen;
	bool timeOpen;
	bool watchOpen;
	bool pixelViewerOpen;
	bool npOpen;
	bool socketsOpen;
	bool apctlOpen;
	bool adhocOpen;
	bool memDumpOpen;
	bool internalsOpen;
	bool sasAudioOpen;
	bool logConfigOpen;
	bool logOpen;
	bool utilityModulesOpen;
	bool atracToolOpen;
	bool memViewOpen[4];
	bool luaConsoleOpen;
	bool audioOutOpen;
	bool paramSFOOpen;
	bool jitViewerOpen;

	// HLE explorer settings
	// bool filterByUsed = true;

	// Various selections
	int selectedModuleId = 0;
	int selectedSymbolModule = 0;
	int selectedUtilityModule = 0;
	int selectedThread = 0;
	// Thread ID whose callstack to show, -1 to follow the current thread.
	int callstackThreadId = -1;
	int selectedCallstackFrame = -1;
	int selectedKernelObject = 0;
	int selectedFramebuffer = -1;
	int selectedBreakpoint = -1;
	int selectedMemCheck = -1;
	int selectedRegBreakpoint = -1;
	int selectedAtracCtx = 0;
	int vfpuDisplayMode = 0;
	int selectedMp3Ctx = 0;
	int selectedAacCtx = 0;
	int selectedMemoryBlock = 0;
	u32 selectedMpegCtx = 0;

	uint64_t selectedTexId = 0;
	bool selectedTexSecondary = false;

	bool realtimePixelPreview = false;
	int breakCount = 0;

	bool displayLatched = false;
	int requesterToken;

	bool sasShowAllVoices = false;
	bool vertexListClipped = false;

	float fbViewerZoom = 1.0f;

	// Touch / mobile helpers.
	bool quickScrollEnabled = true;     // Show the quick scroll strip on the right edge.
	bool quickScrollCollapsed = false;  // Strip collapsed to a single small button.
	bool forceKeyboard = false;         // Keyboard currently active (toggled from the bar). Asks the platform to keep the soft keyboard up.
	bool showKeyboardToggle = true;     // Show the keyboard button in the bar. Hiding it also turns the keyboard off.
	bool keyboardPersist = true;        // Keep the keyboard on after switching to another app and back.
	int barHeight = 100;                // Menu bar height, in percent of the normal frame height (taller is easier to hit by touch).
	int barLeftMargin = 0;              // Empty space left of the first bar button, in percent of the text height (keeps it clear of rounded screen corners).
	bool compactMenu = false;           // Always use the single "Menu" dropdown (it's also chosen automatically on narrow screens).


	// We use a separate ini file from the main PPSSPP config.
	void LoadConfig(const Path &iniFile);
	void SaveConfig(const Path &iniFile);

	void SyncConfig(IniFile *ini, bool save);
};

struct Track;
class ImAtracToolWindow {
public:
	void Draw(ImConfig &cfg);
	void Load();

	char atracPath_[1024]{};
	std::unique_ptr<Track> track_;
	std::string error_;
	std::string data_;
};

class ImWatchWindow {
public:
	ImWatchWindow();
	void Draw(ImConfig &cfg, ImControl &control, MIPSDebugInterface *mipsDebug);
private:
	std::vector<WatchInfo> watches_;

	char editBuffer_[256];
	int editingWatchIndex_ = -1;
	int editingColumn_ = 0;
	bool setEditFocus_ = false;
};

class ImLogWindow {
public:
	ImLogWindow() {}
	void Draw(ImConfig &cfg);

private:
	bool AutoScroll = true;  // Keep scrolling if already at the bottom.
};

class ImDebugger {
public:
	ImDebugger();
	~ImDebugger();

	void Frame(MIPSDebugInterface *mipsDebug, GPUCommon *gpuDebug, Draw::DrawContext *draw);

	// Should be called just before starting a step or run, so that things can
	// save state that they can later compare with, to highlight changes.
	void Snapshot(MIPSState *mips);
	void SnapshotGPU(GPUCommon *mips);

	// Call from the outside.
	void PostCmd(ImCommand cmd) {
		externalCommand_ = cmd;
	}
	void DeviceLost();
	void SaveConfig();

	// Call every frame the debugger isn't shown, instead of Frame().
	void OnHidden();
	// The platform closed the forced soft keyboard (Back, or an app switch without "keep keyboard").
	void OnSoftKeyboardHidden() {
		cfg_.forceKeyboard = false;
	}

private:
	Path ConfigPath();
	void DrawQuickScroll();
	void UpdateSoftKeyboard(bool debuggerVisible);

	RequesterToken reqToken_;

	ImDisasmWindow disasm_;
	ImGeDebuggerWindow geDebugger_;
	ImGeStateWindow geStateWindow_;
	ImMemWindow mem_[4];  // We support 4 separate instances of the memory viewer.
	ImStructViewer structViewer_;
	ImGePixelViewerWindow pixelViewer_;
	ImMemDumpWindow memDumpWindow_;
	ImWatchWindow watchWindow_;
	ImAtracToolWindow atracToolWindow_;
	ImConsole luaConsole_;
	ImLogWindow logWindow_;
	ImJitViewerWindow jitViewer_;

	ImSnapshotState newSnapshot_;
	ImSnapshotState snapshot_;

	int lastCpuStepCount_ = -1;
	int lastGpuStepCount_ = -1;

	// Open variables.
	ImConfig cfg_{};

	ImCommand externalCommand_;

	// Quick scroll strip: the last debugger window the user touched (ImGui window ID).
	ImGuiID quickScrollTarget_ = 0;
	// Whether we last asked the platform to show the soft keyboard.
	bool keyboardShown_ = false;
	bool keyboardPersistSent_ = false;
};

// Simple custom controls and utilities.
void ImClickableValue(const char *id, uint32_t value, ImControl &control, ImCmd cmd);
void ImClickableValueFloat(const char *id, float value);
void ShowInWindowMenuItems(uint32_t addr, ImControl &control);
void ShowInMemoryViewerMenuItem(uint32_t addr, ImControl &control);
void ShowInMemoryDumperMenuItem(uint32_t addr, uint32_t size, MemDumpMode mode, ImControl &control);
void StatusBar(std::string_view str);
inline const char *BoolStr(bool s) { return s ? "true" : "false"; }
