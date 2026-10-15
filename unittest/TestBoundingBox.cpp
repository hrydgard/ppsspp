// Copyright (c) 2012- PPSSPP Project.

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

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Math/CrossSIMD.h"
#include "Common/TimeUtil.h"
#include "Core/System.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "unittest/UnitTest.h"

// The one-vertex-at-a-time TestBoundingBoxFast, as the reference.
template<u32 posFmt, u32 idxFmt>
static bool RefTestBoundingBoxFast(const float *cullMatrix, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, ClipInfoFlags *clipInfoFlags) {
	Mat4F32 cullMat(cullMatrix);
	alignas(16) static const float planesXYData[4] = { 1, -1, 1, -1 };
	Vec4F32 planesXY = Vec4F32::LoadAligned(planesXYData);
	Vec4S32 insideMaskXY = Vec4S32::Zero();
	Vec4S32 insideMaskZ = Vec4S32::Zero();  // Note: This does some duplicate computation. We could avoid it on ARM32 using Vec2S32 but not really worth it.
	Vec4S32 anyOutsideMaskZ = Vec4S32::Zero();

	// Used to reduce the Z precision. This effectively implements the small offsets where Z can be very slightly outside -1..1.
	// In reality we should probably affect X and Y too, but meh.
	alignas(16) static const u32 vertexMaskData[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF00, 0xFFFFFFFF};
	const int stride = dec->VertexSize();
	const s8 *srcdata = (const s8 *)vdata + dec->posoff;
	const s8 *data = srcdata;

	const float vpZScale = gstate.getViewportZScale();

	float minProjZ = FLT_MAX;
	float maxProjZ = -FLT_MAX;

	for (int i = 0; i < vertexCount; i++) {
		switch (idxFmt) {
		case GE_VTYPE_IDX_8BIT:
		{
			u8 idx = ((u8 *)idata)[i];
			data = (const s8 *)srcdata + idx * stride;
			break;
		}
		case GE_VTYPE_IDX_16BIT:
		{
			u16 idx = ((u16 *)idata)[i];
			data = (const s8 *)srcdata + idx * stride;
			break;
		}
		case GE_VTYPE_IDX_32BIT:
		{
			// The PSP ignores the upper 16 bits.
			u16 idx = (u16)((u32 *)idata)[i];
			data = (const s8 *)srcdata + idx * stride;
			break;
		}
		default:
			// We just increment data at the end of the loop.
			break;
		}

		Vec4F32 objPos;
		switch (posFmt) {
		case GE_VTYPE_POS_8BIT:
			objPos = Vec4F32::LoadS8Norm(data);
			break;
		case GE_VTYPE_POS_16BIT:
			objPos = Vec4F32::LoadS16Norm((const s16 *)data);
			break;
		default:
			objPos = Vec4F32::Load((const float *)data);
			break;
		}
		Vec4F32 clipPos = objPos.AsVec3ByMatrix44(cullMat);
		Vec4F32 posW = clipPos.ShuffleWWWW();
		Vec4F32 posXY = clipPos.ShuffleXXYY();
		Vec4F32 planeDistXY = posXY * planesXY + posW;
		insideMaskXY |= planeDistXY.CompareGe(Vec4F32::Zero());
		Vec4F32 posZ = clipPos.ShuffleZZZZ();  // This means that we compute the Z sides twice. Oh well.
		// We need to add the same culling epsilons as when setting up the cull distances in the vertex shader,
		// so we don't over-cull here. We could also cull looser, but I can't figure out how to do so accurately.
		// It's a bit unnecessary to take four reciprocals here, let's see if we can avoid that later.
		Vec4F32 deltaZ = posW.RecipApprox() * 0.0000304f;
		Vec4F32 planeDistZ = posZ * planesXY + posW + deltaZ;
		anyOutsideMaskZ |= planeDistZ.CompareLt(Vec4F32::Zero());
		insideMaskZ |= planeDistZ.CompareGe(Vec4F32::Zero());
		const float projZ = vpZScale * clipPos[2] / clipPos[3];
		if (projZ < minProjZ) {
			minProjZ = projZ;
		}
		if (projZ > maxProjZ) {  // else ruins the minss/maxss optimization.
			maxProjZ = projZ;
		}

		if (idxFmt == GE_VTYPE_IDX_NONE) {
			data += stride;
		}
	}

	if (!AllCompareBitsSet(insideMaskXY) || !AllCompareBitsSet(insideMaskZ)) {
		// All vertices were outside one side of the clipping cube. We can skip the draw entirely.
		return false;
	}

	const float vpZOffset = gstate.getViewportZCenter();
	minProjZ += vpZOffset;
	maxProjZ += vpZOffset;

	ClipInfoFlags flags = ClipInfoFlags::Valid;

	// If the W=-Z plane was intersected, here we can go through the vertices again, and check for X/Y bounds for range culling.
	// However! We need to find a valid way to do so by "backprojecting" the range culling into clip space, which may be a little tricky.
	//
	// If nothing is outside the box, the "inversion" cases (vertices hit the boundary after clipping like Flatout, Sengoku Cannon)
	// cannot happen, and soft clipping is only needed if the viewport is smaller than the valid Z range.
	//
	// Alternatively, we just do a compat flag for the affected games until we can solve this.

	const int depthRangeMin = gstate.getDepthRangeMin();
	const int depthRangeMax = gstate.getDepthRangeMax();
	if ((depthRangeMin != 0 && minProjZ < depthRangeMin) || (depthRangeMax != 0xFFFF && maxProjZ > depthRangeMax)) {
		if (gstate_c.Use(GPU_USE_CLIP_DISTANCE)) {
			flags |= ClipInfoFlags::MinMaxZClip;
		} else {
			// Implement min/max in the fragment shader.
			flags |= ClipInfoFlags::MinMaxZDiscard;
		}
	}

	if (AnyCompareBitsSet(anyOutsideMaskZ) && (!gstate_c.viewportNearPlaneMatchesOutput || PSP_CoreParameter().compat.flags().CorrectCullAfterClip || !gstate_c.Use(GPU_USE_CULL_DISTANCE) || !gstate_c.Use(GPU_USE_CLIP_DISTANCE))) {
		// Some vertices were outside the Z clipping planes. Clip againt Z=-W in software (and do culling, too).
		// Without cull distances the shader can't cull, and triangles entirely beyond the far plane would be
		// depth clamped and drawn (the stands over the pitch in FIFA Soccer). Without clip distances it can't
		// clip against the near plane (Test Drive's hills over the sky, on old Mali).
		// TODO: With a compat flag for Flatout/Sengoku, we'll be able to avoid this in many cases.
		flags |= ClipInfoFlags::SoftClipCull;
	}

	if (minProjZ == maxProjZ) {
		// Probably a 2D draw. Send it through software transform!
		flags |= ClipInfoFlags::FlatZ | ClipInfoFlags::SoftClipCull;
	}

	if (needFragmentDepthClamp()) {
		if (gstate_c.Use(GPU_USE_DEPTH_CLAMP)) {
			if (minProjZ < 0 || maxProjZ > 65535) {
				flags |= ClipInfoFlags::DepthClamp;
			}
		} else if (minProjZ <= 0 || maxProjZ >= 65535) {
			// Including draws right on the limit (Puyo Puyo Fever 2's flat Z = 0 ground): hardware clamp keeps
			// what rounding puts just outside, the clip without it doesn't.
			flags |= ClipInfoFlags::DepthClampFragment;
		}
	}

	*clipInfoFlags = flags;
	return true;
}

