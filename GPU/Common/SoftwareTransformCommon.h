#pragma once

#include "Common/CommonTypes.h"

#include "VertexDecoderCommon.h"

class FramebufferManagerCommon;
class TextureCacheCommon;

enum SoftwareTransformAction {
	SW_DRAW_PRIMITIVES,
	SW_CLEAR,
};

struct SoftwareTransformResult {
	SoftwareTransformAction action;
	u32 color;
	float depth;

	bool setStencil;
	u8 stencilValue;
};

void SoftwareTransform(int prim, u8 *decoded, int vertexCount, u32 vertexType, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex, FramebufferManagerCommon *fbman, TextureCacheCommon *texCache, TransformedVertex *transformed, TransformedVertex *transformedExpanded, TransformedVertex *&drawBuffer, 
	int &numTrans, bool &drawIndexed, SoftwareTransformResult *result);
