#include <algorithm>
#include <cstring>
#include <cstdint>

#include "Common/Math/CrossSIMD.h"
#include "GPU/Common/DepthRaster.h"
#include "GPU/Math3D.h"
#include "Common/Math/math_util.h"
#include "GPU/Common/VertexDecoderCommon.h"

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

alignas(16) static const int zero123[4]  = {0, 1, 2, 3};

struct Edge {
	// Dimensions of our pixel group
	static const int stepXSize = 4;
	static const int stepYSize = 1;

	Vec4S32 oneStepX;
	Vec4S32 oneStepY;

	Vec4S32 init(int v0x, int v0y, int v1x, int v1y, int p0x, int p0y) {
		// Edge setup
		int A = v0y - v1y;
		int B = v1x - v0x;
		int C = v0x * v1y - v0y * v1x;

		// Step deltas
		oneStepX = Vec4S32::Splat(A * stepXSize);
		oneStepY = Vec4S32::Splat(B * stepYSize);

		// x/y values for initial pixel block. Add horizontal offsets.
		Vec4S32 x = Vec4S32::Splat(p0x) + Vec4S32::LoadAligned(zero123);
		Vec4S32 y = Vec4S32::Splat(p0y);

		// Edge function values at origin
		return Vec4S32::Splat(A) * x + Vec4S32::Splat(B) * y + Vec4S32::Splat(C);
	}
};

enum class TriangleResult {
	OK,
	NoPixels,
	Backface,
	TooSmall,
};

constexpr int MIN_TWICE_TRI_AREA = 10;

