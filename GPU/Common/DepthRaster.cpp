#include <algorithm>
#include <cstring>
#include <cstdint>

#include "Common/Math/CrossSIMD.h"
#include "GPU/Common/DepthRaster.h"
#include "GPU/Math3D.h"
#include "Common/Math/math_util.h"
#include "GPU/Common/VertexDecoderCommon.h"

DepthScissor DepthScissor::Tile(int tile, int numTiles) const {
	if (numTiles == 1) {
		return *this;
	}
	// First tiling algorithm: Split into vertical slices.
	int w = x2 - x1;
	int tileW = (w / numTiles) & ~3;  // Round to four pixels.

	// TODO: Should round x1 to four pixels as well! except the first one

	DepthScissor scissor;
	scissor.x1 = x1 + tileW * tile;
	scissor.x2 = (tile == numTiles - 1) ? x2 : (x1 + tileW * (tile + 1));
	scissor.y1 = y1;
	scissor.y2 = y2;
	return scissor;
}

// x1/x2 etc are the scissor rect.
static void DepthRasterRect(uint16_t *dest, int stride, const DepthScissor scissor, int v1x, int v1y, int v2x, int v2y, short depthValue, ZCompareMode compareMode) {
	// Swap coordinates if needed, we don't back-face-cull rects.
	// We also ignore the UV rotation here.
	if (v1x > v2x) {
		std::swap(v1x, v2x);
	}
	if (v1y > v2y) {
		std::swap(v1y, v2y);
	}

	if (v1x < scissor.x1) {
		v1x = scissor.x1;
	}
	if (v2x > scissor.x2) {
		v2x = scissor.x2 + 1;  // PSP scissors are inclusive
	}
	if (v1x >= v2x) {
		return;
	}

	if (v1y < scissor.y1) {
		v1y = scissor.y1;
	}
	if (v2y > scissor.y2) {
		v2y = scissor.y2 + 1;
	}
	if (v1y >= v2y) {
		return;
	}

	Vec8U16 valueX8 = Vec8U16::Splat(depthValue);
	for (int y = v1y; y < v2y; y++) {
		uint16_t *ptr = (uint16_t *)(dest + stride * y + v1x);
		int w = v2x - v1x;
		switch (compareMode) {
		case ZCompareMode::Always:
			if (depthValue == 0) {
				memset(ptr, 0, w * 2);
			} else {
				while (w >= 8) {
					valueX8.Store(ptr);
					ptr += 8;
					w -= 8;
				}
				// Non-simd trailer.
				while (w > 0) {
					*ptr++ = depthValue;
					w--;
				}
			}
			break;
		default:
			// TODO
			break;
		}
	}
}

alignas(16) static const int zero123[4] = {0, 1, 2, 3};

enum class TriangleStat {
	OK,
	NoPixels,
	SmallOrBackface,
};

constexpr int MIN_TWICE_TRI_AREA = 10;

