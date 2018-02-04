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

#include <string.h>
#include <algorithm>

#include "profiler/profiler.h"

#include "Common/CPUDetect.h"
#include "Common/MemoryUtil.h"
#include "Core/Config.h"

#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"  // only needed for UVScale stuff

static void CopyQuadIndex(u16 *&indices, GEPatchPrimType type, const int idx0, const int idx1, const int idx2, const int idx3) {
	if (type == GE_PATCHPRIM_LINES) {
		*(indices++) = idx0;
		*(indices++) = idx2;
		*(indices++) = idx1;
		*(indices++) = idx3;
		*(indices++) = idx1;
		*(indices++) = idx2;
	} else {
		*(indices++) = idx0;
		*(indices++) = idx2;
		*(indices++) = idx1;
		*(indices++) = idx1;
		*(indices++) = idx2;
		*(indices++) = idx3;
	}
}

static void BuildIndex(u16 *indices, int &count, int num_u, int num_v, GEPatchPrimType prim_type, int total) {
	for (int v = 0; v < num_v; ++v) {
		for (int u = 0; u < num_u; ++u) {
			int idx0 = v * (num_u + 1) + u + total; // Top left
			int idx2 = (v + 1) * (num_u + 1) + u + total; // Bottom left

			CopyQuadIndex(indices, prim_type, idx0, idx0 + 1, idx2, idx2 + 1);
			count += 6;
		}
	}
}

struct Weight {
	float weights[4], derivs[4];
};

class Bezier3DWeight {
private:
	void CalcWeights(float t, Weight &w) {
		// Bernstein 3D basis polynomial
		w.weights[0] = (1 - t) * (1 - t) * (1 - t);
		w.weights[1] = 3 * t * (1 - t) * (1 - t);
		w.weights[2] = 3 * t * t * (1 - t);
		w.weights[3] = t * t * t;

		// Derivative
		w.derivs[0] = -3 * (1 - t) * (1 - t);
		w.derivs[1] = 9 * t * t - 12 * t + 3;
		w.derivs[2] = 3 * (2 - 3 * t) * t;
		w.derivs[3] = 3 * t * t;
	}
public:
	Weight *CalcWeightsAll(u32 key) {
		int tess = (int)key;
		Weight *weights = new Weight[tess + 1];
		const float inv_u = 1.0f / (float)tess;
		for (int i = 0; i < tess + 1; ++i) {
			const float t = (float)i * inv_u;
			CalcWeights(t, weights[i]);
		}
		return weights;
	}

	u32 ToKey(int tess, int count, int type) {
		return tess;
	}
};

class Spline3DWeight {
private:
	struct KnotDiv {
		float _3_0 = 1.0f / 3.0f;
		float _4_1 = 1.0f / 3.0f;
		float _5_2 = 1.0f / 3.0f;
		float _3_1 = 1.0f / 2.0f;
		float _4_2 = 1.0f / 2.0f;
		float _3_2 = 1.0f; // Always 1
	};

	// knot should be an array sized n + 5  (n + 1 + 1 + degree (cubic))
	void CalcKnots(int n, int type, float *knots, KnotDiv *divs) {
		// Basic theory (-2 to +3), optimized with KnotDiv (-2 to +0) 
	//	for (int i = 0; i < n + 5; ++i) {
		for (int i = 0; i < n + 2; ++i) {
			knots[i] = (float)i - 2;
		}

		// The first edge is open
		if ((type & 1) != 0) {
			knots[0] = 0;
			knots[1] = 0;

			divs[0]._3_0 = 1.0f;
			divs[0]._4_1 = 1.0f / 2.0f;
			divs[0]._3_1 = 1.0f;
			if (n > 1)
				divs[1]._3_0 = 1.0f / 2.0f;
		}
		// The last edge is open
		if ((type & 2) != 0) {
			//	knots[n + 2] = (float)n; // Got rid of this line optimized with KnotDiv
			//	knots[n + 3] = (float)n; // Got rid of this line optimized with KnotDiv
			//	knots[n + 4] = (float)n; // Got rid of this line optimized with KnotDiv
			divs[n - 1]._4_1 = 1.0f / 2.0f;
			divs[n - 1]._5_2 = 1.0f;
			divs[n - 1]._4_2 = 1.0f;
			if (n > 1)
				divs[n - 2]._5_2 = 1.0f / 2.0f;
		}
	}

