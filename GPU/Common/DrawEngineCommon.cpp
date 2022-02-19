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

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Core/Config.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#define QUAD_INDICES_MAX 65536

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

DrawEngineCommon::DrawEngineCommon() : decoderMap_(16) {
	decJitCache_ = new VertexDecoderJitCache();
	transformed = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	transformedExpanded = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
}

DrawEngineCommon::~DrawEngineCommon() {
	FreeMemoryPages(transformed, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);
	delete decJitCache_;
	decoderMap_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		delete decoder;
	});
	ClearSplineBezierWeights();
}

void DrawEngineCommon::Init() {
	useHWTransform_ = g_Config.bHardwareTransform;
	useHWTessellation_ = UpdateUseHWTessellation(g_Config.bHardwareTessellation);
}

VertexDecoder *DrawEngineCommon::GetVertexDecoder(u32 vtype) {
	VertexDecoder *dec = decoderMap_.Get(vtype);
	if (dec)
		return dec;
	dec = new VertexDecoder();
	dec->SetVertexType(vtype, decOptions_, decJitCache_);
	decoderMap_.Insert(vtype, dec);
	return dec;
}

int DrawEngineCommon::ComputeNumVertsToDecode() const {
	int vertsToDecode = 0;
	if (drawCalls[0].indexType == GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT) {
		for (int i = 0; i < numDrawCalls; i++) {
			const DeferredDrawCall &dc = drawCalls[i];
			vertsToDecode += dc.vertexCount;
		}
	} else {
		// TODO: Share this computation with DecodeVertsStep?
		for (int i = 0; i < numDrawCalls; i++) {
			const DeferredDrawCall &dc = drawCalls[i];
			int lastMatch = i;
			const int total = numDrawCalls;
			int indexLowerBound = dc.indexLowerBound;
			int indexUpperBound = dc.indexUpperBound;
			for (int j = i + 1; j < total; ++j) {
				if (drawCalls[j].verts != dc.verts)
					break;

				indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
				lastMatch = j;
			}
			vertsToDecode += indexUpperBound - indexLowerBound + 1;
			i = lastMatch;
		}
	}
	return vertsToDecode;
}

void DrawEngineCommon::DecodeVerts(u8 *dest) {
	const UVScale origUV = gstate_c.uv;
	for (; decodeCounter_ < numDrawCalls; decodeCounter_++) {
		gstate_c.uv = drawCalls[decodeCounter_].uvScale;
		DecodeVertsStep(dest, decodeCounter_, decodedVerts_);  // NOTE! DecodeVertsStep can modify decodeCounter_!
	}
	gstate_c.uv = origUV;

	// Sanity check
	if (indexGen.Prim() < 0) {
		ERROR_LOG_REPORT(G3D, "DecodeVerts: Failed to deduce prim: %i", indexGen.Prim());
		// Force to points (0)
		indexGen.AddPrim(GE_PRIM_POINTS, 0, true);
	}
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

std::string DrawEngineCommon::DebugGetVertexLoaderString(std::string id, DebugShaderStringType stringType) {
	u32 mapId;
	memcpy(&mapId, &id[0], sizeof(mapId));
	VertexDecoder *dec = decoderMap_.Get(mapId);
	return dec ? dec->GetString(stringType) : "N/A";
}

struct Plane {
	float x, y, z, w;
	void Set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; }
	float Test(float f[3]) const { return x * f[0] + y * f[1] + z * f[2] + w; }
};

static void PlanesFromMatrix(float mtx[16], Plane planes[6]) {
	planes[0].Set(mtx[3]-mtx[0], mtx[7]-mtx[4], mtx[11]-mtx[8], mtx[15]-mtx[12]);  // Right
	planes[1].Set(mtx[3]+mtx[0], mtx[7]+mtx[4], mtx[11]+mtx[8], mtx[15]+mtx[12]);  // Left
	planes[2].Set(mtx[3]+mtx[1], mtx[7]+mtx[5], mtx[11]+mtx[9], mtx[15]+mtx[13]);  // Bottom
	planes[3].Set(mtx[3]-mtx[1], mtx[7]-mtx[5], mtx[11]-mtx[9], mtx[15]-mtx[13]);  // Top
	planes[4].Set(mtx[3]+mtx[2], mtx[7]+mtx[6], mtx[11]+mtx[10], mtx[15]+mtx[14]); // Near
	planes[5].Set(mtx[3]-mtx[2], mtx[7]-mtx[6], mtx[11]-mtx[10], mtx[15]-mtx[14]); // Far
}

