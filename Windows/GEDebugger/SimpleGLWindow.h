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

#include <functional>
#include "CommonWindows.h"

#include "Common/GPU/OpenGL/GLSLProgram.h"
#include "Common/CommonWindows.h"
#include "Windows/W32Util/ContextMenu.h"

struct SimpleGLWindow {
	static const wchar_t *windowClass;

	enum Format {
		FORMAT_565_REV = 0x00,
		FORMAT_5551_REV = 0x01,
		FORMAT_4444_REV = 0x02,
		FORMAT_8888 = 0x03,
		FORMAT_565 = 0x04,
		FORMAT_5551 = 0x05,
		FORMAT_4444 = 0x06,
		FORMAT_5551_BGRA_REV = 0x09,
		FORMAT_4444_BGRA_REV = 0x0A,
		FORMAT_8888_BGRA = 0x0B,

		FORMAT_FLOAT = 0x10,
		FORMAT_16BIT = 0x11,
		FORMAT_8BIT = 0x12,
		FORMAT_24BIT_8X = 0x13,
		FORMAT_24X_8BIT = 0x14,

		FORMAT_FLOAT_DIV_256 = 0x18,
		FORMAT_24BIT_8X_DIV_256 = 0x1B,
	};

	enum Flags {
		RESIZE_NONE = 0x00,
		RESIZE_CENTER = 0x01,
		RESIZE_SHRINK_FIT = 0x02,
		RESIZE_SHRINK_CENTER = 0x03,
		RESIZE_GROW_FIT = 0x04,
		RESIZE_GROW_CENTER = 0x05,
		RESIZE_BEST_FIT = 0x06,
		RESIZE_BEST_CENTER = 0x07,

		ALPHA_IGNORE = 0x00,
		ALPHA_BLEND = 0x08,
	};

	SimpleGLWindow(HWND wnd);
	~SimpleGLWindow();

	void Clear();
	void Draw(const u8 *data, int w, int h, bool flipped = false, Format = FORMAT_8888);
	void Redraw(bool andSwap = true);
	void Initialize(u32 flags);
	static SimpleGLWindow *GetFrom(HWND hwnd);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// To draw something custom.
	void Begin();
	void End();

	void SetFlags(u32 flags) {
		flags_ = flags;
	}

	void Swap() {
		swapped_ = true;
		SwapBuffers(hDC_);
	}

	int Width() {
		return w_;
	}

	int Height() {
		return h_;
	}

	bool HasTex() {
		return tw_ > 0 && th_ > 0;
	}

	int TexWidth() {
		return tw_;
	}

	int TexHeight() {
		return th_;
	}

	void GetContentSize(float &x, float &y, float &fw, float &fh);

	void SetRedrawCallback(std::function<void()> callback) {
		redrawCallback_ = callback;
	}

	void SetHoverCallback(std::function<void(int, int)> hoverCallback) {
		hoverCallback_ = hoverCallback;
	}

	// Called first with 0 that it's opening, then the selected item.
	void SetRightClickMenu(ContextMenuID menu, std::function<void(int, int, int)> callback) {
		rightClickCallback_ = callback;
		rightClickMenu_ = menu;
	}

	static void RegisterClass();
protected:
	void SetupGL();
	void ResizeGL(int w, int h);
	void CreateProgram();
	void GenerateChecker();
	void DrawChecker();
	bool DragStart(int mouseX, int mouseY);
	bool DragContinue(int mouseX, int mouseY);
	bool DragEnd(int mouseX, int mouseY);
	bool Hover(int mouseX, int mouseY);
	bool Leave();
	bool RightClick(int mouseX, int mouseY);
	bool ToggleZoom();
	POINT PosFromMouse(int mouseX, int mouseY);
	const u8 *Reformat(const u8 *data, Format fmt, u32 numPixels);

	HWND hWnd_;
	HDC hDC_;
	HGLRC hGLRC_;
	bool valid_ = false;
	// Width and height of the window.
	int w_;
	int h_;
	// Last texture size/flipped for Redraw().
	int tw_;
	int th_;
	bool tflipped_;

	GLSLProgram *drawProgram_ = nullptr;
	GLuint vao_ = 0;
	GLuint ibuf_ = 0;
	GLuint vbuf_ = 0;
	GLuint checker_ = 0;
	GLuint tex_ = 0;
	u32 flags_ = 0;
	// Disable shrink (toggled by double click.)
	bool zoom_ = false;
	bool dragging_ = false;
	bool inRedrawCallback_ = false;
	bool swapped_ = false;
	int dragStartX_;
	int dragStartY_;
	u32 dragLastUpdate_;
	// Offset to position the texture is drawn at.
	int offsetX_ = 0;
	int offsetY_ = 0;
	u32 *reformatBuf_ = nullptr;
	u32 reformatBufSize_ = 0;

	std::function<void()> redrawCallback_;
	std::function<void(int, int)> hoverCallback_;
	std::function<void(int, int, int)> rightClickCallback_;
	ContextMenuID rightClickMenu_;
};
