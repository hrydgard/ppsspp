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

/*
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef min
#undef max
*/

#include <string.h>
#include <algorithm>

#include "profiler/profiler.h"

#include "Common/CPUDetect.h"
#include "Core/Config.h"

#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#if defined(_M_SSE)
#include <emmintrin.h>

inline __m128 SSECrossProduct(__m128 a, __m128 b)
{
	const __m128 left = _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1)), _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2)));
	const __m128 right = _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2)), _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1)));
	return _mm_sub_ps(left, right);
}

inline __m128 SSENormalizeMultiplierSSE2(__m128 v)
{
	const __m128 sq = _mm_mul_ps(v, v);
	const __m128 r2 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 1));
	const __m128 r3 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 2));
	const __m128 res = _mm_add_ss(r3, _mm_add_ss(r2, sq));

	const __m128 rt = _mm_rsqrt_ss(res);
	return _mm_shuffle_ps(rt, rt, _MM_SHUFFLE(0, 0, 0, 0));
}

#if _M_SSE >= 0x401
#include <smmintrin.h>

inline __m128 SSENormalizeMultiplierSSE4(__m128 v)
{
	return _mm_rsqrt_ps(_mm_dp_ps(v, v, 0xFF));
}

inline __m128 SSENormalizeMultiplier(bool useSSE4, __m128 v)
{
	if (useSSE4)
		return SSENormalizeMultiplierSSE4(v);
	return SSENormalizeMultiplierSSE2(v);
}
#else
inline __m128 SSENormalizeMultiplier(bool useSSE4, __m128 v)
{
	return SSENormalizeMultiplierSSE2(v);
}
#endif

#endif


#define START_OPEN 1
#define END_OPEN 2



static void CopyQuad(u8 *&dest, const SimpleVertex *v1, const SimpleVertex *v2, const SimpleVertex* v3, const SimpleVertex *v4) {
	int vertexSize = sizeof(SimpleVertex);
	memcpy(dest, v1, vertexSize);
	dest += vertexSize;
	memcpy(dest, v2, vertexSize);
	dest += vertexSize;
	memcpy(dest, v3, vertexSize);
	dest += vertexSize;
	memcpy(dest, v4, vertexSize);
	dest += vertexSize;
}

static void CopyQuadIndex(u16 *&indices, GEPatchPrimType type, const int idx0, const int idx1, const int idx2, const int idx3) {
	if (type == GE_PATCHPRIM_LINES) {
		*(indices++) = idx0;
		*(indices++) = idx2;
		*(indices++) = idx1;
		*(indices++) = idx3;
		*(indices++) = idx1;
		*(indices++) = idx2;
	}
	else {
		*(indices++) = idx0;
		*(indices++) = idx2;
		*(indices++) = idx1;
		*(indices++) = idx1;
		*(indices++) = idx2;
		*(indices++) = idx3;
	}
}

#undef b2

// Bernstein basis functions
inline float bern0(float x) { return (1 - x) * (1 - x) * (1 - x); }
inline float bern1(float x) { return 3 * x * (1 - x) * (1 - x); }
inline float bern2(float x) { return 3 * x * x * (1 - x); }
inline float bern3(float x) { return x * x * x; }

// Not sure yet if these have any use
inline float bern0deriv(float x) { return -3 * (x - 1) * (x - 1); }
inline float bern1deriv(float x) { return 9 * x * x - 12 * x + 3; }
inline float bern2deriv(float x) { return 3 * (2 - 3 * x) * x; }
inline float bern3deriv(float x) { return 3 * x * x; }

// http://en.wikipedia.org/wiki/Bernstein_polynomial
static Vec3Packedf Bernstein3D(const Vec3Packedf& p0, const Vec3Packedf& p1, const Vec3Packedf& p2, const Vec3Packedf& p3, float x) {
	if (x == 0) return p0;
	else if (x == 1) return p3;
	return p0 * bern0(x) + p1 * bern1(x) + p2 * bern2(x) + p3 * bern3(x);
}

static Vec3Packedf Bernstein3DDerivative(const Vec3Packedf& p0, const Vec3Packedf& p1, const Vec3Packedf& p2, const Vec3Packedf& p3, float x) {
	return p0 * bern0deriv(x) + p1 * bern1deriv(x) + p2 * bern2deriv(x) + p3 * bern3deriv(x);
}

