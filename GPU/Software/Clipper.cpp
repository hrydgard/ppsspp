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

#include "GPU/GPUState.h"

#include "GPU/Software/BinManager.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"
#include "GPU/Software/TransformUnit.h"

#include "Common/Profiler/Profiler.h"

namespace Clipper {

enum {
	CLIP_NEG_Z_BIT = 0x20,
};

static inline int CalcClipMask(const ClipCoords &v) {
	// This checks `x / w` compared to 1 or -1, skipping the division.
	if (v.z < -v.w)
		return -1;
	return 0;
}

// With depth clip on, a vertex beyond the near plane is clipped away before the viewport, so its screen
// position being out of range doesn't matter (the new vertices get checked after clipping).
static inline bool OutsideRangeBeforeClip(const ClipVertexData &v, bool depthClip) {
	return v.OutsideRange() && !(depthClip && CalcClipMask(v.clippos) != 0);
}

inline bool different_signs(float x, float y) {
	return ((x <= 0 && y > 0) || (x > 0 && y <= 0));
}

inline float clip_dotprod(const ClipVertexData &vert, float A, float B, float C, float D) {
	return (vert.clippos.x * A + vert.clippos.y * B + vert.clippos.z * C + vert.clippos.w * D);
}

inline void clip_interpolate(ClipVertexData &dest, float t, const ClipVertexData &a, const ClipVertexData &b) {
	bool outsideRange = false;
	dest.Lerp(t, a, b);
	dest.v.screenpos = TransformUnit::ClipToScreen(dest.clippos, &outsideRange);
	dest.v.clipw = dest.clippos.w;

	// If the clipped coordinate is outside range, then we throw it away.
	// This prevents a lot of inversions that shouldn't be drawn.
	if (outsideRange)
		dest.v.screenpos.x = 0x7FFFFFFF;
}

#define CLIP_LINE(PLANE_BIT, A, B, C, D)						\
{																\
	if (mask & PLANE_BIT) {										\
		float dp0 = clip_dotprod(*Vertices[0], A, B, C, D );	\
		float dp1 = clip_dotprod(*Vertices[1], A, B, C, D );	\
																\
		if (mask0 & PLANE_BIT) {								\
			if (dp0 < 0) {										\
				float t = dp1 / (dp1 - dp0);					\
				clip_interpolate(*Vertices[0], t, *Vertices[1], *Vertices[0]); \
			}													\
		}														\
		dp0 = clip_dotprod(*Vertices[0], A, B, C, D );			\
																\
		if (mask1 & PLANE_BIT) {								\
			if (dp1 < 0) {										\
				float t = dp1 / (dp1- dp0);						\
				clip_interpolate(*Vertices[1], t, *Vertices[1], *Vertices[0]); \
			}													\
		}														\
	}															\
}

// The GE compares the (float24) clip coordinates directly, no division: outside a plane when |c| > w. A
// primitive is culled when all its vertices are outside the same plane, x, y or z, for every primitive type
// and with clipping and depth clamp on or off (gpu/probe exp176 for x and y: a vertex exactly on the plane is
// inside, one a float24 step past it outside).
static bool cullXY = true;

void SetCullXY(bool cull) {
	cullXY = cull;
}

static inline int OutsideMask(const ClipCoords &p) {
	int mask = 0;
	if (cullXY) {
		if (p.x > p.w) mask |= 1;
		if (-p.x > p.w) mask |= 2;
		if (p.y > p.w) mask |= 4;
		if (-p.y > p.w) mask |= 8;
	}
	if (p.z > p.w) mask |= 16;
	if (-p.z > p.w) mask |= 32;
	return mask;
}

static void RotateUV(const VertexData &tl, const VertexData &br, VertexData &tr, VertexData &bl) {
	const int x1 = tl.screenpos.x;
	const int x2 = br.screenpos.x;
	const int y1 = tl.screenpos.y;
	const int y2 = br.screenpos.y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2)) {
		std::swap(bl.texturecoords, tr.texturecoords);
	}
}

