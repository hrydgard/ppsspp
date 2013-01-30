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

// Keeps track of allocated FBOs.



#include "../Globals.h"

struct GLSLProgram;

enum PspDisplayPixelFormat {
	PSP_DISPLAY_PIXEL_FORMAT_565 = 0,
	PSP_DISPLAY_PIXEL_FORMAT_5551 = 1,
	PSP_DISPLAY_PIXEL_FORMAT_4444 = 2,
	PSP_DISPLAY_PIXEL_FORMAT_8888 = 3,
};

class FramebufferManager {
public:
	FramebufferManager();
	~FramebufferManager();

	/* Better do this first:
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.blend.disable();
	*/

	void DrawPixels(const u8 *framebuf, int pixelFormat, int linesize);
	void DrawActiveTexture(float w, float h, bool flip = false);



private:

	// Used by DrawPixels
	unsigned int backbufTex;

	u8 *convBuf;
	GLSLProgram *draw2dprogram;
};