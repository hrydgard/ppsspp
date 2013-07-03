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

void DrawTriangle(VertexData vertexdata[3])
{
	DrawingCoords vertices[3] = { vertexdata[0].drawpos, vertexdata[1].drawpos, vertexdata[2].drawpos };

	int minX = std::min(std::min(vertices[0].x, vertices[1].x), vertices[2].x);
	int minY = std::min(std::min(vertices[0].y, vertices[1].y), vertices[2].y);
	int maxX = std::max(std::max(vertices[0].x, vertices[1].x), vertices[2].x);
	int maxY = std::max(std::max(vertices[0].y, vertices[1].y), vertices[2].y);

	minX = std::max(minX, gstate.getScissorX1());
	maxX = std::min(maxX, gstate.getScissorX2());
	minY = std::max(minY, gstate.getScissorY1());
	maxY = std::min(maxY, gstate.getScissorY2());

	DrawingCoords p(minX, minY, 0);
	for (p.y = minY; p.y <= maxY; ++p.y) {
		for (p.x = minX; p.x <= maxX; ++p.x) {
			int w0 = orient2d(vertices[1], vertices[2], p);
			int w1 = orient2d(vertices[2], vertices[0], p);
			int w2 = orient2d(vertices[0], vertices[1], p);

			// If p is on or inside all edges, render pixel
			// TODO: Should only render when it's on the left of the right edge
			if (w0 >=0 && w1 >= 0 && w2 >= 0) {
				// TODO: Make sure this is not ridiculously small?
				float den = 1.0f/vertexdata[0].clippos.w * w0 + 1.0f/vertexdata[1].clippos.w * w1 + 1.0f/vertexdata[2].clippos.w * w2;

				// TODO: Depth range test

				// TODO: Is it safe to ignore gstate.isDepthTestEnabled() when clear mode is enabled?
				if ((gstate.isDepthTestEnabled() && !gstate.isModeThrough()) || gstate.isModeClear()) {
					u16 z = (u16)((vertexdata[0].drawpos.z * w0 / vertexdata[0].clippos.w + vertexdata[1].drawpos.z * w1 / vertexdata[1].clippos.w + vertexdata[2].drawpos.z * w2 / vertexdata[2].clippos.w) / den);

					if (!DepthTestPassed(p.x, p.y, z))
						continue;

					// TODO: Is this condition correct?
					if (gstate.isDepthWriteEnabled() || ((gstate.clearmode&0x40) && gstate.isModeClear()))
						SetPixelDepth(p.x, p.y, z);
				}

				float s = (vertexdata[0].texturecoords.s() * w0 / vertexdata[0].clippos.w + vertexdata[1].texturecoords.s() * w1 / vertexdata[1].clippos.w + vertexdata[2].texturecoords.s() * w2 / vertexdata[2].clippos.w) / den;
				float t = (vertexdata[0].texturecoords.t() * w0 / vertexdata[0].clippos.w + vertexdata[1].texturecoords.t() * w1 / vertexdata[1].clippos.w + vertexdata[2].texturecoords.t() * w2 / vertexdata[2].clippos.w) / den;
				Vec3<int> prim_color_rgb(0, 0, 0);
				int prim_color_a = 0;
				Vec3<int> sec_color(0, 0, 0);
				if ((gstate.shademodel&1) == GE_SHADE_GOURAUD) {
					prim_color_rgb.r() = (int)((vertexdata[0].color0.r() * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.r() * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.r() * w2 / vertexdata[2].clippos.w) / den);
					prim_color_rgb.g() = (int)((vertexdata[0].color0.g() * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.g() * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.g() * w2 / vertexdata[2].clippos.w) / den);
					prim_color_rgb.b() = (int)((vertexdata[0].color0.b() * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.b() * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.b() * w2 / vertexdata[2].clippos.w) / den);
					prim_color_a = (int)((vertexdata[0].color0.a() * w0 / vertexdata[0].clippos.w + vertexdata[1].color0.a() * w1 / vertexdata[1].clippos.w + vertexdata[2].color0.a() * w2 / vertexdata[2].clippos.w) / den);
					sec_color.r() = (int)((vertexdata[0].color1.r() * w0 / vertexdata[0].clippos.w + vertexdata[1].color1.r() * w1 / vertexdata[1].clippos.w + vertexdata[2].color1.r() * w2 / vertexdata[2].clippos.w) / den);
					sec_color.g() = (int)((vertexdata[0].color1.g() * w0 / vertexdata[0].clippos.w + vertexdata[1].color1.g() * w1 / vertexdata[1].clippos.w + vertexdata[2].color1.g() * w2 / vertexdata[2].clippos.w) / den);
					sec_color.b() = (int)((vertexdata[0].color1.b() * w0 / vertexdata[0].clippos.w + vertexdata[1].color1.b() * w1 / vertexdata[1].clippos.w + vertexdata[2].color1.b() * w2 / vertexdata[2].clippos.w) / den);
				} else {
					prim_color_rgb.r() = vertexdata[2].color0.r();
					prim_color_rgb.g() = vertexdata[2].color0.g();
					prim_color_rgb.b() = vertexdata[2].color0.b();
					prim_color_a = vertexdata[2].color0.a();
					sec_color.r() = vertexdata[2].color1.r();
					sec_color.g() = vertexdata[2].color1.g();
					sec_color.b() = vertexdata[2].color1.b();
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

				// TODO: Finish alpha blending support
//				if (!gstate.isAlphaBlendEnabled())
					SetPixelColor(p.x, p.y, Vec4<int>(prim_color_rgb.r(), prim_color_rgb.g(), prim_color_rgb.b(), prim_color_a).ToRGBA());
/*				else {
					u32 dst = GetPixelColor(p.x, p.y);
					u32 A, B;
					SET_R(A, GET_A(prim_color));
					SET_G(A, GET_A(prim_color));
					SET_B(A, GET_A(prim_color));
					SET_A(A, GET_A(prim_color));
					SET_R(B, 255 - GET_A(prim_color));
					SET_G(B, 255 - GET_A(prim_color));
					SET_B(B, 255 - GET_A(prim_color));
					SET_A(B, 255 - GET_A(prim_color));
					SET_R(prim_color, (GET_R(prim_color)*GET_R(A)+GET_R(dst)*GET_R(B))/255);
					SET_G(prim_color, (GET_G(prim_color)*GET_G(A)+GET_G(dst)*GET_G(B))/255);
					SET_B(prim_color, (GET_B(prim_color)*GET_B(A)+GET_B(dst)*GET_B(B))/255);
					SET_A(prim_color, (GET_A(prim_color)*GET_A(A)+GET_A(dst)*GET_A(B))/255);
					SetPixelColor(p.x, p.y, prim_color.Compactify());
				}*/
			}
		}
	}
}

} // namespace
