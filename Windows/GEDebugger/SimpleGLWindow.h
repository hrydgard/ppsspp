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

	enum ResizeType {
		RESIZE_NONE,
		RESIZE_SHRINK_FIT,
	};

	SimpleGLWindow(HINSTANCE hInstance, HWND hParent, int x, int y, int w, int h);
	~SimpleGLWindow() {
		if (drawProgram_ != NULL) {
			glsl_destroy(drawProgram_);
		}
		if (tex_) {
			glDeleteTextures(1, &tex_);
		}
	}

	void RegisterWindowClass();
	void Create(int x, int y, int w, int h);
	void SetupGL();
	void ResizeGL(int w, int h);
	void CreateProgram();

	void Clear();
	void Draw(u8 *data, int w, int h, ResizeType resize = RESIZE_NONE);

	void Swap() {
		SwapBuffers(hDC_);
	}

	static bool windowClassExists_;

	HINSTANCE hInstance_;
	HWND hParent_;
	HWND hWnd_;
	HDC hDC_;
	HGLRC hGLRC_;
	bool valid_;
	int w_;
	int h_;

	GLSLProgram *drawProgram_;
	GLuint tex_;
};