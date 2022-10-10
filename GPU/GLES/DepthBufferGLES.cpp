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

#include <algorithm>

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Core/ConfigValues.h"
#include "Core/Reporting.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"

static const char *depth_dl_fs = R"(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif
#if __VERSION__ >= 130
#define varying in
#define texture2D texture
#define gl_FragColor fragColor0
out vec4 fragColor0;
#endif
varying vec2 v_texcoord0;
uniform vec2 u_depthFactor;
uniform vec4 u_depthShift;
uniform vec4 u_depthTo8;
uniform sampler2D tex;
void main() {
  float depth = texture2D(tex, v_texcoord0).r;
  // At this point, clamped maps [0, 1] to [0, 65535].
  float clamped = clamp((depth + u_depthFactor.x) * u_depthFactor.y, 0.0, 1.0);

  vec4 enc = u_depthShift * clamped;
  enc = floor(mod(enc, 256.0)) * u_depthTo8;
  // Let's ignore the bits outside 16 bit precision.
  gl_FragColor = enc.yzww;
}
)";

static const char *depth_vs = R"(
#ifdef GL_ES
precision highp float;
#endif
#if __VERSION__ >= 130
#define attribute in
#define varying out
#endif
attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;
void main() {
  v_texcoord0 = a_texcoord0;
  gl_Position = a_position;
}
)";

static bool SupportsDepthTexturing() {
	if (gl_extensions.IsGLES) {
		return gl_extensions.OES_packed_depth_stencil && (gl_extensions.OES_depth_texture || gl_extensions.GLES3);
	}
	return gl_extensions.VersionGEThan(3, 0);
}

void FramebufferManagerGLES::ReadbackDepthbufferSync(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "ReadbackDepthbufferSync: vfb->fbo == 0");
		return;
	}

	const u32 z_address = vfb->z_address;
	DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

	int dstByteOffset = y * vfb->z_stride * sizeof(u16);
	u16 *depth = (u16 *)Memory::GetPointer(z_address + dstByteOffset);
	ReadbackDepthbufferSync(vfb->fbo, x, y, w, h, depth, vfb->z_stride);

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
}

