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
#include "GPU/GPUStateSIMDUtil.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DepthRaster.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex),
};

DrawEngineCommon::DrawEngineCommon() : decoderMap_(32) {
	if (g_Config.bVertexDecoderJit && (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR)) {
		decJitCache_ = new VertexDecoderJitCache();
	}
	transformed_ = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	transformedExpanded_ = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decoded_ = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex_ = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	_dbg_assert_(transformed_);
	_dbg_assert_(transformedExpanded_);
	_dbg_assert_(decoded_);
	_dbg_assert_(decIndex_);

	indexGen.Setup(decIndex_);

	InitDepthRaster();
}

DrawEngineCommon::~DrawEngineCommon() {
	FreeMemoryPages(decoded_, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex_, DECODED_INDEX_BUFFER_SIZE);
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
	uint32_t vertTypeID = GetVertTypeID(vtype, 0, applySkinInDecode_);

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
	// Grab temp buffer space from large offsets in decoded_. Not exactly safe for large draws.
	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (vertexCount > 1024 || gstate_c.Use(GPU_USE_VIRTUAL_REALITY)) {
		return true;
	}

	SimpleVertex *corners = (SimpleVertex *)(decoded_ + 65536 * 12);
	float *verts = (float *)(decoded_ + 65536 * 18);

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
		u8 *temp_buffer = decoded_ + 65536 * 24;

		if ((inds || (vertType & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)))) {
			// Need for Speed Carbon ends up on this path! With a single bone weight.

			u16 indexLowerBound = 0;
			u16 indexUpperBound = (u16)vertexCount - 1;

			if (vertexCount > 0 && inds) {
				GetIndexBounds(inds, vertexCount, vertType, &indexLowerBound, &indexUpperBound);
			}
			// TODO: Avoid normalization if just plain skinning.
			// Force software skinning.
			const u32 vertTypeID = GetVertTypeID(vertType, gstate.getUVGenMode(), true);
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
template<u32 posFmt, u32 idxFmt>
static bool TestBoundingBoxFast(const float *cullMatrix, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, ClipInfoFlags *clipInfoFlags) {
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
			u32 idx = ((u32 *)idata)[i];
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

	if (needFragmentMinMaxClipping() && (minProjZ < gstate.getDepthRangeMin() || maxProjZ > gstate.getDepthRangeMax())) {
		if (gstate_c.Use(GPU_USE_CLIP_DISTANCE)) {
			flags |= ClipInfoFlags::MinMaxZClip;
		} else {
			// Implement min/max in the fragment shader.
			flags |= ClipInfoFlags::MinMaxZDiscard;
		}
	}

	if (AnyCompareBitsSet(anyOutsideMaskZ) && (!gstate_c.viewportNearPlaneMatchesOutput || PSP_CoreParameter().compat.flags().CorrectCullAfterClip)) {
		// Some vertices were outside the Z clipping planes. Clip againt Z=-W in software (and do culling, too).
		// TODO: With a compat flag for Flatout/Sengoku, we'll be able to avoid this in many cases, unless
		// GPU_USE_CULL_DISTANCE is missing, in which case we need it for culling.
		flags |= ClipInfoFlags::SoftClipCull;
	}

	if (minProjZ == maxProjZ) {
		// Probably a 2D draw. Send it through software transform!
		flags |= ClipInfoFlags::FlatZ | ClipInfoFlags::SoftClipCull;
	}

	if (needFragmentDepthClamp() && (minProjZ < 0 || maxProjZ > 65535)) {
		if (gstate_c.Use(GPU_USE_DEPTH_CLAMP)) {
			flags |= ClipInfoFlags::DepthClamp;
		} else {
			flags |= ClipInfoFlags::DepthClampFragment;
		}
	}

	*clipInfoFlags = flags;
	return true;
}

bool DrawEngineCommon::TestBoundingBoxFast(const float *cullMatrix, const void *vdata, const void *idata, int vertexCount, const VertexDecoder *dec, u32 vertType, ClipInfoFlags *flags) {
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
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_NONE>(cullMatrix, vdata, nullptr, vertexCount, dec, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_8BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_8BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_16BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_16BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		default:
			break;
		}
		break;
	case GE_VTYPE_IDX_32BIT:
		switch (vertType & GE_VTYPE_POS_MASK) {
		case GE_VTYPE_POS_8BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_8BIT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_16BIT: return ::TestBoundingBoxFast<GE_VTYPE_POS_16BIT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
		case GE_VTYPE_POS_FLOAT: return ::TestBoundingBoxFast<GE_VTYPE_POS_FLOAT, GE_VTYPE_IDX_32BIT>(cullMatrix, vdata, idata, vertexCount, dec, flags);
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

int DrawEngineCommon::ComputeNumVertsToDecode() const {
	int sum = 0;
	for (int i = 0; i < numDrawVerts_; i++) {
		sum += drawVerts_[i].indexUpperBound + 1 - drawVerts_[i].indexLowerBound;
	}
	return sum;
}



// Takes a list of consecutive PRIM opcodes, and extends the current draw call to include them.
// This is just a performance optimization. NOTE: This isn't compatible with really accurate culling,
// unless we refactor things a bit.
int DrawEngineCommon::ExtendNonIndexedPrim(const uint32_t *cmd, const uint32_t *stall, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead, bool isTriangle, ClipInfoFlags clipInfoFlags) {
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
		if (numDrawInds >= MAX_DEFERRED_DRAW_INDS || vertexCountInDrawCalls_ + offset + vertexCount > VERTEX_BUFFER_MAX) {
			break;
		}
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
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawVerts_ >= MAX_DEFERRED_DRAW_VERTS || numDrawInds_ >= MAX_DEFERRED_DRAW_INDS || vertexCountInDrawCalls_ + vertexCount > VERTEX_BUFFER_MAX) {
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

	if (inds && numDrawVerts > decodeVertsCounter_ && drawVerts_[numDrawVerts - 1].verts == verts && !applySkin) {
		// Same vertex pointer as a previous un-decoded draw call - let's just extend the decode!
		di.vertDecodeIndex = numDrawVerts - 1;
		u16 lb;
		u16 ub;
		GetIndexBounds(inds, vertexCount, vertTypeID, &lb, &ub);
		DeferredVerts &dv = drawVerts_[numDrawVerts - 1];
		if (lb < dv.indexLowerBound)
			dv.indexLowerBound = lb;
		if (ub > dv.indexUpperBound)
			dv.indexUpperBound = ub;
	} else {
		// Record a new draw, and a new index gen.
		DeferredVerts &dv = drawVerts_[numDrawVerts];
		numDrawVerts_ = numDrawVerts + 1;  // Increment the uncached variable
		dv.verts = verts;
		dv.vertexCount = vertexCount;
		dv.uvScale = LoadUVScaleOffset(gstate);
		// Does handle the unindexed case.
		GetIndexBounds(inds, vertexCount, vertTypeID, &dv.indexLowerBound, &dv.indexUpperBound);
	}

	vertexCountInDrawCalls_ += vertexCount;
	seenPrims_ |= (1 << prim);

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// This prevents issues with consecutive self-renders in Ridge Racer.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		Flush();
	}
	return true;
}

void DrawEngineCommon::BeginFrame() {
	applySkinInDecode_ = g_Config.bSoftwareSkinning;
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
		if (count + numDecodedVerts >= VERTEX_BUFFER_MAX) {
			// Hit our limit! Stop decoding in this draw.
			break;
		}

		// Decode the verts (and at the same time apply morphing/skinning). Simple.
		const u8 *startPos = (const u8 *)dv.verts + indexLowerBound * dec->VertexSize();
		dec->DecodeVerts(dest + numDecodedVerts * stride, startPos, &dv.uvScale, count);
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
		found->ToString(temp, false);
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
	DEPTH_TRANSFORMED_BYTES = DEPTH_TRANSFORMED_MAX_VERTS * 4 * sizeof(float),
	DEPTH_SCREENVERTS_COMPONENT_COUNT = VERTEX_BUFFER_MAX,
	DEPTH_SCREENVERTS_COMPONENT_BYTES = DEPTH_SCREENVERTS_COMPONENT_COUNT * sizeof(int) + 384,
	DEPTH_SCREENVERTS_TOTAL_BYTES = DEPTH_SCREENVERTS_COMPONENT_BYTES * 3,
	DEPTH_INDEXBUFFER_BYTES = DEPTH_TRANSFORMED_MAX_VERTS * 3 * sizeof(uint16_t),  // hmmm
};

// We process vertices for depth rendering in several stages:
// First, we transform and collect vertices into depthTransformed_ (4-vectors, xyzw).
// Then, we group and cull the vertices into four-triangle groups, which are placed in
// depthScreenVerts_, with x, y and z separated into different part of the array.
// (Alternatively, if drawing rectangles, they're just added linearly).
// After that, we send these groups out for SIMD setup and rasterization.
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
		depthDraws_.reserve(256);
		depthTransformed_ = (float *)AllocateMemoryPages(DEPTH_TRANSFORMED_BYTES, MEM_PROT_READ | MEM_PROT_WRITE);
		depthScreenVerts_ = (int *)AllocateMemoryPages(DEPTH_SCREENVERTS_TOTAL_BYTES, MEM_PROT_READ | MEM_PROT_WRITE);
		depthIndices_ = (uint16_t *)AllocateMemoryPages(DEPTH_INDEXBUFFER_BYTES, MEM_PROT_READ | MEM_PROT_WRITE);
	}
}