// A mix of ideas from Intel's sample and ryg's rasterizer blog series.
template<ZCompareMode compareMode, bool lowQ>
void DepthRaster4Triangles(int stats[3], uint16_t *depthBuf, int stride, DepthScissor scissor, const int *tx, const int *ty, const float *tz) {
	// Triangle setup. This is done using SIMD, four triangles at a time.
	// 16x16->32 multiplications are doable on SSE2, which should be all we need.

	// We use 4x1 SIMD tiles for simplicity. 2x2 would be ideal but stores/loads get annoying.

	// NOTE: Triangles are stored in groups of 4.
	Vec4S32 x0 = Vec4S32::LoadAligned(tx);
	Vec4S32 y0 = Vec4S32::LoadAligned(ty);
	Vec4S32 x1 = Vec4S32::LoadAligned(tx + 4);
	Vec4S32 y1 = Vec4S32::LoadAligned(ty + 4);
	Vec4S32 x2 = Vec4S32::LoadAligned(tx + 8);
	Vec4S32 y2 = Vec4S32::LoadAligned(ty + 8);

	if (lowQ) {
		y0 &= Vec4S32::Splat(~1);
		y1 &= Vec4S32::Splat(~1);
		y2 &= Vec4S32::Splat(~1);
	}

	// FixupAfterMinMax is just 16->32 sign extension, in case the current platform (like SSE2) just has 16-bit min/max operations.
	Vec4S32 minX = x0.Min16(x1).Min16(x2).Max16(Vec4S32::Splat(scissor.x1)).FixupAfterMinMax();
	Vec4S32 maxX = x0.Max16(x1).Max16(x2).Min16(Vec4S32::Splat(scissor.x2)).FixupAfterMinMax();
	Vec4S32 minY = y0.Min16(y1).Min16(y2).Max16(Vec4S32::Splat(scissor.y1)).FixupAfterMinMax();
	Vec4S32 maxY = y0.Max16(y1).Max16(y2).Min16(Vec4S32::Splat(scissor.y2)).FixupAfterMinMax();

	Vec4S32 triArea = (x1 - x0).Mul16(y2 - y0) - (x2 - x0).Mul16(y1 - y0);

	// Edge setup
	Vec4S32 A12 = y1 - y2;
	Vec4S32 B12 = x2 - x1;
	Vec4S32 C12 = x1.Mul16(y2) - y1.Mul16(x2);

	Vec4S32 A20 = y2 - y0;
	Vec4S32 B20 = x0 - x2;
	Vec4S32 C20 = x2.Mul16(y0) - y2.Mul16(x0);

	Vec4S32 A01 = y0 - y1;
	Vec4S32 B01 = x1 - x0;
	Vec4S32 C01 = x0.Mul16(y1) - y0.Mul16(x1);

	constexpr int stepXSize = 4;
	constexpr int stepYSize = lowQ ? 2 : 1;

	constexpr int stepXShift = 2;
	constexpr int stepYShift = lowQ ? 1 : 0;

	// Step deltas
	Vec4S32 stepX12 = A12.Shl<stepXShift>();
	Vec4S32 stepY12 = B12.Shl<stepYShift>();
	Vec4S32 stepX20 = A20.Shl<stepXShift>();
	Vec4S32 stepY20 = B20.Shl<stepYShift>();
	Vec4S32 stepX01 = A01.Shl<stepXShift>();
	Vec4S32 stepY01 = B01.Shl<stepYShift>();

	// Prepare to interpolate Z
	Vec4F32 oneOverTriArea = Vec4F32FromS32(triArea).Recip();
	Vec4F32 zbase = Vec4F32::LoadAligned(tz);
	Vec4F32 z_20 = (Vec4F32::LoadAligned(tz + 4) - zbase) * oneOverTriArea;
	Vec4F32 z_01 = (Vec4F32::LoadAligned(tz + 8) - zbase) * oneOverTriArea;
	Vec4F32 zdx = z_20 * Vec4F32FromS32(stepX20) + z_01 * Vec4F32FromS32(stepX01);
	Vec4F32 zdy = z_20 * Vec4F32FromS32(stepY20) + z_01 * Vec4F32FromS32(stepY01);

	// Shared setup is done, now loop per-triangle in the group of four.
	for (int t = 0; t < 4; t++) {
		// Check for bad triangle.
		// Using operator[] on the vectors actually seems to result in pretty good code.
		if (maxX[t] <= minX[t] || maxY[t] <= minY[t]) {
			// No pixels, or outside screen.
			// Most of these are now gone in the initial pass, but not all since we cull
			// in 4-groups there.
			stats[(int)TriangleStat::NoPixels]++;
			continue;
		}

		if (triArea[t] < MIN_TWICE_TRI_AREA) {
			stats[(int)TriangleStat::SmallOrBackface]++;  // Or zero area.
			continue;
		}

		const int minXT = minX[t] & ~3;
		const int maxXT = maxX[t] & ~3;

		const int minYT = minY[t];
		const int maxYT = maxY[t];

		// Convert to wide registers.
		Vec4S32 initialX = Vec4S32::Splat(minXT) + Vec4S32::LoadAligned(zero123);
		int initialY = minY[t];
		_dbg_assert_(A12[t] < 32767);
		_dbg_assert_(A12[t] > -32767);
		_dbg_assert_(A20[t] < 32767);
		_dbg_assert_(A20[t] > -32767);
		_dbg_assert_(A01[t] < 32767);
		_dbg_assert_(A01[t] > -32767);

		// TODO: The latter subexpression can be broken out of this loop, but reduces block size flexibility.
		Vec4S32 w0_row = Vec4S32::Splat(A12[t]).Mul16(initialX) + Vec4S32::Splat(B12[t] * initialY + C12[t]);
		Vec4S32 w1_row = Vec4S32::Splat(A20[t]).Mul16(initialX) + Vec4S32::Splat(B20[t] * initialY + C20[t]);
		Vec4S32 w2_row = Vec4S32::Splat(A01[t]).Mul16(initialX) + Vec4S32::Splat(B01[t] * initialY + C01[t]);

		Vec4F32 zrow = Vec4F32::Splat(zbase[t]) + Vec4F32FromS32(w1_row) * z_20[t] + Vec4F32FromS32(w2_row) * z_01[t];
		Vec4F32 zdeltaX = Vec4F32::Splat(zdx[t]);
		Vec4F32 zdeltaY = Vec4F32::Splat(zdy[t]);

		Vec4S32 oneStepX12 = Vec4S32::Splat(stepX12[t]);
		Vec4S32 oneStepY12 = Vec4S32::Splat(stepY12[t]);
		Vec4S32 oneStepX20 = Vec4S32::Splat(stepX20[t]);
		Vec4S32 oneStepY20 = Vec4S32::Splat(stepY20[t]);
		Vec4S32 oneStepX01 = Vec4S32::Splat(stepX01[t]);
		Vec4S32 oneStepY01 = Vec4S32::Splat(stepY01[t]);
		// Rasterize
		for (int y = minYT; y <= maxYT; y += stepYSize, w0_row += oneStepY12, w1_row += oneStepY20, w2_row += oneStepY01, zrow += zdeltaY) {
			// Barycentric coordinates at start of row
			Vec4S32 w0 = w0_row;
			Vec4S32 w1 = w1_row;
			Vec4S32 w2 = w2_row;
			Vec4F32 zs = zrow;

			uint16_t *rowPtr = depthBuf + stride * y;

			for (int x = minXT; x <= maxXT; x += stepXSize, w0 += oneStepX12, w1 += oneStepX20, w2 += oneStepX01, zs += zdeltaX) {
				// If p is on or inside all edges for any pixels,
				// render those pixels.
				Vec4S32 signCalc = w0 | w1 | w2;

				// TODO: Check if this check is profitable. Maybe only for big triangles?
				if (!AnyZeroSignBit(signCalc)) {
					continue;
				}

				Vec4U16 bufferValues = Vec4U16::Load(rowPtr + x);
				Vec4U16 shortMaskInv = SignBits32ToMaskU16(signCalc);
				// Now, the mask has 1111111 where we should preserve the contents of the depth buffer.

				Vec4U16 shortZ = Vec4U16::FromVec4F32(zs);

				// This switch is on a templated constant, so should collapse away.
				Vec4U16 writeVal;
				switch (compareMode) {
				case ZCompareMode::Greater:
					// To implement the greater/greater-than comparison, we can combine mask and max.
					// Unfortunately there's no unsigned max on SSE2, it's synthesized by xoring 0x8000 on input and output.
					// We use AndNot to zero out Z results, before doing Max with the buffer.
					writeVal = shortZ.AndNot(shortMaskInv).Max(bufferValues);
					break;
				case ZCompareMode::Less:
					// This time, we OR the mask and use .Min.
					writeVal = (shortZ | shortMaskInv).Min(bufferValues);
					break;
				case ZCompareMode::Always:  // UNTESTED
					// This could be replaced with a vblend operation.
					writeVal = ((bufferValues & shortMaskInv) | shortZ.AndNot(shortMaskInv));
					break;
				}
				writeVal.Store(rowPtr + x);
				if (lowQ) {
					writeVal.Store(rowPtr + stride + x);
				}
			}
		}

		stats[(int)TriangleStat::OK]++;
	}
}

