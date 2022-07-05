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

#include <cstdint>
#include <string>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "Common/CommonFuncs.h"

const int PSP_MODEL_FAT = 0;
const int PSP_MODEL_SLIM = 1;
const int PSP_DEFAULT_FIRMWARE = 660;
static const int8_t VOLUME_OFF = 0;
static const int8_t VOLUME_FULL = 10;

enum class CPUCore {
	INTERPRETER = 0,
	JIT = 1,
	IR_JIT = 2,
};

enum {
	ROTATION_AUTO = 0,
	ROTATION_LOCKED_HORIZONTAL = 1,
	ROTATION_LOCKED_VERTICAL = 2,
	ROTATION_LOCKED_HORIZONTAL180 = 3,
	ROTATION_LOCKED_VERTICAL180 = 4,
	ROTATION_AUTO_HORIZONTAL = 5,
};

enum TextureFiltering {
	TEX_FILTER_AUTO = 1,
	TEX_FILTER_FORCE_NEAREST = 2,
	TEX_FILTER_FORCE_LINEAR = 3,
	TEX_FILTER_AUTO_MAX_QUALITY = 4,
};

enum BufferFilter {
	SCALE_LINEAR = 1,
	SCALE_NEAREST = 2,
};

// Software is not among these because it will have one of these perform the blit to display.
enum class GPUBackend {
	OPENGL = 0,
	DIRECT3D9 = 1,
	DIRECT3D11 = 2,
	VULKAN = 3,
};

inline std::string GPUBackendToString(GPUBackend backend) {
	switch (backend) {
	case GPUBackend::OPENGL:
		return "OPENGL";
	case GPUBackend::DIRECT3D9:
		return "DIRECT3D9";
	case GPUBackend::DIRECT3D11:
		return "DIRECT3D11";
	case GPUBackend::VULKAN:
		return "VULKAN";
	}
	// Intentionally not a default so we get a warning.
	return "INVALID";
}

inline GPUBackend GPUBackendFromString(const std::string &backend) {
	if (!strcasecmp(backend.c_str(), "OPENGL") || backend == "0")
		return GPUBackend::OPENGL;
	if (!strcasecmp(backend.c_str(), "DIRECT3D9") || backend == "1")
		return GPUBackend::DIRECT3D9;
	if (!strcasecmp(backend.c_str(), "DIRECT3D11") || backend == "2")
		return GPUBackend::DIRECT3D11;
	if (!strcasecmp(backend.c_str(), "VULKAN") || backend == "3")
		return GPUBackend::VULKAN;
	return GPUBackend::OPENGL;
}

enum AudioBackendType {
	AUDIO_BACKEND_AUTO,
	AUDIO_BACKEND_DSOUND,
	AUDIO_BACKEND_WASAPI,
};

// For iIOTimingMethod.
enum IOTimingMethods {
	IOTIMING_FAST = 0,
	IOTIMING_HOST = 1,
	IOTIMING_REALISTIC = 2,
};

enum class SmallDisplayZoom {
	STRETCH = 0,
	PARTIAL_STRETCH = 1,
	AUTO = 2,
	MANUAL = 3,
};

enum class AutoLoadSaveState {
	OFF = 0,
	OLDEST = 1,
	NEWEST = 2,
};

enum class FastForwardMode {
	CONTINUOUS = 0,
	SKIP_FLIP = 2,
};

enum class BackgroundAnimation {
	OFF = 0,
	FLOATING_SYMBOLS = 1,
	RECENT_GAMES = 2,
	WAVE = 3,
	MOVING_BACKGROUND = 4,
};

enum class AnalogFpsMode {
	AUTO = 0,
	MAPPED_DIRECTION = 1,
	MAPPED_DIR_TO_OPPOSITE_DIR = 2,
};
