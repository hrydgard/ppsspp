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

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Colors.h"

#include <algorithm>

extern FormatBuffer fb;
extern FormatBuffer depthbuf;

extern u32 clut[4096];

namespace Rasterizer {

//static inline int orient2d(const DrawingCoords& v0, const DrawingCoords& v1, const DrawingCoords& v2)
static inline int orient2d(const ScreenCoords& v0, const ScreenCoords& v1, const ScreenCoords& v2)
{
	return ((int)v1.x-(int)v0.x)*((int)v2.y-(int)v0.y) - ((int)v1.y-(int)v0.y)*((int)v2.x-(int)v0.x);
}

static inline int orient2dIncX(int dY01)
{
	return dY01;
}

static inline int orient2dIncY(int dX01)
{
	return -dX01;
}

static inline int GetPixelDataOffset(unsigned int texel_size_bits, unsigned int row_pitch_bits, unsigned int u, unsigned int v)
{
	if (!gstate.isTextureSwizzled())
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

	// TODO: HACK: for some reason, the second part needs to be diviced by two for CLUT4 textures to work properly.
	return tile_idx * tile_size_bits/8 + ((u % (tile_size_bits / texel_size_bits)))/((texel_size_bits == 4) ? 2 : 1);
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
		ERROR_LOG_REPORT(G3D, "Software: Unsupported palette format: %x", gstate.getClutPaletteFormat());
		return 0;
	}
}

static inline void GetTexelCoordinates(int level, float s, float t, unsigned int& u, unsigned int& v)
{
	s *= getFloat24(gstate.texscaleu);
	t *= getFloat24(gstate.texscalev);

	s += getFloat24(gstate.texoffsetu);
	t += getFloat24(gstate.texoffsetv);

	// TODO: Is this really only necessary for UV mapping?
	if (gstate.isTexCoordClampedS()) {
		if (s > 1.0) s = 1.0;
		if (s < 0) s = 0;
	} else {
		// Subtracting floor works for negative and positive to discard the non-fractional part.
		s -= floor(s);
	}
	if (gstate.isTexCoordClampedT()) {
		if (t > 1.0) t = 1.0;
		if (t < 0.0) t = 0.0;
	} else {
		// Subtracting floor works for negative and positive to discard the non-fractional part.
		t -= floor(t);
	}

	int width = 1 << (gstate.texsize[level] & 0xf);
	int height = 1 << ((gstate.texsize[level]>>8) & 0xf);

	// TODO: These should really be multiplied by 256 to get fixed point coordinates
	// so we can do texture filtering later.

	u = (unsigned int)(s * width); // TODO: width-1 instead?
	v = (unsigned int)(t * height); // TODO: width-1 instead?
}

static inline void GetTexelCoordinatesThrough(int level, float s, float t, unsigned int& u, unsigned int& v)
{
	// Not actually sure which clamp/wrap modes should be applied. Let's just wrap for now.
	int width = 1 << (gstate.texsize[level] & 0xf);
	int height = 1 << ((gstate.texsize[level]>>8) & 0xf);

	// TODO: These should really be multiplied by 256 to get fixed point coordinates
	// so we can do texture filtering later.

	// Wrap!
	u = (unsigned int)(s) & (width - 1);
	v = (unsigned int)(t) & (height - 1);
}

