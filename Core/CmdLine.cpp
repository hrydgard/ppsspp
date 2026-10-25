#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "Core/Config.h"
#include "Core/CmdLine.h"
#include "Core/WebServer.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/FileUtil.h"
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
	Double,
	String,
	Enum,
};

struct CommandLineParam {
	size_t offsetInStruct;  // calculate with offsetof(CommandLineOptions, member). NOTE: Must point to an optional<> of the correct type!
	CmdParamType type;
	const char *longName;
	char shortName;  // can be 0 for no short name
	const char *docString;
	CmdLineMode mode;
	const char **enumValues;
	int enumCount;
};

enum class ParseParamResult {
	Success,
	NoMatch,
	BadValue,
};

// Decimal, or hex with 0x, optionally signed. Not base 0, which would read a leading 0 as octal.
// Up to 0xFFFFFFFF is accepted so a full 32-bit mask can be given, and wraps into the int.
static bool ParseIntParam(const std::string &value, int *out) {
	const char *p = value.c_str();
	bool negative = false;
	if (*p == '-' || *p == '+') {
		negative = *p == '-';
		p++;
	}
	int base = 10;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	// strtoull would take another sign or leading spaces here.
	if (!isxdigit((unsigned char)*p)) {
		return false;
	}
	char *end = nullptr;
	errno = 0;
	const unsigned long long magnitude = strtoull(p, &end, base);
	if (*end != '\0' || errno == ERANGE || magnitude > (negative ? 0x80000000ULL : 0xFFFFFFFFULL)) {
		return false;
	}
	*out = negative ? (int)(0 - (uint32_t)magnitude) : (int)(uint32_t)magnitude;
	return true;
}