// This will always run on the main thread. Though, might consider moving the transforms out and just storing verts instead?
void DecodeAndTransformForDepthRaster(float *dest, const float *worldviewproj, const void *vertexData, int indexLowerBound, int indexUpperBound, const VertexDecoder *dec, u32 vertTypeID) {
	// TODO: Ditch skinned and morphed prims for now since we don't have a fast way to skin without running the full decoder.
	_dbg_assert_((vertTypeID & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) == 0);

	int vertexStride = dec->VertexSize();
	int offset = dec->posoff;

	Mat4F32 mat(worldviewproj);

	const u8 *startPtr = (const u8 *)vertexData + indexLowerBound * vertexStride;
	int count = indexUpperBound - indexLowerBound + 1;

	switch (vertTypeID & GE_VTYPE_POS_MASK) {
	case GE_VTYPE_POS_FLOAT:
		for (int i = 0; i < count; i++) {
			const float *data = (const float *)(startPtr + i * vertexStride + offset);
			Vec4F32::Load(data).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	case GE_VTYPE_POS_16BIT:
		for (int i = 0; i < count; i++) {
			const s16 *data = ((const s16 *)((const s8 *)startPtr + i * vertexStride + offset));
			Vec4F32::LoadConvertS16(data).Mul(1.0f / 32768.f).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	case GE_VTYPE_POS_8BIT:
		for (int i = 0; i < count; i++) {
			const s8 *data = (const s8 *)startPtr + i * vertexStride + offset;
			Vec4F32::LoadConvertS8(data).Mul(1.0f / 128.0f).AsVec3ByMatrix44(mat).Store(dest + i * 4);
		}
		break;
	}
}

void TransformPredecodedForDepthRaster(float *dest, const float *worldviewproj, const void *decodedVertexData, const VertexDecoder *dec, int count) {
	// TODO: Ditch skinned and morphed prims for now since we don't have a fast way to skin without running the full decoder.
	_dbg_assert_((dec->VertexType() & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) == 0);

	int vertexStride = dec->GetDecVtxFmt().stride;
	int offset = dec->GetDecVtxFmt().posoff;

	Mat4F32 mat(worldviewproj);

	const u8 *startPtr = (const u8 *)decodedVertexData;
	// Decoded position format is always float3.
	for (int i = 0; i < count; i++) {
		const float *data = (const float *)(startPtr + i * vertexStride + offset);
		Vec4F32::Load(data).AsVec3ByMatrix44(mat).Store(dest + i * 4);
	}
}

void ConvertPredecodedThroughForDepthRaster(float *dest, const void *decodedVertexData, const VertexDecoder *dec, int count) {
	// TODO: Ditch skinned and morphed prims for now since we don't have a fast way to skin without running the full decoder.
	_dbg_assert_((dec->VertexType() & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) == 0);

	int vertexStride = dec->GetDecVtxFmt().stride;
	int offset = dec->GetDecVtxFmt().posoff;

	const u8 *startPtr = (const u8 *)decodedVertexData;
	// Decoded position format is always float3.
	for (int i = 0; i < count; i++) {
		const float *data = (const float *)(startPtr + i * vertexStride + offset);
		// Just pass the position straight through - this is through mode!
		// A W of one makes projection a no-op, without branching.
		Vec4F32::Load(data).WithLane3One().Store(dest + i * 4);
	}
}

int DepthRasterClipIndexedRectangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor) {
	int outCount = 0;
	const int count = draw.vertexCount;
	for (int i = 0; i < count; i += 2) {
		const float *verts[2] = {
			transformed + indexBuffer[i] * 4,
			transformed + indexBuffer[i + 1] * 4,
		};

		// Check if any vertex is behind the 0 plane.
		if (verts[0][3] < 0.0f || verts[1][3] < 0.0f) {
			// Ditch this rectangle.
			continue;
		}

		// These names are wrong .. until we transpose.
		// TODO: Maybe combine two rects here at a time. But hardly relevant for performance.
		Vec4F32 x = Vec4F32::Load(verts[0]);
		Vec4F32 y = Vec4F32::Load(verts[1]);
		Vec4F32 z = Vec4F32::Zero();
		Vec4F32 w = Vec4F32::Zero();
		Vec4F32::Transpose(x, y, z, w);
		// Now the names are accurate! Since we only have two vertices, the third and fourth member of each vector is zero
		// and will not be stored (well it will be stored, but it'll be overwritten by the next vertex).
		Vec4F32 recipW = w.Recip();

		x *= recipW;
		y *= recipW;
		z *= recipW;

		Vec4S32FromF32(x).Store2(tx + outCount);
		Vec4S32FromF32(y).Store2(ty + outCount);
		z.Clamp(0.0f, 65535.0f).Store2(tz + outCount);
		outCount += 2;
	}
	return outCount;
}

int DepthRasterClipIndexedTriangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor) {
	int outCount = 0;

	int flipCull = 0;
	if (draw.cullEnabled && draw.cullMode == GE_CULL_CW) {
		flipCull = 3;
	}
	const bool cullEnabled = draw.cullEnabled;

	static const float zerovec[4] = {0.0f, 0.0f, 0.0f, 1.0f};

	int collected = 0;
	int planeCulled = 0;
	int boxCulled = 0;
	const float *verts[12];  // four triangles at a time!
	const int count = draw.vertexCount;

	// Not exactly the same guardband as on the real PSP, but good enough to prevent 16-bit overflow in raster.
	// This is slightly off-center since we are already in screen space, but whatever.
	Vec4S32 guardBandTopLeft = Vec4S32::Splat(-4096);
	Vec4S32 guardBandBottomRight = Vec4S32::Splat(4096);

	Vec4S32 scissorX1 = Vec4S32::Splat((float)scissor.x1);
	Vec4S32 scissorY1 = Vec4S32::Splat((float)scissor.y1);
	Vec4S32 scissorX2 = Vec4S32::Splat((float)scissor.x2);
	Vec4S32 scissorY2 = Vec4S32::Splat((float)scissor.y2);

	// Add cheap pre-projection pre-checks for bad triangle here. Not much we can do safely other than checking W.
	auto validVert = [](const float *v) -> bool {
		if (v[3] <= 0.0f || v[2] <= 0.0f) {
			return false;
		}
		/*
		if (v[2] >= 65535.0f * v[3]) {
			return false;
		}*/
		return true;
	};

	for (int i = 0; i < count; i += 3) {
		// Collect valid triangles into buffer.
		const float *v0 = transformed + indexBuffer[i] * 4;
		const float *v1 = transformed + indexBuffer[i + (1 ^ flipCull)] * 4;
		const float *v2 = transformed + indexBuffer[i + (2 ^ flipCull)] * 4;
		// Don't collect triangle if any vertex is beyond the planes.
		// TODO: Optimize this somehow.
		if (validVert(v0) && validVert(v1) && validVert(v2)) {
			verts[collected] = v0;
			verts[collected + 1] = v1;
			verts[collected + 2] = v2;
			collected += 3;
		} else {
			planeCulled++;
		}

		if (i >= count - 3 && collected != 12) {
			// Last iteration. Zero out any remaining triangles.
			for (int j = collected; j < 12; j++) {
				verts[j] = zerovec;
			}
			collected = 12;
		}

		if (collected != 12) {
			// Fetch more!
			continue;
		}

		collected = 0;

		// These names are wrong .. until we transpose.
		Vec4F32 x0 = Vec4F32::Load(verts[0]);
		Vec4F32 x1 = Vec4F32::Load(verts[1]);
		Vec4F32 x2 = Vec4F32::Load(verts[2]);
		Vec4F32 y0 = Vec4F32::Load(verts[3]);
		Vec4F32 y1 = Vec4F32::Load(verts[4]);
		Vec4F32 y2 = Vec4F32::Load(verts[5]);
		Vec4F32 z0 = Vec4F32::Load(verts[6]);
		Vec4F32 z1 = Vec4F32::Load(verts[7]);
		Vec4F32 z2 = Vec4F32::Load(verts[8]);
		Vec4F32 w0 = Vec4F32::Load(verts[9]);
		Vec4F32 w1 = Vec4F32::Load(verts[10]);
		Vec4F32 w2 = Vec4F32::Load(verts[11]);

		Vec4F32::Transpose(x0, y0, z0, w0);
		Vec4F32::Transpose(x1, y1, z1, w1);
		Vec4F32::Transpose(x2, y2, z2, w2);

		// Now the names are accurate!

		// Let's project all three vertices, for all four triangles.
		Vec4F32 recipW0 = w0.Recip();
		Vec4F32 recipW1 = w1.Recip();
		Vec4F32 recipW2 = w2.Recip();
		x0 *= recipW0;
		y0 *= recipW0;
		z0 *= recipW0;
		x1 *= recipW1;
		y1 *= recipW1;
		z1 *= recipW1;
		x2 *= recipW2;
		y2 *= recipW2;
		z2 *= recipW2;

		// Check bounding box size. Cast to integer for crude rounding (and to approximately match the rasterizer).
		Vec4S32 minX = Vec4S32FromF32(x0.Min(x1.Min(x2)));
		Vec4S32 minY = Vec4S32FromF32(y0.Min(y1.Min(y2)));
		Vec4S32 maxX = Vec4S32FromF32(x0.Max(x1.Max(x2)));
		Vec4S32 maxY = Vec4S32FromF32(y0.Max(y1.Max(y2)));

		// If all are equal in any dimension, all four triangles are tiny nonsense and can be skipped early.
		Vec4S32 eqMask = minX.CompareEq(maxX) | minY.CompareEq(maxY);

		// Otherwise we just proceed to triangle setup with all four for now.
		// We could also save the computed boxes for later..
		// TODO: Merge into below checks? Though nice with an early out.
		if (!AnyZeroSignBit(eqMask)) {
			boxCulled += 4;
			continue;
		}

		// Create a mask to kill coordinates of triangles that poke outside the guardband (or are just empty).
		Vec4S32 inGuardBand =
			((minX.CompareGt(guardBandTopLeft) & maxX.CompareLt(guardBandBottomRight)) &
				(minY.CompareGt(guardBandTopLeft) & maxY.CompareLt(guardBandBottomRight))).AndNot(eqMask);

		// Create another mask to kill off-screen triangles. Not perfectly accurate.
		inGuardBand &= (maxX.CompareGt(scissorX1) & minX.CompareLt(scissorX2)) & (maxY.CompareGt(scissorY1) & minY.CompareLt(scissorY2));

		// It's enough to smash one coordinate to make future checks (like the tri area check) fail.
		x0 &= inGuardBand;
		x1 &= inGuardBand;
		x2 &= inGuardBand;

		// Floating point double triangle area. Can't be reused for the integer-snapped raster reliably (though may work...)
		// Still good for culling early and pretty cheap to compute.
		Vec4F32 doubleTriArea = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) - Vec4F32::Splat((float)(MIN_TWICE_TRI_AREA));
		if (!AnyZeroSignBit(doubleTriArea)) {
			gpuStats.numDepthRasterEarlySize += 4;
			continue;
		}

		// Note: If any triangle is outside the guardband, (just) its X coords get zeroed, and it'll later get rejected.
		Vec4S32FromF32(x0).Store(tx + outCount);
		Vec4S32FromF32(x1).Store(tx + outCount + 4);
		Vec4S32FromF32(x2).Store(tx + outCount + 8);
		Vec4S32FromF32(y0).Store(ty + outCount);
		Vec4S32FromF32(y1).Store(ty + outCount + 4);
		Vec4S32FromF32(y2).Store(ty + outCount + 8);
		z0.Store(tz + outCount);
		z1.Store(tz + outCount + 4);
		z2.Store(tz + outCount + 8);

#ifdef _DEBUG
		for (int i = 0; i < 12; i++) {
			_dbg_assert_(tx[outCount + i] < 32767);
			_dbg_assert_(tx[outCount + i] >= -32768);
			_dbg_assert_(tx[outCount + i] < 32767);
			_dbg_assert_(tx[outCount + i] >= -32768);
		}
#endif

		outCount += 12;

		if (!cullEnabled) {
			// If culling is off, store the triangles again, with the first two vertices swapped.
			(Vec4S32FromF32(x0) & inGuardBand).Store(tx + outCount);
			(Vec4S32FromF32(x2) & inGuardBand).Store(tx + outCount + 4);
			(Vec4S32FromF32(x1) & inGuardBand).Store(tx + outCount + 8);
			Vec4S32FromF32(y0).Store(ty + outCount);
			Vec4S32FromF32(y2).Store(ty + outCount + 4);
			Vec4S32FromF32(y1).Store(ty + outCount + 8);
			z0.Store(tz + outCount);
			z2.Store(tz + outCount + 4);
			z1.Store(tz + outCount + 8);

			outCount += 12;
		}
	}

	gpuStats.numDepthRasterZCulled += planeCulled;
	gpuStats.numDepthEarlyBoxCulled += boxCulled;
	return outCount;
}