// This is used for rectangle texture projection, which is very uncommon.
// To avoid complicating the common rectangle path, this just uses triangles.
static void AddTriangleRect(const VertexData &v0, const VertexData &v1, BinManager &binner) {
	VertexData buf[4];
	buf[0] = v1;
	buf[0].screenpos = ScreenCoords(v0.screenpos.x, v0.screenpos.y, v1.screenpos.z);
	buf[0].texturecoords = v0.texturecoords;

	buf[1] = v1;
	buf[1].screenpos = ScreenCoords(v0.screenpos.x, v1.screenpos.y, v1.screenpos.z);
	buf[1].texturecoords = Vec3Packed<float>(v0.texturecoords.x, v1.texturecoords.y, v0.texturecoords.z);

	buf[2] = v1;
	buf[2].screenpos = ScreenCoords(v1.screenpos.x, v0.screenpos.y, v1.screenpos.z);
	buf[2].texturecoords = Vec3Packed<float>(v1.texturecoords.x, v0.texturecoords.y, v1.texturecoords.z);

	buf[3] = v1;

	VertexData *topleft = &buf[0];
	VertexData *topright = &buf[1];
	VertexData *bottomleft = &buf[2];
	VertexData *bottomright = &buf[3];

	// DrawTriangle always culls, so sort out the drawing order.
	for (int i = 0; i < 4; ++i) {
		if (buf[i].screenpos.x < topleft->screenpos.x && buf[i].screenpos.y < topleft->screenpos.y)
			topleft = &buf[i];
		if (buf[i].screenpos.x > topright->screenpos.x && buf[i].screenpos.y < topright->screenpos.y)
			topright = &buf[i];
		if (buf[i].screenpos.x < bottomleft->screenpos.x && buf[i].screenpos.y > bottomleft->screenpos.y)
			bottomleft = &buf[i];
		if (buf[i].screenpos.x > bottomright->screenpos.x && buf[i].screenpos.y > bottomright->screenpos.y)
			bottomright = &buf[i];
	}

	RotateUV(v0, v1, *topright, *bottomleft);

	binner.AddTriangle(*topleft, *topright, *bottomleft);
	binner.AddTriangle(*bottomleft, *topright, *topleft);
	binner.AddTriangle(*topright, *bottomright, *bottomleft);
	binner.AddTriangle(*bottomleft, *bottomright, *topright);
}

void ProcessRect(const ClipVertexData &v0, const ClipVertexData &v1, BinManager &binner) {
	if (!binner.State().throughMode) {
		// If any verts were outside range, throw the entire prim away.
		if (v0.OutsideRange() || v1.OutsideRange())
			return;

		if (OutsideMask(v0.clippos) & OutsideMask(v1.clippos))
			return;
		// Rects aren't clipped: one with a vertex behind the camera is culled, with depth clip on or off.
		if (!(v0.clippos.w > 0.0f && v1.clippos.w > 0.0f))
			return;

		bool splitFog = v0.v.fogdepth != v1.v.fogdepth;
		if (splitFog) {
			// If they match the same 1/255, we can consider the fog flat.  Seen in Resistance.
			// More efficient if we can avoid splitting.
			static constexpr float foghalfstep = 0.5f / 255.0f;
			if (v1.v.fogdepth - foghalfstep <= v0.v.fogdepth && v1.v.fogdepth + foghalfstep >= v0.v.fogdepth)
				splitFog = false;
		}
		if (splitFog) {
			// Rectangles seem to always use nearest along X for fog depth, but reversed.
			// TODO: Check exactness of middle.
			VertexData vhalf0 = v1.v;
			vhalf0.screenpos.x = v0.v.screenpos.x + (v1.v.screenpos.x - v0.v.screenpos.x) / 2;
			vhalf0.texturecoords.x = v0.v.texturecoords.x + (v1.v.texturecoords.x - v0.v.texturecoords.x) / 2;

			VertexData vhalf1 = v1.v;
			vhalf1.screenpos.x = v0.v.screenpos.x + (v1.v.screenpos.x - v0.v.screenpos.x) / 2;
			vhalf1.screenpos.y = v0.v.screenpos.y;
			vhalf1.texturecoords.x = v0.v.texturecoords.x + (v1.v.texturecoords.x - v0.v.texturecoords.x) / 2;
			vhalf1.texturecoords.y = v0.v.texturecoords.y;

			VertexData vrev1 = v1.v;
			vrev1.fogdepth = v0.v.fogdepth;

			if (binner.State().textureProj) {
				AddTriangleRect(v0.v, vhalf0, binner);
				AddTriangleRect(vhalf1, vrev1, binner);
			} else {
				binner.AddRect(v0.v, vhalf0);
				binner.AddRect(vhalf1, vrev1);
			}
		} else if (binner.State().textureProj) {
			AddTriangleRect(v0.v, v1.v, binner);
		} else {
			binner.AddRect(v0.v, v1.v);
		}
	} else {
		// through mode handling
		if (Rasterizer::RectangleFastPath(v0.v, v1.v, binner)) {
			return;
		} else if (gstate.isModeClear() && !gstate.isDitherEnabled()) {
			binner.AddClearRect(v0.v, v1.v);
		} else {
			binner.AddRect(v0.v, v1.v);
		}
	}
}

