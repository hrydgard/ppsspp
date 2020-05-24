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

#include <vector>
#include <string>

#include "gfx/texture_atlas.h"

#include "Common/CommonTypes.h"

class PointerWrap;

/////////////////////////////////////////////////////////////////////////////////////////////
// PPGeDraw: Super simple internal drawing API for 2D overlays like sceUtility messageboxes
// etc. Goes through the Ge emulation so that it's 100% portable - will work
// splendidly on any existing GPU backend, including the future software backend.

// Uploads the necessary texture atlas and other data to kernel RAM, and reserves
// space for the display list. The PSP must be inited.
void __PPGeInit();

// Saves to and restores from savestates (kernel RAM pointers, etc.)
void __PPGeDoState(PointerWrap &p);

// Just frees up the allocated kernel memory.
void __PPGeShutdown();

// Save and restore the Ge context. PPGeEnd() kicks off the generated display list.
void PPGeBegin();
void PPGeEnd();

enum class PPGeAlign {
	BOX_LEFT = 0x00,
	BOX_RIGHT = 0x01,
	BOX_HCENTER = 0x02,

	BOX_TOP = 0x00,
	BOX_BOTTOM = 0x10,
	BOX_VCENTER = 0x20,

	BOX_CENTER = 0x22,

	ANY = 0xFF,
};
inline bool operator &(const PPGeAlign &lhs, const PPGeAlign &rhs) {
	return ((int)lhs & (int)rhs) != 0;
}

enum {
	PPGE_LINE_NONE         = 0,
	PPGE_LINE_USE_ELLIPSIS = 1, // use ellipses in too long words
	PPGE_LINE_WRAP_WORD    = 2,
	PPGE_LINE_WRAP_CHAR    = 4,
};

// Get the metrics of the bounding box of the text without changing the buffer or state.
void PPGeMeasureText(float *w, float *h, const char *text, float scale, int WrapType = PPGE_LINE_NONE, int wrapWidth = 0);

// Draws some text using the one font we have.
// Clears the text buffer when done.
void PPGeDrawText(const char *text, float x, float y, PPGeAlign align, float scale = 1.0f, u32 color = 0xFFFFFFFF);
void PPGeDrawTextWrapped(const char *text, float x, float y, float wrapWidth, float wrapHeight, PPGeAlign align, float scale = 1.0f, u32 color = 0xFFFFFFFF);

// Draws a "4-patch" for button-like things that can be resized.
void PPGeDraw4Patch(ImageID atlasImage, float x, float y, float w, float h, u32 color = 0xFFFFFFFF);

// Just blits an image to the screen, multiplied with the color.
void PPGeDrawImage(ImageID atlasImage, float x, float y, int align, u32 color = 0xFFFFFFFF);
void PPGeDrawImage(ImageID atlasImage, float x, float y, float w, float h, int align, u32 color = 0xFFFFFFFF);
void PPGeDrawImage(float x, float y, float w, float h, float u1, float v1, float u2, float v2, int tw, int th, u32 color);

void PPGeNotifyFrame();

class PPGeImage {
public:
	PPGeImage(const std::string &pspFilename);
	PPGeImage(u32 pngPointer, size_t pngSize);
	~PPGeImage();

	void SetTexture();

	// Does not normally need to be called (except to force preloading.)
	bool Load();
	void Free();

	void DoState(PointerWrap &p);

	// Do not use, only for savestate upgrading.
	void CompatLoad(u32 texture, int width, int height);

	int Width() const {
		return width_;
	}

	int Height() const {
		return height_;
	}

	static void Decimate();

private:
	static std::vector<PPGeImage *> loadedTextures_;

	std::string filename_;

	// Only valid if filename_.empty().
	u32 png_;
	size_t size_;

	u32 texture_;
	int width_;
	int height_;

	int lastFrame_;
};

void PPGeDrawRect(float x1, float y1, float x2, float y2, u32 color);

void PPGeSetDefaultTexture();
void PPGeDisableTexture();