static void spline_n_4(int i, float t, float *knot, float *splineVal) {
	knot += i + 1;

#ifdef _M_SSE
	const __m128 knot012 = _mm_loadu_ps(&knot[0]);
	const __m128 knot345 = _mm_loadu_ps(&knot[3]);
	const __m128 t012 = _mm_sub_ps(_mm_set_ps1(t), knot012);
	const __m128 f30_41_52 = _mm_div_ps(t012, _mm_sub_ps(knot345, knot012));

	const __m128 knot343 = _mm_shuffle_ps(knot345, knot345, _MM_SHUFFLE(3, 0, 1, 0));
	const __m128 knot122 = _mm_shuffle_ps(knot012, knot012, _MM_SHUFFLE(3, 2, 2, 1));
	const __m128 t122 = _mm_shuffle_ps(t012, t012, _MM_SHUFFLE(3, 2, 2, 1));
	const __m128 f31_42_32 = _mm_div_ps(t122, _mm_sub_ps(knot343, knot122));

	// It's still faster to use SSE, even with this.
	float MEMORY_ALIGNED16(ff30_41_52[4]);
	float MEMORY_ALIGNED16(ff31_42_32[4]);
	_mm_store_ps(ff30_41_52, f30_41_52);
	_mm_store_ps(ff31_42_32, f31_42_32);

	const float &f30 = ff30_41_52[0];
	const float &f41 = ff30_41_52[1];
	const float &f52 = ff30_41_52[2];
	const float &f31 = ff31_42_32[0];
	const float &f42 = ff31_42_32[1];
	const float &f32 = ff31_42_32[2];
#else
	// TODO: Maybe compilers could be coaxed into vectorizing this code without the above explicitly...
	float t0 = (t - knot[0]);
	float t1 = (t - knot[1]);
	float t2 = (t - knot[2]);
	// TODO: All our knots are integers so we should be able to get rid of these divisions (How?)
	float f30 = t0/(knot[3]-knot[0]);
	float f41 = t1/(knot[4]-knot[1]);
	float f52 = t2/(knot[5]-knot[2]);
	float f31 = t1/(knot[3]-knot[1]);
	float f42 = t2/(knot[4]-knot[2]);
	float f32 = t2/(knot[3]-knot[2]);
#endif

	float a = (1-f30)*(1-f31);
	float b = (f31*f41);
	float c = (1-f41)*(1-f42);
	float d = (f42*f52);

	splineVal[0] = a-(a*f32);
	splineVal[1] = 1-a-b+((a+b+c-1)*f32);
	splineVal[2] = b+((1-b-c-d)*f32);
	splineVal[3] = d*f32;
}

// knot should be an array sized n + 5  (n + 1 + 1 + degree (cubic))
static void spline_knot(int n, int type, float *knot) {
	memset(knot, 0, sizeof(float) * (n + 5));
	for (int i = 0; i < n - 1; ++i)
		knot[i + 3] = (float)i;

	if ((type & 1) == 0) {
		knot[0] = -3;
		knot[1] = -2;
		knot[2] = -1;
	}
	if ((type & 2) == 0) {
		knot[n + 2] = (float)(n - 1);
		knot[n + 3] = (float)(n);
		knot[n + 4] = (float)(n + 1);
	} else {
		knot[n + 2] = (float)(n - 2);
		knot[n + 3] = (float)(n - 2);
		knot[n + 4] = (float)(n - 2);
	}
}

