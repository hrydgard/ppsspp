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
#include "Common/GPU/thin3d.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Reporting.h"
#include "GPU/Common/Draw2D.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

static const InputDef inputs[2] = {
	{ "vec2", "a_position", Draw::SEM_POSITION },
	{ "vec2", "a_texcoord", Draw::SEM_TEXCOORD0 },
};

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

void GenerateDraw2DFs(char *buffer, const ShaderLanguageDesc &lang, const Draw::Bugs &bugs) {
	ShaderWriter writer(buffer, lang, ShaderStage::Fragment, nullptr, 0);
	writer.DeclareSampler2D("samp", 0);
	writer.DeclareTexture2D("tex", 0);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);
	writer.C("  vec4 outColor = ").SampleTexture2D("tex", "samp", "v_texcoord.xy").C(";\n");
	writer.EndFSMain("outColor");
}

void GenerateDraw2DVS(char *buffer, const ShaderLanguageDesc &lang) {
	ShaderWriter writer(buffer, lang, ShaderStage::Vertex, nullptr, 0);

	writer.BeginVSMain(inputs, Slice<UniformDef>::empty(), varyings);

	writer.C("  v_texcoord = a_texcoord;\n");    // yes, this should be right. Should be 2.0 in the far corners.
	writer.C("  gl_Position = vec4(a_position, 0.0, 1.0);\n");
	writer.F("  gl_Position.y *= %s1.0;\n", lang.viewportYSign);

	writer.EndVSMain(varyings);
}

// verts have positions in clip coordinates.
void FramebufferManagerCommon::DrawStrip2D(Draw::Texture *tex, Draw2DVertex *verts, int vertexCount) {
	using namespace Draw;

	if (!draw2DPipelineLinear_) {
		const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

		char *fsCode = new char[4000];
		char *vsCode = new char[4000];
		GenerateDraw2DFs(fsCode, shaderLanguageDesc, draw_->GetBugs());
		GenerateDraw2DVS(vsCode, shaderLanguageDesc);

		draw2DFs_ = draw_->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fsCode, strlen(fsCode), "draw2d_fs");
		draw2DVs_ = draw_->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)vsCode, strlen(vsCode), "draw2d_vs");

		_assert_(draw2DFs_ && draw2DVs_);

		InputLayoutDesc desc = {
			{
				{ 16, false },
			},
			{
				{ 0, SEM_POSITION, DataFormat::R32G32_FLOAT, 0 },
				{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 8 },
			},
		};
		InputLayout *inputLayout = draw_->CreateInputLayout(desc);

		BlendState *blendOff = draw_->CreateBlendState({ false, 0xF });
		DepthStencilStateDesc dsDesc{};
		DepthStencilState *noDepthStencil = draw_->CreateDepthStencilState(dsDesc);
		RasterState *rasterNoCull = draw_->CreateRasterState({});

		PipelineDesc stencilWriteDesc{
			Primitive::TRIANGLE_STRIP,
			{ draw2DVs_, draw2DFs_ },
			inputLayout, noDepthStencil, blendOff, rasterNoCull, nullptr,
		};
		draw2DPipelineLinear_ = draw_->CreateGraphicsPipeline(stencilWriteDesc);
		_assert_(draw2DPipelineLinear_);

		delete[] fsCode;
		delete[] vsCode;

		rasterNoCull->Release();
		blendOff->Release();
		noDepthStencil->Release();
		inputLayout->Release();

		SamplerStateDesc descLinear{};
		descLinear.magFilter = TextureFilter::LINEAR;
		descLinear.minFilter = TextureFilter::LINEAR;
		descLinear.mipFilter = TextureFilter::LINEAR;
		descLinear.wrapU = TextureAddressMode::CLAMP_TO_EDGE;
		descLinear.wrapV = TextureAddressMode::CLAMP_TO_EDGE;
		draw2DSamplerLinear_= draw_->CreateSamplerState(descLinear);
	}

	draw_->BindPipeline(draw2DPipelineLinear_);
	if (tex) {
		draw_->BindTextures(TEX_SLOT_PSP_TEXTURE, 1, &tex);
	}
	draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, &draw2DSamplerLinear_);
	draw_->DrawUP(verts, vertexCount);
}
