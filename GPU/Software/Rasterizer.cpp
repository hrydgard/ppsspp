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
#include "Colors.h"

extern u8* fb;
extern u8* depthbuf;

extern u32 clut[4096];

namespace Rasterizer {

static inline int orient2d(const DrawingCoords& v0, const DrawingCoords& v1, const DrawingCoords& v2)
{
	return ((int)v1.x-(int)v0.x)*((int)v2.y-(int)v0.y) - ((int)v1.y-(int)v0.y)*((int)v2.x-(int)v0.x);
}


static inline int GetPixelDataOffset(unsigned int texel_size_bits, unsigned int row_pitch_bits, unsigned int u, unsigned int v)
{
	if (!(gstate.texmode & 1))
		return v * row_pitch_bits *texel_size_bits/8 / 8 + u * texel_size_bits / 8;

	int tile_size_bits = 32;
	int tiles_in_block_horizontal = 4;
	int tiles_in_block_vertical = 8;

	int texels_per_tile = tile_size_bits / texel_size_bits;
	int tile_u = u / texels_per_tile;

	int tile_idx = (v % tiles_in_block_vertical) * (tiles_in_block_horizontal) +
	// TODO: not sure if the *texel_size_bits/8 factor is correct
					(v / tiles_in_block_vertical) * ((row_pitch_bits*texel_size_bits/8/tile_size_bits)*tiles_in_block_vertical) +
					(tile_u % tiles_in_block_horizontal) + 
					(tile_u / tiles_in_block_horizontal) * (tiles_in_block_horizontal*tiles_in_block_vertical);
	return tile_idx * tile_size_bits/8 + ((u % (tile_size_bits / texel_size_bits)));
}

static inline u32 LookupColor(unsigned int index, unsigned int level)
{
	const bool mipmapShareClut = (gstate.texmode & 0x100) == 0;
	const int clutSharingOffset = mipmapShareClut ? 0 : level * 16;

	 // TODO: No idea if these bswaps are correct
	switch (gstate.getClutPaletteFormat()) {
		case GE_TFMT_5650:
			return DecodeRGB565(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

		case GE_TFMT_5551:
			return DecodeRGBA5551(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

		case GE_TFMT_4444:
			return DecodeRGBA4444(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

		case GE_TFMT_8888:
			return DecodeRGBA8888(clut[index + clutSharingOffset]);

		default:
			ERROR_LOG(G3D, "Unsupported palette format: %x", gstate.getClutPaletteFormat());
			return 0;
	}
}

static inline u32 GetClutIndex(u32 index) {
    const u32 clutBase = gstate.getClutIndexStartPos();
    const u32 clutMask = gstate.getClutIndexMask();
    const u8 clutShift = gstate.getClutIndexShift();
    return ((index >> clutShift) & clutMask) | clutBase;
}

static inline u32 SampleNearest(int level, float s, float t)
{
	GETextureFormat texfmt = gstate.getTextureFormat();
	u32 texaddr = (gstate.texaddr[level] & 0xFFFFF0) | ((gstate.texbufwidth[level] << 8) & 0x0F000000);
	u8* srcptr = (u8*)Memory::GetPointer(texaddr); // TODO: not sure if this is the right place to load from...?

	int width = 1 << (gstate.texsize[level] & 0xf);
	int height = 1 << ((gstate.texsize[level]>>8) & 0xf);

	// Special rules for kernel textures (PPGe), TODO: Verify!
	int texbufwidth = (texaddr < PSP_GetUserMemoryBase()) ? gstate.texbufwidth[level] & 0x1FFF : gstate.texbufwidth[level] & 0x7FF;

	// TODO: Should probably check if textures are aligned properly...

	// TODO: Not sure if that through mode treatment is correct..
	unsigned int u = (gstate.isModeThrough()) ? s : s * width; // TODO: -1?
	unsigned int v = (gstate.isModeThrough()) ? t : t * height; // TODO: -1?

	// TODO: texcoord wrapping!!

	// TODO: Assert tmap.tmn == 0 (uv texture mapping mode)

	if (texfmt == GE_TFMT_4444) {
		srcptr += GetPixelDataOffset(16, texbufwidth*8, u, v);
		return DecodeRGBA4444(*(u16*)srcptr);
	} else if (texfmt == GE_TFMT_5551) {
		srcptr += GetPixelDataOffset(16, texbufwidth*8, u, v);
		return DecodeRGBA5551(*(u16*)srcptr);
	} else if (texfmt == GE_TFMT_5650) {
		srcptr += GetPixelDataOffset(16, texbufwidth*8, u, v);
		return DecodeRGB565(*(u16*)srcptr);
	} else if (texfmt == GE_TFMT_8888) {
		srcptr += GetPixelDataOffset(32, texbufwidth*8, u, v);
		return DecodeRGBA8888(*(u32*)srcptr);
	} else if (texfmt == GE_TFMT_CLUT32) {
		srcptr += GetPixelDataOffset(32, texbufwidth*8, u, v);

		u32 val = srcptr[0] + (srcptr[1] << 8) + (srcptr[2] << 16) + (srcptr[3] << 24);

		return LookupColor(GetClutIndex(val), level);
	} else if (texfmt == GE_TFMT_CLUT16) {
		srcptr += GetPixelDataOffset(16, texbufwidth*8, u, v);

		u16 val = srcptr[0] + (srcptr[1] << 8);

		return LookupColor(GetClutIndex(val), level);
	} else if (texfmt == GE_TFMT_CLUT8) {
		srcptr += GetPixelDataOffset(8, texbufwidth*8, u, v);

		u8 val = *srcptr;

		return LookupColor(GetClutIndex(val), level);
	} else if (texfmt == GE_TFMT_CLUT4) {
		srcptr += GetPixelDataOffset(4, texbufwidth*8, u, v);

		u8 val = (u & 1) ? (srcptr[0] >> 4) : (srcptr[0] & 0xF);

		return LookupColor(GetClutIndex(val), level);
	} else {
		ERROR_LOG(G3D, "Unsupported texture format: %x", texfmt);
		return 0;
	}
}

// NOTE: These likely aren't endian safe
static inline u32 GetPixelColor(int x, int y)
{
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		return DecodeRGB565(*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()]);

	case GE_FORMAT_5551:
		return DecodeRGBA5551(*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()]);

	case GE_FORMAT_4444:
		return DecodeRGBA4444(*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()]);

	case GE_FORMAT_8888:
		return *(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()];
	}
	return 0;
}