void FramebufferManagerGLES::ReadbackDepthbufferSync(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride) {
	if (!fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "ReadbackDepthbufferSync: bad fbo");
		return;
	}
	// Old desktop GL can download depth, but not upload.
	if (gl_extensions.IsGLES && !SupportsDepthTexturing()) {
		return;
	}

	// Pixel size always 4 here because we always request float or RGBA.
	const u32 bufSize = w * h * 4;
	if (!convBuf_ || convBufSize_ < bufSize) {
		delete[] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	const bool useColorPath = gl_extensions.IsGLES;
	bool format16Bit = false;

	GLRenderManager *render = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	if (useColorPath) {
		if (!depthDownloadProgram_) {
			std::string errorString;
			static std::string vs_code, fs_code;
			vs_code = ApplyGLSLPrelude(depth_vs, GL_VERTEX_SHADER);
			fs_code = ApplyGLSLPrelude(depth_dl_fs, GL_FRAGMENT_SHADER);
			std::vector<GLRShader *> shaders;
			shaders.push_back(render->CreateShader(GL_VERTEX_SHADER, vs_code, "depth_dl"));
			shaders.push_back(render->CreateShader(GL_FRAGMENT_SHADER, fs_code, "depth_dl"));
			std::vector<GLRProgram::Semantic> semantics;
			semantics.push_back({ 0, "a_position" });
			semantics.push_back({ 1, "a_texcoord0" });
			std::vector<GLRProgram::UniformLocQuery> queries;
			queries.push_back({ &u_depthDownloadTex, "tex" });
			queries.push_back({ &u_depthDownloadFactor, "u_depthFactor" });
			queries.push_back({ &u_depthDownloadShift, "u_depthShift" });
			queries.push_back({ &u_depthDownloadTo8, "u_depthTo8" });
			std::vector<GLRProgram::Initializer> inits;
			inits.push_back({ &u_depthDownloadTex, 0, TEX_SLOT_PSP_TEXTURE });
			GLRProgramFlags flags{};
			depthDownloadProgram_ = render->CreateProgram(shaders, semantics, queries, inits, flags);
			for (auto iter : shaders) {
				render->DeleteShader(iter);
			}
			if (!depthDownloadProgram_) {
				ERROR_LOG_REPORT(G3D, "Failed to compile depthDownloadProgram! This shouldn't happen.\n%s", errorString.c_str());
			}
		}

		shaderManager_->DirtyLastShader();
		auto *blitFBO = GetTempFBO(TempFBO::COPY, fbo->Width(), fbo->Height());
		draw_->BindFramebufferAsRenderTarget(blitFBO, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "ReadbackDepthbufferSync");
		render->SetViewport({ 0, 0, (float)fbo->Width(), (float)fbo->Height(), 0.0f, 1.0f });

		// We must bind the program after starting the render pass, and set the color mask after clearing.
		render->SetScissor({ 0, 0, fbo->Width(), fbo->Height() });
		render->SetDepth(false, false, GL_ALWAYS);
		render->SetRaster(false, GL_CCW, GL_FRONT, GL_FALSE, GL_FALSE);
		render->BindProgram(depthDownloadProgram_);

		if (!gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH)) {
			float factors[] = { 0.0f, 1.0f };
			render->SetUniformF(&u_depthDownloadFactor, 2, factors);
		} else {
			const float factor = DepthSliceFactor();
			float factors[] = { -0.5f * (factor - 1.0f) * (1.0f / factor), factor };
			render->SetUniformF(&u_depthDownloadFactor, 2, factors);
		}
		float shifts[] = { 16777215.0f, 16777215.0f / 256.0f, 16777215.0f / 65536.0f, 16777215.0f / 16777216.0f };
		render->SetUniformF(&u_depthDownloadShift, 4, shifts);
		float to8[] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
		render->SetUniformF(&u_depthDownloadTo8, 4, to8);

		draw_->BindFramebufferAsTexture(fbo, TEX_SLOT_PSP_TEXTURE, Draw::FB_DEPTH_BIT, 0);
		float u1 = 1.0f;
		float v1 = 1.0f;
		DrawActiveTexture(x, y, w, h, fbo->Width(), fbo->Height(), 0.0f, 0.0f, u1, v1, ROTATION_LOCKED_HORIZONTAL, DRAWTEX_NEAREST);

		draw_->CopyFramebufferToMemorySync(blitFBO, Draw::FB_COLOR_BIT, x, y, w, h, Draw::DataFormat::R8G8B8A8_UNORM, convBuf_, w, "ReadbackDepthbufferSync");

		textureCache_->ForgetLastTexture();
		// TODO: Use 4444 so we can copy lines directly (instead of 32 -> 16 on CPU)?
		format16Bit = true;
	} else {
		draw_->CopyFramebufferToMemorySync(fbo, Draw::FB_DEPTH_BIT, x, y, w, h, Draw::DataFormat::D32F, convBuf_, w, "ReadbackDepthbufferSync");
		format16Bit = false;
	}

	if (format16Bit) {
		// In this case, we used the shader to apply depth scale factors.
		uint16_t *dest = pixels;
		const u32_le *packed32 = (u32_le *)convBuf_;
		for (int yp = 0; yp < h; ++yp) {
			for (int xp = 0; xp < w; ++xp) {
				dest[xp] = packed32[xp] & 0xFFFF;
			}
			dest += pixelsStride;
			packed32 += w;
		}
	} else {
		// TODO: Apply this in the shader?  May have precision issues if it becomes important to match.
		// We downloaded float values directly in this case.
		uint16_t *dest = pixels;
		const GLfloat *packedf = (GLfloat *)convBuf_;
		DepthScaleFactors depthScale = GetDepthScaleFactors();
		for (int yp = 0; yp < h; ++yp) {
			for (int xp = 0; xp < w; ++xp) {
				float scaled = depthScale.Apply(packedf[xp]);
				if (scaled <= 0.0f) {
					dest[xp] = 0;
				} else if (scaled >= 65535.0f) {
					dest[xp] = 65535;
				} else {
					dest[xp] = (int)scaled;
				}
			}
			dest += pixelsStride;
			packedf += w;
		}
	}
}
