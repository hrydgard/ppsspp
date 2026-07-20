#include "Core/Config.h"
#include "Core/CmdLine.h"
#include "Common/StringUtils.h"
#include "Common/Log/LogManager.h"

#ifdef HAVE_LIBRETRO_VFS

// Actually, this probably shouldn't be built at all for libretro! But let's add the guard.

#define PRINT_STDOUT(...) printf(__VA_ARGS__)
#define PRINT_STDERR(...) printf(__VA_ARGS__)

#else

#define PRINT_STDOUT(...) printf(__VA_ARGS__)
#define PRINT_STDERR(...) fprintf(stderr, __VA_ARGS__)

#endif

enum class CmdParamType {
	Bool,
	BoolInverse,
	Int,
	String,
};

struct CommandLineParam {
	size_t offsetInStruct;  // calculate with offsetof(CommandLineOptions, member). NOTE: Must point to an optional<> of the correct type!
	CmdParamType type;
	const char *longName;
	char shortName;  // can be 0 for no short name
	const char *docString;
	CmdLineMode mode;
};

#define POFF(member) \
	{offsetof(CommandLineOptions, member)}

enum class ParseParamResult {
	Success,
	NoMatch,
	BadValue,
};

ParseParamResult SetValue(CommandLineOptions *options, const CommandLineParam &param, const std::string &value) {
	switch (param.type) {
	case CmdParamType::Bool:
	case CmdParamType::BoolInverse:
	{
		bool v = true;
		if (value == "true" || value == "1") {
			v = true;
		} else if (value == "false" || value == "0") {
			v = false;
		} else {
			PRINT_STDERR("Error: Invalid value for boolean parameter --%s: '%s'. Expected 'true' or 'false'.\n", param.longName, value.c_str());
			return ParseParamResult::BadValue;
		}

		if (param.type == CmdParamType::BoolInverse) {
			v = !v;
		}
		*reinterpret_cast<std::optional<bool> *>(reinterpret_cast<uint8_t *>(options) + param.offsetInStruct) = v;
		break;
	}
	case CmdParamType::Int:
		*reinterpret_cast<std::optional<int> *>(reinterpret_cast<uint8_t *>(options) + param.offsetInStruct) = std::stoi(value);
		break;
	case CmdParamType::String:
		*reinterpret_cast<std::optional<std::string> *>(reinterpret_cast<uint8_t *>(options) + param.offsetInStruct) = value;
		break;
	default:
		break;
	}
	return ParseParamResult::Success;
}

// Helper to parse parameters that take an argument.
// Supports: --param=value, --param value, -p value
// Returns true if argument was consumed, false otherwise.
// On success, 'value' contains the argument string and 'i' is incremented if needed.
static ParseParamResult ParseParameterStr(int argc, const char *argv[], size_t &i, const CommandLineParam &param, CommandLineOptions *options) {
	const char *arg = argv[i];
	const size_t len = strlen(arg);

	// Check for short form: -p value
	if (param.shortName && len == 2 && arg[0] == '-' && arg[1] == param.shortName) {
		if (i + 1 < argc) {
			ParseParamResult result = SetValue(options, param, argv[i + 1]);
			i += 2;  // Skip both -p and value
			return result;
		}
		PRINT_STDERR("Error: -%c requires an argument.\n", param.shortName);
		return ParseParamResult::BadValue;
	}

	// Check for long form with equals: --param=value
	if (param.longName) {
		std::string prefix = std::string("--") + param.longName + "=";
		if (startsWith(arg, prefix)) {
			ParseParamResult result = SetValue(options, param, arg + prefix.size());
			i++;  // Skip --param=value
			return result;
		}

		// Check for boolean/inverse-boolean parameter - these don't take a separate param value.
		if (param.type == CmdParamType::Bool && equals(arg, std::string("--") + param.longName)) {
			ParseParamResult result = SetValue(options, param, "true");
			i++;  // Skip --param
			return result;
		} else if (param.type == CmdParamType::BoolInverse && equals(arg, std::string("--") + param.longName)) {
			ParseParamResult result = SetValue(options, param, "false");
			i++;  // Skip --param
			return result;
		}

		// Check for long form with space: --param value
		if (equals(arg, std::string("--") + param.longName)) {
			if (i + 1 < argc) {
				ParseParamResult result = SetValue(options, param, argv[i + 1]);
				i += 2;  // Skip both --param and value
				return result;
			} else {
				PRINT_STDERR("Error: --%s requires an argument.\n", param.longName);
				return ParseParamResult::BadValue;
			}
		}

		// Else no match, continue.
	}
	return ParseParamResult::NoMatch;
}

