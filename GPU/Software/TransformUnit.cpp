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

#include "ppsspp_config.h"

#include <cmath>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Math/math_util.h"
#include "Common/MemoryUtil.h"
#include "Common/Profiler/Profiler.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "Common/Math/SIMDHeaders.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Lighting.h"
#include "GPU/Software/RasterizerRectangle.h"
#include "GPU/Software/TransformUnit.h"

// For the SSE4 stuff
#if PPSSPP_ARCH(SSE2)
#include <smmintrin.h>
#endif

#define TRANSFORM_BUF_SIZE (65536 * 48)

TransformUnit::TransformUnit() {
	decoded_ = (u8 *)AllocateAlignedMemory(TRANSFORM_BUF_SIZE, 16);
	_assert_(decoded_);
	binner_ = new BinManager();
}

TransformUnit::~TransformUnit() {
	FreeAlignedMemory(decoded_);
	delete binner_;
}

bool TransformUnit::IsStarted() {
	return binner_ && decoded_;
}

SoftwareDrawEngine::SoftwareDrawEngine() {
	flushOnParams_ = false;
}

SoftwareDrawEngine::~SoftwareDrawEngine() {}

void SoftwareDrawEngine::NotifyConfigChanged() {
	DrawEngineCommon::NotifyConfigChanged();
	applySkinInDecode_ = true;
}

void SoftwareDrawEngine::Flush() {
	transformUnit.Flush(gpuCommon_, "debug");
}

void SoftwareDrawEngine::DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, bool clockwise, int *bytesRead) {
	_assert_msg_(clockwise, "Mixed cull mode not supported.");
	transformUnit.SubmitPrimitive(verts, inds, prim, vertexCount, vertTypeID, bytesRead, this);
}

void SoftwareDrawEngine::DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation) {
	uint32_t vertTypeID = GetVertTypeID(gstate.vertType | GE_VTYPE_POS_FLOAT, gstate.getUVGenMode(), true);

	int flipCull = cullMode != gstate.getCullMode() ? 1 : 0;
	// TODO: For now, just setting all dirty.
	transformUnit.SetDirty(SoftDirty(-1));
	gstate.cullmode ^= flipCull;

	// TODO: This is a bit ugly.  Should bypass when clipping...
	uint32_t xScale = gstate.viewportxscale;
	uint32_t xCenter = gstate.viewportxcenter;
	uint32_t yScale = gstate.viewportyscale;
	uint32_t yCenter = gstate.viewportycenter;
	uint32_t zScale = gstate.viewportzscale;
	uint32_t zCenter = gstate.viewportzcenter;

	// Force scale to 1 and center to zero.
	gstate.viewportxscale = (GE_CMD_VIEWPORTXSCALE << 24) | 0x3F8000;
	gstate.viewportxcenter = (GE_CMD_VIEWPORTXCENTER << 24) | 0x000000;
	gstate.viewportyscale = (GE_CMD_VIEWPORTYSCALE << 24) | 0x3F8000;
	gstate.viewportycenter = (GE_CMD_VIEWPORTYCENTER << 24) | 0x000000;
	// Z we scale to 65535 for neg z clipping.
	gstate.viewportzscale = (GE_CMD_VIEWPORTZSCALE << 24) | 0x477FFF;
	gstate.viewportzcenter = (GE_CMD_VIEWPORTZCENTER << 24) | 0x000000;

	// Before we start, submit 0 prims to reset the prev prim type.
	// Following submits will always be KEEP_PREVIOUS.
	if (!continuation)
		transformUnit.SubmitPrimitive(nullptr, nullptr, prim, 0, vertTypeID, nullptr, this);

	for (int i = 0; i < vertexCount; i++) {
		ClipVertexData vert;
		vert.clippos = ClipCoords(buffer[i].pos);
		vert.v.texturecoords.x = buffer[i].u;
		vert.v.texturecoords.y = buffer[i].v;
		vert.v.texturecoords.z = buffer[i].uv_w;
		if (gstate.isModeThrough()) {
			vert.v.texturecoords.x *= gstate.getTextureWidth(0);
			vert.v.texturecoords.y *= gstate.getTextureHeight(0);
		} else {
			vert.clippos.z *= 1.0f / 65535.0f;
		}
		vert.v.clipw = buffer[i].pos_w;
		vert.v.color0 = buffer[i].color0_32;
		vert.v.color1 = gstate.isUsingSecondaryColor() && !gstate.isModeThrough() ? buffer[i].color1_32 : 0;
		vert.v.fogdepth = buffer[i].fog;
		vert.v.screenpos.x = (int)(buffer[i].x * 16.0f);
		vert.v.screenpos.y = (int)(buffer[i].y * 16.0f);
		vert.v.screenpos.z = (u16)(u32)buffer[i].z;

		transformUnit.SubmitImmVertex(vert, this);
	}

	gstate.viewportxscale = xScale;
	gstate.viewportxcenter = xCenter;
	gstate.viewportyscale = yScale;
	gstate.viewportycenter = yCenter;
	gstate.viewportzscale = zScale;
	gstate.viewportzcenter = zCenter;

	gstate.cullmode ^= flipCull;
	// TODO: Should really clear, but a bunch of values are forced so we this is safest.
	transformUnit.SetDirty(SoftDirty(-1));
}

