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

#include "TransformPipeline.h"
#include "Core/MemMap.h"
#include "GPU/Math3D.h"

// PSP compatible format so we can use the end of the pipeline
struct SimpleVertex {
	float uv[2];
	u8 color[4];
	Vec3f nrm;
	Vec3f pos;
};

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 TransformDrawEngine::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	VertexDecoder *dec = GetVertexDecoder(vertType);
	dec->DecodeVerts(bufPtr, inPtr, lowerBound, upperBound);

	// OK, morphing eliminated but bones still remain to be taken care of.
	// Let's do a partial software transform where we only do skinning.

	VertexReader reader(bufPtr, dec->GetDecVtxFmt(), vertType);

	SimpleVertex *sverts = (SimpleVertex *)outPtr;	

	const u8 defaultColor[4] = {
		gstate.getMaterialAmbientR(),
		gstate.getMaterialAmbientG(),
		gstate.getMaterialAmbientB(),
		gstate.getMaterialAmbientA(),
	};

	// Let's have two separate loops, one for non skinning and one for skinning.
	if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		int numBoneWeights = vertTypeGetNumBoneWeights(vertType);
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
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
			Vec3f psum(0,0,0);
			Vec3f nsum(0,0,0);
			for (int i = 0; i < numBoneWeights; i++) {
				if (weights[i] != 0.0f) {
					Vec3ByMatrix43(bpos, pos, gstate.boneMatrix+i*12);
					Vec3f tpos(bpos);
					psum += tpos * weights[i];

					Norm3ByMatrix43(bnrm, nrm, gstate.boneMatrix+i*12);
					Vec3f tnorm(bnrm);
					nsum += tnorm * weights[i];
				}
			}
			sv.pos = psum;
			sv.nrm = nsum;
		}
	} else {
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
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
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & GE_VTYPE_IDX_MASK);
}


// Just to get something on the screen, we'll just not subdivide correctly.
void TransformDrawEngine::DrawBezier(int ucount, int vcount) {
	 if ((ucount - 1) % 3 != 0 || (vcount - 1) % 3 != 0) 
		ERROR_LOG_REPORT(G3D, "Unsupported bezier parameters ucount=%i, vcount=%i", ucount, vcount);

	u16 *indices = new u16[ucount * vcount * 6];

	static bool reported = false;
	if (!reported) {
		Reporting::ReportMessage("Unsupported bezier curve");
		reported = true;
	}

	// if (gstate.patchprimitive)
	// Generate indices for a rectangular mesh.
	int c = 0;
	for (int y = 0; y < ucount; y++) {
		for (int x = 0; x < vcount - 1; x++) {
			indices[c++] = y * (vcount - 1)+ x;
			indices[c++] = y * (vcount - 1) + x + 1;
			indices[c++] = (y + 1) * (vcount - 1) + x + 1;
			indices[c++] = (y + 1) * (vcount - 1) + x + 1;
			indices[c++] = (y + 1) * (vcount - 1) + x;
			indices[c++] = y * (vcount - 1) + x;
		}
	}

	// We are free to use the "decoded" buffer here.
	// Let's split it into two to get a second buffer, there's enough space.
	u8 *decoded2 = decoded + 65536 * 24;

	// Alright, now for the vertex data.
	// For now, we will simply inject UVs.

	float customUV[4 * 4 * 2];
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			customUV[(y * 4 + x) * 2 + 0] = (float)x/3.0f;
			customUV[(y * 4 + x) * 2 + 1] = (float)y/3.0f;
		}
	}

	if (!vertTypeGetTexCoordMask(gstate.vertType)) {
		VertexDecoder *dec = GetVertexDecoder(gstate.vertType);
		dec->SetVertexType(gstate.vertType);
		u32 newVertType = dec->InjectUVs(decoded2, Memory::GetPointer(gstate_c.vertexAddr), customUV, 16);
		SubmitPrim(decoded2, &indices[0], GE_PRIM_TRIANGLES, c, newVertType, GE_VTYPE_IDX_16BIT, 0);
	} else {
		SubmitPrim(Memory::GetPointer(gstate_c.vertexAddr), &indices[0], GE_PRIM_TRIANGLES, c, gstate.vertType, GE_VTYPE_IDX_16BIT, 0);
	}
	Flush();  // as our vertex storage here is temporary, it will only survive one draw.

	delete [] indices;
}