static inline void SetPixelColor(int x, int y, u32 value)
{
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()] = RGBA8888To565(value);
		break;

	case GE_FORMAT_5551:
		*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()] = RGBA8888To5551(value);
		break;

	case GE_FORMAT_4444:
		*(u16*)&fb[4*x + 4*y*gstate.FrameBufStride()] = RGBA8888To4444(value);
		break;

	case GE_FORMAT_8888:
		*(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()] = value;
		break;
	}
}

static inline u16 GetPixelDepth(int x, int y)
{
	return *(u16*)&depthbuf[2*x + 2*y*gstate.DepthBufStride()];
}

static inline void SetPixelDepth(int x, int y, u16 value)
{
	*(u16*)&depthbuf[2*x + 2*y*gstate.DepthBufStride()] = value;
}

static inline u8 GetPixelStencil(int x, int y)
{
	// TODO: Fix for other pixel formats ?
	return (((*(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()]) & 0x80000000) != 0) ? 0xFF : 0;
}

static inline void SetPixelStencil(int x, int y, u8 value)
{
	// TODO: Fix for other pixel formats ?
	*(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()] = (*(u32*)&fb[4*x + 4*y*gstate.FrameBufStride()] & ~0x80000000) | ((value&0x80)<<24);
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

	default:
		return 0;
	}
}

static inline bool IsRightSideOrFlatBottomLine(const Vec2<u10>& vertex, const Vec2<u10>& line1, const Vec2<u10>& line2)
{
	if (line1.y == line2.y) {
		// just check if vertex is above us => bottom line parallel to x-axis
		return vertex.y < line1.y;
	} else {
		// check if vertex is on our left => right side
		return vertex.x < line1.x + (line2.x - line1.x) * (vertex.y - line1.y) / (line2.y - line1.y);
	}
}

static inline void ApplyStencilOp(int op, int x, int y)
{
	u8 old_stencil = GetPixelStencil(x, y); // TODO: Apply mask?
	u8 reference_stencil = gstate.getStencilTestRef(); // TODO: Apply mask?

	switch (op) {
		case GE_STENCILOP_KEEP:
			return;

		case GE_STENCILOP_ZERO:
			SetPixelStencil(x, y, 0);
			return;

		case GE_STENCILOP_REPLACE:
			SetPixelStencil(x, y, reference_stencil);
			break;

		case GE_STENCILOP_INVERT:
			SetPixelStencil(x, y, ~old_stencil);
			break;

		case GE_STENCILOP_INCR:
			// TODO: Does this overflow?
			SetPixelStencil(x, y, old_stencil+1);
			break;

		case GE_STENCILOP_DECR:
			// TODO: Does this underflow?
			SetPixelStencil(x, y, old_stencil-1);
			break;
	}
}