VertexDecoder *SoftwareDrawEngine::FindVertexDecoder(u32 vtype) {
	const u32 vertTypeID = GetVertTypeID(vtype, gstate.getUVGenMode(), true);
	return DrawEngineCommon::GetVertexDecoder(vertTypeID);
}

WorldCoords TransformUnit::ModelToWorld(const ModelCoords &coords) {
	return Vec3ByMatrix43(coords, gstate.worldMatrix);
}

WorldCoords TransformUnit::ModelToWorldNormal(const ModelCoords &coords) {
	return Norm3ByMatrix43(coords, gstate.worldMatrix);
}

template <bool depthClamp, bool alwaysCheckRange>
static ScreenCoords ClipToScreenInternal(Vec3f scaled, const ClipCoords &coords, bool *outside_range_flag) {
	ScreenCoords ret;

	// Account for rounding for X and Y.
	// TODO: Validate actual rounding range.
	const float SCREEN_BOUND = 4095.0f + (15.5f / 16.0f);

	// This matches hardware tests - depth is clamped when this flag is on.
	if constexpr (depthClamp) {
		// Note: if the depth is clipped (z/w <= -1.0), the outside_range_flag should NOT be set, even for x and y.
		if ((alwaysCheckRange || coords.z > -coords.w) && (scaled.x >= SCREEN_BOUND || scaled.y >= SCREEN_BOUND || scaled.x < 0 || scaled.y < 0)) {
			*outside_range_flag = true;
		}

		if (scaled.z < 0.f)
			scaled.z = 0.f;
		else if (scaled.z > 65535.0f)
			scaled.z = 65535.0f;
	} else if (scaled.x > SCREEN_BOUND || scaled.y >= SCREEN_BOUND || scaled.x < 0 || scaled.y < 0 || scaled.z < 0.0f || scaled.z >= 65536.0f) {
		*outside_range_flag = true;
	}

	// 16 = 0xFFFF / 4095.9375
	// Round up at 0.625 to the nearest subpixel.
	static_assert(SCREEN_SCALE_FACTOR == 16, "Currently only supports scale 16");
	int x = (int)(scaled.x * 16.0f + 0.375f - gstate.getOffsetX16());
	int y = (int)(scaled.y * 16.0f + 0.375f - gstate.getOffsetY16());
	return ScreenCoords(x, y, scaled.z);
}

static inline ScreenCoords ClipToScreenInternal(const ClipCoords &coords, bool *outside_range_flag) {
	// Parameters here can seem invalid, but the PSP is fine with negative viewport widths etc.
	// The checking that OpenGL and D3D do is actually quite superflous as the calculations still "work"
	// with some pretty crazy inputs, which PSP games are happy to do at times.
	float xScale = gstate.getViewportXScale();
	float xCenter = gstate.getViewportXCenter();
	float yScale = gstate.getViewportYScale();
	float yCenter = gstate.getViewportYCenter();
	float zScale = gstate.getViewportZScale();
	float zCenter = gstate.getViewportZCenter();

	float x = coords.x * xScale / coords.w + xCenter;
	float y = coords.y * yScale / coords.w + yCenter;
	float z = coords.z * zScale / coords.w + zCenter;

	if (gstate.isDepthClampEnabled()) {
		return ClipToScreenInternal<true, true>(Vec3f(x, y, z), coords, outside_range_flag);
	}
	return ClipToScreenInternal<false, true>(Vec3f(x, y, z), coords, outside_range_flag);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords &coords, bool *outsideRangeFlag) {
	return ClipToScreenInternal(coords, outsideRangeFlag);
}

ScreenCoords TransformUnit::DrawingToScreen(const DrawingCoords &coords, u16 z) {
	ScreenCoords ret;
	ret.x = (u32)coords.x * SCREEN_SCALE_FACTOR;
	ret.y = (u32)coords.y * SCREEN_SCALE_FACTOR;
	ret.z = z;
	return ret;
}

enum class MatrixMode {
	POS_TO_CLIP = 1,
	WORLD_TO_CLIP = 2,
};

struct TransformState {
	Lighting::State lightingState;

	float matrix[16];
	Vec4f posToFog;
	Vec3f screenScale;
	Vec3f screenAdd;

	ScreenCoords(*roundToScreen)(Vec3f scaled, const ClipCoords &coords, bool *outside_range_flag);

	struct {
		bool enableTransform : 1;
		bool enableLighting : 1;
		bool enableFog : 1;
		bool readUV : 1;
		bool negateNormals : 1;
		uint8_t uvGenMode : 2;
		uint8_t matrixMode : 2;
	};
};