	void CalcWeights(float t, const float *knots, const KnotDiv &div, Weight &w) {
#ifdef _M_SSE
		const __m128 knot012 = _mm_loadu_ps(knots);
		const __m128 t012 = _mm_sub_ps(_mm_set_ps1(t), knot012);
		const __m128 f30_41_52 = _mm_mul_ps(t012, _mm_loadu_ps(&div._3_0));
		const __m128 f52_31_42 = _mm_mul_ps(t012, _mm_loadu_ps(&div._5_2));
		const float &f32 = t012.m128_f32[2];

		// Following comments are for explains order of the multiply.
	//	float a = (1-f30)*(1-f31);
	//	float c = (1-f41)*(1-f42);
	//	float b = (  f31 *   f41);
	//	float d = (  f42 *   f52);
		const __m128 f30_41_31_42 = _mm_shuffle_ps(f30_41_52, f52_31_42, _MM_SHUFFLE(2, 1, 1, 0));
		const __m128 f31_42_41_52 = _mm_shuffle_ps(f52_31_42, f30_41_52, _MM_SHUFFLE(2, 1, 2, 1));
		const __m128 c1_1_0_0 = { 1, 1, 0, 0 };
		const __m128 acbd = _mm_mul_ps(_mm_sub_ps(c1_1_0_0, f30_41_31_42), _mm_sub_ps(c1_1_0_0, f31_42_41_52));
		const float &a = acbd.m128_f32[0];
		const float &b = acbd.m128_f32[2];
		const float &c = acbd.m128_f32[1];
		const float &d = acbd.m128_f32[3];

		// For derivative
		const float &f31 = f30_41_31_42.m128_f32[2];
		const float &f42 = f30_41_31_42.m128_f32[3];
#else
		// TODO: Maybe compilers could be coaxed into vectorizing this code without the above explicitly...
		float t0 = (t - knots[0]);
		float t1 = (t - knots[1]);
		float t2 = (t - knots[2]);

		float f30 = t0 * div._3_0;
		float f41 = t1 * div._4_1;
		float f52 = t2 * div._5_2;
		float f31 = t1 * div._3_1;
		float f42 = t2 * div._4_2;
		float f32 = t2 * div._3_2;

		float a = (1 - f30) * (1 - f31);
		float b = (f31 * f41);
		float c = (1 - f41) * (1 - f42);
		float d = (f42 * f52);
#endif
		w.weights[0] = a * (1 - f32); // (1-f30)*(1-f31)*(1-f32)
		w.weights[1] = 1 - a - b + ((a + b + c - 1) * f32);
		w.weights[2] = b + ((1 - b - c - d) * f32);
		w.weights[3] = d * f32; // f32*f42*f52

		// Derivative
		float i1 = (1 - f31) * (1 - f32);
		float i2 = f31 * (1 - f32) + (1 - f42) * f32;
		float i3 = f42 * f32;

		float f130 = i1 * div._3_0;
		float f241 = i2 * div._4_1;
		float f352 = i3 * div._5_2;

		w.derivs[0] = 3 * (0 - f130);
		w.derivs[1] = 3 * (f130 - f241);
		w.derivs[2] = 3 * (f241 - f352);
		w.derivs[3] = 3 * (f352 - 0);
	}
public:
	Weight *CalcWeightsAll(u32 key) {
		int tess, count, type;
		FromKey(key, tess, count, type);
		const int num_patches = count - 3;
		Weight *weights = new Weight[tess * num_patches + 1];

	//	float *knots = new float[num_patches + 5];
		float *knots = new float[num_patches + 2]; // Optimized with KnotDiv, must use +5 in theory 
		KnotDiv *divs = new KnotDiv[num_patches];
		CalcKnots(num_patches, type, knots, divs);

		const float inv_tess = 1.0f / (float)tess;
		for (int i = 0; i < num_patches; ++i) {
			const int _tess = (i == num_patches - 1) ? (tess + 1) : tess;
			for (int j = 0; j < _tess; ++j) {
				const int index = i * tess + j;
				const float t = (float)index * inv_tess;
				CalcWeights(t, knots + i, divs[i], weights[index]);
			}
		}

		delete[] knots;
		delete[] divs;

		return weights;
	}

	u32 ToKey(int tess, int count, int type) {
		return tess | (count << 8) | (type << 16);
	}

	void FromKey(u32 key, int &tess, int &count, int &type) {
		tess = key & 0xFF; count = (key >> 8) & 0xFF; type = (key >> 16) & 0xFF;
	}
};

