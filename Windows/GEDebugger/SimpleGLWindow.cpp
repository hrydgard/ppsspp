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

#include "math/lin/matrix4x4.h"
#include "gfx_es2/glsl_program.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"

const PTCHAR SimpleGLWindow::windowClass = _T("SimpleGLWindow");
bool SimpleGLWindow::windowClassExists_;

static const char tex_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"	gl_FragColor.rgb = texture2D(sampler0, v_texcoord0).rgb;\n"
	"	gl_FragColor.a = 1.0;\n"
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

SimpleGLWindow::SimpleGLWindow(HINSTANCE hInstance, HWND hParent, int x, int y, int w, int h)
	: hInstance_(hInstance), hParent_(hParent), valid_(false), drawProgram_(NULL), tex_(0) {
	RegisterWindowClass();
	Create(x, y, w, h);
	SetupGL();
	ResizeGL(w, h);
	CreateProgram();
	GenerateChecker();
}

SimpleGLWindow::~SimpleGLWindow() {
	if (drawProgram_ != NULL) {
		glsl_destroy(drawProgram_);
	}
	if (tex_) {
		glDeleteTextures(1, &tex_);
		glDeleteTextures(1, &checker_);
	}
};

void SimpleGLWindow::RegisterWindowClass() {
	if (windowClassExists_) {
		return;
	}

	static WNDCLASSEX wndClass = {
		sizeof(WNDCLASSEX),
		CS_HREDRAW | CS_VREDRAW,
		DefWindowProc,
		0,
		0,
		hInstance_,
		NULL,
		LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)GetStockObject(BLACK_BRUSH),
		NULL,
		windowClass,
		NULL,
	};
	RegisterClassEx(&wndClass);
	windowClassExists_ = true;
}

void SimpleGLWindow::Create(int x, int y, int w, int h) {
	DWORD style = WS_CHILD | WS_VISIBLE;
	hWnd_ = CreateWindowEx(0, windowClass, _T(""), style, x, y, w, h, hParent_, NULL, hInstance_, NULL);
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

void SimpleGLWindow::Draw(u8 *data, int w, int h, bool flipped, Format fmt, ResizeType resize) {
	DrawChecker();

	glDisable(GL_BLEND);
	glViewport(0, 0, w_, h_);
	glScissor(0, 0, w_, h_);

	GLint components = GL_RGBA;
	GLenum glfmt;
	if (fmt == FORMAT_8888) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glfmt = GL_UNSIGNED_BYTE;
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
		if (fmt == FORMAT_4444) {
			glfmt = GL_UNSIGNED_SHORT_4_4_4_4;
		} else if (fmt == FORMAT_5551) {
			glfmt = GL_UNSIGNED_SHORT_5_5_5_1;
		} else if (fmt == FORMAT_565) {
			glfmt = GL_UNSIGNED_SHORT_5_6_5;
			components = GL_RGB;
		}
	}

	glBindTexture(GL_TEXTURE_2D, tex_);
	glTexImage2D(GL_TEXTURE_2D, 0, components, w, h, 0, components, glfmt, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glsl_bind(drawProgram_);

	float fw = (float)w, fh = (float)h;
	float x = 0.0f, y = 0.0f;
	if (resize == RESIZE_SHRINK_FIT || resize == RESIZE_SHRINK_CENTER) {
		float wscale = fw / w_, hscale = fh / h_;

		// Too wide, and width is the biggest problem, so scale based on that.
		if (wscale > 1.0f && wscale > hscale) {
			fw = (float)w_;
			fh /= wscale;
		} else if (hscale > 1.0f) {
			fw /= hscale;
			fh = (float)h_;
		}

		if (resize == RESIZE_SHRINK_CENTER) {
			x = ((float)w_ - fw) / 2;
			y = ((float)h_ - fh) / 2;
		}
	}
	const float pos[12] = {x,y,0, x+fw,y,0, x+fw,y+fh,0, x,y+fh,0};
	static const float texCoords[8] = {0,0, 1,0, 1,1, 0,1};
	static const float texCoordsFlipped[8] = {0,1, 1,1, 1,0, 0,0};
	static const GLubyte indices[4] = {0,1,3,2};

	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)w_, (float)h_, 0, -1, 1);
	glUniformMatrix4fv(drawProgram_->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, flipped ? texCoordsFlipped : texCoords);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);

	Swap();
}

void SimpleGLWindow::Clear() {
	DrawChecker();
	Swap();
}