static inline void GetTextureCoordinates(const VertexData& v0, const VertexData& v1, const VertexData& v2, int w0, int w1, int w2, float& s, float& t)
{
	switch (gstate.getUVGenMode()) {
	case GE_TEXMAP_TEXTURE_COORDS:
	case GE_TEXMAP_UNKNOWN:
	case GE_TEXMAP_ENVIRONMENT_MAP:
		{
			// TODO: What happens if vertex has no texture coordinates?
			// Note that for environment mapping, texture coordinates have been calculated during lighting
			float q0 = 1.f / v0.clippos.w;
			float q1 = 1.f / v1.clippos.w;
			float q2 = 1.f / v2.clippos.w;
			float q = q0 * w0 + q1 * w1 + q2 * w2;
			s = (v0.texturecoords.s() * q0 * w0 + v1.texturecoords.s() * q1 * w1 + v2.texturecoords.s() * q2 * w2) / q;
			t = (v0.texturecoords.t() * q0 * w0 + v1.texturecoords.t() * q1 * w1 + v2.texturecoords.t() * q2 * w2) / q;
		}
		break;
	case GE_TEXMAP_TEXTURE_MATRIX:
		{
			// projection mapping, TODO: Move this code to TransformUnit!
			Vec3<float> source;
			if (gstate.getUVProjMode() == GE_PROJMAP_POSITION) {
			source = ((v0.modelpos * w0 + v1.modelpos * w1 + v2.modelpos * w2) / (w0+w1+w2));
			} else {
			ERROR_LOG_REPORT(G3D, "Software: Unsupported UV projection mode %x", gstate.getUVProjMode());
			}

			Mat3x3<float> tgen(gstate.tgenMatrix);
			Vec3<float> stq = tgen * source + Vec3<float>(gstate.tgenMatrix[9], gstate.tgenMatrix[10], gstate.tgenMatrix[11]);
			s = stq.x/stq.z;
			t = stq.y/stq.z;
		}
		break;
	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported texture mapping mode %x!", gstate.getUVGenMode());
		break;
	}	
}

static inline u32 SampleNearest(int level, unsigned int u, unsigned int v, u8 *srcptr, int texbufwidthbits)
{
	if (!srcptr)
		return 0;
	GETextureFormat texfmt = gstate.getTextureFormat();

	// TODO: Should probably check if textures are aligned properly...

	switch (texfmt) {
	case GE_TFMT_4444:
		srcptr += GetPixelDataOffset(16, texbufwidthbits, u, v);
		return DecodeRGBA4444(*(u16*)srcptr);
	
	case GE_TFMT_5551:
		srcptr += GetPixelDataOffset(16, texbufwidthbits, u, v);
		return DecodeRGBA5551(*(u16*)srcptr);

	case GE_TFMT_5650:
		srcptr += GetPixelDataOffset(16, texbufwidthbits, u, v);
		return DecodeRGB565(*(u16*)srcptr);

	case GE_TFMT_8888:
		srcptr += GetPixelDataOffset(32, texbufwidthbits, u, v);
		return DecodeRGBA8888(*(u32*)srcptr);

	case GE_TFMT_CLUT32:
		{
			srcptr += GetPixelDataOffset(32, texbufwidthbits, u, v);
			u32 val = srcptr[0] + (srcptr[1] << 8) + (srcptr[2] << 16) + (srcptr[3] << 24);
			return LookupColor(gstate.transformClutIndex(val), level);
		}
	case GE_TFMT_CLUT16:
		{
			srcptr += GetPixelDataOffset(16, texbufwidthbits, u, v);
			u16 val = srcptr[0] + (srcptr[1] << 8);
			return LookupColor(gstate.transformClutIndex(val), level);
		}
	case GE_TFMT_CLUT8:
		{
			srcptr += GetPixelDataOffset(8, texbufwidthbits, u, v);
			u8 val = *srcptr;
			return LookupColor(gstate.transformClutIndex(val), level);
		}
	case GE_TFMT_CLUT4:
		{
			srcptr += GetPixelDataOffset(4, texbufwidthbits, u, v);
			u8 val = (u & 1) ? (srcptr[0] >> 4) : (srcptr[0] & 0xF);
			return LookupColor(gstate.transformClutIndex(val), level);
		}
	case GE_TFMT_DXT1:
		{
			DXT1Block *block = (DXT1Block *)srcptr + (v / 4) * (texbufwidthbits / 8 / 4) + (u / 4);
			u32 data[4 * 4];
			DecodeDXT1Block(data, block, 4);
			return DecodeRGBA8888(data[4 * (v % 4) + (u % 4)]);
		}
	case GE_TFMT_DXT3:
		{
			DXT3Block *block = (DXT3Block *)srcptr + (v / 4) * (texbufwidthbits / 8 / 4) + (u / 4);
			u32 data[4 * 4];
			DecodeDXT3Block(data, block, 4);
			return DecodeRGBA8888(data[4 * (v % 4) + (u % 4)]);
		}
	case GE_TFMT_DXT5:
		{
			DXT5Block *block = (DXT5Block *)srcptr + (v / 4) * (texbufwidthbits / 8 / 4) + (u / 4);
			u32 data[4 * 4];
			DecodeDXT5Block(data, block, 4);
			return DecodeRGBA8888(data[4 * (v % 4) + (u % 4)]);
		}
	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported texture format: %x", texfmt);
		return 0;
	}
}

