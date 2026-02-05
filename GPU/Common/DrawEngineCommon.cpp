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
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DepthRaster.h"
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
	useHWTessellation_ = UpdateUseHWTessellation(g_Config.bHardwareTessellation);
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
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
	}

	int bytesRead;
	uint32_t vertTypeID = GetVertTypeID(vtype, 0, applySkinInDecode_);

	bool clockwise = !gstate.isCullEnabled() || gstate.getCullMode() == cullMode;
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	SubmitPrim(&temp[0], nullptr, prim, vertexCount, dec, vertTypeID, clockwise, &bytesRead);
	Flush();

	if (!prevThrough) {
		gstate.vertType &= ~GE_VTYPE_THROUGH;
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
	}
}

// Gated by DIRTY_CULL_PLANES
void DrawEngineCommon::UpdatePlanes() {
	float view[16];
	float viewproj[16];
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(viewproj, view, gstate.projMatrix);

	// Next, we need to apply viewport, scissor, region, and even offset - but only for X/Y.
	// Note that the PSP does not clip against the viewport.
	const Vec2f baseOffset = Vec2f(gstate.getOffsetX(), gstate.getOffsetY());
	// Region1 (rate) is used as an X1/Y1 here, matching PSP behavior.
	minOffset_ = baseOffset + Vec2f(std::max(gstate.getRegionRateX() - 0x100, gstate.getScissorX1()), std::max(gstate.getRegionRateY() - 0x100, gstate.getScissorY1())) - Vec2f(1.0f, 1.0f);
	maxOffset_ = baseOffset + Vec2f(std::min(gstate.getRegionX2(), gstate.getScissorX2()), std::min(gstate.getRegionY2(), gstate.getScissorY2())) + Vec2f(1.0f, 1.0f);

	// Let's not handle these special cases in the fast culler.
	offsetOutsideEdge_ = maxOffset_.x >= 4096.0f || minOffset_.x < 1.0f || minOffset_.y < 1.0f || maxOffset_.y >= 4096.0f;

	// Now let's apply the viewport to our scissor/region + offset range.
	Vec2f inverseViewportScale = Vec2f(1.0f / gstate.getViewportXScale(), 1.0f / gstate.getViewportYScale());
	Vec2f minViewport = (minOffset_ - Vec2f(gstate.getViewportXCenter(), gstate.getViewportYCenter())) * inverseViewportScale;
	Vec2f maxViewport = (maxOffset_ - Vec2f(gstate.getViewportXCenter(), gstate.getViewportYCenter())) * inverseViewportScale;

	Vec2f viewportInvSize = Vec2f(1.0f / (maxViewport.x - minViewport.x), 1.0f / (maxViewport.y - minViewport.y));

	Lin::Matrix4x4 applyViewport{};
	// Scale to the viewport's size.
	applyViewport.xx = 2.0f * viewportInvSize.x;
	applyViewport.yy = 2.0f * viewportInvSize.y;
	applyViewport.zz = 1.0f;
	applyViewport.ww = 1.0f;
	// And offset to the viewport's centers.
	applyViewport.wx = -(maxViewport.x + minViewport.x) * viewportInvSize.x;
	applyViewport.wy = -(maxViewport.y + minViewport.y) * viewportInvSize.y;

	float mtx[16];
	Matrix4ByMatrix4(mtx, viewproj, applyViewport.m);
	// I'm sure there's some fairly optimized way to set these. If we make a version of Matrix4ByMatrix4
	// that returns a transpose, it looks like these will be more straightforward.
	planes_.Set(0, mtx[3] - mtx[0], mtx[7] - mtx[4], mtx[11] - mtx[8],  mtx[15] - mtx[12]);  // Right
	planes_.Set(1, mtx[3] + mtx[0], mtx[7] + mtx[4], mtx[11] + mtx[8],  mtx[15] + mtx[12]);  // Left
	planes_.Set(2, mtx[3] + mtx[1], mtx[7] + mtx[5], mtx[11] + mtx[9],  mtx[15] + mtx[13]);  // Bottom
	planes_.Set(3, mtx[3] - mtx[1], mtx[7] - mtx[5], mtx[11] - mtx[9],  mtx[15] - mtx[13]);  // Top
	planes_.Set(4, mtx[3] + mtx[2], mtx[7] + mtx[6], mtx[11] + mtx[10], mtx[15] + mtx[14]);  // Near
	planes_.Set(5, mtx[3] - mtx[2], mtx[7] - mtx[6], mtx[11] - mtx[10], mtx[15] - mtx[14]);  // Far
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
	if (vertexCount > 1024) {
		return true;
	}

	SimpleVertex *corners = (SimpleVertex *)(decoded_ + 65536 * 12);
	float *verts = (float *)(decoded_ + 65536 * 18);

	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY))
		return true;

	// Due to world matrix updates per "thing", this isn't quite as effective as it could be if we did world transform
	// in here as well. Though, it still does cut down on a lot of updates in Tekken 6.
	if (gstate_c.IsDirty(DIRTY_CULL_PLANES)) {
		UpdatePlanes();
		gpuStats.numPlaneUpdates++;
		gstate_c.Clean(DIRTY_CULL_PLANES);
	}

	// Try to skip NormalizeVertices if it's pure positions. No need to bother with a vertex decoder
	// and a large vertex format.
	if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_FLOAT && !inds) {
		memcpy(verts, vdata, sizeof(float) * 3 * vertexCount);
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
			u16 indexLowerBound = 0;
			u16 indexUpperBound = (u16)vertexCount - 1;

			if (vertexCount > 0 && inds) {
				GetIndexBounds(inds, vertexCount, vertType, &indexLowerBound, &indexUpperBound);
			}
			// TODO: Avoid normalization if just plain skinning.
			// Force software skinning.
			const u32 vertTypeID = GetVertTypeID(vertType, gstate.getUVGenMode(), true);
			::NormalizeVertices(corners, temp_buffer, (const u8 *)vdata, indexLowerBound, indexUpperBound, dec, vertType);
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

	// Pretransform the verts in-place so we don't have to do it inside the loop.
	// We do this differently in the fast version below since we skip the max/minOffset checks there
	// making it easier to get the whole thing ready for SIMD.
	for (int i = 0; i < vertexCount; i++) {
		float worldpos[3];
		Vec3ByMatrix43(worldpos, &verts[i * 3], gstate.worldMatrix);
		memcpy(&verts[i * 3], worldpos, 12);
	}

	// Note: near/far are not checked without clamp/clip enabled, so we skip those planes.
	int totalPlanes = gstate.isDepthClampEnabled() ? 6 : 4;
	for (int plane = 0; plane < totalPlanes; plane++) {
		int inside = 0;
		int out = 0;
		for (int i = 0; i < vertexCount; i++) {
			// Test against the frustum planes, and count.
			// TODO: We should test 4 vertices at a time using SIMD.
			// I guess could also test one vertex against 4 planes at a time, though a lot of waste at the common case of 6.
			const float *worldpos = verts + i * 3;
			float value = planes_.Test(plane, worldpos);
			if (value <= -FLT_EPSILON)  // Not sure why we use exactly this value. Probably '< 0' would do.
				out++;
			else
				inside++;
		}

		// No vertices inside this one plane? Don't need to draw.
		if (inside == 0) {
			// All out - but check for X and Y if the offset was near the cullbox edge.
			bool outsideEdge = false;
			switch (plane) {
			case 0: outsideEdge = maxOffset_.x >= 4096.0f; break;
			case 1: outsideEdge = minOffset_.x < 1.0f; break;
			case 2: outsideEdge = minOffset_.y < 1.0f; break;
			case 3: outsideEdge = maxOffset_.y >= 4096.0f; break;
			}

			// Only consider this outside if offset + scissor/region is fully inside the cullbox.
			if (!outsideEdge)
				return false;
		}

		// Any out. For testing that the planes are in the right locations.
		// if (out != 0) return false;
	}
	return true;
}

