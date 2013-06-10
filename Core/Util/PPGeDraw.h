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

#include "../../Globals.h"
#include "ppge_atlas.h"

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

// If you want to draw using this texture but not go through the PSP GE emulation,
// jsut call this. Will bind the texture to unit 0.
void PPGeBindTexture();

enum {
	PPGE_ALIGN_LEFT = 0,
	PPGE_ALIGN_RIGHT = 16,
	PPGE_ALIGN_TOP = 0,
	PPGE_ALIGN_BOTTOM = 1,
	PPGE_ALIGN_HCENTER = 4,
	PPGE_ALIGN_VCENTER = 8,
	PPGE_ALIGN_VBASELINE = 32,  // text only, possibly not yet working

	PPGE_ALIGN_CENTER = PPGE_ALIGN_HCENTER | PPGE_ALIGN_VCENTER,
	PPGE_ALIGN_TOPLEFT = PPGE_ALIGN_TOP | PPGE_ALIGN_LEFT,
	PPGE_ALIGN_TOPRIGHT = PPGE_ALIGN_TOP | PPGE_ALIGN_RIGHT,
	PPGE_ALIGN_BOTTOMLEFT = PPGE_ALIGN_BOTTOM | PPGE_ALIGN_LEFT,
	PPGE_ALIGN_BOTTOMRIGHT = PPGE_ALIGN_BOTTOM | PPGE_ALIGN_RIGHT,
};

enum {
	PPGE_ESCAPE_NONE,
	PPGE_ESCAPE_BACKSLASHED,
};

enum {
	PPGE_LINE_NONE = 0,
	PPGE_LINE_USE_ELLIPSIS = 1, // use ellipses in too long words
	PPGE_LINE_WRAP_WORD = 2,
	PPGE_LINE_WRAP_CHAR = 4,
};

// Get the metrics of the bounding box of the text without changing the buffer or state.
void PPGeMeasureText(float *w, float *h, int *n, 
					const char *text, float scale, int WrapType = PPGE_LINE_NONE, int wrapWidth = 0);

// Overwrite the current text lines buffer so it can be drawn later.
void PPGePrepareText(const char *text, float x, float y, int align, float scale, 
					int WrapType = PPGE_LINE_NONE, int wrapWidth = 0);

// Get the metrics of the bounding box of the currently stated text.
void PPGeMeasureCurrentText(float *x, float *y, float *w, float *h, int *n);

// These functions must be called between PPGeBegin and PPGeEnd.

// Draw currently buffered text using the state from PPGeGetTextBoundingBox() call.
// Clears the buffer and state when done.
void PPGeDrawCurrentText(u32 color = 0xFFFFFFFF);

// Draws some text using the one font we have.
// Clears the text buffer when done.
void PPGeDrawText(const char *text, float x, float y, int align, float scale = 1.0f, u32 color = 0xFFFFFFFF);
void PPGeDrawTextWrapped(const char *text, float x, float y, float wrapWidth, int align, float scale = 1.0f, u32 color = 0xFFFFFFFF);

// Draws a "4-patch" for button-like things that can be resized.
void PPGeDraw4Patch(int atlasImage, float x, float y, float w, float h, u32 color = 0xFFFFFFFF);

// Just blits an image to the screen, multiplied with the color.
void PPGeDrawImage(int atlasImage, float x, float y, int align, u32 color = 0xFFFFFFFF);
void PPGeDrawImage(int atlasImage, float x, float y, float w, float h, int align, u32 color = 0xFFFFFFFF);
void PPGeDrawImage(float x, float y, float w, float h, float u1, float v1, float u2, float v2, int tw, int th, u32 color);

void PPGeDrawRect(float x1, float y1, float x2, float y2, u32 color);

void PPGeSetDefaultTexture();
void PPGeSetTexture(u32 dataAddr, int width, int height);
void PPGeDisableTexture();