void ProcessPoint(const ClipVertexData &v0, BinManager &binner) {
	// If any verts were outside range, throw the entire prim away.
	if (!binner.State().throughMode) {
		if (v0.OutsideRange())
			return;
		if (OutsideMask(v0.clippos))
			return;
	}

	// Points need no clipping. Will be bounds checked in the rasterizer (which seems backwards?)
	binner.AddPoint(v0.v);
}

void ProcessLine(const ClipVertexData &v0, const ClipVertexData &v1, BinManager &binner) {
	if (binner.State().throughMode) {
		// Actually, should clip this one too so we don't need to do bounds checks in the rasterizer.
		binner.AddLine(v0.v, v1.v);
		return;
	}

	const bool depthClip = gstate.isDepthClipEnabled();
	// If any verts were outside range, throw the entire prim away.
	if (OutsideRangeBeforeClip(v0, depthClip) || OutsideRangeBeforeClip(v1, depthClip))
		return;

	if (OutsideMask(v0.clippos) & OutsideMask(v1.clippos))
		return;

	int mask0 = CalcClipMask(v0.clippos);
	int mask1 = CalcClipMask(v1.clippos);
	int mask = mask0 | mask1;
	// See ProcessTriangle.
	if (!depthClip) {
		if (!(v0.clippos.w > 0.0f && v1.clippos.w > 0.0f))
			return;
		mask = 0;
	}
	if ((mask & CLIP_NEG_Z_BIT) == 0) {
		binner.AddLine(v0.v, v1.v);
		return;
	}

	ClipVertexData ClippedVertices[2] = { v0, v1 };
	ClipVertexData *Vertices[2] = { &ClippedVertices[0], &ClippedVertices[1] };
	CLIP_LINE(CLIP_NEG_Z_BIT,  0,  0,  1, 1);

	ClipVertexData data[2] = { *Vertices[0], *Vertices[1] };
	if (!data[0].OutsideRange() && !data[1].OutsideRange())
		binner.AddLine(data[0].v, data[1].v);
}