static void _SplinePatchLowQuality(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType) {
	// Fast and easy way - just draw the control points, generate some very basic normal vector substitutes.
	// Very inaccurate but okay for Loco Roco. Maybe should keep it as an option because it's fast.

	const int tile_min_u = (spatch.type_u & START_OPEN) ? 0 : 1;
	const int tile_min_v = (spatch.type_v & START_OPEN) ? 0 : 1;
	const int tile_max_u = (spatch.type_u & END_OPEN) ? spatch.count_u - 1 : spatch.count_u - 2;
	const int tile_max_v = (spatch.type_v & END_OPEN) ? spatch.count_v - 1 : spatch.count_v - 2;

	float tu_width = (float)spatch.count_u - 3.0f;
	float tv_height = (float)spatch.count_v - 3.0f;
	tu_width /= (float)(tile_max_u - tile_min_u);
	tv_height /= (float)(tile_max_v - tile_min_v);

	GEPatchPrimType prim_type = gstate.getPatchPrimitiveType();

	int i = 0;
	for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
		for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
			int point_index = tile_u + tile_v * spatch.count_u;

			SimpleVertex v0 = *spatch.points[point_index];
			SimpleVertex v1 = *spatch.points[point_index + 1];
			SimpleVertex v2 = *spatch.points[point_index + spatch.count_u];
			SimpleVertex v3 = *spatch.points[point_index + spatch.count_u + 1];

			// Generate UV. TODO: Do this even if UV specified in control points?
			if ((origVertType & GE_VTYPE_TC_MASK) == 0) {
				float u = (tile_u - tile_min_u) * tu_width;
				float v = (tile_v - tile_min_v) * tv_height;

				v0.uv[0] = u;
				v0.uv[1] = v;
				v1.uv[0] = u + tu_width;
				v1.uv[1] = v;
				v2.uv[0] = u;
				v2.uv[1] = v + tv_height;
				v3.uv[0] = u + tu_width;
				v3.uv[1] = v + tv_height;
			}

			// Generate normal if lighting is enabled (otherwise there's no point).
			// This is a really poor quality algorithm, we get facet normals.
			if (gstate.isLightingEnabled()) {
				Vec3Packedf norm = Cross(v1.pos - v0.pos, v2.pos - v0.pos);
				norm.Normalize();
				if (gstate.patchfacing & 1)
					norm *= -1.0f;
				v0.nrm = norm;
				v1.nrm = norm;
				v2.nrm = norm;
				v3.nrm = norm;
			}

			int idx0 = i * 4 + 0;
			int idx1 = i * 4 + 1;
			int idx2 = i * 4 + 2;
			int idx3 = i * 4 + 3;
			i++;

			CopyQuad(dest, &v0, &v1, &v2, &v3);
			CopyQuadIndex(indices, prim_type, idx0, idx1, idx2, idx3);
			count += 6;
		}
	}

}

static inline void AccumulateWeighted(Vec3f &out, const Vec3Packedf &in, const Vec4f &w) {
#ifdef _M_SSE
	out.vec = _mm_add_ps(out.vec, _mm_mul_ps(_mm_loadu_ps(in.AsArray()), w.vec));
#else
	out += in * w.x;
#endif
}

static inline void AccumulateWeighted(Vec4f &out, const Vec4f &in, const Vec4f &w) {
#ifdef _M_SSE
	out.vec = _mm_add_ps(out.vec, _mm_mul_ps(in.vec, w.vec));
#else
	out += in * w;
#endif
}

