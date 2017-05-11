// Copyright (c) 2017- PPSSPP Project.

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

#include <unordered_map>
#include <mutex>
#include "Common/ColorConv.h"
#include "Core/Reporting.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Sampler.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

using namespace Math3D;

extern u32 clut[4096];

namespace Sampler {

static u32 SampleNearest(int u, int v, const u8 *tptr, int bufw, int level);

std::mutex jitCacheLock;
SamplerJitCache *jitCache = nullptr;

void Init() {
	jitCache = new SamplerJitCache();
}

void Shutdown() {
	delete jitCache;
	jitCache = nullptr;
}

NearestFunc GetNearestFunc() {
	SamplerID id;
	jitCache->ComputeSamplerID(&id);
	NearestFunc jitted = jitCache->GetSampler(id);
	if (jitted) {
		return jitted;
	}

	return &SampleNearest;
}

SamplerJitCache::SamplerJitCache()
#if PPSSPP_ARCH(ARM64)
 : fp(this)
#endif
{
	// 256k should be enough.
	AllocCodeSpace(1024 * 64 * 4);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#elif defined(ARM)
	BKPT(0);
	BKPT(0);
#endif
}

void SamplerJitCache::Clear() {
	ClearCodeSpace(0);
	cache_.clear();
}

void SamplerJitCache::ComputeSamplerID(SamplerID *id_out) {
	SamplerID id;

	id.texfmt = gstate.getTextureFormat();
	id.clutfmt = gstate.getClutPaletteFormat();
	id.swizzle = gstate.isTextureSwizzled();
	// Only CLUT4 can use separate CLUTs per mimap.
	id.useSharedClut = gstate.isClutSharedForMipmaps() || gstate.getTextureFormat() != GE_TFMT_CLUT4;
	id.hasClutMask = gstate.getClutIndexMask() != 0xFF;
	id.hasClutShift = gstate.getClutIndexShift() != 0;
	id.hasClutOffset = gstate.getClutIndexStartPos() != 0;

	*id_out = id;
}

NearestFunc SamplerJitCache::GetSampler(const SamplerID &id) {
	std::lock_guard<std::mutex> guard(jitCacheLock);

	auto it = cache_.find(id);
	if (it != cache_.end()) {
		return it->second;
	}

	// TODO: What should be the min size?  Can we even hit this?
	if (GetSpaceLeft() < 16384) {
		Clear();
	}

	// TODO
#ifdef _M_X64
	NearestFunc func = Compile(id);
	cache_[id] = func;
	return func;
#else
	return nullptr;
#endif
}

template <unsigned int texel_size_bits>
static inline int GetPixelDataOffset(unsigned int row_pitch_bytes, unsigned int u, unsigned int v)
{
	if (!gstate.isTextureSwizzled())
		return (v * (row_pitch_bytes * texel_size_bits >> 3)) + (u * texel_size_bits >> 3);

	const int tile_size_bits = 32;
	const int tiles_in_block_horizontal = 4;
	const int tiles_in_block_vertical = 8;

	int texels_per_tile = tile_size_bits / texel_size_bits;
	int tile_u = u / texels_per_tile;
	int tile_idx = (v % tiles_in_block_vertical) * (tiles_in_block_horizontal) +
	// TODO: not sure if the *texel_size_bits/8 factor is correct
					(v / tiles_in_block_vertical) * ((row_pitch_bytes*texel_size_bits/(tile_size_bits))*tiles_in_block_vertical) +
					(tile_u % tiles_in_block_horizontal) +
					(tile_u / tiles_in_block_horizontal) * (tiles_in_block_horizontal*tiles_in_block_vertical);

	return tile_idx * (tile_size_bits / 8) + ((u % texels_per_tile) * texel_size_bits) / 8;
}

static inline u32 LookupColor(unsigned int index, unsigned int level)
{
	const bool mipmapShareClut = gstate.isClutSharedForMipmaps();
	const int clutSharingOffset = mipmapShareClut ? 0 : level * 16;

	switch (gstate.getClutPaletteFormat()) {
	case GE_CMODE_16BIT_BGR5650:
		return RGB565ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR5551:
		return RGBA5551ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR4444:
		return RGBA4444ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_32BIT_ABGR8888:
		return clut[index + clutSharingOffset];

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported palette format: %x", gstate.getClutPaletteFormat());
		return 0;
	}
}

struct Nearest4 {
	MEMORY_ALIGNED16(u32 v[4]);

