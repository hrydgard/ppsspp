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

	std::optional<std::string> appendConfig;
	std::optional<std::string> root;  // mount root, needs more explanation
	std::optional<std::string> stateToLoad;

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

	// Headless options
	std::optional<bool> compare;
	std::optional<bool> bench;
	std::optional<bool> verbose;

	std::optional<std::string> screenshotFilename;
	std::optional<std::string> screenshotFilenameSave;
	std::optional<int> timeout;
};