void ComputeTransformState(TransformState *state, const VertexReader &vreader) {
	state->enableTransform = !vreader.isThrough();
	state->enableLighting = gstate.isLightingEnabled();
	state->enableFog = gstate.isFogEnabled();
	state->readUV = !gstate.isModeClear() && gstate.isTextureMapEnabled() && vreader.hasUV();
	state->negateNormals = gstate.areNormalsReversed();

	state->uvGenMode = gstate.getUVGenMode();
	if (state->uvGenMode == GE_TEXMAP_UNKNOWN)
		state->uvGenMode = GE_TEXMAP_TEXTURE_COORDS;

	if (state->enableTransform) {
		bool canSkipWorldPos = true;
		if (state->enableLighting) {
			Lighting::ComputeState(&state->lightingState, vreader.hasColor0());
			canSkipWorldPos = !state->lightingState.usesWorldPos;
		} else {
			state->lightingState.usesWorldNormal = state->uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP;
		}

		float world[16];
		float view[16];
		float worldview[16];
		ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
		if (state->enableFog || canSkipWorldPos) {
			ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
			Matrix4ByMatrix4(worldview, world, view);
		}

		if (canSkipWorldPos) {
			state->matrixMode = (uint8_t)MatrixMode::POS_TO_CLIP;
			Matrix4ByMatrix4(state->matrix, worldview, gstate.projMatrix);
		} else {
			state->matrixMode = (uint8_t)MatrixMode::WORLD_TO_CLIP;
			Matrix4ByMatrix4(state->matrix, view, gstate.projMatrix);
		}

		if (state->enableFog) {
			float fogEnd = getFloat24(gstate.fog1);
			float fogSlope = getFloat24(gstate.fog2);

			// We bake fog end and slope into the dot product.
			state->posToFog = Vec4f(worldview[2], worldview[6], worldview[10], worldview[14] + fogEnd);

			// If either are NAN/INF, we simplify so there's no inf + -inf muddying things.
			// This is required for Outrun to render proper skies, for example.
			// The PSP treats these exponents as if they were valid.
			if (my_isnanorinf(fogEnd)) {
				bool sign = std::signbit(fogEnd);
				// The multiply would reverse it if it wasn't infinity (doesn't matter if it's infnan.)
				if (std::signbit(fogSlope))
					sign = !sign;
				// Also allow a multiply by zero (slope) to result in zero, regardless of sign.
				// Act like it was negative and clamped to zero.
				if (fogSlope == 0.0f)
					sign = true;

				// Since this is constant for the entire draw, we don't even use infinity.
				float forced = sign ? 0.0f : 1.0f;
				state->posToFog = Vec4f(0.0f, 0.0f, 0.0f, forced);
			} else if (my_isnanorinf(fogSlope)) {
				// We can't have signs differ with infinities, so we use a large value.
				// Anything outside [0, 1] will clamp, so this essentially forces extremes.
				fogSlope = std::signbit(fogSlope) ? -262144.0f : 262144.0f;
				state->posToFog *= fogSlope;
			} else {
				state->posToFog *= fogSlope;
			}
		}

		state->screenScale = Vec3f(gstate.getViewportXScale(), gstate.getViewportYScale(), gstate.getViewportZScale());
		state->screenAdd = Vec3f(gstate.getViewportXCenter(), gstate.getViewportYCenter(), gstate.getViewportZCenter());
	}

	if (gstate.isDepthClampEnabled())
		state->roundToScreen = &ClipToScreenInternal<true, false>;
	else
		state->roundToScreen = &ClipToScreenInternal<false, false>;
}

#if defined(_M_SSE)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline __m128 Dot43SSE4(__m128 a, __m128 b) {
	__m128 multiplied = _mm_mul_ps(a, _mm_insert_ps(b, _mm_set1_ps(1.0f), 0x30));
	__m128 lanes3311 = _mm_movehdup_ps(multiplied);
	__m128 partial = _mm_add_ps(multiplied, lanes3311);
	return _mm_add_ss(partial, _mm_movehl_ps(lanes3311, partial));
}
#endif

static inline float Dot43(const Vec4f &a, const Vec3f &b) {
#if defined(_M_SSE) && !PPSSPP_ARCH(X86)
	if (cpu_info.bSSE4_1)
		return _mm_cvtss_f32(Dot43SSE4(a.vec, b.vec));
#elif PPSSPP_ARCH(ARM64_NEON)
	float32x4_t multipled = vmulq_f32(a.vec, vsetq_lane_f32(1.0f, b.vec, 3));
	float32x2_t add1 = vget_low_f32(vpaddq_f32(multipled, multipled));
	float32x2_t add2 = vpadd_f32(add1, add1);
	return vget_lane_f32(add2, 0);
#endif
	return Dot(a, Vec4f(b, 1.0f));
}

