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
#include <cstring>
#include <type_traits>
#include <vector>

#include "Common/Common.h"
#include <algorithm>
#include <cstddef>

#include "Common/Math/CrossSIMD.h"
#include "Common/Profiler/Profiler.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"  // only needed for UVScale stuff
#include "GPU/GPUStateSIMDUtil.h"
#include "GPU/Software/GEMath.h"

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
		// A zigzag per quad: left edge down, the diagonal up to the top right, right edge down (gpu/probe exp175,
		// exact: line colors and lit start pixels show the directions, and overlapping pixels the order).
		*(indices++) = idx0;
		*(indices++) = idx2;
		*(indices++) = idx2;
		*(indices++) = idx1;
		*(indices++) = idx1;
		*(indices++) = idx3;
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

// Bezier patches as the GE evaluates them (gpu/probe exp48-51, exp139-140, bit exact). The parameter is k/256,
// rounded toward the middle of the patch, and columns (v) are evaluated before rows (u), by de Casteljau.
// Positions and texture coordinates: each lerp in 16-bit fixed point at the larger operand's exponent, both
// operands truncated toward zero to that grid, then a + floor((b - a) k / 256). At k = 0 or 256 a lerp passes
// its operand through unchanged. Colors: the same lerp on (c << 7) | 0x7F per channel, result >> 7.
// Normals (exp142-143): the cross product (as the matrix unit sums it) of two tangents, each the difference of
// the de Casteljau's last two points: along u, the row's; along v, those of the columns evaluated along u.
static int BezierParam256(int i, int n) {
	return 2 * i <= n ? (256 * i) / n : 256 - (256 * (n - i)) / n;
}

static float GEBezierLerp(float a, float b, int k) {
	if (k == 0) {
		return a;
	} else if (k == 256) {
		return b;
	}
	uint32_t ba, bb;
	memcpy(&ba, &a, 4);
	memcpy(&bb, &b, 4);
	const int ea = (ba >> 23) & 0xFF;
	const int eb = (bb >> 23) & 0xFF;
	const int e = std::max(ea, eb);
	if (e == 0) {
		return 0.0f;
	}
	// Mantissas (24 bits) shifted to 16 bits at exponent e, truncated toward zero.
	auto toFixed = [e](uint32_t bits, int ex) -> int {
		if (ex == 0 || e - ex >= 16) {
			return 0;
		}
		const int m = (int)(((bits & 0x7FFFFF) | 0x800000) >> (e - ex + 8));
		return (bits & 0x80000000) ? -m : m;
	};
	const int fa = toFixed(ba, ea);
	const int fb = toFixed(bb, eb);
	const int r = fa + (((fb - fa) * k) >> 8);
	// r * 2^(e - 127 - 15), exact.
	if (e <= 15) {
		return ldexpf((float)r, e - 127 - 15);
	}
	const uint32_t scaleBits = (uint32_t)(e - 15) << 23;
	float scale;
	memcpy(&scale, &scaleBits, 4);
	return (float)r * scale;
}

// Also returns the de Casteljau's last two points, which the normal's tangents come from.
static float GEBezierEval(const float p[4], int k, float *ab = nullptr, float *bc = nullptr) {
	const float a = GEBezierLerp(p[0], p[1], k);
	const float b = GEBezierLerp(p[1], p[2], k);
	const float c = GEBezierLerp(p[2], p[3], k);
	const float l = GEBezierLerp(a, b, k);
	const float r = GEBezierLerp(b, c, k);
	if (ab) {
		*ab = l;
		*bc = r;
	}
	return GEBezierLerp(l, r, k);
}

static int GEBezierEvalColor(const int p[4], int k) {
	auto lerp = [k](int a, int b) { return a + (((b - a) * k) >> 8); };
	const int a = lerp(p[0], p[1]);
	const int b = lerp(p[1], p[2]);
	const int c = lerp(p[2], p[3]);
	return lerp(lerp(a, b), lerp(b, c));
}

// Splines as the GE evaluates them (gpu/probe exp144-145, bit exact): de Boor at t = k/256 (k as for Bezier)
// with the lerps above. Each blending factor (t - K[i]) / span comes from the nearer of its two knots, in 1/256:
// floor(d / span) from the left one, 256 - floor(d / span) from the right. A segment's last point is the next
// one's first (k = 0). An open end repeats its knot four times, a closed one keeps the unit spacing.
struct GESplineParam {
	int seg;
	int alpha[6];  // level 1 for knots l-2, l-1, l, level 2 for l-1, l, level 3 for l (l = seg + 3)
	int k;
};

static GESplineParam GESplineParamAt(int index, int tess, int numPatches, int type) {
	GESplineParam p;
	p.seg = index / tess;
	p.k = BezierParam256(index % tess, tess);
	if (p.seg == numPatches) {
		p.seg = numPatches - 1;
		p.k = 256;
	}
	auto knot = [&](int i) {
		if (i < 3 && (type & 1))
			return 0;
		if (i > numPatches + 3 && (type & 2))
			return numPatches;
		return i - 3;
	};
	const int l = p.seg + 3;
	const int is[6] = { l - 2, l - 1, l, l - 1, l, l };
	const int rs[6] = { 1, 1, 1, 2, 2, 3 };
	for (int n = 0; n < 6; ++n) {
		const int i = is[n];
		const int span = knot(i + 4 - rs[n]) - knot(i);
		const int dl = 256 * (p.seg - knot(i)) + p.k;
		const int dr = 256 * span - dl;
		p.alpha[n] = span == 0 ? 0 : (dl <= dr ? dl / span : 256 - dr / span);
	}
	return p;
}

static float GESplineEval(const float d[4], const int a[6], float *ab = nullptr, float *bc = nullptr) {
	const float l0 = GEBezierLerp(d[0], d[1], a[0]);
	const float l1 = GEBezierLerp(d[1], d[2], a[1]);
	const float l2 = GEBezierLerp(d[2], d[3], a[2]);
	const float m0 = GEBezierLerp(l0, l1, a[3]);
	const float m1 = GEBezierLerp(l1, l2, a[4]);
	if (ab) {
		*ab = m0;
		*bc = m1;
	}
	return GEBezierLerp(m0, m1, a[5]);
}

static int GESplineEvalColor(const int d[4], const int a[6]) {
	auto lerp = [](int x, int y, int k) { return x + (((y - x) * k) >> 8); };
	const int l0 = lerp(d[0], d[1], a[0]);
	const int l1 = lerp(d[1], d[2], a[1]);
	const int l2 = lerp(d[2], d[3], a[2]);
	return lerp(lerp(l0, l1, a[3]), lerp(l1, l2, a[4]), a[5]);
}