// NOTE: These likely aren't endian safe
static inline u32 GetPixelColor(int x, int y)
{
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		return DecodeRGB565(fb.Get16(x, y, gstate.FrameBufStride()));

	case GE_FORMAT_5551:
		return DecodeRGBA5551(fb.Get16(x, y, gstate.FrameBufStride()));

	case GE_FORMAT_4444:
		return DecodeRGBA4444(fb.Get16(x, y, gstate.FrameBufStride()));

	case GE_FORMAT_8888:
		return fb.Get32(x, y, gstate.FrameBufStride());

	case GE_FORMAT_INVALID:
		_dbg_assert_msg_(G3D, false, "Software: invalid framebuf format.");
	}
	return 0;
}

static inline void SetPixelColor(int x, int y, u32 value)
{
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888To565(value));
		break;

	case GE_FORMAT_5551:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888To5551(value));
		break;

	case GE_FORMAT_4444:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888To4444(value));
		break;

	case GE_FORMAT_8888:
		fb.Set32(x, y, gstate.FrameBufStride(), value);
		break;

	case GE_FORMAT_INVALID:
		_dbg_assert_msg_(G3D, false, "Software: invalid framebuf format.");
	}
}

static inline u16 GetPixelDepth(int x, int y)
{
	return depthbuf.Get16(x, y, gstate.DepthBufStride());
}

static inline void SetPixelDepth(int x, int y, u16 value)
{
	depthbuf.Set16(x, y, gstate.DepthBufStride(), value);
}

static inline u8 GetPixelStencil(int x, int y)
{
	if (gstate.FrameBufFormat() == GE_FORMAT_565) {
		// Always treated as 0 for comparison purposes.
		return 0;
	} else if (gstate.FrameBufFormat() == GE_FORMAT_5551) {
		return ((fb.Get16(x, y, gstate.FrameBufStride()) & 0x8000) != 0) ? 0xFF : 0;
	} else if (gstate.FrameBufFormat() == GE_FORMAT_4444) {
		return Convert4To8(fb.Get16(x, y, gstate.FrameBufStride()) >> 12);
	} else {
		return fb.Get32(x, y, gstate.FrameBufStride()) >> 24;
	}
}

static inline void SetPixelStencil(int x, int y, u8 value)
{
	// TODO: This seems like it maybe respects the alpha mask (at least in some scenarios?)

	if (gstate.FrameBufFormat() == GE_FORMAT_565) {
		// Do nothing
	} else if (gstate.FrameBufFormat() == GE_FORMAT_5551) {
		u16 pixel = fb.Get16(x, y, gstate.FrameBufStride()) & ~0x8000;
		pixel |= value != 0 ? 0x8000 : 0;
		fb.Set16(x, y, gstate.FrameBufStride(), pixel);
	} else if (gstate.FrameBufFormat() == GE_FORMAT_4444) {
		u16 pixel = fb.Get16(x, y, gstate.FrameBufStride()) & ~0xF000;
		pixel |= (u16)value << 12;
		fb.Set16(x, y, gstate.FrameBufStride(), pixel);
	} else {
		u32 pixel = fb.Get32(x, y, gstate.FrameBufStride()) & ~0xFF000000;
		pixel |= (u32)value << 24;
		fb.Set32(x, y, gstate.FrameBufStride(), pixel);
	}
}

static inline bool DepthTestPassed(int x, int y, u16 z)
{
	u16 reference_z = GetPixelDepth(x, y);

	switch (gstate.getDepthTestFunction()) {
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
		return vertex.x < line1.x + ((int)line2.x - (int)line1.x) * ((int)vertex.y - (int)line1.y) / ((int)line2.y - (int)line1.y);
	}
}