ClipVertexData TransformUnit::ReadVertex(const VertexReader &vreader, const TransformState &state) {
	PROFILE_THIS_SCOPE("read_vert");
	// If we ever thread this, we'll have to change this.
	ClipVertexData vertex;

	ModelCoords pos;
	// VertexDecoder normally scales z, but we want it unscaled.
	vreader.ReadPosThroughZ16(pos.AsArray());

	static Vec3Packedf lastTC;
	if (state.readUV) {
		vreader.ReadUV(vertex.v.texturecoords.AsArray());
		vertex.v.texturecoords.q() = 0.0f;
		lastTC = vertex.v.texturecoords;
	} else {
		vertex.v.texturecoords = lastTC;
	}

	static Vec3f lastnormal;
	if (vreader.hasNormal())
		vreader.ReadNrm(lastnormal.AsArray());
	Vec3f normal = lastnormal;
	if (state.negateNormals)
		normal = -normal;

	if (vreader.hasColor0()) {
		vertex.v.color0 = vreader.ReadColor0_8888();
	} else {
		vertex.v.color0 = gstate.getMaterialAmbientRGBA();
	}

	vertex.v.color1 = 0;

	if (state.enableTransform) {
		WorldCoords worldpos;

		switch (MatrixMode(state.matrixMode)) {
		case MatrixMode::POS_TO_CLIP:
			vertex.clippos = Vec3ByMatrix44(pos, state.matrix);
			break;

		case MatrixMode::WORLD_TO_CLIP:
			worldpos = TransformUnit::ModelToWorld(pos);
			vertex.clippos = Vec3ByMatrix44(worldpos, state.matrix);
			break;
		}

		Vec3f screenScaled;
#ifdef _M_SSE
		screenScaled.vec = _mm_mul_ps(vertex.clippos.vec, state.screenScale.vec);
		screenScaled.vec = _mm_div_ps(screenScaled.vec, _mm_shuffle_ps(vertex.clippos.vec, vertex.clippos.vec, _MM_SHUFFLE(3, 3, 3, 3)));
		screenScaled.vec = _mm_add_ps(screenScaled.vec, state.screenAdd.vec);
#else
		screenScaled = vertex.clippos.xyz() * state.screenScale / vertex.clippos.w + state.screenAdd;
#endif
		bool outside_range_flag = false;
		vertex.v.screenpos = state.roundToScreen(screenScaled, vertex.clippos, &outside_range_flag);
		if (outside_range_flag) {
			// We use this, essentially, as the flag.
			vertex.v.screenpos.x = 0x7FFFFFFF;
			return vertex;
		}

		if (state.enableFog) {
			vertex.v.fogdepth = Dot43(state.posToFog, pos);
		} else {
			vertex.v.fogdepth = 1.0f;
		}
		vertex.v.clipw = vertex.clippos.w;

		Vec3<float> worldnormal;
		if (state.lightingState.usesWorldNormal) {
			worldnormal = TransformUnit::ModelToWorldNormal(normal);
			worldnormal.NormalizeOr001();
		}

		// Time to generate some texture coords.  Lighting will handle shade mapping.
		if (state.uvGenMode == GE_TEXMAP_TEXTURE_MATRIX) {
			Vec3f source;
			switch (gstate.getUVProjMode()) {
			case GE_PROJMAP_POSITION:
				source = pos;
				break;

			case GE_PROJMAP_UV:
				source = Vec3f(vertex.v.texturecoords.uv(), 0.0f);
				break;

			case GE_PROJMAP_NORMALIZED_NORMAL:
				// This does not use 0, 0, 1 if length is zero.
				source = normal.Normalized(cpu_info.bSSE4_1);
				break;

			case GE_PROJMAP_NORMAL:
				source = normal;
				break;
			}

			// Note that UV scale/offset are not used in this mode.
			Vec3<float> stq = Vec3ByMatrix43(source, gstate.tgenMatrix);
			vertex.v.texturecoords = Vec3Packedf(stq.x, stq.y, stq.z);
		} else if (state.uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP) {
			Lighting::GenerateLightST(vertex.v, worldnormal);
		}

		PROFILE_THIS_SCOPE("light");
		if (state.enableLighting)
			Lighting::Process(vertex.v, worldpos, worldnormal, state.lightingState);
	} else {
		vertex.v.screenpos.x = (int)(pos[0] * SCREEN_SCALE_FACTOR);
		vertex.v.screenpos.y = (int)(pos[1] * SCREEN_SCALE_FACTOR);
		vertex.v.screenpos.z = pos[2];
		vertex.v.clipw = 1.0f;
		vertex.v.fogdepth = 1.0f;
	}

	return vertex;
}

void TransformUnit::SetDirty(SoftDirty flags) {
	binner_->SetDirty(flags);
}
SoftDirty TransformUnit::GetDirty() {
	return binner_->GetDirty();
}

