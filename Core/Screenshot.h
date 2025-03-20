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

#include "Common/File/Path.h"

#include <functional>

struct GPUDebugBuffer;
namespace Draw {
class DrawContext;
}

enum class ScreenshotFormat {
	PNG,
	JPG,
};

// NOTE: The first two may need rotation, depending on the backend and screen orientation.
// This is handled internally in TakeGameScreenshot().
enum ScreenshotType {
	// What's being show on screen (e.g. including FPS, etc.)
	SCREENSHOT_OUTPUT,
	// What the game rendered (e.g. at render resolution) to the display.
	SCREENSHOT_DISPLAY,
	// What the game is in-progress rendering now.
	SCREENSHOT_RENDER,
};

enum class ScreenshotResult {
	ScreenshotNotPossible,
	DelayedResult,  // This specifies that the actual result is one of the two below and will arrive in the callback.
	// These result can be delayed and arrive in the callback, if one is specified.
	FailedToWriteFile,
	Success,
};

const u8 *ConvertBufferToScreenshot(const GPUDebugBuffer &buf, bool alpha, u8 *&temp, u32 &w, u32 &h);

// Can only be used while in game.
// If the callback is passed in, the saving action happens on a background thread.
ScreenshotResult TakeGameScreenshot(Draw::DrawContext *draw, const Path &filename, ScreenshotFormat fmt, ScreenshotType type, int maxRes = -1, std::function<void(bool success)> callback = nullptr);

bool Save888RGBScreenshot(const Path &filename, ScreenshotFormat fmt, const u8 *bufferRGB888, int w, int h);
bool Save8888RGBAScreenshot(const Path &filename, const u8 *bufferRGBA8888, int w, int h);
// Overallocate bufferPNG for better encoding speed.
bool Save8888RGBAScreenshot(std::vector<uint8_t> &bufferPNG, const u8 *bufferRGBA8888, int w, int h);