// NOTE: This doesn't handle through-mode, indexing, morph, or skinning.
// TODO: For high vertex counts, we should just take the min/max of all the verts, and test the resulting six cube
// corners. That way we can cull more draws quite cheaply.
// We could take the min/max during the regular vertex decode, and just skip the draw call if it's trivially culled.
// This would help games like Midnight Club (that one does a lot of out-of-bounds drawing) immensely.
bool DrawEngineCommon::TestBoundingBoxFast(const void *vdata, int vertexCount, const VertexDecoder *dec, u32 vertType) {
	SimpleVertex *corners = (SimpleVertex *)(decoded_ + 65536 * 12);
	float *verts = (float *)(decoded_ + 65536 * 18);

	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY))
		return true;

	// Due to world matrix updates per "thing", this isn't quite as effective as it could be if we did world transform
	// in here as well. Though, it still does cut down on a lot of updates in Tekken 6.
	if (gstate_c.IsDirty(DIRTY_CULL_PLANES)) {
		UpdatePlanes();
		gpuStats.numPlaneUpdates++;
		gstate_c.Clean(DIRTY_CULL_PLANES);
	}

	// Also let's just bail if offsetOutsideEdge_ is set, instead of handling the cases.
	// NOTE: This is written to in UpdatePlanes so can't check it before.
	if (offsetOutsideEdge_)
		return true;

	// Simple, most common case.
	int stride = dec->VertexSize();
	int offset = dec->posoff;
	int vertStride = 3;

	// TODO: Possibly do the plane tests directly against the source formats instead of converting.
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
	{
#if PPSSPP_ARCH(SSE2)
		__m128 scaleFactor = _mm_set1_ps(1.0f / 32768.0f);
		for (int i = 0; i < vertexCount; i++) {
			const s16 *data = ((const s16 *)((const s8 *)vdata + i * stride + offset));
			__m128i bits = _mm_loadl_epi64((const __m128i*)data);
			// Sign extension. Hacky without SSE4.
			bits = _mm_srai_epi32(_mm_unpacklo_epi16(bits, bits), 16);
			__m128 pos = _mm_mul_ps(_mm_cvtepi32_ps(bits), scaleFactor);
			_mm_storeu_ps(verts + i * 3, pos);  // TODO: use stride 4 to avoid clashing writes?
		}
#elif PPSSPP_ARCH(ARM_NEON)
		for (int i = 0; i < vertexCount; i++) {
			const s16 *dataPtr = ((const s16 *)((const s8 *)vdata + i * stride + offset));
			int32x4_t data = vmovl_s16(vld1_s16(dataPtr));
			float32x4_t pos = vcvtq_n_f32_s32(data, 15);  // >> 15 = division by 32768.0f
			vst1q_f32(verts + i * 3, pos);
		}
#else
		for (int i = 0; i < vertexCount; i++) {
			const s16 *data = ((const s16 *)((const s8 *)vdata + i * stride + offset));
			for (int j = 0; j < 3; j++) {
				verts[i * 3 + j] = data[j] * (1.0f / 32768.0f);
			}
		}
#endif
		break;
	}
	case GE_VTYPE_POS_FLOAT:
		// No need to copy in this case, we can just read directly from the source format with a stride.
		verts = (float *)((uint8_t *)vdata + offset);
		vertStride = stride / 4;
		break;
	}

	// We only check the 4 sides. Near/far won't likely make a huge difference.
	// We test one vertex against 4 planes to get some SIMD. Vertices need to be transformed to world space
	// for testing, don't want to re-do that, so we have to use that "pivot" of the data.
