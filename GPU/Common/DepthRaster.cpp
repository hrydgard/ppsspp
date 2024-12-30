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

constexpr int stepXSize = 4;
constexpr int stepYSize = 1;

enum class TriangleResult {
	OK,
	NoPixels,
	SmallOrBackface,
};

constexpr int MIN_TWICE_TRI_AREA = 10;

// Adapted from Intel's depth rasterizer example.
// Started with the scalar version, will SIMD-ify later.
// x1/y1 etc are the scissor rect.
template<ZCompareMode compareMode>
TriangleResult DepthRasterTriangle(uint16_t *depthBuf, int stride, DepthScissor scissor, const int *tx, const int *ty, const float *tz) {
	// BEGIN triangle setup. This should be done SIMD, four triangles at a time.
	// 16x16->32 multiplications are doable on SSE2, which should be all we need.

	// We use 4x1 SIMD tiles for simplicity. 2x2 would be ideal but stores/loads get annoying.

	// NOTE: Triangles are stored in groups of 4.
	float x0 = tx[0];
	float y0 = ty[0];
	float x1 = tx[4];
	float y1 = ty[4];
	float x2 = tx[8];
	float y2 = ty[8];

	// Load the entire scissor rect into one SIMD register.
	// Vec4F32 scissor = Vec4F32::LoadConvertS16(&scissor.x1);

	int minX = (int)std::max(std::min(std::min(x0, x1), x2), (float)scissor.x1) & ~3;
	int maxX = (int)std::min(std::max(std::max(x0, x1), x2) + 3, (float)scissor.x2) & ~3;
	int minY = (int)std::max(std::min(std::min(y0, y1), y2), (float)scissor.y1);
	int maxY = (int)std::min(std::max(std::max(y0, y1), y2), (float)scissor.y2);

	// TODO: Cull really small triangles here - we can increase the threshold a bit probably.
	int triArea = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);

	float oneOverTriArea = 1.0f / (float)triArea;

	// Edge setup
	int A12 = y1 - y2;
	int B12 = x2 - x1;
	int C12 = x1 * y2 - y1 * x2;

	// Edge setup
	int A20 = y2 - y0;
	int B20 = x0 - x2;
	int C20 = x2 * y0 - y2 * x0;

	// Edge setup
	int A01 = y0 - y1;
	int B01 = x1 - x0;
	int C01 = x0 * y1 - y0 * x1;

	// Step deltas
	int stepX12 = A12 * stepXSize;
	int stepY12 = B12 * stepYSize;
	int stepX20 = A20 * stepXSize;
	int stepY20 = B20 * stepYSize;
	int stepX01 = A01 * stepXSize;
	int stepY01 = B01 * stepYSize;

	// Prepare to interpolate Z
	float zbase = tz[0];
	float z_20 = (tz[4] - tz[0]) * oneOverTriArea;
	float z_01 = (tz[8] - tz[0]) * oneOverTriArea;
	float zdx = z_20 * (float)stepX20 + z_01 * (float)stepX01;
	float zdy = z_20 * (float)stepY20 + z_01 * (float)stepY01;

	// Edge function values at origin
	// TODO: We could SIMD the second part here.
	for (int t = 0; t < 1; t++) {
		// Check for bad triangle.
		if (triArea /*[t]*/ <= 0) {
			continue;
		}

		if (maxX == minX || maxY == minY) {
			// No pixels, or outside screen.
			// Most of these are now gone in the initial pass.
			return TriangleResult::NoPixels;
		}

		if (triArea < MIN_TWICE_TRI_AREA) {
			return TriangleResult::SmallOrBackface;  // Or zero area.
		}

		// Convert per-triangle values to wide registers.
		Vec4S32 initialX = Vec4S32::Splat(minX) + Vec4S32::LoadAligned(zero123);
		int initialY = minY;

		Vec4S32 w0_row = Vec4S32::Splat(A12) * initialX + Vec4S32::Splat(B12 * initialY + C12);
		Vec4S32 w1_row = Vec4S32::Splat(A20) * initialX + Vec4S32::Splat(B20 * initialY + C20);
		Vec4S32 w2_row = Vec4S32::Splat(A01) * initialX + Vec4S32::Splat(B01 * initialY + C01);

		Vec4F32 zrow = Vec4F32::Splat(zbase) + Vec4F32FromS32(w1_row) * z_20 + Vec4F32FromS32(w2_row) * z_01;
		Vec4F32 zdeltaX = Vec4F32::Splat(zdx);
		Vec4F32 zdeltaY = Vec4F32::Splat(zdy);

		Vec4S32 oneStepX12 = Vec4S32::Splat(stepX12);
		Vec4S32 oneStepY12 = Vec4S32::Splat(stepY12);
		Vec4S32 oneStepX20 = Vec4S32::Splat(stepX20);
		Vec4S32 oneStepY20 = Vec4S32::Splat(stepY20);
		Vec4S32 oneStepX01 = Vec4S32::Splat(stepX01);
		Vec4S32 oneStepY01 = Vec4S32::Splat(stepY01);
		// Rasterize
		for (int y = minY; y <= maxY; y += stepYSize, w0_row += oneStepY12, w1_row += oneStepY20, w2_row += oneStepY01, zrow += zdeltaY) {
			// Barycentric coordinates at start of row
			Vec4S32 w0 = w0_row;
			Vec4S32 w1 = w1_row;
			Vec4S32 w2 = w2_row;
			Vec4F32 zs = zrow;

			uint16_t *rowPtr = depthBuf + stride * y;

			for (int x = minX; x <= maxX; x += stepXSize, w0 += oneStepX12, w1 += oneStepX20, w2 += oneStepX01, zs += zdeltaX) {
				// If p is on or inside all edges for any pixels,
				// render those pixels.
				Vec4S32 signCalc = w0 | w1 | w2;
				if (!AnyZeroSignBit(signCalc)) {
					continue;
				}

				Vec4U16 bufferValues = Vec4U16::Load(rowPtr + x);
				Vec4U16 shortMaskInv = SignBits32ToMaskU16(signCalc);
				// Now, the mask has 1111111 where we should preserve the contents of the depth buffer.

				Vec4U16 shortZ = Vec4U16::FromVec4F32(zs);

				// This switch is on a templated constant, so should collapse away.
				switch (compareMode) {
				case ZCompareMode::Greater:
					// To implement the greater/greater-than comparison, we can combine mask and max.
					// Unfortunately there's no unsigned max on SSE2, it's synthesized by xoring 0x8000 on input and output.
					// We use AndNot to zero out Z results, before doing Max with the buffer.
					AndNot(shortZ, shortMaskInv).Max(bufferValues).Store(rowPtr + x);
					break;
				case ZCompareMode::Less:  // UNTESTED
					// This time, we OR the mask and use .Min.
					(shortZ | shortMaskInv).Min(bufferValues).Store(rowPtr + x);
					break;
				case ZCompareMode::Always:  // UNTESTED
					// This could be replaced with a vblend operation.
					((bufferValues & shortMaskInv) | AndNot(shortZ, shortMaskInv)).Store(rowPtr + x);
					break;
				}
			}
		}
	}
	return TriangleResult::OK;
}

