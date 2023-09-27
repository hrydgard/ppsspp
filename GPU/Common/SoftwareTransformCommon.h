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

#include "Common/CommonTypes.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GPU/Common/VertexDecoderCommon.h"

class FramebufferManagerCommon;
class TextureCacheCommon;

enum SoftwareTransformAction {
	SW_NOT_READY,
	SW_DRAW_PRIMITIVES,
	SW_CLEAR,
};

struct SoftwareTransformResult {
	SoftwareTransformAction action;
	u32 color;
	float depth;

	bool setStencil;
	u8 stencilValue;

	bool setSafeSize;
	u32 safeWidth;
	u32 safeHeight;

	TransformedVertex *drawBuffer;
	int drawNumTrans;
	bool drawIndexed;
};

struct SoftwareTransformParams {
	u8 *decoded;
	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;
	FramebufferManagerCommon *fbman;
	TextureCacheCommon *texCache;
	bool allowClear;
	bool allowSeparateAlphaClear;
	bool provokeFlatFirst;
	bool flippedY;
	bool usesHalfZ;
};

class SoftwareTransform {
public:
	SoftwareTransform(SoftwareTransformParams &params) : params_(params) {
	}

	void SetProjMatrix(const float mtx[14], bool invertedX, bool invertedY, const Lin::Vec3 &trans, const Lin::Vec3 &scale);
	void Decode(int prim, u32 vertexType, const DecVtxFormat &decVtxFormat, int maxIndex, SoftwareTransformResult *result);
	void DetectOffsetTexture(int maxIndex);
	void BuildDrawingParams(int prim, int vertexCount, u32 vertType, u16 *&inds, int &maxIndex, SoftwareTransformResult *result);

protected:
	void CalcCullParams(float &minZValue, float &maxZValue);
	void ExpandRectangles(int vertexCount, int &maxIndex, u16 *&inds, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode);
	void ExpandLines(int vertexCount, int &maxIndex, u16 *&inds, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode);
	void ExpandPoints(int vertexCount, int &maxIndex, u16 *&inds, const TransformedVertex *transformed, TransformedVertex *transformedExpanded, int &numTrans, bool throughmode);

	const SoftwareTransformParams &params_;
	Lin::Matrix4x4 projMatrix_;
};