// Spline implementation copied and modified from neobrain's softgpu (orphis code?)

#define START_OPEN_U 1
#define END_OPEN_U 2
#define START_OPEN_V 4
#define END_OPEN_V 8

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct HWSplinePatch {
	SimpleVertex *points[16];
	int type;

	// We need to generate both UVs and normals later...

	// These are used to generate UVs.
	int u_index, v_index;
};

static void CopyTriangle(u8 *&dest, SimpleVertex *v1, SimpleVertex *v2, SimpleVertex* v3) {
	int vertexSize = sizeof(SimpleVertex);
	memcpy(dest, v1, vertexSize);
	dest += vertexSize;
	memcpy(dest, v2, vertexSize);
	dest += vertexSize;
	memcpy(dest, v3, vertexSize);
	dest += vertexSize;
}

// http://en.wikipedia.org/wiki/Bernstein_polynomial
Vec3f Bernstein3D(const Vec3f p0, const Vec3f p1, const Vec3f p2, const Vec3f p3, float u) {
	return p0 * (1.0f - u*u*u) + p1 * (3 * u * (1 - u) * (1 - u)) + p2 * (3 * u * u * (1 - u)) + p3 * u * u * u;
}


void TesselatePatch(u8 *&dest, int &count, const HWSplinePatch &patch, u32 origVertType) {
	if (true) {
		// TODO: Should do actual patch subdivision instead of just drawing the control points!
		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : 1;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : 1;
		const int tile_max_u = (patch.type & END_OPEN_U) ? 3 : 2;
		const int tile_max_v = (patch.type & END_OPEN_V) ? 3 : 2;

		float u_base = patch.u_index / 3.0f;
		float v_base = patch.v_index / 3.0f;

		const float third = 1.0f / 3.0f;

		for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
			for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
				int point_index = tile_u + tile_v*4;

				SimpleVertex v0 = *patch.points[point_index];
				SimpleVertex v1 = *patch.points[point_index+1];
				SimpleVertex v2 = *patch.points[point_index+4];
				SimpleVertex v3 = *patch.points[point_index+5];

				// Generate UV. TODO: Do this even if UV specified in control points?
				if ((origVertType & GE_VTYPE_TC_MASK) == 0) {
					float u = u_base + tile_u * third;
					float v = v_base + tile_v * third;
					v0.uv[0] = u;
					v0.uv[1] = v;
					v1.uv[0] = u + third;
					v1.uv[1] = v;
					v2.uv[0] = u;
					v2.uv[1] = v + third;
					v3.uv[0] = u + third;
					v3.uv[1] = v + third;
				}

				// Generate normal if lighting is enabled (otherwise there's no point).
				// This is a really poor quality algorithm, we get facet normals.
				if (gstate.isLightingEnabled()) {
					Vec3f norm = Cross(v1.pos - v0.pos, v2.pos - v0.pos);
					norm.Normalize();
					if (gstate.patchfacing & 1)
						norm *= -1.0f;
					v0.nrm = norm;
					v1.nrm = norm;
					v2.nrm = norm;
					v3.nrm = norm;
				}

				CopyTriangle(dest, &v0, &v2, &v1);
				CopyTriangle(dest, &v1, &v2, &v3);
				count += 6;
			}
		}
	} else {
		// TODO: This doesn't work yet, hence it's the else in an "if (true)".

		int tess_u = gstate.getPatchDivisionU();
		int tess_v = gstate.getPatchDivisionV();
		
		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : tess_u / 3;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : tess_v / 3;
		const int tile_max_u = (patch.type & END_OPEN_U) ? tess_u + 1 : tess_u * 2 / 3;
		const int tile_max_v = (patch.type & END_OPEN_V) ? tess_v + 1: tess_v * 2 / 3;

		// First compute all the positions and put them in an array
		Vec3f *positions = new Vec3f[(tess_u + 1) * (tess_v) + 1];

		for (int tile_v = 0; tile_v < tess_v + 1; ++tile_v) {
			for (int tile_u = 0; tile_u < tess_u + 1; ++tile_u) {
				float u = ((float)tile_u / (float)tess_u);
				float v = ((float)tile_v / (float)tess_v);
				
				// It must be possible to do some zany iterative solution instead of fully evaluating at every point.
				Vec3f pos1 = Bernstein3D(patch.points[0]->pos, patch.points[1]->pos, patch.points[2]->pos, patch.points[3]->pos, u);
				Vec3f pos2 = Bernstein3D(patch.points[4]->pos, patch.points[5]->pos, patch.points[6]->pos, patch.points[7]->pos, u);
				Vec3f pos3 = Bernstein3D(patch.points[8]->pos, patch.points[9]->pos, patch.points[10]->pos, patch.points[11]->pos, u);
				Vec3f pos4 = Bernstein3D(patch.points[12]->pos, patch.points[13]->pos, patch.points[14]->pos, patch.points[15]->pos, u);

				positions[tile_v * (tess_u + 1)] = Bernstein3D(pos1, pos2, pos3, pos4, v);
			}
		}

		/*
		for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
			for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
				Vec3f pos = Bernstein3D(patch.)


			int point_index = tile_u + tile_v*4;

		SimpleVertex v0 = patch.points[point_index];
		SimpleVertex v1 = patch.points[point_index+1];
		SimpleVertex v2 = patch.points[point_index+4];
		SimpleVertex v3 = patch.points[point_index+5];

		CopyTriangle(dest, v0, v2, v1);
		count += 6;
		*/
	}
}