template <bool origNrm, bool origCol, bool origTc, bool useSSE4>
static void SplinePatchFullQuality(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int quality, int maxVertices) {
	// Full (mostly) correct tessellation of spline patches.
	// Not very fast.

	float *knot_u = new float[spatch.count_u + 4];
	float *knot_v = new float[spatch.count_v + 4];
	spline_knot(spatch.count_u - 1, spatch.type_u, knot_u);
	spline_knot(spatch.count_v - 1, spatch.type_v, knot_v);

	// Increase tesselation based on the size. Should be approximately right?
	int patch_div_s = (spatch.count_u - 3) * gstate.getPatchDivisionU();
	int patch_div_t = (spatch.count_v - 3) * gstate.getPatchDivisionV();
	if (quality > 1) {
		patch_div_s /= quality;
		patch_div_t /= quality;
	}

	// Downsample until it fits, in case crazy tesselation factors are sent.
	while ((patch_div_s + 1) * (patch_div_t + 1) > maxVertices) {
		patch_div_s /= 2;
		patch_div_t /= 2;
	}

	if (patch_div_s < 2) patch_div_s = 2;
	if (patch_div_t < 2) patch_div_t = 2;

	// First compute all the vertices and put them in an array
	SimpleVertex *&vertices = (SimpleVertex*&)dest;

	float tu_width = (float)spatch.count_u - 3.0f;
	float tv_height = (float)spatch.count_v - 3.0f;

	// int max_idx = spatch.count_u * spatch.count_v;

	bool computeNormals = gstate.isLightingEnabled();

	float one_over_patch_div_s = 1.0f / (float)(patch_div_s);
	float one_over_patch_div_t = 1.0f / (float)(patch_div_t);

	for (int tile_v = 0; tile_v < patch_div_t + 1; tile_v++) {
		float v = (float)tile_v * (float)(spatch.count_v - 3) * one_over_patch_div_t;
		if (v < 0.0f)
			v = 0.0f;
		for (int tile_u = 0; tile_u < patch_div_s + 1; tile_u++) {
			float u = (float)tile_u * (float)(spatch.count_u - 3) * one_over_patch_div_s;
			if (u < 0.0f)
				u = 0.0f;
			SimpleVertex *vert = &vertices[tile_v * (patch_div_s + 1) + tile_u];
			Vec4f vert_color(0, 0, 0, 0);
			Vec3f vert_pos;
			vert_pos.SetZero();
			Vec3f vert_nrm;
			if (origNrm) {
				vert_nrm.SetZero();
			}
			if (origCol) {
				vert_color.SetZero();
			} else {
				memcpy(vert->color, spatch.points[0]->color, 4);
			}
			if (origTc) {
				vert->uv[0] = 0.0f;
				vert->uv[1] = 0.0f;
			} else {
				vert->uv[0] = tu_width * ((float)tile_u * one_over_patch_div_s);
				vert->uv[1] = tv_height * ((float)tile_v * one_over_patch_div_t);
			}


			// Collect influences from surrounding control points.
			float u_weights[4];
			float v_weights[4];

			int iu = (int)u;
			int iv = (int)v;

			// TODO: Would really like to fix the surrounding logic somehow to get rid of these but I can't quite get it right..
			// Without the previous epsilons and with large count_u, we will end up doing an out of bounds access later without these.
			if (iu >= spatch.count_u - 3) iu = spatch.count_u - 4;
			if (iv >= spatch.count_v - 3) iv = spatch.count_v - 4;

			spline_n_4(iu, u, knot_u, u_weights);
			spline_n_4(iv, v, knot_v, v_weights);

			// Handle degenerate patches. without this, spatch.points[] may read outside the number of initialized points.
			int patch_w = std::min(spatch.count_u - iu, 4);
			int patch_h = std::min(spatch.count_v - iv, 4);

			for (int ii = 0; ii < patch_w; ++ii) {
				for (int jj = 0; jj < patch_h; ++jj) {
					float u_spline = u_weights[ii];
					float v_spline = v_weights[jj];
					float f = u_spline * v_spline;

					if (f > 0.0f) {
#ifdef _M_SSE
						Vec4f fv(_mm_set_ps1(f));
#else
						Vec4f fv = Vec4f::AssignToAll(f);
#endif
						int idx = spatch.count_u * (iv + jj) + (iu + ii);
						/*
						if (idx >= max_idx) {
							char temp[512];
							snprintf(temp, sizeof(temp), "count_u: %d count_v: %d patch_w: %d patch_h: %d  ii: %d  jj: %d  iu: %d  iv: %d  patch_div_s: %d  patch_div_t: %d\n", spatch.count_u, spatch.count_v, patch_w, patch_h, ii, jj, iu, iv, patch_div_s, patch_div_t);
							OutputDebugStringA(temp);
							DebugBreak();
						}*/
						SimpleVertex *a = spatch.points[idx];
						AccumulateWeighted(vert_pos, a->pos, fv);
						if (origTc) {
							vert->uv[0] += a->uv[0] * f;
							vert->uv[1] += a->uv[1] * f;
						}
						if (origCol) {
							Vec4f a_color = Vec4f::FromRGBA(a->color_32);
							AccumulateWeighted(vert_color, a_color, fv);
						}
						if (origNrm) {
							AccumulateWeighted(vert_nrm, a->nrm, fv);
						}
					}
				}
			}
			vert->pos = vert_pos;
			if (origNrm) {
#ifdef _M_SSE
				const __m128 normalize = SSENormalizeMultiplier(useSSE4, vert_nrm.vec);
				vert_nrm.vec = _mm_mul_ps(vert_nrm.vec, normalize);
#else
				vert_nrm.Normalize();
#endif
				vert->nrm = vert_nrm;
			} else {
				vert->nrm.SetZero();
				vert->nrm.z = 1.0f;
			}
			if (origCol) {
				vert->color_32 = vert_color.ToRGBA();
			}
		}
	}

	delete[] knot_u;
	delete[] knot_v;

	// Hacky normal generation through central difference.
	if (gstate.isLightingEnabled() && !origNrm) {
#ifdef _M_SSE
		const __m128 facing = (gstate.patchfacing & 1) != 0 ? _mm_set_ps1(-1.0f) : _mm_set_ps1(1.0f);
#endif

		for (int v = 0; v < patch_div_t + 1; v++) {
			Vec3f vl_pos = vertices[v * (patch_div_s + 1)].pos;
			Vec3f vc_pos = vertices[v * (patch_div_s + 1)].pos;

			for (int u = 0; u < patch_div_s + 1; u++) {
				const int l = std::max(0, u - 1);
				const int t = std::max(0, v - 1);
				const int r = std::min(patch_div_s, u + 1);
				const int b = std::min(patch_div_t, v + 1);

				const Vec3f vr_pos = vertices[v * (patch_div_s + 1) + r].pos;

#ifdef _M_SSE
				const __m128 right = _mm_sub_ps(vr_pos.vec, vl_pos.vec);

				const Vec3f vb_pos = vertices[b * (patch_div_s + 1) + u].pos;
				const Vec3f vt_pos = vertices[t * (patch_div_s + 1) + u].pos;
				const __m128 down = _mm_sub_ps(vb_pos.vec, vt_pos.vec);

				const __m128 crossed = SSECrossProduct(right, down);
				const __m128 normalize = SSENormalizeMultiplier(useSSE4, crossed);

				Vec3f finalNrm = _mm_mul_ps(normalize, _mm_mul_ps(crossed, facing));
				vertices[v * (patch_div_s + 1) + u].nrm = finalNrm;
#else
				const Vec3Packedf &right = vr_pos - vl_pos;
				const Vec3Packedf &down = vertices[b * (patch_div_s + 1) + u].pos - vertices[t * (patch_div_s + 1) + u].pos;

				vertices[v * (patch_div_s + 1) + u].nrm = Cross(right, down).Normalized();
				if (gstate.patchfacing & 1) {
					vertices[v * (patch_div_s + 1) + u].nrm *= -1.0f;
				}
#endif

				// Rotate for the next one to the right.
				vl_pos = vc_pos;
				vc_pos = vr_pos;
			}
		}
	}

	GEPatchPrimType prim_type = gstate.getPatchPrimitiveType();
	// Tesselate.
	for (int tile_v = 0; tile_v < patch_div_t; ++tile_v) {
		for (int tile_u = 0; tile_u < patch_div_s; ++tile_u) {
			int idx0 = tile_v * (patch_div_s + 1) + tile_u;
			int idx1 = tile_v * (patch_div_s + 1) + tile_u + 1;
			int idx2 = (tile_v + 1) * (patch_div_s + 1) + tile_u;
			int idx3 = (tile_v + 1) * (patch_div_s + 1) + tile_u + 1;

			CopyQuadIndex(indices, prim_type, idx0, idx1, idx2, idx3);
			count += 6;
		}
	}
}

