#pragma once

#include <optional>
#include "Common/Log.h"
#include "Core/ConfigValues.h"

enum class CommandLineParseResult {
	Continue,
	Exit,
	Error,
};

enum class CmdLineMode {
	Both,
	Application,
	Headless,
};

// We collect command line options in this struct, then we apply it to the config after it's been loaded.
// This parser is shared between regular PPSSPP and headless, so there are some options that are only useful
// in one of them.
// When adding new options, don't forget to update g_autoParams in CmdLine.cpp (or write manual parsing).
struct CommandLineOptions {
	// If returns CommandLineParseResult::Exit or ::Error, the program should exit immediately (with an error return code if Error).
	CommandLineParseResult Parse(int argc, const char *argv[], CmdLineMode mode = CmdLineMode::Application);
	void ApplyToConfig() const;
	int PrintUsage(const char *progname, const char *situationText) const;

	CmdLineMode mode;

	std::optional<bool> fullscreen;
	std::optional<GPUBackend> gpuBackend;
	std::optional<bool> softwareRendering;
	std::optional<bool> enableLogging;
	std::optional<LogLevel> logLevel;  // Override log level with this.
	std::optional<std::string> log;
	std::vector<std::string> bootFilenames;

	std::optional<CPUCore> cpuCore;

	std::optional<std::string> startScreen;

	std::optional<bool> escapeExit;
	std::optional<bool> pauseMenuExit;

	// Enables the WebSocket debugger on startup, on this port (0 = pick automatically).
	// Also breaks the CPU at start in the headless build. See docs/WebSocketDebugger.md.
	std::optional<int> debuggerPort;

	// Overrides g_Config.bAutoSaveLoadSymbols for this run only (see SymbolMap::SaveModuleSymbols/
	// LoadModuleSymbols and Core/HLE/sceKernelModule.cpp) - handy for headless runs that want
	// symbol names without persisting the setting via Settings > Developer Tools.
	std::optional<bool> autoSaveLoadSymbols;

	// Attempts to boot the vsh, which will only work if the correct files are present in the flash
	// and once we've fixed all the bugs. This is just here to allow testing.
	std::optional<bool> bootVSH;

	std::optional<std::string> appendConfig;
	std::optional<std::string> root;  // This is supposed to configure host0:.

	std::optional<std::string> nand;  // Set the root nand directory (one level above flash0, ...)

	// Memory stick root (the directory containing PSP/GAME, PSP/SYSTEM, ...). Mainly for headless,
	// which otherwise always uses "memstick" next to the executable - so testing a real game there
	// meant copying it in. Points at the same layout the app uses, so the two can share one.
	std::optional<std::string> memStick;
	std::optional<std::string> stateToLoad;

	// Headless: unpack the firmware inside an official updater EBOOT.PBP (given as the boot
	// filename) into this directory, then exit without booting anything.
	// See Core/Util/PSARUnpack.h - the API can also filter to a subfolder, which the command line
	// deliberately doesn't expose since the in-app use is specifically flash0:/font.
	std::optional<std::string> unpackUpdater;
	// Headless: which PSP model the unpacker resolves names against - "01g".."12g", or "any"
	// (the default) to take whatever file list names each file first.
	std::optional<std::string> unpackUpdaterModel;

	std::optional<int> memReadAction;
	std::optional<int> memWriteAction;
	std::optional<int> breakAction;

	// Log a native stack trace (Windows only) on an otherwise-unhandled access violation.
	std::optional<bool> logNativeCrashes;

	// SDL only: Option to force a specific OpenGL version (42="4.2",
	// etc.; -1 means "try them all").
	// Implemented as a workaround for https://github.com/hrydgard/ppsspp/issues/20687
	// NOTE: this is currently not persistent (doesn't
	// go to config), even though --graphics=openglX.Y
	// also sets the GPU backend which does persist.
	int force_gl_version = -1;

#ifndef _DEBUG
	bool showLogWindow = false;
#else
	bool showLogWindow = true;
#endif
	std::string configFilename = "";
	std::string controlsConfigFilename = "";

	bool optionS = false;   // a legacy option

	std::optional<bool> oldAtrac;

	// Headless options that may also be mildly useful in application mode
	std::optional<int> resolutionScale;


	// Headless options
	std::optional<bool> compare;
	std::optional<bool> bench;
	std::optional<bool> verbose;
	std::optional<double> timeout;
	std::optional<bool> printEqualLines;

	std::optional<std::string> screenshotFilename;
	std::optional<std::string> screenshotFilenameSave;
	std::optional<std::string> screenshotFilenameDiff;
	// Headless: preserve the alpha channel when saving PNG screenshots.
	std::optional<bool> screenshotSaveKeepAlpha;

	// Headless: mount an ISO/CSO on umd1:.
	std::optional<std::string> mountIso;
	// Headless: also log through OutputDebugString (Windows).
	std::optional<bool> odsLog;
	// Headless: maximum allowed MSE error for screenshot comparison.
	std::optional<double> maxScreenshotError;
	// Headless: test names to skip. May be specified more than once.
	std::vector<std::string> ignoredTests;
	// Headless: generate C++ interpreter dispatch code to stdout and exit.
	std::optional<bool> generateInterpreterDispatch;

	// SDL only.
	std::optional<int> xres;
	std::optional<int> yres;
	std::optional<double> dpi;
	std::optional<double> scale;
};