#if PPSSPP_ARCH(SSE2)
	const __m128 worldX = _mm_loadu_ps(gstate.worldMatrix);
	const __m128 worldY = _mm_loadu_ps(gstate.worldMatrix + 3);
	const __m128 worldZ = _mm_loadu_ps(gstate.worldMatrix + 6);
	const __m128 worldW = _mm_loadu_ps(gstate.worldMatrix + 9);
	const __m128 planeX = _mm_loadu_ps(planes_.x);
	const __m128 planeY = _mm_loadu_ps(planes_.y);
	const __m128 planeZ = _mm_loadu_ps(planes_.z);
	const __m128 planeW = _mm_loadu_ps(planes_.w);
	__m128 inside = _mm_set1_ps(0.0f);
	for (int i = 0; i < vertexCount; i++) {
		const float *pos = verts + i * vertStride;
		__m128 worldpos = _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(worldX, _mm_set1_ps(pos[0])),
				_mm_mul_ps(worldY, _mm_set1_ps(pos[1]))
			),
			_mm_add_ps(
				_mm_mul_ps(worldZ, _mm_set1_ps(pos[2])),
				worldW
			)
		);
		// OK, now we check it against the four planes.
		// This is really curiously similar to a matrix multiplication (well, it is one).
		__m128 posX = _mm_shuffle_ps(worldpos, worldpos, _MM_SHUFFLE(0, 0, 0, 0));
		__m128 posY = _mm_shuffle_ps(worldpos, worldpos, _MM_SHUFFLE(1, 1, 1, 1));
		__m128 posZ = _mm_shuffle_ps(worldpos, worldpos, _MM_SHUFFLE(2, 2, 2, 2));
		__m128 planeDist = _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(planeX, posX),
				_mm_mul_ps(planeY, posY)
			),
			_mm_add_ps(
				_mm_mul_ps(planeZ, posZ),
				planeW
			)
		);
		inside = _mm_or_ps(inside, _mm_cmpge_ps(planeDist, _mm_setzero_ps()));
	}
	// 0xF means that we found at least one vertex inside every one of the planes.
	// We don't bother with counts, though it wouldn't be hard if we had a use for them.
	return _mm_movemask_ps(inside) == 0xF;
