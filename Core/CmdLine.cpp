#include "Core/Config.h"
#include "Core/CmdLine.h"
#include "Common/StringUtils.h"

void CommandLineOptions::Parse(int argc, const char *argv[]) {
	constexpr std::string_view gpuBackendStr = "--graphics=";
	constexpr std::string_view configOption = "--config=";
	constexpr std::string_view controlsOption = "--controlconfig=";

#ifdef _DEBUG
	enableLogging = true;
#endif

	// The rest is handled in NativeInit().
	for (size_t i = 1; i < argc; ++i) {
		const size_t len = strlen(argv[i]);
		if (argv[i][0] != '-' || len < 2) {
			continue;
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
			}
		}

		// Simple bool commands

		// NOTE: We need to parse --fullscreen early, before we create the window.
		if (equals(argv[i], "--fullscreen")) {
			fullscreen = true;
		} else if (equals(argv[i], "--windowed")) {
			fullscreen = false;
		}
		// Commands with parameters. TODO: Should support both space and equals, like --config=foo.ini and --config foo.ini
		if (startsWith(argv[i], gpuBackendStr)) {
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
		}
	}
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
