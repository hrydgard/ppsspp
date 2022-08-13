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

#include <cmath>
#include <algorithm>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Math/math_util.h"
#include "Common/MemoryUtil.h"
#include "Common/Profiler/Profiler.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/Lighting.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"
#include "GPU/Software/TransformUnit.h"

#define TRANSFORM_BUF_SIZE (65536 * 48)

TransformUnit::TransformUnit() {
	decoded_ = (u8 *)AllocateMemoryPages(TRANSFORM_BUF_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	binner_ = new BinManager();
}

TransformUnit::~TransformUnit() {
	FreeMemoryPages(decoded_, TRANSFORM_BUF_SIZE);
	delete binner_;
}

SoftwareDrawEngine::SoftwareDrawEngine() {
	// All this is a LOT of memory, need to see if we can cut down somehow.  Used for splines.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
}

SoftwareDrawEngine::~SoftwareDrawEngine() {
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
}

void SoftwareDrawEngine::DispatchFlush() {
	transformUnit.Flush("debug");
}

void SoftwareDrawEngine::DispatchSubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) {
	_assert_msg_(cullMode == gstate.getCullMode(), "Mixed cull mode not supported.");
	transformUnit.SubmitPrimitive(verts, inds, prim, vertexCount, vertTypeID, bytesRead, this);
}

void SoftwareDrawEngine::DispatchSubmitImm(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) {
	_assert_msg_(cullMode == gstate.getCullMode(), "Mixed cull mode not supported.");
	// TODO: For now, just setting all dirty.
	transformUnit.SetDirty(SoftDirty(-1));
	transformUnit.SubmitPrimitive(verts, inds, prim, vertexCount, vertTypeID, bytesRead, this);
	// TODO: Should really clear, but the vertex type is faked so things might need resetting...
	transformUnit.SetDirty(SoftDirty(-1));
}

