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

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/Profiler/Profiler.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"  // only needed for UVScale stuff

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

namespace Spline {

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

void BuildIndex(u16 *indices, int &count, int num_u, int num_v, GEPatchPrimType prim_type, int total) {
	for (int v = 0; v < num_v; ++v) {
		for (int u = 0; u < num_u; ++u) {
			int idx0 = v * (num_u + 1) + u + total; // Top left
			int idx2 = (v + 1) * (num_u + 1) + u + total; // Bottom left

			CopyQuadIndex(indices, prim_type, idx0, idx0 + 1, idx2, idx2 + 1);
			count += 6;
		}
	}
}

class Bezier3DWeight {
private:
	static void CalcWeights(float t, Weight &w) {
		// Bernstein 3D basis polynomial
		w.basis[0] = (1 - t) * (1 - t) * (1 - t);
		w.basis[1] = 3 * t * (1 - t) * (1 - t);
		w.basis[2] = 3 * t * t * (1 - t);
		w.basis[3] = t * t * t;

		// Derivative
		w.deriv[0] = -3 * (1 - t) * (1 - t);
		w.deriv[1] = 9 * t * t - 12 * t + 3;
		w.deriv[2] = 3 * (2 - 3 * t) * t;
		w.deriv[3] = 3 * t * t;
	}
public:
	static Weight *CalcWeightsAll(u32 key) {
		int tess = (int)key;
		Weight *weights = new Weight[tess + 1];
		const float inv_tess = 1.0f / (float)tess;
		for (int i = 0; i < tess + 1; ++i) {
			const float t = (float)i * inv_tess;
			CalcWeights(t, weights[i]);
		}
		return weights;
	}

	static u32 ToKey(int tess, int count, int type) {
		return tess;
	}

	static int CalcSize(int tess, int count) {
		return tess + 1;
	}

	static WeightCache<Bezier3DWeight> weightsCache;
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
	static void CalcKnots(int n, int type, float *knots, KnotDiv *divs) {
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

	static void CalcWeights(float t, const float *knots, const KnotDiv &div, Weight &w) {
#ifdef _M_SSE
		const __m128 knot012 = _mm_loadu_ps(knots);
		const __m128 t012 = _mm_sub_ps(_mm_set_ps1(t), knot012);
		const __m128 f30_41_52 = _mm_mul_ps(t012, _mm_loadu_ps(&div._3_0));
		const __m128 f52_31_42 = _mm_mul_ps(t012, _mm_loadu_ps(&div._5_2));

		// Following comments are for explains order of the multiply.
	//	float a = (1-f30)*(1-f31);
	//	float c = (1-f41)*(1-f42);
	//	float b = (  f31 *   f41);
	//	float d = (  f42 *   f52);
		const __m128 f30_41_31_42 = _mm_shuffle_ps(f30_41_52, f52_31_42, _MM_SHUFFLE(2, 1, 1, 0));
		const __m128 f31_42_41_52 = _mm_shuffle_ps(f52_31_42, f30_41_52, _MM_SHUFFLE(2, 1, 2, 1));
		const __m128 c1_1_0_0 = { 1, 1, 0, 0 };
		const __m128 acbd = _mm_mul_ps(_mm_sub_ps(c1_1_0_0, f30_41_31_42), _mm_sub_ps(c1_1_0_0, f31_42_41_52));

		alignas(16) float f_t012[4];
		alignas(16) float f_acbd[4];
		alignas(16) float f_f30_41_31_42[4];
		_mm_store_ps(f_t012, t012);
		_mm_store_ps(f_acbd, acbd);
		_mm_store_ps(f_f30_41_31_42, f30_41_31_42);

		const float &f32 = f_t012[2];

		const float &a = f_acbd[0];
		const float &b = f_acbd[2];
		const float &c = f_acbd[1];
		const float &d = f_acbd[3];

		// For derivative
		const float &f31 = f_f30_41_31_42[2];
		const float &f42 = f_f30_41_31_42[3];
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
		w.basis[0] = a * (1 - f32); // (1-f30)*(1-f31)*(1-f32)
		w.basis[1] = 1 - a - b + ((a + b + c - 1) * f32);
		w.basis[2] = b + ((1 - b - c - d) * f32);
		w.basis[3] = d * f32; // f32*f42*f52

		// Derivative
		float i1 = (1 - f31) * (1 - f32);
		float i2 = f31 * (1 - f32) + (1 - f42) * f32;
		float i3 = f42 * f32;

		float f130 = i1 * div._3_0;
		float f241 = i2 * div._4_1;
		float f352 = i3 * div._5_2;

		w.deriv[0] = 3 * (0 - f130);
		w.deriv[1] = 3 * (f130 - f241);
		w.deriv[2] = 3 * (f241 - f352);
		w.deriv[3] = 3 * (f352 - 0);
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
			const int start = (i == 0) ? 0 : 1;
			for (int j = start; j <= tess; ++j) {
				const int index = i * tess + j;
				const float t = (float)index * inv_tess;
				CalcWeights(t, knots + i, divs[i], weights[index]);
			}
		}

		delete[] knots;
		delete[] divs;

		return weights;
	}

