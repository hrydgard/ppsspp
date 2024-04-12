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
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
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

void GenerateDepthDownloadFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(depthUniforms, varyings);
	writer.C("  float depth = ").SampleTexture2D("tex", "v_texcoord").C(".r; \n");
	// At this point, clamped maps [0, 1] to [0, 65535].
	writer.C("  float clamped = clamp((depth - u_depthFactor.x) * u_depthFactor.y, 0.0, 1.0);\n");
	writer.C("  vec4 enc = u_depthShift * clamped;\n");
	writer.C("  enc = floor(mod(enc, 256.0)) * u_depthTo8;\n");
	writer.C("  vec4 outColor = enc.yzww;\n"); // Let's ignore the bits outside 16 bit precision.
	writer.EndFSMain("outColor");
}

void GenerateDepthDownloadVs(ShaderWriter &writer) {
	writer.BeginVSMain(vs_inputs, Slice<UniformDef>::empty(), varyings);
	writer.C("v_texcoord = a_position * 2.0;\n");
	writer.C("gl_Position = vec4(v_texcoord * 2.0 - vec2(1.0, 1.0), 0.0, 1.0);");
	writer.EndVSMain(varyings);
}

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

static bool SupportsDepthTexturing() {
	if (gl_extensions.IsGLES) {
		return gl_extensions.OES_packed_depth_stencil && (gl_extensions.OES_depth_texture || gl_extensions.GLES3);
	}
	return gl_extensions.ARB_texture_float;
}

Draw::Pipeline *CreateReadbackPipeline(Draw::DrawContext *draw, const char *tag, const UniformBufferDesc *ubDesc, const char *fs, const char *fsTag, const char *vs, const char *vsTag) {
	using namespace Draw;

	const ShaderLanguageDesc &shaderLanguageDesc = draw->GetShaderLanguageDesc();

	ShaderModule *readbackFs = draw->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fs, strlen(fs), fsTag);
	ShaderModule *readbackVs = draw->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)vs, strlen(vs), vsTag);
	_assert_(readbackFs && readbackVs);

	static const InputLayoutDesc desc = {
		8,
		{
			{ SEM_POSITION, DataFormat::R32G32_FLOAT, 0 },
		},
	};
	InputLayout *inputLayout = draw->CreateInputLayout(desc);

	BlendState *blendOff = draw->CreateBlendState({ false, 0xF });
	DepthStencilState *stencilIgnore = draw->CreateDepthStencilState({});
	RasterState *rasterNoCull = draw->CreateRasterState({});

	PipelineDesc readbackDesc{
		Primitive::TRIANGLE_LIST,
		{ readbackVs, readbackFs },
		inputLayout, stencilIgnore, blendOff, rasterNoCull, ubDesc,
	};
	Draw::Pipeline *pipeline = draw->CreateGraphicsPipeline(readbackDesc, tag);
	_assert_(pipeline);

	rasterNoCull->Release();
	blendOff->Release();
	stencilIgnore->Release();
	inputLayout->Release();

	readbackFs->Release();
	readbackVs->Release();

	return pipeline;
}

