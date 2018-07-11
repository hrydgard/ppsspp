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

const int PSP_MODEL_FAT = 0;
const int PSP_MODEL_SLIM = 1;
const int PSP_DEFAULT_FIRMWARE = 660;
static const int8_t VOLUME_OFF = 0;
static const int8_t VOLUME_MAX = 10;

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