template <bool origNrm, bool origCol, bool origTc>
static inline void SplinePatchFullQualityDispatch4(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int quality, int maxVertices) {
	if (cpu_info.bSSE4_1)
		SplinePatchFullQuality<origNrm, origCol, origTc, true>(dest, indices, count, spatch, origVertType, quality, maxVertices);
	else
		SplinePatchFullQuality<origNrm, origCol, origTc, false>(dest, indices, count, spatch, origVertType, quality, maxVertices);
}

template <bool origNrm, bool origCol>
static inline void SplinePatchFullQualityDispatch3(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int quality, int maxVertices) {
	bool origTc = (origVertType & GE_VTYPE_TC_MASK) != 0;

	if (origTc)
		SplinePatchFullQualityDispatch4<origNrm, origCol, true>(dest, indices, count, spatch, origVertType, quality, maxVertices);
	else
		SplinePatchFullQualityDispatch4<origNrm, origCol, false>(dest, indices, count, spatch, origVertType, quality, maxVertices);
}

template <bool origNrm>
static inline void SplinePatchFullQualityDispatch2(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int quality, int maxVertices) {
	bool origCol = (origVertType & GE_VTYPE_COL_MASK) != 0;

	if (origCol)
		SplinePatchFullQualityDispatch3<origNrm, true>(dest, indices, count, spatch, origVertType, quality, maxVertices);
	else
		SplinePatchFullQualityDispatch3<origNrm, false>(dest, indices, count, spatch, origVertType, quality, maxVertices);
}

static void SplinePatchFullQualityDispatch(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int quality, int maxVertices) {
	bool origNrm = (origVertType & GE_VTYPE_NRM_MASK) != 0;

	if (origNrm)
		SplinePatchFullQualityDispatch2<true>(dest, indices, count, spatch, origVertType, quality, maxVertices);
	else
		SplinePatchFullQualityDispatch2<false>(dest, indices, count, spatch, origVertType, quality, maxVertices);
}