typedef bool (*BoundingBoxFunc)(const float *, const void *, const void *, int, const VertexDecoder *, ClipInfoFlags *);

static BoundingBoxFunc RefBoundingBoxFunc(u32 vtype) {
#define REF_IDX(pos) \
	switch (vtype & GE_VTYPE_IDX_MASK) { \
	case GE_VTYPE_IDX_NONE: return &RefTestBoundingBoxFast<pos, GE_VTYPE_IDX_NONE>; \
	case GE_VTYPE_IDX_8BIT: return &RefTestBoundingBoxFast<pos, GE_VTYPE_IDX_8BIT>; \
	case GE_VTYPE_IDX_16BIT: return &RefTestBoundingBoxFast<pos, GE_VTYPE_IDX_16BIT>; \
	default: return &RefTestBoundingBoxFast<pos, GE_VTYPE_IDX_32BIT>; \
	}
	switch (vtype & GE_VTYPE_POS_MASK) {
	case GE_VTYPE_POS_8BIT: REF_IDX(GE_VTYPE_POS_8BIT);
	case GE_VTYPE_POS_16BIT: REF_IDX(GE_VTYPE_POS_16BIT);
	default: REF_IDX(GE_VTYPE_POS_FLOAT);
	}
#undef REF_IDX
}

