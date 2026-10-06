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

#include <cfloat>
#include <climits>
#include <cmath>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Math/math_util.h"
#include "Common/MemoryUtil.h"
#include "Common/Profiler/Profiler.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexReader.h"
#include "GPU/GPUStateSIMDUtil.h"
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

SoftwareDrawEngine::SoftwareDrawEngine() {
	flushOnParams_ = false;
}

SoftwareDrawEngine::~SoftwareDrawEngine() {}

void SoftwareDrawEngine::NotifyConfigChanged() {
	DrawEngineCommon::NotifyConfigChanged();
}

void SoftwareDrawEngine::Flush() {
	transformUnit.Flush(gpuCommon_, "debug");
}

void SoftwareDrawEngine::DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, bool clockwise, int *bytesRead, ClipInfoFlags clipInfoFlags) {
	_assert_msg_(clockwise, "Mixed cull mode not supported.");
	transformUnit.SubmitPrimitive(verts, inds, prim, vertexCount, vertTypeID, bytesRead, this);
}

void SoftwareDrawEngine::DispatchSubmitImm(GEPrimitiveType prim, TransformedVertex *buffer, int vertexCount, int cullMode, bool continuation) {
	uint32_t vertTypeID = GetVertTypeID(gstate.vertType | GE_VTYPE_POS_FLOAT, gstate.getUVGenMode());

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
	const u32 vertTypeID = GetVertTypeID(vtype, gstate.getUVGenMode());
	return DrawEngineCommon::GetVertexDecoder(vertTypeID);
}

WorldCoords TransformUnit::ModelToWorldNormal(const ModelCoords &coords) {
	// Each component summed like a matrix row (gpu/probe exp61).
	const float *m = gstate.worldMatrix;
	return WorldCoords(GEDot(coords, Vec3f(m[0], m[3], m[6])), GEDot(coords, Vec3f(m[1], m[4], m[7])), GEDot(coords, Vec3f(m[2], m[5], m[8])));
}

// A clip space component from the combined matrix (gpu/probe exp32, exp34, exp42). The position is a
// float24; the translation is a term of its own.
static inline float GEClipComponent(const Vec3f &v, const float m[16], int c) {
	GERowTerm terms[4] = {
		GEProduct(TruncateToFloat24(v.x), m[c]),
		GEProduct(TruncateToFloat24(v.y), m[4 + c]),
		GEProduct(TruncateToFloat24(v.z), m[8 + c]),
		GEProduct(1.0f, m[12 + c]),
	};
	return GERowSum(terms, 4);
}


// A texture coordinate from the 4x3 texture matrix, summed like a clip space row (gpu/probe exp64).
static inline float GETexGenComponent(const Vec3f &v, const float m[12], int c) {
	GERowTerm terms[4] = {
		GEProduct(TruncateToFloat24(v.x), m[c]),
		GEProduct(TruncateToFloat24(v.y), m[3 + c]),
		GEProduct(TruncateToFloat24(v.z), m[6 + c]),
		GEProduct(1.0f, m[9 + c]),
	};
	return GERowSum(terms, 4);
}

// Multiplies two matrices the way the GE combines world, view and projection (gpu/probe exp35, exp42):
// in the order (world * view) * projection, each entry summed like a row in GEClipComponent.
static void GECombineMatrices(float out[16], const float a[16], const float b[16]) {
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			GERowTerm terms[4];
			for (int k = 0; k < 4; ++k) {
				terms[k] = GEProduct(a[r * 4 + k], b[k * 4 + c]);
			}
			out[r * 4 + c] = GERowSum(terms, 4);
		}
	}
}

// A screen coordinate as the GE computes it (gpu/depth/transformprecision for Z, gpu/probe for X and Y):
// the component divided by w is it times the reciprocal above, truncated to a float24, then scaled and
// offset with GEAdd. Around a center of 2048, that lands X and Y on the 1/16 subpixel grid.
static inline float GEViewport(float clipC, float clipW, float scale, float center) {
	const float w = TruncateToFloat24(clipW);
	if (!std::isfinite(w) || !std::isfinite(clipC) || fabsf(w) < FLT_MIN) {
		return clipC * scale / clipW + center;
	}
	const float ndc = ProductToFloat24((double)TruncateToFloat24(clipC) * GERecip(w));
	return GEAdd(ProductToFloat24((double)ndc * scale), center);
}

