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

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

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

static bool CheckStencilBits(const u8 *src, const VirtualFramebuffer *dstBuffer, int &values, u8 &usedBits) {
	switch (dstBuffer->fb_format) {
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
	case GE_FORMAT_DEPTH16:
	case GE_FORMAT_CLUT8:
		// Inconceivable.
		_assert_(false);
		return false;
	}

	return true;
}

struct StencilUB {
	float stencilValue;
};

const UniformBufferDesc stencilUBDesc { sizeof(StencilUB), {
	{ "stencilValue", -1, 0, UniformType::FLOAT1, 0 },
} };

// TODO: Merge this with UniformBufferDesc
static const UniformDef uniforms[1] = {
	{ "float", "stencilValue", 0 },
};

static const InputDef inputs[1] = {
	{ "vec2", "a_position", Draw::SEM_POSITION, }
};

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[1] = {
	{ 0, "tex" },
};

void GenerateStencilFs(char *buffer, const ShaderLanguageDesc &lang, const Draw::Bugs &bugs, bool useExport) {
	std::vector<const char *> extensions;
	if (useExport)
		extensions.push_back("#extension GL_ARB_shader_stencil_export : require");

	ShaderWriter writer(buffer, lang, ShaderStage::Fragment, extensions);
	writer.HighPrecisionFloat();
	writer.DeclareSamplers(samplers);

	if (bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI) || bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO)) {
		writer.C("layout (depth_unchanged) out float gl_FragDepth;\n");
	}

	writer.C("float roundAndScaleTo255f(in float x) { return floor(x * 255.99); }\n");

	writer.BeginFSMain(uniforms, varyings);

	writer.C("  vec4 index = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");
	writer.C("  vec4 outColor = index.aaaa;\n");  // Only care about a.
	if (useExport) {
		writer.C("  gl_FragStencilRefARB = int(roundAndScaleTo255f(index.a));\n");
	} else {
		writer.C("  float shifted = roundAndScaleTo255f(index.a) / roundAndScaleTo255f(stencilValue);\n");
		// Bitwise operations on floats, ugh.
		writer.C("  if (mod(floor(shifted), 2.0) < 0.99) DISCARD;\n");
	}

	if (bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI) || bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO)) {
		writer.C("  gl_FragDepth = gl_FragCoord.z;\n");
	}

	writer.EndFSMain("outColor");
}

// This can probably be shared with some other shaders, like reinterpret or the future depth upload.
void GenerateStencilVs(char *buffer, const ShaderLanguageDesc &lang) {
	ShaderWriter writer(buffer, lang, ShaderStage::Vertex);

	writer.BeginVSMain(lang.vertexIndex ? Slice<InputDef>::empty() : inputs, Slice<UniformDef>::empty(), varyings);

	if (lang.vertexIndex) {
		writer.C("  float x = float((gl_VertexIndex & 1) << 1);\n");
		writer.C("  float y = float(gl_VertexIndex & 2);\n");
		writer.C("  v_texcoord = vec2(x, y);\n");
	} else {
		writer.C("  v_texcoord = a_position * 2.0;\n");    // yes, this should be right. Should be 2.0 in the far corners.
	}
	writer.C("  gl_Position = vec4(v_texcoord * 2.0 - vec2(1.0, 1.0), 0.0, 1.0);\n");

	writer.EndVSMain(varyings);
}

