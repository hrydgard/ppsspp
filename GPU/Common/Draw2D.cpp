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
#include "GPU/Common/GPUStateUtils.h"

static const InputDef inputs[2] = {
	{ "vec2", "a_position", Draw::SEM_POSITION },
	{ "vec2", "a_texcoord0", Draw::SEM_TEXCOORD0 },
};

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[1] = {
	{ "tex" },
};

RasterChannel GenerateDraw2DFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_NONE);
	writer.C("  vec4 outColor = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");
	writer.EndFSMain("outColor", FSFLAG_NONE);

	return RASTER_COLOR;
}

RasterChannel GenerateDraw2DDepthFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_WRITEDEPTH);
	writer.C("  vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);\n");
	writer.C("  gl_FragDepth = ").SampleTexture2D("tex", "v_texcoord.xy").C(".x;\n");
	writer.EndFSMain("outColor", FSFLAG_WRITEDEPTH);

	return RASTER_DEPTH;
}

RasterChannel GenerateDraw2D565ToDepthFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_WRITEDEPTH);
	writer.C("  vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);\n");
	// Unlike when just copying a depth buffer, here we're generating new depth values so we'll
	// have to apply the scaling.
	DepthScaleFactors factors = GetDepthScaleFactors();
	writer.C("  vec3 rgb = ").SampleTexture2D("tex", "v_texcoord.xy").C(".xyz;\n");
	writer.F("  highp float depthValue = (floor(rgb.x * 31.99) + floor(rgb.y * 63.99) * 32.0 + floor(rgb.z * 31.99) * 2048.0) / 65535.0; \n");
	if (factors.scale != 1.0 || factors.offset != 0.0) {
		writer.F("  gl_FragDepth = (depthValue / %f) + %f;\n", factors.scale / 65535.0f, factors.offset);
	} else {
		writer.C("  gl_FragDepth = depthValue;\n");
	}
	writer.EndFSMain("outColor", FSFLAG_WRITEDEPTH);
	return RASTER_DEPTH;
}

void GenerateDraw2DVS(ShaderWriter &writer) {
	writer.BeginVSMain(inputs, Slice<UniformDef>::empty(), varyings);

	writer.C("  v_texcoord = a_texcoord0;\n");    // yes, this should be right. Should be 2.0 in the far corners.
	writer.C("  gl_Position = vec4(a_position, 0.0, 1.0);\n");

	writer.EndVSMain(varyings);
}

void FramebufferManagerCommon::Ensure2DResources() {
	using namespace Draw;

	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	if (!draw2DVs_) {
		char *vsCode = new char[4000];
		ShaderWriter writer(vsCode, shaderLanguageDesc, ShaderStage::Vertex);
		GenerateDraw2DVS(writer);
		draw2DVs_ = draw_->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)vsCode, strlen(vsCode), "draw2d_vs");
		_assert_(draw2DVs_);
		delete[] vsCode;
	}

	if (!draw2DSamplerLinear_) {
		SamplerStateDesc descLinear{};
		descLinear.magFilter = TextureFilter::LINEAR;
		descLinear.minFilter = TextureFilter::LINEAR;
		descLinear.mipFilter = TextureFilter::LINEAR;
		descLinear.wrapU = TextureAddressMode::CLAMP_TO_EDGE;
		descLinear.wrapV = TextureAddressMode::CLAMP_TO_EDGE;
		draw2DSamplerLinear_ = draw_->CreateSamplerState(descLinear);
	}

	if (!draw2DSamplerNearest_) {
		SamplerStateDesc descNearest{};
		descNearest.magFilter = TextureFilter::NEAREST;
		descNearest.minFilter = TextureFilter::NEAREST;
		descNearest.mipFilter = TextureFilter::NEAREST;
		descNearest.wrapU = TextureAddressMode::CLAMP_TO_EDGE;
		descNearest.wrapV = TextureAddressMode::CLAMP_TO_EDGE;
		draw2DSamplerNearest_ = draw_->CreateSamplerState(descNearest);
	}
}

Draw::Pipeline *FramebufferManagerCommon::Create2DPipeline(RasterChannel (*generate)(ShaderWriter &)) {
	using namespace Draw;
	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	char *fsCode = new char[4000];
	ShaderWriter writer(fsCode, shaderLanguageDesc, ShaderStage::Fragment);
	RasterChannel channel = generate(writer);

	ShaderModule *fs = draw_->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fsCode, strlen(fsCode), "draw2d_fs");
	delete[] fsCode;

	_assert_(fs);

	// verts have positions in 2D clip coordinates.
	static const InputLayoutDesc desc = {
		{
			{ 16, false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 8 },
		},
	};
	InputLayout *inputLayout = draw_->CreateInputLayout(desc);

	BlendState *blend = draw_->CreateBlendState({ false, channel == RASTER_COLOR ? 0xF : 0 });

	DepthStencilStateDesc dsDesc{};
	if (channel == RASTER_DEPTH) {
		dsDesc.depthTestEnabled = true;
		dsDesc.depthWriteEnabled = true;
		dsDesc.depthCompare = Draw::Comparison::ALWAYS;
	}

	DepthStencilState *depthStencil = draw_->CreateDepthStencilState(dsDesc);
	RasterState *rasterNoCull = draw_->CreateRasterState({});

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_STRIP,
		{ draw2DVs_, fs },
		inputLayout,
		depthStencil,
		blend, rasterNoCull, nullptr,
	};

	Draw::Pipeline *pipeline = draw_->CreateGraphicsPipeline(pipelineDesc);

	fs->Release();

	rasterNoCull->Release();
	blend->Release();
	depthStencil->Release();
	inputLayout->Release();

	return pipeline;
}

void FramebufferManagerCommon::DrawStrip2D(Draw::Texture *tex, Draw2DVertex *verts, int vertexCount, bool linearFilter, Draw2DShader shader) {
	using namespace Draw;

	Ensure2DResources();

	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	switch (shader) {
	case DRAW2D_COPY_COLOR:
		if (!draw2DPipelineColor_) {
			draw2DPipelineColor_ = Create2DPipeline(&GenerateDraw2DFs);
		}
		draw_->BindPipeline(draw2DPipelineColor_);
		break;

	case DRAW2D_COPY_DEPTH:
		if (!draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Can't do it
			return;
		}
		if (!draw2DPipelineDepth_) {
			draw2DPipelineDepth_ = Create2DPipeline(&GenerateDraw2DDepthFs);
		}
		draw_->BindPipeline(draw2DPipelineDepth_);
		break;

	case DRAW2D_565_TO_DEPTH:
		if (!draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Can't do it
			return;
		}
		if (!draw2DPipeline565ToDepth_) {
			draw2DPipeline565ToDepth_ = Create2DPipeline(&GenerateDraw2D565ToDepthFs);
		}
		draw_->BindPipeline(draw2DPipeline565ToDepth_);
		break;
	}

	if (tex) {
		draw_->BindTextures(TEX_SLOT_PSP_TEXTURE, 1, &tex);
	}
	draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, linearFilter ? &draw2DSamplerLinear_ : &draw2DSamplerNearest_);
	draw_->DrawUP(verts, vertexCount);
}
