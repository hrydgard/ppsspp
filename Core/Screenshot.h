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

struct GPUDebugBuffer;

enum ScreenshotFormat {
	SCREENSHOT_PNG,
	SCREENSHOT_JPG,
};

enum ScreenshotType {
	// What's being show on screen (e.g. including FPS, etc.)
	SCREENSHOT_OUTPUT,
	// What the game rendered (e.g. at render resolution) to the display.
	// Can only be used while in game.
	SCREENSHOT_DISPLAY,
	// What the game is in-progress rendering now.
	// Can only be used while in game.
	SCREENSHOT_RENDER,
};

const u8 *ConvertBufferTo888RGB(const GPUDebugBuffer &buf, u8 *&temp, u32 &w, u32 &h);

bool TakeGameScreenshot(const char *filename, ScreenshotFormat fmt, ScreenshotType type, int *width = nullptr, int *height = nullptr, int maxRes = -1);