void ProcessTriangle(const ClipVertexData &v0, const ClipVertexData &v1, const ClipVertexData &v2, const ClipVertexData &provoking, BinManager &binner, bool reversed) {
	int mask = 0;
	if (!binner.State().throughMode) {
		const bool depthClip = gstate.isDepthClipEnabled();
		// If any verts were outside range, throw the entire prim away.
		if (OutsideRangeBeforeClip(v0, depthClip) || OutsideRangeBeforeClip(v1, depthClip) || OutsideRangeBeforeClip(v2, depthClip))
			return;
		// If all verts have negative W, we also cull.
		if (v0.clippos.w < 0.0f && v1.clippos.w < 0.0f && v2.clippos.w < 0.0f)
			return;

		mask |= CalcClipMask(v0.clippos);
		mask |= CalcClipMask(v1.clippos);
		mask |= CalcClipMask(v2.clippos);

		if (OutsideMask(v0.clippos) & OutsideMask(v1.clippos) & OutsideMask(v2.clippos))
			return;

		// With depth clip off, the GE doesn't clip at the near plane: the part beyond it is drawn with
		// extrapolated depth. A vertex behind the camera (w <= 0) culls the whole triangle instead.
		if (!depthClip) {
			if (!(v0.clippos.w > 0.0f && v1.clippos.w > 0.0f && v2.clippos.w > 0.0f))
				return;
			mask = 0;
		}
	}

	// No clipping is common, let's skip processing if we can.
	if ((mask & CLIP_NEG_Z_BIT) == 0) {
		if (gstate.getShadeMode() == GE_SHADE_FLAT) {
			// So that the order of clipping doesn't matter...
			VertexData corrected2 = v2.v;
			corrected2.color0 = provoking.v.color0;
			corrected2.color1 = provoking.v.color1;
			binner.AddTriangle(v0.v, v1.v, corrected2);
		} else {
			binner.AddTriangle(v0.v, v1.v, v2.v);
		}
		return;
	}

	// Clip at the near plane like the GE (gpu/probe exp43, exp44): new vertices are interpolated from
	// the inside vertex with the GE's math, and with one vertex outside (o), the quad is split from the
	// vertex before it (p) in the submitted order: (p, a, b) and (p, b, n), a and b being the new
	// vertices on p-o and o-n. reversed means we got the vertices in the opposite order.
	const ClipVertexData *src[3] = { &v0, &v1, &v2 };
	bool outside[3];
	int numOutside = 0;
	for (int i = 0; i < 3; ++i) {
		outside[i] = src[i]->clippos.z < -src[i]->clippos.w;
		numOutside += outside[i] ? 1 : 0;
	}

	auto nearPoint = [](ClipVertexData &dest, const ClipVertexData &in, const ClipVertexData &out) {
		const float t = TransformUnit::NearPlaneT(in.clippos, out.clippos);
		dest.Lerp(t, in, out);
		dest.clippos = TransformUnit::NearPlanePoint(in.clippos, out.clippos, t);
		// Texture coordinates go through the same arithmetic as the position (gpu/probe exp136).
		for (int c = 0; c < 3; ++c) {
			const float delta = TruncateToFloat24(GEAdd(out.v.texturecoords[c], -in.v.texturecoords[c]));
			dest.v.texturecoords[c] = TruncateToFloat24(GEAdd(ProductToFloat24((double)t * delta), in.v.texturecoords[c]));
		}
		bool outsideRange = false;
		dest.v.screenpos = TransformUnit::ClipToScreen(dest.clippos, &outsideRange);
		dest.v.clipw = dest.clippos.w;
		if (outsideRange)
			dest.v.screenpos.x = 0x7FFFFFFF;
	};

	ClipVertexData made[2];
	const ClipVertexData *tris[2][3];
	int numTris = 0;
	if (numOutside == 1) {
		const int o = outside[0] ? 0 : (outside[1] ? 1 : 2);
		const int p = reversed ? (o + 1) % 3 : (o + 2) % 3;
		const int n = reversed ? (o + 2) % 3 : (o + 1) % 3;
		nearPoint(made[0], *src[p], *src[o]);
		nearPoint(made[1], *src[n], *src[o]);
		// Keep the winding we were given.
		const int first = reversed ? 2 : 1, second = reversed ? 1 : 2;
		tris[0][0] = src[p]; tris[0][first] = &made[0]; tris[0][second] = &made[1];
		tris[1][0] = src[p]; tris[1][first] = &made[1]; tris[1][second] = src[n];
		numTris = 2;
	} else if (numOutside == 2) {
		const int i = !outside[0] ? 0 : (!outside[1] ? 1 : 2);
		const int n = (i + 1) % 3, p = (i + 2) % 3;
		nearPoint(made[0], *src[i], *src[n]);
		nearPoint(made[1], *src[i], *src[p]);
		tris[0][0] = src[i]; tris[0][1] = &made[0]; tris[0][2] = &made[1];
		numTris = 1;
	}

	for (int t = 0; t < numTris; ++t) {
		const ClipVertexData &subv0 = *tris[t][0];
		const ClipVertexData &subv1 = *tris[t][1];
		const ClipVertexData &subv2 = *tris[t][2];
		if (subv0.OutsideRange() || subv1.OutsideRange() || subv2.OutsideRange())
			continue;

		if (gstate.getShadeMode() == GE_SHADE_FLAT) {
			// So that the order of clipping doesn't matter...
			VertexData corrected2 = subv2.v;
			corrected2.color0 = provoking.v.color0;
			corrected2.color1 = provoking.v.color1;
			binner.AddTriangle(subv0.v, subv1.v, corrected2);
		} else {
			binner.AddTriangle(subv0.v, subv1.v, subv2.v);
		}
	}
}

} // namespace
