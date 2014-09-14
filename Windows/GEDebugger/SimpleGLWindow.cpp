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

#include <WindowsX.h>
#include "math/lin/matrix4x4.h"
#include "gfx_es2/glsl_program.h"
#include "Common/Common.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"

const PTCHAR SimpleGLWindow::windowClass = _T("SimpleGLWindow");

void SimpleGLWindow::RegisterClass() {
	WNDCLASSEX wndClass;

	wndClass.cbSize         = sizeof(wndClass);
	wndClass.lpszClassName  = windowClass;
	wndClass.hInstance      = GetModuleHandle(0);
	wndClass.lpfnWndProc    = WndProc;
	wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
	wndClass.hIcon          = 0;
	wndClass.lpszMenuName   = 0;
	wndClass.hbrBackground  = (HBRUSH)GetSysColorBrush(COLOR_WINDOW);
	wndClass.style          = CS_DBLCLKS;
	wndClass.cbClsExtra     = 0;
	wndClass.cbWndExtra     = sizeof(SimpleGLWindow *);
	wndClass.hIconSm        = 0;

	RegisterClassEx(&wndClass);
}

static const char tex_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
	"	gl_FragColor.a = clamp(gl_FragColor.a, 0.2, 1.0);\n"
	"}\n";

static const char basic_vs[] =
#ifndef USING_GLES2
	"#version 120\n"
#endif
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"uniform mat4 u_viewproj;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"}\n";

SimpleGLWindow::SimpleGLWindow(HWND wnd)
	: hWnd_(wnd), valid_(false), drawProgram_(nullptr), tex_(0), flags_(0), zoom_(false),
	  dragging_(false), offsetX_(0), offsetY_(0), reformatBuf_(nullptr) {
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG) this);
}

SimpleGLWindow::~SimpleGLWindow() {
	if (drawProgram_ != nullptr) {
		glsl_destroy(drawProgram_);
	}
	if (tex_) {
		glDeleteTextures(1, &tex_);
		glDeleteTextures(1, &checker_);
	}
	delete [] reformatBuf_;
};

void SimpleGLWindow::Initialize(u32 flags) {
	RECT rect;
	GetWindowRect(hWnd_, &rect);

	SetFlags(flags);
	SetupGL();
	ResizeGL(rect.right-rect.left,rect.bottom-rect.top);
	CreateProgram();
	GenerateChecker();
}

void SimpleGLWindow::SetupGL() {
	int pixelFormat;

	static PIXELFORMATDESCRIPTOR pfd = {0};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 16;
	pfd.iLayerType = PFD_MAIN_PLANE;

#define ENFORCE(x, msg) { if (!(x)) { ERROR_LOG(COMMON, "SimpleGLWindow: %s (%08x)", msg, GetLastError()); return; } }

	ENFORCE(hDC_ = GetDC(hWnd_), "Unable to create DC.");
	ENFORCE(pixelFormat = ChoosePixelFormat(hDC_, &pfd), "Unable to match pixel format.");
	ENFORCE(SetPixelFormat(hDC_, pixelFormat, &pfd), "Unable to set pixel format.");
	ENFORCE(hGLRC_ = wglCreateContext(hDC_), "Unable to create GL context.");
	ENFORCE(wglMakeCurrent(hDC_, hGLRC_), "Unable to activate GL context.");

	glewInit();
	valid_ = true;
}

void SimpleGLWindow::ResizeGL(int w, int h) {
	if (!valid_) {
		return;
	}

	wglMakeCurrent(hDC_, hGLRC_);

	glViewport(0, 0, w, h);
	glScissor(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	w_ = w;
	h_ = h;
}

void SimpleGLWindow::CreateProgram() {
	if (!valid_) {
		return;
	}

	wglMakeCurrent(hDC_, hGLRC_);

	drawProgram_ = glsl_create_source(basic_vs, tex_fs);
	glGenTextures(1, &tex_);
	glGenTextures(1, &checker_);

	glsl_bind(drawProgram_);
	glUniform1i(drawProgram_->sampler0, 0);
	glsl_unbind();

	glEnableVertexAttribArray(drawProgram_->a_position);
	glEnableVertexAttribArray(drawProgram_->a_texcoord0);
}

void SimpleGLWindow::GenerateChecker() {
	if (!valid_) {
		return;
	}

	const static u8 checkerboard[] = {
		255,255,255,255, 195,195,195,255,
		195,195,195,255, 255,255,255,255,
	};

	wglMakeCurrent(hDC_, hGLRC_);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, checker_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, checkerboard);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void SimpleGLWindow::DrawChecker() {
	wglMakeCurrent(hDC_, hGLRC_);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glBindTexture(GL_TEXTURE_2D, checker_);

	glDisable(GL_BLEND);
	glViewport(0, 0, w_, h_);
	glScissor(0, 0, w_, h_);

	glsl_bind(drawProgram_);

	float fw = (float)w_, fh = (float)h_;
	const float pos[12] = {0,0,0, fw,0,0, fw,fh,0, 0,fh,0};
	const float texCoords[8] = {0,fh/22, fw/22,fh/22, fw/22,0, 0,0};
	const GLubyte indices[4] = {0,1,3,2};

	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)w_, (float)h_, 0, -1, 1);
	glUniformMatrix4fv(drawProgram_->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
}