static Vec3f ClipToScreen(const Vec4f& coords) {
	float xScale = gstate.getViewportXScale();
	float xCenter = gstate.getViewportXCenter();
	float yScale = gstate.getViewportYScale();
	float yCenter = gstate.getViewportYCenter();
	float zScale = gstate.getViewportZScale();
	float zCenter = gstate.getViewportZCenter();

	float x = coords.x * xScale / coords.w + xCenter;
	float y = coords.y * yScale / coords.w + yCenter;
	float z = coords.z * zScale / coords.w + zCenter;

	// 16 = 0xFFFF / 4095.9375
	return Vec3f(x * 16 - gstate.getOffsetX16(), y * 16 - gstate.getOffsetY16(), z);
}

static Vec3f ScreenToDrawing(const Vec3f& coords) {
	Vec3f ret;
	ret.x = coords.x * (1.0f / 16.0f);
	ret.y = coords.y * (1.0f / 16.0f);
	ret.z = coords.z;
	return ret;
}

void DrawEngineCommon::Resized() {
	decJitCache_->Clear();
	lastVType_ = -1;
	dec_ = nullptr;
	decoderMap_.Iterate([&](const uint32_t vtype, VertexDecoder *decoder) {
		delete decoder;
	});
	decoderMap_.Clear();
	ClearTrackedVertexArrays();

	useHWTransform_ = g_Config.bHardwareTransform;
	useHWTessellation_ = UpdateUseHWTessellation(g_Config.bHardwareTessellation);
}

u32 DrawEngineCommon::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType, int *vertexSize) {
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	if (vertexSize)
		*vertexSize = dec->VertexSize();
	return DrawEngineCommon::NormalizeVertices(outPtr, bufPtr, inPtr, dec, lowerBound, upperBound, vertType);
}

// This code has plenty of potential for optimization.
//
// It does the simplest and safest test possible: If all points of a bbox is outside a single of
// our clipping planes, we reject the box. Tighter bounds would be desirable but would take more calculations.
bool DrawEngineCommon::TestBoundingBox(const void* control_points, int vertexCount, u32 vertType, int *bytesRead) {
	SimpleVertex *corners = (SimpleVertex *)(decoded + 65536 * 12);
	float *verts = (float *)(decoded + 65536 * 18);

	// Try to skip NormalizeVertices if it's pure positions. No need to bother with a vertex decoder
	// and a large vertex format.
	if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_FLOAT) {
		verts = (float *)control_points;
		*bytesRead = 3 * sizeof(float) * vertexCount;
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_8BIT) {
		const s8 *vtx = (const s8 *)control_points;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 128.0f);
		}
		*bytesRead = 3 * sizeof(s8) * vertexCount;
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_16BIT) {
		const s16 *vtx = (const s16*)control_points;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 32768.0f);
		}
		*bytesRead = 3 * sizeof(s16) * vertexCount;
	} else {
		// Simplify away bones and morph before proceeding
		u8 *temp_buffer = decoded + 65536 * 24;
		int vertexSize = 0;
		NormalizeVertices((u8 *)corners, temp_buffer, (const u8 *)control_points, 0, vertexCount, vertType, &vertexSize);
		for (int i = 0; i < vertexCount; i++) {
			verts[i * 3] = corners[i].pos.x;
			verts[i * 3 + 1] = corners[i].pos.y;
			verts[i * 3 + 2] = corners[i].pos.z;
		}
		*bytesRead = vertexSize * vertexCount;
	}

	Plane planes[6];

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	// TODO: Create a Matrix4x3ByMatrix4x3, and Matrix4x4ByMatrix4x3?
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);
	PlanesFromMatrix(worldviewproj, planes);
	for (int plane = 0; plane < 6; plane++) {
		int inside = 0;
		int out = 0;
		for (int i = 0; i < vertexCount; i++) {
			// Here we can test against the frustum planes!
			float value = planes[plane].Test(verts + i * 3);
			if (value < 0)
				out++;
			else
				inside++;
		}

		if (inside == 0) {
			// All out
			return false;
		}

		// Any out. For testing that the planes are in the right locations.
		// if (out != 0) return false;
	}
	return true;
}

