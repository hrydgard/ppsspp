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