struct BoundingBoxRng {
	u32 state = 0x12345678;
	u32 Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	int Int(int n) { return (int)(Next() % (u32)n); }
	float Float(float lo, float hi) { return lo + (hi - lo) * (float)(Next() & 0xFFFFFF) / (float)0x1000000; }
};

static u32 ToFloat24(float f) {
	u32 bits;
	memcpy(&bits, &f, sizeof(bits));
	return bits >> 8;
}

static const u32 posFormats[] = { GE_VTYPE_POS_8BIT, GE_VTYPE_POS_16BIT, GE_VTYPE_POS_FLOAT };
static const u32 idxFormats[] = { GE_VTYPE_IDX_NONE, GE_VTYPE_IDX_8BIT, GE_VTYPE_IDX_16BIT, GE_VTYPE_IDX_32BIT };

// Fills a draw with random vertices (positions mostly in a range that straddles the clipping cube, the rest of
// each vertex random bytes) and indices.
static void MakeBoundingBoxDraw(BoundingBoxRng &rng, const VertexDecoder &dec, u32 vtype, int numVerts, int count, std::vector<u8> &verts, std::vector<u8> &inds) {
	const int stride = dec.VertexSize();
	verts.resize(stride * numVerts + 16);
	for (u8 &b : verts)
		b = (u8)rng.Next();
	for (int v = 0; v < numVerts; v++) {
		u8 *pos = verts.data() + v * stride + dec.posoff;
		for (int c = 0; c < 3; c++) {
			switch (vtype & GE_VTYPE_POS_MASK) {
			case GE_VTYPE_POS_8BIT: pos[c] = (u8)rng.Next(); break;
			case GE_VTYPE_POS_16BIT: { s16 s = (s16)rng.Next(); memcpy(pos + c * 2, &s, 2); break; }
			default:
			{
				float f = rng.Float(-2.0f, 2.0f);
				const int special = rng.Int(64);
				if (special == 0)
					f = 0.0f;
				else if (special == 1)
					f = -0.0f;
				else if (special == 2)
					f = rng.Float(-1e30f, 1e30f);
				else if (special == 3)
					f = NAN;
				memcpy(pos + c * 4, &f, 4);
				break;
			}
			}
		}
	}
	const int idxSize = (vtype & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_8BIT ? 1 : (vtype & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT ? 2 : 4;
	inds.resize(count * idxSize);
	for (int i = 0; i < count; i++) {
		// The PSP ignores the upper 16 bits of 32-bit indices.
		u32 idx = (u32)rng.Int(numVerts) | (idxSize == 4 ? (rng.Next() & 0xFFFF0000) : 0);
		memcpy(inds.data() + i * idxSize, &idx, idxSize);
	}
}

// The vectorized TestBoundingBoxFast against the one-vertex-at-a-time version it replaced: the same result and
// flags for random draws in every position and index format.
bool TestBoundingBoxFast() {
	const GEState savedState = gstate;
	BoundingBoxRng rng;
	VertexDecoderOptions opts{};
	std::vector<u8> verts, inds;
	int culled = 0, passed = 0, mismatches = 0;

	for (int trial = 0; trial < 30000; trial++) {
		const u32 vtype = posFormats[rng.Int(3)] | idxFormats[rng.Int(4)] | (rng.Int(2) ? GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888 : 0);
		VertexDecoder dec{};
		dec.SetVertexType(GetVertTypeID(vtype, 0), opts, nullptr);
		const bool indexed = (vtype & GE_VTYPE_IDX_MASK) != 0;
		const int count = 1 + rng.Int(trial % 8 == 0 ? 300 : 13);
		const int numVerts = indexed ? 1 + rng.Int(64) : count;
		MakeBoundingBoxDraw(rng, dec, vtype, numVerts, count, verts, inds);

		// A projection-like matrix: some of the draws are behind the camera, or partly outside.
		float matrix[16];
		for (int i = 0; i < 16; i++)
			matrix[i] = rng.Float(-1.5f, 1.5f);
		matrix[15] = rng.Float(-0.5f, 3.0f);

		gstate.viewportzscale = (GE_CMD_VIEWPORTZSCALE << 24) | ToFloat24(rng.Float(-40000.0f, 40000.0f));
		gstate.viewportzcenter = (GE_CMD_VIEWPORTZCENTER << 24) | ToFloat24(rng.Float(0.0f, 65535.0f));
		gstate.minz = (GE_CMD_MINZ << 24) | (rng.Int(2) ? 0 : rng.Int(30000));
		gstate.maxz = (GE_CMD_MAXZ << 24) | (rng.Int(2) ? 0xFFFF : 30000 + rng.Int(35535));
		gstate.depthClipEnable = (GE_CMD_DEPTHCLIPENABLE << 24) | rng.Int(2);

		ClipInfoFlags refFlags{}, flags{};
		const bool refResult = RefBoundingBoxFunc(vtype)(matrix, verts.data(), inds.data(), count, &dec, &refFlags);
		const bool result = DrawEngineCommon::TestBoundingBoxFast(matrix, verts.data(), inds.data(), count, &dec, vtype, &flags);
		if (result != refResult || (result && flags != refFlags)) {
			if (mismatches++ < 10)
				printf("Mismatch: vtype %08x count %d: %d (flags %x), expected %d (flags %x)\n", vtype, count, result, (u32)flags, refResult, (u32)refFlags);
		}
		(refResult ? passed : culled)++;
	}
	printf("%d draws culled, %d passed\n", culled, passed);
	EXPECT_EQ_INT(mismatches, 0);
	// Both outcomes need plenty of cases for the comparison to mean anything.
	EXPECT_TRUE(culled > 3000 && passed > 3000);

	// Speed, on a typical large draw: 16-bit positions, no indices.
	{
		const u32 vtype = GE_VTYPE_POS_16BIT | GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888;
		VertexDecoder dec{};
		dec.SetVertexType(GetVertTypeID(vtype, 0), opts, nullptr);
		const int count = 256;
		MakeBoundingBoxDraw(rng, dec, vtype, count, count, verts, inds);
		const float matrix[16] = { 1.2f, 0, 0, 0, 0, 1.5f, 0, 0, 0, 0, -1.0f, -1.0f, 0, 0, 0.9f, 1.1f };
		gstate.minz = (GE_CMD_MINZ << 24);
		gstate.maxz = (GE_CMD_MAXZ << 24) | 0xFFFF;
		const int reps = 20000;
		for (int pass = 0; pass < 2; pass++) {
			ClipInfoFlags flags{};
			int sum = 0;
			const double start = time_now_d();
			for (int r = 0; r < reps; r++) {
				if (pass == 0)
					sum += RefTestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_NONE>(matrix, verts.data(), nullptr, count, &dec, &flags);
				else
					sum += DrawEngineCommon::TestBoundingBoxFast(matrix, verts.data(), nullptr, count, &dec, vtype, &flags);
			}
			const double ns = (time_now_d() - start) * 1e9 / ((double)reps * count);
			printf("%s: %.2f ns per vertex (%d)\n", pass == 0 ? "one at a time" : "four at a time", ns, sum);
		}
	}

	gstate = savedState;
	return true;
}
