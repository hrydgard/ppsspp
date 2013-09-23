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
}

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

	glsl_bind(drawProgram_);
	glUniform1i(drawProgram_->sampler0, 0);
	glsl_unbind();
}

void SimpleGLWindow::Draw(u8 *data, int w, int h) {
	wglMakeCurrent(hDC_, hGLRC_);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glDisable(GL_BLEND);
	glViewport(0, 0, w_, h_);
	glScissor(0, 0, w_, h_);

	glBindTexture(GL_TEXTURE_2D, tex_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glsl_bind(drawProgram_);

	float fw = (float)w, fh = (float)h;
	const float pos[12] = {0,0,0, fw,0,0, fw,fh,0, 0,fh,0};
	const float texCoords[8] = {0,1, 1,1, 1,0, 0,0};
	const GLubyte indices[4] = {0,1,3,2};

	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)w_, (float)h_, 0, -1, 1);
	glUniformMatrix4fv(drawProgram_->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glVertexAttribPointer(drawProgram_->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(drawProgram_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	glEnableVertexAttribArray(drawProgram_->a_position);
	glEnableVertexAttribArray(drawProgram_->a_texcoord0);
	glUniform1i(drawProgram_->sampler0, 0);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
	glDisableVertexAttribArray(drawProgram_->a_position);
	glDisableVertexAttribArray(drawProgram_->a_texcoord0);

	glBindTexture(GL_TEXTURE_2D, 0);

	Swap();
}

void SimpleGLWindow::Clear() {
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	Swap();
}