ParseParamResult SetValue(CommandLineOptions *options, const CommandLineParam &param, const std::string &value) {
	uint8_t *optionsPtr = reinterpret_cast<uint8_t *>(options);
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
		*reinterpret_cast<std::optional<bool> *>(optionsPtr + param.offsetInStruct) = v;
		break;
	}
	case CmdParamType::Int:
	{
		// Not std::stoi - that throws on junk, which takes the process down with no diagnostic.
		int v;
		if (!ParseIntParam(value, &v)) {
			PRINT_STDERR("Error: Invalid value for integer parameter --%s: '%s'.\n", param.longName, value.c_str());
			return ParseParamResult::BadValue;
		}
		*reinterpret_cast<std::optional<int> *>(optionsPtr + param.offsetInStruct) = v;
		break;
	}
	case CmdParamType::Double:
	{
		double v;
		if (sscanf(value.c_str(), "%lf", &v) != 1) {
			PRINT_STDERR("Error: Invalid value for number parameter --%s: '%s'.\n", param.longName, value.c_str());
			return ParseParamResult::BadValue;
		}
		*reinterpret_cast<std::optional<double> *>(optionsPtr + param.offsetInStruct) = v;
		break;
	}
	case CmdParamType::String:
		*reinterpret_cast<std::optional<std::string> *>(optionsPtr + param.offsetInStruct) = value;
		break;
	case CmdParamType::Enum:
	{
		int enumIndex = -1;
		for (int i = 0; i < param.enumCount; ++i) {
			if (value == param.enumValues[i]) {
				enumIndex = i;
				break;
			}
		}
		if (enumIndex == -1) {
			PRINT_STDERR("Error: Invalid value for parameter --%s: '%s'. Expected one of: ", param.longName, value.c_str());
			for (int i = 0; i < param.enumCount; ++i) {
				PRINT_STDERR("%s%s", param.enumValues[i], (i + 1 < param.enumCount) ? ", " : "\n");
			}
			return ParseParamResult::BadValue;
		}
		*reinterpret_cast<std::optional<int> *>(optionsPtr + param.offsetInStruct) = enumIndex;
		break;
	}
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
		// SetValue() does the inverting, so both get "true".
		if ((param.type == CmdParamType::Bool || param.type == CmdParamType::BoolInverse) && equals(arg, std::string("--") + param.longName)) {
			ParseParamResult result = SetValue(options, param, "true");
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

// Enum ExceptionAction
const char *g_ExceptionActionValues[] = {
	"default",
	"ignore",
	"log",
	"break",
};

#define POFF(member) offsetof(CommandLineOptions, member)
static const CommandLineParam g_autoParams[] = {
	{POFF(fullscreen), CmdParamType::Bool, "fullscreen", '\0', "Force full screen mode", CmdLineMode::Application},
	{POFF(fullscreen), CmdParamType::BoolInverse, "windowed", '\0', "Force windowed mode", CmdLineMode::Application},
	{POFF(startScreen), CmdParamType::String, "start-screen", '\0', "Start on a specific screen (e.g. 'gamesettings', 'touchscreentest')", CmdLineMode::Application},
	{POFF(escapeExit), CmdParamType::Bool, "escape-exit", '\0', "Escape key exits the application", CmdLineMode::Application},
	{POFF(gameWindow), CmdParamType::Bool, "game-window", '\0', "Show the game in a second window, for testing multiple outputs (Vulkan, or Metal on Mac)", CmdLineMode::Application},
	{POFF(pauseMenuExit), CmdParamType::Bool, "pause-menu-exit", '\0', "Exit the application when the game ends, instead of returning to the menu", CmdLineMode::Application},
	{POFF(appendConfig), CmdParamType::String, "appendconfig", '\0', "Merge config FILE into the current configuration"},
	{POFF(root), CmdParamType::String, "root", 'r', "Mount directory as the root of host0:/."},
	{POFF(memStick), CmdParamType::String, "memstick", '\0', "Memory stick root directory (contains PSP/GAME etc)"},
	{POFF(nand), CmdParamType::String, "nand", '\0', "Root NAND directory, one level above flash0 (default: the memstick's PSP/NAND)"},
	{POFF(stateToLoad), CmdParamType::String, "state", '\0', "Load state from specified file"},
	{POFF(stateToSave), CmdParamType::String, "save-state", '\0', "Save a state to FILE partway through the run", CmdLineMode::Headless},
	{POFF(compare), CmdParamType::Bool, "compare", 'c', "Enable comparison mode", CmdLineMode::Headless},
	{POFF(bench), CmdParamType::Bool, "bench", 'b', "Enable benchmark mode", CmdLineMode::Headless},
	{POFF(oldAtrac), CmdParamType::Bool, "old-atrac", '\0', "Use old ATRAC decoder"},
	{POFF(jitProfile), CmdParamType::String, "jit-profile", '\0', "Profile the CPU core and write its hottest blocks to FILE on exit"},
	{POFF(memStickInserted), CmdParamType::BoolInverse, "no-memstick", '\0', "Run with the memory stick removed"},
	{POFF(log), CmdParamType::String, "log", '\0', "Output log to FILE", CmdLineMode::Application},
	{POFF(enableLogging), CmdParamType::Bool, "log", '\0', "Full log output, not just emulated printfs", CmdLineMode::Headless},
	{POFF(screenshotFilename), CmdParamType::String, "screenshot", '\0', "Compare rendered output against a reference screenshot FILE", CmdLineMode::Headless},
	{POFF(screenshotFilenameSave), CmdParamType::String, "screenshot-save", '\0', "Save rendered screenshot to specified path (PNG if the path ends in .png, BMP otherwise)", CmdLineMode::Headless},
	{POFF(screenshotDir), CmdParamType::String, "screenshot-dir", '\0', "Save each test's screenshot as DIR/<test path>.png (relative test paths keep their folders)", CmdLineMode::Headless},
	{POFF(replayEnd), CmdParamType::Int, "replay-end", '\0', "When replaying a GE frame dump, only draw primitives up to this one (1-based)", CmdLineMode::Headless},
	{POFF(dumpCompat), CmdParamType::Bool, "dump-compat", '\0', "Apply the game's compat.ini settings when replaying a GE frame dump (off by default)", CmdLineMode::Headless},
	{POFF(hardwareTransform), CmdParamType::Bool, "hardware-transform", '\0', "Transform vertices on the GPU (default true); false transforms them on the CPU", CmdLineMode::Headless},
	{POFF(vertexJit), CmdParamType::Bool, "vertex-jit", '\0', "Decode vertices with the JIT where the host can (default; =false for the C++ decoder)", CmdLineMode::Headless},
	{POFF(emulateMediaEngine), CmdParamType::Bool, "me-core", '\0', "Emulate the Media Engine for homebrew that runs code on it (default false)"},
	{POFF(shaderCache), CmdParamType::Bool, "shader-cache", '\0', "Load and save the per-game shader cache (default true); =false to time shader compiles like a first boot"},
	{POFF(disableGPUFeatures), CmdParamType::String, "disable-gpu-features", '\0', "Act as if the GPU lacked these, to test the fallbacks: comma-separated clip-distance, cull-distance, depth-clamp, framebuffer-fetch, dual-source-blend", CmdLineMode::Headless},
	{POFF(depthFilenameSave), CmdParamType::String, "depth-save", '\0', "Save the depth buffer to FILE along with the screenshot (u32 width, u32 height, u16 depths; float depths if FILE ends in .f32)", CmdLineMode::Headless},
	{POFF(screenshotRenderTarget), CmdParamType::Bool, "screenshot-render-target", '\0', "Take screenshots of the current render target instead of the displayed framebuffer", CmdLineMode::Headless},
	{POFF(screenshotFilenameDiff), CmdParamType::String, "screenshot-diff", '\0', "Save a visual comparison image to FILE when comparing screenshots", CmdLineMode::Headless},
	{POFF(screenshotSaveKeepAlpha), CmdParamType::Bool, "screenshot-keep-alpha", '\0', "Preserve the alpha channel when saving PNG screenshots (default: alpha is forced to 255)", CmdLineMode::Headless},
	{POFF(dumpAudioFilename), CmdParamType::String, "dump-audio", '\0', "Record the mixed audio output to a WAV file", CmdLineMode::Headless},
	{POFF(pressScript), CmdParamType::String, "press", '\0', "Hold buttons at emulated times: SECONDS:MASK[:DURATION],...", CmdLineMode::Headless},
	{POFF(screenshotAt), CmdParamType::String, "screenshot-at", '\0', "Save screenshots at emulated times: SECONDS:FILE,...", CmdLineMode::Headless},
	{POFF(timeoutWall), CmdParamType::Double, "timeout-wall", '\0', "Stop the run after this many real seconds", CmdLineMode::Headless},
	{POFF(timeoutEmulated), CmdParamType::Double, "timeout-emulated", '\0', "Stop the run after this many emulated seconds", CmdLineMode::Headless},
	// The old name for --timeout-wall, kept working because it's in a lot of scripts.
	{POFF(timeoutWall), CmdParamType::Double, "timeout", '\0', "Alias for --timeout-wall", CmdLineMode::Headless},
	{POFF(frameStats), CmdParamType::Bool, "frame-stats", '\0', "Print flips per emulated second at the end of the run, counted from --state's load", CmdLineMode::Headless},
	{POFF(maxScreenshotError), CmdParamType::Double, "max-mse", '\0', "Maximum allowed MSE error for screenshot comparison", CmdLineMode::Headless},
	{POFF(mountIso), CmdParamType::String, "mount", 'm', "Mount ISO/CSO on umd1: when booting an ELF/PBP or --vsh"},
	{POFF(unpackUpdater), CmdParamType::String, "unpack-updater", '\0', "Unpack the firmware in an updater EBOOT.PBP into DIR and exit", CmdLineMode::Headless},
	{POFF(unpackUpdaterModel), CmdParamType::String, "unpack-updater-model", '\0', "PSP model to unpack for (01g..12g, default any)", CmdLineMode::Headless},
	{POFF(unpackUpdaterFilter), CmdParamType::String, "unpack-updater-filter", '\0', "Only unpack entries under this path, e.g. flash0:/font/", CmdLineMode::Headless},
	{POFF(firmwareFromDisc), CmdParamType::Bool, "firmware-from-disc", '\0', "Install the firmware bundled on the booted disc and use it for this run", CmdLineMode::Headless},
	{POFF(installPkg), CmdParamType::String, "install-pkg", '\0', "Install the game update in a .pkg into DIR and exit", CmdLineMode::Headless},
	{POFF(reModule), CmdParamType::String, "re-module", '\0', "Load one PRX standalone and write a reverse-engineering report, then exit", CmdLineMode::Headless},
	{POFF(reOut), CmdParamType::String, "re-out", '\0', "Directory for --re-module output (default: re-out)", CmdLineMode::Headless},
	{POFF(reFunc), CmdParamType::String, "re-func", '\0', "Only disassemble this function of --re-module, by name or address", CmdLineMode::Headless},
	{POFF(reSyms), CmdParamType::String, "re-syms", '\0', "Apply a .ppsym file of known names before dumping --re-module", CmdLineMode::Headless},
	{POFF(reRawBase), CmdParamType::String, "re-raw-base", '\0', "Treat --re-module as a raw code image loaded at this address, e.g. 0x08300000", CmdLineMode::Headless},
	{POFF(reDecrypt), CmdParamType::String, "re-decrypt", '\0', "Decrypt one encrypted PSP file (PRX or ME image) and exit", CmdLineMode::Headless},
	{POFF(reDecryptOut), CmdParamType::String, "re-decrypt-out", '\0', "Output file for --re-decrypt (default: decrypted.bin)", CmdLineMode::Headless},
	{POFF(reDecryptKey), CmdParamType::String, "re-decrypt-key", '\0', "Secure install ID (32 hex digits) for --re-decrypt of a game patch installed on a PSP; also needs --fuse-id", CmdLineMode::Headless},
	{POFF(fuseId), CmdParamType::String, "fuse-id", '\0', "Fuse ID (12 hex digits) of the PSP a game patch was installed on"},
	{POFF(dumpFile), CmdParamType::String, "dump-file", '\0', "Copy one file out of the disc image, e.g. disc0:/PSP_GAME/USRDIR/MOVIE.PMF, or list a directory, and exit", CmdLineMode::Headless},
	{POFF(dumpFileOut), CmdParamType::String, "dump-file-out", '\0', "Output file for --dump-file (default: the file's own name)", CmdLineMode::Headless},
	{POFF(odsLog), CmdParamType::Bool, "odslog", 'o', "Also log through OutputDebugString (Windows)", CmdLineMode::Headless},
	{POFF(generateInterpreterDispatch), CmdParamType::Bool, "generate-interpreter-dispatch", '\0', "Generate C++ interpreter dispatch code (ExecInstruction) to stdout and exit", CmdLineMode::Headless},
	{POFF(resolutionScale), CmdParamType::Int, "resolution-scale", '\0', "Set the resolution scale factor"},
	{POFF(lockedCPUSpeed), CmdParamType::Int, "cpu-mhz", '\0', "Lock the emulated CPU clock at this many MHz (0 = let the game set it)"},
	{POFF(msaa), CmdParamType::Int, "msaa", '\0', "Set the MSAA level (0 = off, 1 = 2x, 2 = 4x, 3 = 8x)"},
	{POFF(debuggerPort), CmdParamType::Int, "debugger", '\0', "Enable the WebSocket debugger on this port (0 = pick automatically); see docs/WebSocketDebugger.md"},
	{POFF(debuggerRunPort), CmdParamType::Int, "debugger-run", '\0', "Like --debugger, but starts running instead of waiting at the entry point", CmdLineMode::Headless},
	{POFF(autoSaveLoadSymbols), CmdParamType::Bool, "auto-save-load-symbols", '\0', "Auto save/load per-module and per-game symbol files (see bAutoSaveLoadSymbols)", CmdLineMode::Both},
	{POFF(bootVSH), CmdParamType::Bool, "vsh", '\0', "Boot the VSH (requires files dumped from a PSP in the flash0 directory)"},
	{POFF(disableHLE), CmdParamType::Int, "disable-hle", '\0', "Bitmask of libraries to run the real firmware module for instead of our HLE", CmdLineMode::Both},
	{POFF(forceHLE), CmdParamType::Int, "force-hle", '\0', "Bitmask of libraries to run our HLE for, even where the real module is the default", CmdLineMode::Both},
	{POFF(memReadAction), CmdParamType::Enum, "memread", '\0', "Set the action for memory read exceptions", CmdLineMode::Both, g_ExceptionActionValues, ARRAY_SIZE(g_ExceptionActionValues)},
	{POFF(memWriteAction), CmdParamType::Enum, "memwrite", '\0', "Set the action for memory write exceptions", CmdLineMode::Both, g_ExceptionActionValues, ARRAY_SIZE(g_ExceptionActionValues)},
	{POFF(breakAction), CmdParamType::Enum, "break", '\0', "Set the action for break exceptions", CmdLineMode::Both, g_ExceptionActionValues, ARRAY_SIZE(g_ExceptionActionValues)},
	{POFF(logNativeCrashes), CmdParamType::Bool, "log-native-crashes", '\0', "Log a native stack trace (Windows only) on an otherwise-unhandled crash", CmdLineMode::Both},
	{POFF(verbose), CmdParamType::Bool, "verbose", '\0', "Enable verbose output", CmdLineMode::Both},
	{POFF(printEqualLines), CmdParamType::Bool, "print-equal-lines", '\0', "Print lines that are equal during comparison", CmdLineMode::Headless},
	{POFF(xres), CmdParamType::Int, "xres", '\0', "Set X resolution", CmdLineMode::Application},
	{POFF(yres), CmdParamType::Int, "yres", '\0', "Set Y resolution", CmdLineMode::Application},
	{POFF(dpi), CmdParamType::Double, "dpi", '\0', "Set DPI", CmdLineMode::Application},
	{POFF(scale), CmdParamType::Double, "scale", '\0', "Set scale", CmdLineMode::Application},
	{POFF(skipLogo), CmdParamType::Bool, "skip-logo", '\0', "Skip the logo screen", CmdLineMode::Application},
	// TODO: At some point we should maybe simply expose all config settings to be set directly from the command line automatically?
};

// NOTE: On Windows this prints nothing unfortunately, since PPSSPP is not a "console app" (PPSSPPHeadless is though, there it works).
// A fun trick we could do is AttachConsole(ATTACH_PARENT_PROCESS) which works at least from git bash and powershell, if we also use WriteConsole.
// However it's not exactly ideal.
int CommandLineOptions::PrintUsage(const char *progname, const char *situationText) const {
	// NOTE: by convention, --help outputs to stdout,
	// not to stderr, since it is intended output in this
	// case (usage printed under different circumstances,
	// say in response to error during parsing commandline,
	// may go to stderr).
	// NOTE: wording largely taken from
	// https://www.ppsspp.org/docs/reference/command-line/
	if (situationText) {
		PRINT_STDOUT("%s\n\n", situationText);
	} else {
		if (mode == CmdLineMode::Application) {
			PRINT_STDOUT("PPSSPP - a PSP emulator\n");
		} else {
			PRINT_STDOUT("PPSSPP Headless\n");
			PRINT_STDOUT("This is primarily meant as a non-interactive test tool.\n\n");
		}
	}

	if (mode == CmdLineMode::Application) {
		PRINT_STDOUT("Usage: %s [options] [FILE]\n\n", progname);
	} else {
		PRINT_STDOUT("Usage: %s [options] [FILE1] [FILE2] [FILE3]...\n\n", progname);
	}

	PRINT_STDOUT("Launches FILE (for example, an ISO image) if present.\n");
	PRINT_STDOUT("See headless/README.md for more details.\n\n");
	PRINT_STDOUT("Options:\n");
	PRINT_STDOUT("  -h, --help            show this message and exit\n");
	PRINT_STDOUT("  --version             show version information and exit\n");

	PRINT_STDOUT("  -d                    set the log level to debug\n");
	PRINT_STDOUT("  -v                    set the log level to verbose\n");
	PRINT_STDOUT("  --loglevel=INTEGER    set the log level to specified value\n");
	if (mode == CmdLineMode::Application) {
		PRINT_STDOUT("  --log=FILE        output log to FILE\n");
	}
	PRINT_STDOUT("  --state=FILE          load state from FILE\n");

	PRINT_STDOUT("  --cpu=CPU             use the specified CPU core (interpreter, ir, jit, jit-ir)\n");

	for (const auto &param : g_autoParams) {
		if (param.mode != CmdLineMode::Both && param.mode != mode) {
			// Skip mode-irrelevant parameters in help.
			continue;
		}
		char key[64]{};
		snprintf(key, ARRAY_SIZE(key), "  --%s%s%s", param.longName,
			param.shortName ? ", -" : "",
			param.shortName ? std::string(1, param.shortName).c_str() : "");
		// Fill key with spacing, keeping at least one space before the doc string
		// even if the name itself ran past the normal column width.
		size_t padTo = std::max(strlen(key) + 1, (size_t)24);
		padTo = std::min(padTo, ARRAY_SIZE(key) - 1);
		for (size_t j = strlen(key); j < padTo; ++j) {
			key[j] = ' ';
		}
		if (param.type == CmdParamType::Enum) {
			PRINT_STDOUT("%s%s\n", key, param.docString);
			PRINT_STDOUT("                        options: ");
			for (int i = 0; i < param.enumCount; ++i) {
				PRINT_STDOUT("%s%s", param.enumValues[i], i + 1 < param.enumCount ? ", " : "");
			}
			PRINT_STDOUT("\n");
		} else {
			PRINT_STDOUT("%s%s\n", key, param.docString);
		}
	}

	if (mode == CmdLineMode::Headless) {
		PRINT_STDOUT("  --ignore TESTNAME     ignore the specified test (may be repeated)\n");
	}

	PRINT_STDOUT("  --graphics=BACKEND    use a different gpu backend\n");
	PRINT_STDOUT("                        options: gles, software, etc. (also opengl3.1, etc.)\n");
	if (mode == CmdLineMode::Headless) {
		PRINT_STDOUT("                        and null, which draws nothing\n");
	}
	return 0;
}

// Logging should be done with plain printf here.
// Error reporting is done with PRINT_STDERR(....).
// Actually might want to reconsider given Android...
CommandLineParseResult CommandLineOptions::Parse(int argc, const char *argv[], CmdLineMode mode) {
	this->mode = mode;
	constexpr std::string_view cpuBackendStr = "--cpu=";
	constexpr std::string_view gpuBackendStr = "--graphics=";
	constexpr std::string_view configOption = "--config=";
	constexpr std::string_view controlsOption = "--controlconfig=";
	constexpr std::string_view logLevelOption = "--loglevel=";

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
				PrintUsage(argv[0], nullptr);
				return CommandLineParseResult::Exit;
			case 'i':
			case 'j':
			case 'J':
				// Removed cpu core options. Fail rather than quietly run on the default core.
				PRINT_STDERR("Error: %s was removed, use --cpu=interpreter, ir, jit or jit-ir.\n", argv[i]);
				return CommandLineParseResult::Error;
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
				return CommandLineParseResult::Error;
			} // else nomatch
		}

		if (parsedAutoParam) {
			// We already incremented i.
			continue;
		} else if (equals(argv[i], "--help")) {
			PrintUsage(argv[0], nullptr);
			return CommandLineParseResult::Exit;
		} else if (equals(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return CommandLineParseResult::Exit;
			// Commands with parameters. TODO: Should support both space and equals, like --config=foo.ini and --config foo.ini
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
			} else if (restOfOption == "metal") {
				gpuBackend = GPUBackend::METAL;
				softwareRendering = false;
			} else if (restOfOption == "software") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = true;
			} else if (restOfOption == "null" && mode == CmdLineMode::Headless) {
				// Runs display lists but draws nothing, for measuring the CPU emulation.
				nullGraphics = true;
				softwareRendering = false;
			} else if (sscanf(restOfOption.c_str(), "gles%lg", &glVersionTemp) == 1 || sscanf(restOfOption.c_str(), "opengl%lg", &glVersionTemp) == 1) {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = false;
				force_gl_version = int(10.0 * glVersionTemp + 0.5);
			} else if (restOfOption == "gles" || restOfOption == "opengl") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = false;
			} else {
				// Bad value, report error and exit.
				PRINT_STDERR("Invalid value for --graphics=: %s", restOfOption.c_str());
				return CommandLineParseResult::Error;
			}
		} else if (startsWith(argv[i], cpuBackendStr)) {
			const std::string restOfOption = argv[i] + cpuBackendStr.size();
			// Force software rendering off, as picking gles implies HW acceleration.
			// We could add more options for software such as "software-gles",
			// "software-vulkan" and "software-d3d11", or something similar.
			// For now, software rendering force-activates OpenGL.
			double glVersionTemp = 0.0f;
			if (restOfOption == "interpreter") {
				cpuCore = CPUCore::INTERPRETER;
			} else if (restOfOption == "jit") {
				cpuCore = CPUCore::JIT;
			} else if (restOfOption == "jit-ir") {
				cpuCore = CPUCore::JIT_IR;
			} else if (restOfOption == "ir") {
				cpuCore = CPUCore::IR_INTERPRETER;
			} else {
				// Bad value, report error and exit.
				PRINT_STDERR("Invalid value for --cpu=: %s", restOfOption.c_str());
				return CommandLineParseResult::Error;
			}
		} else if (startsWith(argv[i], configOption)) {
			configFilename = std::string(argv[i] + configOption.size());
		} else if (startsWith(argv[i], controlsOption)) {
			controlsConfigFilename = std::string(argv[i] + controlsOption.size());
		} else if (startsWith(argv[i], logLevelOption)) {
			logLevel = static_cast<LogLevel>(atoi(argv[i] + logLevelOption.size()));
		} else if (equals(argv[i], "--ignore")) {
			// Headless: skip a specific test. Can be repeated, so not a regular auto-param.
			if (i + 1 < argc) {
				ignoredTests.emplace_back(argv[i + 1]);
				i += 2;
				continue;
			} else {
				PRINT_STDERR("Error: --ignore requires an argument.\n");
				return CommandLineParseResult::Error;
			}
		} else if (startsWith(argv[i], "--")) {
			PRINT_STDERR("Error: Unknown parameter: %s\n", argv[i]);
			return CommandLineParseResult::Error;
		}
		// To the next argument.
		i++;
	}

	if (debuggerPort.has_value() && debuggerRunPort.has_value()) {
		PRINT_STDERR("Error: --debugger and --debugger-run can't be used together.\n");
		return CommandLineParseResult::Error;
	}

	// Final adjustments to adjust for old inconsistent code
	if (log.has_value()) {
		enableLogging = true;
		showLogWindow = true;
	}

	return CommandLineParseResult::Continue;
}