void TransformDrawEngine::SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;
	
	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();
	
	int num_patches_u = count_u - 3;
	int num_patches_v = count_v - 3;

	// TODO: Do something less idiotic to manage this buffer
	HWSplinePatch* patches = new HWSplinePatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
		for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
			HWSplinePatch& patch = patches[patch_u + patch_v * num_patches_u];

			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u + point%4) + (patch_v + point/4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.type = (type_u | (type_v << 2));
			if (patch_u != 0) patch.type &= ~START_OPEN_U;
			if (patch_v != 0) patch.type &= ~START_OPEN_V;
			if (patch_u != num_patches_u-1) patch.type &= ~END_OPEN_U;
			if (patch_v != num_patches_v-1) patch.type &= ~END_OPEN_V;
		}
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		HWSplinePatch& patch = patches[patch_idx];
		TesselatePatch(dest, count, patch, origVertType);
	}
	delete[] patches;

	u32 vertTypeWithoutIndex = vertType & ~GE_VTYPE_IDX_MASK;

	SubmitPrim(decoded2, 0, GE_PRIM_TRIANGLES, count, vertTypeWithoutIndex, GE_VTYPE_IDX_NONE, 0);
	Flush();
}

void TransformDrawEngine::SubmitBezier(void* control_points, void* indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();

	// Bezier patches share less control points than spline patches. Otherwise they are pretty much the same (except bezier don't support the open/close thing)
	int num_patches_u = (count_u - 1) / 3;
	int num_patches_v = (count_v - 1) / 3;
	HWSplinePatch* patches = new HWSplinePatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; patch_u++) {
		for (int patch_v = 0; patch_v < num_patches_v; patch_v++) {
			HWSplinePatch& patch = patches[patch_u + patch_v * num_patches_u];
			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u * 3 + point%4) + (patch_v * 3 + point/4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.u_index = patch_u * 3;
			patch.v_index = patch_v * 3;
			patch.type = START_OPEN_U | START_OPEN_V | END_OPEN_U | END_OPEN_V;
		}
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		HWSplinePatch& patch = patches[patch_idx];
		TesselatePatch(dest, count, patch, origVertType);
	}
	delete[] patches;

	u32 vertTypeWithoutIndex = vertType & ~GE_VTYPE_IDX_MASK;

	SubmitPrim(decoded2, 0, GE_PRIM_TRIANGLES, count, vertTypeWithoutIndex, GE_VTYPE_IDX_NONE, 0);
	Flush();
}
