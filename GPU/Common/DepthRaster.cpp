#include <algorithm>
#include <cstring>
#include <cstdint>

#include "Common/Math/CrossSIMD.h"
#include "GPU/Common/DepthRaster.h"
#include "GPU/Math3D.h"
#include "Common/Math/math_util.h"
#include "GPU/Common/VertexDecoderCommon.h"

void DepthRasterRect(uint16_t *dest, int stride, int x1, int y1, int x2, int y2, short depthValue, GEComparison depthCompare) {
	// Swap coordinates if needed, we don't back-face-cull rects.
	// We also ignore the UV rotation here.
	if (x1 > x2) {
		std::swap(x1, x2);
	}
	if (y1 > y2) {
		std::swap(y1, y2);
	}
	if (x1 == x2 || y1 == y2) {
		return;
	}

#if PPSSPP_ARCH(SSE2)
	__m128i valueX8 = _mm_set1_epi16(depthValue);
	for (int y = y1; y < y2; y++) {
		__m128i *ptr = (__m128i *)(dest + stride * y + x1);
		int w = x2 - x1;
		switch (depthCompare) {
		case GE_COMP_ALWAYS:
			if (depthValue == 0) {
				memset(ptr, 0, w * 2);
			} else {
				while (w >= 8) {
					_mm_storeu_si128(ptr, valueX8);
					ptr++;
					w -= 8;
				}
			}
			break;
			// TODO: Trailer
		case GE_COMP_NEVER:
			break;
		default:
			// TODO
			break;
		}
	}

#elif PPSSPP_ARCH(ARM64_NEON)
	uint16x8_t valueX8 = vdupq_n_u16(depthValue);
	for (int y = y1; y < y2; y++) {
		uint16_t *ptr = (uint16_t *)(dest + stride * y + x1);
		int w = x2 - x1;

		switch (depthCompare) {
		case GE_COMP_ALWAYS:
			if (depthValue == 0) {
				memset(ptr, 0, w * 2);
			} else {
				while (w >= 8) {
					vst1q_u16(ptr, valueX8);
					ptr += 8;
					w -= 8;
				}
			}
			break;
			// TODO: Trailer
		case GE_COMP_NEVER:
			break;
		default:
			// TODO
			break;
		}
	}
#else
	// Do nothing for now
#endif
}