	static u32 ToKey(int tess, int count, int type) {
		return tess | (count << 8) | (type << 16);
	}

	static void FromKey(u32 key, int &tess, int &count, int &type) {
		tess = key & 0xFF; count = (key >> 8) & 0xFF; type = (key >> 16) & 0xFF;
	}

	static int CalcSize(int tess, int count) {
		return (count - 3) * tess + 1;
	}

	static WeightCache<Spline3DWeight> weightsCache;
};

WeightCache<Bezier3DWeight> Bezier3DWeight::weightsCache;
WeightCache<Spline3DWeight> Spline3DWeight::weightsCache;

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

ControlPoints::ControlPoints(const SimpleVertex *const *points, int size, SimpleBufferManager &managedBuf) {
	pos = (Vec3f *)managedBuf.Allocate(sizeof(Vec3f) * size);
	tex = (Vec2f *)managedBuf.Allocate(sizeof(Vec2f) * size);
	col = (Vec4f *)managedBuf.Allocate(sizeof(Vec4f) * size);
	if (pos && tex && col)
		Convert(points, size);
}

void ControlPoints::Convert(const SimpleVertex *const *points, int size) {
	for (int i = 0; i < size; ++i) {
		pos[i] = Vec3f(points[i]->pos);
		tex[i] = Vec2f(points[i]->uv);
		col[i] = Vec4f::FromRGBA(points[i]->color_32);
	}
	defcolor = points[0]->color_32;
}

template<class Surface>
class SubdivisionSurface {
public:
	template <bool sampleNrm, bool sampleCol, bool sampleTex, bool useSSE4, bool patchFacing>
	static void Tessellate(OutputBuffers &output, const Surface &surface, const ControlPoints &points, const Weight2D &weights) {
		const float inv_u = 1.0f / (float)surface.tess_u;
		const float inv_v = 1.0f / (float)surface.tess_v;

		for (int patch_u = 0; patch_u < surface.num_patches_u; ++patch_u) {
			const int start_u = surface.GetTessStart(patch_u);
			for (int patch_v = 0; patch_v < surface.num_patches_v; ++patch_v) {
				const int start_v = surface.GetTessStart(patch_v);

				// Prepare 4x4 control points to tessellate
				const int idx = surface.GetPointIndex(patch_u, patch_v);
				const int idx_v[4] = { idx, idx + surface.num_points_u, idx + surface.num_points_u * 2, idx + surface.num_points_u * 3 };
				Tessellator<Vec3f> tess_pos(points.pos, idx_v);
				Tessellator<Vec4f> tess_col(points.col, idx_v);
				Tessellator<Vec2f> tess_tex(points.tex, idx_v);
				Tessellator<Vec3f> tess_nrm(points.pos, idx_v);

				for (int tile_u = start_u; tile_u <= surface.tess_u; ++tile_u) {
					const int index_u = surface.GetIndexU(patch_u, tile_u);
					const Weight &wu = weights.u[index_u];

					// Pre-tessellate U lines
					tess_pos.SampleU(wu.basis);
					if constexpr (sampleCol)
						tess_col.SampleU(wu.basis);
					if constexpr (sampleTex)
						tess_tex.SampleU(wu.basis);
					if constexpr (sampleNrm)
						tess_nrm.SampleU(wu.deriv);

					for (int tile_v = start_v; tile_v <= surface.tess_v; ++tile_v) {
						const int index_v = surface.GetIndexV(patch_v, tile_v);
						const Weight &wv = weights.v[index_v];

						SimpleVertex &vert = output.vertices[surface.GetIndex(index_u, index_v, patch_u, patch_v)];

						// Tessellate
						vert.pos = tess_pos.SampleV(wv.basis);
						if constexpr (sampleCol) {
							vert.color_32 = tess_col.SampleV(wv.basis).ToRGBA();
						} else {
							vert.color_32 = points.defcolor;
						}
						if constexpr (sampleTex) {
							tess_tex.SampleV(wv.basis).Write(vert.uv);
						} else {
							// Generate texcoord
							vert.uv[0] = patch_u + tile_u * inv_u;
							vert.uv[1] = patch_v + tile_v * inv_v;
						}
						if constexpr (sampleNrm) {
							const Vec3f derivU = tess_nrm.SampleV(wv.basis);
							const Vec3f derivV = tess_pos.SampleV(wv.deriv);

							vert.nrm = Cross(derivU, derivV).Normalized(useSSE4);
							if constexpr (patchFacing)
								vert.nrm *= -1.0f;
						} else {
							vert.nrm.SetZero();
							vert.nrm.z = 1.0f;
						}
					}
				}
			}
		}

		surface.BuildIndex(output.indices, output.count);
	}