// Adapted from Intel's depth rasterizer example.
// Started with the scalar version, will SIMD-ify later.
// x1/y1 etc are the scissor rect.
template<ZCompareMode compareMode>
TriangleResult DepthRasterTriangle(uint16_t *depthBuf, int stride, DepthScissor scissor, const int *tx, const int *ty, const float *tz) {
	// BEGIN triangle setup. This should be done SIMD, four triangles at a time.
	// Due to the many multiplications, we might want to do it in floating point as 32-bit integer muls
	// are slow on SSE2.

	// NOTE: Triangles are stored in groups of 4.
	int v0x = tx[0];
	int v0y = ty[0];
	int v1x = tx[4];
	int v1y = ty[4];
	int v2x = tx[8];
	int v2y = ty[8];

	// use fixed-point only for X and Y.  Avoid work for Z and W.
	// We use 4x1 tiles for simplicity.
	int minX = std::max(std::min(std::min(v0x, v1x), v2x), (int)scissor.x1) & ~3;
	int maxX = std::min(std::max(std::max(v0x, v1x), v2x) + 3, (int)scissor.x2) & ~3;
	int minY = std::max(std::min(std::min(v0y, v1y), v2y), (int)scissor.y1);
	int maxY = std::min(std::max(std::max(v0y, v1y), v2y), (int)scissor.y2);
	if (maxX == minX || maxY == minY) {
		// No pixels, or outside screen.
		return TriangleResult::NoPixels;
	}

	// TODO: Cull really small triangles here - we can increase the threshold a bit probably.
	int triArea = (v1y - v2y) * v0x + (v2x - v1x) * v0y + (v1x * v2y - v2x * v1y);
	if (triArea < 0) {
		return TriangleResult::Backface;
	}
	if (triArea < MIN_TWICE_TRI_AREA) {
		return TriangleResult::TooSmall;  // Or zero area.
	}

	float oneOverTriArea = 1.0f / (float)triArea;

	Edge e01, e12, e20;

	Vec4S32 w0_row = e12.init(v1x, v1y, v2x, v2y, minX, minY);
	Vec4S32 w1_row = e20.init(v2x, v2y, v0x, v0y, minX, minY);
	Vec4S32 w2_row = e01.init(v0x, v0y, v1x, v1y, minX, minY);

	// Prepare to interpolate Z
	Vec4F32 zz0 = Vec4F32::Splat(tz[0]);
	Vec4F32 zz1 = Vec4F32::Splat((tz[4] - tz[0]) * oneOverTriArea);
	Vec4F32 zz2 = Vec4F32::Splat((tz[8] - tz[0]) * oneOverTriArea);

	Vec4F32 zdeltaX = zz1 * Vec4F32FromS32(e20.oneStepX) + zz2 * Vec4F32FromS32(e01.oneStepX);
	Vec4F32 zdeltaY = zz1 * Vec4F32FromS32(e20.oneStepY) + zz2 * Vec4F32FromS32(e01.oneStepY);
	Vec4F32 zrow = zz0 + Vec4F32FromS32(w1_row) * zz1 + Vec4F32FromS32(w2_row) * zz2;

	// Rasterize
	for (int y = minY; y <= maxY; y += Edge::stepYSize, w0_row += e12.oneStepY, w1_row += e20.oneStepY, w2_row += e01.oneStepY, zrow += zdeltaY) {
		// Barycentric coordinates at start of row
		Vec4S32 w0 = w0_row;
		Vec4S32 w1 = w1_row;
		Vec4S32 w2 = w2_row;
		Vec4F32 zs = zrow;

		uint16_t *rowPtr = depthBuf + stride * y;

		for (int x = minX; x <= maxX; x += Edge::stepXSize, w0 += e12.oneStepX, w1 += e20.oneStepX, w2 += e01.oneStepX, zs += zdeltaX) {
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

int DepthRasterClipIndexedRectangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw) {
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

int DepthRasterClipIndexedTriangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw) {
	int outCount = 0;

	int flipCull = 0;
	if (draw.cullEnabled && draw.cullMode == GE_CULL_CW) {
		flipCull = 3;
	}
	const bool cullEnabled = draw.cullEnabled;

	static const float zerovec[4] = {};

	int collected = 0;
	int planeCulled = 0;
	const float *verts[12];  // four triangles at a time!
	const int count = draw.vertexCount;
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
	return outCount;
}

// Rasterizes screen-space vertices.
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, const int *tx, const int *ty, const float *tz, int count, const DepthDraw &draw) {
	// Prim should now be either TRIANGLES or RECTs.
	_dbg_assert_(draw.prim == GE_PRIM_RECTANGLES || draw.prim == GE_PRIM_TRIANGLES);

	switch (draw.prim) {
	case GE_PRIM_RECTANGLES:
		for (int i = 0; i < count; i += 2) {
			uint16_t z = (uint16_t)tz[i + 1];  // depth from second vertex
			// TODO: Should clip coordinates to the scissor rectangle.
			// We remove the subpixel information here.
			DepthRasterRect(depth, depthStride, draw.scissor, tx[i], ty[i], tx[i + 1], ty[i + 1], z, draw.compareMode);
		}
		gpuStats.numDepthRasterPrims += count / 2;
		break;
	case GE_PRIM_TRIANGLES:
	{
		int stats[4]{};
		// Batches of 4 triangles, as output by the clip function.
		for (int i = 0; i < count; i += 12) {
			switch (draw.compareMode) {
			case ZCompareMode::Greater:
			{
				DepthRaster4Triangles<ZCompareMode::Greater>(stats, depth, depthStride, draw.scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			case ZCompareMode::Less:
			{
				DepthRaster4Triangles<ZCompareMode::Less>(stats, depth, depthStride, draw.scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			case ZCompareMode::Always:
			{
				DepthRaster4Triangles<ZCompareMode::Always>(stats, depth, depthStride, draw.scissor, &tx[i], &ty[i], &tz[i]);
				break;
			}
			}
		}
		gpuStats.numDepthRasterBackface += stats[(int)TriangleResult::Backface];
		gpuStats.numDepthRasterNoPixels += stats[(int)TriangleResult::NoPixels];
		gpuStats.numDepthRasterTooSmall += stats[(int)TriangleResult::TooSmall];
		gpuStats.numDepthRasterPrims += stats[(int)TriangleResult::OK];
		break;
	}
	default:
		_dbg_assert_(false);
	}
}