// TODO: This probably is not the best interface.
bool DrawEngineCommon::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

	if (!Memory::IsValidAddress(gstate_c.vertexAddr) || count == 0)
		return false;

	bool savedVertexFullAlpha = gstate_c.vertexFullAlpha;

	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
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
	NormalizeVertices((u8 *)(&simpleVertices[0]), (u8 *)(&temp_buffer[0]), Memory::GetPointer(gstate_c.vertexAddr), indexLowerBound, indexUpperBound, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

	vertices.resize(indexUpperBound + 1);
	uint32_t vertType = gstate.vertType;
	for (int i = indexLowerBound; i <= indexUpperBound; ++i) {
		const SimpleVertex &vert = simpleVertices[i];

		if ((vertType & GE_VTYPE_THROUGH) != 0) {
			if (vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = vert.pos.x;
			vertices[i].y = vert.pos.y;
			vertices[i].z = vert.pos.z;
			if (vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
			vertices[i].nx = 0;  // No meaningful normals in through mode
			vertices[i].ny = 0;
			vertices[i].nz = 1.0f;
		} else {
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			Vec3f screenPos = ClipToScreen(clipPos);
			Vec3f drawPos = ScreenToDrawing(screenPos);

			if (vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			// Should really have separate coordinates for before and after transform.
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = drawPos.z;
			if (vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
			vertices[i].nx = vert.nrm.x;
			vertices[i].ny = vert.nrm.y;
			vertices[i].nz = vert.nrm.z;
		}
	}

	gstate_c.vertexFullAlpha = savedVertexFullAlpha;

	return true;
}

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 DrawEngineCommon::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, VertexDecoder *dec, int lowerBound, int upperBound, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	dec->DecodeVerts(bufPtr, inPtr, lowerBound, upperBound);

	// OK, morphing eliminated but bones still remain to be taken care of.
	// Let's do a partial software transform where we only do skinning.

	VertexReader reader(bufPtr, dec->GetDecVtxFmt(), vertType);

	SimpleVertex *sverts = (SimpleVertex *)outPtr;

	const u8 defaultColor[4] = {
		(u8)gstate.getMaterialAmbientR(),
		(u8)gstate.getMaterialAmbientG(),
		(u8)gstate.getMaterialAmbientB(),
		(u8)gstate.getMaterialAmbientA(),
	};

	// Let's have two separate loops, one for non skinning and one for skinning.
	if (!g_Config.bSoftwareSkinning && (vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		int numBoneWeights = vertTypeGetNumBoneWeights(vertType);
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i - lowerBound);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			}

			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}

			float nrm[3], pos[3];
			float bnrm[3], bpos[3];

			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tessellation anyway, not sure if any need to supply
				reader.ReadNrm(nrm);
			} else {
				nrm[0] = 0;
				nrm[1] = 0;
				nrm[2] = 1.0f;
			}
			reader.ReadPos(pos);

			// Apply skinning transform directly
			float weights[8];
			reader.ReadWeights(weights);
			// Skinning
			Vec3Packedf psum(0, 0, 0);
			Vec3Packedf nsum(0, 0, 0);
			for (int w = 0; w < numBoneWeights; w++) {
				if (weights[w] != 0.0f) {
					Vec3ByMatrix43(bpos, pos, gstate.boneMatrix + w * 12);
					Vec3Packedf tpos(bpos);
					psum += tpos * weights[w];

					Norm3ByMatrix43(bnrm, nrm, gstate.boneMatrix + w * 12);
					Vec3Packedf tnorm(bnrm);
					nsum += tnorm * weights[w];
				}
			}
			sv.pos = psum;
			sv.nrm = nsum;
		}
	} else {
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i - lowerBound);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			} else {
				sv.uv[0] = 0.0f;  // This will get filled in during tessellation
				sv.uv[1] = 0.0f;
			}
			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}
			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tessellation anyway, not sure if any need to supply
				reader.ReadNrm((float *)&sv.nrm);
			} else {
				sv.nrm.x = 0.0f;
				sv.nrm.y = 0.0f;
				sv.nrm.z = 1.0f;
			}
			reader.ReadPos((float *)&sv.pos);
		}
	}

	// Okay, there we are! Return the new type (but keep the index bits)
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & (GE_VTYPE_IDX_MASK | GE_VTYPE_THROUGH));
}

void DrawEngineCommon::ApplyFramebufferRead(FBOTexState *fboTexState) {
	if (gstate_c.Supports(GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)) {
		*fboTexState = FBO_TEX_READ_FRAMEBUFFER;
	} else {
		gpuStats.numCopiesForShaderBlend++;
		*fboTexState = FBO_TEX_COPY_BIND_TEX;
	}

	gstate_c.Dirty(DIRTY_SHADERBLEND);
}