static inline Vec4<int> GetTextureFunctionOutput(const Vec3<int>& prim_color_rgb, int prim_color_a, float s, float t)
{
	Vec4<int> texcolor = Vec4<int>::FromRGBA(/*TextureDecoder::*/SampleNearest(0, s, t));
	Vec3<int> out_rgb;
	int out_a;

	bool rgba = (gstate.texfunc & 0x100) != 0;

	switch (gstate.getTextureFunction()) {
	case GE_TEXFUNC_MODULATE:
		out_rgb = prim_color_rgb * texcolor.rgb() / 255;
		out_a = (rgba) ? (prim_color_a * texcolor.a() / 255) : prim_color_a;
		break;

	case GE_TEXFUNC_DECAL:
	{
		int t = (rgba) ? texcolor.a() : 255;
		int invt = (rgba) ? 255 - t : 0;
		out_rgb = (invt * prim_color_rgb + t * texcolor.rgb()) / 255;
		out_a = prim_color_a;
		break;
	}

	case GE_TEXFUNC_BLEND:
	{
		const Vec3<int> const255(255, 255, 255);
		const Vec3<int> texenv(gstate.getTextureEnvColR(), gstate.getTextureEnvColG(), gstate.getTextureEnvColB());
		out_rgb = ((const255 - texcolor.rgb()) * prim_color_rgb + texcolor.rgb() * texenv) / 255;
		out_a = prim_color_a * ((rgba) ? texcolor.a() : 255) / 255;
		break;
	}

	case GE_TEXFUNC_REPLACE:
		out_rgb = texcolor.rgb();
		out_a = (rgba) ? texcolor.a() : prim_color_a;
		break;

	case GE_TEXFUNC_ADD:
		out_rgb = prim_color_rgb + texcolor.rgb();
		if (out_rgb.r() > 255) out_rgb.r() = 255;
		if (out_rgb.g() > 255) out_rgb.g() = 255;
		if (out_rgb.b() > 255) out_rgb.b() = 255;
		out_a = prim_color_a * ((rgba) ? texcolor.a() : 255) / 255;
		break;

	default:
		ERROR_LOG(G3D, "Unknown texture function %x", gstate.getTextureFunction());
	}

	return Vec4<int>(out_rgb.r(), out_rgb.g(), out_rgb.b(), out_a);
}
// Draws triangle, vertices specified in counter-clockwise direction (TODO: Make sure this is actually enforced)
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

	int bias0 = IsRightSideOrFlatBottomLine(v0.drawpos.xy(), v1.drawpos.xy(), v2.drawpos.xy()) ? -1 : 0;
	int bias1 = IsRightSideOrFlatBottomLine(v1.drawpos.xy(), v2.drawpos.xy(), v0.drawpos.xy()) ? -1 : 0;
	int bias2 = IsRightSideOrFlatBottomLine(v2.drawpos.xy(), v0.drawpos.xy(), v1.drawpos.xy()) ? -1 : 0;

	DrawingCoords p(minX, minY, 0);
	for (p.y = minY; p.y <= maxY; ++p.y) {
		for (p.x = minX; p.x <= maxX; ++p.x) {
			int w0 = orient2d(v1.drawpos, v2.drawpos, p) + bias0;
			int w1 = orient2d(v2.drawpos, v0.drawpos, p) + bias1;
			int w2 = orient2d(v0.drawpos, v1.drawpos, p) + bias2;

			// If p is on or inside all edges, render pixel
			// TODO: Should only render when it's on the left of the right edge
			if (w0 >=0 && w1 >= 0 && w2 >= 0) {
				if (w0 == w1 && w1 == w2 && w2 == 0)
					continue;

				// TODO: Make sure this is not ridiculously small?
				float den = 1.0f/v0.clippos.w * w0 + 1.0f/v1.clippos.w * w1 + 1.0f/v2.clippos.w * w2;

				// TODO: Depth range test

				float s = (v0.texturecoords.s() * w0 / v0.clippos.w + v1.texturecoords.s() * w1 / v1.clippos.w + v2.texturecoords.s() * w2 / v2.clippos.w) / den;
				float t = (v0.texturecoords.t() * w0 / v0.clippos.w + v1.texturecoords.t() * w1 / v1.clippos.w + v2.texturecoords.t() * w2 / v2.clippos.w) / den;

				Vec3<int> prim_color_rgb(0, 0, 0);
				int prim_color_a = 0;
				Vec3<int> sec_color(0, 0, 0);
				if ((gstate.shademodel&1) == GE_SHADE_GOURAUD) {
					// NOTE: When not casting color0 and color1 to float vectors, this code suffers from severe overflow issues.
					// Not sure if that should be regarded as a bug or if casting to float is a valid fix.
					// TODO: Is that the correct way to interpolate?
					prim_color_rgb = ((v0.color0.rgb().Cast<float>() * w0 +
									v1.color0.rgb().Cast<float>() * w1 +
									v2.color0.rgb().Cast<float>() * w2) / (w0+w1+w2)).Cast<int>();
					prim_color_a = (int)((v0.color0.a() * w0 + v1.color0.a() * w1 + v2.color0.a() * w2) / (w0+w1+w2));
					sec_color = ((v0.color1.Cast<float>() * w0 +
									v1.color1.Cast<float>() * w1 +
									v2.color1.Cast<float>() * w2) / (w0+w1+w2)).Cast<int>();
				} else {
					prim_color_rgb = v2.color0.rgb();
					prim_color_a = v2.color0.a();
					sec_color = v2.color1;
				}

				// TODO: Also disable if vertex has no texture coordinates?
				if (gstate.isTextureMapEnabled() && !gstate.isModeClear()) {
					Vec4<int> out = GetTextureFunctionOutput(prim_color_rgb, prim_color_a, s, t);
					prim_color_rgb = out.rgb();
					prim_color_a = out.a();
				}

				if (gstate.isColorDoublingEnabled()) {
					// TODO: Do we need to clamp here?
					prim_color_rgb *= 2;
					sec_color *= 2;
				}

				prim_color_rgb += sec_color;

				// TODO: Fogging

				if (gstate.isColorTestEnabled()) {
					bool pass = false;
					Vec3<int> ref = Vec3<int>::FromRGB(gstate.colorref&(gstate.colormask&0xFFFFFF));
					Vec3<int> color = Vec3<int>::FromRGB(prim_color_rgb.ToRGB()&(gstate.colormask&0xFFFFFF));
					switch (gstate.colortest & 0x3) {
						case GE_COMP_NEVER:
							pass = false;
							break;
						case GE_COMP_ALWAYS:
							pass = true;
							break;
						case GE_COMP_EQUAL:
							pass = (color.r() == ref.r() && color.g() == ref.g() && color.b() == ref.b());
							break;
						case GE_COMP_NOTEQUAL:
							pass = (color.r() != ref.r() || color.g() != ref.g() || color.b() != ref.b());
							break;
					}
					if (!pass)
						continue;
				}

				if (gstate.isAlphaTestEnabled()) {
					bool pass = false;
					u8 ref = (gstate.alphatest>>8) & (gstate.alphatest>>16);
					u8 alpha = prim_color_a & (gstate.alphatest>>16);

					switch (gstate.alphatest & 0x7) {
						case GE_COMP_NEVER:
							pass = false;
							break;
						case GE_COMP_ALWAYS:
							pass = true;
							break;
						case GE_COMP_EQUAL:
							pass = (alpha == ref);
							break;
						case GE_COMP_NOTEQUAL:
							pass = (alpha != ref);
							break;
						case GE_COMP_LESS:
							pass = (alpha < ref);
							break;
						case GE_COMP_LEQUAL:
							pass = (alpha <= ref);
							break;
						case GE_COMP_GREATER:
							pass = (alpha > ref);
							break;
						case GE_COMP_GEQUAL:
							pass = (alpha >= ref);
							break;
					}
					if (!pass)
						continue;
				}

				if (gstate.isStencilTestEnabled() && !gstate.isModeClear()) {
					bool pass = false;
					u8 stencil = GetPixelStencil(p.x, p.y) & gstate.getStencilTestMask(); // TODO: Magic?
					u8 ref = gstate.getStencilTestRef() & gstate.getStencilTestMask();
					switch (gstate.getStencilTestFunction()) {
						case GE_COMP_NEVER:
							pass = false;
							break;
						case GE_COMP_ALWAYS:
							pass = true;
							break;
						case GE_COMP_EQUAL:
							pass = (stencil == ref);
							break;
						case GE_COMP_NOTEQUAL:
							pass = (stencil != ref);
							break;
						case GE_COMP_LESS:
							pass = (stencil < ref);
							break;
						case GE_COMP_LEQUAL:
							pass = (stencil <= ref);
							break;
						case GE_COMP_GREATER:
							pass = (stencil > ref);
							break;
						case GE_COMP_GEQUAL:
							pass = (stencil >= ref);
							break;
					}

					if (!pass) {
						ApplyStencilOp(gstate.getStencilOpSFail(), p.x, p.y);
						continue;
					}
				}

				// TODO: Is it safe to ignore gstate.isDepthTestEnabled() when clear mode is enabled?
				if ((gstate.isDepthTestEnabled() && !gstate.isModeThrough()) || gstate.isModeClear()) {
					// TODO: Is that the correct way to interpolate?
					u16 z = (u16)((v0.drawpos.z * w0 + v1.drawpos.z * w1 + v2.drawpos.z * w2) / (w0+w1+w2));

					// TODO: Verify that stencil op indeed needs to be applied here even if stencil testing is disabled
					if (!DepthTestPassed(p.x, p.y, z)) {
						ApplyStencilOp(gstate.getStencilOpZFail(), p.x, p.y);
						continue;
					} else {
						ApplyStencilOp(gstate.getStencilOpZPass(), p.x, p.y);
					}
					// TODO: Is this condition correct?
					if (gstate.isDepthWriteEnabled() || ((gstate.clearmode&0x40) && gstate.isModeClear()))
						SetPixelDepth(p.x, p.y, z);
				}

				Vec4<int> dst = Vec4<int>::FromRGBA(GetPixelColor(p.x, p.y));
				if (gstate.isAlphaBlendEnabled() && !gstate.isModeClear()) {

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
						// TODO: Clamping?
						srccol = Vec3<int>::AssignToAll(255 - 2 * dst.a());
						break;
					case GE_SRCBLEND_FIXA:
						srccol = Vec4<int>::FromRGBA(gstate.getFixA()).rgb();
						break;
					}

					switch (gstate.getBlendFuncB()) {
					case GE_DSTBLEND_SRCCOLOR:
						dstcol = prim_color_rgb;
						break;
					case GE_DSTBLEND_INVSRCCOLOR:
						dstcol = Vec3<int>::AssignToAll(255) - prim_color_rgb;
						break;
					case GE_DSTBLEND_SRCALPHA:
						dstcol = Vec3<int>::AssignToAll(prim_color_a);
						break;
					case GE_DSTBLEND_INVSRCALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - prim_color_a);
						break;
					case GE_DSTBLEND_DSTALPHA:
						dstcol = Vec3<int>::AssignToAll(dst.a());
						break;
					case GE_DSTBLEND_INVDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - dst.a());
						break;
					case GE_DSTBLEND_DOUBLESRCALPHA:
						dstcol = Vec3<int>::AssignToAll(2 * prim_color_a);
						break;
					case GE_DSTBLEND_DOUBLEINVSRCALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - 2 * prim_color_a);
						break;
					case GE_DSTBLEND_DOUBLEDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(2 * dst.a());
						break;
					case GE_DSTBLEND_DOUBLEINVDSTALPHA:
						dstcol = Vec3<int>::AssignToAll(255 - 2 * dst.a());
						break;
					case GE_DSTBLEND_FIXB:
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
				if (prim_color_rgb.r() > 255) prim_color_rgb.r() = 255;
				if (prim_color_rgb.g() > 255) prim_color_rgb.g() = 255;
				if (prim_color_rgb.b() > 255) prim_color_rgb.b() = 255;
				if (prim_color_a > 255) prim_color_a = 255;
				if (prim_color_rgb.r() < 0) prim_color_rgb.r() = 0;
				if (prim_color_rgb.g() < 0) prim_color_rgb.g() = 0;
				if (prim_color_rgb.b() < 0) prim_color_rgb.b() = 0;
				if (prim_color_a < 0) prim_color_a = 0;

				u32 new_color = Vec4<int>(prim_color_rgb.r(), prim_color_rgb.g(), prim_color_rgb.b(), prim_color_a).ToRGBA();
				u32 old_color = GetPixelColor(p.x, p.y);
				new_color = (new_color & ~gstate.getColorMask()) | (old_color & gstate.getColorMask());

				SetPixelColor(p.x, p.y, new_color);
			}
		}
	}
}

} // namespace
