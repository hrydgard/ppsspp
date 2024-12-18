#pragma once

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"

// Specialized, very limited depth-only rasterizer.
// Meant to run in parallel with hardware rendering, in games that read back the depth buffer
// for effects like lens flare.
// So, we can be quite inaccurate without any issues, and skip a lot of functionality.

class VertexDecoder;
void DepthRasterPrim(uint16_t *dest, int stride, int x1, int x2, int y1, int y2, void *bufferData, const void *vertexData, const void *indexData, GEPrimitiveType prim, int count, VertexDecoder *decoder, u32 vertexTypeID, bool clockwise);