template<class T>
class WeightCache : public T {
private:
	std::unordered_map<u32, Weight*> weightsCache;
public:
	Weight* operator [] (u32 key) {
		Weight *&weights = weightsCache[key];
		if (!weights)
			weights = CalcWeightsAll(key);
		return weights;
	}

	void Clear() {
		for (auto it : weightsCache)
			delete[] it.second;
		weightsCache.clear();
	}
};

static WeightCache<Bezier3DWeight> bezierWeightsCache;
static WeightCache<Spline3DWeight> splineWeightsCache;

struct Weight2D {
	const Weight *u, *v;

	template<class T>
	Weight2D(WeightCache<T> &cache, u32 key_u, u32 key_v) {
		u = cache[key_u];
		v = (key_u != key_v) ? cache[key_v] : u; // Use same weights if u == v
	}
};

void DrawEngineCommon::ClearSplineBezierWeights() {
	bezierWeightsCache.Clear();
	splineWeightsCache.Clear();
}

bool CanUseHardwareTessellation(GEPatchPrimType prim) {
	if (g_Config.bHardwareTessellation && !g_Config.bSoftwareRendering) {
		return CanUseHardwareTransform(PatchPrimToPrim(prim));
	}
	return false;
}

// Prepare mesh of one patch for "Instanced Tessellation".
static void TessellateSplinePatchHardware(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch) {
	SimpleVertex *&vertices = (SimpleVertex*&)dest;

	float inv_u = 1.0f / (float)spatch.tess_u;
	float inv_v = 1.0f / (float)spatch.tess_v;

	// Generating simple input vertices for the spline-computing vertex shader.
	for (int tile_v = 0; tile_v < spatch.tess_v + 1; ++tile_v) {
		for (int tile_u = 0; tile_u < spatch.tess_u + 1; ++tile_u) {
			SimpleVertex &vert = vertices[tile_v * (spatch.tess_u + 1) + tile_u];
			vert.pos.x = (float)tile_u * inv_u;
			vert.pos.y = (float)tile_v * inv_v;

			// TODO: Move to shader uniform and unify this method spline and bezier if necessary.
			// For compute normal
			vert.nrm.x = inv_u;
			vert.nrm.y = inv_v;
		}
	}

	BuildIndex(indices, count, spatch.tess_u, spatch.tess_v, spatch.primType);
}

// Tessellate single patch (4x4 control points)
template<typename T>
class Tessellator {
private:
	const T *const p[4]; // T p[v][u]; 4x4 control points
	T u[4]; // Pre-tessellated U lines
public:
	Tessellator(const T *p, const int idx[4]) : p{ p + idx[0], p + idx[1], p + idx[2], p + idx[3] } {}

	// Linear combination
	T Sample(const T p[4], const float w[4]) {
		return p[0] * w[0] + p[1] * w[1] + p[2] * w[2] + p[3] * w[3];
	}

	void SampleEdgeU(int idx) {
		u[0] = p[0][idx];
		u[1] = p[1][idx];
		u[2] = p[2][idx];
		u[3] = p[3][idx];
	}

	void SampleU(const float weights[4]) {
		if (weights[0] == 1.0f) { SampleEdgeU(0); return; } // weights = {1,0,0,0}, first edge is open.
		if (weights[3] == 1.0f) { SampleEdgeU(3); return; } // weights = {0,0,0,1}, last edge is open.

		u[0] = Sample(p[0], weights);
		u[1] = Sample(p[1], weights);
		u[2] = Sample(p[2], weights);
		u[3] = Sample(p[3], weights);
	}

	T SampleV(const float weights[4]) {
		if (weights[0] == 1.0f) return u[0]; // weights = {1,0,0,0}, first edge is open.
		if (weights[3] == 1.0f) return u[3]; // weights = {0,0,0,1}, last edge is open.

		return Sample(u, weights);
	}
};

// Prepare mesh of one patch for "Instanced Tessellation".
static void TessellateBezierPatchHardware(u8 *&dest, u16 *indices, int &count, int tess_u, int tess_v, GEPatchPrimType primType) {
	SimpleVertex *&vertices = (SimpleVertex*&)dest;

	float inv_u = 1.0f / (float)tess_u;
	float inv_v = 1.0f / (float)tess_v;

	// Generating simple input vertices for the bezier-computing vertex shader.
	for (int tile_v = 0; tile_v < tess_v + 1; ++tile_v) {
		for (int tile_u = 0; tile_u < tess_u + 1; ++tile_u) {
			SimpleVertex &vert = vertices[tile_v * (tess_u + 1) + tile_u];

			vert.pos.x = (float)tile_u * inv_u;
			vert.pos.y = (float)tile_v * inv_v;
		}
	}

	BuildIndex(indices, count, tess_u, tess_v, primType);
}