// Adapted from Intel's depth rasterizer example.
// Started with the scalar version, will SIMD-ify later.
// x1/y1 etc are the scissor rect.
void DepthRasterTriangle(uint16_t *depthBuf, int stride, int x1, int y1, int x2, int y2, const int *tx, const int *ty, const int *tz, GEComparison compareMode) {
	int tileStartX = x1;
	int tileEndX = x2;

	int tileStartY = y1;
	int tileEndY = y2;

	// BEGIN triangle setup. This should be done SIMD, four triangles at a time.
	// Due to the many multiplications, we might want to do it in floating point as 32-bit integer muls
	// are slow on SSE2.

	// Convert to whole pixels for now. Later subpixel precision.
	DepthScreenVertex verts[3];
	verts[0].x = tx[0];
	verts[0].y = ty[0];
	verts[0].z = tz[0];
	verts[1].x = tx[2];
	verts[1].y = ty[2];
	verts[1].z = tz[2];
	verts[2].x = tx[1];
	verts[2].y = ty[1];
	verts[2].z = tz[1];

	// use fixed-point only for X and Y.  Avoid work for Z and W.
	int startX = std::max(std::min(std::min(verts[0].x, verts[1].x), verts[2].x), tileStartX);
	int endX = std::min(std::max(std::max(verts[0].x, verts[1].x), verts[2].x) + 1, tileEndX);

	int startY = std::max(std::min(std::min(verts[0].y, verts[1].y), verts[2].y), tileStartY);
	int endY = std::min(std::max(std::max(verts[0].y, verts[1].y), verts[2].y) + 1, tileEndY);
	if (endX == startX || endY == startY) {
		// No pixels, or outside screen.
		return;
	}
	// TODO: Cull really small triangles here.

	// Fab(x, y) =     Ax       +       By     +      C              = 0
	// Fab(x, y) = (ya - yb)x   +   (xb - xa)y + (xa * yb - xb * ya) = 0
	// Compute A = (ya - yb) for the 3 line segments that make up each triangle
	int A0 = verts[1].y - verts[2].y;
	int A1 = verts[2].y - verts[0].y;
	int A2 = verts[0].y - verts[1].y;

	// Compute B = (xb - xa) for the 3 line segments that make up each triangle
	int B0 = verts[2].x - verts[1].x;
	int B1 = verts[0].x - verts[2].x;
	int B2 = verts[1].x - verts[0].x;

	// Compute C = (xa * yb - xb * ya) for the 3 line segments that make up each triangle
	int C0 = verts[1].x * verts[2].y - verts[2].x * verts[1].y;
	int C1 = verts[2].x * verts[0].y - verts[0].x * verts[2].y;
	int C2 = verts[0].x * verts[1].y - verts[1].x * verts[0].y;

	// Compute triangle area.
	// TODO: Cull really small triangles here - we can just raise the comparison value below.
	int triArea = A0 * verts[0].x + B0 * verts[0].y + C0;
	if (triArea <= 0) {
		// Too small to rasterize or backface culled
		// NOTE: Just disabling this check won't enable two-sided rendering.
		// Since it's not that common, let's just queue the triangles with both windings.
		return;
	}

	int rowIdx = (startY * stride + startX);
	int col = startX;
	int row = startY;

	// Calculate slopes at starting corner.
	int alpha0 = (A0 * col) + (B0 * row) + C0;
	int beta0 = (A1 * col) + (B1 * row) + C1;
	int gamma0 = (A2 * col) + (B2 * row) + C2;

	float oneOverTriArea = (1.0f / float(triArea));

	float zz[3];
	zz[0] = (float)verts[0].z;
	zz[1] = (float)(verts[1].z - verts[0].z) * oneOverTriArea;
	zz[2] = (float)(verts[2].z - verts[0].z) * oneOverTriArea;

	// END triangle setup.

	// Incrementally compute Fab(x, y) for all the pixels inside the bounding box formed by (startX, endX) and (startY, endY)
	for (int r = startY; r < endY; r++,
		row++,
		rowIdx += stride,
		alpha0 += B0,
		beta0 += B1,
		gamma0 += B2)
	{
		int idx = rowIdx;

		// Restore row steppers.
		int alpha = alpha0;
		int beta = beta0;
		int gamma = gamma0;

		for (int c = startX; c < endX; c++,
			idx++,
			alpha += A0,
			beta += A1,
			gamma += A2)
		{
			int mask = alpha >= 0 && beta >= 0 && gamma >= 0;
			// Early out if all of this quad's pixels are outside the triangle.
			if (!mask) {
				continue;
			}
			// Compute barycentric-interpolated depth. Could also compute it incrementally.
			float depth = zz[0] + beta * zz[1] + gamma * zz[2];
			float previousDepthValue = (float)depthBuf[idx];

			int depthMask;
			switch (compareMode) {
			case GE_COMP_EQUAL:  depthMask = depth == previousDepthValue; break;
			case GE_COMP_LESS: depthMask = depth < previousDepthValue; break;
			case GE_COMP_LEQUAL: depthMask = depth <= previousDepthValue; break;
			case GE_COMP_GEQUAL: depthMask = depth >= previousDepthValue; break;
			case GE_COMP_GREATER: depthMask = depth > previousDepthValue; break;
			case GE_COMP_NOTEQUAL: depthMask = depth != previousDepthValue; break;
			case GE_COMP_ALWAYS:
			default:
				depthMask = 1;
				break;
			}
			int finalMask = mask & depthMask;
			depth = finalMask == 1 ? depth : previousDepthValue;
			depthBuf[idx] = (u16)depth;
		} //for each column
	} // for each row
}

