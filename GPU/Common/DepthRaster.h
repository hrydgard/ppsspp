#pragma once

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"

struct DepthScreenVertex {
	int x;
	int y;
	uint16_t z;
};

// Specialized, very limited depth-only rasterizer.
// Meant to run in parallel with hardware rendering, in games that read back the depth buffer
// for effects like lens flare.
// So, we can be quite inaccurate without any issues, and skip a lot of functionality.

class VertexDecoder;
struct TransformedVertex;

int DepthRasterClipIndexedTriangles(DepthScreenVertex *screenVerts, const float *transformed, const uint16_t *indexBuffer, int count);
void DecodeAndTransformForDepthRaster(float *dest, GEPrimitiveType prim, const float *worldviewproj, const void *vertexData, int count, VertexDecoder *dec, u32 vertTypeID);
void DepthRasterConvertTransformed(DepthScreenVertex *screenVerts, const TransformedVertex *transformed, int count);
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, GEPrimitiveType prim, int x1, int y1, int x2, int y2, const DepthScreenVertex *screenVerts, int count);
