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

#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "Core/Config.h"

#include <algorithm>

#define QUAD_INDICES_MAX 65536

DrawEngineCommon::DrawEngineCommon() : dec_(nullptr) {
	quadIndices_ = new u16[6 * QUAD_INDICES_MAX];
	decJitCache_ = new VertexDecoderJitCache();
}

DrawEngineCommon::~DrawEngineCommon() {
	delete[] quadIndices_;
	delete decJitCache_;
	for (auto iter = decoderMap_.begin(); iter != decoderMap_.end(); iter++) {
		delete iter->second;
	}
}

VertexDecoder *DrawEngineCommon::GetVertexDecoder(u32 vtype) {
	auto iter = decoderMap_.find(vtype);
	if (iter != decoderMap_.end())
		return iter->second;
	VertexDecoder *dec = new VertexDecoder();
	dec->SetVertexType(vtype, decOptions_, decJitCache_);
	decoderMap_[vtype] = dec;
	return dec;
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
	// TODO: Check for invalid parameters (x2 < x1, etc)
	float vpx1 = getFloat24(gstate.viewportx1);
	float vpx2 = getFloat24(gstate.viewportx2);
	float vpy1 = getFloat24(gstate.viewporty1);
	float vpy2 = getFloat24(gstate.viewporty2);
	float vpz1 = getFloat24(gstate.viewportz1);
	float vpz2 = getFloat24(gstate.viewportz2);

	float retx = coords.x * vpx1 / coords.w + vpx2;
	float rety = coords.y * vpy1 / coords.w + vpy2;
	float retz = coords.z * vpz1 / coords.w + vpz2;

	// 16 = 0xFFFF / 4095.9375
	return Vec3f(retx * 16, rety * 16, retz);
}

static Vec3f ScreenToDrawing(const Vec3f& coords) {
	Vec3f ret;
	ret.x = (coords.x - gstate.getOffsetX16()) * (1.0f / 16.0f);
	ret.y = (coords.y - gstate.getOffsetY16()) * (1.0f / 16.0f);
	ret.z = coords.z;
	return ret;
}

// This code is HIGHLY unoptimized!
//
// It does the simplest and safest test possible: If all points of a bbox is outside a single of
// our clipping planes, we reject the box. Tighter bounds would be desirable but would take more calculations.
bool DrawEngineCommon::TestBoundingBox(void* control_points, int vertexCount, u32 vertType) {
	SimpleVertex *corners = (SimpleVertex *)(decoded + 65536 * 12);
	float *verts = (float *)(decoded + 65536 * 18);

	// Try to skip NormalizeVertices if it's pure positions. No need to bother with a vertex decoder
	// and a large vertex format.
	if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_FLOAT) {
		// memcpy(verts, control_points, 12 * vertexCount);
		verts = (float *)control_points;
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_8BIT) {
		const s8 *vtx = (const s8 *)control_points;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 128.0f);
		}
	} else if ((vertType & 0xFFFFFF) == GE_VTYPE_POS_16BIT) {
		const s16 *vtx = (const s16*)control_points;
		for (int i = 0; i < vertexCount * 3; i++) {
			verts[i] = vtx[i] * (1.0f / 32768.0f);
		}
	} else {
		// Simplify away bones and morph before proceeding
		u8 *temp_buffer = decoded + 65536 * 24;
		NormalizeVertices((u8 *)corners, temp_buffer, (u8 *)control_points, 0, vertexCount, vertType);
		// Special case for float positions only.
		const float *ctrl = (const float *)control_points;
		for (int i = 0; i < vertexCount; i++) {
			verts[i * 3] = corners[i].pos.x;
			verts[i * 3 + 1] = corners[i].pos.y;
			verts[i * 3 + 2] = corners[i].pos.z;
		}
	}

	Plane planes[6];

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
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

	if (!Memory::IsValidAddress(gstate_c.vertexAddr))
		return false;

	bool savedVertexFullAlpha = gstate_c.vertexFullAlpha;

	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const u8 *inds = Memory::GetPointer(gstate_c.indexAddr);
		const u16 *inds16 = (const u16 *)inds;

		if (inds) {
			GetIndexBounds(inds, count, gstate.vertType, &indexLowerBound, &indexUpperBound);
			indices.resize(count);
			switch (gstate.vertType & GE_VTYPE_IDX_MASK) {
			case GE_VTYPE_IDX_16BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds16[i];
				}
				break;
			case GE_VTYPE_IDX_8BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds[i];
				}
				break;
			default:
				return false;
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
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
		} else {
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			Vec3f screenPos = ClipToScreen(clipPos);
			Vec3f drawPos = ScreenToDrawing(screenPos);

			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = drawPos.z;
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
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
				// Normals are generated during tesselation anyway, not sure if any need to supply
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
				sv.uv[0] = 0;  // This will get filled in during tesselation
				sv.uv[1] = 0;
			}
			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}
			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tesselation anyway, not sure if any need to supply
				reader.ReadNrm((float *)&sv.nrm);
			} else {
				sv.nrm.x = 0;
				sv.nrm.y = 0;
				sv.nrm.z = 1.0f;
			}
			reader.ReadPos((float *)&sv.pos);
		}
	}

	// Okay, there we are! Return the new type (but keep the index bits)
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & (GE_VTYPE_IDX_MASK | GE_VTYPE_THROUGH));
}