void DrawEngineCommon::ShutdownDepthRaster() {
	if (depthTransformed_) {
		FreeMemoryPages(depthTransformed_, DEPTH_TRANSFORMED_BYTES);
	}
	if (depthScreenVerts_) {
		FreeMemoryPages(depthScreenVerts_, DEPTH_SCREENVERTS_TOTAL_BYTES);
	}
	if (depthIndices_) {
		FreeMemoryPages(depthIndices_, DEPTH_INDEXBUFFER_BYTES);
	}
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

bool DrawEngineCommon::CalculateDepthDraw(DepthDraw *draw, GEPrimitiveType prim, int vertexCount) {
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

	if (depthVertexCount_ + vertexCount >= DEPTH_TRANSFORMED_MAX_VERTS) {
		// Can't add more. We need to flush.
		return false;
	}

	draw->depthAddr = gstate.getDepthBufRawAddress() | 0x04000000;
	draw->depthStride = gstate.DepthBufStride();
	draw->vertexOffset = depthVertexCount_;
	draw->indexOffset = depthIndexCount_;
	draw->vertexCount = vertexCount;
	draw->cullEnabled = gstate.isCullEnabled();
	draw->cullMode = gstate.getCullMode();
	draw->prim = prim;
	draw->scissor.x1 = gstate.getScissorX1();
	draw->scissor.y1 = gstate.getScissorY1();
	draw->scissor.x2 = gstate.getScissorX2();
	draw->scissor.y2 = gstate.getScissorY2();
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

	DepthDraw draw;
	if (!CalculateDepthDraw(&draw, prim, vertexCount)) {
		return;
	}

	TimeCollector collectStat(&gpuStats.perFrame.msPrepareDepth, coreCollectDebugStats);

	// Decode.
	int numDecoded = 0;
	for (int i = 0; i < numDrawVerts_; i++) {
		const DeferredVerts &dv = drawVerts_[i];
		if (dv.indexUpperBound + 1 - dv.indexLowerBound + numDecoded >= DEPTH_TRANSFORMED_MAX_VERTS) {
			// Hit our limit! Stop decoding in this draw.
			// We should have already broken out in CalculateDepthDraw.
			break;
		}
		// Decode the verts (and at the same time apply morphing/skinning). Simple.
		DecodeAndTransformForDepthRaster(depthTransformed_ + (draw.vertexOffset + numDecoded) * 4, worldviewproj, dv.verts, dv.indexLowerBound, dv.indexUpperBound, dec, vertTypeID);
		numDecoded += dv.indexUpperBound - dv.indexLowerBound + 1;
	}

	// Copy indices.
	memcpy(depthIndices_ + draw.indexOffset, decIndex_, sizeof(uint16_t) * vertexCount);

	// Commit
	depthIndexCount_ += vertexCount;
	depthVertexCount_ += numDecoded;

	if (depthDraws_.empty()) {
		rasterTimeStart_ = time_now_d();
	}

	depthDraws_.push_back(draw);

	// FlushQueuedDepth();
}

void DrawEngineCommon::DepthRasterPredecoded(GEPrimitiveType prim, const void *inVerts, int numDecoded, const VertexDecoder *dec, int vertexCount) {
	if (!gstate.isModeClear() && (!gstate.isDepthTestEnabled() || !gstate.isDepthWriteEnabled())) {
		return;
	}

	DepthDraw draw;
	if (!CalculateDepthDraw(&draw, prim, vertexCount)) {
		return;
	}

	TimeCollector collectStat(&gpuStats.perFrame.msPrepareDepth, coreCollectDebugStats);

	// Make sure these have already been indexed away.
	_dbg_assert_(prim != GE_PRIM_TRIANGLE_STRIP && prim != GE_PRIM_TRIANGLE_FAN);

	if (dec->throughmode) {
		ConvertPredecodedThroughForDepthRaster(depthTransformed_ + 4 * draw.vertexOffset, decoded_, dec, numDecoded);
	} else {
		if (dec->VertexType() & (GE_VTYPE_WEIGHT_MASK | GE_VTYPE_MORPHCOUNT_MASK)) {
			return;
		}
		float worldviewproj[16];
		ComputeFinalProjMatrix().Store(worldviewproj);
		TransformPredecodedForDepthRaster(depthTransformed_ + 4 * draw.vertexOffset, worldviewproj, decoded_, dec, numDecoded);
	}

	// Copy indices.
	memcpy(depthIndices_ + draw.indexOffset, decIndex_, sizeof(uint16_t) * vertexCount);

	// Commit
	depthIndexCount_ += vertexCount;
	depthVertexCount_ += numDecoded;

	depthDraws_.push_back(draw);

	if (depthDraws_.empty()) {
		rasterTimeStart_ = time_now_d();
	}
	// FlushQueuedDepth();
}

void DrawEngineCommon::FlushQueuedDepth() {
	if (rasterTimeStart_ != 0.0) {
		gpuStats.perFrame.msRasterTimeAvailable += time_now_d() - rasterTimeStart_;
		rasterTimeStart_ = 0.0;
	}

	const bool collectStats = coreCollectDebugStats;
	const bool lowQ = g_Config.iDepthRasterMode == (int)DepthRasterMode::LOW_QUALITY;
	for (const auto &draw : depthDraws_) {
		int *tx = depthScreenVerts_;
		int *ty = depthScreenVerts_ + DEPTH_SCREENVERTS_COMPONENT_COUNT;
		float *tz = (float *)(depthScreenVerts_ + DEPTH_SCREENVERTS_COMPONENT_COUNT * 2);

		int outVertCount = 0;

		const float *vertices = depthTransformed_ + 4 * draw.vertexOffset;
		const uint16_t *indices = depthIndices_ + draw.indexOffset;

		DepthScissor tileScissor = draw.scissor.Tile(0, 1);

		{
			TimeCollector collectStat(&gpuStats.perFrame.msCullDepth, collectStats);
			switch (draw.prim) {
			case GE_PRIM_RECTANGLES:
				outVertCount = DepthRasterClipIndexedRectangles(tx, ty, tz, vertices, indices, draw, tileScissor);
				break;
			case GE_PRIM_TRIANGLES:
				outVertCount = DepthRasterClipIndexedTriangles(tx, ty, tz, vertices, indices, draw, tileScissor);
				break;
			default:
				_dbg_assert_(false);
				break;
			}
		}
		if (outVertCount > 0) {
			TimeCollector collectStat(&gpuStats.perFrame.msRasterizeDepth, collectStats);
			if (!Memory::IsValid4AlignedAddress(draw.depthAddr)) {
				continue;
			}
			u16 *depthPtr = (uint16_t *)Memory::GetPointerWriteUnchecked(draw.depthAddr);
			DepthRasterScreenVerts(depthPtr, draw.depthStride, tx, ty, tz, outVertCount, draw, tileScissor, lowQ);
		}
	}

	// Reset queue
	depthIndexCount_ = 0;
	depthVertexCount_ = 0;
	depthDraws_.clear();
}
