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

#include "../../Core/MemMap.h"
#include "../GPUState.h"

#include "Rasterizer.h"

extern u8* fb;
extern u8* depthbuf;

namespace Rasterizer {

static int orient2d(const DrawingCoords& v0, const DrawingCoords& v1, const DrawingCoords& v2)
{
	return ((int)v1.x-(int)v0.x)*((int)v2.y-(int)v0.y) - ((int)v1.y-(int)v0.y)*((int)v2.x-(int)v0.x);
}

u32 SampleNearest(int level, float s, float t)
{
	int texfmt = gstate.texformat & 0xF;
	u32 texaddr = (gstate.texaddr[level] & 0xFFFFF0) | ((gstate.texbufwidth[level] << 8) & 0x0F000000);
	u8* srcptr = (u8*)Memory::GetPointer(texaddr); // TODO: not sure if this is the right place to load from...?

	int width = 1 << (gstate.texsize[level] & 0xf);
	int height = 1 << ((gstate.texsize[level]>>8) & 0xf);

	// TODO: Not sure if that through mode treatment is correct..
	int u = (gstate.isModeThrough()) ? s : s * width; // TODO: -1?
	int v = (gstate.isModeThrough()) ? t : t * height; // TODO: -1?

	// TODO: Assert tmode.hsm == 0 (normal storage mode)
	// TODO: Assert tmap.tmn == 0 (uv texture mapping mode)

	if (texfmt == GE_TFMT_4444) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) >> 4;
		u8 g = (*srcptr) & 0xF;
		u8 b = (*(srcptr+1)) >> 4;
		u8 a = (*(srcptr+1)) & 0xF;
		r = (r << 4) | r;
		g = (g << 4) | g;
		b = (b << 4) | b;
		a = (a << 4) | a;
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_5551) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) & 0x1F;
		u8 g = (((*srcptr) & 0xE0) >> 5) | (((*(srcptr+1))&0x3) << 3);
		u8 b = ((*srcptr+1) & 0x7C) >> 2;
		u8 a = (*(srcptr+1)) >> 7;
		r = (r << 3) | (r >> 2);
		g = (g << 3) | (g >> 2);
		b = (b << 3) | (b >> 2);
		a = (a) ? 0xff : 0;
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_5650) {
		srcptr += 2 * v * width + 2 * u;
		u8 r = (*srcptr) & 0x1F;
		u8 g = (((*srcptr) & 0xE0) >> 5) | (((*(srcptr+1))&0x7) << 3);
		u8 b = ((*srcptr+1) & 0xF8) >> 3;
		u8 a = 0xff;
		r = (r << 3) | (r >> 2);
		g = (g << 2) | (g >> 4);
		b = (b << 3) | (b >> 2);
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else if (texfmt == GE_TFMT_8888) {
		srcptr += 4 * v * width + 4 * u;
		u8 r = *srcptr++;
		u8 g = *srcptr++;
		u8 b = *srcptr++;
		u8 a = *srcptr++;
		return (r << 24) | (g << 16) | (b << 8) | a;
	} else {
		ERROR_LOG(G3D, "Unsupported texture format: %x", texfmt);
	}
}

static inline u32 GetPixelColor(int x, int y)
{
	return *(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()];
}

static inline void SetPixelColor(int x, int y, u32 value)
{
	*(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()] = value;
}

static inline u16 GetPixelDepth(int x, int y)
{
	return *(u16*)&depthbuf[2*x + 2*y*gstate.DepthBufStride()];
}

static inline void SetPixelDepth(int x, int y, u16 value)
{
	*(u16*)&depthbuf[2*x + 2*y*gstate.DepthBufStride()] = value;
}

static inline bool DepthTestPassed(int x, int y, u16 z)
{
	u16 reference_z = GetPixelDepth(x, y);

	if (gstate.isModeClear())
		return true;

	switch (gstate.getDepthTestFunc()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return (z == reference_z);

	case GE_COMP_NOTEQUAL:
		return (z != reference_z);

	case GE_COMP_LESS:
		return (z < reference_z);

	case GE_COMP_LEQUAL:
		return (z <= reference_z);

	case GE_COMP_GREATER:
		return (z > reference_z);

	case GE_COMP_GEQUAL:
		return (z >= reference_z);
	}
}

