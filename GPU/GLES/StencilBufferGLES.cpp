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
#include "Core/Reporting.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"

static const char *stencil_fs =
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"#if __VERSION__ >= 130\n"
"#define varying in\n"
"#define texture2D texture\n"
"#define gl_FragColor fragColor0\n"
"out vec4 fragColor0;\n"
"#endif\n"
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
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"#if __VERSION__ >= 130\n"
"#define attribute in\n"
"#define varying out\n"
"#endif\n"
"attribute vec4 a_position;\n"
"attribute vec2 a_texcoord0;\n"
"varying vec2 v_texcoord0;\n"
"void main() {\n"
"  v_texcoord0 = a_texcoord0;\n"
"  gl_Position = a_position;\n"
"}\n";

bool FramebufferManagerGLES::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
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
		if (dstBuffer->fbo) {
			draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR });
		}
		render_->Clear(0, 0, 0, GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT, 0x8, 0, 0, 0, 0);
		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
		return true;
	}

	if (!stencilUploadProgram_) {
		std::string errorString;
		static std::string vs_code, fs_code;
		vs_code = ApplyGLSLPrelude(stencil_vs, GL_VERTEX_SHADER);
		fs_code = ApplyGLSLPrelude(stencil_fs, GL_FRAGMENT_SHADER);
		std::vector<GLRShader *> shaders;
		shaders.push_back(render_->CreateShader(GL_VERTEX_SHADER, vs_code, "stencil"));
		shaders.push_back(render_->CreateShader(GL_FRAGMENT_SHADER, fs_code, "stencil"));
		std::vector<GLRProgram::UniformLocQuery> queries;
		queries.push_back({ &u_stencilUploadTex, "tex" });
		queries.push_back({ &u_stencilValue, "u_stencilValue" });
		std::vector<GLRProgram::Initializer> inits;
		inits.push_back({ &u_stencilUploadTex, 0, 0 });
		stencilUploadProgram_ = render_->CreateProgram(shaders, {}, queries, inits, false);
		for (auto iter : shaders) {
			render_->DeleteShader(iter);
		}
		if (!stencilUploadProgram_) {
			ERROR_LOG_REPORT(G3D, "Failed to compile stencilUploadProgram! This shouldn't happen.\n%s", errorString.c_str());
		}
	}

	shaderManagerGL_->DirtyLastShader();

	bool useBlit = gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT | GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT);

	// Our fragment shader (and discard) is slow.  Since the source is 1x, we can stencil to 1x.
	// Then after we're done, we'll just blit it across and stretch it there.
	if (dstBuffer->bufferWidth == dstBuffer->renderWidth || !dstBuffer->fbo) {
		useBlit = false;
	}
	u16 w = useBlit ? dstBuffer->bufferWidth : dstBuffer->renderWidth;
	u16 h = useBlit ? dstBuffer->bufferHeight : dstBuffer->renderHeight;

	Draw::Framebuffer *blitFBO = nullptr;
	if (useBlit) {
		blitFBO = GetTempFBO(TempFBO::COPY, w, h, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(blitFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
	} else if (dstBuffer->fbo) {
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR });
	}
	render_->SetViewport({ 0, 0, (float)w, (float)h, 0.0f, 1.0f });

	float u1 = 1.0f;
	float v1 = 1.0f;
	MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight, u1, v1);
	textureCacheGL_->ForgetLastTexture();

	// We must bind the program after starting the render pass, and set the color mask after clearing.
	render_->SetDepth(false, false, GL_ALWAYS);
	render_->Clear(0, 0, 0, GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, 0x8, 0, 0, 0, 0);
	render_->SetStencilFunc(GL_TRUE, GL_ALWAYS, 0xFF, 0xFF);
	render_->SetRaster(false, GL_CCW, GL_FRONT, GL_FALSE);
	render_->BindProgram(stencilUploadProgram_);
	render_->SetNoBlendAndMask(0x8);

	for (int i = 1; i < values; i += i) {
		if (!(usedBits & i)) {
			// It's already zero, let's skip it.
			continue;
		}
		if (dstBuffer->format == GE_FORMAT_4444) {
			render_->SetStencilOp((i << 4) | i, GL_REPLACE, GL_REPLACE, GL_REPLACE);
			render_->SetUniformF1(&u_stencilValue, i * (16.0f / 255.0f));
		} else if (dstBuffer->format == GE_FORMAT_5551) {
			render_->SetStencilOp(0xFF, GL_REPLACE, GL_REPLACE, GL_REPLACE);
			render_->SetUniformF1(&u_stencilValue, i * (128.0f / 255.0f));
		} else {
			render_->SetStencilOp(i, GL_REPLACE, GL_REPLACE, GL_REPLACE);
			render_->SetUniformF1(&u_stencilValue, i * (1.0f / 255.0f));
		}
		DrawActiveTexture(0, 0, dstBuffer->width, dstBuffer->height, dstBuffer->bufferWidth, dstBuffer->bufferHeight, 0.0f, 0.0f, u1, v1, ROTATION_LOCKED_HORIZONTAL, DRAWTEX_NEAREST | DRAWTEX_KEEP_STENCIL_ALPHA);
	}

	if (useBlit) {
		draw_->BlitFramebuffer(blitFBO, 0, 0, w, h, dstBuffer->fbo, 0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight, Draw::FB_STENCIL_BIT, Draw::FB_BLIT_NEAREST);
	}

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
	RebindFramebuffer();
	return true;
}
