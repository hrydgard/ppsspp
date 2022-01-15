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

#include "ppsspp_config.h"
#include <unordered_map>
#include <mutex>
#include "Common/Data/Convert/ColorConv.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRegCache.h"
#include "GPU/Software/Sampler.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

using namespace Math3D;
using namespace Rasterizer;

extern u32 clut[4096];

namespace Sampler {

static Vec4IntResult SOFTRAST_CALL SampleNearest(float s, float t, int x, int y, Vec4IntArg prim_color, const u8 *const *tptr, const int *bufw, int level, int levelFrac, const SamplerID &samplerID);
static Vec4IntResult SOFTRAST_CALL SampleLinear(float s, float t, int x, int y, Vec4IntArg prim_color, const u8 *const *tptr, const int *bufw, int level, int levelFrac, const SamplerID &samplerID);
static Vec4IntResult SOFTRAST_CALL SampleFetch(int u, int v, const u8 *tptr, int bufw, int level, const SamplerID &samplerID);

std::mutex jitCacheLock;
SamplerJitCache *jitCache = nullptr;

void Init() {
	jitCache = new SamplerJitCache();
}

void Shutdown() {
	delete jitCache;
	jitCache = nullptr;
}

bool DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (!jitCache->IsInSpace(ptr)) {
		return false;
	}

	name = jitCache->DescribeCodePtr(ptr);
	return true;
}

NearestFunc GetNearestFunc(SamplerID id) {
	id.linear = false;
	NearestFunc jitted = jitCache->GetNearest(id);
	if (jitted) {
		return jitted;
	}

	return &SampleNearest;
}

LinearFunc GetLinearFunc(SamplerID id) {
	id.linear = true;
	LinearFunc jitted = jitCache->GetLinear(id);
	if (jitted) {
		return jitted;
	}

	return &SampleLinear;
}

FetchFunc GetFetchFunc(SamplerID id) {
	id.fetch = true;
	FetchFunc jitted = jitCache->GetFetch(id);
	if (jitted) {
		return jitted;
	}

	return &SampleFetch;
}

SamplerJitCache::SamplerJitCache()
#if PPSSPP_ARCH(ARM64)
 : fp(this)
#endif
{
	// 256k should be enough.
	AllocCodeSpace(1024 * 64 * 4);
	ClearCodeSpace(0);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32) && (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !PPSSPP_PLATFORM(UWP)
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#elif PPSSPP_ARCH(ARM)
	BKPT(0);
	BKPT(0);
#endif
}

void SamplerJitCache::Clear() {
	ClearCodeSpace(0);
	cache_.clear();
	addresses_.clear();

	const10All16_ = nullptr;
	const10Low_ = nullptr;
	const10All8_ = nullptr;

	constWidth256f_ = nullptr;
	constHeight256f_ = nullptr;
	constWidthMinus1i_ = nullptr;
	constHeightMinus1i_ = nullptr;

	constOnes32_ = nullptr;
	constOnes16_ = nullptr;
	constUNext_ = nullptr;
	constVNext_ = nullptr;

	const5551Swizzle_ = nullptr;
	const5650Swizzle_ = nullptr;
}

void SamplerJitCache::Describe(const std::string &message) {
	descriptions_[GetCodePointer()] = message;
}

std::string SamplerJitCache::DescribeCodePtr(const u8 *ptr) {
	constexpr bool USE_IDS = false;
	ptrdiff_t dist = 0x7FFFFFFF;
	if (USE_IDS) {
		SamplerID found{};
		for (const auto &it : addresses_) {
			ptrdiff_t it_dist = ptr - it.second;
			if (it_dist >= 0 && it_dist < dist) {
				found = it.first;
				dist = it_dist;
			}
		}

		return DescribeSamplerID(found);
	} else {
		std::string found;
		for (const auto &it : descriptions_) {
			ptrdiff_t it_dist = ptr - it.first;
			if (it_dist >= 0 && it_dist < dist) {
				found = it.second;
				dist = it_dist;
			}
		}
		return found;
	}
}