#elif PPSSPP_ARCH(ARM_NEON)
	const float32x4_t worldX = vld1q_f32(gstate.worldMatrix);
	const float32x4_t worldY = vld1q_f32(gstate.worldMatrix + 3);
	const float32x4_t worldZ = vld1q_f32(gstate.worldMatrix + 6);
	const float32x4_t worldW = vld1q_f32(gstate.worldMatrix + 9);
	const float32x4_t planeX = vld1q_f32(planes_.x);
	const float32x4_t planeY = vld1q_f32(planes_.y);
	const float32x4_t planeZ = vld1q_f32(planes_.z);
	const float32x4_t planeW = vld1q_f32(planes_.w);
	uint32x4_t inside = vdupq_n_u32(0);
	for (int i = 0; i < vertexCount; i++) {
		const float *pos = verts + i * vertStride;
		float32x4_t objpos = vld1q_f32(pos);
		float32x4_t worldpos = vaddq_f32(
			vmlaq_laneq_f32(
				vmulq_laneq_f32(worldX, objpos, 0),
				worldY, objpos, 1),
			vmlaq_laneq_f32(worldW, worldZ, objpos, 2)
		);
		// OK, now we check it against the four planes.
		// This is really curiously similar to a matrix multiplication (well, it is one).
		float32x4_t planeDist = vaddq_f32(
			vmlaq_laneq_f32(
				vmulq_laneq_f32(planeX, worldpos, 0),
				planeY, worldpos, 1),
			vmlaq_laneq_f32(planeW, planeZ, worldpos, 2)
		);
		inside = vorrq_u32(inside, vcgezq_f32(planeDist));
	}
	uint64_t insideBits = vget_lane_u64(vreinterpret_u64_u16(vmovn_u32(inside)), 0);
	return ~insideBits == 0;  // InsideBits all ones means that we found at least one vertex inside every one of the planes. We don't bother with counts, though it wouldn't be hard.
#else
	int inside[4]{};
	for (int i = 0; i < vertexCount; i++) {
		const float *pos = verts + i * vertStride;
		float worldpos[3];
		Vec3ByMatrix43(worldpos, pos, gstate.worldMatrix);
		for (int plane = 0; plane < 4; plane++) {
			float value = planes_.Test(plane, worldpos);
			if (value >= 0.0f)
				inside[plane]++;
		}
	}

	for (int plane = 0; plane < 4; plane++) {
		if (inside[plane] == 0) {
			return false;
		}
	}
#endif
	return true;
}

