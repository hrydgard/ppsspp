#pragma once

#include <optional>
#include "Core/ConfigValues.h"

// We collect command line options in this struct, then we apply it to the config after it's been loaded.
struct CommandLineOptions {
	std::optional<bool> fullscreen;
	std::optional<GPUBackend> gpuBackend;
	std::optional<bool> softwareRendering;
	std::optional<bool> enableLogging;

#ifndef _DEBUG
	bool showLogWindow = false;
#else
	bool showLogWindow = true;
#endif
	bool debugLogLevel = false;
	std::string configFilename = "";
	std::string controlsConfigFilename = "";

	bool optionS = true;   // a legacy option

	void Parse(int argc, const char *argv[]);
	void ApplyToConfig() const;
};
