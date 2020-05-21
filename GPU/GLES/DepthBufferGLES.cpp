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
#include "gfx_es2/gpu_features.h"
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
  enc = enc * u_depthTo8;
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

void FramebufferManagerGLES::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	// Pixel size always 4 here because we always request float
	const u32 bufSize = vfb->z_stride * (h - y) * 4;
	const u32 z_address = vfb->z_address;
	const int packWidth = std::min(vfb->z_stride, std::min(x + w, (int)vfb->width));

	if (!convBuf_ || convBufSize_ < bufSize) {
		delete[] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

	const bool useColorPath = gl_extensions.IsGLES;
	bool format16Bit = false;

	if (useColorPath) {
		if (!depthDownloadProgram_) {
			std::string errorString;
			static std::string vs_code, fs_code;
			vs_code = ApplyGLSLPrelude(depth_vs, GL_VERTEX_SHADER);
			fs_code = ApplyGLSLPrelude(depth_dl_fs, GL_FRAGMENT_SHADER);
			std::vector<GLRShader *> shaders;
			shaders.push_back(render_->CreateShader(GL_VERTEX_SHADER, vs_code, "depth_dl"));
			shaders.push_back(render_->CreateShader(GL_FRAGMENT_SHADER, fs_code, "depth_dl"));
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
			depthDownloadProgram_ = render_->CreateProgram(shaders, semantics, queries, inits, false);
			for (auto iter : shaders) {
				render_->DeleteShader(iter);
			}
			if (!depthDownloadProgram_) {
				ERROR_LOG_REPORT(G3D, "Failed to compile depthDownloadProgram! This shouldn't happen.\n%s", errorString.c_str());
			}
		}

		shaderManagerGL_->DirtyLastShader();
		auto *blitFBO = GetTempFBO(TempFBO::COPY, vfb->renderWidth, vfb->renderHeight, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(blitFBO, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "PackDepthbuffer");
		render_->SetViewport({ 0, 0, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f });
		textureCacheGL_->ForgetLastTexture();

		// We must bind the program after starting the render pass, and set the color mask after clearing.
		render_->SetScissor({ 0, 0, vfb->renderWidth, vfb->renderHeight });
		render_->SetDepth(false, false, GL_ALWAYS);
		render_->SetRaster(false, GL_CCW, GL_FRONT, GL_FALSE);
		render_->BindProgram(depthDownloadProgram_);

		if (!gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH)) {
			float factors[] = { 0.0f, 1.0f };
			render_->SetUniformF(&u_depthDownloadFactor, 2, factors);
		} else {
			const float factor = DepthSliceFactor();
			float factors[] = { -0.5f * (factor - 1.0f) * (1.0f / factor), factor };
			render_->SetUniformF(&u_depthDownloadFactor, 2, factors);
		}
		float shifts[] = { 16777215.0f, 16777215.0f / 256.0f, 16777215.0f / 65536.0f, 16777215.0f / 16777216.0f };
		render_->SetUniformF(&u_depthDownloadShift, 4, shifts);
		float to8[] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
		render_->SetUniformF(&u_depthDownloadTo8, 4, to8);

		draw_->BindFramebufferAsTexture(vfb->fbo, TEX_SLOT_PSP_TEXTURE, Draw::FB_DEPTH_BIT, 0);
		float u1 = 1.0f;
		float v1 = 1.0f;
		DrawActiveTexture(x, y, w, h, vfb->renderWidth, vfb->renderHeight, 0.0f, 0.0f, u1, v1, ROTATION_LOCKED_HORIZONTAL, DRAWTEX_NEAREST);

		draw_->CopyFramebufferToMemorySync(blitFBO, Draw::FB_COLOR_BIT, 0, y, packWidth, h, Draw::DataFormat::R8G8B8A8_UNORM, convBuf_, vfb->z_stride, "PackDepthbuffer");
		// TODO: Use 4444 so we can copy lines directly?
		format16Bit = true;
	} else {
		draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_DEPTH_BIT, 0, y, packWidth, h, Draw::DataFormat::D32F, convBuf_, vfb->z_stride, "PackDepthbuffer");
		format16Bit = false;
	}

	int dstByteOffset = y * vfb->z_stride * sizeof(u16);
	u16 *depth = (u16 *)Memory::GetPointer(z_address + dstByteOffset);
	u32_le *packed32 = (u32_le *)convBuf_;
	GLfloat *packedf = (GLfloat *)convBuf_;

	int totalPixels = h == 1 ? packWidth : vfb->z_stride * h;
	if (format16Bit) {
		for (int yp = 0; yp < h; ++yp) {
			int row_offset = vfb->z_stride * yp;
			for (int xp = 0; xp < packWidth; ++xp) {
				const int i = row_offset + xp;
				depth[i] = packed32[i] & 0xFFFF;
			}
		}
	} else {
		for (int yp = 0; yp < h; ++yp) {
			int row_offset = vfb->z_stride * yp;
			for (int xp = 0; xp < packWidth; ++xp) {
				const int i = row_offset + xp;
				float scaled = FromScaledDepth(packedf[i]);
				if (scaled <= 0.0f) {
					depth[i] = 0;
				} else if (scaled >= 65535.0f) {
					depth[i] = 65535;
				} else {
					depth[i] = (int)scaled;
				}
			}
		}
	}

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
}