// 2D bounding box test against scissor. No indexing yet.
// Only supports non-indexed draws with float positions.
bool DrawEngineCommon::TestBoundingBoxThrough(const void *vdata, int vertexCount, const VertexDecoder *dec, u32 vertType, int *bytesRead) {
	// Grab temp buffer space from large offsets in decoded_. Not exactly safe for large draws.
	if (vertexCount > 16) {
		return true;
	}

	// Although this may lead to drawing that shouldn't happen, the viewport is more complex on VR.
	// Let's always say objects are within bounds.
	if (gstate_c.Use(GPU_USE_VIRTUAL_REALITY))
		return true;

	const int stride = dec->VertexSize();
	const int posOffset = dec->posoff;

	*bytesRead = stride * vertexCount;

	bool allOutsideLeft = true;
	bool allOutsideTop = true;
	bool allOutsideRight = true;
	bool allOutsideBottom = true;
	const float left = gstate.getScissorX1();
	const float top = gstate.getScissorY1();
	const float right = gstate.getScissorX2();
	const float bottom = gstate.getScissorY2();

	switch (vertType & GE_VTYPE_POS_MASK) {
	case GE_VTYPE_POS_FLOAT:
	{
		// TODO: This can be SIMD'd, with some trickery.
		for (int i = 0; i < vertexCount; i++) {
			const float *pos = (const float*)((const u8 *)vdata + stride * i + posOffset);
			const float x = pos[0];
			const float y = pos[1];
			if (x >= left) {
				allOutsideLeft = false;
			}
			if (x <= right + 1) {
				allOutsideRight = false;
			}
			if (y >= top) {
				allOutsideTop = false;
			}
			if (y <= bottom + 1) {
				allOutsideBottom = false;
			}
		}
		if (allOutsideLeft || allOutsideTop || allOutsideRight || allOutsideBottom) {
			return false;
		}
		return true;
	}
	default:
		// Shouldn't end up here with the checks outside this function.
		_dbg_assert_(false);
		return true;
	}
}

void DrawEngineCommon::ApplyFramebufferRead(FBOTexState *fboTexState) {
	if (gstate_c.Use(GPU_USE_FRAMEBUFFER_FETCH)) {
		*fboTexState = FBO_TEX_READ_FRAMEBUFFER;
	} else {
		gpuStats.numCopiesForShaderBlend++;
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
// This is just a performance optimization.
int DrawEngineCommon::ExtendNonIndexedPrim(const uint32_t *cmd, const uint32_t *stall, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead, bool isTriangle) {
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

void DrawEngineCommon::SkipPrim(GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertTypeID, int *bytesRead) {
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
bool DrawEngineCommon::SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, const VertexDecoder *dec, u32 vertTypeID, bool clockwise, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawVerts_ >= MAX_DEFERRED_DRAW_VERTS || numDrawInds_ >= MAX_DEFERRED_DRAW_INDS || vertexCountInDrawCalls_ + vertexCount > VERTEX_BUFFER_MAX) {
		Flush();
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
		dv.uvScale = gstate_c.uv;
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

bool DrawEngineCommon::CanUseHardwareTessellation(GEPatchPrimType prim) const {
	if (useHWTessellation_) {
		return CanUseHardwareTransform(PatchPrimToPrim(prim));
	}
	return false;
}

void TessellationDataTransfer::CopyControlPoints(float *pos, float *tex, float *col, int posStride, int texStride, int colStride, const SimpleVertex *const *points, int size, u32 vertType) {
	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasTexCoord = (vertType & GE_VTYPE_TC_MASK) != 0;

	for (int i = 0; i < size; ++i) {
		memcpy(pos, points[i]->pos.AsArray(), 3 * sizeof(float));
		pos += posStride;
	}
	if (hasTexCoord) {
		for (int i = 0; i < size; ++i) {
			memcpy(tex, points[i]->uv, 2 * sizeof(float));
			tex += texStride;
		}
	}
	if (hasColor) {
		for (int i = 0; i < size; ++i) {
			memcpy(col, Vec4f::FromRGBA(points[i]->color_32).AsArray(), 4 * sizeof(float));
			col += colStride;
		}
	}
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

	TimeCollector collectStat(&gpuStats.msPrepareDepth, coreCollectDebugStats);

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

	TimeCollector collectStat(&gpuStats.msPrepareDepth, coreCollectDebugStats);

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
		gpuStats.msRasterTimeAvailable += time_now_d() - rasterTimeStart_;
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
			TimeCollector collectStat(&gpuStats.msCullDepth, collectStats);
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
		{
			TimeCollector collectStat(&gpuStats.msRasterizeDepth, collectStats);
			DepthRasterScreenVerts((uint16_t *)Memory::GetPointerWrite(draw.depthAddr), draw.depthStride, tx, ty, tz, outVertCount, draw, tileScissor, lowQ);
		}
	}

	// Reset queue
	depthIndexCount_ = 0;
	depthVertexCount_ = 0;
	depthDraws_.clear();
}