void SimpleGLWindow::Draw(const u8 *data, int w, int h, bool flipped, Format fmt) {
	wglMakeCurrent(hDC_, hGLRC_);

	GLint components = GL_RGBA;
	GLint memComponents = 0;
	GLenum glfmt;
	const u8 *finalData = data;
	if (fmt == FORMAT_8888) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glfmt = GL_UNSIGNED_BYTE;
	} else if (fmt == FORMAT_8888_BGRA) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glfmt = GL_UNSIGNED_BYTE;
		memComponents = GL_BGRA;
	} else if (fmt == FORMAT_FLOAT) {
		glfmt = GL_FLOAT;
		components = GL_RED;
	} else if (fmt == FORMAT_24BIT_8X) {
		glfmt = GL_UNSIGNED_INT;
		components = GL_RED;
		finalData = Reformat(data, fmt, w * h);
	} else if (fmt == FORMAT_24X_8BIT) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glfmt = GL_UNSIGNED_BYTE;
		components = GL_RED;
		finalData = Reformat(data, fmt, w * h);
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
		if (fmt == FORMAT_4444) {
			glfmt = GL_UNSIGNED_SHORT_4_4_4_4;
		} else if (fmt == FORMAT_5551) {
			glfmt = GL_UNSIGNED_SHORT_5_5_5_1;
		} else if (fmt == FORMAT_565) {
			glfmt = GL_UNSIGNED_SHORT_5_6_5;
			components = GL_RGB;
		} else if (fmt == FORMAT_4444_REV) {
			glfmt = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		} else if (fmt == FORMAT_5551_REV) {
			glfmt = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		} else if (fmt == FORMAT_565_REV) {
			glfmt = GL_UNSIGNED_SHORT_5_6_5_REV;
			components = GL_RGB;
		} else if (fmt == FORMAT_5551_BGRA_REV) {
			glfmt = GL_UNSIGNED_SHORT_1_5_5_5_REV;
			memComponents = GL_BGRA;
		} else if (fmt == FORMAT_4444_BGRA_REV) {
			glfmt = GL_UNSIGNED_SHORT_4_4_4_4_REV;
			memComponents = GL_BGRA;
		} else if (fmt == FORMAT_16BIT) {
			glfmt = GL_UNSIGNED_SHORT;
			components = GL_RED;
		} else if (fmt == FORMAT_8BIT) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glfmt = GL_UNSIGNED_BYTE;
			components = GL_RED;
		} else {
			_dbg_assert_msg_(COMMON, false, "Invalid SimpleGLWindow format.");
		}
	}

	if (memComponents == 0) {
		memComponents = components;
	}

	glBindTexture(GL_TEXTURE_2D, tex_);
	glTexImage2D(GL_TEXTURE_2D, 0, components, w, h, 0, memComponents, glfmt, finalData);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Reset offset when the texture size changes.
	if (tw_ != w || th_ != h) {
		tw_ = w;
		th_ = h;
		offsetX_ = 0;
		offsetY_ = 0;
	}
	tflipped_ = flipped;

	Redraw();
}

void SimpleGLWindow::GetContentSize(float &x, float &y, float &fw, float &fh) {
	fw = (float)tw_;
	fh = (float)th_;
	x = 0.0f;
	y = 0.0f;

	if (flags_ & (RESIZE_SHRINK_FIT | RESIZE_CENTER) && !zoom_) {
		float wscale = fw / w_, hscale = fh / h_;

		// Too wide, and width is the biggest problem, so scale based on that.
		if (wscale > 1.0f && wscale > hscale) {
			fw = (float)w_;
			fh /= wscale;
		} else if (hscale > 1.0f) {
			fw /= hscale;
			fh = (float)h_;
		}
	}
	if (flags_ & RESIZE_CENTER) {
		x = ((float)w_ - fw) / 2;
		y = ((float)h_ - fh) / 2;
	}

	x += offsetX_;
	y += offsetY_;
}