void CommandLineOptions::ApplyToConfig() const {
	if (fuseId.has_value()) {
		g_Config.sFuseID = fuseId.value();
		g_Config.DoNotSaveSetting(&g_Config.sFuseID);
	}
	if (fullscreen.has_value()) {
		g_Config.bFullScreen = fullscreen.value();
		g_Config.DoNotSaveSetting(&g_Config.bFullScreen);
	}
	if (gpuBackend.has_value()) {
		g_Config.iGPUBackend = (int)gpuBackend.value();
		g_Config.DoNotSaveSetting(&g_Config.iGPUBackend);
	}
	if (cpuCore.has_value()) {
		g_Config.iCpuCore = (int)cpuCore.value();
	}
	if (hardwareTransform.has_value()) {
		g_Config.bHardwareTransform = hardwareTransform.value();
		g_Config.DoNotSaveSetting(&g_Config.bHardwareTransform);
	}
	if (emulateMediaEngine.has_value()) {
		g_Config.bEmulateMediaEngine = emulateMediaEngine.value();
		g_Config.DoNotSaveSetting(&g_Config.bEmulateMediaEngine);
	}
	if (shaderCache.has_value()) {
		g_Config.bShaderCache = shaderCache.value();
		g_Config.DoNotSaveSetting(&g_Config.bShaderCache);
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
	if (memStickInserted.has_value()) {
		g_Config.bMemStickInserted = memStickInserted.value();
		g_Config.DoNotSaveSetting(&g_Config.bMemStickInserted);
	}
	if (escapeExit.has_value()) {
		g_Config.bPauseExitsEmulator = escapeExit.value();
	}
	if (pauseMenuExit.has_value()) {
		g_Config.bPauseMenuExitsEmulator = pauseMenuExit.value();
	}
	if (DebuggerPort().has_value()) {
		g_Config.iRemoteISOPort = DebuggerPort().value();
		g_Config.DoNotSaveSetting(&g_Config.iRemoteISOPort);
		g_Config.bRemoteDebuggerOnStartup = true;
		g_Config.DoNotSaveSetting(&g_Config.bRemoteDebuggerOnStartup);
		// --debugger=0 still means "pick any free port", but a specific port was asked for by
		// something that intends to connect to it, so don't quietly come up on a different one.
		WebServerSetRequireExactPort(DebuggerPort().value() != 0);
	}

	if (autoSaveLoadSymbols.has_value()) {
		g_Config.bAutoSaveLoadSymbols = autoSaveLoadSymbols.value();
		g_Config.DoNotSaveSetting(&g_Config.bAutoSaveLoadSymbols);
	}

	if (disableHLE.has_value()) {
		// DoNotSaveSetting so a per-game config can't quietly put the HLE back.
		g_Config.iDisableHLE = disableHLE.value();
		g_Config.DoNotSaveSetting(&g_Config.iDisableHLE);
	}

	if (forceHLE.has_value()) {
		g_Config.iForceEnableHLE = forceHLE.value();
		g_Config.DoNotSaveSetting(&g_Config.iForceEnableHLE);
	}

	if (logLevel.has_value()) {
		g_logManager.SetAllLogLevels(logLevel.value());
		g_logManager.DoNotSaveChannels();
	}

	if (lockedCPUSpeed.has_value()) {
		g_Config.iLockedCPUSpeed = lockedCPUSpeed.value();
		g_Config.DoNotSaveSetting(&g_Config.iLockedCPUSpeed);
	}

	if (oldAtrac.has_value()) {
		g_Config.bUseOldAtrac = oldAtrac.value();
		g_Config.DoNotSaveSetting(&g_Config.bUseOldAtrac);
	}
	if (jitProfile.has_value()) {
		g_Config.sJitProfileFile = jitProfile.value();
	}

	if (root.has_value()) {
		// No DoNotSaveSetting() here - mountRoot isn't an ordinary setting and never gets
		// written to ppsspp.ini, since the ini itself lives inside the memory stick directory.
		g_Config.mountRoot = Path(root.value());
	}

	if (memStick.has_value()) {
		// No DoNotSaveSetting() here - memStickDirectory isn't an ordinary setting and never gets
		// written to ppsspp.ini, since the ini itself lives inside the memory stick directory.
		g_Config.memStickDirectory = Path(memStick.value());
		File::CreateFullPath(g_Config.memStickDirectory);
		CreateSysDirectories();
	}

	if (nand.has_value()) {
		// No DoNotSaveSetting() here - memStickDirectory isn't an ordinary setting and never gets
		// written to ppsspp.ini, since the ini itself lives inside the memory stick directory.
		g_Config.nandRootDirectory = Path(nand.value());
	}

	if (resolutionScale.has_value()) {
		g_Config.iInternalResolution = resolutionScale.value();
		g_Config.DoNotSaveSetting(&g_Config.iInternalResolution);
	}

	if (msaa.has_value()) {
		g_Config.iMultiSampleLevel = msaa.value();
		g_Config.DoNotSaveSetting(&g_Config.iMultiSampleLevel);
	}

	if (memReadAction.has_value()) {
		g_Config.iExceptionActionMemRead = memReadAction.value();
		g_Config.DoNotSaveSetting(&g_Config.iExceptionActionMemRead);
	}

	if (memWriteAction.has_value()) {
		g_Config.iExceptionActionMemWrite = memWriteAction.value();
		g_Config.DoNotSaveSetting(&g_Config.iExceptionActionMemWrite);
	}

	if (breakAction.has_value()) {
		g_Config.iExceptionActionBreak = breakAction.value();
		g_Config.DoNotSaveSetting(&g_Config.iExceptionActionBreak);
	}

	if (logNativeCrashes.has_value()) {
		g_Config.bLogNativeCrashStackTraces = logNativeCrashes.value();
		g_Config.DoNotSaveSetting(&g_Config.bLogNativeCrashStackTraces);
	}

	// --vsh is applied by the caller (by setting their boot file name variable).

	// Note: dpi is not applied here - it's platform-specific.
	// Platforms should check cmdLineOptions.dpi.has_value() and handle accordingly.
}

std::vector<std::string> SplitCommandLine(const std::string &value) {
	std::vector<std::string> args;
	const char *p = value.c_str();

	while (*p != '\0') {
		while (isspace(*p)) {
			p++;
		}
		if (*p == '\0') {
			break;
		}

		bool done = false;
		bool quote = false;
		std::string arg;

		while (!done) {
			size_t sz = strcspn(p, "\"\\ \r\n\t");
			arg += std::string(p, sz);
			p += sz;

			switch (*p) {
			case '"':
				quote = !quote;
				p++;
				break;

			case '\\':
				p++;
				arg += std::string(p, 1);
				p++;
				break;

			case '\0':
				done = true;
				break;

			default:
				// If it's not the above, it's whitespace.
				if (!quote) {
					done = true;
				} else {
					sz = strspn(p, " \r\n\t");
					arg += std::string(p, sz);
					p += sz;
				}
				break;
			}
		}

		args.push_back(arg);

		while (isspace(*p)) {
			p++;
		}
	}
	return args;
}
