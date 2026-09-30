// Copyright (c) 2012- PPSSPP Project.

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

// Correctness and speed of the software Bezier/spline tessellation in GPU/Common/SplineCommon.cpp,
// so that it can be optimized with a quick way to see that it still gets the right answer.
//
// The reference evaluates the surfaces the textbook way, in double precision: Bernstein polynomials
// for Bezier patches, and Cox-de Boor for the cubic B-splines, with the knots clamped at an "open"
// edge. Normals come from finite differences, taken a hair inside the patch so that they also come
// out as the limit at a pole. It shares nothing with the tessellator on purpose.

#include <cmath>
#include <cstdio>
#include <vector>

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/ge_constants.h"

#include "unittest/UnitTest.h"

using namespace Spline;

namespace {

struct D3 {
	double x = 0.0, y = 0.0, z = 0.0;
	D3 operator+(const D3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
	D3 operator-(const D3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
	D3 operator*(double s) const { return { x * s, y * s, z * s }; }
	double Length() const { return sqrt(x * x + y * y + z * z); }
};

D3 Cross(const D3 &a, const D3 &b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// Everything a control point or a tessellated vertex carries, as doubles.
struct RefVertex {
	D3 pos;
	double uv[2]{};
	double color[4]{};  // 0-255
};

double Bernstein(int i, double t) {
	const double s = 1.0 - t;
	switch (i) {
	case 0: return s * s * s;
	case 1: return 3.0 * t * s * s;
	case 2: return 3.0 * t * t * s;
	default: return t * t * t;
	}
}

// The knots of a cubic B-spline with count control points: uniform integers, so that the curve runs
// over [0, count - 3]. An open edge repeats its end knot four times, pulling the curve through the
// end control point. type bit 0 opens the first edge, bit 1 the last.
std::vector<double> SplineKnots(int count, int type) {
	const int n = count - 3;
	std::vector<double> knots(count + 4);
	for (int j = 0; j < (int)knots.size(); j++) {
		knots[j] = j - 3;
	}
	if (type & 1) {
		for (int j = 0; j < 4; j++) {
			knots[j] = 0.0;
		}
	}
	if (type & 2) {
		for (int j = n + 3; j < n + 7; j++) {
			knots[j] = n;
		}
	}
	return knots;
}

// Cox-de Boor, for one basis function.
double BSplineBasis(const std::vector<double> &knots, int i, int degree, double t) {
	if (degree == 0) {
		return knots[i] <= t && t < knots[i + 1] ? 1.0 : 0.0;
	}
	double result = 0.0;
	const double d1 = knots[i + degree] - knots[i];
	if (d1 != 0.0) {
		result += (t - knots[i]) / d1 * BSplineBasis(knots, i, degree - 1, t);
	}
	const double d2 = knots[i + degree + 1] - knots[i + 1];
	if (d2 != 0.0) {
		result += (knots[i + degree + 1] - t) / d2 * BSplineBasis(knots, i + 1, degree - 1, t);
	}
	return result;
}

// The four control points that affect a parameter along one axis, and their weights. Only these
// are evaluated, and nothing is allocated, so the test stays quick in a debug build.
struct Weights4 {
	int first;
	double w[4];
};

// A surface to evaluate: either one Bezier patch (4x4 control points), or a whole spline.
struct RefSurface {
	bool bezier = true;
	int pointsU = 4, pointsV = 4;
	std::vector<RefVertex> points;  // pointsU * pointsV, u fastest.
	int firstU = 0, firstV = 0;  // Bezier: the patch's first control point.
	std::vector<double> knotsU, knotsV;  // Spline.
	double endU = 1.0, endV = 1.0;  // The parameter range is [0, end].

	Weights4 Weights(bool alongU, double t) const {
		Weights4 out;
		if (bezier) {
			out.first = alongU ? firstU : firstV;
			for (int i = 0; i < 4; i++) {
				out.w[i] = Bernstein(i, t);
			}
			return out;
		}
		// The basis is half-open, so step back from the very end of the range.
		const double end = alongU ? endU : endV;
		const double tt = std::min(t, end - 1e-9);
		const std::vector<double> &knots = alongU ? knotsU : knotsV;
		// The span tt is in: knots[span] <= tt < knots[span + 1]. Basis functions span - 3 to span
		// are the only ones that aren't zero there.
		int span = 3;
		while (span + 1 < (int)knots.size() - 4 && knots[span + 1] <= tt) {
			span++;
		}
		out.first = span - 3;
		for (int i = 0; i < 4; i++) {
			out.w[i] = BSplineBasis(knots, out.first + i, 3, tt);
		}
		return out;
	}

	RefVertex Evaluate(double u, double v, bool posOnly = false) const {
		const Weights4 wu = Weights(true, u);
		const Weights4 wv = Weights(false, v);
		RefVertex out;
		for (int j = 0; j < 4; j++) {
			for (int i = 0; i < 4; i++) {
				const double w = wu.w[i] * wv.w[j];
				const RefVertex &p = points[(wv.first + j) * pointsU + wu.first + i];
				out.pos = out.pos + p.pos * w;
				if (posOnly) {
					continue;
				}
				for (int c = 0; c < 2; c++) {
					out.uv[c] += p.uv[c] * w;
				}
				for (int c = 0; c < 4; c++) {
					out.color[c] += p.color[c] * w;
				}
			}
		}
		return out;
	}

	// The surface normal, cross(dP/du, dP/dv), from central differences at a point moved a little
	// towards the middle - at a pole the derivatives vanish, and this gives the limit.
	D3 Normal(double u, double v) const {
		const double inset = 1e-4, h = 1e-6;
		u += (u < endU * 0.5) ? inset : -inset;
		v += (v < endV * 0.5) ? inset : -inset;
		const D3 du = (Evaluate(u + h, v, true).pos - Evaluate(u - h, v, true).pos) * (0.5 / h);
		const D3 dv = (Evaluate(u, v + h, true).pos - Evaluate(u, v - h, true).pos) * (0.5 / h);
		const D3 n = Cross(du, dv);
		return n * (1.0 / n.Length());
	}
};

struct TestCase {
	const char *name;
	bool bezier;
	int pointsU, pointsV;
	int tessU, tessV;
	int typeU = 0, typeV = 0;  // Spline only.
	u32 vertType = GE_VTYPE_POS_FLOAT | GE_VTYPE_NRM_FLOAT | GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888;
	bool patchFacing = false;
	int poleEdge = -1;  // Collapse this edge to one point: 0 = first row (v = 0), 1 = first column (u = 0).
	float poleNoise = 0.0f;  // ...but only nearly, the way animated control points come out.
};

// A bumpy, uneven grid of control points with varying UVs and colors, so that a mixed-up weight or
// attribute shows.
std::vector<SimpleVertex> MakeControlPoints(const TestCase &tc) {
	std::vector<SimpleVertex> points(tc.pointsU * tc.pointsV);
	for (int j = 0; j < tc.pointsV; j++) {
		for (int i = 0; i < tc.pointsU; i++) {
			SimpleVertex &p = points[j * tc.pointsU + i];
			p.pos = Vec3Packedf(i * 0.5f + 0.07f * j, 0.4f * sinf(i * 0.7f + j * 0.3f) + 0.1f * i, j * 0.45f - 0.05f * i);
			p.nrm = Vec3Packedf(0.0f, 1.0f, 0.0f);
			p.uv[0] = i * 0.25f + 0.02f * j;
			p.uv[1] = j * 0.3f;
			p.color[0] = (u8)(40 + 13 * i);
			p.color[1] = (u8)(200 - 11 * j);
			p.color[2] = (u8)(90 + 7 * i + 5 * j);
			p.color[3] = (u8)(255 - 9 * i);
		}
	}
	if (tc.poleEdge == 0) {
		for (int i = 1; i < tc.pointsU; i++) {
			points[i].pos = Vec3Packedf(points[0].pos.x + i * tc.poleNoise, points[0].pos.y - i * tc.poleNoise, points[0].pos.z);
		}
	} else if (tc.poleEdge == 1) {
		for (int j = 1; j < tc.pointsV; j++) {
			points[j * tc.pointsU].pos = Vec3Packedf(points[0].pos.x, points[0].pos.y + j * tc.poleNoise, points[0].pos.z - j * tc.poleNoise);
		}
	}
	return points;
}

RefSurface MakeReference(const TestCase &tc, const std::vector<SimpleVertex> &points) {
	RefSurface ref;
	ref.bezier = tc.bezier;
	ref.pointsU = tc.pointsU;
	ref.pointsV = tc.pointsV;
	for (const SimpleVertex &p : points) {
		RefVertex r;
		r.pos = { p.pos.x, p.pos.y, p.pos.z };
		r.uv[0] = p.uv[0];
		r.uv[1] = p.uv[1];
		for (int c = 0; c < 4; c++) {
			r.color[c] = p.color[c];
		}
		ref.points.push_back(r);
	}
	if (!tc.bezier) {
		ref.knotsU = SplineKnots(tc.pointsU, tc.typeU);
		ref.knotsV = SplineKnots(tc.pointsV, tc.typeV);
		ref.endU = tc.pointsU - 3;
		ref.endV = tc.pointsV - 3;
	}
	return ref;
}

struct Output {
	std::vector<SimpleVertex> vertices;
	std::vector<u16> indices;
	int indexCount = 0;
	int tessU = 0, tessV = 0;  // As the surface ended up, after Init.
	int patchesU = 0, patchesV = 0;
};

template <class Surface>
void Tessellate(Surface &surface, u32 vertType, const std::vector<SimpleVertex> &points, int numVerts, int numIndices, Output &out) {
	const int numPoints = surface.num_points_u * surface.num_points_v;
	std::vector<const SimpleVertex *> pointers(numPoints);
	for (int i = 0; i < numPoints; i++) {
		pointers[i] = &points[i];
	}
	std::vector<Vec3f> pos(numPoints);
	std::vector<Vec2f> tex(numPoints);
	std::vector<Vec4f> col(numPoints);
	ControlPoints cpoints;
	cpoints.pos = pos.data();
	cpoints.tex = tex.data();
	cpoints.col = col.data();
	cpoints.Convert(pointers.data(), numPoints);

	out.vertices.assign(numVerts, SimpleVertex{});
	out.indices.assign(numIndices, 0);
	OutputBuffers buffers;
	buffers.vertices = out.vertices.data();
	buffers.indices = out.indices.data();
	buffers.count = 0;
	SoftwareTessellation(buffers, surface, vertType, cpoints);
	out.indexCount = buffers.count;
	out.tessU = surface.tess_u;
	out.tessV = surface.tess_v;
	out.patchesU = surface.num_patches_u;
	out.patchesV = surface.num_patches_v;
}

void RunTessellator(const TestCase &tc, const std::vector<SimpleVertex> &points, Output &out) {
	const int maxVertices = 65536;
	if (tc.bezier) {
		BezierSurface surface{};
		surface.num_points_u = tc.pointsU;
		surface.num_points_v = tc.pointsV;
		surface.tess_u = tc.tessU;
		surface.tess_v = tc.tessV;
		surface.num_patches_u = (tc.pointsU - 1) / 3;
		surface.num_patches_v = (tc.pointsV - 1) / 3;
		surface.primType = GE_PATCHPRIM_TRIANGLES;
		surface.patchFacing = tc.patchFacing;
		surface.Init(maxVertices);
		const int patches = surface.num_patches_u * surface.num_patches_v;
		Tessellate(surface, tc.vertType, points, (surface.tess_u + 1) * (surface.tess_v + 1) * patches, surface.tess_u * surface.tess_v * 6 * patches, out);
	} else {
		SplineSurface surface{};
		surface.num_points_u = tc.pointsU;
		surface.num_points_v = tc.pointsV;
		surface.tess_u = tc.tessU;
		surface.tess_v = tc.tessV;
		surface.type_u = tc.typeU;
		surface.type_v = tc.typeV;
		surface.num_patches_u = tc.pointsU - 3;
		surface.num_patches_v = tc.pointsV - 3;
		surface.primType = GE_PATCHPRIM_TRIANGLES;
		surface.patchFacing = tc.patchFacing;
		surface.Init(maxVertices);
		const int divU = surface.num_patches_u * surface.tess_u;
		const int divV = surface.num_patches_v * surface.tess_v;
		Tessellate(surface, tc.vertType, points, (divU + 1) * (divV + 1), divU * divV * 6, out);
	}
}

bool CheckVertex(const TestCase &tc, const RefSurface &ref, const SimpleVertex &got, const SimpleVertex &defaultPoint,
	double u, double v, double genU, double genV, int index) {
	const RefVertex want = ref.Evaluate(u, v);
	const D3 gotPos = { got.pos.x, got.pos.y, got.pos.z };
	const double posError = (gotPos - want.pos).Length();
	if (posError > 1e-4) {
		printf("%s: vertex %d at (%.3f, %.3f): pos %f %f %f, want %f %f %f\n", tc.name, index, u, v,
			gotPos.x, gotPos.y, gotPos.z, want.pos.x, want.pos.y, want.pos.z);
		return false;
	}

	const bool hasTex = (tc.vertType & GE_VTYPE_TC_MASK) != 0;
	const double wantU = hasTex ? want.uv[0] : genU;
	const double wantV = hasTex ? want.uv[1] : genV;
	if (fabs(got.uv[0] - wantU) > 1e-4 || fabs(got.uv[1] - wantV) > 1e-4) {
		printf("%s: vertex %d at (%.3f, %.3f): uv %f %f, want %f %f\n", tc.name, index, u, v, got.uv[0], got.uv[1], wantU, wantV);
		return false;
	}

	const bool hasColor = (tc.vertType & GE_VTYPE_COL_MASK) != 0;
	for (int c = 0; c < 4; c++) {
		const double wantC = hasColor ? want.color[c] : defaultPoint.color[c];
		if (fabs(got.color[c] - wantC) > 1.001) {
			printf("%s: vertex %d at (%.3f, %.3f): color channel %d is %d, want %.2f\n", tc.name, index, u, v, c, got.color[c], wantC);
			return false;
		}
	}

	const bool hasNormal = (tc.vertType & GE_VTYPE_NRM_MASK) != 0;
	D3 wantN = { 0.0, 0.0, 1.0 };
	if (hasNormal) {
		wantN = ref.Normal(u, v);
		if (tc.patchFacing) {
			wantN = wantN * -1.0;
		}
	}
	const D3 gotN = { got.nrm.x, got.nrm.y, got.nrm.z };
	// The length only to rsqrt precision: x86 normalizes with a bare _mm_rsqrt_ps (relative error up to 3.7e-4).
	const double gotLen = gotN.Length();
	const double dot = (gotN.x * wantN.x + gotN.y * wantN.y + gotN.z * wantN.z) / gotLen;
	if (!(fabs(gotLen - 1.0) < 5e-4 && dot > 0.9999)) {
		printf("%s: vertex %d at (%.3f, %.3f): normal %f %f %f, want %f %f %f\n", tc.name, index, u, v,
			gotN.x, gotN.y, gotN.z, wantN.x, wantN.y, wantN.z);
		return false;
	}
	return true;
}

bool CheckIndices(const TestCase &tc, const Output &out, int expectedCount) {
	if (out.indexCount != expectedCount) {
		printf("%s: %d indices, want %d\n", tc.name, out.indexCount, expectedCount);
		return false;
	}
	for (int i = 0; i < out.indexCount; i++) {
		if (out.indices[i] >= out.vertices.size()) {
			printf("%s: index %d is %d, past the %d vertices\n", tc.name, i, out.indices[i], (int)out.vertices.size());
			return false;
		}
	}
	return true;
}

bool CheckCase(const TestCase &tc) {
	const std::vector<SimpleVertex> points = MakeControlPoints(tc);
	Output out;
	RunTessellator(tc, points, out);

	if (tc.bezier) {
		const int verticesPerPatch = (out.tessU + 1) * (out.tessV + 1);
		for (int pv = 0; pv < out.patchesV; pv++) {
			for (int pu = 0; pu < out.patchesU; pu++) {
				RefSurface ref = MakeReference(tc, points);
				ref.firstU = pu * 3;
				ref.firstV = pv * 3;
				const int base = (pv * out.patchesU + pu) * verticesPerPatch;
				for (int tv = 0; tv <= out.tessV; tv++) {
					for (int tu = 0; tu <= out.tessU; tu++) {
						const int index = base + tv * (out.tessU + 1) + tu;
						const double u = (double)tu / out.tessU, v = (double)tv / out.tessV;
						if (!CheckVertex(tc, ref, out.vertices[index], points[0], u, v, pu + u, pv + v, index)) {
							return false;
						}
					}
				}
			}
		}
		return CheckIndices(tc, out, out.tessU * out.tessV * 6 * out.patchesU * out.patchesV);
	}

	const RefSurface ref = MakeReference(tc, points);
	const int divU = out.patchesU * out.tessU, divV = out.patchesV * out.tessV;
	for (int iv = 0; iv <= divV; iv++) {
		for (int iu = 0; iu <= divU; iu++) {
			const int index = iv * (divU + 1) + iu;
			const double u = (double)iu / out.tessU, v = (double)iv / out.tessV;
			if (!CheckVertex(tc, ref, out.vertices[index], points[0], u, v, u, v, index)) {
				return false;
			}
		}
	}
	return CheckIndices(tc, out, divU * divV * 6);
}

}  // namespace

bool TestSplineTessellation() {
	const int savedQuality = g_Config.iSplineBezierQuality;
	g_Config.iSplineBezierQuality = (int)SplineQuality::HIGH_QUALITY;

	const u32 posOnly = GE_VTYPE_POS_FLOAT;
	const u32 posNrm = GE_VTYPE_POS_FLOAT | GE_VTYPE_NRM_FLOAT;

	std::vector<TestCase> cases = {
		{ "bezier, one patch", true, 4, 4, 8, 8 },
		{ "bezier, 2x3 patches, uneven tessellation", true, 7, 10, 5, 3 },
		{ "bezier, no attributes", true, 7, 4, 4, 6, 0, 0, posOnly },
		{ "bezier, normals only", true, 4, 7, 6, 2, 0, 0, posNrm },
		{ "bezier, patch facing", true, 4, 4, 5, 5, 0, 0, posNrm, true },
		{ "bezier, pole at v = 0", true, 4, 4, 8, 8, 0, 0, posNrm, false, 0 },
		{ "bezier, pole at u = 0", true, 4, 4, 8, 8, 0, 0, posNrm, false, 1 },
		{ "spline, pole at v = 0, open", false, 5, 6, 4, 4, 3, 3, posNrm, false, 0 },
		{ "bezier, pole at u = 0 with rounding noise", true, 4, 4, 8, 8, 0, 0, posNrm, false, 1, 1e-7f },
		{ "spline, pole at v = 0 with rounding noise", false, 5, 6, 4, 4, 3, 3, posNrm, false, 0, 1e-7f },
	};
	// Each edge type on each axis, one axis with three patches and the other with two.
	cases.push_back({ "spline, closed/open", false, 6, 5, 4, 6, 0, 3 });
	cases.push_back({ "spline, open first/open last", false, 6, 5, 4, 6, 1, 2 });
	cases.push_back({ "spline, open last/open first", false, 6, 5, 4, 6, 2, 1 });
	cases.push_back({ "spline, open/closed", false, 6, 5, 4, 6, 3, 0 });
	// A single patch, where both ends' knots meet in the same span.
	cases.push_back({ "spline, one patch, closed", false, 4, 4, 3, 3, 0, 0 });
	cases.push_back({ "spline, one patch, open first", false, 4, 4, 3, 3, 1, 1 });
	cases.push_back({ "spline, one patch, open last", false, 4, 4, 3, 3, 2, 2 });
	cases.push_back({ "spline, one patch, open", false, 4, 4, 3, 3, 3, 3 });
	cases.push_back({ "spline, no attributes", false, 7, 5, 2, 5, 1, 2, posOnly });
	cases.push_back({ "spline, patch facing", false, 5, 5, 4, 4, 0, 3, posNrm, true });

	bool ok = true;
	for (const TestCase &tc : cases) {
		if (!CheckCase(tc)) {
			ok = false;
		}
	}

	// Speed, for whoever is optimizing this. A big spline and a batch of Bezier patches, the two
	// shapes games send, with every attribute sampled.
	{
		TestCase spline = { "speed, spline", false, 10, 10, 8, 8, 3, 3 };
		TestCase bezier = { "speed, bezier", true, 10, 10, 8, 8 };
		for (const TestCase *tc : { &spline, &bezier }) {
			const std::vector<SimpleVertex> points = MakeControlPoints(*tc);
			Output out;
			const double callsPerSecond = CallsPerSecond([&] { RunTessellator(*tc, points, out); }, 0.1, 1);
			printf("%s: %dx%d control points, %d vertices: %.2f Mverts/s\n", tc->name, tc->pointsU, tc->pointsV,
				(int)out.vertices.size(), callsPerSecond * out.vertices.size() / 1000000.0);
		}
	}

	g_Config.iSplineBezierQuality = savedQuality;
	return ok;
}
