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

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/LogReporting.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/Math/CrossSIMD.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/TimeUtil.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/GPUStateSIMDUtil.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DepthRaster.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "Common/CPUDetect.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexReader.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex),
};

DrawEngineCommon::DrawEngineCommon() : decoderMap_(32) {
	if (g_Config.bVertexDecoderJit && PSP_CoreParameter().bUseVertexDecoderJit) {
		decJitCache_ = new VertexDecoderJitCache();
	}
	transformed_ = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	transformedExpanded_ = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decoded_ = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex_ = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	bboxScratch_ = (u8 *)AllocateMemoryPages(BBOX_SCRATCH_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	_dbg_assert_(transformed_);
	_dbg_assert_(transformedExpanded_);
	_dbg_assert_(decoded_);
	_dbg_assert_(decIndex_);
	_dbg_assert_(bboxScratch_);

	indexGen.Setup(decIndex_);

	InitDepthRaster();
}

DrawEngineCommon::~DrawEngineCommon() {
	FreeMemoryPages(decoded_, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex_, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(bboxScratch_, BBOX_SCRATCH_SIZE);
	FreeMemoryPages(transformed_, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded_, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);
	ShutdownDepthRaster();
	delete decJitCache_;
	decoderMap_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		delete decoder;
	});
	ClearSplineBezierWeights();
}

void DrawEngineCommon::Init() {
	NotifyConfigChanged();
}

std::vector<std::string> DrawEngineCommon::DebugGetVertexLoaderIDs() {
	std::vector<std::string> ids;
	decoderMap_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		std::string id;
		id.resize(sizeof(vtype));
		memcpy(&id[0], &vtype, sizeof(vtype));
		ids.push_back(id);
	});
	return ids;
}

std::string DrawEngineCommon::DebugGetVertexLoaderString(std::string_view id, DebugShaderStringType stringType) {
	if (id.size() < sizeof(u32)) {
		return "N/A";
	}
	u32 mapId;
	memcpy(&mapId, &id[0], sizeof(mapId));
	VertexDecoder *dec;
	if (decoderMap_.Get(mapId, &dec)) {
		return dec->GetString(stringType);
	} else {
		return "N/A";
	}
}

void DrawEngineCommon::NotifyConfigChanged() {
	if (decJitCache_)
		decJitCache_->Clear();
	lastVType_ = -1;
	dec_ = nullptr;
	decoderMap_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		delete decoder;
	});
	decoderMap_.Clear();

	useHWTransform_ = g_Config.bHardwareTransform;
}

void DrawEngineCommon::DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation) {
	// Instead of plumbing through properly (we'd need to inject these pretransformed vertices in the middle
	// of SoftwareTransform(), which would take a lot of refactoring), we'll cheat and just turn these into
	// through vertices.
	// Since the only known use is Thrillville and it only uses it to clear, we just use color and pos.
	struct ImmVertex {
		float uv[2];
		uint32_t color;
		float xyz[3];
	};
	std::vector<ImmVertex> temp;
	temp.resize(vertexCount);
	uint32_t color1Used = 0;
	for (int i = 0; i < vertexCount; i++) {
		// Since we're sending through, scale back up to w/h.
		temp[i].uv[0] = buffer[i].u * gstate.getTextureWidth(0);
		temp[i].uv[1] = buffer[i].v * gstate.getTextureHeight(0);
		temp[i].color = buffer[i].color0_32;
		temp[i].xyz[0] = buffer[i].pos[0];
		temp[i].xyz[1] = buffer[i].pos[1];
		temp[i].xyz[2] = buffer[i].pos[2];
		color1Used |= buffer[i].color1_32;
	}
	int vtype = GE_VTYPE_TC_FLOAT | GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_THROUGH;
	// TODO: Handle fog and secondary color somehow?

	if (gstate.isFogEnabled() && !gstate.isModeThrough()) {
		WARN_LOG_REPORT_ONCE(geimmfog, Log::G3D, "Imm vertex used fog");
	}
	if (color1Used != 0 && gstate.isUsingSecondaryColor() && !gstate.isModeThrough()) {
		WARN_LOG_REPORT_ONCE(geimmcolor1, Log::G3D, "Imm vertex used secondary color");
	}

	bool prevThrough = gstate.isModeThrough();
	// Code checks this reg directly, not just the vtype ID.
	if (!prevThrough) {
		gstate.vertType |= GE_VTYPE_THROUGH;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
	}

	int bytesRead;
	uint32_t vertTypeID = GetVertTypeID(vtype, 0);

	bool clockwise = !gstate.isCullEnabled() || gstate.getCullMode() == cullMode;
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	SubmitPrim(&temp[0], nullptr, prim, vertexCount, dec, vertTypeID, clockwise, &bytesRead, clipInfoFlags_);
	Flush();

	if (!prevThrough) {
		gstate.vertType &= ~GE_VTYPE_THROUGH;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
	}
}

// This code has plenty of potential for optimization.
//
// It does the simplest and safest test possible: If all points of a bbox is outside a single of
// our clipping planes, we reject the box. Tighter bounds would be desirable but would take more calculations.
// The name is a slight misnomer, because any bounding shape will work, not just boxes.
//
// Potential optimizations:
// * SIMD-ify the plane culling, and also the vertex data conversion (could even group together xxxxyyyyzzzz for example)
// * Compute min/max of the verts, and then compute a bounding sphere and check that against the planes.
//   - Less accurate, but..
//   - Only requires six plane evaluations then.
bool DrawEngineCommon::TestBoundingBox(const void *vdata, const void *inds, int vertexCount, const VertexDecoder *dec, u32 vertType) {
	// The scratch buffer is sized for 1024 vertices. Although this may lead to drawing that shouldn't happen,
	// the viewport is more complex on VR. Let's always say objects are within bounds.
	if (vertexCount > 1024 || gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		return true;
	}

	SimpleVertex *corners = (SimpleVertex *)(bboxScratch_ + BBOX_SCRATCH_CORNERS_OFFSET);
	float *verts = (float *)(bboxScratch_ + BBOX_SCRATCH_VERTS_OFFSET);

	// Try to skip NormalizeVertices if it's pure positions. No need to bother with a vertex decoder
	// and a large vertex format.

	// BBOX games:
	// - Outrun 2006
	// - Tekken 6  (FLOAT only)
	// - Smash Court Tennis 3 (All formats)
	// - Need for Speed Carbon

	if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_FLOAT && !inds) {
		// Most games that use bbox use floating point bboxes (Outrun, Tekken 6, Smash Court Tennis 3, Need for Speed Carbon etc).
		// memcpy(verts, vdata, sizeof(float) * 3 * vertexCount);
		verts = (float *)vdata;
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_8BIT && !inds) {
		const s8 *vtx = (const s8 *)vdata;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 128.0f);
		}
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_16BIT && !inds) {
		const s16 *vtx = (const s16 *)vdata;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 32768.0f);
		}
	} else {
		// Simplify away indices, bones, and morph before proceeding.
		u8 *temp_buffer = bboxScratch_ + BBOX_SCRATCH_TEMP_OFFSET;

		if ((inds || (vertType & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)))) {
			// Need for Speed Carbon ends up on this path! With a single bone weight.

			u16 indexLowerBound = 0;
			u16 indexUpperBound = (u16)vertexCount - 1;

			if (vertexCount > 0 && inds) {
				GetIndexBounds(inds, vertexCount, vertType, &indexLowerBound, &indexUpperBound);
				if (indexUpperBound > 1024) {
					// NormalizeVertices below writes indexUpperBound - indexLowerBound + 1 vertices
					// into corners, which only has room until the verts region above it. The index
					// values are the game's, so the vertexCount cap doesn't bound them. A bbox test
					// over this many verts is counter-productive anyway - say it's visible.
					return true;
				}
			}
			// TODO: Avoid normalization if just plain skinning.
			const u32 vertTypeID = GetVertTypeID(vertType, gstate.getUVGenMode());
			UVScale uvScale{};  // We don't care about UV.
			::NormalizeVertices(corners, temp_buffer, (const u8 *)vdata, indexLowerBound, indexUpperBound, uvScale, dec, vertType);
			IndexConverter conv(vertType, inds);
			for (int i = 0; i < vertexCount; i++) {
				verts[i * 3] = corners[conv(i)].pos.x;
				verts[i * 3 + 1] = corners[conv(i)].pos.y;
				verts[i * 3 + 2] = corners[conv(i)].pos.z;
			}
		} else {
			// Simple, most common case.
			int stride = dec->VertexSize();
			int offset = dec->posoff;
			switch (vertType & GE_VTYPE_POS_MASK) {
			case GE_VTYPE_POS_8BIT:
				for (int i = 0; i < vertexCount; i++) {
					const s8 *data = (const s8 *)vdata + i * stride + offset;
					for (int j = 0; j < 3; j++) {
						verts[i * 3 + j] = data[j] * (1.0f / 128.0f);
					}
				}
				break;
			case GE_VTYPE_POS_16BIT:
				for (int i = 0; i < vertexCount; i++) {
					const s16 *data = ((const s16 *)((const s8 *)vdata + i * stride + offset));
					for (int j = 0; j < 3; j++) {
						verts[i * 3 + j] = data[j] * (1.0f / 32768.0f);
					}
				}
				break;
			case GE_VTYPE_POS_FLOAT:
				for (int i = 0; i < vertexCount; i++)
					memcpy(&verts[i * 3], (const u8 *)vdata + stride * i + offset, sizeof(float) * 3);
				break;
			}
		}
	}

	// Unclear why the top/left is off by a pixel.
	const int left   = gstate.getOffsetX() + std::max(gstate.getRegionX1(), gstate.getScissorX1()) - 1;
	const int top    = gstate.getOffsetY() + std::max(gstate.getRegionY1(), gstate.getScissorY1()) - 1;
	const int right  = gstate.getOffsetX() + std::min(gstate.getRegionX2(), gstate.getScissorX2()) + 1;
	const int bottom = gstate.getOffsetY() + std::min(gstate.getRegionY2(), gstate.getScissorY2()) + 1;

	// This is strange, it seems if the draw box is at all outside the 4096x4096 coordinate space, all checks pass.
	// It seems very odd that the hardware would have checks for this.
	if (right >= 4096 || bottom >= 4096 || left < 1.0f || top < 1.0f) {
		return true;
	}

	// TODO: How accurate should we be?
	// TODO: Use CrossSIMD.
	int insideCount[6] = {0};
	for (int i = 0; i < vertexCount; i++) {
		// Complete the transform to see if the vertex should be ignored. Not sure if we need to go to these lengths...
		const float *objpos = verts + i * 3;

		float projpos[4];
		Vec3ByMatrix44(projpos, objpos, gstate_c.worldviewproj);

		if (projpos[2] >= -projpos[3]) {
			insideCount[4]++;
		}
		if (projpos[2] <= projpos[3]) {
			insideCount[5]++;
		}

		const float w = projpos[3];
		// const float invW = 1.0f / w;
		const float screenpos[3] = {
			(projpos[0] * gstate.getViewportXScale()) + gstate.getViewportXCenter() * w,
			(projpos[1] * gstate.getViewportYScale()) + gstate.getViewportYCenter() * w,
			(projpos[2] * gstate.getViewportZScale()) + gstate.getViewportZCenter() * w,
		};

		const float drawX = screenpos[0];
		const float drawY = screenpos[1];

		if (drawX >= left * w) {
			insideCount[0]++;
		}
		if (drawX <= right * w) {
			insideCount[1]++;
		}
		if (drawY >= top * w) {
			insideCount[2]++;
		}
		if (drawY <= bottom * w) {
			insideCount[3]++;
		}
	}

	int countToCheck = gstate.isDepthClipEnabled() ? 6 : 4;
