#pragma once

#include "GPU/GPU.h"

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
	DRAW2D_565_TO_DEPTH,
	DRAW2D_565_TO_DEPTH_DESWIZZLE,
};

inline RasterChannel Draw2DSourceChannel(Draw2DShader shader) {
	switch (shader) {
	case DRAW2D_COPY_DEPTH:
		return RASTER_DEPTH;
	case DRAW2D_COPY_COLOR:
	case DRAW2D_565_TO_DEPTH:
	case DRAW2D_565_TO_DEPTH_DESWIZZLE:
	default:
		return RASTER_COLOR;
	}
}

struct Draw2DPipelineInfo {
	RasterChannel readChannel;
	RasterChannel writeChannel;
};

struct Draw2DPipeline {
	Draw::Pipeline *pipeline;
	Draw2DPipelineInfo info;
	void Release() {
		pipeline->Release();
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

	void DrawStrip2D(Draw::Texture *tex, Draw2DVertex *verts, int vertexCount, bool linearFilter, Draw2DPipeline *pipeline, float texW = 0.0f, float texH = 0.0f, int scaleFactor = 0);
	void Ensure2DResources();

private:
	Draw::DrawContext *draw_;

	Draw::SamplerState *draw2DSamplerLinear_ = nullptr;
	Draw::SamplerState *draw2DSamplerNearest_ = nullptr;
	Draw::ShaderModule *draw2DVs_ = nullptr;
};