class SimpleBufferManager {
private:
	u8 *buf_;
	size_t totalSize, maxSize_;
public:
	SimpleBufferManager(u8 *buf, size_t maxSize)
		: buf_(buf), totalSize(0), maxSize_(maxSize) {}

	u8 *Allocate(size_t size) {
		size = (size + 15) & ~15; // Align for 16 bytes

		if ((totalSize + size) > maxSize_)
			return nullptr; // No more memory

		size_t tmp = totalSize;
		totalSize += size;
		return buf_ + tmp;
	}
};

template<class Patch>
class SubdivisionSurface {
private:
	Vec3f *pos;
	Vec4f *col;
	Vec2f *tex;
	u32_le defcolor;
	const Patch &patch;
	const Weight2D &weights;
public:
	SubdivisionSurface(SimpleBufferManager &managedBuf, const SimpleVertex *const *points, const Patch &patch, const Weight2D &weights)
		: patch(patch), weights(weights)
	{
		int size = patch.count_u * patch.count_v;
		pos = (Vec3f *)managedBuf.Allocate(sizeof(Vec3f) * size);
		tex = (Vec2f *)managedBuf.Allocate(sizeof(Vec2f) * size);
		col = (Vec4f *)managedBuf.Allocate(sizeof(Vec4f) * size);
		for (int idx = 0; idx < size; ++idx) {
			pos[idx] = Vec3f(points[idx]->pos);
			tex[idx] = Vec2f(points[idx]->uv);
			col[idx] = Vec4f::FromRGBA(points[idx]->color_32);
		}
		defcolor = points[0]->color_32;
	}

	template <bool sampleNrm, bool sampleCol, bool sampleTex, bool useSSE4>
	void Tessellate(SimpleVertex *vertices, u16 *indices, int &count) {
		const float inv_u = 1.0f / (float)patch.tess_u;
		const float inv_v = 1.0f / (float)patch.tess_v;

		for (int patch_u = 0; patch_u < patch.num_patches_u; ++patch_u) {
			const int tess_u = patch.GetTessU(patch_u);
			for (int patch_v = 0; patch_v < patch.num_patches_v; ++patch_v) {
				const int tess_v = patch.GetTessV(patch_v);

				// Prepare 4x4 control points to tessellate
				const int idx = patch.GetPointIndex(patch_u, patch_v);
				const int idx_v[4] = { idx, idx + patch.count_u, idx + patch.count_u * 2, idx + patch.count_u * 3 };
				Tessellator<Vec3f> tess_pos(pos, idx_v);
				Tessellator<Vec4f> tess_col(col, idx_v);
				Tessellator<Vec2f> tess_tex(tex, idx_v);
				Tessellator<Vec3f> tess_nrm(pos, idx_v);

				for (int tile_u = 0; tile_u < tess_u; ++tile_u) {
					const int index_u = patch.GetIndexU(patch_u, tile_u);
					const Weight &wu = weights.u[index_u];

					// Pre-tessellate U lines
					tess_pos.SampleU(wu.weights);
					if (sampleCol)
						tess_col.SampleU(wu.weights);
					if (sampleTex)
						tess_tex.SampleU(wu.weights);
					if (sampleNrm)
						tess_nrm.SampleU(wu.derivs);

					for (int tile_v = 0; tile_v < tess_v; ++tile_v) {
						const int index_v = patch.GetIndexV(patch_v, tile_v);
						const Weight &wv = weights.v[index_v];

						SimpleVertex &vert = vertices[patch.GetIndex(index_u, index_v, patch_u, patch_v)];

						// Tessellate
						vert.pos = tess_pos.SampleV(wv.weights);
						if (sampleCol) {
							vert.color_32 = tess_col.SampleV(wv.weights).ToRGBA();
						} else {
							vert.color_32 = defcolor;
						}
						if (sampleTex) {
							tess_tex.SampleV(wv.weights).Write(vert.uv);
						} else {
							// Generate texcoord
							vert.uv[0] = patch_u + tile_u * inv_u;
							vert.uv[1] = patch_v + tile_v * inv_v;
						}
						if (sampleNrm) {
							const Vec3f derivU = tess_nrm.SampleV(wv.weights);
							const Vec3f derivV = tess_pos.SampleV(wv.derivs);

							vert.nrm = Cross(derivU, derivV).Normalized(useSSE4);
							if (patch.patchFacing)
								vert.nrm *= -1.0f;
						} else {
							vert.nrm.SetZero();
						}
					}
				}
			}
		}

		patch.BuildIndex(indices, count);
	}

