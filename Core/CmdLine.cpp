#include "Core/Config.h"
#include "Core/CmdLine.h"
#include "Common/StringUtils.h"

static int printUsage(int argc, const char *argv[]) {
	// NOTE: by convention, --help outputs to stdout,
	// not to stderr, since it is intended output in this
	// case (usage printed under different circumstances,
	// say in response to error during parsing commandline,
	// may go to stderr).
	FILE *dst = stdout;

	const char *progname = argc > 0 ? argv[0] : "ppsspp";
	// NOTE: wording largely taken from
	// https://www.ppsspp.org/docs/reference/command-line/
	fprintf(dst, "PPSSPP - a PSP emulator (SDL build)\n");
	fprintf(dst, "Usage: %s [options] [FILE]\n\n", progname);
	fprintf(dst, "Launches FILE (e.g. ISO image) if present.\n");
	fprintf(dst, "Options (some of these are specific to SDL backend):\n");
	fprintf(dst, "  -h, --help            show this message and exit\n");
	fprintf(dst, "  --version             show version information and exit\n");

	fprintf(dst, "  -d                    set the log level to debug\n");
	fprintf(dst, "  -v                    set the log level to verbose\n");
	fprintf(dst, "  --loglevel=INTEGER    set the log level to specified value\n");
	fprintf(dst, "  --log=FILE            output log to FILE\n");
	fprintf(dst, "  --state=FILE          load state from FILE\n");

	fprintf(dst, "  -i                    use the interpreter\n");
	fprintf(dst, "  -r                    use IR interpreter\n");
	fprintf(dst, "  -j                    use JIT\n");
	fprintf(dst, "  -J                    use IR JIT\n");

	fprintf(dst, "  --fullscreen          force full screen mode, ignoring saved configuration\n");
	fprintf(dst, "  --windowed            force windowed mode, ignoring saved configuration\n");
	fprintf(dst, "  --xres PIXELS         set X resolution\n");
	fprintf(dst, "  --yres PIXELS         set Y resolution\n");
	fprintf(dst, "  --dpi  FACTOR         set DPI\n");
	fprintf(dst, "  --scale FACTOR        set scale\n");
	fprintf(dst, "  --ipad                set resolution to 1024x768\n");
	fprintf(dst, "  --portrait            portrait mode\n");
	fprintf(dst, "  --graphics=BACKEND    use a different gpu backend\n");
	fprintf(dst, "                        options: gles, software, etc. (also opengl3.1, etc.)\n");

	fprintf(dst, "  --pause-menu-exit     change \"Exit to menu\" in pause menu to \"Exit\"\n");
	fprintf(dst, "  --escape-exit         escape key exits the application\n");
	fprintf(dst, "  --gamesettings        go directly to settings\n");
	fprintf(dst, "  --touchscreentest     go directly to the touchscreentest screen\n");
	fprintf(dst, "  --appendconfig=FILE   merge config FILE into the current configuration\n");

	return 0;
}

// Logging should be done with plain printf here.
// Error reporting is done with fprintf(stderr, ....).
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
				fprintf(stderr, "Warning: Ignoring extra boot filename '%s'.\n", argv[i]);
			}
		}

		// single char commands, like -l, -s, -d
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
}