// Screen Z is floored.
static inline float GEScreenZ(float clipZ, float clipW, float zScale, float zCenter) {
	return floorf(GEViewport(clipZ, clipW, zScale, zCenter));
}

template <bool depthClamp, bool alwaysCheckRange>
static ScreenCoords ClipToScreenInternal(Vec3f scaled, const ClipCoords &coords, bool *outside_range_flag) {
	// Account for rounding for X and Y.
	// TODO: Validate actual rounding range.
	constexpr float SCREEN_BOUND = 4095.0f + (15.5f / 16.0f);

	// This matches hardware tests - depth is clamped when this flag is on.
	if constexpr (depthClamp) {
		// A vertex the near plane clips away (z < -w) doesn't set the flag, even for x and y. One exactly on
		// the plane isn't clipped, so its range counts (gpu/clipping/guardband).
		// Written as a test for inside, so a NaN coordinate is outside: converting it to an integer below differs
		// by CPU, and on x86-64 its triangles became huge slivers.
		if ((alwaysCheckRange || !(coords.z < -coords.w)) && !(scaled.x < SCREEN_BOUND && scaled.y < SCREEN_BOUND && scaled.x >= 0 && scaled.y >= 0)) {
			*outside_range_flag = true;
		}

		if (scaled.z < 0.f)
			scaled.z = 0.f;
		else if (scaled.z > 65535.0f)
			scaled.z = 65535.0f;
	} else if (!(scaled.x <= SCREEN_BOUND && scaled.y < SCREEN_BOUND && scaled.x >= 0 && scaled.y >= 0 && scaled.z >= 0.0f && scaled.z < 65536.0f)) {
		*outside_range_flag = true;
	}

	// 16 = 0xFFFF / 4095.9375
	static_assert(SCREEN_SCALE_FACTOR == 16, "Currently only supports scale 16");
	int x = (int)floorf(scaled.x * 16.0f) - gstate.getOffsetX16();
	int y = (int)floorf(scaled.y * 16.0f) - gstate.getOffsetY16();
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

	float x = GEViewport(coords.x, coords.w, xScale, xCenter);
	float y = GEViewport(coords.y, coords.w, yScale, yCenter);
	float z = GEScreenZ(coords.z, coords.w, zScale, zCenter);

	if (gstate.isDepthClipEnabled()) {
		return ClipToScreenInternal<true, true>(Vec3f(x, y, z), coords, outside_range_flag);
	}
	return ClipToScreenInternal<false, true>(Vec3f(x, y, z), coords, outside_range_flag);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords &coords, bool *outsideRangeFlag) {
	return ClipToScreenInternal(coords, outsideRangeFlag);
}

// Near plane clipping (gpu/probe exp43-46): from the inside vertex, t = d_in / (d_in - d_out) with
// d = z + w, using the GE's reciprocal, and each coordinate is in + t * (out - in), all in float24 math.
float TransformUnit::NearPlaneT(const ClipCoords &in, const ClipCoords &out) {
	const float dIn = GEAdd(in.z, in.w);
	const float dOut = GEAdd(out.z, out.w);
	const float den = TruncateToFloat24(GEAdd(dIn, -dOut));
	return ProductToFloat24((double)TruncateToFloat24(dIn) * GERecip(den));
}

ClipCoords TransformUnit::NearPlanePoint(const ClipCoords &in, const ClipCoords &out, float t) {
	ClipCoords result;
	for (int c = 0; c < 4; ++c) {
		const float delta = TruncateToFloat24(GEAdd(out[c], -in[c]));
		result[c] = TruncateToFloat24(GEAdd(ProductToFloat24((double)t * delta), in[c]));
	}
	return result;
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
	// With finite fog parameters, the GE's own arithmetic (gpu/probe exp20): the view z as a row of the
	// combined world-view matrix, then float24(GEAdd(z, end) * slope).
	bool fogGE;
	float viewZColumn[4];
	float fogEnd;
	float fogSlope;
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
	// The UV scale and offset, applied in ReadVertex with the GE's arithmetic (the vertex decoder leaves
	// them out then, see UsesGEUVScale).
	bool geUVScale;
	float uvScale[2];
	float uvOffset[2];
};