static const CommandLineParam g_autoParams[] = {
	{POFF(fullscreen), CmdParamType::Bool, "fullscreen", 0, "Force full screen mode", CmdLineMode::Application},
	{POFF(fullscreen), CmdParamType::BoolInverse, "windowed", 0, "Force windowed mode", CmdLineMode::Application},
	{POFF(startScreen), CmdParamType::String, "start-screen", 0, "Start on a specific screen (e.g. 'gamesettings', 'touchscreentest')", CmdLineMode::Application},
	{POFF(escapeExit), CmdParamType::Bool, "escape-exit", 0, "Escape key exits the application", CmdLineMode::Application},
	{POFF(pauseMenuExit), CmdParamType::Bool, "pause-menu-exit", 0, "Change \"Exit to menu\" in pause menu to \"Exit\"", CmdLineMode::Application},
	{POFF(appendConfig), CmdParamType::String, "appendconfig", 0, "Merge config FILE into the current configuration"},
	{POFF(root), CmdParamType::String, "root", 'r', "Mount root directory"},
	{POFF(stateToLoad), CmdParamType::String, "state", 0, "Load state from specified file"},
	{POFF(compare), CmdParamType::Bool, "compare", 'c', "Enable comparison mode"},
	{POFF(bench), CmdParamType::Bool, "bench", 'b', "Enable benchmark mode"},
	{POFF(oldAtrac), CmdParamType::Bool, "old-atrac", 0, "Use old ATRAC decoder"},
	{POFF(log), CmdParamType::String, "log", 0, "Output log to FILE"},
	{POFF(screenshotFilename), CmdParamType::String, "screenshot", 0, "Take a screenshot and save to FILE"},
	{POFF(screenshotFilenameSave), CmdParamType::String, "screenshot-save", 0, "Save screenshot to specified path"},
	{POFF(timeout), CmdParamType::Int, "timeout", 0, "Set the timeout value"},
};

// NOTE: On Windows this prints nothing unfortunately, since PPSSPP is not a "console app".
// A fun trick we could do is AttachConsole(ATTACH_PARENT_PROCESS) which works at least from git bash and powershell, if we also use WriteConsole.
// However it's not exactly ideal.
static int printUsage(int argc, const char *argv[], CmdLineMode mode) {
	// NOTE: by convention, --help outputs to stdout,
	// not to stderr, since it is intended output in this
	// case (usage printed under different circumstances,
	// say in response to error during parsing commandline,
	// may go to stderr).
	const char *progname = argc > 0 ? argv[0] : "ppsspp";
	// NOTE: wording largely taken from
	// https://www.ppsspp.org/docs/reference/command-line/
	if (mode == CmdLineMode::Application) {
		PRINT_STDOUT("PPSSPP - a PSP emulator\n");
	} else {
		PRINT_STDOUT("PPSSPP Headless\n");
		PRINT_STDOUT("This is primarily meant as a non-interactive test tool.\n\n");
	}
	PRINT_STDOUT("PPSSPP - a PSP emulator\n");
	PRINT_STDOUT("Usage: %s [options] [FILE]\n\n", progname);
	PRINT_STDOUT("Launches FILE (e.g. ISO image) if present.\n");
	PRINT_STDOUT("Options (some of these are specific to SDL backend):\n");
	PRINT_STDOUT("  -h, --help            show this message and exit\n");
	PRINT_STDOUT("  --version             show version information and exit\n");

	PRINT_STDOUT("  -d                    set the log level to debug\n");
	PRINT_STDOUT("  -v                    set the log level to verbose\n");
	PRINT_STDOUT("  --loglevel=INTEGER    set the log level to specified value\n");
	PRINT_STDOUT("  --log=FILE            output log to FILE\n");
	PRINT_STDOUT("  --state=FILE          load state from FILE\n");

	PRINT_STDOUT("  -i                    use the interpreter\n");
	PRINT_STDOUT("  -r                    use IR interpreter\n");
	PRINT_STDOUT("  -j                    use JIT\n");
	PRINT_STDOUT("  -J                    use IR JIT\n");

	for (const auto &param : g_autoParams) {
		if (param.mode != CmdLineMode::Both && param.mode != mode) {
			// Skip mode-irrelevant parameters in help.
			continue;
		}
		PRINT_STDOUT("  --%s%s%s\n", param.longName,
			param.shortName ? ", -" : "",
			param.shortName ? std::string(1, param.shortName).c_str() : "");
		PRINT_STDOUT("                        %s\n", param.docString);
	}

	// These are only available in SDL.
	if (mode == CmdLineMode::Application) {
		PRINT_STDOUT("  --xres PIXELS         set X resolution\n");
		PRINT_STDOUT("  --yres PIXELS         set Y resolution\n");
		PRINT_STDOUT("  --dpi DPI             set DPI\n");
		PRINT_STDOUT("  --scale FACTOR        set scale\n");
	}
	PRINT_STDOUT("  --graphics=BACKEND    use a different gpu backend\n");
	PRINT_STDOUT("                        options: gles, software, etc. (also opengl3.1, etc.)\n");
	return 0;
}

