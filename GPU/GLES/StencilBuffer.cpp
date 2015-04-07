// Copyright (c) 2014- PPSSPP Project.

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
#include "Core/Reporting.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TextureCache.h"

static const char *stencil_fs =
#ifdef USING_GLES2
"#version 100\n"
"precision highp float;\n"
#endif
"varying vec2 v_texcoord0;\n"
"uniform float u_stencilValue;\n"
"uniform sampler2D tex;\n"
"float roundAndScaleTo255f(in float x) { return floor(x * 255.99); }\n"
"void main() {\n"
"  vec4 index = texture2D(tex, v_texcoord0);\n"
"  gl_FragColor = vec4(index.a);\n"
"  float shifted = roundAndScaleTo255f(index.a) / roundAndScaleTo255f(u_stencilValue);\n"
"  if (mod(floor(shifted), 2.0) < 0.99) discard;\n"
"}\n";

static const char *stencil_vs =
#ifdef USING_GLES2
"#version 100\n"
"precision highp float;\n"
#endif
"attribute vec4 a_position;\n"
"attribute vec2 a_texcoord0;\n"
"varying vec2 v_texcoord0;\n"
"void main() {\n"
"  v_texcoord0 = a_texcoord0;\n"
"  gl_Position = a_position;\n"
"}\n";

static u8 StencilBits5551(const u8 *ptr8, u32 numPixels) {
	const u32 *ptr = (const u32 *)ptr8;

	for (u32 i = 0; i < numPixels / 2; ++i) {
		if (ptr[i] & 0x80008000) {
			return 1;
		}
	}

	return 0;
}

static u8 StencilBits4444(const u8 *ptr8, u32 numPixels) {
	const u32 *ptr = (const u32 *)ptr8;
	u32 bits = 0;

	for (u32 i = 0; i < numPixels / 2; ++i) {
		bits |= ptr[i];
	}

	return ((bits >> 12) & 0xF) | (bits >> 28);
}

static u8 StencilBits8888(const u8 *ptr8, u32 numPixels) {
	const u32 *ptr = (const u32 *)ptr8;
	u32 bits = 0;

	for (u32 i = 0; i < numPixels; ++i) {
		bits |= ptr[i];
	}

	return bits >> 24;
}

bool FramebufferManager::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
	if (!MayIntersectFramebuffer(addr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (MaskedEqual(vfb->fb_address, addr)) {
			dstBuffer = vfb;
		}
	}
	if (!dstBuffer) {
		return false;
	}

	int values = 0;
	u8 usedBits = 0;

	const u8 *src = Memory::GetPointer(addr);
	if (!src)
		return false;

	switch (dstBuffer->format) {
	case GE_FORMAT_565:
		// Well, this doesn't make much sense.
		return false;
	case GE_FORMAT_5551:
		usedBits = StencilBits5551(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 2;
		break;
	case GE_FORMAT_4444:
		usedBits = StencilBits4444(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 16;
		break;
	case GE_FORMAT_8888:
		usedBits = StencilBits8888(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 256;
		break;
	case GE_FORMAT_INVALID:
		// Impossible.
		break;
	}

	if (usedBits == 0) {
		if (skipZero) {
			// Common when creating buffers, it's already 0.  We're done.
			return false;
		}

		// Let's not bother with the shader if it's just zero.
		glstate.scissorTest.disable();
		glstate.colorMask.set(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		glClearColor(0, 0, 0, 0);
		glClearStencil(0);
		glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		return true;
	}

	if (!stencilUploadProgram_) {
		std::string errorString;
		stencilUploadProgram_ = glsl_create_source(stencil_vs, stencil_fs, &errorString);
		if (!stencilUploadProgram_) {
			ERROR_LOG_REPORT(G3D, "Failed to compile stencilUploadProgram! This shouldn't happen.\n%s", errorString.c_str());
		} else {
			glsl_bind(stencilUploadProgram_);
		}

		GLint u_tex = glsl_uniform_loc(stencilUploadProgram_, "tex");
		glUniform1i(u_tex, 0);
	} else {
		glsl_bind(stencilUploadProgram_);
	}

	shaderManager_->DirtyLastShader();

	DisableState();
	glstate.colorMask.set(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glstate.stencilTest.enable();
	glstate.stencilOp.set(GL_REPLACE, GL_REPLACE, GL_REPLACE);

	bool useBlit = false;
	bool useNV = false;

#ifndef USING_GLES2
	if (gl_extensions.FBO_ARB) {
		useNV = false;
		useBlit = true;
	}
#else
	if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
		useNV = !gl_extensions.GLES3;
		useBlit = true;
	}
#endif

	// Our fragment shader (and discard) is slow.  Since the source is 1x, we can stencil to 1x.
	// Then after we're done, we'll just blit it across and stretch it there.
	if (dstBuffer->bufferWidth == dstBuffer->renderWidth || !dstBuffer->fbo) {
		useBlit = false;
	}
	u16 w = useBlit ? dstBuffer->bufferWidth : dstBuffer->renderWidth;
	u16 h = useBlit ? dstBuffer->bufferHeight : dstBuffer->renderHeight;

	FBO *blitFBO = NULL;
	if (useBlit) {
		blitFBO = GetTempFBO(w, h, FBO_8888);
		fbo_bind_as_render_target(blitFBO);
	} else if (dstBuffer->fbo) {
		fbo_bind_as_render_target(dstBuffer->fbo);
	}
	glViewport(0, 0, w, h);

	MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight);
	textureCache_->ForgetLastTexture();

	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);

	glstate.stencilFunc.set(GL_ALWAYS, 0xFF, 0xFF);

	GLint u_stencilValue = glsl_uniform_loc(stencilUploadProgram_, "u_stencilValue");
	for (int i = 1; i < values; i += i) {
		if (!(usedBits & i)) {
			// It's already zero, let's skip it.
			continue;
		}
		// DrawActiveTexture unbinds it, so rebind here before setting uniforms.
		glsl_bind(stencilUploadProgram_);
		if (dstBuffer->format == GE_FORMAT_4444) {
			glstate.stencilMask.set((i << 4) | i);
			glUniform1f(u_stencilValue, i * (16.0f / 255.0f));
		} else if (dstBuffer->format == GE_FORMAT_5551) {
			glstate.stencilMask.set(0xFF);
			glUniform1f(u_stencilValue, i * (128.0f / 255.0f));
		} else {
			glstate.stencilMask.set(i);
			glUniform1f(u_stencilValue, i * (1.0f / 255.0f));
		}
		DrawActiveTexture(0, 0, 0, dstBuffer->width, dstBuffer->height, dstBuffer->bufferWidth, dstBuffer->bufferHeight, false, 0.0f, 0.0f, 1.0f, 1.0f, stencilUploadProgram_);
	}
	glstate.stencilMask.set(0xFF);

	if (useBlit) {
		fbo_bind_as_render_target(dstBuffer->fbo);
		fbo_bind_for_read(blitFBO);
		if (!useNV) {
			glBlitFramebuffer(0, 0, w, h, 0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight, GL_STENCIL_BUFFER_BIT, GL_NEAREST);
		} else {
#if defined(USING_GLES2) && defined(ANDROID)  // We only support this extension on Android, it's not even available on PC.
			glBlitFramebufferNV(0, 0, w, h, 0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight, GL_STENCIL_BUFFER_BIT, GL_NEAREST);
#endif // defined(USING_GLES2) && defined(ANDROID)
		}
	}

	RebindFramebuffer();
	return true;
}