NearestFunc SamplerJitCache::GetNearest(const SamplerID &id) {
	std::lock_guard<std::mutex> guard(jitCacheLock);

	auto it = cache_.find(id);
	if (it != cache_.end()) {
		return (NearestFunc)it->second;
	}

	// TODO: What should be the min size?  Can we even hit this?
	if (GetSpaceLeft() < 16384) {
		Clear();
	}

#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	if (g_Config.bSoftwareRenderingJit) {
		addresses_[id] = GetCodePointer();
		NearestFunc func = CompileNearest(id);
		cache_[id] = (NearestFunc)func;
		return func;
	}
#endif
	return nullptr;
}

LinearFunc SamplerJitCache::GetLinear(const SamplerID &id) {
	std::lock_guard<std::mutex> guard(jitCacheLock);

	auto it = cache_.find(id);
	if (it != cache_.end()) {
		return (LinearFunc)it->second;
	}

	// TODO: What should be the min size?  Can we even hit this?
	if (GetSpaceLeft() < 16384) {
		Clear();
	}

#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	if (g_Config.bSoftwareRenderingJit) {
		addresses_[id] = GetCodePointer();
		LinearFunc func = CompileLinear(id);
		cache_[id] = (NearestFunc)func;
		return func;
	}
#endif
	return nullptr;
}

FetchFunc SamplerJitCache::GetFetch(const SamplerID &id) {
	std::lock_guard<std::mutex> guard(jitCacheLock);

	auto it = cache_.find(id);
	if (it != cache_.end()) {
		return (FetchFunc)it->second;
	}

	// TODO: What should be the min size?  Can we even hit this?
	if (GetSpaceLeft() < 16384) {
		Clear();
	}

#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	if (g_Config.bSoftwareRenderingJit) {
		addresses_[id] = GetCodePointer();
		FetchFunc func = CompileFetch(id);
		cache_[id] = (NearestFunc)func;
		return func;
	}
#endif
	return nullptr;
}

template <uint32_t texel_size_bits>
static inline int GetPixelDataOffset(uint32_t row_pitch_pixels, uint32_t u, uint32_t v, bool swizzled) {
	if (!swizzled)
		return (v * (row_pitch_pixels * texel_size_bits >> 3)) + (u * texel_size_bits >> 3);

	const int tile_size_bits = 32;
	const int tiles_in_block_horizontal = 4;
	const int tiles_in_block_vertical = 8;

	int texels_per_tile = tile_size_bits / texel_size_bits;
	int tile_u = u / texels_per_tile;
	int tile_idx = (v % tiles_in_block_vertical) * (tiles_in_block_horizontal) +
	// TODO: not sure if the *texel_size_bits/8 factor is correct
					(v / tiles_in_block_vertical) * ((row_pitch_pixels*texel_size_bits/(tile_size_bits))*tiles_in_block_vertical) +
					(tile_u % tiles_in_block_horizontal) +
					(tile_u / tiles_in_block_horizontal) * (tiles_in_block_horizontal*tiles_in_block_vertical);

	return tile_idx * (tile_size_bits / 8) + ((u % texels_per_tile) * texel_size_bits) / 8;
}

static inline u32 LookupColor(unsigned int index, unsigned int level, const SamplerID &samplerID) {
	const int clutSharingOffset = samplerID.useSharedClut ? 0 : level * 16;

	switch (samplerID.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		return RGB565ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR5551:
		return RGBA5551ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR4444:
		return RGBA4444ToRGBA8888(reinterpret_cast<u16*>(clut)[index + clutSharingOffset]);

	case GE_CMODE_32BIT_ABGR8888:
		return clut[index + clutSharingOffset];

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported palette format: %x", samplerID.ClutFmt());
		return 0;
	}
}

uint32_t TransformClutIndex(uint32_t index, const SamplerID &samplerID) {
	return gstate.transformClutIndex(index);
}

