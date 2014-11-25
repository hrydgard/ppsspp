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

#pragma once

#include <string>

enum CPUCore {
	CPU_INTERPRETER,
	CPU_JIT,
};

enum GPUCore {
	GPU_NULL,
	GPU_GLES,
	GPU_SOFTWARE,
	GPU_DIRECTX9,
};

class FileLoader;

// PSP_CoreParameter()
struct CoreParameter {
	CoreParameter() : collectEmuLog(0), unthrottle(false), fpsLimit(0), updateRecent(true), freezeNext(false), frozen(false), mountIsoLoader(nullptr) {}
	CPUCore cpuCore;
	GPUCore gpuCore;
	bool enableSound;  // there aren't multiple sound cores.

	std::string fileToStart;
	std::string mountIso;  // If non-empty, and fileToStart is an ELF or PBP, will mount this ISO in the background to umd1:.
	std::string mountRoot;  // If non-empty, and fileToStart is an ELF or PBP, mount this as host0: / umd0:.
	std::string errorString;

	bool startPaused;
	bool printfEmuLog;  // writes "emulator:" logging to stdout
	std::string *collectEmuLog;
	bool headLess;   // Try to avoid messageboxes etc

	// Internal PSP resolution
	int renderWidth;
	int renderHeight;

	// Actual pixel output resolution (for use by glViewport and the like)
	int pixelWidth;
	int pixelHeight;

	// Can be modified at runtime.
	bool unthrottle;
	int fpsLimit;

	bool updateRecent;

	// Freeze-frame. For nvidia perfhud profiling. Developers only.
	bool freezeNext;
	bool frozen;

	FileLoader *mountIsoLoader;
};