VertexDecoder *SoftwareDrawEngine::FindVertexDecoder(u32 vtype) {
	const u32 vertTypeID = (vtype & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	return DrawEngineCommon::GetVertexDecoder(vertTypeID);
}

WorldCoords TransformUnit::ModelToWorld(const ModelCoords &coords) {
	return Vec3ByMatrix43(coords, gstate.worldMatrix);
}

WorldCoords TransformUnit::ModelToWorldNormal(const ModelCoords &coords) {
	return Norm3ByMatrix43(coords, gstate.worldMatrix);
}

ViewCoords TransformUnit::WorldToView(const WorldCoords &coords) {
	return Vec3ByMatrix43(coords, gstate.viewMatrix);
}

ClipCoords TransformUnit::ViewToClip(const ViewCoords &coords) {
	return Vec3ByMatrix44(coords, gstate.projMatrix);
}

template <bool depthClamp, bool writeOutsideFlag>
static ScreenCoords ClipToScreenInternal(Vec3f scaled, const ClipCoords &coords, bool *outside_range_flag) {
	ScreenCoords ret;

	// Account for rounding for X and Y.
	// TODO: Validate actual rounding range.
	const float SCREEN_BOUND = 4095.0f + (15.5f / 16.0f);

	// This matches hardware tests - depth is clamped when this flag is on.
	if (depthClamp) {
		// Note: if the depth is clipped (z/w <= -1.0), the outside_range_flag should NOT be set, even for x and y.
		if (writeOutsideFlag && coords.z > -coords.w && (scaled.x >= SCREEN_BOUND || scaled.y >= SCREEN_BOUND || scaled.x < 0 || scaled.y < 0)) {
			*outside_range_flag = true;
		}

		if (scaled.z < 0.f)
			scaled.z = 0.f;
		else if (scaled.z > 65535.0f)
			scaled.z = 65535.0f;
	} else if (writeOutsideFlag && (scaled.x > SCREEN_BOUND || scaled.y >= SCREEN_BOUND || scaled.x < 0 || scaled.y < 0)) {
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
		if (outside_range_flag)
			return ClipToScreenInternal<true, true>(Vec3f(x, y, z), coords, outside_range_flag);
		return ClipToScreenInternal<true, false>(Vec3f(x, y, z), coords, outside_range_flag);
	}
	if (outside_range_flag)
		return ClipToScreenInternal<false, true>(Vec3f(x, y, z), coords, outside_range_flag);
	return ClipToScreenInternal<false, false>(Vec3f(x, y, z), coords, outside_range_flag);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords &coords) {
	return ClipToScreenInternal(coords, nullptr);
}

ScreenCoords TransformUnit::DrawingToScreen(const DrawingCoords &coords, u16 z) {
	ScreenCoords ret;
	ret.x = (u32)coords.x * SCREEN_SCALE_FACTOR;
	ret.y = (u32)coords.y * SCREEN_SCALE_FACTOR;
	ret.z = z;
	return ret;
}

enum class MatrixMode {
	NONE = 0,
	POS_TO_CLIP = 1,
	POS_TO_VIEW = 2,
	WORLD_TO_CLIP = 3,
};

struct TransformState {
	Lighting::State lightingState;

	float fogEnd;
	float fogSlope;

	float matrix[16];
	Vec3f screenScale;
	Vec3f screenAdd;

	ScreenCoords(*roundToScreen)(Vec3f scaled, const ClipCoords &coords, bool *outside_range_flag);

	struct {
		bool enableTransform : 1;
		bool enableLighting : 1;
		bool enableFog : 1;
		bool readUV : 1;
		bool readWeights : 1;
		bool negateNormals : 1;
		uint8_t uvGenMode : 2;
		uint8_t matrixMode : 2;
	};
};

void ComputeTransformState(TransformState *state, const VertexReader &vreader) {
	state->enableTransform = !gstate.isModeThrough();
	state->enableLighting = gstate.isLightingEnabled();
	state->enableFog = gstate.isFogEnabled();
	state->readUV = !gstate.isModeClear() && gstate.isTextureMapEnabled() && vreader.hasUV();
	state->readWeights = vertTypeIsSkinningEnabled(gstate.vertType) && state->enableTransform;
	state->negateNormals = gstate.areNormalsReversed();

	state->uvGenMode = gstate.getUVGenMode();

	if (state->enableTransform) {
		if (state->enableFog) {
			state->fogEnd = getFloat24(gstate.fog1);
			state->fogSlope = getFloat24(gstate.fog2);
			// Same fixup as in ShaderManagerGLES.cpp
			if (my_isnanorinf(state->fogEnd)) {
				state->fogEnd = std::signbit(state->fogEnd) ? -INFINITY : INFINITY;
			}
			if (my_isnanorinf(state->fogSlope)) {
				state->fogSlope = std::signbit(state->fogSlope) ? -INFINITY : INFINITY;
			}
		}

		bool canSkipWorldPos = true;
		bool canSkipViewPos = !state->enableFog;
		if (state->enableLighting) {
			Lighting::ComputeState(&state->lightingState, vreader.hasColor0());
			for (int i = 0; i < 4; ++i) {
				if (!state->lightingState.lights[i].enabled)
					continue;
				if (!state->lightingState.lights[i].directional)
					canSkipWorldPos = false;
			}
		}

		float world[16];
		float view[16];
		if (canSkipWorldPos && canSkipViewPos) {
			state->matrixMode = (uint8_t)MatrixMode::POS_TO_CLIP;

			ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
			ConvertMatrix4x3To4x4(view, gstate.viewMatrix);

			float worldview[16];
			Matrix4ByMatrix4(worldview, world, view);
			Matrix4ByMatrix4(state->matrix, worldview, gstate.projMatrix);
		} else if (canSkipWorldPos) {
			state->matrixMode = (uint8_t)MatrixMode::POS_TO_VIEW;

			ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
			ConvertMatrix4x3To4x4(view, gstate.viewMatrix);

			Matrix4ByMatrix4(state->matrix, world, view);
		} else if (canSkipViewPos) {
			state->matrixMode = (uint8_t)MatrixMode::WORLD_TO_CLIP;

			ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
			Matrix4ByMatrix4(state->matrix, view, gstate.projMatrix);
		} else {
			state->matrixMode = (uint8_t)MatrixMode::NONE;
		}

		state->screenScale = Vec3f(gstate.getViewportXScale(), gstate.getViewportYScale(), gstate.getViewportZScale());
		state->screenAdd = Vec3f(gstate.getViewportXCenter(), gstate.getViewportYCenter(), gstate.getViewportZCenter());
	}

	if (gstate.isDepthClampEnabled())
		state->roundToScreen = &ClipToScreenInternal<true, true>;
	else
		state->roundToScreen = &ClipToScreenInternal<false, true>;
}

VertexData TransformUnit::ReadVertex(VertexReader &vreader, const TransformState &state, bool &outside_range_flag) {
	PROFILE_THIS_SCOPE("read_vert");
	VertexData vertex;

	ModelCoords pos;
	// VertexDecoder normally scales z, but we want it unscaled.
	vreader.ReadPosThroughZ16(pos.AsArray());

	if (state.readUV) {
		vreader.ReadUV(vertex.texturecoords.AsArray());
	}

	Vec3<float> normal;
	if (vreader.hasNormal()) {
		vreader.ReadNrm(normal.AsArray());

		if (state.negateNormals)
			normal = -normal;
	}

	if (state.readWeights) {
		float W[8] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
		vreader.ReadWeights(W);

		Vec3<float> tmppos(0.f, 0.f, 0.f);
		Vec3<float> tmpnrm(0.f, 0.f, 0.f);

		for (int i = 0; i < vertTypeGetNumBoneWeights(gstate.vertType); ++i) {
			Vec3<float> step = Vec3ByMatrix43(pos, gstate.boneMatrix + i * 12);
			tmppos += step * W[i];
			if (vreader.hasNormal()) {
				step = Norm3ByMatrix43(normal, gstate.boneMatrix + i * 12);
				tmpnrm += step * W[i];
			}
		}

		pos = tmppos;
		if (vreader.hasNormal())
			normal = tmpnrm;
	}

	if (vreader.hasColor0()) {
#ifdef _M_SSE
		vreader.ReadColor0_8888((u8 *)vertex.color0.AsArray());
		vertex.color0.ivec = _mm_unpacklo_epi8(vertex.color0.ivec, _mm_setzero_si128());
		vertex.color0.ivec = _mm_unpacklo_epi16(vertex.color0.ivec, _mm_setzero_si128());
#else
		float col[4];
		vreader.ReadColor0(col);
		vertex.color0 = Vec4<int>(col[0]*255, col[1]*255, col[2]*255, col[3]*255);
#endif
	} else {
		vertex.color0 = Vec4<int>::FromRGBA(gstate.getMaterialAmbientRGBA());
	}

#ifdef _M_SSE
	vertex.color1 = _mm_setzero_si128();
#else
	vertex.color1 = Vec3<int>(0, 0, 0);
#endif

	if (state.enableTransform) {
		WorldCoords worldpos;
		ModelCoords viewpos;

		switch (MatrixMode(state.matrixMode)) {
		case MatrixMode::NONE:
			worldpos = TransformUnit::ModelToWorld(pos);
			viewpos = TransformUnit::WorldToView(worldpos);
			vertex.clippos = TransformUnit::ViewToClip(viewpos);
			break;

		case MatrixMode::POS_TO_CLIP:
			vertex.clippos = Vec3ByMatrix44(pos, state.matrix);
			break;

		case MatrixMode::POS_TO_VIEW:
#ifdef _M_SSE
			viewpos = Vec3ByMatrix44(pos, state.matrix).vec;
#else
			viewpos = Vec3ByMatrix44(pos, state.matrix).rgb();
#endif
			vertex.clippos = TransformUnit::ViewToClip(viewpos);
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
		vertex.screenpos = state.roundToScreen(screenScaled, vertex.clippos, &outside_range_flag);
		if (outside_range_flag)
			return vertex;

		if (state.enableFog) {
			vertex.fogdepth = (viewpos.z + state.fogEnd) * state.fogSlope;
		} else {
			vertex.fogdepth = 1.0f;
		}

		Vec3<float> worldnormal;
		if (vreader.hasNormal()) {
			worldnormal = TransformUnit::ModelToWorldNormal(normal);
			worldnormal.NormalizeOr001();
		} else {
			worldnormal = Vec3<float>(0.0f, 0.0f, 1.0f);
		}

		// Time to generate some texture coords.  Lighting will handle shade mapping.
		if (state.uvGenMode == GE_TEXMAP_TEXTURE_MATRIX) {
			Vec3f source;
			switch (gstate.getUVProjMode()) {
			case GE_PROJMAP_POSITION:
				source = pos;
				break;

			case GE_PROJMAP_UV:
				source = Vec3f(vertex.texturecoords, 0.0f);
				break;

			case GE_PROJMAP_NORMALIZED_NORMAL:
				source = normal.NormalizedOr001(cpu_info.bSSE4_1);
				break;

			case GE_PROJMAP_NORMAL:
				source = normal;
				break;

			default:
				source = Vec3f::AssignToAll(0.0f);
				ERROR_LOG_REPORT(G3D, "Software: Unsupported UV projection mode %x", gstate.getUVProjMode());
				break;
			}

			// TODO: What about uv scale and offset?
			Vec3<float> stq = Vec3ByMatrix43(source, gstate.tgenMatrix);
			float z_recip = 1.0f / stq.z;
			vertex.texturecoords = Vec2f(stq.x * z_recip, stq.y * z_recip);
		} else if (state.uvGenMode == GE_TEXMAP_ENVIRONMENT_MAP) {
			Lighting::GenerateLightST(vertex, worldnormal);
		}

		PROFILE_THIS_SCOPE("light");
		if (state.enableLighting)
			Lighting::Process(vertex, worldpos, worldnormal, state.lightingState);
	} else {
		vertex.screenpos.x = (int)(pos[0] * SCREEN_SCALE_FACTOR);
		vertex.screenpos.y = (int)(pos[1] * SCREEN_SCALE_FACTOR);
		vertex.screenpos.z = pos[2];
		vertex.clippos.w = 1.f;
		vertex.fogdepth = 1.f;
	}

	return vertex;
}

void TransformUnit::SetDirty(SoftDirty flags) {
	binner_->SetDirty(flags);
}
SoftDirty TransformUnit::GetDirty() {
	return binner_->GetDirty();
}

enum class CullType {
	CW,
	CCW,
	OFF,
};

void TransformUnit::SubmitPrimitive(const void* vertices, const void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine)
{
	VertexDecoder &vdecoder = *drawEngine->FindVertexDecoder(vertex_type);
	const DecVtxFormat &vtxfmt = vdecoder.GetDecVtxFmt();

	if (bytesRead)
		*bytesRead = vertex_count * vdecoder.VertexSize();

	// Frame skipping.
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		return;
	}
	// Throughmode never draws 8-bit primitives, maybe because they can't fully specify the screen?
	if ((vertex_type & GE_VTYPE_THROUGH_MASK) != 0 && (vertex_type & GE_VTYPE_POS_MASK) == GE_VTYPE_POS_8BIT)
		return;
	// Vertices without position are just entirely culled.
	if ((vertex_type & GE_VTYPE_POS_MASK) == 0)
		return;

	u16 index_lower_bound = 0;
	u16 index_upper_bound = vertex_count - 1;
	IndexConverter ConvertIndex(vertex_type, indices);

	if (indices)
		GetIndexBounds(indices, vertex_count, vertex_type, &index_lower_bound, &index_upper_bound);
	vdecoder.DecodeVerts(decoded_, vertices, index_lower_bound, index_upper_bound);

	VertexReader vreader(decoded_, vtxfmt, vertex_type);

	static VertexData data[4];  // Normally max verts per prim is 3, but we temporarily need 4 to detect rectangles from strips.
	// This is the index of the next vert in data (or higher, may need modulus.)
	static int data_index = 0;

	static GEPrimitiveType prev_prim = GE_PRIM_POINTS;
	if (prim_type != GE_PRIM_KEEP_PREVIOUS) {
		data_index = 0;
		prev_prim = prim_type;
	} else {
		prim_type = prev_prim;
	}

	int vtcs_per_prim;
	switch (prim_type) {
	case GE_PRIM_POINTS: vtcs_per_prim = 1; break;
	case GE_PRIM_LINES: vtcs_per_prim = 2; break;
	case GE_PRIM_TRIANGLES: vtcs_per_prim = 3; break;
	case GE_PRIM_RECTANGLES: vtcs_per_prim = 2; break;
	default: vtcs_per_prim = 0; break;
	}

	// TODO: Do this in two passes - first process the vertices (before indexing/stripping),
	// then resolve the indices. This lets us avoid transforming shared vertices twice.

	binner_->UpdateState();

	static TransformState transformState;
	if (binner_->HasDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL)) {
		ComputeTransformState(&transformState, vreader);
		binner_->ClearDirty(SoftDirty::LIGHT_ALL | SoftDirty::TRANSFORM_ALL);
	}

	bool skipCull = !gstate.isCullEnabled() || gstate.isModeClear();
	const CullType cullType = skipCull ? CullType::OFF : (gstate.getCullMode() ? CullType::CCW : CullType::CW);

	bool outside_range_flag = false;
	switch (prim_type) {
	case GE_PRIM_POINTS:
	case GE_PRIM_LINES:
	case GE_PRIM_TRIANGLES:
		{
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				if (indices) {
					vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
				} else {
					vreader.Goto(vtx);
				}

				data[data_index++] = ReadVertex(vreader, transformState, outside_range_flag);
				if (data_index < vtcs_per_prim) {
					// Keep reading.  Note: an incomplete prim will stay read for GE_PRIM_KEEP_PREVIOUS.
					continue;
				}

				// Okay, we've got enough verts.  Reset the index for next time.
				data_index = 0;
				if (outside_range_flag) {
					// Cull the prim if it was outside, and move to the next prim.
					outside_range_flag = false;
					continue;
				}

				switch (prim_type) {
				case GE_PRIM_TRIANGLES:
				{
					if (cullType == CullType::OFF) {
						Clipper::ProcessTriangle(data[0], data[1], data[2], data[2], *binner_);
						Clipper::ProcessTriangle(data[2], data[1], data[0], data[2], *binner_);
					} else if (cullType == CullType::CW) {
						Clipper::ProcessTriangle(data[2], data[1], data[0], data[2], *binner_);
					} else {
						Clipper::ProcessTriangle(data[0], data[1], data[2], data[2], *binner_);
					}
					break;
				}

				case GE_PRIM_LINES:
					Clipper::ProcessLine(data[0], data[1], *binner_);
					break;

				case GE_PRIM_POINTS:
					Clipper::ProcessPoint(data[0], *binner_);
					break;

				default:
					_dbg_assert_msg_(false, "Unexpected prim type: %d", prim_type);
				}
			}
			break;
		}

	case GE_PRIM_RECTANGLES:
		for (int vtx = 0; vtx < vertex_count; ++vtx) {
			if (indices) {
				vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
			} else {
				vreader.Goto(vtx);
			}

			data[data_index++] = ReadVertex(vreader, transformState, outside_range_flag);
			if (outside_range_flag) {
				outside_range_flag = false;
				// Note: this is the post increment index.  If odd, we set the first vert.
				if (data_index & 1) {
					// Skip the next one and forget this one.
					vtx++;
					data_index--;
				} else {
					// Forget both of the last 2.
					data_index -= 2;
				}
			}

			if (data_index == 4 && gstate.isModeThrough() && cullType == CullType::OFF) {
				if (Rasterizer::DetectRectangleThroughModeSlices(binner_->State(), data)) {
					data[1] = data[3];
					data_index = 2;
				}
			}

			if (data_index == 4) {
				Clipper::ProcessRect(data[0], data[1], *binner_);
				Clipper::ProcessRect(data[2], data[3], *binner_);
				data_index = 0;
			}
		}

		if (data_index >= 2) {
			Clipper::ProcessRect(data[0], data[1], *binner_);
			data_index -= 2;
		}
		break;

	case GE_PRIM_LINE_STRIP:
		{
			// Don't draw a line when loading the first vertex.
			// If data_index is 1 or 2, etc., it means we're continuing a line strip.
			int skip_count = data_index == 0 ? 1 : 0;
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				if (indices) {
					vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
				} else {
					vreader.Goto(vtx);
				}

				data[(data_index++) & 1] = ReadVertex(vreader, transformState, outside_range_flag);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
				} else {
					// We already incremented data_index, so data_index & 1 is previous one.
					Clipper::ProcessLine(data[data_index & 1], data[(data_index & 1) ^ 1], *binner_);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_STRIP:
		{
			// Don't draw a triangle when loading the first two vertices.
			int skip_count = data_index >= 2 ? 0 : 2 - data_index;

			// If index count == 4, check if we can convert to a rectangle.
			// This is for Darkstalkers (and should speed up many 2D games).
			if (data_index == 0 && vertex_count == 4 && cullType == CullType::OFF) {
				for (int vtx = 0; vtx < 4; ++vtx) {
					if (indices) {
						vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
					}
					else {
						vreader.Goto(vtx);
					}
					data[vtx] = ReadVertex(vreader, transformState, outside_range_flag);
				}

				// If a strip is effectively a rectangle, draw it as such!
				int tl = -1, br = -1;
				if (!outside_range_flag && Rasterizer::DetectRectangleFromStrip(binner_->State(), data, &tl, &br)) {
					Clipper::ProcessRect(data[tl], data[br], *binner_);
					break;
				}
			}

			outside_range_flag = false;
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				if (indices) {
					vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
				} else {
					vreader.Goto(vtx);
				}

				int provoking_index = (data_index++) % 3;
				data[provoking_index] = ReadVertex(vreader, transformState, outside_range_flag);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (cullType == CullType::OFF) {
					Clipper::ProcessTriangle(data[0], data[1], data[2], data[provoking_index], *binner_);
					Clipper::ProcessTriangle(data[2], data[1], data[0], data[provoking_index], *binner_);
				} else if ((!(int)cullType) ^ ((data_index - 1) % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0], data[provoking_index], *binner_);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2], data[provoking_index], *binner_);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_FAN:
		{
			// Don't draw a triangle when loading the first two vertices.
			// (this doesn't count the central one.)
			int skip_count = data_index <= 1 ? 1 : 0;
			int start_vtx = 0;

			// Only read the central vertex if we're not continuing.
			if (data_index == 0) {
				if (indices) {
					vreader.Goto(ConvertIndex(0) - index_lower_bound);
				} else {
					vreader.Goto(0);
				}
				data[0] = ReadVertex(vreader, transformState, outside_range_flag);
				data_index++;
				start_vtx = 1;

				// If the central vertex is outside range, all the points are toast.
				if (outside_range_flag)
					break;
			}

			if (data_index == 1 && vertex_count == 4 && cullType == CullType::OFF) {
				for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
					if (indices) {
						vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
					} else {
						vreader.Goto(vtx);
					}
					data[vtx] = ReadVertex(vreader, transformState, outside_range_flag);
				}

				int tl = -1, br = -1;
				if (!outside_range_flag && Rasterizer::DetectRectangleFromFan(binner_->State(), data, vertex_count, &tl, &br)) {
					Clipper::ProcessRect(data[tl], data[br], *binner_);
					break;
				}
			}

			outside_range_flag = false;
			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				if (indices) {
					vreader.Goto(ConvertIndex(vtx) - index_lower_bound);
				} else {
					vreader.Goto(vtx);
				}

				int provoking_index = 2 - ((data_index++) % 2);
				data[provoking_index] = ReadVertex(vreader, transformState, outside_range_flag);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (cullType == CullType::OFF) {
					Clipper::ProcessTriangle(data[0], data[1], data[2], data[provoking_index], *binner_);
					Clipper::ProcessTriangle(data[2], data[1], data[0], data[provoking_index], *binner_);
				} else if ((!(int)cullType) ^ ((data_index - 1) % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0], data[provoking_index], *binner_);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2], data[provoking_index], *binner_);
				}
			}
			break;
		}

	default:
		ERROR_LOG(G3D, "Unexpected prim type: %d", prim_type);
		break;
	}
}

