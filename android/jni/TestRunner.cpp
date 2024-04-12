// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// TO USE:
// Simply copy pspautotests to the root of the USB memory / SD card of your android device.
// Then go to Settings / Developer Menu / Run CPU tests.
// It currently just runs one test but that can be easily changed.

#include <string>
#include <sstream>
#include <iostream>

#include "ppsspp_config.h"
#include "Common/System/Display.h"

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "TestRunner.h"

static const char * const testsToRun[] = {
	"cpu/cpu_alu/cpu_alu",
	"cpu/fpu/fpu",
	"cpu/icache/icache",
	"cpu/lsu/lsu",
	"cpu/vfpu/vector",
	"cpu/vfpu/matrix",
	"cpu/vfpu/convert",
	"cpu/vfpu/colors",
	"cpu/vfpu/prefixes",
	"cpu/vfpu/gum",
};

static std::string TrimNewlines(const std::string &s) {
	size_t p = s.find_last_not_of("\r\n");
	if (p == s.npos) {
		return "";
	}
	return s.substr(0, p + 1);
}

bool TestsAvailable() {
#if PPSSPP_PLATFORM(IOS)
	Path testDirectory = g_Config.flash0Directory / "..";
#else
	Path testDirectory = g_Config.memStickDirectory;
#endif
	// Hack to easily run the tests on Windows from the submodule
	if (File::IsDirectory(Path("../pspautotests"))) {
		testDirectory = Path("..");
	}
	return File::Exists(testDirectory / "pspautotests" / "tests");
}

bool RunTests() {
	std::string output;

#if PPSSPP_PLATFORM(IOS)
	Path baseDirectory = g_Config.flash0Directory / "..";
#else
	Path baseDirectory = g_Config.memStickDirectory;
	// Hack to easily run the tests on Windows from the submodule
	if (File::IsDirectory(Path("../pspautotests"))) {
		baseDirectory = Path("..");
	}
#endif

	GraphicsContext *tempCtx = PSP_CoreParameter().graphicsContext;
	// Clear the context during tests.  We set it back later.
	PSP_CoreParameter().graphicsContext = nullptr;

	CoreParameter coreParam;
	coreParam.cpuCore = (CPUCore)g_Config.iCpuCore;
	coreParam.gpuCore = GPUCORE_SOFTWARE;
	coreParam.enableSound = g_Config.bEnableSound;
	coreParam.graphicsContext = nullptr;
	coreParam.mountIso.clear();
	coreParam.mountRoot = baseDirectory / "pspautotests";
	coreParam.startBreak = false;
	coreParam.headLess = true;
	coreParam.renderWidth = 480;
	coreParam.renderHeight = 272;
	coreParam.pixelWidth = 480;
	coreParam.pixelHeight = 272;
	coreParam.collectDebugOutput = &output;
	coreParam.fastForward = true;
	coreParam.updateRecent = false;

	// Never report from tests.
	std::string savedReportHost = g_Config.sReportHost;
	g_Config.sReportHost.clear();

	for (size_t i = 0; i < ARRAY_SIZE(testsToRun); i++) {
		std::string testName = testsToRun[i];
		coreParam.fileToStart = baseDirectory / "pspautotests" / "tests" / (testName + ".prx");
		Path expectedFile = baseDirectory / "pspautotests" / "tests" / (testName + ".expected");

		INFO_LOG(SYSTEM, "Preparing to execute '%s'", testName.c_str());
		std::string error_string;
		output.clear();
		if (!PSP_Init(coreParam, &error_string)) {
			ERROR_LOG(SYSTEM, "Failed to init unittest %s : %s", testsToRun[i], error_string.c_str());
			PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
			PSP_CoreParameter().pixelHeight = g_display.pixel_yres;
			return false;
		}

		PSP_BeginHostFrame();

		// Run the emu until the test exits
		INFO_LOG(SYSTEM, "Test: Entering runloop.");
		while (true) {
			int blockTicks = (int)usToCycles(1000000 / 10);
			while (coreState == CORE_RUNNING) {
				PSP_RunLoopFor(blockTicks);
			}
			// Hopefully coreState is now CORE_NEXTFRAME
			if (coreState == CORE_NEXTFRAME) {
				// set back to running for the next frame
				coreState = CORE_RUNNING;
			} else if (coreState == CORE_POWERDOWN)	{
				INFO_LOG(SYSTEM, "Finished running test %s", testName.c_str());
				break;
			}
		}
		PSP_EndHostFrame();

		std::string expect_results;
		if (!File::ReadTextFileToString(expectedFile, &expect_results)) {
			ERROR_LOG(SYSTEM, "Error opening expectedFile %s", expectedFile.c_str());
			break;
		}

		std::istringstream expected(expect_results);
		std::istringstream logoutput(output);

		int line = 0;
		while (true) {
			++line;
			std::string e, o;
			std::getline(expected, e);
			std::getline(logoutput, o);
			// Remove stray returns
			e = TrimNewlines(e);
			o = TrimNewlines(o);
			if (e != o) {
				ERROR_LOG(SYSTEM, "DIFF on line %i!", line);
				ERROR_LOG(SYSTEM, "O: %s", o.c_str());
				ERROR_LOG(SYSTEM, "E: %s", e.c_str());
			}
			if (expected.eof()) {
				break;
			}
			if (logoutput.eof()) {
				break;
			}
		}
		PSP_Shutdown();
	}
	PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
	PSP_CoreParameter().pixelHeight = g_display.pixel_yres;
	PSP_CoreParameter().headLess = false;
	PSP_CoreParameter().graphicsContext = tempCtx;

	g_Config.sReportHost = savedReportHost;
	return true;  // Managed to execute the tests. Says nothing about the result.
}