void DecodeAndTransformForDepthRaster(float *dest, GEPrimitiveType prim, const float *worldviewproj, const void *vertexData, int count, VertexDecoder *dec, u32 vertTypeID) {
	// TODO: Ditch skinned and morphed prims for now since we don't have a fast way to skin without running the full decoder.
	_dbg_assert_((vertTypeID & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) == 0);

	int vertexStride = dec->VertexSize();
	int offset = dec->posoff;

	Mat4F32 mat(worldviewproj);

	switch (vertTypeID & GE_VTYPE_POS_MASK) {
	case GE_VTYPE_POS_FLOAT:
		for (int i = 0; i < count; i++) {
			const float *data = (const float *)((const u8 *)vertexData + vertexStride * i + offset);
			Vec4F32::Load(data).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	case GE_VTYPE_POS_16BIT:
		for (int i = 0; i < count; i++) {
			const s16 *data = ((const s16 *)((const s8 *)vertexData + i * vertexStride + offset));
			Vec4F32::LoadConvertS16(data).Mul(1.0f / 32768.f).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	case GE_VTYPE_POS_8BIT:
		for (int i = 0; i < count; i++) {
			const s8 *data = (const s8 *)vertexData + i * vertexStride + offset;
			Vec4F32::LoadConvertS8(data).Mul(1.0f / 128.0f).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	}
}

int DepthRasterClipIndexedTriangles(int *tx, int *ty, int *tz, const float *transformed, const uint16_t *indexBuffer, int count) {
	bool cullEnabled = gstate.isCullEnabled();

	const float viewportX = gstate.getViewportXCenter();
	const float viewportY = gstate.getViewportYCenter();
	const float viewportZ = gstate.getViewportZCenter();
	const float viewportScaleX = gstate.getViewportXScale();
	const float viewportScaleY = gstate.getViewportYScale();
	const float viewportScaleZ = gstate.getViewportZScale();

	bool cullCCW = false;

	// OK, we now have the coordinates. Let's transform, we can actually do this in-place.

	int outCount = 0;

	for (int i = 0; i < count; i += 3) {
		const float *verts[3] = {
			transformed + indexBuffer[i] * 4,
			transformed + indexBuffer[i + 1] * 4,
			transformed + indexBuffer[i + 2] * 4,
		};

		// Check if any vertex is behind the 0 plane.
		if (verts[0][3] < 0.0f || verts[1][3] < 0.0f || verts[2][3] < 0.0f) {
			// Ditch this triangle. Later we should clip here.
			continue;
		}

		for (int c = 0; c < 3; c++) {
			const float *src = verts[c];
			float invW = 1.0f / src[3];

			float x = src[0] * invW;
			float y = src[1] * invW;
			float z = src[2] * invW;

			float screen[3];
			screen[0] = (x * viewportScaleX + viewportX) * 16.0f - gstate.getOffsetX16();
			screen[1] = (y * viewportScaleY + viewportY) * 16.0f - gstate.getOffsetY16();
			screen[2] = (z * viewportScaleZ + viewportZ);
			if (screen[2] < 0.0f) {
				screen[2] = 0.0f;
			}
			if (screen[2] >= 65535.0f) {
				screen[2] = 65535.0f;
			}
			tx[outCount] = screen[0] * (1.0f / 16.0f);  // We ditch the subpixel precision here.
			ty[outCount] = screen[1] * (1.0f / 16.0f);
			tz[outCount] = screen[2];
			outCount++;
		}
	}
	return outCount;
}

void DepthRasterConvertTransformed(int *tx, int *ty, int *tz, GEPrimitiveType prim, const TransformedVertex *transformed, int count) {
	_dbg_assert_(prim == GE_PRIM_RECTANGLES || prim == GE_PRIM_TRIANGLES);

	// TODO: This is basically a transpose, or AoS->SoA conversion. There may be fast ways.
	for (int i = 0; i < count; i++) {
		tx[i] = (int)transformed[i].pos[0];
		ty[i] = (int)transformed[i].pos[1];
		tz[i] = (u16)transformed[i].pos[2];
	}
}

// Rasterizes screen-space vertices.
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, GEPrimitiveType prim, int x1, int y1, int x2, int y2, const int *tx, const int *ty, const int *tz, int count) {
	// Prim should now be either TRIANGLES or RECTs.
	_dbg_assert_(prim == GE_PRIM_RECTANGLES || prim == GE_PRIM_TRIANGLES);

	GEComparison compareMode = gstate.getDepthTestFunction();
	if (gstate.isModeClear()) {
		if (!gstate.isClearModeDepthMask()) {
			return;
		}
		compareMode = GE_COMP_ALWAYS;
	} else {
		if (!gstate.isDepthTestEnabled() || !gstate.isDepthWriteEnabled())
			return;
	}

	switch (prim) {
	case GE_PRIM_RECTANGLES:
		for (int i = 0; i < count; i += 2) {
			uint16_t z = tz[i + 1];  // depth from second vertex
			// TODO: Should clip coordinates to the scissor rectangle.
			// We remove the subpixel information here.
			DepthRasterRect(depth, depthStride, tx[i], ty[i], tx[i + 1], ty[i + 1], z, compareMode);
		}
		break;
	case GE_PRIM_TRIANGLES:
		for (int i = 0; i < count; i += 3) {
			DepthRasterTriangle(depth, depthStride, x1, y1, x2, y2, &tx[i], &ty[i], &tz[i], compareMode);
		}
		break;
	default:
		_dbg_assert_(false);
	}
}
