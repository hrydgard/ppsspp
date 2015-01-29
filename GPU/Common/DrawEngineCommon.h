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

#include "GPU/Common/GPUDebugInterface.h"

class VertexDecoder;

enum {
	VERTEX_BUFFER_MAX = 65536,
	DECODED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 48,
	DECODED_INDEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * 20,
	SPLINE_BUFFER_SIZE = VERTEX_BUFFER_MAX * 20,
};

class DrawEngineCommon {
public:
	virtual ~DrawEngineCommon();

	bool TestBoundingBox(void* control_points, int vertexCount, u32 vertType);

	// TODO: This can be shared once the decoder cache / etc. are.
	virtual u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) = 0;

	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	static u32 NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType);

protected:
	// Vertex collector buffers
	u8 *decoded;
	u16 *decIndex;
	u8 *splineBuffer;
};