bool FramebufferManagerCommon::PerformWriteStencilFromMemory(u32 addr, int size, WriteStencil flags) {
	using namespace Draw;

	addr &= 0x3FFFFFFF;
	if (!MayIntersectFramebufferColor(addr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = nullptr;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		// TODO: Maybe we should broadcast to all?  Most of the time, there's only one.
		if (vfb->fb_address == addr && (!dstBuffer || dstBuffer->colorBindSeq < vfb->colorBindSeq)) {
			dstBuffer = vfb;
		}
	}
	if (!dstBuffer) {
		return false;
	}

	int values = 0;
	u8 usedBits = 0;
	bool useExportShader = draw_->GetDeviceCaps().fragmentShaderStencilWriteSupported;

	const u8 *src = Memory::GetPointer(addr);
	if (!src)
		return false;

	// Could skip this when doing useExportShader, but then we couldn't optimize usedBits == 0.
	if (!CheckStencilBits(src, dstBuffer, values, usedBits))
		return false;

	if (usedBits == 0) {
		if (flags & WriteStencil::STENCIL_IS_ZERO) {
			// Common when creating buffers, it's already 0.
			// We're done.
			return false;
		}

		// Otherwise, we can skip alpha in many cases, in which case we don't even use a shader.
		if (flags & WriteStencil::IGNORE_ALPHA) {
			if (dstBuffer->fbo) {
				draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "WriteStencilFromMemory_Clear");
			}
			return true;
		}
	}

	shaderManager_->DirtyLastShader();
	textureCache_->ForgetLastTexture();

	if (!stencilWritePipeline_) {
		const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

		char *fsCode = new char[8192];
		char *vsCode = new char[8192];
		GenerateStencilFs(fsCode, shaderLanguageDesc, draw_->GetBugs(), useExportShader);
		GenerateStencilVs(vsCode, shaderLanguageDesc);

		_assert_msg_(strlen(fsCode) < 8192, "StenFS length error: %d", (int)strlen(fsCode));
		_assert_msg_(strlen(vsCode) < 8192, "StenVS length error: %d", (int)strlen(vsCode));

		ShaderModule *stencilUploadFs = draw_->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fsCode, strlen(fsCode), "stencil_fs");
		ShaderModule *stencilUploadVs = draw_->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)vsCode, strlen(vsCode), "stencil_vs");

		_assert_(stencilUploadFs && stencilUploadVs);

		InputLayoutDesc desc = {
			8,
			{
				{ SEM_POSITION, DataFormat::R32G32_FLOAT, 0 },
			},
		};
		InputLayout *inputLayout = draw_->CreateInputLayout(desc);

		BlendState *blendOff = draw_->CreateBlendState({ false, 0x8 });
		DepthStencilStateDesc dsDesc{};
		dsDesc.stencilEnabled = true;
		dsDesc.stencil.compareOp = Comparison::ALWAYS;
		dsDesc.stencil.depthFailOp = StencilOp::REPLACE;
		dsDesc.stencil.failOp = StencilOp::REPLACE;
		dsDesc.stencil.passOp = StencilOp::REPLACE;
		DepthStencilState *stencilWrite = draw_->CreateDepthStencilState(dsDesc);
		RasterState *rasterNoCull = draw_->CreateRasterState({});

		PipelineDesc stencilWriteDesc{
			Primitive::TRIANGLE_LIST,
			{ stencilUploadVs, stencilUploadFs },
			inputLayout, stencilWrite, blendOff, rasterNoCull, &stencilUBDesc,
		};
		stencilWritePipeline_ = draw_->CreateGraphicsPipeline(stencilWriteDesc, "stencil_upload");
		_assert_(stencilWritePipeline_);

		delete[] fsCode;
		delete[] vsCode;

		rasterNoCull->Release();
		blendOff->Release();
		stencilWrite->Release();
		inputLayout->Release();

		stencilUploadFs->Release();
		stencilUploadVs->Release();

		SamplerStateDesc descNearest{};
		stencilWriteSampler_ = draw_->CreateSamplerState(descNearest);
	}

	// Fullscreen triangle coordinates.
	static const float positions[6] = {
		0.0, 0.0,
		1.0, 0.0,
		0.0, 1.0,
	};

	bool useBlit = draw_->GetDeviceCaps().framebufferStencilBlitSupported;

	// Our fragment shader (and discard) is slow.  Since the source is 1x, we can stencil to 1x.
	// Then after we're done, we'll just blit it across and stretch it there. Not worth doing
	// if already at 1x size though, of course.
	if (dstBuffer->width == dstBuffer->renderWidth || !dstBuffer->fbo) {
		useBlit = false;
	}
	// The blit path doesn't set alpha, so we can't use it if that's needed.
	if (!(flags & WriteStencil::IGNORE_ALPHA)) {
		useBlit = false;
	}

	u16 w = useBlit ? dstBuffer->width : dstBuffer->renderWidth;
	u16 h = useBlit ? dstBuffer->height : dstBuffer->renderHeight;

	Draw::Framebuffer *blitFBO = nullptr;
	if (useBlit) {
		blitFBO = GetTempFBO(TempFBO::STENCIL, w, h);
		draw_->BindFramebufferAsRenderTarget(blitFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::CLEAR }, "WriteStencilFromMemory_Blit");
	} else if (dstBuffer->fbo) {
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "WriteStencilFromMemory_NoBlit");
	}

	Draw::Viewport viewport = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
	draw_->SetViewport(viewport);

	// TODO: Switch the format to a single channel format?
	Draw::Texture *tex = MakePixelTexture(src, dstBuffer->fb_format, dstBuffer->fb_stride, dstBuffer->width, dstBuffer->height);
	if (!tex) {
		// Bad!
		return false;
	}

	draw_->BindTextures(TEX_SLOT_PSP_TEXTURE, 1, &tex);
	draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, &stencilWriteSampler_);

	// We must bind the program after starting the render pass, and set the color mask after clearing.
	draw_->SetScissorRect(0, 0, w, h);
	draw_->BindPipeline(stencilWritePipeline_);

	if (useExportShader) {
		// We only need to do one pass if using an export shader.
		StencilUB ub{};
		draw_->SetStencilParams(0xFF, 0xFF, 0xFF);
		draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
		draw_->DrawUP(positions, 3);
	} else {
		for (int i = 1; i < values; i += i) {
			if (!(usedBits & i)) {
				// It's already zero, let's skip it.
				continue;
			}
			StencilUB ub{};
			if (dstBuffer->fb_format == GE_FORMAT_4444) {
				draw_->SetStencilParams(0xFF, (i << 4) | i, 0xFF);
				ub.stencilValue = i * (16.0f / 255.0f);
			} else if (dstBuffer->fb_format == GE_FORMAT_5551) {
				draw_->SetStencilParams(0xFF, 0xFF, 0xFF);
				ub.stencilValue = i * (128.0f / 255.0f);
			} else {
				draw_->SetStencilParams(0xFF, i, 0xFF);
				ub.stencilValue = i * (1.0f / 255.0f);
			}
			draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
			draw_->DrawUP(positions, 3);
		}
	}

	if (useBlit) {
		// Note that scissors don't affect blits on other APIs than OpenGL, so might want to try to get rid of this.
		draw_->SetScissorRect(0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight);
		draw_->BlitFramebuffer(blitFBO, 0, 0, w, h, dstBuffer->fbo, 0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight, Draw::FB_STENCIL_BIT, Draw::FB_BLIT_NEAREST, "WriteStencilFromMemory_Blit");
		RebindFramebuffer("RebindFramebuffer - Stencil");
	}

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
	return true;
}
