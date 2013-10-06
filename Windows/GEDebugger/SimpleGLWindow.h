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

#include "gfx_es2/glsl_program.h"
#include "Common/CommonWindows.h"
#include "Globals.h"

struct SimpleGLWindow {
	static const PTCHAR windowClass;

	enum Format {
		FORMAT_565 = 0,
		FORMAT_5551 = 1,
		FORMAT_4444 = 2,
		FORMAT_8888 = 3,

		FORMAT_FLOAT = 0x10,
		FORMAT_16BIT = 0x11,
	};

	enum Flags {
		RESIZE_NONE = 0x00,
		RESIZE_CENTER = 0x02,
		RESIZE_SHRINK_FIT = 0x01,
		RESIZE_SHRINK_CENTER = 0x03,
		ALPHA_IGNORE = 0x00,
		ALPHA_BLEND = 0x04,
	};

	SimpleGLWindow(HWND wnd);
	~SimpleGLWindow();

	void Clear();
	void Draw(u8 *data, int w, int h, bool flipped = false, Format = FORMAT_8888);
	void Redraw();
	void Initialize(u32 flags);
	static SimpleGLWindow *GetFrom(HWND hwnd);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void SetFlags(u32 flags) {
		flags_ = flags;
	}

	void Swap() {
		SwapBuffers(hDC_);
	}

	static void RegisterClass();
protected:
	void SetupGL();
	void ResizeGL(int w, int h);
	void CreateProgram();
	void GenerateChecker();
	void DrawChecker();

	HWND hWnd_;
	HDC hDC_;
	HGLRC hGLRC_;
	bool valid_;
	// Width and height of the window.
	int w_;
	int h_;
	// Last texture size/flipped for Redraw().
	int tw_;
	int th_;
	bool tflipped_;

	GLSLProgram *drawProgram_;
	GLuint checker_;
	GLuint tex_;
	u32 flags_;
	// Disable shrink (toggled by double click.)
	bool zoom_;
	bool dragging_;
	int dragStartX_;
	int dragStartY_;
	u32 dragLastUpdate_;
	// Offset to position the texture is drawn at.
	int offsetX_;
	int offsetY_;
};