void TesselateSplinePatch(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int maxVertexCount) {
	switch (g_Config.iSplineBezierQuality) {
	case LOW_QUALITY:
		_SplinePatchLowQuality(dest, indices, count, spatch, origVertType);
		break;
	case MEDIUM_QUALITY:
		SplinePatchFullQualityDispatch(dest, indices, count, spatch, origVertType, 2, maxVertexCount);
		break;
	case HIGH_QUALITY:
		SplinePatchFullQualityDispatch(dest, indices, count, spatch, origVertType, 1, maxVertexCount);
		break;
	}
}

static void _BezierPatchLowQuality(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType) {
	const float third = 1.0f / 3.0f;
	// Fast and easy way - just draw the control points, generate some very basic normal vector subsitutes.
	// Very inaccurate though but okay for Loco Roco. Maybe should keep it as an option.

	float u_base = patch.u_index / 3.0f;
	float v_base = patch.v_index / 3.0f;

	GEPatchPrimType prim_type = gstate.getPatchPrimitiveType();

	for (int tile_v = 0; tile_v < 3; tile_v++) {
		for (int tile_u = 0; tile_u < 3; tile_u++) {
			int point_index = tile_u + tile_v * 4;

			SimpleVertex v0 = *patch.points[point_index];
			SimpleVertex v1 = *patch.points[point_index + 1];
			SimpleVertex v2 = *patch.points[point_index + 4];
			SimpleVertex v3 = *patch.points[point_index + 5];

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
				Vec3Packedf norm = Cross(v1.pos - v0.pos, v2.pos - v0.pos);
				norm.Normalize();
				if (gstate.patchfacing & 1)
					norm *= -1.0f;
				v0.nrm = norm;
				v1.nrm = norm;
				v2.nrm = norm;
				v3.nrm = norm;
			}


			int total = patch.index * 3 * 3 * 4; // A patch has 3x3 tiles, and each tiles have 4 vertices.
			int tile_index = tile_u + tile_v * 3;
			int idx0 = total + tile_index * 4 + 0;
			int idx1 = total + tile_index * 4 + 1;
			int idx2 = total + tile_index * 4 + 2;
			int idx3 = total + tile_index * 4 + 3;

			CopyQuad(dest, &v0, &v1, &v2, &v3);
			CopyQuadIndex(indices, prim_type, idx0, idx1, idx2, idx3);
			count += 6;
		}
	}
}

