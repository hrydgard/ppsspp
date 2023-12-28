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
#include "Common/LogReporting.h"
#include "Core/ConfigValues.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "Common/GPU/ShaderWriter.h"

static const InputDef vs_inputs[] = {
	{ "vec2", "a_position", Draw::SEM_POSITION },
};

struct DepthUB {
	float u_depthFactor[4];
	float u_depthShift[4];
	float u_depthTo8[4];
};

const UniformDef depthUniforms[] = {
	{ "vec4", "u_depthFactor", 0 },
	{ "vec4", "u_depthShift", 1},
	{ "vec4", "u_depthTo8", 2},
};

const UniformBufferDesc depthUBDesc{ sizeof(DepthUB), {
	{ "u_depthFactor", -1, -1, UniformType::FLOAT4, 0 },
	{ "u_depthShift", -1, -1, UniformType::FLOAT4, 16 },
	{ "u_depthTo8", -1, -1, UniformType::FLOAT4, 32 },
} };

static const SamplerDef samplers[] = {
	{ 0, "tex" },
};

static const VaryingDef varyings[] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const char * const stencil_dl_fs = R"(
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
varying vec2 v_texcoord;
lowp uniform usampler2D tex;
void main() {
  uint stencil = texture2D(tex, v_texcoord).r;
  float scaled = float(stencil) / 255.0;
  gl_FragColor = vec4(scaled, scaled, scaled, scaled);
}
)";

static const char * const stencil_vs = R"(
#ifdef GL_ES
precision highp float;
#endif
#if __VERSION__ >= 130
#define attribute in
#define varying out
#endif
attribute vec2 a_position;
varying vec2 v_texcoord;
void main() {
  v_texcoord = a_position * 2.0;
  gl_Position = vec4(v_texcoord * 2.0 - vec2(1.0, 1.0), 0.0, 1.0);
}
)";

Draw::Pipeline *CreateReadbackPipeline(Draw::DrawContext *draw, const char *tag, const UniformBufferDesc *ubDesc, const char *fs, const char *fsTag, const char *vs, const char *vsTag);

// Well, this is not depth, but it's depth/stencil related.
bool FramebufferManagerGLES::ReadbackStencilbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint8_t *pixels, int pixelsStride, Draw::ReadbackMode mode) {
	using namespace Draw;

	if (!fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "ReadbackStencilbufferSync: bad fbo");
		return false;
	}

	const bool useColorPath = gl_extensions.IsGLES;
	if (!useColorPath) {
		return draw_->CopyFramebufferToMemory(fbo, FB_STENCIL_BIT, x, y, w, h, DataFormat::S8, pixels, pixelsStride, ReadbackMode::BLOCK, "ReadbackStencilbufferSync");
	}

	// Unsupported below GLES 3.1 or without ARB_stencil_texturing.
	// OES_texture_stencil8 is related, but used to specify texture data.
	if ((gl_extensions.IsGLES && !gl_extensions.VersionGEThan(3, 1)) && !gl_extensions.ARB_stencil_texturing)
		return false;

	// Pixel size always 4 here because we always request RGBA back.
	const u32 bufSize = w * h * 4;
	if (!convBuf_ || convBufSize_ < bufSize) {
		delete[] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	if (!stencilReadbackPipeline_) {
		stencilReadbackPipeline_ = CreateReadbackPipeline(draw_, "stencil_dl", &depthUBDesc, stencil_dl_fs, "stencil_dl_fs", stencil_vs, "stencil_vs");
		stencilReadbackSampler_ = draw_->CreateSamplerState({});
	}

	shaderManager_->DirtyLastShader();
	auto *blitFBO = GetTempFBO(TempFBO::Z_COPY, fbo->Width(), fbo->Height());
	draw_->BindFramebufferAsRenderTarget(blitFBO, { RPAction::DONT_CARE, RPAction::DONT_CARE, RPAction::DONT_CARE }, "ReadbackStencilbufferSync");
	Draw::Viewport viewport = { 0.0f, 0.0f, (float)fbo->Width(), (float)fbo->Height(), 0.0f, 1.0f };
	draw_->SetViewport(viewport);

	draw_->BindFramebufferAsTexture(fbo, TEX_SLOT_PSP_TEXTURE, FB_STENCIL_BIT, 0);
	draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, &stencilReadbackSampler_);

	// We must bind the program after starting the render pass.
	draw_->SetScissorRect(0, 0, w, h);
	draw_->BindPipeline(stencilReadbackPipeline_);

	// Fullscreen triangle coordinates.
	static const float positions[6] = {
		0.0, 0.0,
		1.0, 0.0,
		0.0, 1.0,
	};
	draw_->DrawUP(positions, 3);

	draw_->CopyFramebufferToMemory(blitFBO, FB_COLOR_BIT, x, y, w, h, DataFormat::R8G8B8A8_UNORM, convBuf_, w, mode, "ReadbackStencilbufferSync");

	textureCache_->ForgetLastTexture();

	// TODO: Use 1/4 width to write all values directly and skip CPU conversion?
	uint8_t *dest = pixels;
	const u32_le *packed32 = (u32_le *)convBuf_;
	for (int yp = 0; yp < h; ++yp) {
		for (int xp = 0; xp < w; ++xp) {
			dest[xp] = packed32[xp] & 0xFF;
		}
		dest += pixelsStride;
		packed32 += w;
	}

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
	return true;
}