class SoftwareVertexReader {
public:
	SoftwareVertexReader(u8 *base, VertexDecoder &vdecoder, u32 vertex_type, int vertex_count, const void *vertices, const void *indices, const TransformState &transformState, TransformUnit &transform)
	: vreader_(base, vdecoder.GetDecVtxFmt(), vertex_type), conv_(vertex_type, indices), transformState_(transformState), transform_(transform) {
		useIndices_ = indices != nullptr;
		lowerBound_ = 0;
		upperBound_ = vertex_count == 0 ? 0 : vertex_count - 1;

		if (useIndices_)
			GetIndexBounds(indices, vertex_count, vertex_type, &lowerBound_, &upperBound_);
		if (vertex_count != 0) {
			const int count = upperBound_ - lowerBound_ + 1;
			vdecoder.DecodeVerts(base, (const u8 *)vertices + vdecoder.VertexSize() * lowerBound_, &gstate_c.uv, count);
		}

		// If we're only using a subset of verts, it's better to decode with random access (usually.)
		// However, if we're reusing a lot of verts, we should read and cache them.
		useCache_ = useIndices_ && vertex_count > (upperBound_ - lowerBound_ + 1);
		if (useCache_ && (int)cached_.size() < upperBound_ - lowerBound_ + 1)
			cached_.resize(std::max(128, upperBound_ - lowerBound_ + 1));
	}

	const VertexReader &GetVertexReader() const {
		return vreader_;
	}

	bool IsThrough() const {
		return vreader_.isThrough();
	}

	void UpdateCache() {
		if (!useCache_)
			return;

		for (int i = 0; i < upperBound_ - lowerBound_ + 1; ++i) {
			vreader_.Goto(i);
			cached_[i] = transform_.ReadVertex(vreader_, transformState_);
		}
	}

	inline ClipVertexData Read(int vtx) {
		if (useIndices_) {
			if (useCache_) {
				return cached_[conv_(vtx) - lowerBound_];
			}
			vreader_.Goto(conv_(vtx) - lowerBound_);
		} else {
			vreader_.Goto(vtx);
		}

		return transform_.ReadVertex(vreader_, transformState_);
	};

protected:
	VertexReader vreader_;
	const IndexConverter conv_;
	const TransformState &transformState_;
	TransformUnit &transform_;
	uint16_t lowerBound_;
	uint16_t upperBound_;
	static std::vector<ClipVertexData> cached_;
	bool useIndices_ = false;
	bool useCache_ = false;
};

// Static to reduce allocations mid-frame.
std::vector<ClipVertexData> SoftwareVertexReader::cached_;

