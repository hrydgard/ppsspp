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

#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "math/lin/matrix4x4.h"

#include "../../Core/Host.h"
#include "../../Core/MemMap.h"
#include "../ge_constants.h"
#include "../GPUState.h"

#include "Framebuffer.h"

const char tex_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
	"}\n";

const char basic_vs[] =
#ifndef USING_GLES2
	"#version 120\n"
#endif
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"uniform mat4 u_viewproj;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"}\n";

FramebufferManager::FramebufferManager() {
	glGenTextures(1, &backbufTex);

	//initialize backbuffer texture
	glBindTexture(GL_TEXTURE_2D, backbufTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 480, 272, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	draw2dprogram = glsl_create_source(basic_vs, tex_fs);

	glsl_bind(draw2dprogram);
	glUniform1i(draw2dprogram->sampler0, 0);
	glsl_unbind();

	// And an initial clear. We don't clear per frame as the games are supposed to handle that
	// by themselves.
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	convBuf = new u8[480 * 272 * 4];
}

FramebufferManager::~FramebufferManager() {
	glDeleteTextures(1, &backbufTex);
	glsl_destroy(draw2dprogram);
	delete [] convBuf;
}

void FramebufferManager::DrawPixels(const u8 *framebuf, int pixelFormat, int linesize) {
	// TODO: We can trivially do these in the shader, and there's no need to
	// upconvert to 8888 for the 16-bit formats.
	for (int y = 0; y < 272; y++) {
		switch (pixelFormat) {
		case PSP_DISPLAY_PIXEL_FORMAT_565:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col) & 0x1f) << 3;
					dst[x * 4 + 1] = ((col >> 5) & 0x3f) << 2;
					dst[x * 4 + 2] = ((col >> 11) & 0x1f) << 3;
					dst[x * 4 + 3] = 255;
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_5551:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col) & 0x1f) << 3;
					dst[x * 4 + 1] = ((col >> 5) & 0x1f) << 3;
					dst[x * 4 + 2] = ((col >> 10) & 0x1f) << 3;
					dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_8888:
			{
				const u8 *src = framebuf + linesize * 4 * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					dst[x * 4] = src[x * 4];
					dst[x * 4 + 1] = src[x * 4 + 3];
					dst[x * 4 + 2] = src[x * 4 + 2];
					dst[x * 4 + 3] = src[x * 4 + 1];
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_4444:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col >> 8) & 0xf) << 4;
					dst[x * 4 + 1] = ((col >> 4) & 0xf) << 4;
					dst[x * 4 + 2] = (col & 0xf) << 4;
					dst[x * 4 + 3] = (col >> 12) << 4;
				}
			}
			break;
		}
	}

	glBindTexture(GL_TEXTURE_2D,backbufTex);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,480,272, GL_RGBA, GL_UNSIGNED_BYTE, convBuf);
	DrawActiveTexture(480, 272);
}

void FramebufferManager::DrawActiveTexture(float w, float h, bool flip) {
	float u2 = 1.0f;
	float v1 = flip ? 1.0f : 0.0f;
	float v2 = flip ? 0.0f : 1.0f;

	const float pos[12] = {0,0,0, w,0,0, w,h,0, 0,h,0};
	const float texCoords[8] = {0, v1, u2, v1, u2, v2, 0, v2};

	glsl_bind(draw2dprogram);
	Matrix4x4 ortho;
	ortho.setOrtho(0, 480, 272, 0, -1, 1);
	glUniformMatrix4fv(draw2dprogram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(draw2dprogram->a_position);
	glEnableVertexAttribArray(draw2dprogram->a_texcoord0);
	glVertexAttribPointer(draw2dprogram->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(draw2dprogram->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);	
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// TODO: TRIANGLE_STRIP is more likely to be optimized.
	glDisableVertexAttribArray(draw2dprogram->a_position);
	glDisableVertexAttribArray(draw2dprogram->a_texcoord0);
	glsl_unbind();
}