struct Nearest4 {
	alignas(16) u32 v[4];

	operator u32() const {
		return v[0];
	}
};

template <int N>
inline static Nearest4 SOFTRAST_CALL SampleNearest(const int u[N], const int v[N], const u8 *srcptr, int texbufw, int level, const SamplerID &samplerID) {
	Nearest4 res;
	if (!srcptr) {
		memset(res.v, 0, sizeof(res.v));
		return res;
	}

	// TODO: Should probably check if textures are aligned properly...

	switch (samplerID.TexFmt()) {
	case GE_TFMT_4444:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i], samplerID.swizzle);
			res.v[i] = RGBA4444ToRGBA8888(*(const u16 *)src);
		}
		return res;
	
	case GE_TFMT_5551:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i], samplerID.swizzle);
			res.v[i] = RGBA5551ToRGBA8888(*(const u16 *)src);
		}
		return res;

	case GE_TFMT_5650:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i], samplerID.swizzle);
			res.v[i] = RGB565ToRGBA8888(*(const u16 *)src);
		}
		return res;

	case GE_TFMT_8888:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<32>(texbufw, u[i], v[i], samplerID.swizzle);
			res.v[i] = *(const u32 *)src;
		}
		return res;

	case GE_TFMT_CLUT32:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<32>(texbufw, u[i], v[i], samplerID.swizzle);
			u32 val = src[0] + (src[1] << 8) + (src[2] << 16) + (src[3] << 24);
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), 0, samplerID);
		}
		return res;

	case GE_TFMT_CLUT16:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<16>(texbufw, u[i], v[i], samplerID.swizzle);
			u16 val = src[0] + (src[1] << 8);
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), 0, samplerID);
		}
		return res;

	case GE_TFMT_CLUT8:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<8>(texbufw, u[i], v[i], samplerID.swizzle);
			u8 val = *src;
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), 0, samplerID);
		}
		return res;

	case GE_TFMT_CLUT4:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<4>(texbufw, u[i], v[i], samplerID.swizzle);
			u8 val = (u[i] & 1) ? (src[0] >> 4) : (src[0] & 0xF);
			// Only CLUT4 uses separate mipmap palettes.
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), level, samplerID);
		}
		return res;

	case GE_TFMT_DXT1:
		for (int i = 0; i < N; ++i) {
			const DXT1Block *block = (const DXT1Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			res.v[i] = GetDXT1Texel(block, u[i] % 4, v[i] % 4);
		}
		return res;

	case GE_TFMT_DXT3:
		for (int i = 0; i < N; ++i) {
			const DXT3Block *block = (const DXT3Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			res.v[i] = GetDXT3Texel(block, u[i] % 4, v[i] % 4);
		}
		return res;

	case GE_TFMT_DXT5:
		for (int i = 0; i < N; ++i) {
			const DXT5Block *block = (const DXT5Block *)srcptr + (v[i] / 4) * (texbufw / 4) + (u[i] / 4);
			res.v[i] = GetDXT5Texel(block, u[i] % 4, v[i] % 4);
		}
		return res;

	default:
		ERROR_LOG_REPORT(G3D, "Software: Unsupported texture format: %x", samplerID.TexFmt());
		memset(res.v, 0, sizeof(res.v));
		return res;
	}
}

static inline int ClampUV(int v, int height) {
	if (v >= height - 1)
		return height - 1;
	else if (v < 0)
		return 0;
	return v;
}

static inline int WrapUV(int v, int height) {
	return v & (height - 1);
}

template <int N>
static inline void ApplyTexelClamp(int out_u[N], int out_v[N], const int u[N], const int v[N], int width, int height, const SamplerID &samplerID) {
	if (samplerID.clampS) {
		for (int i = 0; i < N; ++i) {
			out_u[i] = ClampUV(u[i], width);
		}
	} else {
		for (int i = 0; i < N; ++i) {
			out_u[i] = WrapUV(u[i], width);
		}
	}
	if (samplerID.clampT) {
		for (int i = 0; i < N; ++i) {
			out_v[i] = ClampUV(v[i], height);
		}
	} else {
		for (int i = 0; i < N; ++i) {
			out_v[i] = WrapUV(v[i], height);
		}
	}
}