static void _BezierPatchHighQuality(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType, int maxVertices) {
	const float third = 1.0f / 3.0f;

	// Downsample until it fits, in case crazy tesselation factors are sent.
	while ((tess_u + 1) * (tess_v + 1) > maxVertices) {
		tess_u /= 2;
		tess_v /= 2;
	}

	// First compute all the vertices and put them in an array
	SimpleVertex *&vertices = (SimpleVertex*&)dest;

	Vec3Packedf *horiz = new Vec3Packedf[(tess_u + 1) * 4];
	Vec3Packedf *horiz2 = horiz + (tess_u + 1) * 1;
	Vec3Packedf *horiz3 = horiz + (tess_u + 1) * 2;
	Vec3Packedf *horiz4 = horiz + (tess_u + 1) * 3;

	Vec3Packedf *derivU1 = new Vec3Packedf[(tess_u + 1) * 4];
	Vec3Packedf *derivU2 = derivU1 + (tess_u + 1) * 1;
	Vec3Packedf *derivU3 = derivU1 + (tess_u + 1) * 2;
	Vec3Packedf *derivU4 = derivU1 + (tess_u + 1) * 3;

	bool computeNormals = gstate.isLightingEnabled();

	// Precompute the horizontal curves to we only have to evaluate the vertical ones.
	for (int i = 0; i < tess_u + 1; i++) {
		float u = ((float)i / (float)tess_u);
		horiz[i] = Bernstein3D(patch.points[0]->pos, patch.points[1]->pos, patch.points[2]->pos, patch.points[3]->pos, u);
		horiz2[i] = Bernstein3D(patch.points[4]->pos, patch.points[5]->pos, patch.points[6]->pos, patch.points[7]->pos, u);
		horiz3[i] = Bernstein3D(patch.points[8]->pos, patch.points[9]->pos, patch.points[10]->pos, patch.points[11]->pos, u);
		horiz4[i] = Bernstein3D(patch.points[12]->pos, patch.points[13]->pos, patch.points[14]->pos, patch.points[15]->pos, u);

		if (computeNormals) {
			derivU1[i] = Bernstein3DDerivative(patch.points[0]->pos, patch.points[1]->pos, patch.points[2]->pos, patch.points[3]->pos, u);
			derivU2[i] = Bernstein3DDerivative(patch.points[4]->pos, patch.points[5]->pos, patch.points[6]->pos, patch.points[7]->pos, u);
			derivU3[i] = Bernstein3DDerivative(patch.points[8]->pos, patch.points[9]->pos, patch.points[10]->pos, patch.points[11]->pos, u);
			derivU4[i] = Bernstein3DDerivative(patch.points[12]->pos, patch.points[13]->pos, patch.points[14]->pos, patch.points[15]->pos, u);
		}
	}


	for (int tile_v = 0; tile_v < tess_v + 1; ++tile_v) {
		for (int tile_u = 0; tile_u < tess_u + 1; ++tile_u) {
			float u = ((float)tile_u / (float)tess_u);
			float v = ((float)tile_v / (float)tess_v);
			float bu = u;
			float bv = v;

			// TODO: Should be able to precompute the four curves per U, then just Bernstein per V. Will benefit large tesselation factors.
			const Vec3Packedf &pos1 = horiz[tile_u];
			const Vec3Packedf &pos2 = horiz2[tile_u];
			const Vec3Packedf &pos3 = horiz3[tile_u];
			const Vec3Packedf &pos4 = horiz4[tile_u];

			SimpleVertex &vert = vertices[tile_v * (tess_u + 1) + tile_u];

			if (computeNormals) {
				const Vec3Packedf &derivU1_ = derivU1[tile_u];
				const Vec3Packedf &derivU2_ = derivU2[tile_u];
				const Vec3Packedf &derivU3_ = derivU3[tile_u];
				const Vec3Packedf &derivU4_ = derivU4[tile_u];

				Vec3Packedf derivU = Bernstein3D(derivU1_, derivU2_, derivU3_, derivU4_, bv);
				Vec3Packedf derivV = Bernstein3DDerivative(pos1, pos2, pos3, pos4, bv);

				vert.nrm = Cross(derivU, derivV).Normalized();
				if (gstate.patchfacing & 1)
					vert.nrm *= -1.0f;
			}
			else {
				vert.nrm.SetZero();
			}

			vert.pos = Bernstein3D(pos1, pos2, pos3, pos4, bv);

			if ((origVertType & GE_VTYPE_TC_MASK) == 0) {
				// Generate texcoord
				vert.uv[0] = u + patch.u_index * third;
				vert.uv[1] = v + patch.v_index * third;
			} else {
				// Sample UV from control points
				patch.sampleTexUV(u, v, vert.uv[0], vert.uv[1]);
			} 

			if (origVertType & GE_VTYPE_COL_MASK) {
				patch.sampleColor(u, v, vert.color);
			} else {
				memcpy(vert.color, patch.points[0]->color, 4);
			}
		}
	}
	delete[] derivU1;
	delete[] horiz;

	GEPatchPrimType prim_type = gstate.getPatchPrimitiveType();
	// Combine the vertices into triangles.
	for (int tile_v = 0; tile_v < tess_v; ++tile_v) {
		for (int tile_u = 0; tile_u < tess_u; ++tile_u) {
			int total = patch.index * (tess_u + 1) * (tess_v + 1);
			int idx0 = total + tile_v * (tess_u + 1) + tile_u;
			int idx1 = total + tile_v * (tess_u + 1) + tile_u + 1;
			int idx2 = total + (tile_v + 1) * (tess_u + 1) + tile_u;
			int idx3 = total + (tile_v + 1) * (tess_u + 1) + tile_u + 1;

			CopyQuadIndex(indices, prim_type, idx0, idx1, idx2, idx3);
			count += 6;
		}
	}
	dest += (tess_u + 1) * (tess_v + 1) * sizeof(SimpleVertex);
}

void TesselateBezierPatch(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType, int maxVertices) {
	switch (g_Config.iSplineBezierQuality) {
	case LOW_QUALITY:
		_BezierPatchLowQuality(dest, indices, count, tess_u, tess_v, patch, origVertType);
		break;
	case MEDIUM_QUALITY:
		_BezierPatchHighQuality(dest, indices, count, tess_u / 2, tess_v / 2, patch, origVertType, maxVertices);
		break;
	case HIGH_QUALITY:
		_BezierPatchHighQuality(dest, indices, count, tess_u, tess_v, patch, origVertType, maxVertices);
		break;
	}
}


