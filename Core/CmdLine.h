#pragma once

#include <optional>
#include "Core/ConfigValues.h"

enum class CommandLineParseResult {
	Continue,
	Exit,
	Error,
};

// We collect command line options in this struct, then we apply it to the config after it's been loaded.
struct CommandLineOptions {
	std::optional<bool> fullscreen;
	std::optional<GPUBackend> gpuBackend;
	std::optional<bool> softwareRendering;
	std::optional<bool> enableLogging;

	std::optional<std::string> bootFilename;

#ifndef _DEBUG
	bool showLogWindow = false;
#else
	bool showLogWindow = true;
#endif
	bool debugLogLevel = false;
	std::string configFilename = "";
	std::string controlsConfigFilename = "";

	bool optionS = false;   // a legacy option

	// If returns CommandLineParseResult::Exit or ::Error, the program should exit immediately (with an error return code if Error).
	CommandLineParseResult Parse(int argc, const char *argv[]);
	void ApplyToConfig() const;
};