	using TessFunc = void(*)(OutputBuffers &, const Surface &, const ControlPoints &, const Weight2D &);
	TEMPLATE_PARAMETER_DISPATCHER_FUNCTION(Tess, SubdivisionSurface::Tessellate, TessFunc);

	static void Tessellate(OutputBuffers &output, const Surface &surface, const ControlPoints &points, const Weight2D &weights, u32 origVertType) {
		const bool params[] = {
			(origVertType & GE_VTYPE_NRM_MASK) != 0 || gstate.isLightingEnabled(),
			(origVertType & GE_VTYPE_COL_MASK) != 0,
			(origVertType & GE_VTYPE_TC_MASK) != 0,
			cpu_info.bSSE4_1,
			surface.patchFacing,
		};
		static TemplateParameterDispatcher<TessFunc, ARRAY_SIZE(params), Tess> dispatcher; // Initialize only once

		TessFunc func = dispatcher.GetFunc(params);
		func(output, surface, points, weights);
	}
};

template<class Surface>
void SoftwareTessellation(OutputBuffers &output, const Surface &surface, u32 origVertType, const ControlPoints &points) {
	using WeightType = typename Surface::WeightType;
	u32 key_u = WeightType::ToKey(surface.tess_u, surface.num_points_u, surface.type_u);
	u32 key_v = WeightType::ToKey(surface.tess_v, surface.num_points_v, surface.type_v);
	Weight2D weights(WeightType::weightsCache, key_u, key_v);

	SubdivisionSurface<Surface>::Tessellate(output, surface, points, weights, origVertType);
}

template void SoftwareTessellation<BezierSurface>(OutputBuffers &output, const BezierSurface &surface, u32 origVertType, const ControlPoints &points);
template void SoftwareTessellation<SplineSurface>(OutputBuffers &output, const SplineSurface &surface, u32 origVertType, const ControlPoints &points);

template<class Surface>
static void HardwareTessellation(OutputBuffers &output, const Surface &surface, u32 origVertType,
	const SimpleVertex *const *points, TessellationDataTransfer *tessDataTransfer) {
	using WeightType = typename Surface::WeightType;
	u32 key_u = WeightType::ToKey(surface.tess_u, surface.num_points_u, surface.type_u);
	u32 key_v = WeightType::ToKey(surface.tess_v, surface.num_points_v, surface.type_v);
	Weight2D weights(WeightType::weightsCache, key_u, key_v);
	weights.size_u = WeightType::CalcSize(surface.tess_u, surface.num_points_u);
	weights.size_v = WeightType::CalcSize(surface.tess_v, surface.num_points_v);
	tessDataTransfer->SendDataToShader(points, surface.num_points_u, surface.num_points_v, origVertType, weights);

	// Generating simple input vertices for the spline-computing vertex shader.
	float inv_u = 1.0f / (float)surface.tess_u;
	float inv_v = 1.0f / (float)surface.tess_v;
	for (int patch_u = 0; patch_u < surface.num_patches_u; ++patch_u) {
		const int start_u = surface.GetTessStart(patch_u);
		for (int patch_v = 0; patch_v < surface.num_patches_v; ++patch_v) {
			const int start_v = surface.GetTessStart(patch_v);
			for (int tile_u = start_u; tile_u <= surface.tess_u; ++tile_u) {
				const int index_u = surface.GetIndexU(patch_u, tile_u);
				for (int tile_v = start_v; tile_v <= surface.tess_v; ++tile_v) {
					const int index_v = surface.GetIndexV(patch_v, tile_v);
					SimpleVertex &vert = output.vertices[surface.GetIndex(index_u, index_v, patch_u, patch_v)];
					// Index for the weights
					vert.pos.x = index_u;
					vert.pos.y = index_v;
					// For texcoord generation
					vert.nrm.x = patch_u + (float)tile_u * inv_u;
					vert.nrm.y = patch_v + (float)tile_v * inv_v;
					// Patch position
					vert.pos.z = patch_u;
					vert.nrm.z = patch_v;
				}
			}
		}
	}
	surface.BuildIndex(output.indices, output.count);
}

} // namespace Spline