void SimpleGLWindow::Redraw(bool andSwap) {
	DrawChecker();

	if (tw_ == 0 && th_ == 0) {
		if (andSwap) {
			Swap();
		}
		return;
	}

	if (flags_ & ALPHA_BLEND) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
	} else {
		glDisable(GL_BLEND);
	}
	glViewport(0, 0, w_, h_);
	glScissor(0, 0, w_, h_);

	glBindTexture(GL_TEXTURE_2D, tex_);
	glsl_bind(drawProgram_);

	float fw, fh;
	float x, y;
	GetContentSize(x, y, fw, fh);

	const float pos[12] = {x,y,0, x+fw,y,0, x+fw,y+fh,0, x,y+fh,0};
	static const float texCoords[8] = {0,0, 1,0, 1,1, 0,1};
	static const float texCoordsFlipped[8] = {0,1, 1,1, 1,0, 0,0};
	static const GLubyte indices[4] = {0,1,3,2};

	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)w_, (float)h_, 0, -1, 1);
	glUniformMatrix4fv(drawProgram_->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, tflipped_ ? texCoordsFlipped : texCoords);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);

	if (andSwap) {
		Swap();
	}
}

void SimpleGLWindow::Clear() {
	tw_ = 0;
	th_ = 0;
	Redraw();
}

void SimpleGLWindow::Begin() {
	Redraw(false);

	glDisableVertexAttribArray(drawProgram_->a_position);
	glDisableVertexAttribArray(drawProgram_->a_texcoord0);
}

void SimpleGLWindow::End() {
	glEnableVertexAttribArray(drawProgram_->a_position);
	glEnableVertexAttribArray(drawProgram_->a_texcoord0);

	Swap();
}

bool SimpleGLWindow::DragStart(int mouseX, int mouseY) {
	// Only while zoomed in, otherwise it's shrink to fit mode or fixed.
	if (!zoom_) {
		return false;
	}

	dragging_ = true;
	SetCapture(hWnd_);
	dragStartX_ = mouseX - offsetX_;
	dragStartY_ = mouseY - offsetY_;
	dragLastUpdate_ = GetTickCount();

	return true;
}

bool SimpleGLWindow::DragContinue(int mouseX, int mouseY) {
	if (!dragging_) {
		return false;
	}

	offsetX_ = mouseX - dragStartX_;
	offsetY_ = mouseY - dragStartY_;

	const u32 MS_BETWEEN_DRAG_REDRAWS = 5;
	if (GetTickCount() - dragLastUpdate_ > MS_BETWEEN_DRAG_REDRAWS) {
		Redraw();
	}

	return true;
}

bool SimpleGLWindow::DragEnd(int mouseX, int mouseY) {
	if (!dragging_) {
		return false;
	}

	dragging_ = false;
	ReleaseCapture();
	Redraw();

	return true;
}

bool SimpleGLWindow::ToggleZoom() {
	// Reset the offset when zooming out (or in, doesn't matter.)
	offsetX_ = 0;
	offsetY_ = 0;

	zoom_ = !zoom_;
	Redraw();

	return true;
}

const u8 *SimpleGLWindow::Reformat(const u8 *data, Format fmt, u32 numPixels) {
	if (!reformatBuf_ || reformatBufSize_ < numPixels) {
		delete [] reformatBuf_;
		reformatBuf_ = new u32[numPixels];
		reformatBufSize_ = numPixels;
	}

	const u32 *data32 = (const u32 *)data;
	if (fmt == FORMAT_24BIT_8X) {
		for (u32 i = 0; i < numPixels; ++i) {
			reformatBuf_[i] = (data32[i] << 8) | ((data32[i] >> 16) & 0xFF);
		}
	} else if (fmt == FORMAT_24X_8BIT) {
		u8 *buf8 = (u8 *)reformatBuf_;
		for (u32 i = 0; i < numPixels; ++i) {
			u32 v = (data32[i] >> 24) & 0xFF;
			buf8[i] = v;
		}
	}
	return (const u8 *)reformatBuf_;
}

SimpleGLWindow *SimpleGLWindow::GetFrom(HWND hwnd) {
	return (SimpleGLWindow*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

LRESULT CALLBACK SimpleGLWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SimpleGLWindow *win = SimpleGLWindow::GetFrom(hwnd);

	switch(msg)
	{
	case WM_NCCREATE:
		win = new SimpleGLWindow(hwnd);
		
		// Continue with window creation.
		return win != NULL ? TRUE : FALSE;

	case WM_NCDESTROY:
		delete win;
		return 0;

	case WM_LBUTTONDBLCLK:
		if (win->ToggleZoom()) {
			return 0;
		}
		break;

	case WM_LBUTTONDOWN:
		if (win->DragStart(GET_X_LPARAM(lParam),  GET_Y_LPARAM(lParam))) {
			return 0;
		}
		break;

	case WM_LBUTTONUP:
		if (win->DragEnd(GET_X_LPARAM(lParam),  GET_Y_LPARAM(lParam))) {
			return 0;
		}
		break;

	case WM_MOUSEMOVE:
		if (win->DragContinue(GET_X_LPARAM(lParam),  GET_Y_LPARAM(lParam))) {
			return 0;
		}
		break;

	case WM_PAINT:
		win->Redraw();
		break;
	}
	
	return DefWindowProc(hwnd, msg, wParam, lParam);
}