#if 0
	// For debugging, the exclusive check. This should make it obvious where the culling borders are in screen space.
	for (int i = 0; i < countToCheck; i++) {
		if (insideCount[i] != vertexCount) {
			return false;
		}
	}
#endif

	for (int i = 0; i < countToCheck; i++) {
		if (insideCount[i] == 0) {
			// All verts were outside one side.
			return false;
		}
	}

	return true;
}

// This optionally culls collections of points against the six planes, and always computes the min and max of Z and W.
//
// The result of that is then used to determine if we need to drop down to software transform+clip or we can hand
// off to hardware, with whatever capabilities are available.
//
// NOTE: This doesn't handle through-mode or indexing (morph or skinning can be handled if they're implemented in software during decode).
template<u32 idxFmt>
static inline const s8 *BoundingBoxVertex(const s8 *srcdata, const void *idata, int stride, int i) {
	switch (idxFmt) {
	case GE_VTYPE_IDX_8BIT: return srcdata + ((const u8 *)idata)[i] * stride;
	case GE_VTYPE_IDX_16BIT: return srcdata + ((const u16 *)idata)[i] * stride;
	// The PSP ignores the upper 16 bits.
	case GE_VTYPE_IDX_32BIT: return srcdata + (u16)((const u32 *)idata)[i] * stride;
	default: return srcdata + i * stride;
	}
}

template<u32 posFmt>
static inline Vec4F32 BoundingBoxPos(const s8 *data) {
	switch (posFmt) {
	case GE_VTYPE_POS_8BIT: return Vec4F32::LoadS8Norm(data);
	case GE_VTYPE_POS_16BIT: return Vec4F32::LoadS16Norm((const s16 *)data);
	default: return Vec4F32::Load((const float *)data);
	}
}

// a / b, rounded correctly (ARM32 NEON can only approximate it).
static inline Vec4F32 DivideExact(Vec4F32 a, Vec4F32 b) {
#if PPSSPP_ARCH(ARM_NEON) && !PPSSPP_ARCH(ARM64_NEON)
	// The VFP registers overlap the NEON ones, so these divide in place.
	float32x4_t r = a.v;
	r = vsetq_lane_f32(vgetq_lane_f32(a.v, 0) / vgetq_lane_f32(b.v, 0), r, 0);
	r = vsetq_lane_f32(vgetq_lane_f32(a.v, 1) / vgetq_lane_f32(b.v, 1), r, 1);
	r = vsetq_lane_f32(vgetq_lane_f32(a.v, 2) / vgetq_lane_f32(b.v, 2), r, 2);
	r = vsetq_lane_f32(vgetq_lane_f32(a.v, 3) / vgetq_lane_f32(b.v, 3), r, 3);
	return Vec4F32{ r };
#else
	a /= b;
	return a;
#endif
}

// Whether ClipCullPrims can clip every triangle of a draw with this state, not needing the clip distance.
static bool CpuCanClipAll() {
	const GETexMapMode uvGen = gstate.getUVGenMode();
	const bool normalTexGen = uvGen == GE_TEXMAP_ENVIRONMENT_MAP || (uvGen == GE_TEXMAP_TEXTURE_MATRIX && gstate.getUVProjMode() == GE_PROJMAP_NORMALIZED_NORMAL);
	return gstate.getShadeMode() != GE_SHADE_FLAT && !(gstate.isTextureMapEnabled() && normalTexGen);
}

// This optionally culls collections of points against the six planes, and always computes the min and max of Z and W.
//
// The result of that is then used to determine if we need to drop down to software transform+clip or we can hand
// off to hardware, with whatever capabilities are available.
//
// NOTE: This doesn't handle through-mode (morph or skinning can be handled if they're implemented in software during decode).
// Whether screen Z from minProjZ to maxProjZ reaches past a depth range that isn't the full one.
static bool ReachesPastDepthRange(float minProjZ, float maxProjZ) {
	const int depthRangeMin = gstate.getDepthRangeMin();
	const int depthRangeMax = gstate.getDepthRangeMax();
	return (depthRangeMin != 0 && minProjZ < depthRangeMin) || (depthRangeMax != 0xFFFF && maxProjZ > depthRangeMax);
}