static inline bool StencilTestPassed(u8 stencil)
{
	// TODO: Does the masking logic make any sense?
	stencil &= gstate.getStencilTestMask();
	u8 ref = gstate.getStencilTestRef() & gstate.getStencilTestMask();
	switch (gstate.getStencilTestFunction()) {
		case GE_COMP_NEVER:
			return false;

		case GE_COMP_ALWAYS:
			return true;

		case GE_COMP_EQUAL:
			return ref == stencil;

		case GE_COMP_NOTEQUAL:
			return ref != stencil;

		case GE_COMP_LESS:
			return ref < stencil;

		case GE_COMP_LEQUAL:
			return ref <= stencil;

		case GE_COMP_GREATER:
			return ref > stencil;

		case GE_COMP_GEQUAL:
			return ref >= stencil;
	}
	return true;
}

static inline u8 ApplyStencilOp(int op, int x, int y)
{
	u8 old_stencil = GetPixelStencil(x, y); // TODO: Apply mask?
	u8 reference_stencil = gstate.getStencilTestRef(); // TODO: Apply mask?

	switch (op) {
		case GE_STENCILOP_KEEP:
			return old_stencil;

		case GE_STENCILOP_ZERO:
			return 0;

		case GE_STENCILOP_REPLACE:
			return reference_stencil;

		case GE_STENCILOP_INVERT:
			return ~old_stencil;

		case GE_STENCILOP_INCR:
			switch (gstate.FrameBufFormat()) {
			case GE_FORMAT_8888:
				if (old_stencil != 0xFF) {
					return old_stencil + 1;
				}
				return old_stencil;
			case GE_FORMAT_5551:
				return 0xFF;
			case GE_FORMAT_4444:
				if (old_stencil < 0xF0) {
					return old_stencil + 0x10;
				}
				return old_stencil;
			default:
				return old_stencil;
			}
			break;

		case GE_STENCILOP_DECR:
			switch (gstate.FrameBufFormat()) {
			case GE_FORMAT_4444:
				if (old_stencil >= 0x10)
					return old_stencil - 0x10;
				break;
			default:
				if (old_stencil != 0)
					return old_stencil - 1;
				return old_stencil;
			}
			break;
	}

	return old_stencil;
}

static inline u32 ApplyLogicOp(GELogicOp op, u32 old_color, u32 new_color)
{
	switch (op) {
	case GE_LOGIC_CLEAR:
		new_color = 0;
		break;

	case GE_LOGIC_AND:
		new_color = new_color & old_color;
		break;

	case GE_LOGIC_AND_REVERSE:
		new_color = new_color & ~old_color;
		break;

	case GE_LOGIC_COPY:
		//new_color = new_color;
		break;

	case GE_LOGIC_AND_INVERTED:
		new_color = ~new_color & old_color;
		break;

	case GE_LOGIC_NOOP:
		new_color = old_color;
		break;

	case GE_LOGIC_XOR:
		new_color = new_color ^ old_color;
		break;

	case GE_LOGIC_OR:
		new_color = new_color | old_color;
		break;

	case GE_LOGIC_NOR:
		new_color = ~(new_color | old_color);
		break;

	case GE_LOGIC_EQUIV:
		new_color = ~(new_color ^ old_color);
		break;

	case GE_LOGIC_INVERTED:
		new_color = ~old_color;
		break;

	case GE_LOGIC_OR_REVERSE:
		new_color = new_color | ~old_color;
		break;

	case GE_LOGIC_COPY_INVERTED:
		new_color = ~new_color;
		break;

	case GE_LOGIC_OR_INVERTED:
		new_color = ~new_color | old_color;
		break;

	case GE_LOGIC_NAND:
		new_color = ~(new_color & old_color);
		break;

	case GE_LOGIC_SET:
		new_color = 0xFFFFFFFF;
		break;
	}

	return new_color;
}

