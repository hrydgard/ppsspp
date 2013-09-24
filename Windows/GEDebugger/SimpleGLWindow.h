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
	};

	enum ResizeType {
		RESIZE_NONE,
		RESIZE_SHRINK_FIT,
		RESIZE_SHRINK_CENTER,
	};

	SimpleGLWindow(HWND wnd);
	~SimpleGLWindow();

	void Clear();
	void Draw(u8 *data, int w, int h, bool flipped = false, Format = FORMAT_8888, ResizeType resize = RESIZE_NONE);
	void Initialize();
	static SimpleGLWindow* getFrom(HWND hwnd);
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void Swap() {
		SwapBuffers(hDC_);
	}

	static void registerClass();
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
	int w_;
	int h_;

	GLSLProgram *drawProgram_;
	GLuint checker_;
	GLuint tex_;
};