void TransformUnit::SubmitPrimitive(const void* vertices, const void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine)
{
	VertexDecoder &vdecoder = *drawEngine->FindVertexDecoder(vertex_type);

	if (bytesRead)
		*bytesRead = vertex_count * vdecoder.VertexSize();

	// Frame skipping.
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		return;
	}
	// Vertices without position are just entirely culled.
	// Note: Throughmode does draw 8-bit primitives, but positions are always zero - handled in decode.
	if ((vertex_type & GE_VTYPE_POS_MASK) == 0)
		return;

	static TransformState transformState;
	SoftwareVertexReader vreader(decoded_, vdecoder, vertex_type, vertex_count, vertices, indices, transformState, *this);

	if (prim_type != GE_PRIM_KEEP_PREVIOUS) {
		data_index_ = 0;
		prev_prim_ = prim_type;
	} else {
		prim_type = prev_prim_;
	}

	binner_->UpdateState();
	hasDraws_ = true;

	if (binner_->HasDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL)) {
		ComputeTransformState(&transformState, vreader.GetVertexReader());
		binner_->ClearDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL);
	}
	vreader.UpdateCache();

	bool skipCull = !gstate.isCullEnabled() || gstate.isModeClear();
	const CullType cullType = skipCull ? CullType::OFF : (gstate.getCullMode() ? CullType::CCW : CullType::CW);

	if (vreader.IsThrough() && cullType == CullType::OFF && prim_type == GE_PRIM_TRIANGLES && data_index_ == 0 && vertex_count >= 6 && ((vertex_count) % 6) == 0) {
		// Some games send rectangles as a series of regular triangles.
		// We look for this, but only in throughmode.
		ClipVertexData buf[6];
		// Could start at data_index_ and copy to buf, but there's little reason.
		int buf_index = 0;
		_assert_(data_index_ == 0);

		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			buf[buf_index++] = vreader.Read(vtx);
			if (buf_index < 6)
				continue;

			int tl = -1, br = -1;
			if (Rasterizer::DetectRectangleFromPair(binner_->State(), buf, &tl, &br)) {
				Clipper::ProcessRect(buf[tl], buf[br], *binner_);
			} else {
				SendTriangle(cullType, &buf[0]);
				SendTriangle(cullType, &buf[3]);
			}

			buf_index = 0;
		}

		if (buf_index >= 3) {
			SendTriangle(cullType, &buf[0]);
			data_index_ = 0;
			for (int i = 3; i < buf_index; ++i) {
				data_[data_index_++] = buf[i];
			}
		} else if (buf_index > 0) {
			for (int i = 0; i < buf_index; ++i) {
				data_[i] = buf[i];
			}
			data_index_ = buf_index;
		} else {
			data_index_ = 0;
		}

		return;
	}

	// Note: intentionally, these allow for the case of vertex_count == 0, but data_index_ > 0.
	// This is used for immediate-mode primitives.
	switch (prim_type) {
	case GE_PRIM_POINTS:
		for (int i = 0; i < data_index_; ++i)
			Clipper::ProcessPoint(data_[i], *binner_);
		data_index_ = 0;
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			data_[0] = vreader.Read(vtx);
			Clipper::ProcessPoint(data_[0], *binner_);
		}
		break;

	case GE_PRIM_LINES:
		for (int i = 0; i < data_index_ - 1; i += 2)
			Clipper::ProcessLine(data_[i + 0], data_[i + 1], *binner_);
		data_index_ &= 1;
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			data_[data_index_++] = vreader.Read(vtx);
			if (data_index_ == 2) {
				Clipper::ProcessLine(data_[0], data_[1], *binner_);
				data_index_ = 0;
			}
		}
		break;

	case GE_PRIM_TRIANGLES:
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			data_[data_index_++] = vreader.Read(vtx);
			if (data_index_ < 3) {
				// Keep reading.  Note: an incomplete prim will stay read for GE_PRIM_KEEP_PREVIOUS.
				continue;
			}
			// Okay, we've got enough verts.  Reset the index for next time.
			data_index_ = 0;

			SendTriangle(cullType, &data_[0]);
		}
		// In case vertex_count was 0.
		if (data_index_ >= 3) {
			SendTriangle(cullType, &data_[0]);
			data_index_ = 0;
		}
		break;

	case GE_PRIM_RECTANGLES:
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			data_[data_index_++] = vreader.Read(vtx);

			if (data_index_ == 4 && vreader.IsThrough() && cullType == CullType::OFF) {
				if (Rasterizer::DetectRectangleThroughModeSlices(binner_->State(), data_)) {
					data_[1] = data_[3];
					data_index_ = 2;
				}
			}

			if (data_index_ == 4) {
				Clipper::ProcessRect(data_[0], data_[1], *binner_);
				Clipper::ProcessRect(data_[2], data_[3], *binner_);
				data_index_ = 0;
			}
		}

		if (data_index_ >= 2) {
			Clipper::ProcessRect(data_[0], data_[1], *binner_);
			data_index_ -= 2;
		}
		break;

	case GE_PRIM_LINE_STRIP:
		{
			// Don't draw a line when loading the first vertex.
			// If data_index_ is 1 or 2, etc., it means we're continuing a line strip.
			int skip_count = data_index_ == 0 ? 1 : 0;
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				data_[(data_index_++) & 1] = vreader.Read(vtx);

				if (skip_count) {
					--skip_count;
				} else {
					// We already incremented data_index_, so data_index_ & 1 is previous one.
					Clipper::ProcessLine(data_[data_index_ & 1], data_[(data_index_ & 1) ^ 1], *binner_);
				}
			}
			// If this is from immediate-mode drawing, we always had one new vert (already in data_.)
			if (isImmDraw_ && data_index_ >= 2)
				Clipper::ProcessLine(data_[data_index_ & 1], data_[(data_index_ & 1) ^ 1], *binner_);
			break;
		}

	case GE_PRIM_TRIANGLE_STRIP:
		{
			// Don't draw a triangle when loading the first two vertices.
			int skip_count = data_index_ >= 2 ? 0 : 2 - data_index_;
			int start_vtx = 0;

			// If index count == 4, check if we can convert to a rectangle.
			// This is for Darkstalkers (and should speed up many 2D games).
			if (data_index_ == 0 && vertex_count >= 4 && (vertex_count & 1) == 0 && cullType == CullType::OFF) {
				for (int base = 0; base < vertex_count - 2; base += 2) {
					for (int vtx = base == 0 ? 0 : 2; vtx < 4; ++vtx) {
						data_[vtx] = vreader.Read(base + vtx);
					}

					// If a strip is effectively a rectangle, draw it as such!
					int tl = -1, br = -1;
					if (Rasterizer::DetectRectangleFromStrip(binner_->State(), data_, &tl, &br)) {
						Clipper::ProcessRect(data_[tl], data_[br], *binner_);
						start_vtx += 2;
						skip_count = 2;
						if (base + 4 >= vertex_count) {
							start_vtx = vertex_count;
							break;
						}

						// Just copy the first two so we can detect easier.
						// TODO: Maybe should give detection two halves?
						data_[0] = data_[2];
						data_[1] = data_[3];
						data_index_ = 2;
					} else {
						// Go into triangle mode.  Unfortunately, we re-read the verts.
						break;
					}
				}
			}

			for (int vtx = start_vtx; vtx < vertex_count && skip_count > 0; ++vtx) {
				int provoking_index = (data_index_++) % 3;
				data_[provoking_index] = vreader.Read(vtx);
				--skip_count;
				++start_vtx;
			}

			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				int provoking_index = (data_index_++) % 3;
				data_[provoking_index] = vreader.Read(vtx);

				int wind = (data_index_ - 1) % 2;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				SendTriangle(altCullType, &data_[0], provoking_index);
			}

			// If this is from immediate-mode drawing, we always had one new vert (already in data_.)
			if (isImmDraw_ && data_index_ >= 3) {
				int provoking_index = (data_index_ - 1) % 3;
				int wind = (data_index_ - 1) % 2;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				SendTriangle(altCullType, &data_[0], provoking_index);
			}
			break;
		}

	case GE_PRIM_TRIANGLE_FAN:
		{
			// Don't draw a triangle when loading the first two vertices.
			// (this doesn't count the central one.)
			int skip_count = data_index_ <= 1 ? 1 : 0;
			int start_vtx = 0;

			// Only read the central vertex if we're not continuing.
			if (data_index_ == 0 && vertex_count > 0) {
				data_[0] = vreader.Read(0);
				data_index_++;
				start_vtx = 1;
			}

			if (data_index_ == 1 && vertex_count == 4 && cullType == CullType::OFF) {
				for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
					data_[vtx] = vreader.Read(vtx);
				}

				int tl = -1, br = -1;
				if (Rasterizer::DetectRectangleFromFan(binner_->State(), data_, &tl, &br)) {
					Clipper::ProcessRect(data_[tl], data_[br], *binner_);
					break;
				}
			}

			for (int vtx = start_vtx; vtx < vertex_count && skip_count > 0; ++vtx) {
				int provoking_index = 2 - ((data_index_++) % 2);
				data_[provoking_index] = vreader.Read(vtx);
				--skip_count;
				++start_vtx;
			}

			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				int provoking_index = 2 - ((data_index_++) % 2);
				data_[provoking_index] = vreader.Read(vtx);

				int wind = (data_index_ - 1) % 2;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				SendTriangle(altCullType, &data_[0], provoking_index);
			}

			// If this is from immediate-mode drawing, we always had one new vert (already in data_.)
			if (isImmDraw_ && data_index_ >= 3) {
				int wind = (data_index_ - 1) % 2;
				int provoking_index = 2 - wind;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				SendTriangle(altCullType, &data_[0], provoking_index);
			}
			break;
		}

	default:
		ERROR_LOG(Log::G3D, "Unexpected prim type: %d", prim_type);
		break;
	}
}

