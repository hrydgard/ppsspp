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
#include <fstream>
#include <sstream>
#include <iostream>

#include "base/basictypes.h"
#include "base/display.h"
#include "base/logging.h"
#include "gfx_es2/gl_state.h"

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

void RunTests()
{
	std::string output;

#ifdef IOS
	const std::string baseDirectory = g_Config.flash0Directory + "../";
#else
	const std::string baseDirectory = g_Config.memStickDirectory;
#endif

	CoreParameter coreParam;
	coreParam.cpuCore = g_Config.bJit ? CPU_JIT : CPU_INTERPRETER;
	coreParam.gpuCore = g_Config.bSoftwareRendering ? GPU_SOFTWARE : GPU_GLES;
	coreParam.enableSound = g_Config.bEnableSound;
	coreParam.mountIso = "";
	coreParam.mountRoot = baseDirectory + "pspautotests/";
	coreParam.startPaused = false;
	coreParam.printfEmuLog = false;
	coreParam.headLess = true;
	coreParam.renderWidth = 480;
	coreParam.renderHeight = 272;
	coreParam.pixelWidth = 480;
	coreParam.pixelHeight = 272;
	coreParam.collectEmuLog = &output;
	coreParam.unthrottle = true;
	coreParam.updateRecent = false;

	// Never report from tests.
	std::string savedReportHost = g_Config.sReportHost;
	g_Config.sReportHost = "";

	for (size_t i = 0; i < ARRAY_SIZE(testsToRun); i++) {
		const char *testName = testsToRun[i];
		coreParam.fileToStart = baseDirectory + "pspautotests/tests/" + testName + ".prx";
		std::string expectedFile = baseDirectory + "pspautotests/tests/" + testName + ".expected";

		ILOG("Preparing to execute %s", testName);
		std::string error_string;
		output = "";
		if (!PSP_Init(coreParam, &error_string)) {
			ELOG("Failed to init unittest %s : %s", testsToRun[i], error_string.c_str());
			PSP_CoreParameter().pixelWidth = pixel_xres;
			PSP_CoreParameter().pixelHeight = pixel_yres;
			return;
		}

		// Run the emu until the test exits
		while (true) {
			int blockTicks = usToCycles(1000000 / 10);
			while (coreState == CORE_RUNNING) {
				PSP_RunLoopFor(blockTicks);
			}
			// Hopefully coreState is now CORE_NEXTFRAME
			if (coreState == CORE_NEXTFRAME) {
				// set back to running for the next frame
				coreState = CORE_RUNNING;
			} else if (coreState == CORE_POWERDOWN)	{
				ILOG("Finished running test %s", testName);
				break;
			}
		}
	
		std::ifstream expected(expectedFile.c_str(), std::ios_base::in);
		if (!expected) {
			ELOG("Error opening expectedFile %s", expectedFile.c_str());
			break;
		}

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
				ELOG("DIFF on line %i!", line);
				ELOG("O: %s", o.c_str());
				ELOG("E: %s", e.c_str());
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
	glstate.Restore();
	glstate.viewport.set(0,0,pixel_xres,pixel_yres);
	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;
	PSP_CoreParameter().headLess = false;
	g_Config.sReportHost = savedReportHost;
}