static inline void GetTexelCoordinates(int level, float s, float t, int &out_u, int &out_v, int x, int y, const SamplerID &samplerID) {
	int width = gstate.getTextureWidth(level);
	int height = gstate.getTextureHeight(level);

	int base_u = (int)(s * width * 256.0f) + 12 - x;
	int base_v = (int)(t * height * 256.0f) + 12 - y;

	base_u >>= 8;
	base_v >>= 8;

	ApplyTexelClamp<1>(&out_u, &out_v, &base_u, &base_v, width, height, samplerID);
}

static Vec4IntResult SOFTRAST_CALL SampleNearest(float s, float t, int x, int y, Vec4IntArg prim_color, const u8 *const *tptr, const int *bufw, int level, int levelFrac, const SamplerID &samplerID) {
	int u, v;

	// Nearest filtering only.  Round texcoords.
	GetTexelCoordinates(level, s, t, u, v, x, y, samplerID);
	Vec4<int> c0 = Vec4<int>::FromRGBA(SampleNearest<1>(&u, &v, tptr[0], bufw[0], level, samplerID).v[0]);

	if (levelFrac) {
		GetTexelCoordinates(level + 1, s, t, u, v, x, y, samplerID);
		Vec4<int> c1 = Vec4<int>::FromRGBA(SampleNearest<1>(&u, &v, tptr[1], bufw[1], level + 1, samplerID).v[0]);

		c0 = (c1 * levelFrac + c0 * (16 - levelFrac)) / 16;
	}

	return GetTextureFunctionOutput(prim_color, ToVec4IntArg(c0), samplerID);
}