bool FramebufferManagerCommon::ReadbackDepthbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride, int destW, int destH, Draw::ReadbackMode mode) {
	using namespace Draw;

	if (!fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "ReadbackDepthbufferSync: bad fbo");
		return false;
	}
	// Old desktop GL can download depth, but not upload.
	if (gl_extensions.IsGLES && !SupportsDepthTexturing()) {
		return false;
	}

	// Pixel size always 4 here because we always request float or RGBA.
	const u32 bufSize = destW * destH * 4;
	if (!convBuf_ || convBufSize_ < bufSize) {
		delete[] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	float scaleX = (float)destW / w;
	float scaleY = (float)destH / h;

	bool useColorPath = gl_extensions.IsGLES || scaleX != 1.0f || scaleY != 1.0f;
	bool format16Bit = false;

	if (useColorPath) {
		if (!depthReadbackPipeline_) {
			const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();
			char depth_dl_fs[1024];
			char depth_dl_vs[1024];
			ShaderWriter fsWriter(depth_dl_fs, shaderLanguageDesc, ShaderStage::Fragment);
			ShaderWriter vsWriter(depth_dl_vs, shaderLanguageDesc, ShaderStage::Vertex);
			GenerateDepthDownloadFs(fsWriter);
			GenerateDepthDownloadVs(vsWriter);
			depthReadbackPipeline_ = CreateReadbackPipeline(draw_, "depth_dl", &depthUBDesc, depth_dl_fs, "depth_dl_fs", depth_dl_vs, "depth_dl_vs");
			depthReadbackSampler_ = draw_->CreateSamplerState({});
		}

		shaderManager_->DirtyLastShader();
		auto *blitFBO = GetTempFBO(TempFBO::Z_COPY, fbo->Width() * scaleX, fbo->Height() * scaleY);
		draw_->BindFramebufferAsRenderTarget(blitFBO, { RPAction::DONT_CARE, RPAction::DONT_CARE, RPAction::DONT_CARE }, "ReadbackDepthbufferSync");
		Draw::Viewport viewport = { 0.0f, 0.0f, (float)destW, (float)destH, 0.0f, 1.0f };
		draw_->SetViewport(viewport);
		draw_->SetScissorRect(0, 0, fbo->Width() * scaleX, fbo->Height() * scaleY);

		draw_->BindFramebufferAsTexture(fbo, TEX_SLOT_PSP_TEXTURE, FB_DEPTH_BIT, 0);
		draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, &depthReadbackSampler_);

		// We must bind the program after starting the render pass.
		draw_->BindPipeline(depthReadbackPipeline_);

		DepthUB ub{};

		// Setting this to 0.95f eliminates flickering lights with delayed readback in Syphon Filter.
		// That's pretty ugly though! But we'll need to do that if we're gonna enable delayed readback in those games.
		const float fudgeFactor = 1.0f;
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		ub.u_depthFactor[0] = depthScale.Offset();
		ub.u_depthFactor[1] = depthScale.Scale();

		// These are for packing a float in u8x4 colors. We should support more suitable readback formats on APIs that can do it.
		static constexpr float shifts[] = { 16777215.0f, 16777215.0f / 256.0f, 16777215.0f / 65536.0f, 16777215.0f / 16777216.0f };
		memcpy(ub.u_depthShift, shifts, sizeof(shifts));
		static constexpr float to8[] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
		memcpy(ub.u_depthTo8, to8, sizeof(to8));

		draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));

		// Fullscreen triangle coordinates.
		static const float positions[6] = {
			0.0, 0.0,
			1.0, 0.0,
			0.0, 1.0,
		};
		draw_->DrawUP(positions, 3);

		draw_->CopyFramebufferToMemory(blitFBO, FB_COLOR_BIT,
			x * scaleX, y * scaleY, w * scaleX, h * scaleY,
			DataFormat::R8G8B8A8_UNORM, convBuf_, destW, mode, "ReadbackDepthbufferSync");

		textureCache_->ForgetLastTexture();
		// TODO: Use 4444 (or better, R16_UNORM) so we can copy lines directly (instead of 32 -> 16 on CPU)?
		format16Bit = true;
	} else {
		draw_->CopyFramebufferToMemory(fbo, FB_DEPTH_BIT, x, y, w, h, DataFormat::D32F, convBuf_, w, mode, "ReadbackDepthbufferSync");
		format16Bit = false;
	}

	// TODO: Move this conversion into the backends.
	if (format16Bit) {
		// In this case, we used the shader to apply depth scale factors.
		// This can be SSE'd or NEON'd very efficiently, though ideally we would avoid this conversion by using R16_UNORM for readback.
		uint16_t *dest = pixels;
		const u32_le *packed32 = (u32_le *)convBuf_;
		for (int yp = 0; yp < destH; ++yp) {
			for (int xp = 0; xp < destW; ++xp) {
				dest[xp] = packed32[xp] & 0xFFFF;
			}
			dest += pixelsStride;
			packed32 += destW;
		}
	} else {
		// TODO: Apply this in the shader?  May have precision issues if it becomes important to match.
		// We downloaded float values directly in this case.
		uint16_t *dest = pixels;
		const float *packedf = (float *)convBuf_;
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		for (int yp = 0; yp < destH; ++yp) {
			for (int xp = 0; xp < destW; ++xp) {
				float scaled = depthScale.DecodeToU16(packedf[xp]);
				if (scaled <= 0.0f) {
					dest[xp] = 0;
				} else if (scaled >= 65535.0f) {
					dest[xp] = 65535;
				} else {
					dest[xp] = (int)scaled;
				}
			}
			dest += pixelsStride;
			packedf += destW;
		}
	}

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
	return true;
}