template<u32 posFmt, u32 idxFmt>
static bool TestBoundingBoxFast(const float *cullMatrix, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, bool cpuCullSupported, ClipInfoFlags *clipInfoFlags) {
	// Four vertices at a time, one per lane. Past the end, the last vertex fills the lanes, which changes nothing.
	// The matrix stays in four registers, multiplied by lane. Splatted, it took sixteen, and was reloaded from the
	// stack every iteration.
	const Vec4F32 col0 = Vec4F32::Load(cullMatrix);
	const Vec4F32 col1 = Vec4F32::Load(cullMatrix + 4);
	const Vec4F32 col2 = Vec4F32::Load(cullMatrix + 8);
	const Vec4F32 tx = Vec4F32::Splat(cullMatrix[12]);
	const Vec4F32 ty = Vec4F32::Splat(cullMatrix[13]);
	const Vec4F32 tz = Vec4F32::Splat(cullMatrix[14]);
	const Vec4F32 tw = Vec4F32::Splat(cullMatrix[15]);

	// For each side of the clipping cube, whether any vertex is inside it, and whether any is outside the Z sides.
	Vec4S32 insideLeft = Vec4S32::Zero();
	Vec4S32 insideRight = Vec4S32::Zero();
	Vec4S32 insideTop = Vec4S32::Zero();
	Vec4S32 insideBottom = Vec4S32::Zero();
	Vec4S32 insideNear = Vec4S32::Zero();
	Vec4S32 insideFar = Vec4S32::Zero();
	Vec4S32 anyOutsideZ = Vec4S32::Zero();
	Vec4S32 anyBehindNear = Vec4S32::Zero();

	const int stride = dec->VertexSize();
	const s8 *srcdata = (const s8 *)vdata + dec->posoff;
	const int last = vertexCount - 1;

	const Vec4F32 vpZScale = Vec4F32::Splat(gstate.getViewportZScale());
	const Vec4F32 zero = Vec4F32::Zero();
	Vec4F32 minZ = Vec4F32::Splat(FLT_MAX);
	Vec4F32 maxZ = Vec4F32::Splat(-FLT_MAX);
	// With depth clipping, what's behind the near plane is clipped away before it's drawn, so its screen Z (which
	// behind the eye is meaningless) doesn't count, and the near plane's does instead, below. Counting it made every
	// draw crossing the near plane take the fragment shader depth clamp, whose depth doesn't match the other draws'
	// (Monster Hunter 3rd HD's ground on PowerVR GLES).
	const bool depthClip = gstate.isDepthClipEnabled();

	for (int i = 0; i < vertexCount; i += 4) {
		Vec4F32 objX = BoundingBoxPos<posFmt>(BoundingBoxVertex<idxFmt>(srcdata, idata, stride, i));
		Vec4F32 objY = BoundingBoxPos<posFmt>(BoundingBoxVertex<idxFmt>(srcdata, idata, stride, std::min(i + 1, last)));
		Vec4F32 objZ = BoundingBoxPos<posFmt>(BoundingBoxVertex<idxFmt>(srcdata, idata, stride, std::min(i + 2, last)));
		Vec4F32 unused = BoundingBoxPos<posFmt>(BoundingBoxVertex<idxFmt>(srcdata, idata, stride, std::min(i + 3, last)));
		Vec4F32::Transpose(objX, objY, objZ, unused);

		// The same operations in the same order as AsVec3ByMatrix44(), so the same rounding.
		const Vec4F32 x = (objX.MulLane<0>(col0) + objY.MulLane<0>(col1)) + (objZ.MulLane<0>(col2) + tx);
		const Vec4F32 y = (objX.MulLane<1>(col0) + objY.MulLane<1>(col1)) + (objZ.MulLane<1>(col2) + ty);
		const Vec4F32 z = (objX.MulLane<2>(col0) + objY.MulLane<2>(col1)) + (objZ.MulLane<2>(col2) + tz);
		const Vec4F32 w = (objX.MulLane<3>(col0) + objY.MulLane<3>(col1)) + (objZ.MulLane<3>(col2) + tw);

		insideLeft |= (x + w).CompareGe(zero);
		insideRight |= (w - x).CompareGe(zero);
		insideTop |= (y + w).CompareGe(zero);
		insideBottom |= (w - y).CompareGe(zero);
		// We need to add the same culling epsilons as when setting up the cull distances in the vertex shader,
		// so we don't over-cull here. We could also cull looser, but I can't figure out how to do so accurately.
		const Vec4F32 deltaZ = w.RecipApprox() * 0.0000304f;
		const Vec4F32 nearDist = (z + w) + deltaZ;
		const Vec4F32 farDist = (w - z) + deltaZ;
		insideNear |= nearDist.CompareGe(zero);
		insideFar |= farDist.CompareGe(zero);
		anyOutsideZ |= nearDist.CompareLt(zero) | farDist.CompareLt(zero);
		const Vec4S32 behindNear = (z + w).CompareLt(zero);
		anyBehindNear |= behindNear;

		// NaN compares false, so it's never picked.
		const Vec4F32 projZ = DivideExact(vpZScale * z, w);
		const Vec4S32 notCounted = depthClip ? behindNear : Vec4S32::Zero();
		minZ = Select(projZ.CompareLt(minZ).AndNot(notCounted), projZ, minZ);
		maxZ = Select(projZ.CompareGt(maxZ).AndNot(notCounted), projZ, maxZ);
	}

	if (!AnyCompareBitsSet(insideLeft) || !AnyCompareBitsSet(insideRight) || !AnyCompareBitsSet(insideTop) ||
		!AnyCompareBitsSet(insideBottom) || !AnyCompareBitsSet(insideNear) || !AnyCompareBitsSet(insideFar)) {
		// All vertices were outside one side of the clipping cube. We can skip the draw entirely.
		return false;
	}

	float minProjZ = FLT_MAX;
	float maxProjZ = -FLT_MAX;
	for (int i = 0; i < 4; i++) {
		if (minZ[i] < minProjZ)
			minProjZ = minZ[i];
		if (maxZ[i] > maxProjZ)
			maxProjZ = maxZ[i];
	}

	const float vpZOffset = gstate.getViewportZCenter();
	minProjZ += vpZOffset;
	maxProjZ += vpZOffset;
	// Of the vertices themselves, without what clipping makes.
	const float minVertexZ = minProjZ, maxVertexZ = maxProjZ;
	const bool clipsNear = depthClip && AnyCompareBitsSet(anyBehindNear);
	// Where clipping puts the vertices it makes (z = -w).
	const float nearZ = vpZOffset - gstate.getViewportZScale();
	if (clipsNear) {
		minProjZ = std::min(minProjZ, nearZ);
		maxProjZ = std::max(maxProjZ, nearZ);
	}

	ClipInfoFlags flags = ClipInfoFlags::Valid;

	// If the W=-Z plane was intersected, here we can go through the vertices again, and check for X/Y bounds for range culling.
	// However! We need to find a valid way to do so by "backprojecting" the range culling into clip space, which may be a little tricky.
	//
	// If nothing is outside the box, the "inversion" cases (vertices hit the boundary after clipping like Flatout, Sengoku Cannon)
	// cannot happen, and soft clipping is only needed if the viewport is smaller than the valid Z range.
	//
	// Alternatively, we just do a compat flag for the affected games until we can solve this.

	if (ReachesPastDepthRange(minProjZ, maxProjZ)) {
		if (gstate_c.Use(GPU_USE_CLIP_DISTANCE)) {
			flags |= ClipInfoFlags::MinMaxZClip;
		} else {
			// Implement min/max in the fragment shader.
			flags |= ClipInfoFlags::MinMaxZDiscard;
		}
	}

	// With both of the viewport's Z planes on the min/max clip planes, those clip exactly like the GE, and also take
	// away what's entirely beyond the far plane, which otherwise only cull distances can (Naruto Shippuden: Ultimate
	// Ninja Impact's grass, whose depth then matches the ground's).
	const bool clipPlanesDoIt = gstate_c.viewportZPlanesOnClipPlanes && gstate.isDepthClipEnabled();
	const bool hardwareClipsZ = gstate_c.Use(GPU_USE_CLIP_DISTANCE) && (clipPlanesDoIt || (gstate_c.Use(GPU_USE_CULL_DISTANCE) && gstate_c.viewportNearPlaneMatchesOutput));
	// The GPU clips and culls like the GE then, except for range culling the parts clipping makes, which the games
	// flagged CorrectCullAfterClip need.
	const bool correctCullAfterClip = PSP_CoreParameter().compat.flags().CorrectCullAfterClip;
	const bool gpuDoesZ = hardwareClipsZ && !correctCullAfterClip;
	// Otherwise the CPU clips at the near plane and culls like the GE, including the range culling after clipping, and
	// the vertices stay on hardware transform, the same math as the other passes over the surface: mixing in software
	// transform made those depth tests fail on PowerVR. What it can't clip is left to the clip distance.
	const bool cpuCulls = cpuCullSupported && !gpuDoesZ && (!clipPlanesDoIt || correctCullAfterClip) && gstate.isDepthClipEnabled() && (gstate_c.Use(GPU_USE_CLIP_DISTANCE) || CpuCanClipAll());
	if (cpuCulls) {
		if (AnyCompareBitsSet(anyOutsideZ)) {
			flags |= ClipInfoFlags::CpuCull;
		}
	} else if (AnyCompareBitsSet(anyOutsideZ) && !gpuDoesZ) {
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
		} else if (minVertexZ <= 0 || maxVertexZ >= 65535 || (clipsNear && (nearZ < 0 || nearZ > 65535))) {
			// Including vertices right on the limit (Puyo Puyo Fever 2's flat Z = 0 ground): hardware clamp keeps
			// what rounding puts just outside, the clip without it doesn't. On the near plane, losing that is the
			// near clip.
			flags |= ClipInfoFlags::DepthClampFragment;
		}
	}

	*clipInfoFlags = flags;
	return true;
}

// Never CpuCull or SoftClipCull: without the bounding box test, nothing says the draw crosses a Z plane.
ClipInfoFlags DrawEngineCommon::UncheckedClipInfoFlags() {
	ClipInfoFlags flags{};
	if (gstate.isModeThrough() || gstate.isModeClear()) {
		return flags;
	}
	// Hardware depth clamp and clip planes are always on for these, so do the same with the fallbacks.
	if (needFragmentDepthClamp() && !gstate_c.Use(GPU_USE_DEPTH_CLAMP)) {
		flags |= ClipInfoFlags::Valid | ClipInfoFlags::DepthClampFragment;
	}
	if (needFragmentMinMaxClipping() && !gstate_c.Use(GPU_USE_CLIP_DISTANCE)) {
		// The discard costs fill rate (a quarter of the frame in Tekken 6 at 4x on Mali), and skinned draws are
		// rarely past the range, so they're checked once decoded. Morphed ones are rare, and keep it.
		const bool skinned = (gstate.vertType & GE_VTYPE_WEIGHT_MASK) != 0;
		flags |= ClipInfoFlags::Valid | (skinned ? ClipInfoFlags::MinMaxZCheck : ClipInfoFlags::MinMaxZDiscard);
	}
	return flags;
}

void DrawEngineCommon::CheckSkinnedMinMaxZ(bool useHWTransform) {
	if (!(clipInfoFlags_ & ClipInfoFlags::Valid) || !(clipInfoFlags_ & ClipInfoFlags::MinMaxZCheck)) {
		return;
	}
	clipInfoFlags_ &= ~ClipInfoFlags::MinMaxZCheck;
	if (!useHWTransform) {
		clipInfoFlags_ |= ClipInfoFlags::MinMaxZDiscard;
		return;
	}

	// Into decoded_, where the backends put skinned vertices anyway. Then the screen Z of each, as in
	// TestBoundingBoxFast: four at a time, and only z and w.
	DecodeVerts(dec_, decoded_);
	const int count = numDecodedVerts_;
	if (count == 0) {
		return;
	}
	const DecVtxFormat &fmt = dec_->GetDecVtxFmt();
	const float *m = gstate_c.cullMatrix;
	const Vec4F32 col0 = Vec4F32::Load(m);
	const Vec4F32 col1 = Vec4F32::Load(m + 4);
	const Vec4F32 col2 = Vec4F32::Load(m + 8);
	const Vec4F32 tz = Vec4F32::Splat(m[14]);
	const Vec4F32 tw = Vec4F32::Splat(m[15]);
	const Vec4F32 vpZScale = Vec4F32::Splat(gstate.getViewportZScale());
	const Vec4F32 zero = Vec4F32::Zero();
	const bool depthClip = gstate.isDepthClipEnabled();
	Vec4F32 minZ = Vec4F32::Splat(FLT_MAX);
	Vec4F32 maxZ = Vec4F32::Splat(-FLT_MAX);
	Vec4S32 anyBehindNear = Vec4S32::Zero();
	// The positions are last in the decoded vertex, so each load reads 4 bytes into the next one, or past the last,
	// which is still in decoded_.
	const s8 *posData = (const s8 *)decoded_ + fmt.posoff;
	const int stride = fmt.stride;
	auto test4 = [&](const s8 *p0, const s8 *p1, const s8 *p2, const s8 *p3) {
		Vec4F32 objX = Vec4F32::Load((const float *)p0);
		Vec4F32 objY = Vec4F32::Load((const float *)p1);
		Vec4F32 objZ = Vec4F32::Load((const float *)p2);
		Vec4F32 unused = Vec4F32::Load((const float *)p3);
		Vec4F32::Transpose(objX, objY, objZ, unused);
		const Vec4F32 z = (objX.MulLane<2>(col0) + objY.MulLane<2>(col1)) + (objZ.MulLane<2>(col2) + tz);
		const Vec4F32 w = (objX.MulLane<3>(col0) + objY.MulLane<3>(col1)) + (objZ.MulLane<3>(col2) + tw);
		const Vec4S32 behindNear = (z + w).CompareLt(zero);
		anyBehindNear |= behindNear;
		const Vec4F32 projZ = DivideExact(vpZScale * z, w);
		// With depth clipping, what's behind the near plane is clipped away, and the near plane counts instead.
		const Vec4S32 notCounted = depthClip ? behindNear : Vec4S32::Zero();
		minZ = Select(projZ.CompareLt(minZ).AndNot(notCounted), projZ, minZ);
		maxZ = Select(projZ.CompareGt(maxZ).AndNot(notCounted), projZ, maxZ);
	};
	int i = 0;
	const s8 *p = posData;
	for (; i + 4 <= count; i += 4, p += 4 * stride) {
		test4(p, p + stride, p + 2 * stride, p + 3 * stride);
	}
	if (i < count) {
		// The last vertex fills the remaining lanes, which changes nothing.
		const int last = count - 1;
		test4(p, posData + std::min(i + 1, last) * stride, posData + std::min(i + 2, last) * stride, posData + last * stride);
	}
	float minProjZ = FLT_MAX;
	float maxProjZ = -FLT_MAX;
	for (int i = 0; i < 4; i++) {
		minProjZ = std::min(minProjZ, minZ[i]);
		maxProjZ = std::max(maxProjZ, maxZ[i]);
	}
	const float vpZOffset = gstate.getViewportZCenter();
	minProjZ += vpZOffset;
	maxProjZ += vpZOffset;
	if (depthClip && AnyCompareBitsSet(anyBehindNear)) {
		const float nearZ = vpZOffset - gstate.getViewportZScale();
		minProjZ = std::min(minProjZ, nearZ);
		maxProjZ = std::max(maxProjZ, nearZ);
	}
	if (ReachesPastDepthRange(minProjZ, maxProjZ)) {
		clipInfoFlags_ |= ClipInfoFlags::MinMaxZDiscard;
	}
}