// Logging should be done with plain printf here.
// Error reporting is done with PRINT_STDERR(....).
// Actually might want to reconsider given Android...
CommandLineParseResult CommandLineOptions::Parse(int argc, const char *argv[], CmdLineMode mode) {
	constexpr std::string_view gpuBackendStr = "--graphics=";
	constexpr std::string_view configOption = "--config=";
	constexpr std::string_view controlsOption = "--controlconfig=";

#ifdef _DEBUG
	enableLogging = true;
#endif

	// The rest is handled in NativeInit().
	// NOTE: We don't increment i here, as we'll sometimes handle options that read the next argument.
	for (size_t i = 1; i < argc; ) {
		const size_t len = strlen(argv[i]);

		if (len > 0 && argv[i][0] != '-') {
			// This is a filename to boot. Headless supports multiple filenames.
			if (bootFilenames.size() == 0 || mode == CmdLineMode::Headless) {
				bootFilenames.emplace_back(argv[i]);
			} else {
				// Already have a filename. Ignore.
				PRINT_STDERR("Ignoring extra boot filename '%s' (can only pass in one).\n", argv[i]);
				i++;
				continue;
			}
		}

		// single char commands, like -l, -s
		if (len == 2) {
			switch (argv[i][1]) {
			case 'l':
				showLogWindow = true;
				enableLogging = true;
				break;
			case 's':
				optionS = true;
				break;
			case 'd':
				logLevel = LogLevel::LDEBUG;
				break;
			case 'v':
				logLevel = LogLevel::LVERBOSE;
				break;
			case 'h':
				printUsage(argc, argv, mode);
				return CommandLineParseResult::Exit;
			// Legacy cpucore options.
			case 'j':
				cpuCore = CPUCore::JIT;
				break;
			case 'i':
				cpuCore = CPUCore::INTERPRETER;
				break;
			case 'r':
				cpuCore = CPUCore::IR_INTERPRETER;
				break;
			case 'J':
				cpuCore = CPUCore::JIT_IR;
				break;
			}
		}

#if defined(__APPLE__)
		// On Apple system debugged executable may get -NSDocumentRevisionsDebugMode YES in argv.
		if (equals(argv[i], "-NSDocumentRevisionsDebugMode")) {
			// Ignore the arg and the YES
			i += 2;
			continue;
		}
#endif

		bool parsedAutoParam = false;
		// Loop through all the auto commands, call ParseParameterWithArg to handle them.
		for (const auto &param : g_autoParams) {
			if (param.mode != CmdLineMode::Both && param.mode != mode) {
				continue;
			}
			ParseParamResult result = ParseParameterStr(argc, argv, i, param, this);
			if (result == ParseParamResult::Success) {
				parsedAutoParam = true;
				break;
			} else if (result == ParseParamResult::BadValue) {
				return CommandLineParseResult::Exit;
			} // else nomatch
		}

		if (parsedAutoParam) {
			// We already incremented i.
			continue;
		} else if (equals(argv[i], "--help")) {
			printUsage(argc, argv, mode);
			return CommandLineParseResult::Exit;
		} else if (equals(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return CommandLineParseResult::Exit;
			// Commands with parameters. TODO: Should support both space and equals, like --config=foo.ini and --config foo.ini
		} else if (equals(argv[i], "--jit-ir")) {
			// Headless legacy
			cpuCore = CPUCore::JIT_IR;
		} else if (equals(argv[i], "--ir")) {
			// Headless legacy
			cpuCore = CPUCore::IR_INTERPRETER;
		} else if (startsWith(argv[i], gpuBackendStr)) {
			const std::string restOfOption = argv[i] + gpuBackendStr.size();
			// Force software rendering off, as picking gles implies HW acceleration.
			// We could add more options for software such as "software-gles",
			// "software-vulkan" and "software-d3d11", or something similar.
			// For now, software rendering force-activates OpenGL.
			double glVersionTemp = 0.0f;
			if (restOfOption == "directx11" || restOfOption == "d3d11") {
				gpuBackend = GPUBackend::DIRECT3D11;
				softwareRendering = false;
			} else if (restOfOption == "vulkan") {
				gpuBackend = GPUBackend::VULKAN;
				softwareRendering = false;
			} else if (restOfOption == "software") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = true;
			} else if (sscanf(restOfOption.c_str(), "gles%lg", &glVersionTemp) == 1 || sscanf(restOfOption.c_str(), "opengl%lg", &glVersionTemp) == 1) {
				g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
				g_Config.bSoftwareRendering = false;
				force_gl_version = int(10.0 * glVersionTemp + 0.5);
			} else if (restOfOption == "gles") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = false;
			} else {
				// Bad value, report error and exit.
				PRINT_STDERR("Invalid value for --graphics=: %s", restOfOption.c_str());
				return CommandLineParseResult::Exit;
			}
		} else if (startsWith(argv[i], configOption)) {
			configFilename = std::string(argv[i] + configOption.size());
		} else if (startsWith(argv[i], controlsOption)) {
			controlsConfigFilename = std::string(argv[i] + controlsOption.size());
		} else {
			// Report unknown argument later once this is complete.
		}
		// To the next argument.
		i++;
	}

	// Final adjustments to adjust for old inconsistent code
	if (log.has_value()) {
		enableLogging = true;
		showLogWindow = true;
	}

	return CommandLineParseResult::Continue;
}