// UV gen mode 0 in transform mode: u * scale + offset as the GE computes it (gpu/probe exp66).
static bool UsesGEUVScale(u32 vertexType) {
	return (vertexType & GE_VTYPE_THROUGH_MASK) == 0 && gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS;
}

void ComputeTransformState(TransformState *state, const VertexReader &vreader) {
	state->enableTransform = !vreader.isThrough();
	state->enableLighting = gstate.isLightingEnabled();
	state->enableFog = gstate.isFogEnabled();
	state->readUV = !gstate.isModeClear() && gstate.isTextureMapEnabled() && vreader.hasUV();
	state->geUVScale = !vreader.isThrough() && gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS;
	state->uvScale[0] = TruncateToFloat24(getFloat24(gstate.texscaleu));
	state->uvScale[1] = TruncateToFloat24(getFloat24(gstate.texscalev));
	state->uvOffset[0] = TruncateToFloat24(getFloat24(gstate.texoffsetu));
	state->uvOffset[1] = TruncateToFloat24(getFloat24(gstate.texoffsetv));
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
		if (state->uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP) {
			// Shade mapping uses the light vector as lighting sees it, which depends on position for other lights.
			if (!gstate.isDirectionalLight(gstate.getUVLS0()) || !gstate.isDirectionalLight(gstate.getUVLS1())) {
				canSkipWorldPos = false;
			}
		}
		// The viewer direction (see PSPViewDirection), normalized like the GE does.
		Vec3f viewDir(gstate.viewMatrix[2], gstate.viewMatrix[5], gstate.viewMatrix[8]);
		if (GENormalize(viewDir) == 0.0f)
			viewDir = Vec3f(0.0f, 0.0f, 1.0f);
		state->lightingState.viewDir = viewDir;

		float world[16];
		float view[16];
		float worldview[16];
		ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
		ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
		GECombineMatrices(worldview, world, view);

		// Clip coordinates always come from the model position and the matrices combined like the GE does
		// it, in the order (world * view) * projection. The world position is only needed for lighting.
		state->matrixMode = (uint8_t)(canSkipWorldPos ? MatrixMode::POS_TO_CLIP : MatrixMode::WORLD_TO_CLIP);
		GECombineMatrices(state->matrix, worldview, gstate.projMatrix);

		if (state->enableFog) {
			float fogEnd = getFloat24(gstate.fog1);
			float fogSlope = getFloat24(gstate.fog2);

			// We bake fog end and slope into the dot product.
			state->posToFog = Vec4f(worldview[2], worldview[6], worldview[10], worldview[14] + fogEnd);
			state->fogGE = !my_isnanorinf(fogEnd) && !my_isnanorinf(fogSlope);
			for (int i = 0; i < 4; ++i)
				state->viewZColumn[i] = worldview[2 + 4 * i];
			state->fogEnd = TruncateToFloat24(fogEnd);
			state->fogSlope = TruncateToFloat24(fogSlope);

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

	if (gstate.isDepthClipEnabled())
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
	vreader.ReadPosThrough(pos.AsArray());

	static Vec3Packedf lastTC;
	if (state.readUV) {
		vreader.ReadUV(vertex.v.texturecoords.AsArray());
		vertex.v.texturecoords.q() = 0.0f;
		if (state.geUVScale) {
			// The decoder only normalized them (8 and 16 bit UVs are unsigned).
			for (int i = 0; i < 2; ++i) {
				const float scaled = ProductToFloat24((double)TruncateToFloat24(vertex.v.texturecoords[i]) * state.uvScale[i]);
				vertex.v.texturecoords[i] = TruncateToFloat24(GEAdd(scaled, state.uvOffset[i]));
			}
		}
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
		// Clip coordinates with the GE's precision; the Test Drive map depends on it together with the
		// depth math below (#12786).
		{
			for (int c = 0; c < 4; ++c) {
				vertex.clippos[c] = GEClipComponent(pos, state.matrix, c);
			}
		}

		Vec3f screenScaled;
		screenScaled.x = GEViewport(vertex.clippos.x, vertex.clippos.w, state.screenScale.x, state.screenAdd.x);
		screenScaled.y = GEViewport(vertex.clippos.y, vertex.clippos.w, state.screenScale.y, state.screenAdd.y);
		screenScaled.z = GEScreenZ(vertex.clippos.z, vertex.clippos.w, state.screenScale.z, state.screenAdd.z);
		bool outside_range_flag = false;
		vertex.v.screenpos = state.roundToScreen(screenScaled, vertex.clippos, &outside_range_flag);
		if (outside_range_flag) {
			// We use this, essentially, as the flag.
			vertex.v.screenpos.x = 0x7FFFFFFF;
			return vertex;
		}

		if (state.enableFog && state.fogGE) {
			GERowTerm terms[4] = {
				GEProduct(TruncateToFloat24(pos.x), state.viewZColumn[0]),
				GEProduct(TruncateToFloat24(pos.y), state.viewZColumn[1]),
				GEProduct(TruncateToFloat24(pos.z), state.viewZColumn[2]),
				GEProduct(1.0f, state.viewZColumn[3]),
			};
			const float viewZ = GERowSum(terms, 4);
			const float f = ProductToFloat24((double)TruncateToFloat24(GEAdd(viewZ, state.fogEnd)) * state.fogSlope);
			vertex.v.fogdepth = GEFogFactor(f) * (1.0f / 256.0f);
		} else if (state.enableFog) {
			vertex.v.fogdepth = GEFogFactor(Dot43(state.posToFog, pos)) * (1.0f / 256.0f);
		} else {
			vertex.v.fogdepth = 1.0f;
		}
		vertex.v.clipw = vertex.clippos.w;

		// The normal stays as the world matrix leaves it: lighting scales its dot products by the
		// reciprocal length instead (gpu/probe exp3, world matrix cases).
		Vec3<float> worldnormal;
		float normalRsqrt = 1.0f;
		if (state.lightingState.usesWorldNormal) {
			worldnormal = TransformUnit::ModelToWorldNormal(normal);
			const float len2 = GEDot(worldnormal, worldnormal);
			if (len2 > 0.0f && std::isfinite(len2)) {
				normalRsqrt = GERsqrt(len2);
			} else if (len2 != 0.0f) {
				worldnormal = Vec3f(0.0f, 0.0f, 1.0f);
			}
			// A zero normal stays zero: no diffuse or specular from any light (gpu/probe exp173; SOCOM
			// UCES01242 has meshes without normals but lit).
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
				source = normal;
				GENormalize(source);
				break;

			case GE_PROJMAP_NORMAL:
				source = normal;
				break;
			}

			// Note that UV scale/offset are not used in this mode.
			vertex.v.texturecoords = Vec3Packedf(GETexGenComponent(source, gstate.tgenMatrix, 0), GETexGenComponent(source, gstate.tgenMatrix, 1), GETexGenComponent(source, gstate.tgenMatrix, 2));
		} else if (state.uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP) {
			Lighting::GenerateLightST(vertex.v, pos, worldnormal, normalRsqrt, state.lightingState.viewDir);
		}

		PROFILE_THIS_SCOPE("light");
		if (state.enableLighting)
			Lighting::Process(vertex.v, pos, worldnormal, normalRsqrt, state.lightingState);
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

static float ReadRawComponent(const u8 *p, int fmt, int i) {
	switch (fmt) {
	case 1: return ((const s8 *)p)[i] * (1.0f / 128.0f);
	case 2: { s16 v; memcpy(&v, p + 2 * i, 2); return v * (1.0f / 32768.0f); }
	case 3: { float v; memcpy(&v, p + 4 * i, 4); return v; }
	default: return 0.0f;
	}
}

static float ReadRawWeight(const u8 *p, int fmt, int i) {
	switch (fmt) {
	case 1: return p[i] * (1.0f / 128.0f);
	case 2: { u16 v; memcpy(&v, p + 2 * i, 2); return v * (1.0f / 32768.0f); }
	case 3: { float v; memcpy(&v, p + 4 * i, 4); return v; }
	default: return 0.0f;
	}
}

// One morphed component as the GE computes it (gpu/probe exp67, exp107): each frame's value times its
// weight as a float24, summed in frame order with the GE's adder.
template <typename Read>
static float GEMorphComponent(const VertexDecoder &dec, const u8 *in, int off, int c, Read read) {
	float acc = 0.0f;
	for (int k = 0; k < dec.morphcount; ++k) {
		const float v = TruncateToFloat24(read(in + k * dec.onesize_ + off, c));
		const float w = TruncateToFloat24(gstate_c.morphWeights[k]);
		const float term = v == 0.0f || w == 0.0f ? 0.0f : ProductToFloat24((double)v * w);
		if (term != 0.0f)
			acc = acc == 0.0f ? term : TruncateToFloat24(GEAdd(acc, term));
	}
	return acc;
}

// Skinning as the GE does it (gpu/probe exp68, exp70, exp71, bit exact): each bone matrix is scaled by
// its weight, every entry a float24 product, and one accumulator then runs through all the bones in
// order, adding each bone's translation, then x, y and z times its column, with the GE's adder.
// Normals the same without the translation. With morph targets, the morph comes first, weights included
// (gpu/probe exp108). Overwrites what the vertex decoder skinned in float.
static void ApplyGESkinning(u8 *decoded, const VertexDecoder &dec, const u8 *raw, int count) {
	const DecVtxFormat &fmt = dec.GetDecVtxFmt();
	const int nweights = dec.nweights;
	for (int v = 0; v < count; ++v) {
		const u8 *in = raw + v * dec.VertexSize();
		u8 *out = decoded + v * fmt.stride;
		auto component = [&](int off, int c, auto read) {
			return dec.morphcount > 1 ? GEMorphComponent(dec, in, off, c, read) : read(in + off, c);
		};
		float bones[8][12];
		for (int b = 0; b < nweights; ++b) {
			const float w = TruncateToFloat24(component(dec.weightoff, b, [&](const u8 *p, int i) { return ReadRawWeight(p, dec.weighttype, i); }));
			for (int k = 0; k < 12; ++k) {
				const float m = gstate.boneMatrix[b * 12 + k];
				bones[b][k] = m == 0.0f || w == 0.0f ? 0.0f : ProductToFloat24((double)TruncateToFloat24(m) * w);
			}
		}
		auto skin = [&](const float src[3], bool translate, float dst[3]) {
			for (int c = 0; c < 3; ++c) {
				float acc = 0.0f;
				auto add = [&](float term) {
					if (term != 0.0f)
						acc = acc == 0.0f ? TruncateToFloat24(term) : TruncateToFloat24(GEAdd(acc, term));
				};
				for (int b = 0; b < nweights; ++b) {
					if (translate)
						add(bones[b][9 + c]);
					for (int j = 0; j < 3; ++j)
						add(GEProduct(TruncateToFloat24(src[j]), bones[b][j * 3 + c]).Value());
				}
				dst[c] = acc;
			}
		};
		if (dec.pos) {
			float pos[3], skinned[3];
			for (int i = 0; i < 3; ++i)
				pos[i] = component(dec.posoff, i, [&](const u8 *p, int c) { return ReadRawComponent(p, dec.pos, c); });
			skin(pos, true, skinned);
			memcpy(out + fmt.posoff, skinned, sizeof(skinned));
		}
		if (dec.nrm && fmt.nrmfmt == DEC_FLOAT_3) {
			float nrm[3], skinned[3];
			for (int i = 0; i < 3; ++i)
				nrm[i] = component(dec.nrmoff, i, [&](const u8 *p, int c) { return ReadRawComponent(p, dec.nrm, c); });
			skin(nrm, false, skinned);
			memcpy(out + fmt.nrmoff, skinned, sizeof(skinned));
		}
	}
}

// A vertex color channel (0 r, 1 g, 2 b, 3 a) expanded to 8 bits.
static float ReadRawColorChannel(const u8 *p, int fmt, int c) {
	u16 v16;
	memcpy(&v16, p, 2);
	switch (fmt) {
	case GE_VTYPE_COL_565 >> GE_VTYPE_COL_SHIFT: {
		const u32 c8 = RGB565ToRGBA8888(v16);
		return (float)((c8 >> (8 * c)) & 0xFF);
	}
	case GE_VTYPE_COL_5551 >> GE_VTYPE_COL_SHIFT: {
		const u32 c8 = RGBA5551ToRGBA8888(v16);
		return (float)((c8 >> (8 * c)) & 0xFF);
	}
	case GE_VTYPE_COL_4444 >> GE_VTYPE_COL_SHIFT: {
		const u32 c8 = RGBA4444ToRGBA8888(v16);
		return (float)((c8 >> (8 * c)) & 0xFF);
	}
	case GE_VTYPE_COL_8888 >> GE_VTYPE_COL_SHIFT:
		return (float)p[c];
	default:
		return 0.0f;
	}
}

// Morphing as the GE does it (gpu/probe exp67, exp107): each frame's value times its weight as a
// float24, summed in frame order with the GE's adder. Positions and normals as read; UVs too (8 and
// 16 bit ones unsigned); colors per channel expanded to 8 bits, the sum floored. Overwrites the
// decoder's float result.
static void ApplyGEMorph(u8 *decoded, const VertexDecoder &dec, const u8 *raw, int count) {
	const DecVtxFormat &fmt = dec.GetDecVtxFmt();
	auto morphWith = [&](const u8 *in, int off, int n, float *dst, auto read) {
		for (int c = 0; c < n; ++c)
			dst[c] = GEMorphComponent(dec, in, off, c, read);
	};
	auto morph = [&](const u8 *in, int off, int cfmt, float dst[3]) {
		morphWith(in, off, 3, dst, [&](const u8 *p, int c) { return ReadRawComponent(p, cfmt, c); });
	};
	for (int v = 0; v < count; ++v) {
		const u8 *in = raw + v * dec.VertexSize();
		u8 *out = decoded + v * fmt.stride;
		float r[3];
		if (dec.pos) {
			morph(in, dec.posoff, dec.pos, r);
			memcpy(out + fmt.posoff, r, sizeof(r));
		}
		if (dec.nrm && fmt.nrmfmt == DEC_FLOAT_3) {
			morph(in, dec.nrmoff, dec.nrm, r);
			memcpy(out + fmt.nrmoff, r, sizeof(r));
		}
		if (dec.tc && fmt.uvfmt == DEC_FLOAT_2) {
			float uv[2];
			morphWith(in, dec.tcoff, 2, uv, [&](const u8 *p, int c) { return ReadRawWeight(p, dec.tc, c); });
			memcpy(out + fmt.uvoff, uv, sizeof(uv));
		}
		if (dec.col && fmt.c0fmt == DEC_U8_4) {
			float ch[4];
			morphWith(in, dec.coloff, 4, ch, [&](const u8 *p, int c) { return ReadRawColorChannel(p, dec.col, c); });
			for (int c = 0; c < 4; ++c)
				out[fmt.c0off + c] = (u8)std::clamp((int)std::floor(ch[c]), 0, 255);
		}
	}
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
			const UVScale uvScale = UsesGEUVScale(vertex_type) ? UVScale{ 1.0f, 1.0f, 0.0f, 0.0f } : LoadUVScaleOffset(gstate);
			vdecoder.DecodeVerts(base, (const u8 *)vertices + vdecoder.VertexSize() * lowerBound_, &uvScale, count);
			const u8 *raw = (const u8 *)vertices + vdecoder.VertexSize() * lowerBound_;
			if (vdecoder.morphcount > 1 && !vdecoder.throughmode)
				ApplyGEMorph(base, vdecoder, raw, count);
			if (vdecoder.weighttype != 0 && !vdecoder.throughmode)
				ApplyGESkinning(base, vdecoder, raw, count);
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
			if (Rasterizer::DetectRectangleFromPair(binner_->State(), buf, &tl, &br) && Rasterizer::RectangleMatchesTriangles(binner_->State(), buf[tl].v, buf[br].v)) {
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
					if (Rasterizer::DetectRectangleFromStrip(binner_->State(), data_, &tl, &br) && Rasterizer::RectangleMatchesTriangles(binner_->State(), data_[tl].v, data_[br].v)) {
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
				// Odd triangles reach us in the opposite order from the GE's (gpu/probe exp45).
				SendTriangle(altCullType, &data_[0], provoking_index, wind != 0);
			}

			// If this is from immediate-mode drawing, we always had one new vert (already in data_.)
			if (isImmDraw_ && data_index_ >= 3) {
				int provoking_index = (data_index_ - 1) % 3;
				int wind = (data_index_ - 1) % 2;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				// Odd triangles reach us in the opposite order from the GE's (gpu/probe exp45).
				SendTriangle(altCullType, &data_[0], provoking_index, wind != 0);
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
				if (Rasterizer::DetectRectangleFromFan(binner_->State(), data_, &tl, &br) && Rasterizer::RectangleMatchesTriangles(binner_->State(), data_[tl].v, data_[br].v)) {
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
				// Odd triangles reach us in the opposite order from the GE's (gpu/probe exp45).
				SendTriangle(altCullType, &data_[0], provoking_index, wind != 0);
			}

			// If this is from immediate-mode drawing, we always had one new vert (already in data_.)
			if (isImmDraw_ && data_index_ >= 3) {
				int wind = (data_index_ - 1) % 2;
				int provoking_index = 2 - wind;
				CullType altCullType = cullType == CullType::OFF ? cullType : CullType((int)cullType ^ wind);
				SendTriangle(altCullType, &data_[0], provoking_index, wind != 0);
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

	// Hm, shouldn't we mask away the position bits of the vertType?
	uint32_t vertTypeID = GetVertTypeID(gstate.vertType | GE_VTYPE_POS_FLOAT, gstate.getUVGenMode());
	// This now processes the step with shared logic, given the existing data_.
	isImmDraw_ = true;
	Clipper::SetCullXY(false);
	SubmitPrimitive(nullptr, nullptr, GE_PRIM_KEEP_PREVIOUS, 0, vertTypeID, nullptr, drawEngine);
	Clipper::SetCullXY(true);
	isImmDraw_ = false;
}

void TransformUnit::SendTriangle(CullType cullType, const ClipVertexData *verts, int provoking, bool orderReversed) {
	if (cullType == CullType::OFF) {
		Clipper::ProcessTriangle(verts[0], verts[1], verts[2], verts[provoking], *binner_, orderReversed);
		Clipper::ProcessTriangle(verts[2], verts[1], verts[0], verts[provoking], *binner_, !orderReversed);
	} else if (cullType == CullType::CW) {
		Clipper::ProcessTriangle(verts[2], verts[1], verts[0], verts[provoking], *binner_, !orderReversed);
	} else {
		Clipper::ProcessTriangle(verts[0], verts[1], verts[2], verts[provoking], *binner_, orderReversed);
	}
}

void TransformUnit::Flush(GPUCommon *common, const char *reason) {
	if (!hasDraws_)
		return;

	binner_->Flush(reason);
	common->NotifyFlush();
	hasDraws_ = false;
}

void TransformUnit::GetStats(StringWriter &w) {
	// TODO: More stats?
	binner_->GetStats(w);
}

void TransformUnit::FlushIfOverlap(GPUCommon *common, const char *reason, bool modifying, uint32_t addr, uint32_t stride, uint32_t w, uint32_t h) {
	if (!hasDraws_)
		return;

	if (binner_->HasPendingWrite(addr, stride, w, h))
		Flush(common, reason);
	if (modifying && binner_->HasPendingRead(addr, stride, w, h))
		Flush(common, reason);
}

void TransformUnit::NotifyTexFlush() {
	binner_->NotifyTexFlush();
}

void TransformUnit::NotifyClutUpdate(const void *src) {
	binner_->UpdateClut(src);
}