void TransformUnit::SubmitImmVertex(const ClipVertexData &vert, SoftwareDrawEngine *drawEngine) {
	// Where we put it is different for STRIP/FAN types.
	switch (prev_prim_) {
	case GE_PRIM_POINTS:
	case GE_PRIM_LINES:
	case GE_PRIM_TRIANGLES:
	case GE_PRIM_RECTANGLES:
		// This is the easy one.  SubmitPrimitive resets data_index_.
		data_[data_index_++] = vert;
		break;

	case GE_PRIM_LINE_STRIP:
		// This one alternates, and data_index_ > 0 means it draws a segment.
		data_[(data_index_++) & 1] = vert;
		break;

	case GE_PRIM_TRIANGLE_STRIP:
		data_[(data_index_++) % 3] = vert;
		break;

	case GE_PRIM_TRIANGLE_FAN:
		if (data_index_ == 0) {
			data_[data_index_++] = vert;
		} else {
			int provoking_index = 2 - ((data_index_++) % 2);
			data_[provoking_index] = vert;
		}
		break;

	default:
		_assert_msg_(false, "Invalid prim type: %d", (int)prev_prim_);
		break;
	}

	uint32_t vertTypeID = GetVertTypeID(gstate.vertType | GE_VTYPE_POS_FLOAT, gstate.getUVGenMode(), true);
	// This now processes the step with shared logic, given the existing data_.
	isImmDraw_ = true;
	SubmitPrimitive(nullptr, nullptr, GE_PRIM_KEEP_PREVIOUS, 0, vertTypeID, nullptr, drawEngine);
	isImmDraw_ = false;
}

void TransformUnit::SendTriangle(CullType cullType, const ClipVertexData *verts, int provoking) {
	if (cullType == CullType::OFF) {
		Clipper::ProcessTriangle(verts[0], verts[1], verts[2], verts[provoking], *binner_);
		Clipper::ProcessTriangle(verts[2], verts[1], verts[0], verts[provoking], *binner_);
	} else if (cullType == CullType::CW) {
		Clipper::ProcessTriangle(verts[2], verts[1], verts[0], verts[provoking], *binner_);
	} else {
		Clipper::ProcessTriangle(verts[0], verts[1], verts[2], verts[provoking], *binner_);
	}
}