static inline Vec4<int> GetTextureFunctionOutput(const Vec3<int>& prim_color_rgb, int prim_color_a, const Vec4<int>& texcolor)
{
	Vec3<int> out_rgb;
	int out_a;

	bool rgba = gstate.isTextureAlphaUsed();

	switch (gstate.getTextureFunction()) {
	case GE_TEXFUNC_MODULATE:
		out_rgb = prim_color_rgb * texcolor.rgb() / 255;
		out_a = (rgba) ? (prim_color_a * texcolor.a() / 255) : prim_color_a;
		break;

	case GE_TEXFUNC_DECAL:
	{
		int t = (rgba) ? texcolor.a() : 255;
		int invt = (rgba) ? 255 - t : 0;
		out_rgb = (prim_color_rgb * invt + texcolor.rgb() * t) / 255;
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
		ERROR_LOG_REPORT(G3D, "Software: Unknown texture function %x", gstate.getTextureFunction());
		out_rgb = Vec3<int>::AssignToAll(0);
		out_a = 0;
	}

	return Vec4<int>(out_rgb.r(), out_rgb.g(), out_rgb.b(), out_a);
}

static inline bool ColorTestPassed(Vec3<int> color)
{
	const u32 mask = gstate.getColorTestMask();
	const u32 c = color.ToRGB() & mask;
	const u32 ref = gstate.getColorTestRef() & mask;
	switch (gstate.getColorTestFunction()) {
		case GE_COMP_NEVER:
			return false;

		case GE_COMP_ALWAYS:
			return true;

		case GE_COMP_EQUAL:
			return c == ref;

		case GE_COMP_NOTEQUAL:
			return c != ref;

		default:
			ERROR_LOG_REPORT(G3D, "Software: Invalid colortest function: %d", gstate.getColorTestFunction());
			break;
	}
	return true;
}

static inline bool AlphaTestPassed(int alpha)
{
	const u8 mask = gstate.getAlphaTestMask() & 0xFF;
	const u8 ref = gstate.getAlphaTestRef() & mask;
	alpha &= mask;

	switch (gstate.getAlphaTestFunction()) {
		case GE_COMP_NEVER:
			return false;

		case GE_COMP_ALWAYS:
			return true;

		case GE_COMP_EQUAL:
			return (alpha == ref);

		case GE_COMP_NOTEQUAL:
			return (alpha != ref);

		case GE_COMP_LESS:
			return (alpha < ref);

		case GE_COMP_LEQUAL:
			return (alpha <= ref);

		case GE_COMP_GREATER:
			return (alpha > ref);

		case GE_COMP_GEQUAL:
			return (alpha >= ref);
	}
	return true;
}

static inline Vec3<int> GetSourceFactor(int source_a, const Vec4<int>& dst)
{
	switch (gstate.getBlendFuncA()) {
	case GE_SRCBLEND_DSTCOLOR:
		return dst.rgb();

	case GE_SRCBLEND_INVDSTCOLOR:
		return Vec3<int>::AssignToAll(255) - dst.rgb();

	case GE_SRCBLEND_SRCALPHA:
		return Vec3<int>::AssignToAll(source_a);

	case GE_SRCBLEND_INVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - source_a);

	case GE_SRCBLEND_DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case GE_SRCBLEND_INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case GE_SRCBLEND_DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source_a);

	case GE_SRCBLEND_DOUBLEINVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - 2 * source_a);

	case GE_SRCBLEND_DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case GE_SRCBLEND_DOUBLEINVDSTALPHA:
		// TODO: Clamping?
		return Vec3<int>::AssignToAll(255 - 2 * dst.a());

	case GE_SRCBLEND_FIXA:
		return Vec4<int>::FromRGBA(gstate.getFixA()).rgb();

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unknown source factor %x", gstate.getBlendFuncA());
		return Vec3<int>();
	}
}

static inline Vec3<int> GetDestFactor(const Vec3<int>& source_rgb, int source_a, const Vec4<int>& dst)
{
	switch (gstate.getBlendFuncB()) {
	case GE_DSTBLEND_SRCCOLOR:
		return source_rgb;

	case GE_DSTBLEND_INVSRCCOLOR:
		return Vec3<int>::AssignToAll(255) - source_rgb;

	case GE_DSTBLEND_SRCALPHA:
		return Vec3<int>::AssignToAll(source_a);

	case GE_DSTBLEND_INVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - source_a);

	case GE_DSTBLEND_DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case GE_DSTBLEND_INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case GE_DSTBLEND_DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source_a);

	case GE_DSTBLEND_DOUBLEINVSRCALPHA:
		return Vec3<int>::AssignToAll(255 - 2 * source_a);

	case GE_DSTBLEND_DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case GE_DSTBLEND_DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - 2 * dst.a());

	case GE_DSTBLEND_FIXB:
		return Vec4<int>::FromRGBA(gstate.getFixB()).rgb();

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unknown dest factor %x", gstate.getBlendFuncB());
		return Vec3<int>();
	}
}

