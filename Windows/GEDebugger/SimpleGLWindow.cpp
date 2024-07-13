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

#include "Common/CommonTypes.h"
#include "Common/CommonWindows.h"
#include "Common/Log.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/OpenGL/GLSLProgram.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GL/gl.h"
#include "GL/wglew.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Windows/W32Util/ContextMenu.h"

#include "Core/System.h"

const wchar_t *SimpleGLWindow::windowClass = L"SimpleGLWindow";

using namespace Lin;

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
	"#version 120\n"
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"uniform mat4 u_viewproj;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"}\n";

SimpleGLWindow::SimpleGLWindow(HWND wnd)
	: hWnd_(wnd) {
	SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR) this);
}

SimpleGLWindow::~SimpleGLWindow() {
	if (vao_ != 0) {
		glDeleteVertexArrays(1, &vao_);
	}
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

#define ENFORCE(x, msg) { if (!(x)) { ERROR_LOG(Log::Common, "SimpleGLWindow: %s (%08x)", msg, (uint32_t)GetLastError()); return; } }

	ENFORCE(hDC_ = GetDC(hWnd_), "Unable to create DC.");
	ENFORCE(pixelFormat = ChoosePixelFormat(hDC_, &pfd), "Unable to match pixel format.");
	ENFORCE(SetPixelFormat(hDC_, pixelFormat, &pfd), "Unable to set pixel format.");
	ENFORCE(hGLRC_ = wglCreateContext(hDC_), "Unable to create GL context.");
	ENFORCE(wglMakeCurrent(hDC_, hGLRC_), "Unable to activate GL context.");

	valid_ = glewInit() == GLEW_OK;

	// Switch to a modern context so RenderDoc doesn't get mad.
	HGLRC oldGL = hGLRC_;
	if (wglewIsSupported("WGL_ARB_create_context") == 1) {
		static const int attribs33[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
			WGL_CONTEXT_MINOR_VERSION_ARB, 3,
			WGL_CONTEXT_FLAGS_ARB, 0,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			0,
		};
		hGLRC_ = wglCreateContextAttribsARB(hDC_, 0, attribs33);

		if (!hGLRC_) {
			hGLRC_ = oldGL;
		} else {
			// Switch to the new ARB context.
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(oldGL);
			wglMakeCurrent(hDC_, hGLRC_);

			valid_ = glewInit() == GLEW_OK;
		}
	}
}

void SimpleGLWindow::ResizeGL(int w, int h) {
	if (!valid_) {
		return;
	}

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

	if (gl_extensions.ARB_vertex_array_object) {
		glGenVertexArrays(1, &vao_);
		glBindVertexArray(vao_);

		glGenBuffers(1, &ibuf_);
		glGenBuffers(1, &vbuf_);

		const GLubyte indices[4] = {0, 1, 3, 2};
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf_);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, vbuf_);
	} else {
		vao_ = 0;
	}

	glEnableVertexAttribArray(drawProgram_->a_position);
	glEnableVertexAttribArray(drawProgram_->a_texcoord0);
}

void SimpleGLWindow::GenerateChecker() {
	if (!valid_) {
		return;
	}

	// 2x2 RGBA bitmap
	static const u8 checkerboard[] = {
		192,192,192,255, 128,128,128,255,
		128,128,128,255, 192,192,192,255,
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
	if (vao_) {
		glBufferData(GL_ARRAY_BUFFER, sizeof(pos) + sizeof(texCoords), nullptr, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pos), pos);
		glBufferSubData(GL_ARRAY_BUFFER, sizeof(pos), sizeof(texCoords), texCoords);
		glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
		glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (const void *)sizeof(pos));
	} else {
		glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
		glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	}
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, vao_ ? 0 : indices);
}

void SimpleGLWindow::Draw(const u8 *data, int w, int h, bool flipped, Format fmt) {
	wglMakeCurrent(hDC_, hGLRC_);

	GLint components = GL_RGBA;
	GLint memComponents = 0;
	GLenum glfmt = GL_UNSIGNED_BYTE;
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
	} else if (fmt == FORMAT_FLOAT_DIV_256) {
		glfmt = GL_UNSIGNED_INT;
		components = GL_RED;
		finalData = Reformat(data, fmt, w * h);
	} else if (fmt == FORMAT_24BIT_8X) {
		glfmt = GL_UNSIGNED_INT;
		components = GL_RED;
		finalData = Reformat(data, fmt, w * h);
	} else if (fmt == FORMAT_24BIT_8X_DIV_256) {
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
			_dbg_assert_msg_(false, "Invalid SimpleGLWindow format.");
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

	if ((flags_ & RESIZE_SHRINK_FIT) != 0 && !zoom_) {
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
	if ((flags_ & RESIZE_GROW_FIT) != 0 && !zoom_) {
		float wscale = fw / w_, hscale = fh / h_;

		if (wscale > hscale && wscale < 1.0f) {
			fw = (float)w_;
			fh /= wscale;
		} else if (hscale > wscale && hscale < 1.0f) {
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

	auto swapWithCallback = [andSwap, this]() {
		if (andSwap) {
			swapped_ = false;
			if (redrawCallback_ && !inRedrawCallback_) {
				inRedrawCallback_ = true;
				redrawCallback_();
				inRedrawCallback_ = false;
			}
			// In case the callback swaps, don't do it twice.
			if (!swapped_) {
				Swap();
			}
		}
	};

	if (tw_ == 0 && th_ == 0) {
		swapWithCallback();
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
	static const float texCoordsNormal[8] = {0,0, 1,0, 1,1, 0,1};
	static const float texCoordsFlipped[8] = {0,1, 1,1, 1,0, 0,0};
	static const GLubyte indices[4] = {0,1,3,2};
	const float *texCoords = tflipped_ ? texCoordsFlipped : texCoordsNormal;

	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)w_, (float)h_, 0, -1, 1);
	glUniformMatrix4fv(drawProgram_->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	if (vao_) {
		glBufferData(GL_ARRAY_BUFFER, sizeof(pos) + sizeof(texCoordsNormal), nullptr, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pos), pos);
		glBufferSubData(GL_ARRAY_BUFFER, sizeof(pos), sizeof(texCoordsNormal), texCoords);
		glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
		glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (const void *)sizeof(pos));
	} else {
		glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
		glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	}
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, vao_ ? 0 : indices);

	if (andSwap) {
		swapWithCallback();
	}
}