static Vec4IntResult SOFTRAST_CALL SampleFetch(int u, int v, const u8 *tptr, int bufw, int level, const SamplerID &samplerID) {
	Nearest4 c = SampleNearest<1>(&u, &v, tptr, bufw, level, samplerID);
	return ToVec4IntResult(Vec4<int>::FromRGBA(c.v[0]));
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexelClampQuad(bool clamp, Vec4IntArg vec, int width) {
	Vec4<int> result = vec;
#ifdef _M_SSE
	if (clamp) {
		// First, clamp to zero.
		__m128i negmask = _mm_cmpgt_epi32(_mm_setzero_si128(), result.ivec);
		result.ivec = _mm_andnot_si128(negmask, result.ivec);

		// Now the high bound.
		__m128i bound = _mm_set1_epi32(width - 1);
		__m128i goodmask = _mm_cmpgt_epi32(bound, result.ivec);
		// Clear the ones that were too high, then or in the high bound to those.
		result.ivec = _mm_and_si128(goodmask, result.ivec);
		result.ivec = _mm_or_si128(result.ivec, _mm_andnot_si128(goodmask, bound));
	} else {
		result.ivec = _mm_and_si128(result.ivec, _mm_set1_epi32(width - 1));
	}
#else
	if (clamp) {
		for (int i = 0; i < 4; ++i) {
			result[i] = ClampUV(result[i], width);
		}
	} else {
		for (int i = 0; i < 4; ++i) {
			result[i] = WrapUV(result[i], width);
		}
	}
#endif

	return ToVec4IntResult(result);
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexelClampQuadS(bool clamp, int u, int width) {
#ifdef _M_SSE
	__m128i uvec = _mm_add_epi32(_mm_set1_epi32(u), _mm_set_epi32(1, 0, 1, 0));
	return ApplyTexelClampQuad(clamp, uvec, width);
#else
	Vec4<int> result = Vec4<int>::AssignToAll(u) + Vec4<int>(0, 1, 0, 1);
	return ApplyTexelClampQuad(clamp, ToVec4IntArg(result), width);
#endif
}

static inline Vec4IntResult SOFTRAST_CALL ApplyTexelClampQuadT(bool clamp, int v, int height) {
#ifdef _M_SSE
	__m128i vvec = _mm_add_epi32(_mm_set1_epi32(v), _mm_set_epi32(1, 1, 0, 0));
	return ApplyTexelClampQuad(clamp, vvec, height);
#else
	Vec4<int> result = Vec4<int>::AssignToAll(v) + Vec4<int>(0, 0, 1, 1);
	return ApplyTexelClampQuad(clamp, ToVec4IntArg(result), height);
#endif
}

static inline Vec4IntResult SOFTRAST_CALL GetTexelCoordinatesQuadS(int level, float in_s, int &frac_u, int x, const SamplerID &samplerID) {
	int width = gstate.getTextureWidth(level);

	int base_u = (int)(in_s * width * 256) + 12 - x - 128;
	frac_u = (int)(base_u >> 4) & 0x0F;
	base_u >>= 8;

	// Need to generate and individually wrap/clamp the four sample coordinates. Ugh.
	return ApplyTexelClampQuadS(samplerID.clampS, base_u, width);
}

static inline Vec4IntResult SOFTRAST_CALL GetTexelCoordinatesQuadT(int level, float in_t, int &frac_v, int y, const SamplerID &samplerID) {
	int height = gstate.getTextureHeight(level);

	int base_v = (int)(in_t * height * 256) + 12 - y - 128;
	frac_v = (int)(base_v >> 4) & 0x0F;
	base_v >>= 8;

	// Need to generate and individually wrap/clamp the four sample coordinates. Ugh.
	return ApplyTexelClampQuadT(samplerID.clampT, base_v, height);
}

static Vec4IntResult SOFTRAST_CALL SampleLinearLevel(float s, float t, int x, int y, const u8 *const *tptr, const int *bufw, int texlevel, const SamplerID &samplerID) {
	int frac_u, frac_v;
	const Vec4<int> u = GetTexelCoordinatesQuadS(texlevel, s, frac_u, x, samplerID);
	const Vec4<int> v = GetTexelCoordinatesQuadT(texlevel, t, frac_v, y, samplerID);
	Nearest4 c = SampleNearest<4>(u.AsArray(), v.AsArray(), tptr[0], bufw[0], texlevel, samplerID);

	Vec4<int> texcolor_tl = Vec4<int>::FromRGBA(c.v[0]);
	Vec4<int> texcolor_tr = Vec4<int>::FromRGBA(c.v[1]);
	Vec4<int> texcolor_bl = Vec4<int>::FromRGBA(c.v[2]);
	Vec4<int> texcolor_br = Vec4<int>::FromRGBA(c.v[3]);
	Vec4<int> top = texcolor_tl * (0x10 - frac_u) + texcolor_tr * frac_u;
	Vec4<int> bot = texcolor_bl * (0x10 - frac_u) + texcolor_br * frac_u;
	return ToVec4IntResult((top * (0x10 - frac_v) + bot * frac_v) / (16 * 16));
}

static Vec4IntResult SOFTRAST_CALL SampleLinear(float s, float t, int x, int y, Vec4IntArg prim_color, const u8 *const *tptr, const int *bufw, int texlevel, int levelFrac, const SamplerID &samplerID) {
	Vec4<int> c0 = SampleLinearLevel(s, t, x, y, tptr, bufw, texlevel, samplerID);
	if (levelFrac) {
		const Vec4<int> c1 = SampleLinearLevel(s, t, x, y, tptr + 1, bufw + 1, texlevel + 1, samplerID);
		c0 = (c1 * levelFrac + c0 * (16 - levelFrac)) / 16;
	}
	return GetTextureFunctionOutput(prim_color, ToVec4IntArg(c0), samplerID);
}

};