	// Define class TemplateParameterDispatcherTess
	TEMPLATE_PARAMETER_DISPATCHER(Tess, SubdivisionSurface::Tessellate);

	void Tessellate(SimpleVertex *vertices, u16 *indices, int &count, u32 origVertType) {
		using TessFunc = void(SubdivisionSurface::*)(SimpleVertex *, u16 *, int &);
		constexpr int NumParams = 4;
		static TemplateParameterDispatcherTess<TessFunc, NumParams> dispatcher; // Initialize only once

		const bool params[NumParams] = {
			(origVertType & GE_VTYPE_NRM_MASK) != 0,
			(origVertType & GE_VTYPE_COL_MASK) != 0,
			(origVertType & GE_VTYPE_TC_MASK) != 0,
			cpu_info.bSSE4_1,
		};
		TessFunc func = dispatcher.GetFunc(params);
		(this->*func)(vertices, indices, count);
	}
};

template<class Patch, class Cache>
static void SoftwareTessellation(SimpleVertex *vertices, u16 *indices, int &count, const Patch &patch, int origVertType, const SimpleVertex *const *points, SimpleBufferManager &managedBuf, Cache &weightsCache) {
	u32 key_u = weightsCache.ToKey(patch.tess_u, patch.count_u, patch.type_u);
	u32 key_v = weightsCache.ToKey(patch.tess_v, patch.count_v, patch.type_v);
	Weight2D weights(weightsCache, key_u, key_v);

	SubdivisionSurface<Patch> surface(managedBuf, points, patch, weights);
	surface.Tessellate(vertices, indices, count, origVertType);
}

// This maps GEPatchPrimType to GEPrimitiveType.
const GEPrimitiveType primType[] = { GE_PRIM_TRIANGLES, GE_PRIM_LINES, GE_PRIM_POINTS, GE_PRIM_POINTS };

void DrawEngineCommon::SubmitSpline(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead) {
	PROFILE_THIS_SCOPE("spline");
	DispatchFlush();

	// Real hardware seems to draw nothing when given < 4 either U or V.
	if (count_u < 4 || count_v < 4)
		return;

	SimpleBufferManager managedBuf(decoded, DECODED_VERTEX_BUFFER_SIZE);

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	IndexConverter ConvertIndex(vertType, indices);
	if (indices)
		GetIndexBounds(indices, count_u * count_v, vertType, &index_lower_bound, &index_upper_bound);

	VertexDecoder *origVDecoder = GetVertexDecoder((vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24));
	*bytesRead = count_u * count_v * origVDecoder->VertexSize();

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)managedBuf.Allocate(sizeof(SimpleVertex) * (index_upper_bound + 1));
	u8 *temp_buffer = managedBuf.Allocate(sizeof(SimpleVertex) * count_u * count_v);

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

	// If specified as 0, uses 1.
	if (tess_u < 1) tess_u = 1;
	if (tess_v < 1) tess_v = 1;

	// Make an array of pointers to the control points, to get rid of indices.
	const SimpleVertex **points = (const SimpleVertex **)managedBuf.Allocate(sizeof(SimpleVertex *) * count_u * count_v);
	for (int idx = 0; idx < count_u * count_v; idx++)
		points[idx] = simplified_control_points + (indices ? ConvertIndex(idx) : idx);

	int count = 0;
	u8 *dest = splineBuffer;

	SplinePatchLocal patch;
	patch.tess_u = tess_u;
	patch.tess_v = tess_v;
	patch.type_u = type_u;
	patch.type_v = type_v;
	patch.count_u = count_u;
	patch.count_v = count_v;
	patch.num_patches_u = count_u - 3;
	patch.num_patches_v = count_v - 3;
	patch.primType = prim_type;
	patch.patchFacing = patchFacing;

	if (CanUseHardwareTessellation(prim_type)) {
		tessDataTransfer->SendDataToShader(points, count_u * count_v, origVertType);
		TessellateSplinePatchHardware(dest, quadIndices_, count, patch);
		numPatches = (count_u - 3) * (count_v - 3);
	} else {
		patch.Init(SPLINE_BUFFER_SIZE / vertexSize);
		SoftwareTessellation((SimpleVertex *)splineBuffer, quadIndices_, count, patch, origVertType, points, managedBuf, splineWeightsCache);
	}

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if ((origVertType & GE_VTYPE_TC_MASK) != 0) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0.0f;
		gstate_c.uv.vOff = 0.0f;
	}

	uint32_t vertTypeID = GetVertTypeID(vertTypeWithIndex16, gstate.getUVGenMode());

	int generatedBytesRead;
	DispatchSubmitPrim(splineBuffer, quadIndices_, PatchPrimToPrim(prim_type), count, vertTypeID, &generatedBytesRead);

	DispatchFlush();

	if ((origVertType & GE_VTYPE_TC_MASK) != 0) {
		gstate_c.uv = prevUVScale;
	}
}