void DrawTriangle(const VertexData& v0, const VertexData& v1, const VertexData& v2)
{
	int minX = std::min(std::min(v0.drawpos.x, v1.drawpos.x), v2.drawpos.x);
	int minY = std::min(std::min(v0.drawpos.y, v1.drawpos.y), v2.drawpos.y);
	int maxX = std::max(std::max(v0.drawpos.x, v1.drawpos.x), v2.drawpos.x);
	int maxY = std::max(std::max(v0.drawpos.y, v1.drawpos.y), v2.drawpos.y);

	minX = std::max(minX, gstate.getScissorX1());
	maxX = std::min(maxX, gstate.getScissorX2());
	minY = std::max(minY, gstate.getScissorY1());
	maxY = std::min(maxY, gstate.getScissorY2());

	DrawingCoords p(minX, minY, 0);
	for (p.y = minY; p.y <= maxY; ++p.y) {
		for (p.x = minX; p.x <= maxX; ++p.x) {
			int w0 = orient2d(v1.drawpos, v2.drawpos, p);
			int w1 = orient2d(v2.drawpos, v0.drawpos, p);
			int w2 = orient2d(v0.drawpos, v1.drawpos, p);

			// If p is on or inside all edges, render pixel
			// TODO: Should only render when it's on the left of the right edge
			if (w0 >=0 && w1 >= 0 && w2 >= 0) {
				// TODO: Make sure this is not ridiculously small?
				float den = 1.0f/v0.clippos.w * w0 + 1.0f/v1.clippos.w * w1 + 1.0f/v2.clippos.w * w2;

				// TODO: Depth range test

				// TODO: Is it safe to ignore gstate.isDepthTestEnabled() when clear mode is enabled?
				if ((gstate.isDepthTestEnabled() && !gstate.isModeThrough()) || gstate.isModeClear()) {
					u16 z = (u16)((v0.drawpos.z * w0 / v0.clippos.w + v1.drawpos.z * w1 / v1.clippos.w + v2.drawpos.z * w2 / v2.clippos.w) / den);

					if (!DepthTestPassed(p.x, p.y, z))
						continue;

					// TODO: Is this condition correct?
					if (gstate.isDepthWriteEnabled() || ((gstate.clearmode&0x40) && gstate.isModeClear()))
						SetPixelDepth(p.x, p.y, z);
				}

				float s = (v0.texturecoords.s() * w0 / v0.clippos.w + v1.texturecoords.s() * w1 / v1.clippos.w + v2.texturecoords.s() * w2 / v2.clippos.w) / den;
				float t = (v0.texturecoords.t() * w0 / v0.clippos.w + v1.texturecoords.t() * w1 / v1.clippos.w + v2.texturecoords.t() * w2 / v2.clippos.w) / den;
				Vec3<int> prim_color_rgb(0, 0, 0);
				int prim_color_a = 0;
				Vec3<int> sec_color(0, 0, 0);
				if ((gstate.shademodel&1) == GE_SHADE_GOURAUD) {
					prim_color_rgb = ((v0.color0.rgb() * w0 / v0.clippos.w + v1.color0.rgb() * w1 / v1.clippos.w + v2.color0.rgb() * w2 / v2.clippos.w) / den).Cast<int>();
					prim_color_a = (int)((v0.color0.a() * w0 / v0.clippos.w + v1.color0.a() * w1 / v1.clippos.w + v2.color0.a() * w2 / v2.clippos.w) / den);
					sec_color = ((v0.color1 * w0 / v0.clippos.w + v1.color1 * w1 / v1.clippos.w + v2.color1 * w2 / v2.clippos.w) / den).Cast<int>();
				} else {
					prim_color_rgb = v2.color0.rgb();
					prim_color_a = v2.color0.a();
					sec_color = v2.color1;
				}

				// TODO: Also disable if vertex has no texture coordinates?

				if (gstate.isTextureMapEnabled() && !gstate.isModeClear()) {
					Vec4<int> texcolor = Vec4<int>::FromRGBA(/*TextureDecoder::*/SampleNearest(0, s, t));
					u32 mycolor = (/*TextureDecoder::*/SampleNearest(0, s, t));

					bool rgba = (gstate.texfunc & 0x10) != 0;

					// texture function
					switch (gstate.getTextureFunction()) {
					case GE_TEXFUNC_MODULATE:
						prim_color_rgb = prim_color_rgb * texcolor.rgb() / 255;
						prim_color_a = (rgba) ? (prim_color_a * texcolor.a() / 255) : prim_color_a;
						break;

					case GE_TEXFUNC_DECAL:
					{
						int t = (rgba) ? texcolor.a() : 1;
						int invt = (rgba) ? 255 - t : 0;
						prim_color_rgb = (invt * prim_color_rgb + t * texcolor.rgb()) / 255;
						// prim_color_a = prim_color_a;
						break;
					}

					case GE_TEXFUNC_BLEND:
					{
						const Vec3<int> const255(255, 255, 255);
						const Vec3<int> texenv(gstate.getTextureEnvColR(), gstate.getTextureEnvColG(), gstate.getTextureEnvColB());
						prim_color_rgb = ((const255 - texcolor.rgb()) * prim_color_rgb + texcolor.rgb() * texenv) / 255;
						prim_color_a = prim_color_a * ((rgba) ? texcolor.a() : 255) / 255;
						break;
					}

					case GE_TEXFUNC_REPLACE:
						prim_color_rgb = texcolor.rgb();
						prim_color_a = (rgba) ? texcolor.a() : prim_color_a;
						break;

					case GE_TEXFUNC_ADD:
						prim_color_rgb += texcolor.rgb();
						if (prim_color_rgb.r() > 255) prim_color_rgb.r() = 255;
						if (prim_color_rgb.g() > 255) prim_color_rgb.g() = 255;
						if (prim_color_rgb.b() > 255) prim_color_rgb.b() = 255;
						prim_color_a = prim_color_a * ((rgba) ? texcolor.a() : 255) / 255;
						break;

					default:
						ERROR_LOG(G3D, "Unknown texture function %x", gstate.getTextureFunction());
					}
				}

				if (gstate.isColorDoublingEnabled()) {
					// TODO: Do we need to clamp here?
					prim_color_rgb *= 2;
					sec_color *= 2;
				}

				prim_color_rgb += sec_color;
				if (prim_color_rgb.r() > 255) prim_color_rgb.r() = 255;
				if (prim_color_rgb.g() > 255) prim_color_rgb.g() = 255;
				if (prim_color_rgb.b() > 255) prim_color_rgb.b() = 255;
				if (prim_color_rgb.r() < 0) prim_color_rgb.r() = 0;
				if (prim_color_rgb.g() < 0) prim_color_rgb.g() = 0;
				if (prim_color_rgb.b() < 0) prim_color_rgb.b() = 0;

				// TODO: Fogging

				if (gstate.isAlphaBlendEnabled()) {
					Vec4<int> dst = Vec4<int>::FromRGBA(GetPixelColor(p.x, p.y));

					Vec3<int> srccol(0, 0, 0);
					Vec3<int> dstcol(0, 0, 0);

					switch (gstate.getBlendFuncA()) {
					case GE_SRCBLEND_DSTCOLOR:
						srccol = dst.rgb();
						break;
					case GE_SRCBLEND_INVDSTCOLOR:
						srccol = Vec3<int>::AssignToAll(255) - dst.rgb();
						break;
					case GE_SRCBLEND_SRCALPHA:
						srccol = Vec3<int>::AssignToAll(prim_color_a);
						break;
					case GE_SRCBLEND_INVSRCALPHA:
						srccol = Vec3<int>::AssignToAll(255 - prim_color_a);
						break;
					case GE_SRCBLEND_DSTALPHA:
						srccol = Vec3<int>::AssignToAll(dst.a());
						break;
					case GE_SRCBLEND_INVDSTALPHA:
						srccol = Vec3<int>::AssignToAll(255 - dst.a());
						break;
					case GE_SRCBLEND_DOUBLESRCALPHA:
						srccol = Vec3<int>::AssignToAll(2 * prim_color_a);
						break;
					case GE_SRCBLEND_DOUBLEINVSRCALPHA:
						srccol = Vec3<int>::AssignToAll(255 - 2 * prim_color_a);
						break;
					case GE_SRCBLEND_DOUBLEDSTALPHA:
						srccol = Vec3<int>::AssignToAll(2 * dst.a());
						break;
					case GE_SRCBLEND_DOUBLEINVDSTALPHA:
						srccol = Vec3<int>::AssignToAll(255 - 2 * dst.a());
						break;
					case GE_SRCBLEND_FIXA:
						srccol = Vec4<int>::FromRGBA(gstate.getFixA()).rgb();
						break;
					}

					switch (gstate.getBlendFuncB()) {
					GE_DSTBLEND_SRCCOLOR:
						dstcol = prim_color_rgb;
						break;
					GE_DSTBLEND_INVSRCCOLOR:
						dstcol = Vec3<int>::AssignToAll(255) - prim_color_rgb;
						break;
					GE_DSTBLEND_SRCALPHA:
						dstcol = Vec3<int>::AssignToAll(prim_color_a);
						break;
					GE_DSTBLEND_INVSRCALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - prim_color_a);
						break;
					GE_DSTBLEND_DSTALPHA:
						dstcol = Vec3<int>::AssignToAll(dst.a());
						break;
					GE_DSTBLEND_INVDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - dst.a());
						break;
					GE_DSTBLEND_DOUBLESRCALPHA:
						dstcol = Vec3<int>::AssignToAll(2 * prim_color_a);
						break;
					GE_DSTBLEND_DOUBLEINVSRCALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - 2 * prim_color_a);
						break;
					GE_DSTBLEND_DOUBLEDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(2 * dst.a());
						break;
					GE_DSTBLEND_DOUBLEINVDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - 2 * dst.a());
						break;
					GE_DSTBLEND_FIXB:
						dstcol = Vec4<int>::FromRGBA(gstate.getFixB()).rgb();
						break;
					}

					switch (gstate.getBlendEq()) {
					case GE_BLENDMODE_MUL_AND_ADD:
						prim_color_rgb = (prim_color_rgb * srccol + dst.rgb() * dstcol) / 255;
						break;
					case GE_BLENDMODE_MUL_AND_SUBTRACT:
						prim_color_rgb = (prim_color_rgb * srccol - dst.rgb() * dstcol) / 255;
						break;
					case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
						prim_color_rgb = (dst.rgb() * dstcol - prim_color_rgb * srccol) / 255;
						break;
					case GE_BLENDMODE_MIN:
						prim_color_rgb.r() = std::min(prim_color_rgb.r(), dst.r());
						prim_color_rgb.g() = std::min(prim_color_rgb.g(), dst.g());
						prim_color_rgb.b() = std::min(prim_color_rgb.b(), dst.b());
						break;
					case GE_BLENDMODE_MAX:
						prim_color_rgb.r() = std::max(prim_color_rgb.r(), dst.r());
						prim_color_rgb.g() = std::max(prim_color_rgb.g(), dst.g());
						prim_color_rgb.b() = std::max(prim_color_rgb.b(), dst.b());
						break;
					case GE_BLENDMODE_ABSDIFF:
						prim_color_rgb.r() = ::abs(prim_color_rgb.r() - dst.r());
						prim_color_rgb.g() = ::abs(prim_color_rgb.g() - dst.g());
						prim_color_rgb.b() = ::abs(prim_color_rgb.b() - dst.b());
						break;
					}
				}
				SetPixelColor(p.x, p.y, Vec4<int>(prim_color_rgb.r(), prim_color_rgb.g(), prim_color_rgb.b(), prim_color_a).ToRGBA());
			}
		}
	}
}

} // namespace
