#pragma once

#include "GPU/GPU.h"
#include "Common/GPU/Shader.h"

// For framebuffer copies and similar things that just require passthrough.
struct Draw2DVertex {
	float x;
	float y;
	float u;
	float v;
};

enum Draw2DShader {
	DRAW2D_COPY_COLOR,
	DRAW2D_COPY_DEPTH,
	DRAW2D_ENCODE_R16_TO_DEPTH,
	DRAW2D_565_TO_DEPTH,
	DRAW2D_565_TO_DEPTH_DESWIZZLE,
	DRAW2D_COPY_COLOR_RECT2LIN,
};

inline RasterChannel Draw2DSourceChannel(Draw2DShader shader) {
	switch (shader) {
	case DRAW2D_COPY_DEPTH:
		return RASTER_DEPTH;
	case DRAW2D_COPY_COLOR:
	case DRAW2D_ENCODE_R16_TO_DEPTH:
	case DRAW2D_565_TO_DEPTH:
	case DRAW2D_565_TO_DEPTH_DESWIZZLE:
	default:
		return RASTER_COLOR;
	}
}

struct Draw2DPipelineInfo {
	const char *tag;
	RasterChannel readChannel;
	RasterChannel writeChannel;
	Slice<SamplerDef> samplers;
};

extern const UniformDef g_draw2Duniforms[5];

struct Draw2DPipeline {
	Draw::Pipeline *pipeline;
	Draw2DPipelineInfo info;
	char *code;
	void Release() {
		pipeline->Release();
		delete[] code;
		delete this;
	}
};

class ShaderWriter;

class Draw2D {
public:
	Draw2D(Draw::DrawContext *draw) : draw_(draw) {}
	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	Draw2DPipeline *Create2DPipeline(std::function<Draw2DPipelineInfo(ShaderWriter &)> generate);

	void DrawStrip2D(Draw::Texture *tex, const Draw2DVertex *verts, int vertexCount, bool linearFilter, Draw2DPipeline *pipeline, float texW = 0.0f, float texH = 0.0f, int scaleFactor = 0);

	void Blit(Draw2DPipeline *pipeline, float srcX1, float srcY1, float srcX2, float srcY2, float dstX1, float dstY1, float dstX2, float dstY2, float srcWidth, float srcHeight, float dstWidth, float dstHeight, bool linear, int scaleFactor);
	void Ensure2DResources();

private:
	Draw::DrawContext *draw_;

	Draw::SamplerState *draw2DSamplerLinear_ = nullptr;
	Draw::SamplerState *draw2DSamplerNearest_ = nullptr;
	Draw::ShaderModule *draw2DVs_ = nullptr;
};