void DrawEngineCommon::SubmitBezier(const void *control_points, const void *indices, int tess_u, int tess_v, int count_u, int count_v, GEPatchPrimType prim_type, bool computeNormals, bool patchFacing, u32 vertType, int *bytesRead) {
	PROFILE_THIS_SCOPE("bezier");
	DispatchFlush();

	// Real hardware seems to draw nothing when given < 4 either U or V.
	// This would result in num_patches_u / num_patches_v being 0.
	if (count_u < 4 || count_v < 4)
		return;

	SimpleBufferManager managedBuf(decoded, DECODED_VERTEX_BUFFER_SIZE);

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	IndexConverter ConvertIndex(vertType, indices);
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	VertexDecoder *origVDecoder = GetVertexDecoder((vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24));
	*bytesRead = count_u * count_v * origVDecoder->VertexSize();

	// Simplify away bones and morph before proceeding
	// There are normally not a lot of control points so just splitting decoded should be reasonably safe, although not great.
	SimpleVertex *simplified_control_points = (SimpleVertex *)managedBuf.Allocate(sizeof(SimpleVertex) * (index_upper_bound + 1));
	u8 *temp_buffer = managedBuf.Allocate(sizeof(SimpleVertex) * count_u * count_v);

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

	// If specified as 0, uses 1.
	if (tess_u < 1) tess_u = 1;
	if (tess_v < 1) tess_v = 1;

	// Make an array of pointers to the control points, to get rid of indices.
	const SimpleVertex **points = (const SimpleVertex **)managedBuf.Allocate(sizeof(SimpleVertex *) * count_u * count_v);
	for (int idx = 0; idx < count_u * count_v; idx++)
		points[idx] = simplified_control_points + (indices ? ConvertIndex(idx) : idx);

	int count = 0;
	u8 *dest = splineBuffer;
	u16 *inds = quadIndices_;

	// Bezier patches share less control points than spline patches. Otherwise they are pretty much the same (except bezier don't support the open/close thing)
	int num_patches_u = (count_u - 1) / 3;
	int num_patches_v = (count_v - 1) / 3;
	if (CanUseHardwareTessellation(prim_type)) {
		tessDataTransfer->SendDataToShader(points, count_u * count_v, origVertType);
		TessellateBezierPatchHardware(dest, inds, count, tess_u, tess_v, prim_type);
		numPatches = num_patches_u * num_patches_v;
	} else {
		BezierPatch patch;
		patch.tess_u = tess_u;
		patch.tess_v = tess_v;
		patch.count_u = count_u;
		patch.count_v = count_v;
		patch.num_patches_u = (count_u - 1) / 3;
		patch.num_patches_v = (count_v - 1) / 3;
		patch.primType = prim_type;
		patch.patchFacing = patchFacing;

		patch.Init(SPLINE_BUFFER_SIZE / vertexSize);
		SoftwareTessellation((SimpleVertex *)splineBuffer, quadIndices_, count, patch, origVertType, points, managedBuf, bezierWeightsCache);
	}

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if (origVertType & GE_VTYPE_TC_MASK) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0;
		gstate_c.uv.vOff = 0;
	}

	uint32_t vertTypeID = GetVertTypeID(vertTypeWithIndex16, gstate.getUVGenMode());
	int generatedBytesRead;
	DispatchSubmitPrim(splineBuffer, quadIndices_, PatchPrimToPrim(prim_type), count, vertTypeID, &generatedBytesRead);

	DispatchFlush();

	if (origVertType & GE_VTYPE_TC_MASK) {
		gstate_c.uv = prevUVScale;
	}
}
