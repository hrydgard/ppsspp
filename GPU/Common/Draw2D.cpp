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
	{ 0, "tex", SamplerFlags::ARRAY_ON_VULKAN },
};

const UniformDef g_draw2Duniforms[2] = {
	{ "vec2", "texSize", 0 },
	{ "float", "scaleFactor", 1},
};

struct Draw2DUB {
	float texSizeX;
	float texSizeY;
	float scaleFactor;
};

const UniformBufferDesc draw2DUBDesc{ sizeof(Draw2DUB), {
	{ "texSize", -1, 0, UniformType::FLOAT2, 0 },
	{ "scaleFactor", -1, 1, UniformType::FLOAT1, 8 },
} };


Draw2DPipelineInfo GenerateDraw2DCopyColorFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);
	writer.C("  vec4 outColor = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");
	writer.EndFSMain("outColor");

	return Draw2DPipelineInfo{
		"draw2d_copy_color",
		RASTER_COLOR,
		RASTER_COLOR,
	};
}

Draw2DPipelineInfo GenerateDraw2DCopyColorRect2LinFs(ShaderWriter &writer) {
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(g_draw2Duniforms, varyings);
	writer.C("  vec2 tSize = texSize / scaleFactor;\n");
	writer.C("  vec2 pixels = v_texcoord * tSize;\n");
	writer.C("  float u = mod(pixels.x, tSize.x);\n");
	writer.C("  float v = floor(pixels.x / tSize.x);\n");
	writer.C("  vec4 outColor = ").SampleTexture2D("tex", "vec2(u, v) / tSize").C(";\n");
	writer.EndFSMain("outColor");

	return Draw2DPipelineInfo{
		"draw2d_copy_color_rect2lin",
		RASTER_COLOR,
		RASTER_COLOR,
	};
}

Draw2DPipelineInfo GenerateDraw2DCopyDepthFs(ShaderWriter &writer) {
	writer.SetFlags(ShaderWriterFlags::FS_WRITE_DEPTH);
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);
	writer.C("  vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);\n");
	writer.C("  gl_FragDepth = ").SampleTexture2D("tex", "v_texcoord.xy").C(".x;\n");
	writer.EndFSMain("outColor");

	return Draw2DPipelineInfo{
		"draw2d_copy_depth",
		RASTER_DEPTH,
		RASTER_DEPTH,
	};
}

Draw2DPipelineInfo GenerateDraw2D565ToDepthFs(ShaderWriter &writer) {
	writer.SetFlags(ShaderWriterFlags::FS_WRITE_DEPTH);
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);
	writer.C("  vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);\n");
	// Unlike when just copying a depth buffer, here we're generating new depth values so we'll
	// have to apply the scaling.
	DepthScaleFactors factors = GetDepthScaleFactors();
	writer.C("  vec3 rgb = ").SampleTexture2D("tex", "v_texcoord.xy").C(".xyz;\n");
	writer.F("  highp float depthValue = (floor(rgb.x * 31.99) + floor(rgb.y * 63.99) * 32.0 + floor(rgb.z * 31.99) * 2048.0); \n");
	writer.F("  gl_FragDepth = (depthValue / %f) + %f;\n", factors.scale, factors.offset);
	writer.EndFSMain("outColor");

	return Draw2DPipelineInfo{
		"draw2d_565_to_depth",
		RASTER_COLOR,
		RASTER_DEPTH,
	};
}

Draw2DPipelineInfo GenerateDraw2D565ToDepthDeswizzleFs(ShaderWriter &writer) {
	writer.SetFlags(ShaderWriterFlags::FS_WRITE_DEPTH);
	writer.DeclareSamplers(samplers);
	writer.BeginFSMain(g_draw2Duniforms, varyings);
	writer.C("  vec4 outColor = vec4(0.0, 0.0, 0.0, 0.0);\n");
	// Unlike when just copying a depth buffer, here we're generating new depth values so we'll
	// have to apply the scaling.
	DepthScaleFactors factors = GetDepthScaleFactors();
	writer.C("  vec2 tsize = texSize;\n");
	writer.C("  vec2 coord = v_texcoord * tsize;\n");
	writer.F("  float strip = 4.0 * scaleFactor;\n");
	writer.C("  float in_strip = mod(coord.y, strip);\n");
	writer.C("  coord.y = coord.y - in_strip + strip - in_strip;\n");
	writer.C("  coord /= tsize;\n");
	writer.C("  vec3 rgb = ").SampleTexture2D("tex", "coord").C(".xyz;\n");
	writer.F("  highp float depthValue = (floor(rgb.x * 31.99) + floor(rgb.y * 63.99) * 32.0 + floor(rgb.z * 31.99) * 2048.0); \n");
	writer.F("  gl_FragDepth = (depthValue / %f) + %f;\n", factors.scale, factors.offset);
	writer.EndFSMain("outColor");
	
	return Draw2DPipelineInfo{
		"draw2d_565_to_depth_deswizzle",
		RASTER_COLOR,
		RASTER_DEPTH
	};
}