void SimpleGLWindow::Clear() {
	tw_ = 0;
	th_ = 0;
	Redraw();
}

void SimpleGLWindow::Begin() {
	if (!inRedrawCallback_) {
		Redraw(false);
	}

	if (vao_) {
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	} else {
		glDisableVertexAttribArray(drawProgram_->a_position);
		glDisableVertexAttribArray(drawProgram_->a_texcoord0);
	}
}

void SimpleGLWindow::End() {
	if (vao_) {
		glBindVertexArray(vao_);
		glBindBuffer(GL_ARRAY_BUFFER, vbuf_);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf_);
	} else {
		glEnableVertexAttribArray(drawProgram_->a_position);
		glEnableVertexAttribArray(drawProgram_->a_texcoord0);
	}

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

POINT SimpleGLWindow::PosFromMouse(int mouseX, int mouseY) {
	float fw, fh;
	float x, y;
	GetContentSize(x, y, fw, fh);

	if (mouseX < x || mouseX >= x + fw || mouseY < y || mouseY >= y + fh) {
		// Outside of bounds.
		return POINT{ -1, -1 };
	}

	float tx = (mouseX - x) * (tw_ / fw);
	float ty = (mouseY - y) * (th_ / fh);

	return POINT{ (int)tx, (int)ty };
}

bool SimpleGLWindow::Hover(int mouseX, int mouseY) {
	if (hoverCallback_ == nullptr) {
		return false;
	}

	POINT pos = PosFromMouse(mouseX, mouseY);
	hoverCallback_(pos.x, pos.y);

	if (pos.x == -1 || pos.y == -1) {
		// Outside of bounds, don't track.
		return true;
	}

	// Find out when they are done.
	TRACKMOUSEEVENT tracking = {0};
	tracking.cbSize = sizeof(tracking);
	tracking.dwFlags = TME_LEAVE;
	tracking.hwndTrack = hWnd_;
	TrackMouseEvent(&tracking);

	return true;
}

bool SimpleGLWindow::Leave() {
	if (hoverCallback_ == nullptr) {
		return false;
	}

	hoverCallback_(-1, -1);
	return true;
}

bool SimpleGLWindow::RightClick(int mouseX, int mouseY) {
	if (rightClickCallback_ == nullptr) {
		return false;
	}

	POINT pt{mouseX, mouseY};
	POINT pos = PosFromMouse(mouseX, mouseY);

	rightClickCallback_(0, pos.x, pos.y);

	// We don't want to let the users play with deallocated or uninitialized debugging objects
	GlobalUIState state = GetUIState();
	if (state != UISTATE_INGAME && state != UISTATE_PAUSEMENU) {
		return true;
	}

	int result = TriggerContextMenu(rightClickMenu_, hWnd_, ContextPoint::FromClient(pt));
	if (result > 0) {
		rightClickCallback_(result, pos.x, pos.y);
	}

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
	} else if (fmt == FORMAT_24BIT_8X_DIV_256) {
		for (u32 i = 0; i < numPixels; ++i) {
			int z24 = data32[i] & 0x00FFFFFF;
			int z16 = z24 - 0x800000 + 0x8000;
			reformatBuf_[i] = (z16 << 16) | z16;
		}
	} else if (fmt == FORMAT_FLOAT_DIV_256) {
		for (u32 i = 0; i < numPixels; ++i) {
			double z = *(float *)&data32[i];
			int z24 = (int)(z * 16777215.0);
			int z16 = z24 - 0x800000 + 0x8000;
			reformatBuf_[i] = (z16 << 16) | z16;
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

	int mouseX = 0, mouseY = 0;
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MOUSEMOVE:
		mouseX = (int)(short)LOWORD(lParam);
		mouseY = (int)(short)HIWORD(lParam);
		break;
	default:
		break;
	}

	switch (msg) {
	case WM_NCCREATE:
		win = new SimpleGLWindow(hwnd);
		
		// Continue with window creation.
		return win != nullptr ? TRUE : FALSE;

	case WM_NCDESTROY:
		delete win;
		return 0;

	case WM_LBUTTONDBLCLK:
		if (win->ToggleZoom()) {
			return 0;
		}
		break;

	case WM_LBUTTONDOWN:
		if (win->DragStart(mouseX, mouseY)) {
			return 0;
		}
		break;

	case WM_LBUTTONUP:
		if (win->DragEnd(mouseX, mouseY)) {
			return 0;
		}
		break;

	case WM_MOUSEMOVE:
		if (win->DragContinue(mouseX, mouseY)) {
			return 0;
		}
		if (win->Hover(mouseX, mouseY)) {
			return 0;
		}
		break;

	case WM_RBUTTONUP:
		if (win->RightClick(mouseX, mouseY)) {
			return 0;
		}
		break;

	case WM_MOUSELEAVE:
		if (win->Leave()) {
			return 0;
		}
		break;

	case WM_PAINT:
		win->Redraw();
		break;
	}
	
	return DefWindowProc(hwnd, msg, wParam, lParam);
}
