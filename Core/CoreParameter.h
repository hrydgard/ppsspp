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

#include "Common/File/Path.h"
#include "Core/Compatibility.h"
#include "Core/Loaders.h"

enum GPUCore {
	GPUCORE_GLES = 0,
	GPUCORE_SOFTWARE = 1,
	GPUCORE_DIRECTX11 = 3,
	GPUCORE_VULKAN = 4,
};

enum class FPSLimit {
	NORMAL = 0,
	CUSTOM1 = 1,
	CUSTOM2 = 2,
	ANALOG = 3,
};

class FileLoader;

class GraphicsContext;
namespace Draw {
	class DrawContext;
}

enum class CPUCore;

// PSP_CoreParameter()
struct CoreParameter {
	CoreParameter() {}

	CPUCore cpuCore;
	GPUCore gpuCore;

	GraphicsContext *graphicsContext = nullptr;  // TODO: Find a better place.
	bool enableSound;  // there aren't multiple sound cores.

	Path fileToStart;
	Path mountIso;  // If non-empty, and fileToStart is an ELF or PBP, will mount this ISO in the background to umd1:.
	Path mountRoot;  // If non-empty, and fileToStart is an ELF or PBP, mount this as host0: / umd0:.
	std::string errorString;

	bool startBreak;
	std::string *collectDebugOutput = nullptr;
	bool headLess;   // Try to avoid messageboxes etc

	// Internal PSP rendering resolution and scale factor.
	int renderScaleFactor = 1;
	int renderWidth;
	int renderHeight;

	// Actual output resolution in pixels.
	int pixelWidth;
	int pixelHeight;

	// Can be modified at runtime.
	bool fastForward = false;
	FPSLimit fpsLimit = FPSLimit::NORMAL;
	int analogFpsLimit = 0;

	bool updateRecent = true;

	// Freeze-frame. For nvidia perfhud profiling. Developers only.
	bool freezeNext = false;
	bool frozen = false;

	FileLoader *mountIsoLoader = nullptr;
	IdentifiedFileType fileType = IdentifiedFileType::UNKNOWN;

	Compatibility compat;
};