static inline Vec3<int> AlphaBlendingResult(const Vec3<int>& source_rgb, int source_a, const Vec4<int> dst)
{
	Vec3<int> srcfactor = GetSourceFactor(source_a, dst);
	Vec3<int> dstfactor = GetDestFactor(source_rgb, source_a, dst);

	switch (gstate.getBlendEq()) {
	case GE_BLENDMODE_MUL_AND_ADD:
		return (source_rgb * srcfactor + dst.rgb() * dstfactor) / 255;

	case GE_BLENDMODE_MUL_AND_SUBTRACT:
		return (source_rgb * srcfactor - dst.rgb() * dstfactor) / 255;

	case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
		return (dst.rgb() * dstfactor - source_rgb * srcfactor) / 255;

	case GE_BLENDMODE_MIN:
		return Vec3<int>(std::min(source_rgb.r(), dst.r()),
						std::min(source_rgb.g(), dst.g()),
						std::min(source_rgb.b(), dst.b()));

	case GE_BLENDMODE_MAX:
		return Vec3<int>(std::max(source_rgb.r(), dst.r()),
						std::max(source_rgb.g(), dst.g()),
						std::max(source_rgb.b(), dst.b()));

	case GE_BLENDMODE_ABSDIFF:
		return Vec3<int>(::abs(source_rgb.r() - dst.r()),
						::abs(source_rgb.g() - dst.g()),
						::abs(source_rgb.b() - dst.b()));

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unknown blend function %x", gstate.getBlendEq());
		return Vec3<int>();
	}
}