// Rasterizes screen-space vertices.
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, const int *tx, const int *ty, const float *tz, int count, const DepthDraw &draw, const DepthScissor scissor, bool lowQ) {
	// Prim should now be either TRIANGLES or RECTs.
	_dbg_assert_(draw.prim == GE_PRIM_RECTANGLES || draw.prim == GE_PRIM_TRIANGLES);

	switch (draw.prim) {
	case GE_PRIM_RECTANGLES:
		for (int i = 0; i < count; i += 2) {
			uint16_t z = (uint16_t)tz[i + 1];  // depth from second vertex
			// TODO: Should clip coordinates to the scissor rectangle.
			// We remove the subpixel information here.
			DepthRasterRect(depth, depthStride, scissor, tx[i], ty[i], tx[i + 1], ty[i + 1], z, draw.compareMode);
		}
		gpuStats.numDepthRasterPrims += count / 2;
		break;
	case GE_PRIM_TRIANGLES:
	{
		int stats[3]{};
		// Batches of 4 triangles, as output by the clip function.
		if (lowQ) {
			switch (draw.compareMode) {
			case ZCompareMode::Greater:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Greater, true>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			case ZCompareMode::Less:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Less, true>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			case ZCompareMode::Always:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Always, true>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			}
		} else {
			switch (draw.compareMode) {
			case ZCompareMode::Greater:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Greater, false>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			case ZCompareMode::Less:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Less, false>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			case ZCompareMode::Always:
			{
				for (int i = 0; i < count; i += 12) {
					DepthRaster4Triangles<ZCompareMode::Always, false>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				}
				break;
			}
			}
		}
		gpuStats.numDepthRasterNoPixels += stats[(int)TriangleStat::NoPixels];
		gpuStats.numDepthRasterTooSmall += stats[(int)TriangleStat::SmallOrBackface];
		gpuStats.numDepthRasterPrims += stats[(int)TriangleStat::OK];
		break;
	}
	default:
		_dbg_assert_(false);
	}
}