void GenerateDraw2DVS(ShaderWriter &writer) {
	writer.BeginVSMain(inputs, Slice<UniformDef>::empty(), varyings);

	writer.C("  v_texcoord = a_texcoord0;\n");    // yes, this should be right. Should be 2.0 in the far corners.
	writer.C("  gl_Position = vec4(a_position, 0.0, 1.0);\n");

	writer.EndVSMain(varyings);
}

template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

void Draw2D::DeviceLost() {
	DoRelease(draw2DVs_);
	DoRelease(draw2DSamplerLinear_);
	DoRelease(draw2DSamplerNearest_);
	draw_ = nullptr;
}

void Draw2D::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}

void Draw2D::Ensure2DResources() {
	using namespace Draw;

	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	if (!draw2DVs_) {
		char *vsCode = new char[8192];
		ShaderWriterFlags flags = ShaderWriterFlags::NONE;
		if (gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
			// Hm, we're compiling the vertex shader here, probably don't need this...
			flags = ShaderWriterFlags::FS_AUTO_STEREO;
		}
		ShaderWriter writer(vsCode, shaderLanguageDesc, ShaderStage::Vertex);
		GenerateDraw2DVS(writer);
		_assert_msg_(strlen(vsCode) < 8192, "Draw2D VS length error: %d", (int)strlen(vsCode));
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
		descLinear.wrapW = TextureAddressMode::CLAMP_TO_EDGE;
		draw2DSamplerLinear_ = draw_->CreateSamplerState(descLinear);
	}

	if (!draw2DSamplerNearest_) {
		SamplerStateDesc descNearest{};
		descNearest.magFilter = TextureFilter::NEAREST;
		descNearest.minFilter = TextureFilter::NEAREST;
		descNearest.mipFilter = TextureFilter::NEAREST;
		descNearest.wrapU = TextureAddressMode::CLAMP_TO_EDGE;
		descNearest.wrapV = TextureAddressMode::CLAMP_TO_EDGE;
		descNearest.wrapW = TextureAddressMode::CLAMP_TO_EDGE;
		draw2DSamplerNearest_ = draw_->CreateSamplerState(descNearest);
	}
}

Draw2DPipeline *Draw2D::Create2DPipeline(std::function<Draw2DPipelineInfo (ShaderWriter &)> generate) {
	Ensure2DResources();

	using namespace Draw;
	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	char *fsCode = new char[8192];
	ShaderWriterFlags flags = ShaderWriterFlags::NONE;
	if (gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
		flags = ShaderWriterFlags::FS_AUTO_STEREO;
	}
	ShaderWriter writer(fsCode, shaderLanguageDesc, ShaderStage::Fragment, Slice<const char *>::empty(), flags);
	Draw2DPipelineInfo info = generate(writer);
	_assert_msg_(strlen(fsCode) < 8192, "Draw2D FS length error: %d", (int)strlen(fsCode));

	ShaderModule *fs = draw_->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fsCode, strlen(fsCode), info.tag);

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

	BlendState *blend = draw_->CreateBlendState({ false, info.writeChannel == RASTER_COLOR ? 0xF : 0 });

	DepthStencilStateDesc dsDesc{};
	if (info.writeChannel == RASTER_DEPTH) {
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
		blend,
		rasterNoCull,
		&draw2DUBDesc,
		info.samplers.is_empty() ? samplers : info.samplers,
	};

	Draw::Pipeline *pipeline = draw_->CreateGraphicsPipeline(pipelineDesc, info.tag);

	fs->Release();

	rasterNoCull->Release();
	blend->Release();
	depthStencil->Release();
	inputLayout->Release();

	return new Draw2DPipeline {
		pipeline,
		info,
		fsCode,
	};
}