void TransformUnit::Flush(const char *reason) {
	binner_->Flush(reason);
	GPUDebug::NotifyDraw();
}

void TransformUnit::GetStats(char *buffer, size_t bufsize) {
	// TODO: More stats?
	binner_->GetStats(buffer, bufsize);
}

void TransformUnit::FlushIfOverlap(const char *reason, uint32_t addr, uint32_t stride, uint32_t w, uint32_t h) {
	if (binner_->HasPendingWrite(addr, stride, w, h))
		Flush(reason);
}

void TransformUnit::NotifyClutUpdate(const void *src) {
	binner_->UpdateClut(src);
}

// TODO: This probably is not the best interface.
// Also, we should try to merge this into the similar function in DrawEngineCommon.
bool TransformUnit::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

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
				WARN_LOG_REPORT_ONCE(simpleIndexes32, G3D, "SimpleVertices: Decoding 32-bit indexes");
				for (int i = 0; i < count; ++i) {
					// These aren't documented and should be rare.  Let's bounds check each one.
					if (inds32[i] != (u16)inds32[i]) {
						ERROR_LOG_REPORT_ONCE(simpleIndexes32Bounds, G3D, "SimpleVertices: Index outside 16-bit range");
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
	vdecoder.SetVertexType(gstate.vertType, options);

	if (!Memory::IsValidRange(gstate_c.vertexAddr, (indexUpperBound + 1) * vdecoder.VertexSize()))
		return false;

	DrawEngineCommon::NormalizeVertices((u8 *)(&simpleVertices[0]), (u8 *)(&temp_buffer[0]), Memory::GetPointer(gstate_c.vertexAddr), &vdecoder, indexLowerBound, indexUpperBound, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

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
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			ScreenCoords screenPos = ClipToScreen(clipPos);
			DrawingCoords drawPos = ScreenToDrawing(screenPos);

			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = screenPos.z;
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