void CommandLineOptions::ApplyToConfig() const {
	if (fullscreen.has_value()) {
		g_Config.bFullScreen = fullscreen.value();
		g_Config.DoNotSaveSetting(&g_Config.bFullScreen);
	}
	if (gpuBackend.has_value()) {
		g_Config.iGPUBackend = (int)gpuBackend.value();
		g_Config.DoNotSaveSetting(&g_Config.iGPUBackend);
	}
	if (softwareRendering.has_value()) {
		g_Config.bSoftwareRendering = softwareRendering.value();
		g_Config.DoNotSaveSetting(&g_Config.bSoftwareRendering);
	}
	if (enableLogging.has_value()) {
		g_Config.bEnableLogging = enableLogging.value();
		g_Config.DoNotSaveSetting(&g_Config.bEnableLogging);
	}
	if (optionS) {
		g_Config.bAutoRun = false;
		g_Config.bSaveSettings = false;
	}
	if (cpuCore.has_value()) {
		g_Config.iCpuCore = (int)cpuCore.value();
	}
	if (escapeExit.has_value()) {
		g_Config.bPauseExitsEmulator = escapeExit.value();
	}
	if (pauseMenuExit.has_value()) {
		g_Config.bPauseMenuExitsEmulator = pauseMenuExit.value();
	}

	if (logLevel.has_value()) {
		g_logManager.SetAllLogLevels(logLevel.value());
	}

	if (oldAtrac.has_value()) {
		g_Config.bUseOldAtrac = oldAtrac.value();
		g_Config.DoNotSaveSetting(&g_Config.bUseOldAtrac);
	}

	if (root.has_value()) {
		g_Config.DoNotSaveSetting(&g_Config.mountRoot);
		g_Config.mountRoot = Path(root.value());
	}

	// Note: dpi is not applied here - it's platform-specific.
	// Platforms should check cmdLineOptions.dpi.has_value() and handle accordingly.
}
