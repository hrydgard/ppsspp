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

#include <atomic>
#include <cfloat>
#include <climits>
#include <cmath>
#include <functional>
#include <thread>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Math/math_util.h"
#include "Common/MemoryUtil.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
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

// Chunks of a run's vertices for helper threads to take alongside the emulation thread (StartRun). A chunk is
// claimed by bumping the low half of claim, which only works with the run's generation in the high half, so a
// helper can't take one from a run that's over.
struct TransformUnit::RunJob {
	std::atomic<uint64_t> claim{ 0 };
	std::atomic<int> chunks{ 0 };
	std::atomic<int> done{ 0 };
	// The helper threads with a task (a bit each), and the tasks queued or running, which use this.
	std::atomic<uint32_t> slots{ 0 };
	std::atomic<int> active{ 0 };
	uint32_t gen = 0;
	int maxHelpers = 0;
	std::function<void(int)> work;
	// The run each chunk was last done for.
	std::vector<std::atomic<uint32_t>> chunkGen;

	// Takes and does a chunk of run myGen. False if there are none left to take.
	bool HelpOne(uint32_t myGen) {
		uint64_t v = claim.load(std::memory_order_acquire);
		while ((uint32_t)(v >> 32) == myGen && (int)(uint32_t)v < chunks.load(std::memory_order_relaxed)) {
			if (!claim.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel))
				continue;
			const int chunk = (int)(uint32_t)v;
			work(chunk);
			chunkGen[chunk].store(myGen, std::memory_order_release);
			done.fetch_add(1, std::memory_order_release);
			return true;
		}
		return false;
	}
	void Help(uint32_t myGen) {
		while (HelpOne(myGen)) {
		}
	}
	// A run after lastGen with chunks left to take, within the time a helper waits for one.
	bool WaitForRun(uint32_t lastGen, uint32_t *newGen) {
		if (!BinManager::THREADS_LINGER)
			return false;
		const double until = time_now_d() + LINGER_SECONDS;
		do {
			const uint64_t v = claim.load(std::memory_order_acquire);
			if ((uint32_t)(v >> 32) != lastGen && (int)(uint32_t)v < chunks.load(std::memory_order_relaxed)) {
				*newGen = (uint32_t)(v >> 32);
				return true;
			}
			for (int i = 0; i < 64; ++i)
				std::this_thread::yield();
		} while (time_now_d() < until);
		return false;
	}
	void SpawnHelper(uint32_t myGen);

	// How long a helper waits for the next run: waking it again costs the emulation thread a system call
	// (picked under WSL2, see RUN_MIN_VERTICES).
	static constexpr double LINGER_SECONDS = 300e-6;
};

class TransformRunTask : public Task {
public:
	TransformRunTask(TransformUnit::RunJob *job, uint32_t gen, int slot) : job_(job), gen_(gen), slot_(slot) {}
	TaskType Type() const override { return TaskType::CPU_COMPUTE; }
	TaskPriority Priority() const override { return TaskPriority::HIGH; }
	void Run() override {
		// The next helper: waking a thread is a system call, kept off the emulation thread.
		job_->SpawnHelper(gen_);
		uint32_t gen = gen_;
		do {
			job_->Help(gen);
		} while (job_->WaitForRun(gen, &gen));
		job_->slots.fetch_and(~(1U << slot_), std::memory_order_release);
		job_->active.fetch_sub(1, std::memory_order_release);
	}

private:
	TransformUnit::RunJob *job_;
	uint32_t gen_;
	int slot_;
};

// A task on a helper thread without one, if there are chunks left.
void TransformUnit::RunJob::SpawnHelper(uint32_t myGen) {
	uint32_t taken = slots.load(std::memory_order_relaxed);
	int slot;
	do {
		const uint64_t v = claim.load(std::memory_order_relaxed);
		if ((uint32_t)(v >> 32) != myGen || (int)(uint32_t)v >= chunks.load(std::memory_order_relaxed))
			return;
		slot = 0;
		while (slot < maxHelpers && (taken & (1U << slot)) != 0)
			slot++;
		if (slot >= maxHelpers)
			return;
	} while (!slots.compare_exchange_weak(taken, taken | (1U << slot), std::memory_order_acq_rel));
	active.fetch_add(1, std::memory_order_relaxed);
	// After the drawing threads, which have their own tasks.
	g_threadManager.EnqueueTaskOnThread(BinManager::MAX_DRAW_THREADS + slot, new TransformRunTask(this, myGen, slot));
}

TransformUnit::TransformUnit() {
	decoded_ = (u8 *)AllocateAlignedMemory(TRANSFORM_BUF_SIZE, 16);
	_assert_(decoded_);
	binner_ = new BinManager();
	runJob_ = new RunJob();
}

TransformUnit::~TransformUnit() {
	FinishRun();
	FreeAlignedMemory(decoded_);
	delete binner_;
	while (runJob_->active.load(std::memory_order_acquire) != 0)
		std::this_thread::yield();
	delete runJob_;
}

SoftwareDrawEngine::SoftwareDrawEngine() : transformDecoders_(32) {
	flushOnParams_ = false;
	// Our DispatchSubmitPrim decodes by itself.
	curvesPredecoded_ = false;
}

SoftwareDrawEngine::~SoftwareDrawEngine() {
	ClearTransformDecoders();
}

void SoftwareDrawEngine::NotifyConfigChanged() {
	// That clears the decoder JIT cache, which these decoders' code is in.
	DrawEngineCommon::NotifyConfigChanged();
	ClearTransformDecoders();
}

void SoftwareDrawEngine::ClearTransformDecoders() {
	transformDecoders_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		delete decoder;
	});
	transformDecoders_.Clear();
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