// Draws triangle, vertices specified in counter-clockwise direction
void DrawTriangle(const VertexData& v0, const VertexData& v1, const VertexData& v2)
{
	Vec2<int> d01((int)v0.screenpos.x - (int)v1.screenpos.x, (int)v0.screenpos.y - (int)v1.screenpos.y);
	Vec2<int> d02((int)v0.screenpos.x - (int)v2.screenpos.x, (int)v0.screenpos.y - (int)v2.screenpos.y);
	Vec2<int> d12((int)v1.screenpos.x - (int)v2.screenpos.x, (int)v1.screenpos.y - (int)v2.screenpos.y);

	// Drop primitives which are not in CCW order by checking the cross product
	if (d01.x * d02.y - d01.y * d02.x < 0)
		return;

	int minX = std::min(std::min(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) / 16 * 16;
	int minY = std::min(std::min(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) / 16 * 16;
	int maxX = std::max(std::max(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) / 16 * 16;
	int maxY = std::max(std::max(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) / 16 * 16;

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1(), 0);
	DrawingCoords scissorBR(gstate.getScissorX2(), gstate.getScissorY2(), 0);
	minX = std::max(minX, (int)TransformUnit::DrawingToScreen(scissorTL).x);
	maxX = std::min(maxX, (int)TransformUnit::DrawingToScreen(scissorBR).x);
	minY = std::max(minY, (int)TransformUnit::DrawingToScreen(scissorTL).y);
	maxY = std::min(maxY, (int)TransformUnit::DrawingToScreen(scissorBR).y);

	int bias0 = IsRightSideOrFlatBottomLine(v0.screenpos.xy(), v1.screenpos.xy(), v2.screenpos.xy()) ? -1 : 0;
	int bias1 = IsRightSideOrFlatBottomLine(v1.screenpos.xy(), v2.screenpos.xy(), v0.screenpos.xy()) ? -1 : 0;
	int bias2 = IsRightSideOrFlatBottomLine(v2.screenpos.xy(), v0.screenpos.xy(), v1.screenpos.xy()) ? -1 : 0;

	int texlevel = 0;
	int texbufwidthbits = 0;
	u8 *texptr = NULL;
	if (gstate.isTextureMapEnabled() && !gstate.isModeClear()) {
		// TODO: Always using level 0.
		GETextureFormat texfmt = gstate.getTextureFormat();
		u32 texaddr = gstate.getTextureAddress(texlevel);
		texbufwidthbits = GetTextureBufw(texlevel, texaddr, texfmt) * 8;
		texptr = Memory::GetPointer(texaddr);
	}

	ScreenCoords pprime(minX, minY, 0);
	int w0_base = orient2d(v1.screenpos, v2.screenpos, pprime);
	int w1_base = orient2d(v2.screenpos, v0.screenpos, pprime);
	int w2_base = orient2d(v0.screenpos, v1.screenpos, pprime);

	for (pprime.y = minY; pprime.y <= maxY; pprime.y +=16,
										w0_base += orient2dIncY(d12.x)*16,
										w1_base += orient2dIncY(-d02.x)*16,
										w2_base += orient2dIncY(d01.x)*16) {
		int w0 = w0_base;
		int w1 = w1_base;
		int w2 = w2_base;
		for (pprime.x = minX; pprime.x <= maxX; pprime.x +=16,
											w0 += orient2dIncX(d12.y)*16,
											w1 += orient2dIncX(-d02.y)*16,
											w2 += orient2dIncX(d01.y)*16) {
			DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);

			// If p is on or inside all edges, render pixel
			// TODO: Should we render if the pixel is both on the left and the right side? (i.e. degenerated triangle)
			if (w0 + bias0 >=0 && w1 + bias1 >= 0 && w2 + bias2 >= 0) {
				// TODO: Check if this check is still necessary
				if (w0 == 0 && w1 == 0 && w2 == 0)
					continue;

				float wsum = 1.0f / (w0 + w1 + w2);

				Vec3<int> prim_color_rgb(0, 0, 0);
				int prim_color_a = 0;
				Vec3<int> sec_color(0, 0, 0);
				if (gstate.getShadeMode() == GE_SHADE_GOURAUD) {
					// NOTE: When not casting color0 and color1 to float vectors, this code suffers from severe overflow issues.
					// Not sure if that should be regarded as a bug or if casting to float is a valid fix.
					// TODO: Is that the correct way to interpolate?
					prim_color_rgb = ((v0.color0.rgb().Cast<float>() * w0 +
									v1.color0.rgb().Cast<float>() * w1 +
									v2.color0.rgb().Cast<float>() * w2) * wsum).Cast<int>();
					prim_color_a = (int)(((float)v0.color0.a() * w0 + (float)v1.color0.a() * w1 + (float)v2.color0.a() * w2) * wsum);
					sec_color = ((v0.color1.Cast<float>() * w0 +
									v1.color1.Cast<float>() * w1 +
									v2.color1.Cast<float>() * w2) * wsum).Cast<int>();
				} else {
					prim_color_rgb = v2.color0.rgb();
					prim_color_a = v2.color0.a();
					sec_color = v2.color1;
				}

				if (gstate.isTextureMapEnabled() && !gstate.isModeClear()) {
					unsigned int u = 0, v = 0;
					if (gstate.isModeThrough()) {
						// TODO: Is it really this simple?
						float s = ((v0.texturecoords.s() * w0 + v1.texturecoords.s() * w1 + v2.texturecoords.s() * w2) * wsum);
						float t = ((v0.texturecoords.t() * w0 + v1.texturecoords.t() * w1 + v2.texturecoords.t() * w2) * wsum);
						GetTexelCoordinatesThrough(0, s, t, u, v);
					} else {
						float s = 0, t = 0;
						GetTextureCoordinates(v0, v1, v2, w0, w1, w2, s, t);
						GetTexelCoordinates(0, s, t, u, v);
					}

					Vec4<int> texcolor = Vec4<int>::FromRGBA(SampleNearest(texlevel, u, v, texptr, texbufwidthbits));
					Vec4<int> out = GetTextureFunctionOutput(prim_color_rgb, prim_color_a, texcolor);
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

				// TODO: Is that the correct way to interpolate?
				// Without the (u32), this causes an ICE in some versions of gcc.
				u16 z = (u16)(u32)(((float)v0.screenpos.z * w0 + (float)v1.screenpos.z * w1 + (float)v2.screenpos.z * w2) * wsum);

				// Depth range test
				if (!gstate.isModeThrough())
					if (z < gstate.getDepthRangeMin() || z > gstate.getDepthRangeMax())
						continue;

				if (gstate.isColorTestEnabled() && !gstate.isModeClear())
					if (!ColorTestPassed(prim_color_rgb))
						continue;

				// TODO: Does a need to be clamped?
				if (gstate.isAlphaTestEnabled() && !gstate.isModeClear())
					if (!AlphaTestPassed(prim_color_a))
						continue;

				u8 stencil = GetPixelStencil(p.x, p.y);
				// TODO: Is it safe to ignore gstate.isDepthTestEnabled() when clear mode is enabled?
				if (!gstate.isModeClear() && (gstate.isStencilTestEnabled() || gstate.isDepthTestEnabled())) {
					if (gstate.isStencilTestEnabled() && !StencilTestPassed(stencil)) {
						stencil = ApplyStencilOp(gstate.getStencilOpSFail(), p.x, p.y);
						SetPixelStencil(p.x, p.y, stencil);
						continue;
					}

					// Also apply depth at the same time.  If disabled, same as passing.
					if (gstate.isDepthTestEnabled() && !DepthTestPassed(p.x, p.y, z)) {
						if (gstate.isStencilTestEnabled()) {
							stencil = ApplyStencilOp(gstate.getStencilOpZFail(), p.x, p.y);
							SetPixelStencil(p.x, p.y, stencil);
						}
						continue;
					} else if (gstate.isStencilTestEnabled()) {
						stencil = ApplyStencilOp(gstate.getStencilOpZPass(), p.x, p.y);
					}

					if (gstate.isDepthTestEnabled() && gstate.isDepthWriteEnabled()) {
						SetPixelDepth(p.x, p.y, z);
					}
				} else if (gstate.isModeClear() && gstate.isClearModeDepthWriteEnabled()) {
					SetPixelDepth(p.x, p.y, z);
				}

				if (gstate.isAlphaBlendEnabled() && !gstate.isModeClear()) {
					Vec4<int> dst = Vec4<int>::FromRGBA(GetPixelColor(p.x, p.y));
					prim_color_rgb = AlphaBlendingResult(prim_color_rgb, prim_color_a, dst);
				}
				prim_color_rgb = prim_color_rgb.Clamp(0, 255);

				u32 new_color = Vec4<int>(prim_color_rgb.r(), prim_color_rgb.g(), prim_color_rgb.b(), stencil).ToRGBA();
				u32 old_color = GetPixelColor(p.x, p.y);

				// TODO: Is alpha blending still performed if logic ops are enabled?
				if (gstate.isLogicOpEnabled() && !gstate.isModeClear()) {
					// Logic ops don't affect stencil.
					new_color = (stencil << 24) | (ApplyLogicOp(gstate.getLogicOp(), old_color, new_color) & 0x00FFFFFF);
				}

				if (gstate.isModeClear()) {
					new_color = (new_color & ~gstate.getClearModeColorMask()) | (old_color & gstate.getClearModeColorMask());
				} else {
					new_color = (new_color & ~gstate.getColorMask()) | (old_color & gstate.getColorMask());
				}

				SetPixelColor(p.x, p.y, new_color);
			}
		}
	}
}

bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer)
{
	buffer.Allocate(gstate.DepthBufStride(), 512, GPU_DBG_FORMAT_8BIT);

	u8 *row = buffer.GetData();
	for (int y = 0; y < 512; ++y) {
		for (int x = 0; x < gstate.DepthBufStride(); ++x) {
			row[x] = GetPixelStencil(x, y);
		}
		row += gstate.DepthBufStride();
	}
	return true;
}

bool GetCurrentTexture(GPUDebugBuffer &buffer)
{
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	buffer.Allocate(w, h, GE_FORMAT_8888, false);

	GETextureFormat texfmt = gstate.getTextureFormat();
	u32 texaddr = gstate.getTextureAddress(0);
	int texbufwidthbits = GetTextureBufw(0, texaddr, texfmt) * 8;
	u8 *texptr = Memory::GetPointer(texaddr);

	u32 *row = (u32 *)buffer.GetData();
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			row[x] = SampleNearest(0, x, y, texptr, texbufwidthbits);
		}
		row += w;
	}
	return true;
}

} // namespace