template<ZCompareMode compareMode>
inline void DepthRaster4Triangles(int stats[4], uint16_t *depthBuf, int stride, DepthScissor scissor, const int *tx, const int *ty, const float *tz) {
	for (int i = 0; i < 4; i++) {
		TriangleResult result = DepthRasterTriangle<compareMode>(depthBuf, stride, scissor, tx + i, ty + i, tz + i);
		stats[(int)result]++;
	}
}

// This will always run on the main thread. Though, might consider moving the transforms out and just storing verts instead?
void DecodeAndTransformForDepthRaster(float *dest, const float *worldviewproj, const void *vertexData, int indexLowerBound, int indexUpperBound, VertexDecoder *dec, u32 vertTypeID) {
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

void TransformPredecodedForDepthRaster(float *dest, const float *worldviewproj, const void *decodedVertexData, VertexDecoder *dec, int count) {
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

void ConvertPredecodedThroughForDepthRaster(float *dest, const void *decodedVertexData, VertexDecoder *dec, int count) {
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

	static const float zerovec[4] = {};

	int collected = 0;
	int planeCulled = 0;
	int boxCulled = 0;
	const float *verts[12];  // four triangles at a time!
	const int count = draw.vertexCount;

	Vec4F32 scissorX1 = Vec4F32::Splat((float)scissor.x1);
	Vec4F32 scissorY1 = Vec4F32::Splat((float)scissor.y1);
	Vec4F32 scissorX2 = Vec4F32::Splat((float)scissor.x2);
	Vec4F32 scissorY2 = Vec4F32::Splat((float)scissor.y2);

	for (int i = 0; i < count; i += 3) {
		// Collect valid triangles into buffer.
		const float *v0 = transformed + indexBuffer[i] * 4;
		const float *v1 = transformed + indexBuffer[i + (1 ^ flipCull)] * 4;
		const float *v2 = transformed + indexBuffer[i + (2 ^ flipCull)] * 4;
		// Don't collect triangle if any vertex is behind the 0 plane.
		if (v0[3] > 0.0f && v1[3] > 0.0f && v2[3] > 0.0f) {
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
		z0 = (z0 * recipW0).Clamp(0.0f, 65535.0f);
		x1 *= recipW1;
		y1 *= recipW1;
		z1 = (z1 * recipW1).Clamp(0.0f, 65535.0f);
		x2 *= recipW2;
		y2 *= recipW2;
		z2 = (z2 * recipW2).Clamp(0.0f, 65535.0f);

		// Check bounding box size (clamped to screen edges). Cast to integer for crude rounding (and to match the rasterizer).
		Vec4S32 minX = Vec4S32FromF32(x0.Min(x1.Min(x2)).Max(scissorX1));
		Vec4S32 minY = Vec4S32FromF32(y0.Min(y1.Min(y2)).Max(scissorY1));
		Vec4S32 maxX = Vec4S32FromF32(x0.Max(x1.Max(x2)).Min(scissorX2));
		Vec4S32 maxY = Vec4S32FromF32(y0.Max(y1.Max(y2)).Min(scissorY2));

		// If all are equal in any dimension, all four triangles are tiny nonsense (or outside the scissor) and can be skipped early.
		Vec4S32 eqMask = minX.CompareEq(maxX) | minY.CompareEq(maxY);
		// Otherwise we just proceed to triangle setup with all four for now. Later might want to
		// compact the remaining triangles... Or do more checking here.
		// We could also save the computed boxes for later..
		if (!AnyZeroSignBit(eqMask)) {
			boxCulled += 4;
			continue;
		}

		// Floating point double triangle area. Can't be reused for the integer-snapped raster reliably (though may work...)
		// Still good for culling early and pretty cheap to compute.
		Vec4F32 doubleTriArea = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) - Vec4F32::Splat((float)MIN_TWICE_TRI_AREA);
		if (!AnyZeroSignBit(doubleTriArea)) {
			gpuStats.numDepthRasterEarlySize += 4;
			continue;
		}

		Vec4S32FromF32(x0).Store(tx + outCount);
		Vec4S32FromF32(x1).Store(tx + outCount + 4);
		Vec4S32FromF32(x2).Store(tx + outCount + 8);
		Vec4S32FromF32(y0).Store(ty + outCount);
		Vec4S32FromF32(y1).Store(ty + outCount + 4);
		Vec4S32FromF32(y2).Store(ty + outCount + 8);
		z0.Store(tz + outCount);
		z1.Store(tz + outCount + 4);
		z2.Store(tz + outCount + 8);

		outCount += 12;

		if (!cullEnabled) {
			// If culling is off, store the triangles again, in the opposite order.
			Vec4S32FromF32(x0).Store(tx + outCount);
			Vec4S32FromF32(x2).Store(tx + outCount + 4);
			Vec4S32FromF32(x1).Store(tx + outCount + 8);
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
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, const int *tx, const int *ty, const float *tz, int count, const DepthDraw &draw, const DepthScissor scissor) {
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
		for (int i = 0; i < count; i += 12) {
			switch (draw.compareMode) {
			case ZCompareMode::Greater:
			{
				DepthRaster4Triangles<ZCompareMode::Greater>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			case ZCompareMode::Less:
			{
				DepthRaster4Triangles<ZCompareMode::Less>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			case ZCompareMode::Always:
			{
				DepthRaster4Triangles<ZCompareMode::Always>(stats, depth, depthStride, scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			}
		}
		gpuStats.numDepthRasterNoPixels += stats[(int)TriangleResult::NoPixels];
		gpuStats.numDepthRasterTooSmall += stats[(int)TriangleResult::SmallOrBackface];
		gpuStats.numDepthRasterPrims += stats[(int)TriangleResult::OK];
		break;
	}
	default:
		_dbg_assert_(false);
	}
}