bool DrawEngineCommon::TestBoundingBoxFast(const float *cullMatrix, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, u32 vertType, ClipInfoFlags *flags, bool cpuCullSupported) {
	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		return true;
	} else if (vertexCount == 0) {
		return false;
	}

	// Dispatching like this is a bit ugly, but we want to avoid every possible overhead *inside* TestBoundingBoxFast.
	// That said, I'm not 100% sure it's worth it..
	switch (vertType & GE_VTYPE_IDX_MASK) {
	case GE_VTYPE_IDX_NONE:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, cpuCullSupported, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_8BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_16BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_32BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, cpuCullSupported, flags);
		default:
			break;
		}
		break;
	default:
		break;
	}
	_dbg_assert_(false);
	return true;
}

// 2D bounding box test against scissor. No indexing yet.
// Only supports non-indexed draws with float positions. TODO: Add more float formats.
bool DrawEngineCommon::TestBoundingBoxThrough(GEPrimitiveType prim, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, u32 vertType, int *bytesRead, ClipInfoFlags *flags) {
	// For through mode, we only check FlatZ.
	*flags |= ClipInfoFlags::Valid;

	const int stride = dec->VertexSize();
	const int posOffset = dec->posoff;

	*bytesRead = stride * vertexCount;

	bool allOutsideLeft = true;
	bool allOutsideTop = true;
	bool allOutsideRight = true;
	bool allOutsideBottom = true;
	const float left = gstate.getScissorX1();
	const float top = gstate.getScissorY1();
	const float right = gstate.getScissorX2() + 1;
	const float bottom = gstate.getScissorY2() + 1;

	float minZ = FLT_MAX;
	float maxZ = -FLT_MAX;

	IndexConverter conv(vertType, idata);
	// TODO: This can be SIMD'd, with some trickery.
	for (int i = 0; i < vertexCount; i++) {
		int index = conv(i);

		float x, y, z;
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_FLOAT:
		{
			const float *pos = (const float*)((const u8 *)vdata + stride * index + posOffset);
			x = pos[0];
			y = pos[1];
			z = pos[2];
		}
		break;
		case GE_VTYPE_POS_8BIT:
		{
			// Through mode doesn't really support 8-bit though.
			const u8 *pos8 = (const u8 *)vdata + stride * index + posOffset;
			x = pos8[0];
			y = pos8[1];
			z = pos8[2];
			break;
		}
		case GE_VTYPE_POS_16BIT:
		{
			const s16 *pos16 = (const s16 *)((const u8 *)vdata + stride * index + posOffset);
			x = pos16[0];
			y = pos16[1];
			z = (u16)pos16[2];
			break;
		}
		default:
			return false;
		}
		if (x >= left) {
			allOutsideLeft = false;
		}
		if (x <= right) {
			allOutsideRight = false;
		}
		if (y >= top) {
			allOutsideTop = false;
		}
		if (y <= bottom) {
			allOutsideBottom = false;
		}

		// If prim is rectangles, we only update minZ and maxZ for every second vertex,
		// since the Z for the whole rect is taken from the 2nd.
		if (prim != GE_PRIM_RECTANGLES || (i & 1) == 1) {
			if (z < minZ) {
				minZ = z;
			}
			if (z > maxZ) {
				maxZ = z;
			}
		}
	}

	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		return true;
	}

	if (allOutsideLeft || allOutsideTop || allOutsideRight || allOutsideBottom) {
		return false;
	}

	if (minZ == maxZ) {
		*flags |= ClipInfoFlags::FlatZ;
	}
	return true;
}

bool DrawEngineCommon::EstimateThroughPrimSafeSize(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertType, int *safeWidth, int *safeHeight) {
	if (prim != GE_PRIM_RECTANGLES && prim != GE_PRIM_TRIANGLES) {
		return false;
	}
	if ((vertType & GE_VTYPE_THROUGH_MASK) == 0 || (vertType & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) != 0) {
		return false;
	}

	const int stride = dec->VertexSize();
	const int posOffset = dec->posoff;
	IndexConverter conv(vertType, inds);

	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (int i = 0; i < vertexCount; ++i) {
		const u8 *posPtr = (const u8 *)verts + conv(i) * stride + posOffset;
		float x;
		float y;

		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT:
			x = 0.0f;
			y = 0.0f;
			break;
		case GE_VTYPE_POS_16BIT:
		{
			const s16_le *pos = (const s16_le *)posPtr;
			x = (float)pos[0];
			y = (float)pos[1];
			break;
		}
		case GE_VTYPE_POS_FLOAT:
		{
			const float_le *pos = (const float_le *)posPtr;
			x = pos[0];
			y = pos[1];
			break;
		}
		default:
			return false;
		}

		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x);
		maxY = std::max(maxY, y);
	}

	const int scissorX1 = gstate.getScissorX1();
	const int scissorY1 = gstate.getScissorY1();
	const int scissorX2 = gstate.getScissorX2() + 1;
	const int scissorY2 = gstate.getScissorY2() + 1;
	if (maxX <= scissorX1 || maxY <= scissorY1 || minX >= scissorX2 || minY >= scissorY2) {
		return false;
	}

	*safeWidth = std::clamp((int)ceilf(maxX), 0, scissorX2);
	*safeHeight = std::clamp((int)ceilf(maxY), 0, scissorY2);
	return *safeWidth > 0 && *safeHeight > 0;
}

void DrawEngineCommon::ApplyFramebufferRead(FBOTexState *fboTexState) {
	if (gstate_c.Use(GPU_USE_FRAMEBUFFER_FETCH)) {
		*fboTexState = FBO_TEX_READ_FRAMEBUFFER;
	} else {
		gpuStats.perFrame.numCopiesForShaderBlend++;
		*fboTexState = FBO_TEX_COPY_BIND_TEX;
	}
	gstate_c.Dirty(DIRTY_SHADERBLEND);
}

int DrawEngineCommon::ShaderReadBindFlags() const {
	// The mask forces blending and the logic op into the shader too, so check what the GE asks for.
	const bool maskOnly = pipelineState_.maskState.applyFramebufferRead && !gstate.isAlphaBlendEnabled() &&
		!(gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY);
	return BINDFBCOLOR_MAY_COPY | (maskOnly ? BINDFBCOLOR_MASK_READ : 0) | BINDFBCOLOR_UNCACHED;
}

int DrawEngineCommon::ComputeNumVertsToDecode() const {
	int sum = 0;
	for (int i = 0; i < numDrawVerts_; i++) {
		sum += drawVerts_[i].indexUpperBound + 1 - drawVerts_[i].indexLowerBound;
	}
	return sum;
}

// DrawEngineCommon::vertClass_ bits.
enum : u8 {
	VERT_BEHIND_NEAR = 1,
	VERT_BEYOND_FAR = 2,
};

// Where the edge from the inside vertex a crosses the near plane to the outside vertex b, like the software
// transform's clipper computes it.
static float NearPlaneT(const float *a, const float *b) {
	const float denominator = (b[2] - a[2]) + (b[3] - a[3]);
	const float t = fabsf(denominator) > 0.000001f ? (-a[2] - a[3]) / denominator : 0.0f;
	return std::clamp(t, 0.0f, 1.0f);
}

static bool NearPointOutsideRange(const float *a, const float *b, float t) {
	const float x = a[0] + (b[0] - a[0]) * t;
	const float y = a[1] + (b[1] - a[1]) * t;
	const float w = a[3] + (b[3] - a[3]) * t;
	const float sx = x * gstate.getViewportXScale() / w + gstate.getViewportXCenter();
	const float sy = y * gstate.getViewportYScale() / w + gstate.getViewportYCenter();
	return !(sx >= 0.0f && sy >= 0.0f && sx < 4096.0f && sy < 4096.0f);
}

static void LerpComponent(u8 fmt, u8 *dst, const u8 *a, const u8 *b, float t) {
	int count = 0;
	switch (fmt) {
	case DEC_FLOAT_1: case DEC_FLOAT_2: case DEC_FLOAT_3: case DEC_FLOAT_4:
		count = fmt - DEC_FLOAT_1 + 1;
		for (int i = 0; i < count; i++) {
			const float fa = ((const float *)a)[i], fb = ((const float *)b)[i];
			((float *)dst)[i] = fa + (fb - fa) * t;
		}
		break;
	case DEC_S8_3:
		for (int i = 0; i < 3; i++) {
			const float fa = (s8)a[i], fb = (s8)b[i];
			dst[i] = (u8)(s8)roundf(fa + (fb - fa) * t);
		}
		break;
	case DEC_S16_3:
		for (int i = 0; i < 3; i++) {
			const float fa = ((const s16 *)a)[i], fb = ((const s16 *)b)[i];
			((s16 *)dst)[i] = (s16)roundf(fa + (fb - fa) * t);
		}
		break;
	case DEC_U8_1: case DEC_U8_2: case DEC_U8_3: case DEC_U8_4:
		count = fmt - DEC_U8_1 + 1;
		for (int i = 0; i < count; i++) {
			dst[i] = (u8)roundf(a[i] + (b[i] - a[i]) * t);
		}
		break;
	case DEC_U16_1: case DEC_U16_2: case DEC_U16_3: case DEC_U16_4:
		count = fmt - DEC_U16_1 + 1;
		for (int i = 0; i < count; i++) {
			const float fa = ((const u16 *)a)[i], fb = ((const u16 *)b)[i];
			((u16 *)dst)[i] = (u16)roundf(fa + (fb - fa) * t);
		}
		break;
	default:
		break;
	}
}