void DrawEngineCommon::DecodeVertsStep(u8 *dest, int &i, int &decodedVerts) {
	PROFILE_THIS_SCOPE("vertdec");

	const DeferredDrawCall &dc = drawCalls[i];

	indexGen.SetIndex(decodedVerts);
	int indexLowerBound = dc.indexLowerBound;
	int indexUpperBound = dc.indexUpperBound;

	if (dc.indexType == GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT) {
		// Decode the verts (and at the same time apply morphing/skinning). Simple.
		dec_->DecodeVerts(dest + decodedVerts * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts += indexUpperBound - indexLowerBound + 1;
		
		bool clockwise = true;
		if (gstate.isCullEnabled() && gstate.getCullMode() != dc.cullMode) {
			clockwise = false;
		}
		indexGen.AddPrim(dc.prim, dc.vertexCount, clockwise);
	} else {
		// It's fairly common that games issue long sequences of PRIM calls, with differing
		// inds pointer but the same base vertex pointer. We'd like to reuse vertices between
		// these as much as possible, so we make sure here to combine as many as possible
		// into one nice big drawcall, sharing data.

		// 1. Look ahead to find the max index, only looking as "matching" drawcalls.
		//    Expand the lower and upper bounds as we go.
		int lastMatch = i;
		const int total = numDrawCalls;
		for (int j = i + 1; j < total; ++j) {
			if (drawCalls[j].verts != dc.verts)
				break;

			indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
			indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
			lastMatch = j;
		}

		// 2. Loop through the drawcalls, translating indices as we go.
		switch (dc.indexType) {
		case GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				bool clockwise = true;
				if (gstate.isCullEnabled() && gstate.getCullMode() != drawCalls[j].cullMode) {
					clockwise = false;
				}
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u8 *)drawCalls[j].inds, indexLowerBound, clockwise);
			}
			break;
		case GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				bool clockwise = true;
				if (gstate.isCullEnabled() && gstate.getCullMode() != drawCalls[j].cullMode) {
					clockwise = false;
				}
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u16_le *)drawCalls[j].inds, indexLowerBound, clockwise);
			}
			break;
		case GE_VTYPE_IDX_32BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				bool clockwise = true;
				if (gstate.isCullEnabled() && gstate.getCullMode() != drawCalls[j].cullMode) {
					clockwise = false;
				}
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u32_le *)drawCalls[j].inds, indexLowerBound, clockwise);
			}
			break;
		}

		const int vertexCount = indexUpperBound - indexLowerBound + 1;

		// This check is a workaround for Pangya Fantasy Golf, which sends bogus index data when switching items in "My Room" sometimes.
		if (decodedVerts + vertexCount > VERTEX_BUFFER_MAX) {
			return;
		}

		// 3. Decode that range of vertex data.
		dec_->DecodeVerts(dest + decodedVerts * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts += vertexCount;

		// 4. Advance indexgen vertex counter.
		indexGen.Advance(vertexCount);
		i = lastMatch;
	}
}

inline u32 ComputeMiniHashRange(const void *ptr, size_t sz) {
	// Switch to u32 units, and round up to avoid unaligned accesses.
	// Probably doesn't matter if we skip the first few bytes in some cases.
	const u32 *p = (const u32 *)(((uintptr_t)ptr + 3) & ~3);
	sz >>= 2;

	if (sz > 100) {
		size_t step = sz / 4;
		u32 hash = 0;
		for (size_t i = 0; i < sz; i += step) {
			hash += XXH3_64bits(p + i, 100);
		}
		return hash;
	} else {
		return p[0] + p[sz - 1];
	}
}

u32 DrawEngineCommon::ComputeMiniHash() {
	u32 fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = IndexSize(dec_->VertexType());

	int step;
	if (numDrawCalls < 3) {
		step = 1;
	} else if (numDrawCalls < 8) {
		step = 4;
	} else {
		step = numDrawCalls / 8;
	}
	for (int i = 0; i < numDrawCalls; i += step) {
		const DeferredDrawCall &dc = drawCalls[i];
		if (!dc.inds) {
			fullhash += ComputeMiniHashRange(dc.verts, vertexSize * dc.vertexCount);
		} else {
			int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;
			fullhash += ComputeMiniHashRange((const u8 *)dc.verts + vertexSize * indexLowerBound, vertexSize * (indexUpperBound - indexLowerBound));
			fullhash += ComputeMiniHashRange(dc.inds, indexSize * dc.vertexCount);
		}
	}

	return fullhash;
}