// Skinning in the decoder was wasted (ApplyGESkinning overwrites what it skins), and in God of War it took 4% of
// the emulation thread. The decoders for everything else (bounding boxes, splines) still skin.
VertexDecoder *SoftwareDrawEngine::FindVertexDecoder(u32 vtype) {
	const u32 vertTypeID = GetVertTypeID(vtype, gstate.getUVGenMode());
	VertexDecoder *dec;
	if (transformDecoders_.Get(vertTypeID, &dec))
		return dec;
	VertexDecoderOptions options = decOptions_;
	options.callerSkins = true;
	dec = new VertexDecoder();
	dec->SetVertexType(vertTypeID, options, decJitCache_);
	transformDecoders_.Insert(vertTypeID, dec);
	return dec;
}

// The clip space position from the combined matrix's rows (gpu/probe exp32, exp34, exp42), each component
// summed like a matrix row, the four at once. The position is (x, y, z, 1); the translation is a term of its own.
static inline Vec4F32 GEClipPosition(Vec4F32 pos, const GERowSumRows &rows) {
	return GERowSum4<4>(pos, rows);
}

// The texture coordinates from the 4x3 texture matrix's rows, summed like clip space rows (gpu/probe exp64).
// The source is (x, y, z, 1).
static inline Vec3Packedf GETexGen(Vec4F32 source, const GERowSumRows &rows) {
	alignas(16) float out[4];
	GERowSum4<4>(source, rows).Store(out);
	return Vec3Packedf(out[0], out[1], out[2]);
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
	// The same, taken apart for GEClipPosition, and its columns (a clip coordinate each) for PreparePositions4.
	GERowSumRows clipRows;
	GERowSumEntries clipEntries[4];
	// The world matrix's rows, for the normal: each component summed like a matrix row (gpu/probe exp61).
	GERowSumRows worldNormalRows;
	Vec4f posToFog;
	// With finite fog parameters, the GE's own arithmetic (gpu/probe exp20): the view z as a row of the
	// combined world-view matrix, then float24(GEAdd(z, end) * slope).
	bool fogGE;
	GERowSumRows viewZRows;
	GERowSumEntries viewZEntries;
	float fogEnd;
	float fogSlope;
	Vec3f screenScale;
	Vec3f screenAdd;
	// The same in lanes 0-2 for GEViewport3, when all are finite float24s.
	bool viewport3;
	Vec4F32 screenScale4;
	Vec4F32 screenAdd4;

	bool depthClamp;
	// The position stage can be done four vertices at a time (PreparePositions4).
	bool prepare4;

	// The texture matrix's rows, for GETexGen.
	GERowSumRows tgenRows;

	struct {
		bool enableTransform : 1;
		bool enableLighting : 1;
		bool enableFog : 1;
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
	state->geUVScale = !vreader.isThrough() && gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS && !gstate.isModeClear() && gstate.isTextureMapEnabled();
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
		alignas(16) float viewDir[4] = { gstate.viewMatrix[2], gstate.viewMatrix[5], gstate.viewMatrix[8], 0.0f };
		Vec4F32 dir = Vec4F32::Load(viewDir);
		if (GENormalize4(dir) == 0.0f) {
			viewDir[0] = viewDir[1] = 0.0f;
			viewDir[2] = 1.0f;
			dir = Vec4F32::Load(viewDir);
		}
		state->lightingState.viewDir = dir;

		if (state->uvGenMode == GE_TEXMAP_TEXTURE_MATRIX) {
			// Row k is m[3k..3k+2], padded so the last load stays inside.
			alignas(16) float padded[13];
			memcpy(padded, gstate.tgenMatrix, 12 * sizeof(float));
			padded[12] = 0.0f;
			Vec4F32 rows[4];
			for (int k = 0; k < 4; ++k)
				rows[k] = Vec4F32::Load(padded + 3 * k);
			state->tgenRows.Set(rows);
		}

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
		const float *m = state->matrix;
		const Vec4F32 clipRows[4] = { Vec4F32::Load(m), Vec4F32::Load(m + 4), Vec4F32::Load(m + 8), Vec4F32::Load(m + 12) };
		state->clipRows.Set(clipRows);
		for (int r = 0; r < 4; ++r) {
			const float column[4] = { m[r], m[4 + r], m[8 + r], m[12 + r] };
			state->clipEntries[r].Set(column);
		}
		// Row k is m[3k..3k+2]; the fourth lane reads the next row's first entry and goes unused, as does the
		// normal's.
		const float *w = gstate.worldMatrix;
		const Vec4F32 normalRows[4] = { Vec4F32::Load(w), Vec4F32::Load(w + 3), Vec4F32::Load(w + 6), Vec4F32::Zero() };
		state->worldNormalRows.Set(normalRows);

		if (state->enableFog) {
			float fogEnd = getFloat24(gstate.fog1);
			float fogSlope = getFloat24(gstate.fog2);

			// We bake fog end and slope into the dot product.
			state->posToFog = Vec4f(worldview[2], worldview[6], worldview[10], worldview[14] + fogEnd);
			state->fogGE = !my_isnanorinf(fogEnd) && !my_isnanorinf(fogSlope);
			Vec4F32 viewZRows[4];
			for (int i = 0; i < 4; ++i)
				viewZRows[i] = Vec4F32::Splat(worldview[2 + 4 * i]);
			state->viewZRows.Set(viewZRows);
			const float viewZ[4] = { worldview[2], worldview[6], worldview[10], worldview[14] };
			state->viewZEntries.Set(viewZ);
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
		alignas(16) const float scale4[4] = { state->screenScale.x, state->screenScale.y, state->screenScale.z, 0.0f };
		alignas(16) const float add4[4] = { state->screenAdd.x, state->screenAdd.y, state->screenAdd.z, 0.0f };
		state->screenScale4 = Vec4F32::Load(scale4);
		state->screenAdd4 = Vec4F32::Load(add4);
		state->viewport3 = true;
		for (int i = 0; i < 3; ++i) {
			if (!std::isfinite(scale4[i]) || !std::isfinite(add4[i]) || TruncateToFloat24(scale4[i]) != scale4[i] || TruncateToFloat24(add4[i]) != add4[i])
				state->viewport3 = false;
		}
	}

	state->depthClamp = gstate.isDepthClipEnabled();
	state->prepare4 = state->enableTransform && state->viewport3 && (!state->enableFog || state->fogGE);
}

// GEViewport of x, y and z (lanes 0-2) at once, z not yet floored. False for the inputs GEViewport doesn't
// take the GE's arithmetic for: a non-finite component, or a w that's zero or denormal.
static inline bool GEViewport3(Vec4F32 clip, const TransformState &state, float out[4]) {
	alignas(16) static const int wLane[4] = { 0, 0, 0, -1 };
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 e = Vec4S32FromBits(clip) & expMask;
	if (AnyCompareBitsSet(e.CompareEq(expMask) | (e.CompareEq(Vec4S32::Zero()) & Vec4S32::LoadAligned(wLane))))
		return false;
	const float recip = GERecip(TruncateToFloat24(clip.GetLane<3>()));
	// A zero gives +0 rather than a signed one, which GEAdd doesn't tell apart. Lane 3 ends up zero.
	const Vec4F32 ndc = GEMulFloat24x4(clip, Vec4F32::Splat(recip));
	GEAdd4(GEMulFloat24x4(ndc, state.screenScale4), state.screenAdd4).Store(out);
	return true;
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

// floorf of each lane, for finite ones. A -0 comes out +0.
static inline Vec4F32 Floor4(Vec4F32 v) {
	const Vec4F32 t = Vec4F32FromS32(Vec4S32FromF32(v));
	const Vec4F32 floored = t - Vec4F32FromBits(t.CompareGt(v) & Vec4S32FromBits(Vec4F32::Splat(1.0f)));
	// From 2^23 on, every float is an integer, and the conversion may overflow.
	const Vec4S32 big = Vec4F32FromBits(Vec4S32FromBits(v) & Vec4S32::Splat(0x7FFFFFFF)).CompareGe(Vec4F32::Splat(8388608.0f));
	return Select(big, v, floored);
}

// ReadVertex's position stage (the clip coordinates, the screen position and fog) for four vertices at once,
// a lane each, from their model positions (x, y, z, 1). False when a lane needs what only ReadVertex does: a
// non-finite clip coordinate, or a w that's zero, denormal or too large for GERecip's usual path. The outputs
// are then undefined. Only with state.prepare4.
static bool PreparePositions4(const Vec4F32 pos[4], const TransformState &state, TransformUnit::PreparedPosition out[4]) {
	Vec4F32 a[4] = { pos[0], pos[1], pos[2], pos[3] };
	Vec4F32::Transpose(a[0], a[1], a[2], a[3]);
	GERowSumLeft left[4];
	for (int k = 0; k < 4; ++k)
		left[k] = GERowSumLeft::From(a[k]);
	Vec4F32 clip[4];
	for (int r = 0; r < 4; ++r)
		clip[r] = GERowSumLanes<4>(left, a, state.clipEntries[r]);

	// GEViewport for x, y and z.
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	Vec4S32 bad = Vec4S32::Zero();
	for (int r = 0; r < 4; ++r)
		bad = bad | (Vec4S32FromBits(clip[r]) & expMask).CompareEq(expMask);
	const Vec4F32 recip = GERecip4(clip[3], bad);
	if (AnyCompareBitsSet(bad))
		return false;
	Vec4F32 scaled[3];
	for (int c = 0; c < 3; ++c) {
		const Vec4F32 ndc = GEMulFloat24x4(clip[c], recip);
		scaled[c] = GEAdd4(GEMulFloat24x4(ndc, Vec4F32::Splat(state.screenScale[c])), Vec4F32::Splat(state.screenAdd[c]));
	}
	Vec4F32 z = Floor4(scaled[2]);

	// ClipToScreenInternal.
	const Vec4F32 bound = Vec4F32::Splat(4095.0f + (15.5f / 16.0f));
	const Vec4F32 zero = Vec4F32::Zero();
	const Vec4S32 insideXY = scaled[0].CompareGe(zero) & scaled[1].CompareGe(zero) & scaled[1].CompareLt(bound);
	Vec4S32 outside;
	if (state.depthClamp) {
		// Not for a vertex the near plane clips away.
		const Vec4F32 negW = Vec4F32FromBits(Vec4S32FromBits(clip[3]) ^ Vec4S32::Splat((int)0x80000000));
		outside = ((insideXY & scaled[0].CompareLt(bound)) ^ Vec4S32::Splat(-1)).AndNot(clip[2].CompareLt(negW));
		z = z.Max(zero).Min(Vec4F32::Splat(65535.0f));
	} else {
		outside = (insideXY & scaled[0].CompareLe(bound) & z.CompareGe(zero) & z.CompareLt(Vec4F32::Splat(65536.0f))) ^ Vec4S32::Splat(-1);
	}
	alignas(16) int xs[4], ys[4], zs[4], outs[4];
	(Vec4S32FromF32(Floor4(scaled[0] * 16.0f)) - Vec4S32::Splat(gstate.getOffsetX16())).StoreAligned(xs);
	(Vec4S32FromF32(Floor4(scaled[1] * 16.0f)) - Vec4S32::Splat(gstate.getOffsetY16())).StoreAligned(ys);
	(Vec4S32FromF32(z) & Vec4S32::Splat(0xFFFF)).StoreAligned(zs);
	outside.StoreAligned(outs);

	alignas(16) float fog[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (state.enableFog) {
		// The GE's fog (state.prepare4 leaves the other kind to ReadVertex).
		const Vec4F32 viewZ = GERowSumLanes<4>(left, a, state.viewZEntries);
		Vec4F32 f = GEMulFloat24x4(TruncateToFloat24x4(GEAdd4(viewZ, Vec4F32::Splat(state.fogEnd))), Vec4F32::Splat(state.fogSlope));
		f.StoreAligned(fog);
		for (int i = 0; i < 4; ++i)
			fog[i] = GEFogFactor(fog[i]) * (1.0f / 256.0f);
	}

	Vec4F32::Transpose(clip[0], clip[1], clip[2], clip[3]);
	for (int i = 0; i < 4; ++i) {
		clip[i].Store(out[i].clippos.AsArray());
		out[i].screenpos = ScreenCoords(xs[i], ys[i], (u16)zs[i]);
		out[i].fogdepth = fog[i];
		out[i].outside = outs[i] != 0;
		out[i].valid = true;
	}
	return true;
}

// Writes straight into the destination: building the vertex in a temporary and copying it reloads its
// fields with wider loads than they were stored with, which stalls.
void TransformUnit::ReadVertex(const VertexReader &vreader, const TransformState &state, VertexCarry &carry, ClipVertexData &vertex, const PreparedPosition *prepared) {
	PROFILE_THIS_SCOPE("read_vert");

	// (x, y, z, 1), the left operands of the transform's rows.
	Vec4F32 pos = vreader.ReadPosOne();

	// A format without UVs uses the last ones read, by any draw, textured or not. They're kept as read, and
	// scaled with the scale and offset of the draw using them (gpu/vertices/carry).
	if (vreader.hasUV()) {
		// Through a local: storing the two floats and reloading them as one stalls.
		float uv[2];
		vreader.ReadUV(uv);
		carry.tc = Vec3Packedf(uv[0], uv[1], 0.0f);
		vertex.v.texturecoords = Vec3Packedf(uv[0], uv[1], 0.0f);
	} else {
		vertex.v.texturecoords = carry.tc;
	}
	if (state.geUVScale) {
		// The decoder only normalized them (8 and 16 bit UVs are unsigned).
		for (int i = 0; i < 2; ++i) {
			const float scaled = ProductToFloat24((double)TruncateToFloat24(vertex.v.texturecoords[i]) * state.uvScale[i]);
			vertex.v.texturecoords[i] = TruncateToFloat24(GEAdd(scaled, state.uvOffset[i]));
		}
	}

	// Carried the same way (gpu/vertices/carry). Lane 3 is undefined.
	if (vreader.hasNormal())
		vreader.ReadNrmF32().Store(carry.normal);
	Vec4F32 normal = Vec4F32::Load(carry.normal);
	if (state.negateNormals)
		normal = Vec4F32FromBits(Vec4S32FromBits(normal) ^ Vec4S32::Splat((int)0x80000000));

	if (vreader.hasColor0()) {
		vertex.v.color0 = vreader.ReadColor0_8888();
	} else {
		vertex.v.color0 = gstate.getMaterialAmbientRGBA();
	}

	vertex.v.color1 = 0;

	if (state.enableTransform && prepared && prepared->valid) {
		vertex.clippos = prepared->clippos;
		vertex.v.screenpos = prepared->screenpos;
		if (prepared->outside) {
			vertex.v.screenpos.x = 0x7FFFFFFF;
			return;
		}
		vertex.v.fogdepth = prepared->fogdepth;
	} else if (state.enableTransform) {
		// Clip coordinates with the GE's precision; the Test Drive map depends on it together with the
		// depth math below (#12786).
		Vec4F32 clip = GEClipPosition(pos, state.clipRows);
		clip.Store(vertex.clippos.AsArray());

		alignas(16) float scaled[4];
		if (state.viewport3 && GEViewport3(clip, state, scaled)) {
			scaled[2] = floorf(scaled[2]);
		} else {
			scaled[0] = GEViewport(vertex.clippos.x, vertex.clippos.w, state.screenScale.x, state.screenAdd.x);
			scaled[1] = GEViewport(vertex.clippos.y, vertex.clippos.w, state.screenScale.y, state.screenAdd.y);
			scaled[2] = GEScreenZ(vertex.clippos.z, vertex.clippos.w, state.screenScale.z, state.screenAdd.z);
		}
		const Vec3f screenScaled(scaled[0], scaled[1], scaled[2]);
		bool outside_range_flag = false;
		if (state.depthClamp)
			vertex.v.screenpos = ClipToScreenInternal<true, false>(screenScaled, vertex.clippos, &outside_range_flag);
		else
			vertex.v.screenpos = ClipToScreenInternal<false, false>(screenScaled, vertex.clippos, &outside_range_flag);
		if (outside_range_flag) {
			// We use this, essentially, as the flag.
			vertex.v.screenpos.x = 0x7FFFFFFF;
			return;
		}

		if (state.enableFog && state.fogGE) {
			const float viewZ = GERowSum4<4>(pos, state.viewZRows).GetLane<0>();
			const float f = ProductToFloat24((double)TruncateToFloat24(GEAdd(viewZ, state.fogEnd)) * state.fogSlope);
			vertex.v.fogdepth = GEFogFactor(f) * (1.0f / 256.0f);
		} else if (state.enableFog) {
			vertex.v.fogdepth = GEFogFactor(Dot43(state.posToFog, Vec3f(pos.GetLane<0>(), pos.GetLane<1>(), pos.GetLane<2>()))) * (1.0f / 256.0f);
		} else {
			vertex.v.fogdepth = 1.0f;
		}
	}

	if (state.enableTransform) {
		vertex.v.clipw = vertex.clippos.w;

		// The normal stays as the world matrix leaves it: lighting scales its dot products by the
		// reciprocal length instead (gpu/probe exp3, world matrix cases).
		Vec4F32 worldnormal = Vec4F32::Zero();
		float normalRsqrt = 1.0f;
		if (state.lightingState.usesWorldNormal) {
			worldnormal = GERowSum4<3>(normal, state.worldNormalRows);
			const float len2 = GEDot3(worldnormal, worldnormal);
			if (len2 > 0.0f && std::isfinite(len2)) {
				normalRsqrt = GERsqrt(len2);
			} else if (len2 != 0.0f) {
				static const float zAxis[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
				worldnormal = Vec4F32::Load(zAxis);
			}
			// A zero normal stays zero: no diffuse or specular from any light (gpu/probe exp173; SOCOM
			// UCES01242 has meshes without normals but lit).
		}

		// Time to generate some texture coords.  Lighting will handle shade mapping.
		if (state.uvGenMode == GE_TEXMAP_TEXTURE_MATRIX) {
			Vec4F32 source = pos;
			switch (gstate.getUVProjMode()) {
			case GE_PROJMAP_POSITION:
				source = pos;
				break;

			case GE_PROJMAP_UV:
			{
				alignas(16) const float uv[4] = { vertex.v.texturecoords.u(), vertex.v.texturecoords.v(), 0.0f, 1.0f };
				source = Vec4F32::Load(uv);
				break;
			}

			case GE_PROJMAP_NORMALIZED_NORMAL:
				// This does not use 0, 0, 1 if length is zero.
				source = normal;
				GENormalize4(source);
				source = source.WithLane3One();
				break;

			case GE_PROJMAP_NORMAL:
				source = normal.WithLane3One();
				break;
			}

			// Note that UV scale/offset are not used in this mode.
			vertex.v.texturecoords = GETexGen(source, state.tgenRows);
		} else if (state.uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP) {
			Lighting::GenerateLightST(vertex.v, pos, worldnormal, normalRsqrt, state.lightingState.viewDir);
		}

		PROFILE_THIS_SCOPE("light");
		if (state.enableLighting)
			Lighting::Process(vertex.v, pos, worldnormal, normalRsqrt, state.lightingState);
	} else {
		alignas(16) float posv[4];
		pos.Store(posv);
		vertex.v.screenpos.x = (int)(posv[0] * SCREEN_SCALE_FACTOR);
		vertex.v.screenpos.y = (int)(posv[1] * SCREEN_SCALE_FACTOR);
		vertex.v.screenpos.z = posv[2];
		vertex.v.clipw = 1.0f;
		vertex.v.fogdepth = 1.0f;
	}

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
	const bool hasPos = dec.pos != 0;
	const bool hasNrm = dec.nrm && fmt.nrmfmt == DEC_FLOAT_3;
	// The bone matrices' entries, taken apart for the products.
	GEMulOperand boneOps[8 * 12];
	for (int i = 0; i < nweights * 12; ++i)
		boneOps[i] = GEMulOperand::From(Vec4F32::Splat(gstate.boneMatrix[i]));
	// Four vertices at a time, a lane each; the last group repeats the last vertex.
	for (int v0 = 0; v0 < count; v0 += 4) {
		alignas(16) float weights[8][4];
		alignas(16) float pos[3][4]{}, nrm[3][4]{};
		for (int l = 0; l < 4; ++l) {
			const int v = std::min(v0 + l, count - 1);
			const u8 *in = raw + v * dec.VertexSize();
			auto component = [&](int off, int c, auto read) {
				return dec.morphcount > 1 ? GEMorphComponent(dec, in, off, c, read) : read(in + off, c);
			};
			for (int b = 0; b < nweights; ++b)
				weights[b][l] = TruncateToFloat24(component(dec.weightoff, b, [&](const u8 *p, int i) { return ReadRawWeight(p, dec.weighttype, i); }));
			for (int i = 0; i < 3; ++i) {
				if (hasPos)
					pos[i][l] = component(dec.posoff, i, [&](const u8 *p, int c) { return ReadRawComponent(p, dec.pos, c); });
				if (hasNrm)
					nrm[i][l] = component(dec.nrmoff, i, [&](const u8 *p, int c) { return ReadRawComponent(p, dec.nrm, c); });
			}
		}
		Vec4F32 posv[3], nrmv[3];
		for (int i = 0; i < 3; ++i) {
			posv[i] = Vec4F32::LoadAligned(pos[i]);
			nrmv[i] = Vec4F32::LoadAligned(nrm[i]);
		}

		// Each component of each vertex is a chain of GEAdds in a fixed order: per bone, the translation, then
		// the three products. GEAdd drops a GEProduct's 17th bit, so float24 products give the same sums. A
		// lane whose weight is zero adds zeros, which leave its sums as they are, as skipping the bone would.
		Vec4F32 accPos[3], accNrm[3];
		// Without the fallbacks first, with the operands taken apart once: the fallbacks are rarely needed,
		// and then the group is done again with them.
		Vec4S32 bad = Vec4S32::Zero();
		{
			GEMulOperand posOp[3], nrmOp[3];
			for (int c = 0; c < 3; ++c) {
				accPos[c] = Vec4F32::Zero();
				accNrm[c] = Vec4F32::Zero();
				posOp[c] = GEMulOperand::From(posv[c]);
				nrmOp[c] = GEMulOperand::From(nrmv[c]);
			}
			for (int b = 0; b < nweights; ++b) {
				const Vec4F32 w = Vec4F32::LoadAligned(weights[b]);
				if (!AnyCompareBitsSet(w.CompareEq(Vec4F32::Zero()) ^ Vec4S32::Splat(-1)))
					continue;
				const GEMulOperand wOp = GEMulOperand::From(w);
				const GEMulOperand *m = &boneOps[b * 12];
				for (int c = 0; c < 3; ++c) {
					accPos[c] = GEAddFloat24x4Unchecked(accPos[c], GEMulFloat24x4Unchecked(wOp, m[9 + c], bad), bad);
					for (int j = 0; j < 3; ++j) {
						const GEMulOperand row = GEMulOperand::From(GEMulFloat24x4Unchecked(wOp, m[3 * j + c], bad));
						accPos[c] = GEAddFloat24x4Unchecked(accPos[c], GEMulFloat24x4Unchecked(posOp[j], row, bad), bad);
						if (hasNrm)
							accNrm[c] = GEAddFloat24x4Unchecked(accNrm[c], GEMulFloat24x4Unchecked(nrmOp[j], row, bad), bad);
					}
				}
			}
		}
		if (AnyCompareBitsSet(bad)) {
			for (int c = 0; c < 3; ++c) {
				accPos[c] = Vec4F32::Zero();
				accNrm[c] = Vec4F32::Zero();
			}
			for (int b = 0; b < nweights; ++b) {
				const Vec4F32 w = Vec4F32::LoadAligned(weights[b]);
				if (!AnyCompareBitsSet(w.CompareEq(Vec4F32::Zero()) ^ Vec4S32::Splat(-1)))
					continue;
				// The bone's entries 3j + c (the translation j = 3), times the weights.
				const float *m = gstate.boneMatrix + b * 12;
				for (int c = 0; c < 3; ++c) {
					Vec4F32 rows[4];
					for (int j = 0; j < 4; ++j)
						rows[j] = GEMulFloat24x4(w, Vec4F32::Splat(m[3 * j + c]));
					accPos[c] = GEAddFloat24x4(accPos[c], rows[3]);
					for (int j = 0; j < 3; ++j) {
						accPos[c] = GEAddFloat24x4(accPos[c], GEMulFloat24x4(posv[j], rows[j]));
						if (hasNrm)
							accNrm[c] = GEAddFloat24x4(accNrm[c], GEMulFloat24x4(nrmv[j], rows[j]));
					}
				}
			}
		}

		// Back to a vector per vertex.
		Vec4F32 p[4] = { accPos[0], accPos[1], accPos[2], Vec4F32::Zero() };
		Vec4F32 n[4] = { accNrm[0], accNrm[1], accNrm[2], Vec4F32::Zero() };
		Vec4F32::Transpose(p[0], p[1], p[2], p[3]);
		Vec4F32::Transpose(n[0], n[1], n[2], n[3]);
		alignas(16) float result[4];
		for (int l = 0; l < 4 && v0 + l < count; ++l) {
			u8 *out = decoded + (v0 + l) * fmt.stride;
			if (hasPos) {
				p[l].Store(result);
				memcpy(out + fmt.posoff, result, 3 * sizeof(float));
			}
			if (hasNrm) {
				n[l].Store(result);
				memcpy(out + fmt.nrmoff, result, 3 * sizeof(float));
			}
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

// PreparePositions4 for count vertices from first, in out (which has room for 3 more).
static void PreparePositionRange(VertexReader &vreader, int first, int count, const TransformState &state, TransformUnit::PreparedPosition *out) {
	for (int i = 0; i < count; i += 4) {
		Vec4F32 pos[4];
		for (int j = 0; j < 4; ++j) {
			vreader.Goto(first + std::min(i + j, count - 1));
			pos[j] = vreader.ReadPosOne();
		}
		if (!PreparePositions4(pos, state, &out[i])) {
			for (int j = 0; j < 4; ++j)
				out[i + j].valid = false;
		}
	}
}

class SoftwareVertexReader {
public:
	// With runPos (not negative), the vertices are transformed as part of a run, from there in runVerts_
	// (TransformUnit::StartRun).
	SoftwareVertexReader(u8 *base, VertexDecoder &vdecoder, u32 vertex_type, int vertex_count, const void *vertices, const void *indices, const TransformState &transformState, TransformUnit &transform, int runPos = -1)
	: vreader_(base, vdecoder.GetDecVtxFmt(), vertex_type), conv_(vertex_type, indices), transformState_(transformState), transform_(transform), runPos_(runPos) {
		run_ = runPos >= 0 ? &transform.runVerts_[runPos] : nullptr;
		useIndices_ = indices != nullptr;
		vertexCount_ = vertex_count;
		lowerBound_ = 0;
		upperBound_ = vertex_count == 0 ? 0 : vertex_count - 1;

		if (useIndices_)
			GetIndexBounds(indices, vertex_count, vertex_type, &lowerBound_, &upperBound_);
		if (vertex_count != 0 && !run_) {
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
		if (!useCache_ || run_)
			return;

		// Within a draw, the format has UVs and a normal for every vertex or for none, so the order the
		// cache is filled in doesn't matter.
		TransformUnit::VertexCarry carry = transform_.carry_;
		for (int i = 0; i < upperBound_ - lowerBound_ + 1; ++i) {
			vreader_.Goto(i);
			transform_.ReadVertex(vreader_, transformState_, carry, cached_[i], usePrepared_ ? &prepared_[i] : nullptr);
		}
		// What the next draw carries is the last vertex in draw order (gpu/vertices/carry).
		if (vertexCount_ != 0) {
			vreader_.Goto(useIndices_ ? conv_(vertexCount_ - 1) - lowerBound_ : vertexCount_ - 1);
			ClipVertexData unused;
			transform_.ReadVertex(vreader_, transformState_, transform_.carry_, unused);
		}
	}

	// With the transform state computed: the position stage for all the vertices, four at a time.
	void PreparePositions() {
		usePrepared_ = transformState_.prepare4 && vertexCount_ != 0 && !run_;
		if (!usePrepared_)
			return;
		const int count = upperBound_ - lowerBound_ + 1;
		if ((int)prepared_.size() < count + 3)
			prepared_.resize(std::max(128, count + 3));
		PreparePositionRange(vreader_, 0, count, transformState_, prepared_.data());
	}

	inline void Read(int vtx, ClipVertexData &out) {
		if (run_) {
			transform_.RunNeed(runPos_ + vtx);
			out = run_[vtx];
			return;
		}
		int index = vtx;
		if (useIndices_) {
			index = conv_(vtx) - lowerBound_;
			if (useCache_) {
				out = cached_[index];
				return;
			}
		}
		vreader_.Goto(index);
		transform_.ReadVertex(vreader_, transformState_, transform_.carry_, out, usePrepared_ ? &prepared_[index] : nullptr);
	}

protected:
	VertexReader vreader_;
	const IndexConverter conv_;
	const TransformState &transformState_;
	TransformUnit &transform_;
	int vertexCount_ = 0;
	uint16_t lowerBound_;
	uint16_t upperBound_;
	static std::vector<ClipVertexData> cached_;
	static std::vector<TransformUnit::PreparedPosition> prepared_;
	bool useIndices_ = false;
	bool useCache_ = false;
	bool usePrepared_ = false;
	const ClipVertexData *run_;
	int runPos_;
};

// Static to reduce allocations mid-frame.
std::vector<ClipVertexData> SoftwareVertexReader::cached_;
std::vector<TransformUnit::PreparedPosition> SoftwareVertexReader::prepared_;

// Tuned on a 16 core Ryzen under WSL2, where a thread wake up costs the waker about 15 us and takes about 50 us
// to arrive: on bare metal, other values may pay off. Below this, a run's vertices are transformed a draw at a
// time. Even without helpers, a run is faster than its draws one by one (16 beat 32, 64 and 128 in God of War).
static constexpr int RUN_MIN_VERTICES = 16;
// Vertices per chunk of a run.
static constexpr int RUN_CHUNK = 32;
// Helper threads for a run, besides this one: in God of War, two beat one, three and four.
static constexpr int RUN_MAX_HELPERS = 2;

static TransformState transformState;

// Decodes a run of draws' vertices (runCount from vertices, possibly a single draw's), and starts transforming
// them into runVerts_, in chunks that helper threads share. The draws take each vertex as it's done (RunNeed),
// drawing what they can meanwhile. With the transform state as it is for all of them: the draws in between change
// nothing (SoftGPU::RunVertexCount). False if it doesn't apply.
bool TransformUnit::StartRun(const void *vertices, u32 vertexType, int runCount, VertexDecoder &vdecoder, TransformState &state) {
	const DecVtxFormat &fmt = vdecoder.GetDecVtxFmt();
	if (runCount < RUN_MIN_VERTICES || (size_t)runCount * fmt.stride > TRANSFORM_BUF_SIZE || vdecoder.throughmode)
		return false;
	const u8 *raw = (const u8 *)vertices;
	const UVScale uvScale = UsesGEUVScale(vertexType) ? UVScale{ 1.0f, 1.0f, 0.0f, 0.0f } : LoadUVScaleOffset(gstate);
	vdecoder.DecodeVerts(decoded_, raw, &uvScale, runCount);
	if (vdecoder.morphcount > 1)
		ApplyGEMorph(decoded_, vdecoder, raw, runCount);

	VertexReader reader(decoded_, fmt, vertexType);
	binner_->UpdateState();
	if (binner_->HasDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL)) {
		ComputeTransformState(&state, reader);
		binner_->ClearDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL);
	}

	if ((int)runVerts_.size() < runCount)
		runVerts_.resize(runCount);
	RunJob &job = *runJob_;
	// The new run first, with no chunks to take until it's set up: a helper still looking at an earlier run could
	// otherwise take one with this run's count, done as that run's, and RunNeed would wait for it forever.
	job.gen++;
	job.claim.store(((uint64_t)job.gen << 32) | 0x7FFFFFFF, std::memory_order_release);
	const VertexCarry carry = carry_;
	job.work = [this, &vdecoder, raw, &state, carry](int chunk) {
		TransformRunChunk(chunk, vdecoder, raw, state, carry);
	};
	runCount_ = runCount;
	const int chunks = (runCount + RUN_CHUNK - 1) / RUN_CHUNK;
	job.maxHelpers = std::min({ RUN_MAX_HELPERS, 31, g_threadManager.GetNumLooperThreads() - BinManager::MAX_DRAW_THREADS });
	if ((int)job.chunkGen.size() < chunks) {
		// No helper uses it: FinishRun waited for every chunk taken.
		std::vector<std::atomic<uint32_t>> grown(chunks * 2);
		for (auto &g : grown)
			g.store(0, std::memory_order_relaxed);
		job.chunkGen.swap(grown);
	}
	job.chunks.store(chunks, std::memory_order_relaxed);
	job.done.store(0, std::memory_order_relaxed);
	job.claim.store((uint64_t)job.gen << 32, std::memory_order_release);
	// Helpers waiting for a run take this one without a wake up.
	if (job.maxHelpers > 0 && chunks > 1)
		job.SpawnHelper(job.gen);
	runActive_ = true;

	runNext_ = raw;
	runType_ = vertexType;
	runPos_ = 0;
	runRemaining_ = runCount;
	return true;
}

// Makes sure a vertex of the run (an index into runVerts_) is transformed, doing other chunks while its isn't.
void TransformUnit::RunNeed(int index) {
	RunJob &job = *runJob_;
	const int chunk = index / RUN_CHUNK;
	while (job.chunkGen[chunk].load(std::memory_order_acquire) != job.gen) {
		if (!job.HelpOne(job.gen))
			std::this_thread::yield();
	}
}

// Waits for all of the run's chunks: they use decoded_, the transform state and gstate.
void TransformUnit::FinishRun() {
	if (!runActive_)
		return;
	RunJob &job = *runJob_;
	job.Help(job.gen);
	while (job.done.load(std::memory_order_acquire) < job.chunks.load(std::memory_order_relaxed))
		std::this_thread::yield();
	runActive_ = false;
}

// A chunk of a run: what a draw would do with its vertices, from skinning to ReadVertex.
void TransformUnit::TransformRunChunk(int chunk, const VertexDecoder &vdecoder, const u8 *raw, const TransformState &state, const VertexCarry &carry) {
	const int first = chunk * RUN_CHUNK;
	const int count = std::min(RUN_CHUNK, runCount_ - first);
	const DecVtxFormat &fmt = vdecoder.GetDecVtxFmt();
	if (vdecoder.weighttype != 0)
		ApplyGESkinning(decoded_ + first * fmt.stride, vdecoder, raw + first * vdecoder.VertexSize(), count);
	VertexReader reader(decoded_, fmt, runType_);
	PreparedPosition prepared[RUN_CHUNK + 3];
	if (state.prepare4)
		PreparePositionRange(reader, first, count, state, prepared);
	// Within a draw, the format has UVs and a normal for every vertex or for none, so each chunk can start from
	// the carry the run started with.
	VertexCarry chunkCarry = carry;
	for (int i = 0; i < count; ++i) {
		reader.Goto(first + i);
		ReadVertex(reader, state, chunkCarry, runVerts_[first + i], state.prepare4 ? &prepared[i] : nullptr);
	}
}

void TransformUnit::SubmitPrimitive(const void* vertices, const void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine, int runCount)
{
	VertexDecoder &vdecoder = *drawEngine->FindVertexDecoder(vertex_type);

	if (bytesRead)
		*bytesRead = vertex_count * vdecoder.VertexSize();

	// Frame skipping.
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		runRemaining_ = 0;
		FinishRun();
		return;
	}
	// Vertices without position are just entirely culled.
	// Note: Throughmode does draw 8-bit primitives, but positions are always zero - handled in decode.
	if ((vertex_type & GE_VTYPE_POS_MASK) == 0) {
		runRemaining_ = 0;
		FinishRun();
		return;
	}

	// Part of a run of draws transformed together (StartRun)?
	bool fromRun = !indices && InRun(vertices, vertex_type) && vertex_count <= runRemaining_ && !binner_->HasDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL);
	if (!fromRun) {
		runRemaining_ = 0;
		FinishRun();
		if (!indices && runCount >= vertex_count)
			fromRun = StartRun(vertices, vertex_type, runCount, vdecoder, transformState);
	}
	SoftwareVertexReader vreader(decoded_, vdecoder, vertex_type, vertex_count, vertices, indices, transformState, *this, fromRun ? runPos_ : -1);
	// After the draw: its last vertex is transformed the latest.
	auto advanceRun = [&]() {
		if (!fromRun)
			return;
		// What the next draw carries is this one's last vertex's (gpu/vertices/carry), as reading it would leave it.
		RunNeed(runPos_ + vertex_count - 1);
		VertexReader last(decoded_, vdecoder.GetDecVtxFmt(), vertex_type);
		last.Goto(runPos_ + vertex_count - 1);
		if (last.hasUV()) {
			float uv[2];
			last.ReadUV(uv);
			carry_.tc = Vec3Packedf(uv[0], uv[1], 0.0f);
		}
		if (last.hasNormal())
			last.ReadNrmF32().Store(carry_.normal);
		runPos_ += vertex_count;
		runRemaining_ -= vertex_count;
		runNext_ = (const u8 *)vertices + vertex_count * vdecoder.VertexSize();
		if (runRemaining_ == 0)
			FinishRun();
	};

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
	vreader.PreparePositions();
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
			vreader.Read(vtx, buf[buf_index++]);
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
			vreader.Read(vtx, data_[0]);
			Clipper::ProcessPoint(data_[0], *binner_);
		}
		break;

	case GE_PRIM_LINES:
		for (int i = 0; i < data_index_ - 1; i += 2)
			Clipper::ProcessLine(data_[i + 0], data_[i + 1], *binner_);
		data_index_ &= 1;
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			vreader.Read(vtx, data_[data_index_++]);
			if (data_index_ == 2) {
				Clipper::ProcessLine(data_[0], data_[1], *binner_);
				data_index_ = 0;
			}
		}
		break;

	case GE_PRIM_TRIANGLES:
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			vreader.Read(vtx, data_[data_index_++]);
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
			vreader.Read(vtx, data_[data_index_++]);

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
				vreader.Read(vtx, data_[(data_index_++) & 1]);

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
						vreader.Read(base + vtx, data_[vtx]);
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
				vreader.Read(vtx, data_[provoking_index]);
				--skip_count;
				++start_vtx;
			}

			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				int provoking_index = (data_index_++) % 3;
				vreader.Read(vtx, data_[provoking_index]);

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
				vreader.Read(0, data_[0]);
				data_index_++;
				start_vtx = 1;
			}

			if (data_index_ == 1 && vertex_count == 4 && cullType == CullType::OFF) {
				for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
					vreader.Read(vtx, data_[vtx]);
				}

				int tl = -1, br = -1;
				if (Rasterizer::DetectRectangleFromFan(binner_->State(), data_, &tl, &br) && Rasterizer::RectangleMatchesTriangles(binner_->State(), data_[tl].v, data_[br].v)) {
					Clipper::ProcessRect(data_[tl], data_[br], *binner_);
					break;
				}
			}

			for (int vtx = start_vtx; vtx < vertex_count && skip_count > 0; ++vtx) {
				int provoking_index = 2 - ((data_index_++) % 2);
				vreader.Read(vtx, data_[provoking_index]);
				--skip_count;
				++start_vtx;
			}

			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				int provoking_index = 2 - ((data_index_++) % 2);
				vreader.Read(vtx, data_[provoking_index]);

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
	advanceRun();
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