DrawEngineCommon::CpuClip DrawEngineCommon::BeginCpuClip(bool useHWTransform) {
	CpuClip clip;
	clip.fmt = dec_->GetDecVtxFmt();
	clip.active = useHWTransform && (clipInfoFlags_ & ClipInfoFlags::Valid) && (clipInfoFlags_ & ClipInfoFlags::CpuCull);
	// With lighting, the CPU lights the ends of the edges it clips, so the vertices clipping makes get interpolated lit
	// colors like on the GE. All lit draws have the stream for them then, so whether one is clipped doesn't change its
	// pipeline.
	clip.cpuLit = useHWTransform && CpuLightsClipped();
	if (clip.cpuLit) {
		clip.fmt.cpuLit = 1;
		clip.fmt.ComputeID();
	}
	if (clip.cpuLit != lastCpuLit_) {
		// Different vertex shader and vertex layout.
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE);
		lastCpuLit_ = clip.cpuLit;
	}
	return clip;
}

void DrawEngineCommon::DecodeClipCull(CpuClip *clip, GEPrimitiveType *prim, int *vertexCount, int *maxIndex, bool *useElements) {
	// In decoded_, not the push buffer, which is too slow to read back.
	DecodeVerts(dec_, decoded_);
	DecodeIndsAndGetData(prim, vertexCount, maxIndex, useElements, true);
	clip->numVerts = numDecodedVerts_;
	const int origVerts = clip->numVerts;
	*vertexCount = ClipCullPrims(*prim, decoded_, clip->fmt, &clip->numVerts, decIndex_, *vertexCount);
	*maxIndex = clip->numVerts;
	if (clip->cpuLit && clip->numVerts > origVerts) {
		clip->cpuLitStream = cpuLitStream_.data();
	}
}

void DrawEngineCommon::DepthRasterSubmitHW(GEPrimitiveType prim, int vertexCount, const CpuClip &clip) {
	if (clip.active) {
		DepthRasterPredecoded(prim, decoded_, clip.numVerts, dec_, vertexCount, &clip.fmt);
	} else {
		DepthRasterSubmitRaw(prim, dec_, dec_->VertexType(), vertexCount);
	}
}

bool DrawEngineCommon::CpuLightsClipped() const {
	// With the clip distance, the GPU clips lit draws after lighting like the GE, but can't range cull what clipping
	// makes.
	return cpuLitStreamSupported_ && gstate.isLightingEnabled() && gstate.getShadeMode() != GE_SHADE_FLAT &&
		(!gstate_c.Use(GPU_USE_CLIP_DISTANCE) || PSP_CoreParameter().compat.flags().CorrectCullAfterClip);
}

int DrawEngineCommon::ClipCullPrims(GEPrimitiveType prim, u8 *verts, const DecVtxFormat &fmt, int *numVerts, u16 *inds, int indexCount) {
	// The indices are forced (DecodeIndsAndGetData), which makes strips and fans lists.
	int perPrim;
	switch (prim) {
	case GE_PRIM_TRIANGLES: perPrim = 3; break;
	case GE_PRIM_LINES: perPrim = 2; break;
	case GE_PRIM_POINTS: perPrim = 1; break;
	default: return indexCount;
	}

	const float *m = gstate_c.worldviewproj;
	const int stride = fmt.stride;
	const int origVerts = *numVerts;
	// Grown, never shrunk, so they aren't cleared every time.
	if (clipPos_.size() < origVerts * 4) {
		clipPos_.resize(origVerts * 4);
		vertClass_.resize(origVerts);
	}
	const Mat4F32 mat(m);
	for (int i = 0; i < origVerts; i++) {
		// The decoded position is the last member, so the fourth lane reads into the next vertex, or the rest of
		// decoded_, and AsVec3ByMatrix44 ignores it.
		const float *pos = (const float *)(verts + i * stride + fmt.posoff);
		float *c = &clipPos_[i * 4];
		Vec4F32::Load(pos).AsVec3ByMatrix44(mat).Store(c);
		// The same as the far cull distance in the vertex shader. With w > 0 and w - z >= 0 it can't be negative, so
		// the division (in front of the far plane, mostly) is skipped there.
		const float farDist = c[3] - c[2];
		const bool beyondFar = (farDist < 0.0f || c[3] <= 0.0f) && farDist + 0.0000304f / c[3] < 0.0f;
		const bool behindNear = c[2] < -c[3];
		vertClass_[i] = (behindNear ? VERT_BEHIND_NEAR : 0) | (beyondFar ? VERT_BEYOND_FAR : 0);
	}

	if (perPrim != 3) {
		int out = 0;
		for (int i = 0; i + perPrim <= indexCount; i += perPrim) {
			bool culled = true;
			for (int j = 0; j < perPrim; j++) {
				culled = culled && (vertClass_[inds[i + j]] & VERT_BEYOND_FAR);
			}
			if (culled) {
				continue;
			}
			for (int j = 0; j < perPrim; j++) {
				inds[out++] = inds[i + j];
			}
		}
		return out;
	}

	// Triangles crossing the near plane get clipped here like the GE does it (ProcessTriangle in Clipper.cpp, from
	// gpu/probe exp43, exp44), into one triangle, or two, (p, a, b) and (p, b, n), with o behind the plane. Each is
	// then range culled on the vertices clipping made. The shader can't do that: it exempts the vertices behind the
	// near plane. The new vertices are interpolated from the decoded ones, so the GPU transforms them like the rest.
	// That's only right where the vertex outputs are linear in them: the GE interpolates the lit colors, so with
	// lighting, the ends of the clipped edges are lit here (fmt.cpuLit), and the new vertices take those colors.
	// Normal-based texgen and flat shading (whose provoking colors would have to move) are left to the GPU's clip
	// distance, culling only what is culled whole.
	const bool rangeCulling = gstate_c.Use(GPU_USE_VS_RANGE_CULLING) && gstate_c.submitType == SubmitType::DRAW;
	const bool canClip = CpuCanClipAll() && (!gstate.isLightingEnabled() || fmt.cpuLit);
	const bool reversed = BatchTrianglesReversed();
	const int maxVerts = std::min((int)VERTEX_BUFFER_MAX, (int)DECODED_VERTEX_BUFFER_SIZE / stride);
	const int maxInds = DECODED_INDEX_BUFFER_SIZE / sizeof(u16);
	int vertCount = origVerts;

	// Lights a vertex on the CPU, once, like the software transform. Its colors stay unused (cpulit1.a = 0), as the
	// shader lights it too: only the vertices made from it take them.
	std::optional<Lighter> lighter;
	const u32 vertType = dec_->VertexType();
	const bool lmode = gstate.isUsingSecondaryColor();
	const Vec4f materialAmbientRGBA = Vec4f::FromRGBA(gstate.getMaterialAmbientRGBA());
	VertexReader reader(verts, fmt, vertType);
	if (fmt.cpuLit) {
		lighter.emplace(vertType);
		cpuLitDone_.assign(origVerts, 0);
		if (cpuLitColors_.size() < origVerts * 2) {
			cpuLitColors_.resize(origVerts * 2);
		}
		// Filled when clipping makes a vertex. Until then, the draw uses a stream of zeroes.
		cpuLitStream_.clear();
	}
	auto lightVertex = [&](int v) {
		if (cpuLitDone_[v]) {
			return;
		}
		cpuLitDone_[v] = 1;
		reader.Goto(v);
		float pos[3];
		reader.ReadPosNonThrough(pos);
		Vec4f unlitColor;
		if (reader.hasColor0()) {
			reader.ReadColor0(unlitColor.AsArray());
		} else {
			unlitColor = materialAmbientRGBA;
		}
		Vec3f worldnormal(0, 0, 1);
		if (reader.hasNormal()) {
			Vec3f normal;
			reader.ReadNrm(normal.AsArray());
			if (gstate.areNormalsReversed()) {
				normal = -normal;
			}
			Norm3ByMatrix43(worldnormal.AsArray(), normal.AsArray(), gstate.worldMatrix);
			worldnormal = worldnormal.NormalizedOr001(cpu_info.bSSE4_1);
		}
		float worldpos[3];
		Vec3ByMatrix43(worldpos, pos, gstate.worldMatrix);

		Vec4f c0, c1;
		lighter->Light(c0.AsArray(), c1.AsArray(), unlitColor.AsArray(), Vec3f(worldpos[0], worldpos[1], worldpos[2]), worldnormal);
		if (!lmode) {
			c0 = Vec4f(c0.r() + c1.r(), c0.g() + c1.g(), c0.b() + c1.b(), c0.a());
		}
		cpuLitColors_[v * 2] = c0.ToRGBA();
		cpuLitColors_[v * 2 + 1] = lmode ? (c1.ToRGBA() & 0x00FFFFFF) : 0;
	};

	auto makeVertex = [&](int in, int outside, float t) {
		u8 *dst = verts + vertCount * stride;
		const u8 *a = verts + in * stride;
		const u8 *b = verts + outside * stride;
		if (fmt.cpuLit) {
			lightVertex(in);
			lightVertex(outside);
			if (cpuLitStream_.empty()) {
				// The original vertices keep the shader's lighting.
				cpuLitStream_.assign(origVerts * 8, 0);
			}
			_dbg_assert_(cpuLitStream_.size() == vertCount * 8);
			cpuLitStream_.resize((vertCount + 1) * 8);
			u8 *litDst = &cpuLitStream_[vertCount * 8];
			LerpComponent(DEC_U8_4, litDst, (const u8 *)&cpuLitColors_[in * 2], (const u8 *)&cpuLitColors_[outside * 2], t);
			LerpComponent(DEC_U8_4, litDst + 4, (const u8 *)&cpuLitColors_[in * 2 + 1], (const u8 *)&cpuLitColors_[outside * 2 + 1], t);
			// Use them.
			litDst[7] = 255;
		}
		LerpComponent(DEC_FLOAT_3, dst + fmt.posoff, a + fmt.posoff, b + fmt.posoff, t);
		if (fmt.nrmfmt) LerpComponent(fmt.nrmfmt, dst + fmt.nrmoff, a + fmt.nrmoff, b + fmt.nrmoff, t);
		if (fmt.uvfmt) LerpComponent(fmt.uvfmt, dst + fmt.uvoff, a + fmt.uvoff, b + fmt.uvoff, t);
		if (fmt.c0fmt) LerpComponent(fmt.c0fmt, dst + fmt.c0off, a + fmt.c0off, b + fmt.c0off, t);
		if (fmt.c1fmt) LerpComponent(fmt.c1fmt, dst + fmt.c1off, a + fmt.c1off, b + fmt.c1off, t);
		if (fmt.w0fmt) LerpComponent(fmt.w0fmt, dst + fmt.w0off, a + fmt.w0off, b + fmt.w0off, t);
		if (fmt.w1fmt) LerpComponent(fmt.w1fmt, dst + fmt.w1off, a + fmt.w1off, b + fmt.w1off, t);
		return (u16)vertCount++;
	};

	// A triangle becomes at most two.
	if ((int)clipInds_.size() < indexCount * 2) {
		clipInds_.resize(indexCount * 2);
	}
	u16 *out = clipInds_.data();
	int numOut = 0;
	auto emit = [&](const u16 *tri) {
		out[numOut++] = tri[0];
		out[numOut++] = tri[1];
		out[numOut++] = tri[2];
	};
	const u8 *vertClass = vertClass_.data();
	for (int i = 0; i + 3 <= indexCount; i += 3) {
		const u16 *tri = inds + i;
		const u8 c0 = vertClass[tri[0]], c1 = vertClass[tri[1]], c2 = vertClass[tri[2]];
		if (c0 & c1 & c2) {
			// All behind the near plane, or all beyond the far one.
			continue;
		}
		if (!((c0 | c1 | c2) & VERT_BEHIND_NEAR)) {
			emit(tri);
			continue;
		}
		const bool outside[3] = { (c0 & VERT_BEHIND_NEAR) != 0, (c1 & VERT_BEHIND_NEAR) != 0, (c2 & VERT_BEHIND_NEAR) != 0 };
		const int numOutside = (int)outside[0] + (int)outside[1] + (int)outside[2];

		// The two vertices clipping makes, each from an inside vertex to an outside one.
		int from[2], to[2];
		if (numOutside == 1) {
			const int o = outside[0] ? 0 : (outside[1] ? 1 : 2);
			const int p = reversed ? (o + 1) % 3 : (o + 2) % 3;
			const int n = reversed ? (o + 2) % 3 : (o + 1) % 3;
			from[0] = tri[p]; to[0] = tri[o];
			from[1] = tri[n]; to[1] = tri[o];
		} else {
			const int in = !outside[0] ? 0 : (!outside[1] ? 1 : 2);
			from[0] = tri[in]; to[0] = tri[(in + 1) % 3];
			from[1] = tri[in]; to[1] = tri[(in + 2) % 3];
		}
		float t[2];
		bool madeOutside[2];
		for (int j = 0; j < 2; j++) {
			t[j] = NearPlaneT(&clipPos_[from[j] * 4], &clipPos_[to[j] * 4]);
			madeOutside[j] = rangeCulling && NearPointOutsideRange(&clipPos_[from[j] * 4], &clipPos_[to[j] * 4], t[j]);
		}

		if (!canClip || vertCount + 2 > maxVerts) {
			// Leave the clipping to the GPU, culling only if every part would be culled: b is in both halves.
			if (!(numOutside == 1 ? madeOutside[1] : (madeOutside[0] || madeOutside[1]))) {
				emit(tri);
			}
			continue;
		}

		if (numOutside == 2) {
			if (madeOutside[0] || madeOutside[1]) {
				continue;
			}
			const u16 a = makeVertex(from[0], to[0], t[0]);
			const u16 b = makeVertex(from[1], to[1], t[1]);
			const u16 sub[3] = { (u16)from[0], a, b };
			emit(sub);
			continue;
		}

		if (madeOutside[1]) {
			continue;
		}
		const u16 p = (u16)from[0], n = (u16)from[1];
		const u16 a = madeOutside[0] ? 0 : makeVertex(from[0], to[0], t[0]);
		const u16 b = makeVertex(from[1], to[1], t[1]);
		// Keep the winding we were given.
		const int first = reversed ? 2 : 1, second = reversed ? 1 : 2;
		if (!madeOutside[0]) {
			u16 sub[3];
			sub[0] = p; sub[first] = a; sub[second] = b;
			emit(sub);
		}
		u16 sub[3];
		sub[0] = p; sub[first] = b; sub[second] = n;
		emit(sub);
	}

	numOut = std::min(numOut, maxInds - maxInds % 3);
	memcpy(inds, out, numOut * sizeof(u16));
	*numVerts = vertCount;
	return numOut;
}



