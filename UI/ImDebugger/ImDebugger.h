#pragma once

#include <vector>
#include <string>
#include <set>
#include <map>
#include <utility>

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
struct ImGuiWindow;

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
	bool settingsOpen;  // The debugger's own "Debugger settings" window.

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

	// ---- Appearance, accessibility and touch usability (saved in the [Settings] section) ----
	// Defaults that depend on the device (touch-friendly sizes, gestures...) are set in SyncConfig.
	int uiTheme = 0;             // 0 = keep the app's colors, 1 = Dark, 2 = Light, 3 = High contrast
	int uiScalePercent = 100;    // Text and spacing scale, 50..250
	bool touchFriendly = false;  // Bigger touch targets, thicker scrollbars, larger spacing
	bool touchGestures = false;  // Drag on empty space to scroll, long-press = right click
	bool autoMaximize = false;   // A single newly opened window fills the screen
	bool keepOnScreen = true;    // Never let a window get lost off-screen
	bool showToolbar = true;     // Bottom bar with run/step/keyboard buttons
	int menuMode = 0;            // 0 = automatic, 1 = always compact, 2 = always full
	bool autoKeyboard = true;    // Show the on-screen keyboard when a text field gets focus


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

// One entry per toggleable debugger window. This single table drives the full menu bar,
// the compact menu and the touch-friendly window launcher, so they can never go out of sync.
struct ImWindowEntry {
	const char *category;
	const char *label;
	bool *open;
	bool needsGame;        // Only available while a game is loaded.
	bool separatorBefore;  // Draw a separator above this entry in menus.
};

enum class ImArrangeAction {
	None,
	Tile,
	ToggleMaximize,
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

	// Show/hide the on-screen keyboard (same as the "Keyboard" toggle in the menu bar).
	void ToggleKeyboard() { keyboardToggleRequested_ = true; }
	bool IsKeyboardShown() const { return keyboardShown_; }

private:
	Path ConfigPath();

	// ---- Menus and chrome ----
	void BuildWindowRegistry();
	void DrawMenuBar(CoreState coreState, bool showDebugger, bool allowSaveStates);
	void DrawFileMenu(bool allowSaveStates);
	void DrawEmulationMenu();
	void DrawViewMenu();
	void DrawDebugMenu(CoreState coreState);
	void DrawSymbolFileItems();
	void DrawWindowCategoryMenu(const char *category, bool inGame);
	void DrawRunBreakItem(CoreState coreState);
	void DrawToolbar(CoreState coreState);
	void DrawLauncher();
	void DrawSettings();
	void CloseDebugger();
	void CloseAllWindows();

	// ---- Appearance ----
	void ApplyAppearance();

	// ---- Window management / touch input (all run at the end of Frame) ----
	void ProcessArrangeRequest();
	void AutoMaximizeNewWindow();
	void KeepWindowsOnScreen();
	void TileWindows();
	void SetMaximized(ImGuiWindow *window, bool maximize);
	void HandleTouchScroll();
	void HandleLongPress();

	// ---- On-screen keyboard ----
	void SetSoftKeyboard(bool show);
	void UpdateKeyboard();

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

	// Window registry (see ImWindowEntry).
	std::vector<ImWindowEntry> windows_;

	// Appearance state.
	ImGuiStyle baseStyle_;
	float baseFontScale_ = 1.0f;
	float baseEm_ = 0.0f;
	bool baseMoveFromTitleBar_ = false;
	bool baseCaptured_ = false;
	bool appearanceDirty_ = true;

	// Launcher state.
	bool launcherOpen_ = false;
	bool launcherStayOpen_ = false;
	char launcherFilter_[64]{};

	// Window management state.
	ImArrangeAction pendingArrange_ = ImArrangeAction::None;
	std::map<ImGuiID, std::pair<ImVec2, ImVec2>> savedRects_;  // window id -> (pos, size) before maximizing
	ImGuiID lastToolWindowId_ = 0;
	int frameCounter_ = 0;

	// Touch gesture state.
	ImGuiID touchScrollWindow_ = 0;
	ImGuiID touchScrollTargetX_ = 0;
	ImGuiID touchScrollTargetY_ = 0;
	bool touchScrolling_ = false;
	bool longPressFired_ = false;
	bool rightReleasePending_ = false;

	// On-screen keyboard state.
	bool keyboardShown_ = false;
	bool keyboardForced_ = false;  // Shown manually through the toggle; stays until toggled off.
	bool keyboardToggleRequested_ = false;
	bool prevWantTextInput_ = false;
	int keyboardHideCountdown_ = 0;  // Debounces hide when focus moves between two text fields.
};

// Simple custom controls and utilities.
void ImClickableValue(const char *id, uint32_t value, ImControl &control, ImCmd cmd);
void ImClickableValueFloat(const char *id, float value);
void ShowInWindowMenuItems(uint32_t addr, ImControl &control);
void ShowInMemoryViewerMenuItem(uint32_t addr, ImControl &control);
void ShowInMemoryDumperMenuItem(uint32_t addr, uint32_t size, MemDumpMode mode, ImControl &control);
void StatusBar(std::string_view str);
inline const char *BoolStr(bool s) { return s ? "true" : "false"; }
