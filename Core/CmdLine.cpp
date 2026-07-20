#include "Core/Config.h"
#include "Core/CmdLine.h"
#include "Common/StringUtils.h"

#ifdef HAVE_LIBRETRO_VFS

// Actually, this probably shouldn't be built at all for libretro! But let's add the guard.

#define PRINT_STDOUT(...) printf(__VA_ARGS__)
#define PRINT_STDERR(...) printf(__VA_ARGS__)

#else

#define PRINT_STDOUT(...) printf(__VA_ARGS__)
#define PRINT_STDERR(...) fprintf(stderr, __VA_ARGS__)

#endif

// Helper to parse parameters that take an argument.
// Supports: --param=value, --param value, -p value
// Returns true if argument was consumed, false otherwise.
// On success, 'value' contains the argument string and 'i' is incremented if needed.
static bool ParseParameterWithArg(int argc, const char *argv[], size_t &i,
	const char *longName, char shortName,
	const char **outValue) {
	const char *arg = argv[i];
	const size_t len = strlen(arg);

	// Check for short form: -p value
	if (shortName && len == 2 && arg[0] == '-' && arg[1] == shortName) {
		if (i + 1 < argc) {
			*outValue = argv[i + 1];
			i += 2;  // Skip both -p and value
			return true;
		}
		PRINT_STDERR("Error: -%c requires an argument.\n", shortName);
		return false;
	}

	// Check for long form with equals: --param=value
	if (longName) {
		std::string prefix = std::string("--") + longName + "=";
		if (startsWith(arg, prefix)) {
			*outValue = arg + prefix.size();
			i++;  // Skip --param=value
			return true;
		}

		// Check for long form with space: --param value
		if (equals(arg, std::string("--") + longName)) {
			if (i + 1 < argc) {
				*outValue = argv[i + 1];
				i += 2;  // Skip both --param and value
				return true;
			}
			PRINT_STDERR("Error: --%s requires an argument.\n", longName);
			return false;
		}
	}

	return false;
}

static int printUsage(int argc, const char *argv[]) {
	// NOTE: by convention, --help outputs to stdout,
	// not to stderr, since it is intended output in this
	// case (usage printed under different circumstances,
	// say in response to error during parsing commandline,
	// may go to stderr).
	const char *progname = argc > 0 ? argv[0] : "ppsspp";
	// NOTE: wording largely taken from
	// https://www.ppsspp.org/docs/reference/command-line/
	PRINT_STDOUT("PPSSPP - a PSP emulator (SDL build)\n");
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

	PRINT_STDOUT("  --fullscreen          force full screen mode, ignoring saved configuration\n");
	PRINT_STDOUT("  --windowed            force windowed mode, ignoring saved configuration\n");
	PRINT_STDOUT("  --xres PIXELS         set X resolution\n");
	PRINT_STDOUT("  --yres PIXELS         set Y resolution\n");
	PRINT_STDOUT("  --dpi DPI             set DPI\n");
	PRINT_STDOUT("  --scale FACTOR        set scale\n");
	PRINT_STDOUT("  --ipad                set resolution to 1024x768\n");
	PRINT_STDOUT("  --portrait            portrait mode\n");
	PRINT_STDOUT("  --graphics=BACKEND    use a different gpu backend\n");
	PRINT_STDOUT("                        options: gles, software, etc. (also opengl3.1, etc.)\n");

	PRINT_STDOUT("  --pause-menu-exit     change \"Exit to menu\" in pause menu to \"Exit\"\n");
	PRINT_STDOUT("  --escape-exit         escape key exits the application\n");
	PRINT_STDOUT("  --gamesettings        go directly to settings\n");
	PRINT_STDOUT("  --touchscreentest     go directly to the touchscreentest screen\n");
	PRINT_STDOUT("  --appendconfig=FILE   merge config FILE into the current configuration\n");

	return 0;
}

// Logging should be done with plain printf here.
// Error reporting is done with PRINT_STDERR(....).
// Actually might want to reconsider given Android...
CommandLineParseResult CommandLineOptions::Parse(int argc, const char *argv[]) {
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
			// This is a filename to boot.
			if (!bootFilename.has_value()) {
				bootFilename = std::string(argv[i]);
			} else {
				// Already have a filename.
				PRINT_STDERR("Warning: Ignoring extra boot filename '%s'.\n", argv[i]);
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
				debugLogLevel = true;
				break;
			case 'h':
				printUsage(argc, argv);
				return CommandLineParseResult::Exit;
			case 'v':
				printf("%s\n", PPSSPP_GIT_VERSION);
				return CommandLineParseResult::Exit;
			}
		}

#if defined(__APPLE__)
		// On Apple system debugged executable may get -NSDocumentRevisionsDebugMode YES in argv.
		if (equals(argv[i], "-NSDocumentRevisionsDebugMode")) {
			// Ignore
		}
#endif
		// Simple bool commands

		// NOTE: We need to parse --fullscreen early, before we create the window.
		if (equals(argv[i], "--help")) {
			printUsage(argc, argv);
			return CommandLineParseResult::Exit;
		} else if (equals(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return CommandLineParseResult::Exit;
		} else if (equals(argv[i], "--fullscreen")) {
			fullscreen = true;
		} else if (equals(argv[i], "--windowed")) {
			fullscreen = false;
		// Commands with parameters. TODO: Should support both space and equals, like --config=foo.ini and --config foo.ini
		} else if (startsWith(argv[i], gpuBackendStr)) {
			const std::string_view restOfOption = argv[i] + gpuBackendStr.size();
			// Force software rendering off, as picking gles implies HW acceleration.
			// We could add more options for software such as "software-gles",
			// "software-vulkan" and "software-d3d11", or something similar.
			// For now, software rendering force-activates OpenGL.
			if (restOfOption == "directx11" || restOfOption == "d3d11") {
				gpuBackend = GPUBackend::DIRECT3D11;
				softwareRendering = false;
			} else if (restOfOption == "gles") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = false;
			} else if (restOfOption == "vulkan") {
				gpuBackend = GPUBackend::VULKAN;
				softwareRendering = false;
			} else if (restOfOption == "software") {
				gpuBackend = GPUBackend::OPENGL;
				softwareRendering = true;
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
	// Note: dpi is not applied here - it's platform-specific.
	// Platforms should check cmdLineOptions.dpi.has_value() and handle accordingly.
}