// Takes a list of consecutive PRIM opcodes, and extends the current draw call to include them.
// This is just a performance optimization. NOTE: This isn't compatible with really accurate culling,
// unless we refactor things a bit.
int DrawEngineCommon::ExtendNonIndexedPrim(const uint32_t *cmd, const uint32_t *stall, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead, bool isTriangle, ClipInfoFlags clipInfoFlags, int maxVerts) {
	if (clipInfoFlags & ClipInfoFlags::Valid) {
		clipInfoFlags_ |= clipInfoFlags;
	}

	const uint32_t *start = cmd;
	int prevDrawVerts = numDrawVerts_ - 1;
	DeferredVerts &dv = drawVerts_[prevDrawVerts];
	int offset = dv.vertexCount;

	_dbg_assert_(numDrawInds_ <= MAX_DEFERRED_DRAW_INDS);  // if it's equal, the check below will take care of it before any action is taken.
	_dbg_assert_(numDrawVerts_ > 0);

	if (!clockwise) {
		anyCCWOrIndexed_ = true;
	}
	int seenPrims = 0;
	int numDrawInds = numDrawInds_;
	while (cmd != stall) {
		uint32_t data = *cmd;
		if ((data & 0xFFF80000) != 0x04000000) {
			break;
		}
		GEPrimitiveType newPrim = static_cast<GEPrimitiveType>((data >> 16) & 7);
		if (IsTrianglePrim(newPrim) != isTriangle)
			break;
		int vertexCount = data & 0xFFFF;
		const int expanded = ExpandedVertexCount(newPrim, vertexCount);
		if (numDrawInds >= MAX_DEFERRED_DRAW_INDS || vertexCountInDrawCalls_ + offset + vertexCount > VERTEX_BUFFER_MAX || numVertsToDecode_ + (offset - dv.vertexCount) + vertexCount > VERTEX_BUFFER_MAX || expandedVertsInDrawCalls_ + expanded > VERTEX_BUFFER_MAX) {
			break;
		}
		// The vertices would run off the end of memory. Execute_Prim drops the draw.
		if ((int)(offset - dv.vertexCount) + vertexCount > maxVerts) {
			break;
		}
		expandedVertsInDrawCalls_ += expanded;
		DeferredInds &di = drawInds_[numDrawInds++];
		di.indexType = 0;
		di.prim = newPrim;
		seenPrims |= (1 << newPrim);
		di.clockwise = clockwise;
		di.vertexCount = vertexCount;
		di.vertDecodeIndex = prevDrawVerts;
		di.offset = offset;
		offset += vertexCount;
		cmd++;
	}
	numDrawInds_ = numDrawInds;
	seenPrims_ |= seenPrims;

	int totalCount = offset - dv.vertexCount;
	dv.vertexCount = offset;
	dv.indexUpperBound = dv.vertexCount - 1;
	vertexCountInDrawCalls_ += totalCount;
	numVertsToDecode_ += totalCount;
	*bytesRead = totalCount * dec->VertexSize();
	return cmd - start;
}

void DrawEngineCommon::SkipPrim(GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim)) {
		Flush();
	}

	// This isn't exactly right, if we flushed, since prims can straddle previous calls.
	// But it generally works for common usage.
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		// Has to be set to something, let's assume POINTS (0) if no previous.
		if (prevPrim_ == GE_PRIM_INVALID)
			prevPrim_ = GE_PRIM_POINTS;
		prim = prevPrim_;
	} else {
		prevPrim_ = prim;
	}

	*bytesRead = vertexCount * dec->VertexSize();
}

