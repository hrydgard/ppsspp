// Copyright (c) 2013- PPSSPP Project.

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

#pragma once

#include <vector>
#include "Common/CommonTypes.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/GPUCommon.h"

class FramebufferManagerCommon;
class TextureCacheCommon;

enum SoftwareTransformAction {
	SW_DRAW_INDEXED,
	SW_CLEAR,
	SW_CULLED,  // don't draw
};

struct TransformStats {
	u16 culledTrianglesNear;
	u16 culledTrianglesFar;
	u16 clippedTriangles;  // Only near plane is clipped against
};

struct SoftwareTransformResult {
	u32 color;
	float depth;

	bool setStencil;
	u8 stencilValue;

	bool setSafeSize;
	u32 safeWidth;
	u32 safeHeight;

	TransformedVertex *drawBuffer;
	int drawVertexCount;
	int drawIndexCount;
	bool pixelMapped;

	TransformStats stats;
};

struct SoftwareTransformParams {
	u8 *decoded;
	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;
	bool allowClear;
	bool allowSeparateAlphaClear;
	bool everUsedEqualDepth;
	float pointScale = 1.0f;  // Useful to increase these for debug views of bounding box corners.
};

// Converts an index buffer to make the provoking vertex the last.
// In-place. So, better not be doing this on GPU memory!
// TODO: We could do this already during index decode.
void IndexBufferProvokingLastToFirst(int prim, u16 *inds, int indsSize);

// indsSize is in indices, not bytes.
// NOTE: In case of clipping, this might write extra vertices after params.transformed.
// NOTE: Does not handle line strips, triangle strips or triangle fans - generate indices for those beforehand.
// NOTE2: The output is ALWAYS an indexed triangle list, no matter the input primitive.
SoftwareTransformAction RunSoftwareTransform(SoftwareTransformParams &params, int prim, u32 vertexType, const DecVtxFormat &decVtxFormat, int &numDecodedVerts, int vertsSize, int vertexCount, u16 *&inds, int indsSize, SoftwareTransformResult *result);

class DrawEngineCommon;

// Slow. See description in the cpp file.
u32 NormalizeVertices(SimpleVertex *sverts, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, const VertexDecoder *dec, u32 vertType);

// In the returned data, you should subtract the value of lowerIndexBound from the indices to get the actual vertex index in the vertices array.
// This is because some draws in some games use very large indices, but they only use a small range of them in each PRIM submission.
// Additionally, if the transformed flag is set in flags, the indices will be transformed into "generic" types (triangles instead of strips), etc.
bool GetCurrentDrawAsDebugVertices(DrawEngineCommon *drawEngine, GECommand cmd, GEPrimitiveType prim, GEPrimitiveType *outPrim, int count, std::vector<GPUDebugVertex> *vertices, std::vector<u16> *indices, int *lowerIndexBound, TransformStats *stats, DebugVertexFlags flags);
