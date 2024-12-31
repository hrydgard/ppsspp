#pragma once

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"

struct DepthScreenVertex {
	int x;
	int y;
	int z;
};

// X11.h, sigh.
#ifdef Always
#undef Always
#endif

// We only need to support these three modes.
enum class ZCompareMode : u8 {
	Greater,  // Most common
	Less,  // Less common
	Always,  // Mostly used for clears
};

struct DepthScissor {
	u16 x1;
	u16 y1;
	u16 x2;
	u16 y2;

	DepthScissor Tile(int tile, int numTiles) const;
};

struct DepthDraw {
	u32 depthAddr;
	u16 depthStride;
	u8 cullMode;
	GEPrimitiveType prim;
	ZCompareMode compareMode;
	bool cullEnabled;
	DepthScissor scissor;
	int vertexOffset;
	int indexOffset;
	int vertexCount;
};

// Specialized, very limited depth-only rasterizer.
// Meant to run in parallel with hardware rendering, in games that read back the depth buffer
// for effects like lens flare.
// So, we can be quite inaccurate without any issues, and skip a lot of functionality.

class VertexDecoder;
struct TransformedVertex;

int DepthRasterClipIndexedTriangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor);
int DepthRasterClipIndexedRectangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor);
void DecodeAndTransformForDepthRaster(float *dest, const float *worldviewproj, const void *vertexData, int indexLowerBound, int indexUpperBound, VertexDecoder *dec, u32 vertTypeID);
void TransformPredecodedForDepthRaster(float *dest, const float *worldviewproj, const void *decodedVertexData, VertexDecoder *dec, int count);
void ConvertPredecodedThroughForDepthRaster(float *dest, const void *decodedVertexData, VertexDecoder *dec, int count);
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, const int *tx, const int *ty, const float *tz, int count, const DepthDraw &draw, const DepthScissor scissor, bool lowQ);