// vertTypeID is the vertex type but with the UVGen mode smashed into the top bits.
bool DrawEngineCommon::SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead, ClipInfoFlags clipInfoFlags) {
	const GEPrimitiveType realPrim = prim != GE_PRIM_KEEP_PREVIOUS ? prim : (prevPrim_ == GE_PRIM_INVALID ? GE_PRIM_POINTS : prevPrim_);
	const int expanded = ExpandedVertexCount(realPrim, vertexCount);
	if (expanded > VERTEX_BUFFER_MAX) {
		// Software transform can't expand this many in one draw, so submit it in parts. Points, lines
		// and rectangles are independent of each other; consecutive parts of a line strip share a vertex.
		const bool strip = realPrim == GE_PRIM_LINE_STRIP;
		const int partSize = (realPrim == GE_PRIM_POINTS || strip) ? VERTEX_BUFFER_MAX / 4 : VERTEX_BUFFER_MAX / 2;
		static const int indexSizes[4] = { 0, 1, 2, 4 };
		const int indexSize = indexSizes[(vertTypeID & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT];
		bool any = false;
		for (int start = 0; start < vertexCount - (strip ? 1 : 0); start += partSize) {
			const int count = std::min(partSize + (strip ? 1 : 0), vertexCount - start);
			const void *partVerts = inds ? verts : (const void *)((const u8 *)verts + start * dec->VertexSize());
			const void *partInds = inds ? (const void *)((const u8 *)inds + start * indexSize) : nullptr;
			int partBytesRead = 0;
			any = SubmitPrim(partVerts, partInds, realPrim, count, dec, vertTypeID, clockwise, &partBytesRead, clipInfoFlags) || any;
		}
		*bytesRead = vertexCount * dec->VertexSize();
		return any;
	}

	// The index count doesn't bound how many vertices DecodeVerts will produce (the index range can be
	// sparse), so track that separately, as the growth of the range to decode.
	u16 lowerBound = 0;
	u16 upperBound = 0;
	int decodeGrowth = 0;
	if (vertexCount > 0) {
		GetIndexBounds(inds, vertexCount, vertTypeID, &lowerBound, &upperBound);
		decodeGrowth = upperBound - lowerBound + 1;
		if (CanExtendDecode(verts, inds, dec)) {
			const DeferredVerts &last = drawVerts_[numDrawVerts_ - 1];
			decodeGrowth = std::max(upperBound, last.indexUpperBound) - std::min(lowerBound, last.indexLowerBound) - (last.indexUpperBound - last.indexLowerBound);
		}
	}

	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawVerts_ >= MAX_DEFERRED_DRAW_VERTS || numDrawInds_ >= MAX_DEFERRED_DRAW_INDS || vertexCountInDrawCalls_ + vertexCount > VERTEX_BUFFER_MAX || numVertsToDecode_ + decodeGrowth > VERTEX_BUFFER_MAX || expandedVertsInDrawCalls_ + expanded > VERTEX_BUFFER_MAX) {
		Flush();
	}

	if (clipInfoFlags & ClipInfoFlags::Valid) {
		if (clipInfoFlags_ != clipInfoFlags) {
			Flush();
		}
		clipInfoFlags_ = clipInfoFlags;
	}

	_dbg_assert_(numDrawVerts_ < MAX_DEFERRED_DRAW_VERTS);
	_dbg_assert_(numDrawInds_ < MAX_DEFERRED_DRAW_INDS);

	// This isn't exactly right, if we flushed, since prims can straddle previous calls.
	// But it generally works for common usage.
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		// Has to be set to something, let's assume POINTS (0) if no previous.
		if (prevPrim_ == GE_PRIM_INVALID)
			prevPrim_ = GE_PRIM_POINTS;
		prim = prevPrim_;
	} else {
		prevPrim_ = prim;
	}

	// If vtype has changed, setup the vertex decoder. Don't need to nullcheck dec_ since we set lastVType_ to an invalid value whenever we null it.
	if (vertTypeID != lastVType_) {
		dec_ = dec;
		_dbg_assert_(dec->VertexType() == vertTypeID);
		lastVType_ = vertTypeID;
	} else {
		_dbg_assert_(dec_->VertexType() == lastVType_);
	}

	*bytesRead = vertexCount * dec_->VertexSize();

	// Check that we have enough vertices to form the requested primitive.
	if (vertexCount < 3) {
		if ((vertexCount < 2 && prim > 0) || (prim > GE_PRIM_LINE_STRIP && prim != GE_PRIM_RECTANGLES)) {
			return false;
		}
		if (vertexCount <= 0) {
			// Unfortunately we need to do this check somewhere since GetIndexBounds doesn't handle zero-length arrays.
			return false;
		}
	} else if (prim == GE_PRIM_TRIANGLES) {
		// Make sure the vertex count is divisible by 3, round down. See issue #7503
		const int rem = vertexCount % 3;
		if (rem != 0) {
			vertexCount -= rem;
			GetIndexBounds(inds, vertexCount, vertTypeID, &lowerBound, &upperBound);
		}
	}

	bool applySkin = dec_->skinInDecode;

	DeferredInds &di = drawInds_[numDrawInds_++];
	_dbg_assert_(numDrawInds_ <= MAX_DEFERRED_DRAW_INDS);

	di.inds = inds;
	int indexType = (vertTypeID & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
	if (indexType) {
		anyCCWOrIndexed_ = true;
	}
	di.indexType = indexType;
	di.prim = prim;
	di.clockwise = clockwise;
	if (!clockwise) {
		anyCCWOrIndexed_ = true;
	}
	di.vertexCount = vertexCount;
	const int numDrawVerts = numDrawVerts_;
	di.vertDecodeIndex = numDrawVerts;
	di.offset = 0;

	_dbg_assert_(numDrawVerts <= MAX_DEFERRED_DRAW_VERTS);

	if (CanExtendDecode(verts, inds, dec_)) {
		// Same vertex pointer as a previous un-decoded draw call - let's just extend the decode!
		di.vertDecodeIndex = numDrawVerts - 1;
		DeferredVerts &dv = drawVerts_[numDrawVerts - 1];
		const int oldCount = dv.indexUpperBound - dv.indexLowerBound + 1;
		if (lowerBound < dv.indexLowerBound)
			dv.indexLowerBound = lowerBound;
		if (upperBound > dv.indexUpperBound)
			dv.indexUpperBound = upperBound;
		numVertsToDecode_ += dv.indexUpperBound - dv.indexLowerBound + 1 - oldCount;
	} else {
		// Record a new draw, and a new index gen.
		DeferredVerts &dv = drawVerts_[numDrawVerts];
		numDrawVerts_ = numDrawVerts + 1;  // Increment the uncached variable
		dv.verts = verts;
		dv.vertexCount = vertexCount;
		dv.uvScale = LoadUVScaleOffset(gstate);
		dv.indexLowerBound = lowerBound;
		dv.indexUpperBound = upperBound;
		dv.predecoded = false;
		numVertsToDecode_ += upperBound - lowerBound + 1;
	}

	vertexCountInDrawCalls_ += vertexCount;
	expandedVertsInDrawCalls_ += ExpandedVertexCount(prim, vertexCount);
	seenPrims_ |= (1 << prim);

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// This prevents issues with consecutive self-renders in Ridge Racer.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		Flush();
	}
	return true;
}

void DrawEngineCommon::DecodeVerts(const VertexDecoder *dec, u8 *dest) {
	const int numDrawVerts = numDrawVerts_;
	if (!numDrawVerts) {
		return;
	}
	// Note that this should be able to continue a partial decode - we don't necessarily start from zero here (although we do most of the time).
	int i = decodeVertsCounter_;
	const int stride = (int)dec->GetDecVtxFmt().stride;
	int numDecodedVerts = numDecodedVerts_;  // Move to a local for better codegen.
	for (; i < numDrawVerts; i++) {
		const DeferredVerts &dv = drawVerts_[i];

		const int indexLowerBound = dv.indexLowerBound;
		drawVertexOffsets_[i] = numDecodedVerts - indexLowerBound;
		const int indexUpperBound = dv.indexUpperBound;
		const int count = indexUpperBound - indexLowerBound + 1;
		if (count + numDecodedVerts > VERTEX_BUFFER_MAX) {
			// SubmitPrim flushes before this can happen.
			_dbg_assert_(false);
			break;
		}

		u8 *decodedPos = dest + numDecodedVerts * stride;
		if (dv.predecoded) {
			const u8 *startPos = (const u8 *)dv.verts + indexLowerBound * stride;
			// SubmitCurve puts it in place in decoded_ when it can, but a backend may decode elsewhere.
			if (startPos != decodedPos) {
				memcpy(decodedPos, startPos, count * stride);
			}
		} else {
			// Decode the verts (and at the same time apply morphing/skinning). Simple.
			const u8 *startPos = (const u8 *)dv.verts + indexLowerBound * dec->VertexSize();
			dec->DecodeVerts(decodedPos, startPos, &dv.uvScale, count);
		}
		numDecodedVerts += count;
	}
	numDecodedVerts_ = numDecodedVerts;
	decodeVertsCounter_ = i;
}

int DrawEngineCommon::DecodeInds() {
	// Note that this should be able to continue a partial decode - we don't necessarily start from zero here (although we do most of the time).

	int i = decodeIndsCounter_;
	for (; i < numDrawInds_; i++) {
		const DeferredInds &di = drawInds_[i];

		const int indexOffset = drawVertexOffsets_[di.vertDecodeIndex] + di.offset;
		const bool clockwise = di.clockwise;
		// We've already collapsed subsequent draws with the same vertex pointer, so no tricky logic here anymore.
		// 2. Loop through the drawcalls, translating indices as we go.
		switch (di.indexType) {
		case GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT:
			indexGen.AddPrim(di.prim, di.vertexCount, indexOffset, clockwise);
			break;
		case GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT:
			indexGen.TranslatePrim(di.prim, di.vertexCount, (const u8 *)di.inds, indexOffset, clockwise);
			break;
		case GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT:
			indexGen.TranslatePrim(di.prim, di.vertexCount, (const u16_le *)di.inds, indexOffset, clockwise);
			break;
		case GE_VTYPE_IDX_32BIT >> GE_VTYPE_IDX_SHIFT:
			indexGen.TranslatePrim(di.prim, di.vertexCount, (const u32_le *)di.inds, indexOffset, clockwise);
			break;
		}
	}
	decodeIndsCounter_ = i;

	return indexGen.VertexCount();
}

bool DrawEngineCommon::CanUseHardwareTransform(int prim) const {
	if (!useHWTransform_)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP;
}

bool DrawEngineCommon::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	if (!decJitCache_ || !decJitCache_->IsInSpace(ptr)) {
		return false;
	}

	// Loop through all the decoders and see if we have a match.
	VertexDecoder *found = nullptr;
	u32 foundKey;

	decoderMap_.Iterate([&](u32 key, VertexDecoder *value) {
		if (!found) {
			if (value->IsInSpace(ptr)) {
				foundKey = key;
				found = value;
			}
		}
	});

	if (found) {
		char temp[256];
		found->ToString(temp, sizeof(temp), false);
		name = temp;
		snprintf(temp, sizeof(temp), "_%08X", foundKey);
		name += temp;
		return true;
	} else {
		return false;
	}
}

enum {
	DEPTH_TRANSFORMED_MAX_VERTS = VERTEX_BUFFER_MAX,
	DEPTH_INDEXBUFFER_MAX_INDICES = DEPTH_TRANSFORMED_MAX_VERTS * 3,
};

// We process vertices for depth rendering in several stages:
// First, we transform and collect vertices into a DepthBatch (4-vectors, xyzw).
// Then, we group and cull the vertices into four-triangle groups, which are placed in
// the worker's screen vertices, with x, y and z separated into different part of the array.
// (Alternatively, if drawing rectangles, they're just added linearly).
// After that, the worker sends these groups out for SIMD setup and rasterization.
void DrawEngineCommon::InitDepthRaster() {
	switch ((DepthRasterMode)g_Config.iDepthRasterMode) {
	case DepthRasterMode::DEFAULT:
	case DepthRasterMode::LOW_QUALITY:
		useDepthRaster_ = PSP_CoreParameter().compat.flags().SoftwareRasterDepth;
		break;
	case DepthRasterMode::FORCE_ON:
		useDepthRaster_ = true;
		break;
	case DepthRasterMode::OFF:
		useDepthRaster_ = false;
	}

	if (useDepthRaster_) {
		depthWorker_ = new DepthRasterWorker(DEPTH_TRANSFORMED_MAX_VERTS, DEPTH_INDEXBUFFER_MAX_INDICES);
	}
}

