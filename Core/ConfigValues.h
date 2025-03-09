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
#include <cmath>
#include <string>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "Common/Common.h"
#include "Common/CommonFuncs.h"

constexpr int PSP_MODEL_FAT = 0;
constexpr int PSP_MODEL_SLIM = 1;
constexpr int PSP_DEFAULT_FIRMWARE = 660;
constexpr int VOLUME_OFF = 0;
constexpr int VOLUME_FULL = 10;
constexpr int VOLUMEHI_FULL = 100;  // for newer volume params. will convert them all later

// This matches exactly the old shift-based curve.
float Volume10ToMultiplier(int volume);

// NOTE: This is used for new volume parameters.
// It uses a more intuitive-feeling curve.
float Volume100ToMultiplier(int volume);

// Used for migration from the old settings.
int MultiplierToVolume100(float multiplier);

float UIScaleFactorToMultiplier(int factor);

struct ConfigTouchPos {
	float x;
	float y;
	float scale;
	// Note: Show is not used for all settings.
	bool show;
};

struct ConfigCustomButton {
	uint64_t key;
	int image;
	int shape;
	bool toggle;
	bool repeat;
};

enum class CPUCore {
	INTERPRETER = 0,
	JIT = 1,
	IR_INTERPRETER = 2,
	JIT_IR = 3,
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

enum class ScreenshotMode {
	FinalOutput = 0,
	GameImage = 1,
};

// Software is not among these because it will have one of these perform the blit to display.
enum class GPUBackend {
	OPENGL = 0,
	DIRECT3D11 = 2,
	VULKAN = 3,
};

enum class DepthRasterMode {
	DEFAULT = 0,
	LOW_QUALITY = 1,
	OFF = 2,
	FORCE_ON = 3,
};

enum class RestoreSettingsBits : int {
	SETTINGS = 1,
	CONTROLS = 2,
	RECENT = 4,
};
ENUM_CLASS_BITOPS(RestoreSettingsBits);

std::string GPUBackendToString(GPUBackend backend);
GPUBackend GPUBackendFromString(std::string_view backend);

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
	IOTIMING_UMDSLOWREALISTIC = 3,
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

// iOS only
enum class AppSwitchMode {
	SINGLE_SWIPE_NO_INDICATOR = 0,
	DOUBLE_SWIPE_INDICATOR = 1,
};

// for Config.iShowStatusFlags
enum class ShowStatusFlags {
	FPS_COUNTER = 1 << 1,
	SPEED_COUNTER = 1 << 2,
	BATTERY_PERCENT = 1 << 3,
};

// for iTiltInputType
enum TiltTypes {
	TILT_NULL = 0,
	TILT_ANALOG,
	TILT_DPAD,
	TILT_ACTION_BUTTON,
	TILT_TRIGGER_BUTTONS,
};

enum class ScreenEdgePosition {
	BOTTOM_LEFT = 0,
	BOTTOM_CENTER = 1,
	BOTTOM_RIGHT = 2,
	TOP_LEFT = 3,
	TOP_CENTER = 4,
	TOP_RIGHT = 5,
	CENTER_LEFT = 6,
	CENTER_RIGHT = 7,
	CENTER = 8,  // Used for REALLY important messages! Not RetroAchievements notifications.
	VALUE_COUNT,
};

enum class DebugOverlay : int {
	OFF,
	DEBUG_STATS,
	FRAME_GRAPH,
	FRAME_TIMING,
#ifdef USE_PROFILER
	FRAME_PROFILE,
#endif
	CONTROL,
	Audio,
	GPU_PROFILE,
	GPU_ALLOCATOR,
	FRAMEBUFFER_LIST,
};

// Android-only for now
enum class DisplayFramerateMode : int {
	DEFAULT,
	REQUEST_60HZ,
	FORCE_60HZ_METHOD1,
	FORCE_60HZ_METHOD2,
};

enum class SkipGPUReadbackMode : int {
	NO_SKIP,
	SKIP,
	COPY_TO_TEXTURE,
};

enum class RemoteISOShareType : int {
	RECENT,
	LOCAL_FOLDER,
};