void TransformUnit::Flush(GPUCommon *common, const char *reason) {
	if (!hasDraws_)
		return;

	binner_->Flush(reason);
	common->NotifyFlush();
	hasDraws_ = false;
}

void TransformUnit::GetStats(char *buffer, size_t bufsize) {
	// TODO: More stats?
	binner_->GetStats(buffer, bufsize);
}

void TransformUnit::FlushIfOverlap(GPUCommon *common, const char *reason, bool modifying, uint32_t addr, uint32_t stride, uint32_t w, uint32_t h) {
	if (!hasDraws_)
		return;

	if (binner_->HasPendingWrite(addr, stride, w, h))
		Flush(common, reason);
	if (modifying && binner_->HasPendingRead(addr, stride, w, h))
		Flush(common, reason);
}

void TransformUnit::NotifyClutUpdate(const void *src) {
	binner_->UpdateClut(src);
}

// TODO: This probably is not the best interface.
// Also, we should try to merge this into the similar function in DrawEngineCommon.
bool TransformUnit::GetCurrentDrawAsDebugVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

	if (!Memory::IsValidAddress(gstate_c.vertexAddr) || count == 0)
		return false;

	if (count > 0 && (gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const u8 *inds = Memory::GetPointer(gstate_c.indexAddr);
		const u16_le *inds16 = (const u16_le *)inds;
		const u32_le *inds32 = (const u32_le *)inds;

		if (inds) {
			GetIndexBounds(inds, count, gstate.vertType, &indexLowerBound, &indexUpperBound);
			indices.resize(count);
			switch (gstate.vertType & GE_VTYPE_IDX_MASK) {
			case GE_VTYPE_IDX_8BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds[i];
				}
				break;
			case GE_VTYPE_IDX_16BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds16[i];
				}
				break;
			case GE_VTYPE_IDX_32BIT:
				WARN_LOG_REPORT_ONCE(simpleIndexes32, Log::G3D, "SimpleVertices: Decoding 32-bit indexes");
				for (int i = 0; i < count; ++i) {
					// These aren't documented and should be rare.  Let's bounds check each one.
					if (inds32[i] != (u16)inds32[i]) {
						ERROR_LOG_REPORT_ONCE(simpleIndexes32Bounds, Log::G3D, "SimpleVertices: Index outside 16-bit range");
					}
					indices[i] = (u16)inds32[i];
				}
				break;
			}
		} else {
			indices.clear();
		}
	} else {
		indices.clear();
	}

	static std::vector<u32> temp_buffer;
	static std::vector<SimpleVertex> simpleVertices;
	temp_buffer.resize(std::max((int)indexUpperBound, 8192) * 128 / sizeof(u32));
	simpleVertices.resize(indexUpperBound + 1);

	VertexDecoder vdecoder;
	VertexDecoderOptions options{};
	u32 vertTypeID = GetVertTypeID(gstate.vertType, gstate.getUVGenMode(), true);
	vdecoder.SetVertexType(vertTypeID, options);

	if (!Memory::IsValidRange(gstate_c.vertexAddr, (indexUpperBound + 1) * vdecoder.VertexSize()))
		return false;

	::NormalizeVertices(&simpleVertices[0], (u8 *)(&temp_buffer[0]), Memory::GetPointer(gstate_c.vertexAddr), indexLowerBound, indexUpperBound, &vdecoder, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

	const float zScale = gstate.getViewportZScale();
	const float zCenter = gstate.getViewportZCenter();

	vertices.resize(indexUpperBound + 1);
	for (int i = indexLowerBound; i <= indexUpperBound; ++i) {
		const SimpleVertex &vert = simpleVertices[i];

		if (gstate.isModeThrough()) {
			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = vert.pos.x;
			vertices[i].y = vert.pos.y;
			vertices[i].z = vert.pos.z;
		} else {
			Vec4f clipPos = Vec3ByMatrix44(vert.pos, worldviewproj);
			bool outsideRangeFlag;
			ScreenCoords screenPos = ClipToScreen(clipPos, &outsideRangeFlag);
			float z = clipPos.z * zScale / clipPos.w + zCenter;

			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = (float)screenPos.x / SCREEN_SCALE_FACTOR;
			vertices[i].y = (float)screenPos.y / SCREEN_SCALE_FACTOR;
			vertices[i].z = screenPos.z <= 0 || screenPos.z >= 0xFFFF ? z : (float)screenPos.z;
		}

		if (gstate.vertType & GE_VTYPE_COL_MASK) {
			memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
		} else {
			memset(vertices[i].c, 0, sizeof(vertices[i].c));
		}
		vertices[i].nx = vert.nrm.x;
		vertices[i].ny = vert.nrm.y;
		vertices[i].nz = vert.nrm.z;
	}

	// The GE debugger expects these to be set.
	gstate_c.curTextureWidth = gstate.getTextureWidth(0);
	gstate_c.curTextureHeight = gstate.getTextureHeight(0);

	return true;
}