void DrawEngineCommon::ShutdownDepthRaster() {
	delete depthWorker_;
	depthWorker_ = nullptr;
}

Mat4F32 ComputeFinalProjMatrix() {
	const float viewportTranslate[4] = {
		gstate.getViewportXCenter() - gstate.getOffsetX(),
		gstate.getViewportYCenter() - gstate.getOffsetY(),
		gstate.getViewportZCenter(),
		0.0f,
	};

	// TODO: Simply use Mat4F32 m(gstate_c.worldviewproj);
	Mat4F32 wv = Mul4x3By4x4(Mat4x3F32(gstate.worldMatrix), Mat4F32::Load4x3(gstate.viewMatrix));
	Mat4F32 m = Mul4x4By4x4(wv, Mat4F32(gstate.projMatrix));
	// NOTE: Applying the translation actually works pre-divide, since W is also affected.
	Vec4F32 scale = Vec4F32::LoadF24x3_One(&gstate.viewportxscale);
	Vec4F32 translate = Vec4F32::Load(viewportTranslate);
	TranslateAndScaleInplace(m, scale, translate);
	return m;
}

// numDecoded is how many vertices this draw will write to the batch, which is not the
// same thing as vertexCount (the number of indices) - an indexed draw can decode far more
// vertices than it has indices, or far fewer.
bool DrawEngineCommon::CalculateDepthDraw(DepthDraw *draw, GEPrimitiveType prim, int vertexCount, int numDecoded) {
	switch (prim) {
	case GE_PRIM_INVALID:
	case GE_PRIM_KEEP_PREVIOUS:
	case GE_PRIM_LINES:
	case GE_PRIM_LINE_STRIP:
	case GE_PRIM_POINTS:
		return false;
	default:
		break;
	}

	// Ignore some useless compare modes.
	switch (gstate.getDepthTestFunction()) {
	case GE_COMP_ALWAYS:
		draw->compareMode = ZCompareMode::Always;
		break;
	case GE_COMP_LEQUAL:
	case GE_COMP_LESS:
		draw->compareMode = ZCompareMode::Less;
		break;
	case GE_COMP_GEQUAL:
	case GE_COMP_GREATER:
		draw->compareMode = ZCompareMode::Greater;  // Most common
		break;
	case GE_COMP_NEVER:
	case GE_COMP_EQUAL:
		// These will never have a useful effect in Z-only raster.
		[[fallthrough]];
	case GE_COMP_NOTEQUAL:
		// This is highly unusual, let's just ignore it.
		[[fallthrough]];
	default:
		return false;
	}
	if (gstate.isModeClear()) {
		if (!gstate.isClearModeDepthMask()) {
			return false;
		}
		draw->compareMode = ZCompareMode::Always;
	} else {
		// These should have been caught earlier.
		_dbg_assert_(gstate.isDepthTestEnabled());
		_dbg_assert_(gstate.isDepthWriteEnabled());
	}

	if (numDecoded > DEPTH_TRANSFORMED_MAX_VERTS || vertexCount > DEPTH_INDEXBUFFER_MAX_INDICES) {
		return false;
	}
	const DepthBatch *batch = &depthWorker_->Current();
	if (batch->vertexCount + numDecoded > DEPTH_TRANSFORMED_MAX_VERTS || batch->indexCount + vertexCount > DEPTH_INDEXBUFFER_MAX_INDICES) {
		// Full, start on another.
		depthWorker_->Submit();
		batch = &depthWorker_->Current();
	}

	draw->depthAddr = gstate.getDepthBufRawAddress() | 0x04000000;
	draw->depthStride = gstate.DepthBufStride();
	depthWorker_->Reserve(numDecoded, vertexCount);
	draw->vertexOffset = batch->vertexCount;
	draw->indexOffset = batch->indexCount;
	draw->vertexCount = vertexCount;
	draw->cullEnabled = gstate.isCullEnabled();
	draw->cullMode = gstate.getCullMode();
	draw->prim = prim;
	draw->scissor.x1 = gstate.getScissorX1();
	draw->scissor.y1 = gstate.getScissorY1();
	// The drawing region clips too, like in SoftGPU. Resistance: Retribution's scissor ends at 480,272, one
	// past its region, and depth written to row 272 lands on the textures placed after the depth buffer.
	draw->scissor.x2 = std::min(gstate.getScissorX2(), gstate.getRegionX2());
	draw->scissor.y2 = std::min(gstate.getScissorY2(), gstate.getRegionY2());
	return true;
}

void DrawEngineCommon::DepthRasterSubmitRaw(GEPrimitiveType prim, const VertexDecoder *dec, uint32_t vertTypeID, int vertexCount) {
	if (!gstate.isModeClear() && (!gstate.isDepthTestEnabled() || !gstate.isDepthWriteEnabled())) {
		return;
	}

	if (vertTypeID & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) {
		return;
	}

	_dbg_assert_(prim != GE_PRIM_RECTANGLES);

	float worldviewproj[16];
	ComputeFinalProjMatrix().Store(worldviewproj);

	// How many vertices the decode loop below will write.
	int willDecode = 0;
	for (int i = 0; i < numDrawVerts_; i++) {
		willDecode += drawVerts_[i].indexUpperBound + 1 - drawVerts_[i].indexLowerBound;
	}

	DepthDraw draw;
	if (!CalculateDepthDraw(&draw, prim, vertexCount, willDecode)) {
		return;
	}
	DepthBatch &batch = depthWorker_->Current();

	TimeCollector collectStat(&gpuStats.perFrame.msPrepareDepth, g_coreCollectDebugStats);

	// Decode.
	int numDecoded = 0;
	for (int i = 0; i < numDrawVerts_; i++) {
		const DeferredVerts &dv = drawVerts_[i];
		if (draw.vertexOffset + numDecoded + (dv.indexUpperBound + 1 - dv.indexLowerBound) > DEPTH_TRANSFORMED_MAX_VERTS) {
			// Hit our limit! Stop decoding in this draw.
			// We should have already broken out in CalculateDepthDraw.
			break;
		}
		// Decode the verts (and at the same time apply morphing/skinning). Simple.
		DecodeAndTransformForDepthRaster(batch.transformed + (draw.vertexOffset + numDecoded) * 4, worldviewproj, dv.verts, dv.indexLowerBound, dv.indexUpperBound, dec, vertTypeID);
		numDecoded += dv.indexUpperBound - dv.indexLowerBound + 1;
	}

	// Copy indices.
	memcpy(batch.indices + draw.indexOffset, decIndex_, sizeof(uint16_t) * vertexCount);

	CommitDepthDraw(draw, vertexCount, numDecoded);
}

void DrawEngineCommon::DepthRasterPredecoded(GEPrimitiveType prim, const void *inVerts, int numDecoded, const VertexDecoder *dec, int vertexCount, const DecVtxFormat *fmt) {
	if (!gstate.isModeClear() && (!gstate.isDepthTestEnabled() || !gstate.isDepthWriteEnabled())) {
		return;
	}

	DepthDraw draw;
	if (!CalculateDepthDraw(&draw, prim, vertexCount, numDecoded)) {
		return;
	}
	DepthBatch &batch = depthWorker_->Current();

	TimeCollector collectStat(&gpuStats.perFrame.msPrepareDepth, g_coreCollectDebugStats);

	// Make sure these have already been indexed away.
	_dbg_assert_(prim != GE_PRIM_TRIANGLE_STRIP && prim != GE_PRIM_TRIANGLE_FAN);

	if (dec->throughmode) {
		ConvertPredecodedThroughForDepthRaster(batch.transformed + 4 * draw.vertexOffset, decoded_, dec, numDecoded);
	} else {
		if (dec->VertexType() & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) {
			return;
		}
		float worldviewproj[16];
		ComputeFinalProjMatrix().Store(worldviewproj);
		TransformPredecodedForDepthRaster(batch.transformed + 4 * draw.vertexOffset, worldviewproj, decoded_, fmt ? *fmt : dec->GetDecVtxFmt(), numDecoded);
	}

	// Copy indices.
	memcpy(batch.indices + draw.indexOffset, decIndex_, sizeof(uint16_t) * vertexCount);

	CommitDepthDraw(draw, vertexCount, numDecoded);
}

void DrawEngineCommon::CommitDepthDraw(const DepthDraw &draw, int vertexCount, int numDecoded) {
	DepthBatch &batch = depthWorker_->Current();
	if (batch.draws.empty()) {
		batch.lowQ = g_Config.iDepthRasterMode == (int)DepthRasterMode::LOW_QUALITY;
		if (rasterTimeStart_ == 0.0) {
			rasterTimeStart_ = time_now_d();
		}
	}
	batch.indexCount += vertexCount;
	batch.vertexCount += numDecoded;
	batch.draws.push_back(draw);
}

void DrawEngineCommon::SubmitQueuedDepth() {
	if (depthWorker_) {
		depthWorker_->Submit();
	}
}

void DrawEngineCommon::FlushQueuedDepth() {
	if (!depthWorker_) {
		return;
	}
	depthWorker_->Wait();
	if (rasterTimeStart_ != 0.0) {
		gpuStats.perFrame.msRasterTimeAvailable += time_now_d() - rasterTimeStart_;
		rasterTimeStart_ = 0.0;
	}
	depthWorker_->TakeTimes(&gpuStats.perFrame.msCullDepth, &gpuStats.perFrame.msRasterizeDepth);
}

void DrawEngineCommon::WaitForSubmittedDepth() {
	if (depthWorker_) {
		depthWorker_->WaitForSubmitted();
	}
}

void DrawEngineCommon::WaitForDepthWrites(u32 addr, u32 size) {
	if (depthWorker_) {
		depthWorker_->WaitIfWriting(addr, size);
	}
}