u32 DrawEngineCommon::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	VertexDecoder *dec = GetVertexDecoder(vertTypeID);
	return DrawEngineCommon::NormalizeVertices(outPtr, bufPtr, inPtr, dec, lowerBound, upperBound, vertType);
}

const GEPrimitiveType primType[] = { GE_PRIM_TRIANGLES, GE_PRIM_LINES, GE_PRIM_POINTS };

void DrawEngineCommon::SubmitSpline(const void *control_points, const void *indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertType) {
	PROFILE_THIS_SCOPE("spline");
	DispatchFlush();

	// TODO: Verify correct functionality with < 4.
	if (count_u < 4 || count_v < 4)
		return;

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 18;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

	// TODO: Do something less idiotic to manage this buffer
	SimpleVertex **points = new SimpleVertex *[count_u * count_v];

	// Make an array of pointers to the control points, to get rid of indices.
	for (int idx = 0; idx < count_u * count_v; idx++) {
		if (indices)
			points[idx] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
		else
			points[idx] = simplified_control_points + idx;
	}

	int count = 0;

	u8 *dest = splineBuffer;

	SplinePatchLocal patch;
	patch.type_u = type_u;
	patch.type_v = type_v;
	patch.count_u = count_u;
	patch.count_v = count_v;
	patch.points = points;

	int maxVertexCount = SPLINE_BUFFER_SIZE / vertexSize;
	TesselateSplinePatch(dest, quadIndices_, count, patch, origVertType, maxVertexCount);

	delete[] points;

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if (g_Config.bPrescaleUV) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0;
		gstate_c.uv.vOff = 0;
	}

	int bytesRead;
	DispatchSubmitPrim(splineBuffer, quadIndices_, primType[prim_type], count, vertTypeWithIndex16, &bytesRead);

	DispatchFlush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}

void DrawEngineCommon::SubmitBezier(const void *control_points, const void *indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertType) {
	PROFILE_THIS_SCOPE("bezier");

	DispatchFlush();

	// TODO: Verify correct functionality with < 4.
	if (count_u < 4 || count_v < 4)
		return;

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	// There are normally not a lot of control points so just splitting decoded should be reasonably safe, although not great.
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 18;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, (int)sizeof(SimpleVertex));
	}

	// Bezier patches share less control points than spline patches. Otherwise they are pretty much the same (except bezier don't support the open/close thing)
	int num_patches_u = (count_u - 1) / 3;
	int num_patches_v = (count_v - 1) / 3;
	BezierPatch* patches = new BezierPatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; patch_u++) {
		for (int patch_v = 0; patch_v < num_patches_v; patch_v++) {
			BezierPatch& patch = patches[patch_u + patch_v * num_patches_u];
			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u * 3 + point % 4) + (patch_v * 3 + point / 4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.u_index = patch_u * 3;
			patch.v_index = patch_v * 3;
			patch.index = patch_v * num_patches_u + patch_u;
		}
	}

	int count = 0;
	u8 *dest = splineBuffer;

	// Simple approximation of the real tesselation factor.
	// We shouldn't really split up into separate 4x4 patches, instead we should do something that works
	// like the splines, so we subdivide across the whole "mega-patch".
	if (num_patches_u == 0) num_patches_u = 1;
	if (num_patches_v == 0) num_patches_v = 1;
	int tess_u = gstate.getPatchDivisionU();
	int tess_v = gstate.getPatchDivisionV();
	if (tess_u < 4) tess_u = 4;
	if (tess_v < 4) tess_v = 4;

	u16 *inds = quadIndices_;
	int maxVertices = SPLINE_BUFFER_SIZE / vertexSize;
	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		BezierPatch& patch = patches[patch_idx];
		TesselateBezierPatch(dest, inds, count, tess_u, tess_v, patch, origVertType, maxVertices);
	}
	delete[] patches;

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	UVScale prevUVScale;
	if (g_Config.bPrescaleUV) {
		// We scaled during Normalize already so let's turn it off when drawing.
		prevUVScale = gstate_c.uv;
		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
		gstate_c.uv.uOff = 0;
		gstate_c.uv.vOff = 0;
	}

	int bytesRead;
	DispatchSubmitPrim(splineBuffer, quadIndices_, primType[prim_type], count, vertTypeWithIndex16, &bytesRead);

	DispatchFlush();

	if (g_Config.bPrescaleUV) {
		gstate_c.uv = prevUVScale;
	}
}
