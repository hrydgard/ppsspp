#pragma once

#include "TransformUnit.h" // for VertexData

// Contains fast-paths for rectangles. Mainly for use in DarkStalkers which requires software
// rendering to work correctly, but will benefit other games as well.
// It also handles a bypass for DarkStalkers to avoid first stretching the image to 480x272.
// This greatly improves image quality and speeds things up a lot. Not happy about the grossness
// of the hack but it's just a huge waste of CPU and image quality not to do it.

// The long term goal is to get rid of the specializations by jitting, but it still makes
// sense to specifically detect rectangles that do 1:1 texture mapping (like a sprite), because
// the JIT will then be able to eliminate UV interpolation.

class BinManager;
struct BinCoords;

namespace Rasterizer {
	// Returns true if the normal path should be skipped.
	bool RectangleFastPath(const VertexData &v0, const VertexData &v1, BinManager &binner);
	void DrawSprite(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state);

	bool DetectRectangleFromStrip(const RasterizerState &state, const ClipVertexData data[4], int *tlIndex, int *brIndex);
	bool DetectRectangleFromFan(const RasterizerState &state, const ClipVertexData *data, int *tlIndex, int *brIndex);
	bool DetectRectangleFromPair(const RasterizerState &state, const ClipVertexData data[6], int *tlIndex, int *brIndex);
	bool DetectRectangleThroughModeSlices(const RasterizerState &state, const ClipVertexData data[4]);
}