	operator u32() const {
		return v[0];
	}
};

template <int N>
inline static Nearest4 SampleNearest(int u[N], int v[N], const u8 *srcptr, int texbufw, int level)
{
	Nearest4 res;
	if (!srcptr) {
		memset(res.v, 0, sizeof(res.v));
		return res;
	}

	GETextureFormat texfmt = gstate.getTextureFormat();

	// TODO: Should probably check if textures are aligned properly...

	switch (texfmt) {
	case GE_TFMT_4444:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i]);
			res.v[i] = RGBA4444ToRGBA8888(*(const u16 *)src);
		}
		return res;
	
	case GE_TFMT_5551:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i]);
			res.v[i] = RGBA5551ToRGBA8888(*(const u16 *)src);
		}
		return res;

	case GE_TFMT_5650:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i]);
			res.v[i] = RGB565ToRGBA8888(*(const u16 *)src);
		}
		return res;

	case GE_TFMT_8888:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<32>(texbufw, u[i], v[i]);
			res.v[i] = *(const u32 *)src;
		}
		return res;

	case GE_TFMT_CLUT32:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<32>(texbufw, u[i], v[i]);
			u32 val = src[0] + (src[1] << 8) + (src[2] << 16) + (src[3] << 24);
			res.v[i] = LookupColor(gstate.transformClutIndex(val), 0);
		}
		return res;

	case GE_TFMT_CLUT16:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i]);
			u16 val = src[0] + (src[1] << 8);
			res.v[i] = LookupColor(gstate.transformClutIndex(val), 0);
		}
		return res;

	case GE_TFMT_CLUT8:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<8>(texbufw, u[i], v[i]);
			u8 val = *src;
			res.v[i] = LookupColor(gstate.transformClutIndex(val), 0);
		}
		return res;

	case GE_TFMT_CLUT4:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<4>(texbufw, u[i], v[i]);
			u8 val = (u[i] & 1) ? (src[0] >> 4) : (src[0] & 0xF);
			// Only CLUT4 uses separate mipmap palettes.
			res.v[i] = LookupColor(gstate.transformClutIndex(val), level);
		}
		return res;

	case GE_TFMT_DXT1:
		for (int i = 0; i < N; ++i) {
			const DXT1Block *block = (const DXT1Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			u32 data[4 * 4];
			DecodeDXT1Block(data, block, 4, 4, false);
			res.v[i] = data[4 * (v[i] % 4) + (u[i] % 4)];
		}
		return res;

	case GE_TFMT_DXT3:
		for (int i = 0; i < N; ++i) {
			const DXT3Block *block = (const DXT3Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			u32 data[4 * 4];
			DecodeDXT3Block(data, block, 4, 4);
			res.v[i] = data[4 * (v[i] % 4) + (u[i] % 4)];
		}
		return res;

	case GE_TFMT_DXT5:
		for (int i = 0; i < N; ++i) {
			const DXT5Block *block = (const DXT5Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			u32 data[4 * 4];
			DecodeDXT5Block(data, block, 4, 4);
			res.v[i] = data[4 * (v[i] % 4) + (u[i] % 4)];
		}
		return res;

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported texture format: %x", texfmt);
		memset(res.v, 0, sizeof(res.v));
		return res;
	}
}

static u32 SampleNearest(int u, int v, const u8 *tptr, int bufw, int level) {
	return SampleNearest<1>(&u, &v, tptr, bufw, level);
}

Vec4<int> SampleLinear(NearestFunc sampler, int u[4], int v[4], int frac_u, int frac_v, const u8 *tptr, int bufw, int texlevel) {
	Nearest4 c;
	c.v[0] = sampler(u[0], v[0], tptr, bufw, texlevel);
	c.v[1] = sampler(u[1], v[1], tptr, bufw, texlevel);
	c.v[2] = sampler(u[2], v[2], tptr, bufw, texlevel);
	c.v[3] = sampler(u[3], v[3], tptr, bufw, texlevel);

#if defined(_M_SSE)
	const __m128i z = _mm_setzero_si128();

	__m128i cvec = _mm_load_si128((const __m128i *)c.v);
	__m128i tvec = _mm_unpacklo_epi8(cvec, z);
	tvec = _mm_mullo_epi16(tvec, _mm_set1_epi16(0x100 - frac_v));
	__m128i bvec = _mm_unpackhi_epi8(cvec, z);
	bvec = _mm_mullo_epi16(bvec, _mm_set1_epi16(frac_v));

	// This multiplies the left and right sides.  We shift right after, although this may round down...
	__m128i rowmult = _mm_set_epi16(frac_u, frac_u, frac_u, frac_u, 0x100 - frac_u, 0x100 - frac_u, 0x100 - frac_u, 0x100 - frac_u);
	__m128i tmp = _mm_mulhi_epu16(_mm_add_epi16(tvec, bvec), rowmult);

	// Now we need to add the left and right sides together.
	__m128i res = _mm_add_epi16(tmp, _mm_shuffle_epi32(tmp, _MM_SHUFFLE(3, 2, 3, 2)));
	return Vec4<int>(_mm_unpacklo_epi16(res, z));
#else
	Vec4<int> texcolor_tl = Vec4<int>::FromRGBA(c.v[0]);
	Vec4<int> texcolor_tr = Vec4<int>::FromRGBA(c.v[1]);
	Vec4<int> texcolor_bl = Vec4<int>::FromRGBA(c.v[2]);
	Vec4<int> texcolor_br = Vec4<int>::FromRGBA(c.v[3]);
	// 0x100 causes a slight bias to tl, but without it we'd have to divide by 255 * 255.
	Vec4<int> t = texcolor_tl * (0x100 - frac_u) + texcolor_tr * frac_u;
	Vec4<int> b = texcolor_bl * (0x100 - frac_u) + texcolor_br * frac_u;
	return (t * (0x100 - frac_v) + b * frac_v) / (256 * 256);
#endif
}

};