uint64_t DrawEngineCommon::ComputeHash() {
	uint64_t fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = IndexSize(dec_->VertexType());

	// TODO: Add some caps both for numDrawCalls and num verts to check?
	// It is really very expensive to check all the vertex data so often.
	for (int i = 0; i < numDrawCalls; i++) {
		const DeferredDrawCall &dc = drawCalls[i];
		if (!dc.inds) {
			fullhash += XXH3_64bits((const char *)dc.verts, vertexSize * dc.vertexCount);
		} else {
			int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;
			int j = i + 1;
			int lastMatch = i;
			while (j < numDrawCalls) {
				if (drawCalls[j].verts != dc.verts)
					break;
				indexLowerBound = std::min(indexLowerBound, (int)dc.indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)dc.indexUpperBound);
				lastMatch = j;
				j++;
			}
			// This could get seriously expensive with sparse indices. Need to combine hashing ranges the same way
			// we do when drawing.
			fullhash += XXH3_64bits((const char *)dc.verts + vertexSize * indexLowerBound,
				vertexSize * (indexUpperBound - indexLowerBound));
			// Hm, we will miss some indices when combining above, but meh, it should be fine.
			fullhash += XXH3_64bits((const char *)dc.inds, indexSize * dc.vertexCount);
			i = lastMatch;
		}
	}

	fullhash += XXH3_64bits(&drawCalls[0].uvScale, sizeof(drawCalls[0].uvScale) * numDrawCalls);
	return fullhash;
}

// vertTypeID is the vertex type but with the UVGen mode smashed into the top bits.
void DrawEngineCommon::SubmitPrim(const void *verts, const void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int cullMode, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawCalls >= MAX_DEFERRED_DRAW_CALLS || vertexCountInDrawCalls_ + vertexCount > VERTEX_BUFFER_MAX) {
		DispatchFlush();
	}

	// TODO: Is this the right thing to do?
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		prim = prevPrim_ != GE_PRIM_INVALID ? prevPrim_ : GE_PRIM_POINTS;
	} else {
		prevPrim_ = prim;
	}

	// If vtype has changed, setup the vertex decoder.
	if (vertTypeID != lastVType_) {
		dec_ = GetVertexDecoder(vertTypeID);
		lastVType_ = vertTypeID;
	}

	*bytesRead = vertexCount * dec_->VertexSize();

	// Check that we have enough vertices to form the requested primitive.
	if ((vertexCount < 2 && prim > 0) || (vertexCount < 3 && prim > GE_PRIM_LINE_STRIP && prim != GE_PRIM_RECTANGLES))
		return;

	if (g_Config.bVertexCache) {
		u32 dhash = dcid_;
		dhash = __rotl(dhash ^ (u32)(uintptr_t)verts, 13);
		dhash = __rotl(dhash ^ (u32)(uintptr_t)inds, 13);
		dhash = __rotl(dhash ^ (u32)vertTypeID, 13);
		dhash = __rotl(dhash ^ (u32)vertexCount, 13);
		dcid_ = dhash ^ (u32)prim;
	}

	DeferredDrawCall &dc = drawCalls[numDrawCalls];
	dc.verts = verts;
	dc.inds = inds;
	dc.vertexCount = vertexCount;
	dc.indexType = (vertTypeID & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
	dc.prim = prim;
	dc.cullMode = cullMode;
	dc.uvScale = gstate_c.uv;
	if (inds) {
		GetIndexBounds(inds, vertexCount, vertTypeID, &dc.indexLowerBound, &dc.indexUpperBound);
	} else {
		dc.indexLowerBound = 0;
		dc.indexUpperBound = vertexCount - 1;
	}

	numDrawCalls++;
	vertexCountInDrawCalls_ += vertexCount;

	if (g_Config.bSoftwareSkinning && (vertTypeID & GE_VTYPE_WEIGHT_MASK)) {
		DecodeVertsStep(decoded, decodeCounter_, decodedVerts_);
		decodeCounter_++;
	}

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// Rendertarget == texture? Shouldn't happen. Still, try some mitigations.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		DispatchFlush();
	}
}

bool DrawEngineCommon::CanUseHardwareTransform(int prim) {
	if (!useHWTransform_)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP;
}

bool DrawEngineCommon::CanUseHardwareTessellation(GEPatchPrimType prim) {
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