void Draw2D::Blit(Draw2DPipeline *pipeline, float srcX1, float srcY1, float srcX2, float srcY2, float dstX1, float dstY1, float dstX2, float dstY2, float srcWidth, float srcHeight, float dstWidth, float dstHeight, bool linear, int scaleFactor) {
	float dX = 1.0f / (float)dstWidth;
	float dY = 1.0f / (float)dstHeight;
	float sX = 1.0f / (float)srcWidth;
	float sY = 1.0f / (float)srcHeight;
	Draw2DVertex vtx[4] = {
		{ -1.0f + 2.0f * dX * dstX1, -(1.0f - 2.0f * dY * dstY1), sX * srcX1, sY * srcY1 },
		{ -1.0f + 2.0f * dX * dstX2, -(1.0f - 2.0f * dY * dstY1), sX * srcX2, sY * srcY1 },
		{ -1.0f + 2.0f * dX * dstX1, -(1.0f - 2.0f * dY * dstY2), sX * srcX1, sY * srcY2 },
		{ -1.0f + 2.0f * dX * dstX2, -(1.0f - 2.0f * dY * dstY2), sX * srcX2, sY * srcY2 },
	};

	DrawStrip2D(nullptr, vtx, 4, linear, pipeline, srcWidth, srcHeight, scaleFactor);
}

void Draw2D::DrawStrip2D(Draw::Texture *tex, const Draw2DVertex *verts, int vertexCount, bool linearFilter, Draw2DPipeline *pipeline, float texW, float texH, int scaleFactor) {
	using namespace Draw;

	_dbg_assert_(pipeline);

	if (pipeline->info.writeChannel == RASTER_DEPTH) {
		_dbg_assert_(draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported);

		// We don't filter inputs when writing depth, results will be bad.
		linearFilter = false;
	}

	Draw2DUB ub;
	ub.texSizeX = tex ? tex->Width() : texW;
	ub.texSizeY = tex ? tex->Height() : texH;
	ub.scaleFactor = (float)scaleFactor;

	draw_->BindPipeline(pipeline->pipeline);
	draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));

	if (tex) {
		// This won't work since all the shaders above expect array textures on Vulkan.
		draw_->BindTextures(TEX_SLOT_PSP_TEXTURE, 1, &tex);
	}
	draw_->BindSamplerStates(TEX_SLOT_PSP_TEXTURE, 1, linearFilter ? &draw2DSamplerLinear_ : &draw2DSamplerNearest_);
	draw_->DrawUP(verts, vertexCount);

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE | DIRTY_VERTEXSHADER_STATE);
}

Draw2DPipeline *FramebufferManagerCommon::Get2DPipeline(Draw2DShader shader) {
	using namespace Draw;

	const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();

	Draw2DPipeline *pipeline = nullptr;

	switch (shader) {
	case DRAW2D_COPY_COLOR:
		if (!draw2DPipelineColor_) {
			draw2DPipelineColor_ = draw2D_.Create2DPipeline(&GenerateDraw2DCopyColorFs);
		}
		pipeline = draw2DPipelineColor_;
		break;

	case DRAW2D_COPY_COLOR_RECT2LIN:
		if (!draw2DPipelineColorRect2Lin_) {
			draw2DPipelineColorRect2Lin_ = draw2D_.Create2DPipeline(&GenerateDraw2DCopyColorRect2LinFs);
		}
		pipeline = draw2DPipelineColorRect2Lin_;
		break;

	case DRAW2D_COPY_DEPTH:
		if (!draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Can't do it
			return nullptr;
		}
		if (!draw2DPipelineDepth_) {
			draw2DPipelineDepth_ = draw2D_.Create2DPipeline(&GenerateDraw2DCopyDepthFs);
		}
		pipeline = draw2DPipelineDepth_;
		break;

	case DRAW2D_565_TO_DEPTH:
		if (!draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Can't do it
			return nullptr;
		}
		if (!draw2DPipeline565ToDepth_) {
			draw2DPipeline565ToDepth_ = draw2D_.Create2DPipeline(&GenerateDraw2D565ToDepthFs);
		}
		pipeline = draw2DPipeline565ToDepth_;
		break;

	case DRAW2D_565_TO_DEPTH_DESWIZZLE:
		if (!draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Can't do it
			return nullptr;
		}
		if (!draw2DPipeline565ToDepthDeswizzle_) {
			draw2DPipeline565ToDepthDeswizzle_ = draw2D_.Create2DPipeline(&GenerateDraw2D565ToDepthDeswizzleFs);
		}
		pipeline = draw2DPipeline565ToDepthDeswizzle_;
		break;
	}

	return pipeline;
}