using namespace Spline;

void DrawEngineCommon::ClearSplineBezierWeights() {
	Bezier3DWeight::weightsCache.Clear();
	Spline3DWeight::weightsCache.Clear();
}

// Specialize to make instance (to avoid link error).
template void DrawEngineCommon::SubmitCurve<BezierSurface>(const void *control_points, const void *indices, BezierSurface &surface, u32 vertType, int *bytesRead, const char *scope);
template void DrawEngineCommon::SubmitCurve<SplineSurface>(const void *control_points, const void *indices, SplineSurface &surface, u32 vertType, int *bytesRead, const char *scope);

template<class Surface>
void DrawEngineCommon::SubmitCurve(const void *control_points, const void *indices, Surface &surface, u32 vertType, int *bytesRead, const char *scope) {
	PROFILE_THIS_SCOPE(scope);

	// Real hardware seems to draw nothing when given < 4 either U or V.
	// This would result in num_patches_u / num_patches_v being 0.
	if (surface.num_points_u < 4 || surface.num_points_v < 4)
		return;

	SimpleBufferManager managedBuf(decoded_, DECODED_VERTEX_BUFFER_SIZE / 2);

	int num_points = surface.num_points_u * surface.num_points_v;
	u16 index_lower_bound = 0;
	u16 index_upper_bound = num_points - 1;
	IndexConverter ConvertIndex(vertType, indices);
	if (indices)
		GetIndexBounds(indices, num_points, vertType, &index_lower_bound, &index_upper_bound);

	VertexDecoder *origVDecoder = GetVertexDecoder(GetVertTypeID(vertType, gstate.getUVGenMode(), decOptions_.applySkinInDecode));
	*bytesRead = num_points * origVDecoder->VertexSize();

	// Simplify away bones and morph before proceeding
	// There are normally not a lot of control points so just splitting decoded should be reasonably safe, although not great.
	SimpleVertex *simplified_control_points = (SimpleVertex *)managedBuf.Allocate(sizeof(SimpleVertex) * (index_upper_bound + 1));
	if (!simplified_control_points) {
		ERROR_LOG(Log::G3D, "Failed to allocate space for simplified control points, skipping curve draw");
		return;
	}

	u8 *temp_buffer = managedBuf.Allocate(sizeof(SimpleVertex) * num_points);
	if (!temp_buffer) {
		ERROR_LOG(Log::G3D, "Failed to allocate space for temp buffer, skipping curve draw");
		return;
	}

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(Log::G3D, "Something went really wrong, vertex size: %d vs %d", vertexSize, (int)sizeof(SimpleVertex));
	}

	// Make an array of pointers to the control points, to get rid of indices.
	const SimpleVertex **points = (const SimpleVertex **)managedBuf.Allocate(sizeof(SimpleVertex *) * num_points);
	if (!points) {
		ERROR_LOG(Log::G3D, "Failed to allocate space for control point pointers, skipping curve draw");
		return;
	}
	for (int idx = 0; idx < num_points; idx++)
		points[idx] = simplified_control_points + (indices ? ConvertIndex(idx) : idx);

	OutputBuffers output;
	output.vertices = (SimpleVertex *)(decoded_ + DECODED_VERTEX_BUFFER_SIZE / 2);
	output.indices = decIndex_;
	output.count = 0;

	int maxVerts = DECODED_VERTEX_BUFFER_SIZE / 2 / vertexSize;

	surface.Init(maxVerts);

	if (CanUseHardwareTessellation(surface.primType)) {
		HardwareTessellation(output, surface, origVertType, points, tessDataTransfer);
	} else {
		ControlPoints cpoints(points, num_points, managedBuf);
		if (cpoints.IsValid())
			SoftwareTessellation(output, surface, origVertType, cpoints);
		else
			ERROR_LOG(Log::G3D, "Failed to allocate space for control point values, skipping curve draw");
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

	uint32_t vertTypeID = GetVertTypeID(vertTypeWithIndex16, gstate.getUVGenMode(), decOptions_.applySkinInDecode);
	int generatedBytesRead;
	if (output.count)
		DispatchSubmitPrim(output.vertices, output.indices, PatchPrimToPrim(surface.primType), output.count, vertTypeID, true, &generatedBytesRead);

	if (flushOnParams_)
		DispatchFlush();

	if (origVertType & GE_VTYPE_TC_MASK) {
		gstate_c.uv = prevUVScale;
	}
}