// One control column of a patch evaluated at some v.
struct GEBezierColumn {
	float pos[3];
	float ab[3], bc[3];  // for the normal
	float tex[2];
	int col[4];  // 15 bits
};

struct Weight {
	float basis[4];
	float deriv[4];
};

static WeightTable AllocateWeightTable(int count) {
	WeightTable table;
	table.stride = count + WeightTable::WEIGHT_PADDING;
	table.data = new float[8 * table.stride]();
	return table;
}

static void SetWeight(WeightTable &table, int index, const Weight &w) {
	for (int k = 0; k < 4; k++) {
		table.data[k * table.stride + index] = w.basis[k];
		table.data[(4 + k) * table.stride + index] = w.deriv[k];
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
	static WeightTable CalcWeightsAll(u32 key) {
		int tess = (int)key;
		WeightTable weights = AllocateWeightTable(tess + 1);
		const float inv_tess = 1.0f / (float)tess;
		for (int i = 0; i < tess + 1; ++i) {
			const float t = (float)i * inv_tess;
			Weight w;
			CalcWeights(t, w);
			SetWeight(weights, i, w);
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
			// With a single patch whose first edge is open too, that knot interval is 1, not 2.
			divs[n - 1]._4_1 = (n == 1 && (type & 1) != 0) ? 1.0f : 1.0f / 2.0f;
			divs[n - 1]._5_2 = 1.0f;
			divs[n - 1]._4_2 = 1.0f;
			if (n > 1)
				divs[n - 2]._5_2 = 1.0f / 2.0f;
		}
	}

	static void CalcWeights(float t, const float *knots, const KnotDiv &div, Weight &w) {
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
	WeightTable CalcWeightsAll(u32 key) {
		int tess, count, type;
		FromKey(key, tess, count, type);
		const int num_patches = count - 3;
		WeightTable weights = AllocateWeightTable(tess * num_patches + 1);

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
				Weight w;
				CalcWeights(t, knots + i, divs[i], w);
				SetWeight(weights, index, w);
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

ControlPoints::ControlPoints(const SimpleVertex *const *src, int size, SimpleBufferManager &managedBuf) {
	points = (ControlPoint *)managedBuf.Allocate(sizeof(ControlPoint) * size);
	if (points)
		Convert(src, size);
}

void ControlPoints::Convert(const SimpleVertex *const *src, int size) {
	for (int i = 0; i < size; ++i) {
		const SimpleVertex &v = *src[i];
		ControlPoint &p = points[i];
		p.pos[0] = v.pos.x;
		p.pos[1] = v.pos.y;
		p.pos[2] = v.pos.z;
		p.pos[3] = 0.0f;
		for (int c = 0; c < 4; c++) {
			p.col[c] = (float)v.color[c];
		}
		p.uv[0] = v.uv[0];
		p.uv[1] = v.uv[1];
		p.uv[2] = 0.0f;
		p.uv[3] = 0.0f;
	}
	defcolor = src[0]->color_32;
}

// Four vertices' worth of a 3-component attribute, one component per register.
struct Vec3x4 {
	Vec4F32 x, y, z;
};

static inline Vec3x4 Cross(const Vec3x4 &a, const Vec3x4 &b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static inline Vec4F32 Dot(const Vec3x4 &a, const Vec3x4 &b) {
	return MulAdd(a.x, b.x, MulAdd(a.y, b.y, a.z * b.z));
}

static inline Vec3x4 Select(Vec4S32 mask, const Vec3x4 &ifTrue, const Vec3x4 &ifFalse) {
	return { Select(mask, ifTrue.x, ifFalse.x), Select(mask, ifTrue.y, ifFalse.y), Select(mask, ifTrue.z, ifFalse.z) };
}

static inline Vec3x4 operator *(const Vec3x4 &a, Vec4F32 f) {
	return { a.x * f, a.y * f, a.z * f };
}

// Component c of the four columns, weighted along u.
static inline Vec4F32 SampleU(const float cols[4][4], int c, const Vec4F32 w[4]) {
	Vec4F32 sum = Vec4F32::Splat(cols[0][c]) * w[0];
	sum = MulAdd(Vec4F32::Splat(cols[1][c]), w[1], sum);
	sum = MulAdd(Vec4F32::Splat(cols[2][c]), w[2], sum);
	return MulAdd(Vec4F32::Splat(cols[3][c]), w[3], sum);
}

static inline Vec3x4 SampleU3(const float cols[4][4], const Vec4F32 w[4]) {
	return { SampleU(cols, 0, w), SampleU(cols, 1, w), SampleU(cols, 2, w) };
}

// Folds the four rows of control points into the four columns of one row of vertices.
static inline void SampleV(const ControlPoint *const rows[4], size_t offset, const float w[4], float cols[4][4]) {
	const Vec4F32 w0 = Vec4F32::Splat(w[0]), w1 = Vec4F32::Splat(w[1]), w2 = Vec4F32::Splat(w[2]), w3 = Vec4F32::Splat(w[3]);
	for (int k = 0; k < 4; k++) {
		const float *r0 = (const float *)((const u8 *)&rows[0][k] + offset);
		const float *r1 = (const float *)((const u8 *)&rows[1][k] + offset);
		const float *r2 = (const float *)((const u8 *)&rows[2][k] + offset);
		const float *r3 = (const float *)((const u8 *)&rows[3][k] + offset);
		Vec4F32 sum = Vec4F32::Load(r0) * w0;
		sum = MulAdd(Vec4F32::Load(r1), w1, sum);
		sum = MulAdd(Vec4F32::Load(r2), w2, sum);
		sum = MulAdd(Vec4F32::Load(r3), w3, sum);
		sum.Store(cols[k]);
	}
}

// The GE's spline: each control column at the vertex's v, then the row of those at its u (as for Bezier).
template <bool sampleNrm, bool sampleCol, bool sampleTex, bool patchFacing>
static void TessellateSplineGE(OutputBuffers &output, const SplineSurface &surface, const ControlPoints &points) {
	const int nu = surface.num_patches_u * surface.tess_u + 1;
	const int nv = surface.num_patches_v * surface.tess_v + 1;
	const int pointsU = surface.num_points_u;
	std::vector<GESplineParam> paramsU(nu);
	for (int i = 0; i < nu; ++i)
		paramsU[i] = GESplineParamAt(i, surface.tess_u, surface.num_patches_u, surface.type_u);
	std::vector<GEBezierColumn> columns(pointsU);

	for (int iv = 0; iv < nv; ++iv) {
		const GESplineParam pv = GESplineParamAt(iv, surface.tess_v, surface.num_patches_v, surface.type_v);
		for (int c = 0; c < pointsU; ++c) {
			GEBezierColumn &column = columns[c];
			int idx[4];
			for (int r = 0; r < 4; ++r)
				idx[r] = (pv.seg + r) * pointsU + c;
			for (int j = 0; j < 3; ++j) {
				const float p[4] = { points.points[idx[0]].pos[j], points.points[idx[1]].pos[j], points.points[idx[2]].pos[j], points.points[idx[3]].pos[j] };
				column.pos[j] = GESplineEval(p, pv.alpha, &column.ab[j], &column.bc[j]);
			}
			if constexpr (sampleTex) {
				for (int j = 0; j < 2; ++j) {
					const float p[4] = { points.points[idx[0]].uv[j], points.points[idx[1]].uv[j], points.points[idx[2]].uv[j], points.points[idx[3]].uv[j] };
					column.tex[j] = GESplineEval(p, pv.alpha);
				}
			}
			if constexpr (sampleCol) {
				for (int j = 0; j < 4; ++j) {
					int p[4];
					for (int r = 0; r < 4; ++r)
						p[r] = ((int)points.points[idx[r]].col[j] << 7) | 0x7F;
					column.col[j] = GESplineEvalColor(p, pv.alpha);
				}
			}
		}

		for (int iu = 0; iu < nu; ++iu) {
			const GESplineParam &pu = paramsU[iu];
			const GEBezierColumn *cols = &columns[pu.seg];
			SimpleVertex &vert = output.vertices[surface.GetIndex(iu, iv, 0, 0)];
			Vec3f tangentU, tangentV;
			for (int j = 0; j < 3; ++j) {
				const float row[4] = { cols[0].pos[j], cols[1].pos[j], cols[2].pos[j], cols[3].pos[j] };
				float ab, bc;
				vert.pos[j] = GESplineEval(row, pu.alpha, &ab, &bc);
				if constexpr (sampleNrm) {
					tangentU[j] = GEAdd(bc, -ab);
					const float abRow[4] = { cols[0].ab[j], cols[1].ab[j], cols[2].ab[j], cols[3].ab[j] };
					const float bcRow[4] = { cols[0].bc[j], cols[1].bc[j], cols[2].bc[j], cols[3].bc[j] };
					tangentV[j] = GEAdd(GESplineEval(bcRow, pu.alpha), -GESplineEval(abRow, pu.alpha));
				}
			}
			if constexpr (sampleCol) {
				u32 color = 0;
				for (int j = 0; j < 4; ++j) {
					const int row[4] = { cols[0].col[j], cols[1].col[j], cols[2].col[j], cols[3].col[j] };
					color |= (u32)(GESplineEvalColor(row, pu.alpha) >> 7) << (8 * j);
				}
				vert.color_32 = color;
			} else {
				vert.color_32 = points.defcolor;
			}
			if constexpr (sampleTex) {
				for (int j = 0; j < 2; ++j) {
					const float row[4] = { cols[0].tex[j], cols[1].tex[j], cols[2].tex[j], cols[3].tex[j] };
					vert.uv[j] = GESplineEval(row, pu.alpha);
				}
			} else {
				vert.uv[0] = pu.seg + pu.k * (1.0f / 256.0f);
				vert.uv[1] = pv.seg + pv.k * (1.0f / 256.0f);
			}
			if constexpr (sampleNrm) {
				for (int j = 0; j < 3; ++j) {
					const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
					const GERowTerm terms[2] = { GEProduct(tangentU[j1], tangentV[j2]), GEProduct(-tangentU[j2], tangentV[j1]) };
					vert.nrm[j] = GERowSum(terms, 2);
				}
				if constexpr (patchFacing)
					vert.nrm *= -1.0f;
			} else {
				vert.nrm.SetZero();
				vert.nrm.z = 1.0f;
			}
		}
	}
}

// Four of the GE's lerps at once (GEBezierLerp). Both operands go to 16-bit fixed point at the larger
// exponent by scaling with a power of two and truncating, which truncates toward zero like the GE, then
// the integer lerp, then back by the inverse power of two (exact). (d * k) >> 8 is split into the high and
// low bytes of d, so both multiplies fit 16 bits. Lanes whose larger exponent field is below 16 (values
// under 2^-111, or zero) take the scalar lerp.
// The lanes GELerp4 can't do: the larger exponent field below 16.
static NO_INLINE Vec4F32 GELerp4Small(Vec4F32 a, Vec4F32 b, Vec4S32 k, Vec4S32 small, Vec4F32 r) {
	alignas(16) float av[4], bv[4], rv[4];
	alignas(16) int kv[4], sv[4];
	a.Store(av);
	b.Store(bv);
	r.Store(rv);
	k.Store(kv);
	small.Store(sv);
	for (int i = 0; i < 4; ++i) {
		if (sv[i])
			rv[i] = GEBezierLerp(av[i], bv[i], kv[i]);
	}
	return Vec4F32::Load(rv);
}

// The operands mustn't be denormal (FlushDenormals4 on inputs; a lerp never makes one).
static __forceinline Vec4F32 GELerp4(Vec4F32 a, Vec4F32 b, Vec4S32 k, Vec4S32 k0, Vec4S32 k256) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 absMask = Vec4S32::Splat(0x7FFFFFFF);
	const Vec4S32 ba = Vec4S32FromBits(a);
	const Vec4S32 bb = Vec4S32FromBits(b);
	const Vec4F32 za = a;
	const Vec4F32 zb = b;
	const Vec4S32 e = Vec4S32FromBits(Vec4F32FromBits(ba & absMask).Max(Vec4F32FromBits(bb & absMask))) & expMask;
	// 2^(142 - e) and 2^(e - 142), with e the biased exponent.
	const Vec4F32 down = Vec4F32FromBits(Vec4S32::Splat((int)(269u << 23)) - e);
	const Vec4F32 up = Vec4F32FromBits(e - Vec4S32::Splat(15 << 23));
	const Vec4S32 fa = Vec4S32FromF32(za * down);
	const Vec4S32 fb = Vec4S32FromF32(zb * down);
	const Vec4S32 diff = fb - fa;
	const Vec4S32 q = diff.Shr<8>().Mul16(k) + (diff & Vec4S32::Splat(255)).Mul16(k).Shr<8>();
	const Vec4F32 r = Select(k0, a, Select(k256, b, Vec4F32FromS32(fa + q) * up));
	const Vec4S32 small = e.CompareLt(Vec4S32::Splat(16 << 23));
	if (AnyCompareBitsSet(small))
		return GELerp4Small(a, b, k, small, r);
	return r;
}

// Denormals, which the GE treats as zero, become zero.
static inline Vec4F32 FlushDenormals4(Vec4F32 v) {
	return Select((Vec4S32FromBits(v) & Vec4S32::Splat(0x7F800000)).CompareEq(Vec4S32::Zero()), Vec4F32::Zero(), v);
}

// A lerp between two fixed operands at four ks, with the operands' conversion done once (GELerp4).
struct GELerpFixed {
	Vec4S32 fa, dh, dl;
	Vec4F32 a, b, up;
	bool slow;
	float sa, sb;

	void Init(float x, float y) {
		uint32_t bx, by;
		memcpy(&bx, &x, 4);
		memcpy(&by, &y, 4);
		const int e = std::max((bx >> 23) & 0xFF, (by >> 23) & 0xFF);
		sa = x;
		sb = y;
		a = Vec4F32::Splat(x);
		b = Vec4F32::Splat(y);
		slow = e < 16;
		if (slow)
			return;
		const uint32_t downBits = (uint32_t)(269 - e) << 23;
		const uint32_t upBits = (uint32_t)(e - 15) << 23;
		float down, upf;
		memcpy(&down, &downBits, 4);
		memcpy(&upf, &upBits, 4);
		const int ia = (int)(x * down);
		const int ib = (int)(y * down);
		fa = Vec4S32::Splat(ia);
		dh = Vec4S32::Splat((ib - ia) >> 8);
		dl = Vec4S32::Splat((ib - ia) & 255);
		up = Vec4F32::Splat(upf);
	}

	Vec4F32 At(Vec4S32 k, Vec4S32 k0, Vec4S32 k256) const {
		if (slow)
			return GELerp4(a, b, k, k0, k256);
		const Vec4S32 q = dh.Mul16(k) + dl.Mul16(k).Shr<8>();
		return Select(k0, a, Select(k256, b, Vec4F32FromS32(fa + q) * up));
	}
};

// GEBezierEval4 of four fixed points: the first level from GELerpFixed.
struct GEBezierFixed {
	GELerpFixed l[3];
	void Init(const float p[4]) {
		for (int i = 0; i < 3; ++i)
			l[i].Init(p[i], p[i + 1]);
	}
	Vec4F32 Eval(Vec4S32 k, Vec4S32 k0, Vec4S32 k256, Vec4F32 *ab = nullptr, Vec4F32 *bc = nullptr) const {
		const Vec4F32 a = l[0].At(k, k0, k256);
		const Vec4F32 b = l[1].At(k, k0, k256);
		const Vec4F32 c = l[2].At(k, k0, k256);
		const Vec4F32 x = GELerp4(a, b, k, k0, k256);
		const Vec4F32 y = GELerp4(b, c, k, k0, k256);
		if (ab) {
			*ab = x;
			*bc = y;
		}
		return GELerp4(x, y, k, k0, k256);
	}
};

static NO_INLINE Vec4F32 GEAdd4Small(Vec4F32 a, Vec4F32 b, Vec4S32 small, Vec4F32 r) {
	alignas(16) float av[4], bv[4], rv[4];
	alignas(16) int sv[4];
	a.Store(av);
	b.Store(bv);
	r.Store(rv);
	small.Store(sv);
	for (int i = 0; i < 4; ++i) {
		if (sv[i])
			rv[i] = GEAdd(av[i], bv[i]);
	}
	return Vec4F32::Load(rv);
}

// Four of GEAdd at once: both terms truncated to the precision of the larger one, then added exactly.
static __forceinline Vec4F32 GEAdd4(Vec4F32 a, Vec4F32 b) {
	const Vec4S32 expMask = Vec4S32::Splat(0x7F800000);
	const Vec4S32 absMask = Vec4S32::Splat(0x7FFFFFFF);
	const Vec4S32 ba = Vec4S32FromBits(a);
	const Vec4S32 bb = Vec4S32FromBits(b);
	const Vec4F32 za = Select((ba & expMask).CompareEq(Vec4S32::Zero()), Vec4F32::Zero(), a);
	const Vec4F32 zb = Select((bb & expMask).CompareEq(Vec4S32::Zero()), Vec4F32::Zero(), b);
	const Vec4S32 e = Vec4S32FromBits(Vec4F32FromBits(ba & absMask).Max(Vec4F32FromBits(bb & absMask))) & expMask;
	const Vec4F32 down = Vec4F32FromBits(Vec4S32::Splat((int)(269u << 23)) - e);
	const Vec4F32 up = Vec4F32FromBits(e - Vec4S32::Splat(15 << 23));
	const Vec4F32 r = Vec4F32FromS32(Vec4S32FromF32(za * down) + Vec4S32FromF32(zb * down)) * up;
	const Vec4S32 small = e.CompareLt(Vec4S32::Splat(16 << 23));
	if (AnyCompareBitsSet(small))
		return GEAdd4Small(a, b, small, r);
	return r;
}

// Four de Casteljau evaluations (GEBezierEval), with the last two points for the tangents.
static inline Vec4F32 GEBezierEval4(const Vec4F32 p[4], Vec4S32 k, Vec4S32 k0, Vec4S32 k256, Vec4F32 *ab = nullptr, Vec4F32 *bc = nullptr) {
	const Vec4F32 a = GELerp4(p[0], p[1], k, k0, k256);
	const Vec4F32 b = GELerp4(p[1], p[2], k, k0, k256);
	const Vec4F32 c = GELerp4(p[2], p[3], k, k0, k256);
	const Vec4F32 l = GELerp4(a, b, k, k0, k256);
	const Vec4F32 r = GELerp4(b, c, k, k0, k256);
	if (ab) {
		*ab = l;
		*bc = r;
	}
	return GELerp4(l, r, k, k0, k256);
}

// Colors: the same lerp on 15-bit integers, so (b - a) * k fits 16-bit multiplies as is.
static inline Vec4S32 GEBezierEvalColor4(const Vec4S32 p[4], Vec4S32 k) {
	auto lerp = [k](Vec4S32 a, Vec4S32 b) { return a + (b - a).Mul16(k).Shr<8>(); };
	const Vec4S32 a = lerp(p[0], p[1]);
	const Vec4S32 b = lerp(p[1], p[2]);
	const Vec4S32 c = lerp(p[2], p[3]);
	return lerp(lerp(a, b), lerp(b, c));
}

// Set to evaluate the GE's Bezier patches with the scalar code, which the vectorized code is tested against.
bool g_splineGEScalar = false;

// TessellateBezierGE four lanes at a time: at each v step the patch's four control columns in the four
// lanes, then each row of those at four u steps.
template <bool sampleNrm, bool sampleCol, bool sampleTex, bool patchFacing>
static void TessellateBezierGESIMD(OutputBuffers &output, const BezierSurface &surface, const ControlPoints &points) {
	const int tessU = surface.tess_u;
	const int tessV = surface.tess_v;
	// Per v step: pos, ab, bc (3 each) and uv (2), one vector of four columns each; colors likewise.
	enum { POS = 0, AB = 3, BC = 6, TEX = 9, COMPS = 11 };
	std::vector<Vec4F32> columns((tessV + 1) * COMPS);
	std::vector<Vec4S32> columnColors(sampleCol ? (tessV + 1) * 4 : 0);
	// k for each u step, padded to whole vectors.
	const int groupsU = (tessU + 1 + 3) / 4;
	std::vector<int> ku(groupsU * 4);
	for (int i = 0; i < (int)ku.size(); ++i)
		ku[i] = BezierParam256(std::min(i, tessU), tessU);

	for (int patch_u = 0; patch_u < surface.num_patches_u; ++patch_u) {
		const int start_u = surface.GetTessStart(patch_u);
		for (int patch_v = 0; patch_v < surface.num_patches_v; ++patch_v) {
			const int start_v = surface.GetTessStart(patch_v);
			const ControlPoint *patch = points.points + surface.GetPointIndex(patch_u, patch_v);
			const ControlPoint *const rows[4] = { patch, patch + surface.num_points_u, patch + surface.num_points_u * 2, patch + surface.num_points_u * 3 };

			for (int tile_v = start_v; tile_v <= tessV; ++tile_v) {
				const int kvs = BezierParam256(tile_v, tessV);
				const Vec4S32 kv = Vec4S32::Splat(kvs);
				const Vec4S32 kv0 = kv.CompareEq(Vec4S32::Zero());
				const Vec4S32 kv256 = kv.CompareEq(Vec4S32::Splat(256));
				Vec4F32 *col = &columns[tile_v * COMPS];
				for (int j = 0; j < 3; ++j) {
					Vec4F32 p[4];
					for (int r = 0; r < 4; ++r) {
						alignas(16) const float v[4] = { rows[r][0].pos[j], rows[r][1].pos[j], rows[r][2].pos[j], rows[r][3].pos[j] };
						p[r] = FlushDenormals4(Vec4F32::Load(v));
					}
					col[POS + j] = GEBezierEval4(p, kv, kv0, kv256, &col[AB + j], &col[BC + j]);
				}
				if constexpr (sampleTex) {
					for (int j = 0; j < 2; ++j) {
						Vec4F32 p[4];
						for (int r = 0; r < 4; ++r) {
							alignas(16) const float v[4] = { rows[r][0].uv[j], rows[r][1].uv[j], rows[r][2].uv[j], rows[r][3].uv[j] };
							p[r] = FlushDenormals4(Vec4F32::Load(v));
						}
						col[TEX + j] = GEBezierEval4(p, kv, kv0, kv256);
					}
				}
				if constexpr (sampleCol) {
					for (int j = 0; j < 4; ++j) {
						Vec4S32 p[4];
						for (int r = 0; r < 4; ++r) {
							alignas(16) const int v[4] = { (int)rows[r][0].col[j], (int)rows[r][1].col[j], (int)rows[r][2].col[j], (int)rows[r][3].col[j] };
							p[r] = Vec4S32::Load(v).Shl<7>() | Vec4S32::Splat(0x7F);
						}
						columnColors[tile_v * 4 + j] = GEBezierEvalColor4(p, kv);
					}
				}
			}

			for (int tile_v = start_v; tile_v <= tessV; ++tile_v) {
				const int index_v = surface.GetIndexV(patch_v, tile_v);
				const int kvs = BezierParam256(tile_v, tessV);
				alignas(16) float cols[COMPS][4];
				for (int c = 0; c < COMPS; ++c)
					columns[tile_v * COMPS + c].Store(cols[c]);
				alignas(16) int colColors[4][4];
				if constexpr (sampleCol) {
					for (int j = 0; j < 4; ++j)
						columnColors[tile_v * 4 + j].Store(colColors[j]);
				}
				GEBezierFixed rowPos[3], rowAB[3], rowBC[3], rowTex[2];
				for (int j = 0; j < 3; ++j) {
					rowPos[j].Init(cols[POS + j]);
					if constexpr (sampleNrm) {
						rowAB[j].Init(cols[AB + j]);
						rowBC[j].Init(cols[BC + j]);
					}
				}
				if constexpr (sampleTex) {
					for (int j = 0; j < 2; ++j)
						rowTex[j].Init(cols[TEX + j]);
				}

				for (int tile_u = start_u; tile_u <= tessU; tile_u += 4) {
					const Vec4S32 k = Vec4S32::Load(&ku[tile_u]);
					const Vec4S32 k0 = k.CompareEq(Vec4S32::Zero());
					const Vec4S32 k256 = k.CompareEq(Vec4S32::Splat(256));
					const int lanes = std::min(4, tessU + 1 - tile_u);

					alignas(16) float pos[3][4], uv[2][4], tu[3][4], tv[3][4];
					alignas(16) int color[4][4];
					for (int j = 0; j < 3; ++j) {
						Vec4F32 ab, bc;
						rowPos[j].Eval(k, k0, k256, &ab, &bc).Store(pos[j]);
						if constexpr (sampleNrm) {
							GEAdd4(bc, Vec4F32::Zero() - ab).Store(tu[j]);
							const Vec4F32 eab = rowAB[j].Eval(k, k0, k256);
							const Vec4F32 ebc = rowBC[j].Eval(k, k0, k256);
							GEAdd4(ebc, Vec4F32::Zero() - eab).Store(tv[j]);
						}
					}
					if constexpr (sampleTex) {
						for (int j = 0; j < 2; ++j)
							rowTex[j].Eval(k, k0, k256).Store(uv[j]);
					}
					if constexpr (sampleCol) {
						for (int j = 0; j < 4; ++j) {
							const Vec4S32 p[4] = { Vec4S32::Splat(colColors[j][0]), Vec4S32::Splat(colColors[j][1]), Vec4S32::Splat(colColors[j][2]), Vec4S32::Splat(colColors[j][3]) };
							GEBezierEvalColor4(p, k).Shr<7>().Store(color[j]);
						}
					}

					for (int i = 0; i < lanes; ++i) {
						const int index_u = surface.GetIndexU(patch_u, tile_u + i);
						SimpleVertex &vert = output.vertices[surface.GetIndex(index_u, index_v, patch_u, patch_v)];
						vert.pos = Vec3f(pos[0][i], pos[1][i], pos[2][i]);
						if constexpr (sampleCol) {
							vert.color_32 = (u32)color[0][i] | ((u32)color[1][i] << 8) | ((u32)color[2][i] << 16) | ((u32)color[3][i] << 24);
						} else {
							vert.color_32 = points.defcolor;
						}
						if constexpr (sampleTex) {
							vert.uv[0] = uv[0][i];
							vert.uv[1] = uv[1][i];
						} else {
							// Generated: the parameter itself (exp140).
							vert.uv[0] = patch_u + ku[tile_u + i] * (1.0f / 256.0f);
							vert.uv[1] = patch_v + kvs * (1.0f / 256.0f);
						}
						if constexpr (sampleNrm) {
							// Unnormalized: lighting normalizes it. Vertex normals are unused (exp142).
							for (int j = 0; j < 3; ++j) {
								const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
								const GERowTerm terms[2] = { GEProduct(tu[j1][i], tv[j2][i]), GEProduct(-tu[j2][i], tv[j1][i]) };
								vert.nrm[j] = GERowSum(terms, 2);
							}
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
	}
}

// The GE's Bezier patches: each patch's four control columns at every v step, then each row of those at u.
template <bool sampleNrm, bool sampleCol, bool sampleTex, bool patchFacing>
static void TessellateBezierGE(OutputBuffers &output, const BezierSurface &surface, const ControlPoints &points) {
	std::vector<GEBezierColumn> columns((surface.tess_v + 1) * 4);
	for (int patch_u = 0; patch_u < surface.num_patches_u; ++patch_u) {
		const int start_u = surface.GetTessStart(patch_u);
		for (int patch_v = 0; patch_v < surface.num_patches_v; ++patch_v) {
			const int start_v = surface.GetTessStart(patch_v);
			const ControlPoint *patch = points.points + surface.GetPointIndex(patch_u, patch_v);
			const ControlPoint *const rows[4] = { patch, patch + surface.num_points_u, patch + surface.num_points_u * 2, patch + surface.num_points_u * 3 };

			for (int tile_v = start_v; tile_v <= surface.tess_v; ++tile_v) {
				const int kv = BezierParam256(tile_v, surface.tess_v);
				for (int c = 0; c < 4; ++c) {
					GEBezierColumn &column = columns[tile_v * 4 + c];
					for (int j = 0; j < 3; ++j) {
						const float p[4] = { rows[0][c].pos[j], rows[1][c].pos[j], rows[2][c].pos[j], rows[3][c].pos[j] };
						column.pos[j] = GEBezierEval(p, kv, &column.ab[j], &column.bc[j]);
					}
					if constexpr (sampleTex) {
						for (int j = 0; j < 2; ++j) {
							const float p[4] = { rows[0][c].uv[j], rows[1][c].uv[j], rows[2][c].uv[j], rows[3][c].uv[j] };
							column.tex[j] = GEBezierEval(p, kv);
						}
					}
					if constexpr (sampleCol) {
						for (int j = 0; j < 4; ++j) {
							int p[4];
							for (int r = 0; r < 4; ++r)
								p[r] = ((int)rows[r][c].col[j] << 7) | 0x7F;
							column.col[j] = GEBezierEvalColor(p, kv);
						}
					}
				}
			}

			for (int tile_u = start_u; tile_u <= surface.tess_u; ++tile_u) {
				const int index_u = surface.GetIndexU(patch_u, tile_u);
				const int ku = BezierParam256(tile_u, surface.tess_u);
				for (int tile_v = start_v; tile_v <= surface.tess_v; ++tile_v) {
					const int index_v = surface.GetIndexV(patch_v, tile_v);
					SimpleVertex &vert = output.vertices[surface.GetIndex(index_u, index_v, patch_u, patch_v)];
					const GEBezierColumn *cols = &columns[tile_v * 4];
					Vec3f tangentU, tangentV;
					for (int j = 0; j < 3; ++j) {
						const float row[4] = { cols[0].pos[j], cols[1].pos[j], cols[2].pos[j], cols[3].pos[j] };
						float ab, bc;
						vert.pos[j] = GEBezierEval(row, ku, &ab, &bc);
						if constexpr (sampleNrm) {
							tangentU[j] = GEAdd(bc, -ab);
							const float abRow[4] = { cols[0].ab[j], cols[1].ab[j], cols[2].ab[j], cols[3].ab[j] };
							const float bcRow[4] = { cols[0].bc[j], cols[1].bc[j], cols[2].bc[j], cols[3].bc[j] };
							tangentV[j] = GEAdd(GEBezierEval(bcRow, ku), -GEBezierEval(abRow, ku));
						}
					}
					if constexpr (sampleCol) {
						u32 color = 0;
						for (int j = 0; j < 4; ++j) {
							const int row[4] = { cols[0].col[j], cols[1].col[j], cols[2].col[j], cols[3].col[j] };
							color |= (u32)(GEBezierEvalColor(row, ku) >> 7) << (8 * j);
						}
						vert.color_32 = color;
					} else {
						vert.color_32 = points.defcolor;
					}
					if constexpr (sampleTex) {
						for (int j = 0; j < 2; ++j) {
							const float row[4] = { cols[0].tex[j], cols[1].tex[j], cols[2].tex[j], cols[3].tex[j] };
							vert.uv[j] = GEBezierEval(row, ku);
						}
					} else {
						// Generated: the parameter itself (exp140).
						vert.uv[0] = patch_u + ku * (1.0f / 256.0f);
						vert.uv[1] = patch_v + BezierParam256(tile_v, surface.tess_v) * (1.0f / 256.0f);
					}
					if constexpr (sampleNrm) {
						// Unnormalized: lighting normalizes it. Vertex normals are unused (exp142).
						for (int j = 0; j < 3; ++j) {
							const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
							const GERowTerm terms[2] = { GEProduct(tangentU[j1], tangentV[j2]), GEProduct(-tangentU[j2], tangentV[j1]) };
							vert.nrm[j] = GERowSum(terms, 2);
						}
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
}

// Each patch is evaluated one row of vertices (fixed v) at a time, four vertices along u at once,
// one attribute component per register: first the four control point rows are folded into four
// columns with the v weights, then those are weighted along u.
template<class Surface>
class SubdivisionSurface {
public:
	template <bool sampleNrm, bool sampleCol, bool sampleTex, bool patchFacing>
	static void Tessellate(OutputBuffers &output, const Surface &surface, const ControlPoints &points, const Weight2D &weights) {
		if constexpr (std::is_same_v<Surface, SplineSurface>) {
			if (surface.geExact) {
				TessellateSplineGE<sampleNrm, sampleCol, sampleTex, patchFacing>(output, surface, points);
				surface.BuildIndex(output.indices, output.count);
				return;
			}
		} else {
			if (surface.geExact) {
				if (g_splineGEScalar)
					TessellateBezierGE<sampleNrm, sampleCol, sampleTex, patchFacing>(output, surface, points);
				else
					TessellateBezierGESIMD<sampleNrm, sampleCol, sampleTex, patchFacing>(output, surface, points);
				surface.BuildIndex(output.indices, output.count);
				return;
			}
		}
		const float inv_u = 1.0f / (float)surface.tess_u;
		const float inv_v = 1.0f / (float)surface.tess_v;
		const WeightTable &wu = *weights.u;
		const WeightTable &wv = *weights.v;

		static const int laneOffsets[4] = { 0, 1, 2, 3 };
		const Vec4S32 lanes = Vec4S32::Load(laneOffsets);
		const Vec4F32 one = Vec4F32::Splat(1.0f);
		const Vec4F32 minusOne = Vec4F32::Splat(-1.0f);
		const Vec4F32 zero = Vec4F32::Zero();
		const Vec4F32 defcolor = Vec4F32FromBits(Vec4S32::Splat((int)points.defcolor));
		const Vec4F32 uScale = Vec4F32::Splat(output.uvScale.uScale), uOff = Vec4F32::Splat(output.uvScale.uOff);
		const Vec4F32 vScale = Vec4F32::Splat(output.uvScale.vScale), vOff = Vec4F32::Splat(output.uvScale.vOff);
		// Lanes that had an alpha below 255, among those that are stored.
		Vec4S32 partialAlpha = Vec4S32::Zero();

		for (int patch_u = 0; patch_u < surface.num_patches_u; ++patch_u) {
			const int start_u = surface.GetTessStart(patch_u);
			for (int patch_v = 0; patch_v < surface.num_patches_v; ++patch_v) {
				const int start_v = surface.GetTessStart(patch_v);

				// The 4x4 control points of this patch.
				const ControlPoint *patch = points.points + surface.GetPointIndex(patch_u, patch_v);
				const ControlPoint *const rows[4] = { patch, patch + surface.num_points_u, patch + surface.num_points_u * 2, patch + surface.num_points_u * 3 };

				for (int tile_v = start_v; tile_v <= surface.tess_v; ++tile_v) {
					const int index_v = surface.GetIndexV(patch_v, tile_v);
					const float bv[4] = { wv.Basis(0)[index_v], wv.Basis(1)[index_v], wv.Basis(2)[index_v], wv.Basis(3)[index_v] };
					const float dv[4] = { wv.Deriv(0)[index_v], wv.Deriv(1)[index_v], wv.Deriv(2)[index_v], wv.Deriv(3)[index_v] };

					alignas(16) float pos[4][4];
					alignas(16) float posDV[4][4];
					alignas(16) float col[4][4];
					alignas(16) float tex[4][4];
					SampleV(rows, offsetof(ControlPoint, pos), bv, pos);
					if constexpr (sampleNrm)
						SampleV(rows, offsetof(ControlPoint, pos), dv, posDV);
					if constexpr (sampleCol)
						SampleV(rows, offsetof(ControlPoint, col), bv, col);
					if constexpr (sampleTex)
						SampleV(rows, offsetof(ControlPoint, uv), bv, tex);

					// Which way a pole's limit normal points, when the pole is along v.
					const Vec4F32 signV = (tile_v * 2 > surface.tess_v) ? minusOne : one;

					for (int tile_u = start_u; tile_u <= surface.tess_u; tile_u += 4) {
						const int index_u = surface.GetIndexU(patch_u, tile_u);
						const Vec4F32 bu[4] = { Vec4F32::Load(wu.Basis(0) + index_u), Vec4F32::Load(wu.Basis(1) + index_u), Vec4F32::Load(wu.Basis(2) + index_u), Vec4F32::Load(wu.Basis(3) + index_u) };
						const Vec4S32 tiles = Vec4S32::Splat(tile_u) + lanes;

						Vec3x4 p = SampleU3(pos, bu);

						Vec3x4 nrm;
						if constexpr (sampleNrm) {
							const Vec4F32 du[4] = { Vec4F32::Load(wu.Deriv(0) + index_u), Vec4F32::Load(wu.Deriv(1) + index_u), Vec4F32::Load(wu.Deriv(2) + index_u), Vec4F32::Load(wu.Deriv(3) + index_u) };
							const Vec3x4 derivU = SampleU3(pos, du);
							const Vec3x4 derivV = SampleU3(posDV, bu);

							nrm = Cross(derivU, derivV);
							const Vec4F32 lenU2 = Dot(derivU, derivU);
							const Vec4F32 lenV2 = Dot(derivV, derivV);
							const Vec4S32 pole = lenU2.Min(lenV2).CompareLe(lenU2.Max(lenV2) * 1e-8f);
							if (AnyCompareBitsSet(pole)) {
								// A pole: a patch edge whose control points all meet at one point, like the top
								// of a dome. One derivative vanishes there, so the cross product is zero, or with
								// animated control points just rounding noise, and the normal would be NaN or
								// random (dark patches on Pac-Man Arrangement's ghosts, #12354). Use the limit
								// instead: next to an edge where dP/dv = 0, dP/dv ~ (u - u_edge) * d2P/dudv.
								const Vec3x4 derivUV = SampleU3(posDV, du);
								const Vec4F32 signU = Select(tiles.Shl<1>().CompareGt(Vec4S32::Splat(surface.tess_u)), minusOne, one);
								const Vec3x4 limitU = Cross(derivU, derivUV) * signU;
								const Vec3x4 limitV = Cross(derivUV, derivV) * signV;
								nrm = Select(pole, Select(lenV2.CompareLe(lenU2), limitU, limitV), nrm);
							}

							// Normalize, or 0,0,1 if there's nothing to normalize.
							const Vec4F32 len2 = Dot(nrm, nrm);
							const Vec4S32 valid = len2.CompareGt(zero);
							const Vec4F32 scale = len2.RecipSqrt();
							nrm = { Select(valid, nrm.x * scale, zero), Select(valid, nrm.y * scale, zero), Select(valid, nrm.z * scale, one) };
							if constexpr (patchFacing)
								nrm = nrm * minusOne;
						} else {
							nrm = { zero, zero, one };
						}

						Vec4F32 color;
						if constexpr (sampleCol) {
							Vec4S32 c[4];
							for (int i = 0; i < 4; i++) {
								c[i] = Vec4S32FromF32Round(SampleU(col, i, bu).Clamp(0.0f, 255.0f));
							}
							color = Vec4F32FromBits(c[0] | c[1].Shl<8>() | c[2].Shl<16>() | c[3].Shl<24>());
							const Vec4S32 stored = Vec4S32::Splat(surface.tess_u - tile_u + 1).CompareGt(lanes);
							partialAlpha = partialAlpha | (c[3].CompareLt(Vec4S32::Splat(255)) & stored);
						} else {
							color = defcolor;
						}

						Vec4F32 uvU, uvV;
						if constexpr (sampleTex) {
							uvU = SampleU(tex, 0, bu);
							uvV = SampleU(tex, 1, bu);
						} else {
							// Generate texcoord
							uvU = MulAdd(Vec4F32FromS32(tiles), Vec4F32::Splat(inv_u), Vec4F32::Splat((float)patch_u));
							uvV = Vec4F32::Splat(patch_v + tile_v * inv_v);
						}
						uvU = MulAdd(uvU, uScale, uOff);
						uvV = MulAdd(uvV, vScale, vOff);

						// Transpose into SimpleVertex order: uv, color, nrm, pos.
						Vec4F32 lo[4] = { uvU, uvV, color, nrm.x };
						Vec4F32 hi[4] = { nrm.y, nrm.z, p.x, p.y };
						Vec4F32::Transpose(lo[0], lo[1], lo[2], lo[3]);
						Vec4F32::Transpose(hi[0], hi[1], hi[2], hi[3]);
						alignas(16) float posZ[4];
						p.z.Store(posZ);

						SimpleVertex *dst = output.vertices + surface.GetIndex(index_u, index_v, patch_u, patch_v);
						const int count = std::min(4, surface.tess_u - tile_u + 1);
						for (int i = 0; i < count; i++) {
							float *out = (float *)&dst[i];
							lo[i].Store(out);
							hi[i].Store(out + 4);
							out[8] = posZ[i];
						}
					}
				}
			}
		}

		if constexpr (sampleCol) {
			output.fullAlpha = !AnyCompareBitsSet(partialAlpha);
		} else {
			output.fullAlpha = (points.defcolor >> 24) == 0xFF;
		}

		surface.BuildIndex(output.indices, output.count);
	}

	using TessFunc = void(*)(OutputBuffers &, const Surface &, const ControlPoints &, const Weight2D &);
	TEMPLATE_PARAMETER_DISPATCHER_FUNCTION(Tess, SubdivisionSurface::Tessellate, TessFunc);

	static void Tessellate(OutputBuffers &output, const Surface &surface, const ControlPoints &points, const Weight2D &weights, u32 origVertType) {
		const bool params[] = {
			// Shade mapping uses the normal even with lighting off (gpu/probe exp143).
			(origVertType & GE_VTYPE_NRM_MASK) != 0 || gstate.isLightingEnabled() || gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP,
			(origVertType & GE_VTYPE_COL_MASK) != 0,
			(origVertType & GE_VTYPE_TC_MASK) != 0,
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

	// Where we can, tessellate straight into decoded form, where DecodeVerts would put it, so there's no second
	// decode. Through mode is left to the decoder, which also tracks the UV bounds there. Anything already
	// queued would decode on top of the output, but with flushOnParams_ nothing is.
	const bool predecoded = curvesPredecoded_ && !(vertType & GE_VTYPE_THROUGH) && numDrawVerts_ == 0;
	// The output goes in one half of decoded_, the control points and other temporaries in the other.
	u8 *const outputBuf = predecoded ? decoded_ : decoded_ + DECODED_VERTEX_BUFFER_SIZE / 2;
	SimpleBufferManager managedBuf(predecoded ? decoded_ + DECODED_VERTEX_BUFFER_SIZE / 2 : decoded_, DECODED_VERTEX_BUFFER_SIZE / 2);

	const int num_points = surface.num_points_u * surface.num_points_v;
	u16 index_lower_bound = 0;
	u16 index_upper_bound = num_points - 1;
	IndexConverter ConvertIndex(vertType, indices);
	if (indices) {
		GetIndexBounds(indices, num_points, vertType, &index_lower_bound, &index_upper_bound);
	}

	u32 vertTypeID = GetVertTypeID(vertType, gstate.getUVGenMode());
	VertexDecoder *origVDecoder = GetVertexDecoder(vertTypeID);
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

	const u32 origVertType = vertType;
	UVScale neutralUVScale{1.0f, 1.0f, 0.0f, 0.0f};  // Avoid rescaling UV during normalization, it will happen anyway later (in DispatchSubmitPrim). Although ideally we should avoid running Decode at all there.
	vertType = ::NormalizeVertices(simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, neutralUVScale, origVDecoder, vertType);

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
	for (int idx = 0; idx < num_points; idx++) {
		points[idx] = simplified_control_points + (indices ? ConvertIndex(idx) : idx);
	}

	OutputBuffers output;
	output.vertices = (SimpleVertex *)outputBuf;
	output.indices = decIndex_;
	output.count = 0;

	const int maxVerts = DECODED_VERTEX_BUFFER_SIZE / 2 / vertexSize;

	surface.Init(maxVerts);
	if (predecoded) {
		const GETexMapMode uvGenMode = gstate.getUVGenMode();
		if (uvGenMode == GE_TEXMAP_TEXTURE_COORDS || uvGenMode == GE_TEXMAP_UNKNOWN) {
			output.uvScale = LoadUVScaleOffset(gstate);
		}
	}

	ControlPoints cpoints(points, num_points, managedBuf);
	if (cpoints.IsValid()) {
		// Run the tessellation!
		SoftwareTessellation(output, surface, origVertType, cpoints);
	} else {
		ERROR_LOG(Log::G3D, "Failed to allocate space for control point values, skipping curve draw");
	}

	u32 vertTypeWithIndex16 = (vertType & ~GE_VTYPE_IDX_MASK) | GE_VTYPE_IDX_16BIT;

	vertTypeID = GetVertTypeID(vertTypeWithIndex16, gstate.getUVGenMode());
	int generatedBytesRead;
	if (output.count) {
		ClipInfoFlags flags{};  // Don't need any special processing.
		if (predecoded) {
			VertexDecoder *dec = GetVertexDecoder(vertTypeID);
			if (SubmitPrim(output.vertices, output.indices, PatchPrimToPrim(surface.primType), output.count, dec, vertTypeID, true, &generatedBytesRead, flags)) {
				drawVerts_[numDrawVerts_ - 1].predecoded = true;
				if (!output.fullAlpha) {
					gstate_c.vertexFullAlpha = false;
				}
			}
		} else {
			DispatchSubmitPrim(output.vertices, output.indices, PatchPrimToPrim(surface.primType), output.count, vertTypeID, true, &generatedBytesRead, flags);
		}
	}

	if (flushOnParams_) {
		Flush();
	}
}
