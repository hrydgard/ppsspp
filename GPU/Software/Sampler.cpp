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
#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/LogReporting.h"
#include "Common/Math/CrossSIMD.h"
#include "Common/Math/SIMDHeaders.h"
#include "Core/Config.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRegCache.h"
#include "GPU/Software/Sampler.h"

using namespace Math3D;
using namespace Rasterizer;

namespace Sampler {

static Vec4IntResult SOFTRAST_CALL SampleNearest(float s, float t, Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int level, int levelFrac, const SamplerID &samplerID);
static Vec4IntResult SOFTRAST_CALL SampleLinear(float s, float t, Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int level, int levelFrac, const SamplerID &samplerID);
static Vec4IntResult SOFTRAST_CALL SampleFetch(int u, int v, const u8 *tptr, int bufw, int level, const SamplerID &samplerID);

std::mutex jitCacheLock;
SamplerJitCache *jitCache = nullptr;

void Init() {
	jitCache = new SamplerJitCache();
}

void FlushJit() {
	jitCache->Flush();
}

int JitClearGeneration() {
	return SamplerJitCache::ClearGeneration();
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

NearestFunc GetNearestFunc(SamplerID id, BinManager *binner) {
	id.linear = false;
	NearestFunc jitted = jitCache->GetNearest(id, binner);
	if (jitted) {
		return jitted;
	}

	return &SampleNearest;
}

static LinearFunc GetLinearFallback(const SamplerID &id);

LinearFunc GetLinearFunc(SamplerID id, BinManager *binner) {
	id.linear = true;
	LinearFunc jitted = jitCache->GetLinear(id, binner);
	if (jitted) {
		return jitted;
	}

	return GetLinearFallback(id);
}

FetchFunc GetFetchFunc(SamplerID id, BinManager *binner) {
	id.fetch = true;
	FetchFunc jitted = jitCache->GetFetch(id, binner);
	if (jitted) {
		return jitted;
	}

	return &SampleFetch;
}

thread_local SamplerJitCache::LastCache SamplerJitCache::lastFetch_;
thread_local SamplerJitCache::LastCache SamplerJitCache::lastNearest_;
thread_local SamplerJitCache::LastCache SamplerJitCache::lastLinear_;
int SamplerJitCache::clearGen_ = 0;

// 256k should be enough.
SamplerJitCache::SamplerJitCache() : Rasterizer::CodeBlock(1024 * 64 * 4), cache_(64) {
	lastFetch_.gen = -1;
	lastNearest_.gen = -1;
	lastLinear_.gen = -1;
	clearGen_++;
}

void SamplerJitCache::Clear() {
	clearGen_++;
	CodeBlock::Clear();
	cache_.Clear();
	addresses_.clear();

	const10All16_ = nullptr;
	const10Low_ = nullptr;
	const10All8_ = nullptr;

	constWidthHeight16f_ = nullptr;
	constWidthMinus1i_ = nullptr;
	constHeightMinus1i_ = nullptr;

	constOnes32_ = nullptr;
	constOnes16_ = nullptr;
	constUNext_ = nullptr;
	constVNext_ = nullptr;

	const5551Swizzle_ = nullptr;
	const5650Swizzle_ = nullptr;
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
	}

	return CodeBlock::DescribeCodePtr(ptr);
}

void SamplerJitCache::Flush() {
	std::unique_lock<std::mutex> guard(jitCacheLock);
	for (const auto &queued : compileQueue_) {
		// Might've been compiled after enqueue, but before now.
		size_t queuedKey = std::hash<SamplerID>()(queued);
		if (!cache_.ContainsKey(queuedKey))
			Compile(queued);
	}
	compileQueue_.clear();
}

// Without a backend nothing ever compiles, and a lookup would flush the binner for nothing.
#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
static constexpr bool HAS_SAMPLER_JIT = true;
#else
static constexpr bool HAS_SAMPLER_JIT = false;
#endif

// A texture level whose address isn't valid has no pointer, and the generic samplers read its texels as zero
// (then apply the texture function). The JIT leaves those to them.
static bool CanJit(const SamplerID &id) {
	return HAS_SAMPLER_JIT && g_Config.bSoftwareRenderingJit && !id.hasInvalidPtr;
}

NearestFunc SamplerJitCache::GetByID(const SamplerID &id, size_t key, BinManager *binner) {
	std::unique_lock<std::mutex> guard(jitCacheLock);
	
	NearestFunc func;
	if (cache_.Get(key, &func)) {
		return func;
	}

	if (!binner) {
		// Can't compile, let's try to do it later when there's an opportunity.
		compileQueue_.insert(id);
		return nullptr;
	}

	guard.unlock();
	binner->Flush("compile");
	guard.lock();

	for (const auto &queued : compileQueue_) {
		// Might've been compiled after enqueue, but before now.
		size_t queuedKey = std::hash<SamplerID>()(queued);
		if (!cache_.ContainsKey(queuedKey))
			Compile(queued);
	}
	compileQueue_.clear();

	if (!cache_.ContainsKey(key))
		Compile(id);

	// Okay, should be there now.
	if (cache_.Get(key, &func)) {
		return func;
	} else {
		return nullptr;
	}
}

NearestFunc SamplerJitCache::GetNearest(const SamplerID &id, BinManager *binner) {
	if (!CanJit(id))
		return nullptr;

	const size_t key = std::hash<SamplerID>()(id);
	if (lastNearest_.Match(key, clearGen_))
		return (NearestFunc)lastNearest_.func;

	auto func = GetByID(id, key, binner);
	lastNearest_.Set(key, func, clearGen_);
	return (NearestFunc)func;
}

LinearFunc SamplerJitCache::GetLinear(const SamplerID &id, BinManager *binner) {
	if (!CanJit(id))
		return nullptr;

	const size_t key = std::hash<SamplerID>()(id);
	if (lastLinear_.Match(key, clearGen_))
		return (LinearFunc)lastLinear_.func;

	auto func = GetByID(id, key, binner);
	lastLinear_.Set(key, func, clearGen_);
	return (LinearFunc)func;
}

FetchFunc SamplerJitCache::GetFetch(const SamplerID &id, BinManager *binner) {
	if (!CanJit(id))
		return nullptr;

	const size_t key = std::hash<SamplerID>()(id);
	if (lastFetch_.Match(key, clearGen_))
		return (FetchFunc)lastFetch_.func;

	auto func = GetByID(id, key, binner);
	lastFetch_.Set(key, func, clearGen_);
	return (FetchFunc)func;
}

void SamplerJitCache::Compile(const SamplerID &id) {
	// This should be sufficient.
	if (GetSpaceLeft() < 16384) {
		Clear();
	}

	// We compile them together so the cache can't possibly be cleared in between.
	// We might vary between nearest and linear, so we can't clear between.
#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	SamplerID fetchID = id;
	fetchID.linear = false;
	fetchID.fetch = true;
	addresses_[fetchID] = GetCodePointer();
	cache_.Insert(std::hash<SamplerID>()(fetchID), (NearestFunc)CompileFetch(fetchID));

	SamplerID nearestID = id;
	nearestID.linear = false;
	nearestID.fetch = false;
	addresses_[nearestID] = GetCodePointer();
	cache_.Insert(std::hash<SamplerID>()(nearestID), (NearestFunc)CompileNearest(nearestID));

	SamplerID linearID = id;
	linearID.linear = true;
	linearID.fetch = false;
	addresses_[linearID] = GetCodePointer();
	cache_.Insert(std::hash<SamplerID>()(linearID), (NearestFunc)CompileLinear(linearID));
#endif
}

template <uint32_t texel_size_bits>
static inline int GetPixelDataOffset(uint32_t row_pitch_pixels, uint32_t u, uint32_t v, bool swizzled) {
	if (!swizzled)
		return (v * (row_pitch_pixels * texel_size_bits >> 3)) + (u * texel_size_bits >> 3);

	constexpr uint32_t tile_size_bits = 32;
	constexpr uint32_t tiles_in_block_horizontal = 4;
	constexpr uint32_t tiles_in_block_vertical = 8;

	constexpr uint32_t texels_per_tile = tile_size_bits / texel_size_bits;
	uint32_t tile_u = u / texels_per_tile;
	uint32_t tile_idx = (v % tiles_in_block_vertical) * (tiles_in_block_horizontal) +
	// TODO: not sure if the *texel_size_bits/8 factor is correct
					(v / tiles_in_block_vertical) * ((row_pitch_pixels*texel_size_bits/(tile_size_bits))*tiles_in_block_vertical) +
					(tile_u % tiles_in_block_horizontal) +
					(tile_u / tiles_in_block_horizontal) * (tiles_in_block_horizontal*tiles_in_block_vertical);

	return tile_idx * (tile_size_bits / 8) + ((u % texels_per_tile) * texel_size_bits) / 8;
}

static inline u32 LookupColor(unsigned int index, unsigned int level, const SamplerID &samplerID) {
	int clutSharingOffset = 0;
	if (!samplerID.useSharedClut)
		clutSharingOffset = samplerID.TexFmt() == GE_TFMT_CLUT4 ? level * 16 : (level & 1) * 256;

	switch (samplerID.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		return RGB565ToRGBA8888(samplerID.cached.clut16[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR5551:
		return RGBA5551ToRGBA8888(samplerID.cached.clut16[index + clutSharingOffset]);

	case GE_CMODE_16BIT_ABGR4444:
		return RGBA4444ToRGBA8888(samplerID.cached.clut16[index + clutSharingOffset]);

	case GE_CMODE_32BIT_ABGR8888:
		return samplerID.cached.clut32[index + clutSharingOffset];

	default:
		ERROR_LOG_REPORT(Log::G3D, "Software: Unsupported palette format: %x", samplerID.ClutFmt());
		return 0;
	}
}

uint32_t TransformClutIndex(uint32_t index, const SamplerID &samplerID) {
	if (samplerID.hasClutShift || samplerID.hasClutMask || samplerID.hasClutOffset) {
		const uint8_t shift = (samplerID.cached.clutFormat >> 2) & 0x1F;
		const uint8_t mask = (samplerID.cached.clutFormat >> 8) & 0xFF;
		const uint16_t offset = ((samplerID.cached.clutFormat >> 16) & 0x1F) << 4;
		// We need to wrap any entries beyond the first 1024 bytes.
		const uint16_t offsetMask = samplerID.ClutFmt() == GE_CMODE_32BIT_ABGR8888 ? 0xFF : 0x1FF;

		return ((index >> shift) & mask) | (offset & offsetMask);
	}
	return index & 0xFF;
}

struct Nearest4 {
	alignas(16) u32 v[4];

	operator u32() const {
		return v[0];
	}
};

template <int N>
inline static Nearest4 SOFTRAST_CALL SampleNearest(const int u[N], const int v[N], const u8 *srcptr, uint16_t texbufw, int level, const SamplerID &samplerID) {
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
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), level, samplerID);
		}
		return res;

	case GE_TFMT_CLUT4:
		for (int i = 0; i < N; ++i) {
			const u8 *src = srcptr + GetPixelDataOffset<4>(texbufw, u[i], v[i], samplerID.swizzle);
			u8 val = (u[i] & 1) ? (src[0] >> 4) : (src[0] & 0xF);
			res.v[i] = LookupColor(TransformClutIndex(val, samplerID), level, samplerID);
		}
		return res;

	case GE_TFMT_DXT1:
		for (int i = 0; i < N; ++i) {
			const DXT1Block *block = (const DXT1Block *)srcptr + (v[i] >> 2) * (texbufw >> 2) + (u[i] >> 2);
			res.v[i] = GetDXT1Texel(block, u[i] & 3, v[i] & 3);
		}
		return res;

	case GE_TFMT_DXT3:
		for (int i = 0; i < N; ++i) {
			const DXT3Block *block = (const DXT3Block *)srcptr + (v[i] >> 2) * (texbufw >> 2) + (u[i] >> 2);
			res.v[i] = GetDXT3Texel(block, u[i] & 3, v[i] & 3);
		}
		return res;

	case GE_TFMT_DXT5:
		for (int i = 0; i < N; ++i) {
			const DXT5Block *block = (const DXT5Block *)srcptr + (v[i] >> 2) * (texbufw >> 2) + (u[i] >> 2);
			res.v[i] = GetDXT5Texel(block, u[i] & 3, v[i] & 3);
		}
		return res;

	default:
		ERROR_LOG_REPORT(Log::G3D, "Software: Unsupported texture format: %x", samplerID.TexFmt());
		memset(res.v, 0, sizeof(res.v));
		return res;
	}
}

static inline int ClampUV(int v, int height) {
	if (v >= height - 1)
		return height - 1;
	if (v >= 511)
		return 511;
	else if (v < 0)
		return 0;
	return v;
}

static inline int WrapUV(int v, int height) {
	return v & (height - 1) & 511;
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

// A texel coordinate in 1/16 texel, truncated. Out of int range it's INT_MIN, as the x86 sampler JIT's
// conversion gives (gpu/probe exp5: s >= 2^18 on a 512 wide repeating texture reads texel 0 on a PSP).
static inline int TexelFixed(float f) {
	return f >= -2147483648.0f && f < 2147483648.0f ? (int)f : INT_MIN;
}

static inline void GetTexelCoordinates(int level, float s, float t, int &out_u, int &out_v, const SamplerID &samplerID) {
	int width = samplerID.cached.sizes[level].w;
	int height = samplerID.cached.sizes[level].h;

	// The GE truncates to 1/16 texel, toward zero (gpu/probe exp57).
	int base_u = TexelFixed(s * width * 16.0f);
	int base_v = TexelFixed(t * height * 16.0f);

	base_u >>= 4;
	base_v >>= 4;

	ApplyTexelClamp<1>(&out_u, &out_v, &base_u, &base_v, width, height, samplerID);
}

Vec4IntResult SOFTRAST_CALL GetTextureFunctionOutput(Vec4IntArg prim_color_in, Vec4IntArg texcolor_in, const SamplerID &samplerID) {
	const Vec4<int> prim_color = prim_color_in;
	const Vec4<int> texcolor = texcolor_in;

	Vec3<int> out_rgb;
	int out_a;

	bool rgba = samplerID.useTextureAlpha;

	switch (samplerID.TexFunc()) {
	case GE_TEXFUNC_MODULATE:
	{
#if defined(_M_SSE)
		// Modulate weights slightly on the tex color, by adding one to prim and dividing by 256.
		const __m128i p = _mm_slli_epi16(_mm_packs_epi32(prim_color.ivec, prim_color.ivec), 4);
		const __m128i pboost = _mm_add_epi16(p, _mm_set1_epi16(1 << 4));
		__m128i t = _mm_slli_epi16(_mm_packs_epi32(texcolor.ivec, texcolor.ivec), 4);
		if (samplerID.useColorDoubling) {
			const __m128i amask = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
			const __m128i a = _mm_and_si128(t, amask);
			const __m128i rgb = _mm_andnot_si128(amask, t);
			t = _mm_or_si128(_mm_slli_epi16(rgb, 1), a);
		}
		const __m128i b = _mm_mulhi_epi16(pboost, t);
		out_rgb.ivec = _mm_unpacklo_epi16(b, _mm_setzero_si128());

		if (rgba) {
			return ToVec4IntResult(Vec4<int>(out_rgb.ivec));
		} else {
			out_a = prim_color.a();
		}
#elif PPSSPP_ARCH(ARM64_NEON)
		int32x4_t pboost = vaddq_s32(prim_color.ivec, vdupq_n_s32(1));
		int32x4_t t = texcolor.ivec;
		if (samplerID.useColorDoubling) {
			static const int32_t rgbDouble[4] = { 1, 1, 1, 0 };
			t = vshlq_s32(t, vld1q_s32(rgbDouble));
		}
		out_rgb.ivec = vshrq_n_s32(vmulq_s32(pboost, t), 8);

		if (rgba) {
			return ToVec4IntResult(Vec4<int>(out_rgb.ivec));
		}
		out_a = prim_color.a();
#else
		if (samplerID.useColorDoubling) {
			out_rgb = ((prim_color.rgb() + Vec3<int>::AssignToAll(1)) * texcolor.rgb() * 2) / 256;
		} else {
			out_rgb = (prim_color.rgb() + Vec3<int>::AssignToAll(1)) * texcolor.rgb() / 256;
		}
		out_a = (rgba) ? ((prim_color.a() + 1) * texcolor.a() / 256) : prim_color.a();
#endif
		break;
	}

	case GE_TEXFUNC_DECAL:
		if (rgba) {
			int t = texcolor.a();
			int invt = 255 - t;
			// Both colors are boosted here, making the alpha have more weight.
			Vec3<int> one = Vec3<int>::AssignToAll(1);
			out_rgb = ((prim_color.rgb() + one) * invt + (texcolor.rgb() + one) * t);
			// Keep the bits of accuracy when doubling.
			if (samplerID.useColorDoubling)
				out_rgb /= 128;
			else
				out_rgb /= 256;
		} else {
			if (samplerID.useColorDoubling)
				out_rgb = texcolor.rgb() * 2;
			else
				out_rgb = texcolor.rgb();
		}
		out_a = prim_color.a();
		break;

	case GE_TEXFUNC_BLEND:
	{
		constexpr Vec3<int> const255(255, 255, 255);
		const Vec3<int> texenv = Vec3<int>::FromRGB(samplerID.cached.texBlendColor);

		// Unlike the others (and even alpha), this one simply always rounds up.
		constexpr Vec3<int> roundup = Vec3<int>::AssignToAll(255);
		out_rgb = ((const255 - texcolor.rgb()) * prim_color.rgb() + texcolor.rgb() * texenv + roundup);
		// Must divide by less to keep the precision for doubling to be accurate.
		if (samplerID.useColorDoubling)
			out_rgb /= 128;
		else
			out_rgb /= 256;

		out_a = (rgba) ? ((prim_color.a() + 1) * texcolor.a() / 256) : prim_color.a();
		break;
	}

	case GE_TEXFUNC_REPLACE:
		out_rgb = texcolor.rgb();
		// Doubling even happens for replace.
		if (samplerID.useColorDoubling)
			out_rgb *= 2;
		out_a = (rgba) ? texcolor.a() : prim_color.a();
		break;

	case GE_TEXFUNC_ADD:
	case GE_TEXFUNC_UNKNOWN1:
	case GE_TEXFUNC_UNKNOWN2:
	case GE_TEXFUNC_UNKNOWN3:
		// Don't need to clamp afterward, we always clamp before tests.
		out_rgb = prim_color.rgb() + texcolor.rgb();
		if (samplerID.useColorDoubling)
			out_rgb *= 2;

		// Alpha is still blended the common way.
		out_a = (rgba) ? ((prim_color.a() + 1) * texcolor.a() / 256) : prim_color.a();
		break;
	}

	return ToVec4IntResult(Vec4<int>(out_rgb, out_a));
}

static Vec4IntResult SOFTRAST_CALL SampleNearest(float s, float t, Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int level, int levelFrac, const SamplerID &samplerID) {
	int u, v;

	// Nearest filtering only.  Round texcoords.
	GetTexelCoordinates(level, s, t, u, v, samplerID);
	Vec4<int> c0 = Vec4<int>::FromRGBA(SampleNearest<1>(&u, &v, tptr[0], bufw[0], level, samplerID).v[0]);

	if (levelFrac) {
		GetTexelCoordinates(level + 1, s, t, u, v, samplerID);
		Vec4<int> c1 = Vec4<int>::FromRGBA(SampleNearest<1>(&u, &v, tptr[1], bufw[1], level + 1, samplerID).v[0]);

		c0 = (c1 * levelFrac + c0 * (16 - levelFrac)) >> 4;
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
		__m128i bound = _mm_set1_epi32(width > 512 ? 511 : width - 1);
		__m128i goodmask = _mm_cmpgt_epi32(bound, result.ivec);
		// Clear the ones that were too high, then or in the high bound to those.
		result.ivec = _mm_and_si128(goodmask, result.ivec);
		result.ivec = _mm_or_si128(result.ivec, _mm_andnot_si128(goodmask, bound));
	} else {
		result.ivec = _mm_and_si128(result.ivec, _mm_set1_epi32((width - 1) & 511));
	}
#elif PPSSPP_ARCH(ARM64_NEON)
	if (clamp) {
		// Let's start by clamping to the maximum.
		result.ivec = vminq_s32(result.ivec, vdupq_n_s32(width > 512 ? 511 : width - 1));
		// And then to zero.
		result.ivec = vmaxq_s32(result.ivec, vdupq_n_s32(0));
	} else {
		result.ivec = vandq_s32(result.ivec, vdupq_n_s32((width - 1) & 511));
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
#elif PPSSPP_ARCH(ARM64_NEON)
	static const int32_t u2[4] = { 0, 1, 0, 1 };
	int32x4_t uvec = vaddq_s32(vdupq_n_s32(u), vld1q_s32(u2));
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
#elif PPSSPP_ARCH(ARM64_NEON)
	static const int32_t v2[4] = { 0, 0, 1, 1 };
	int32x4_t vvec = vaddq_s32(vdupq_n_s32(v), vld1q_s32(v2));
	return ApplyTexelClampQuad(clamp, vvec, height);
#else
	Vec4<int> result = Vec4<int>::AssignToAll(v) + Vec4<int>(0, 0, 1, 1);
	return ApplyTexelClampQuad(clamp, ToVec4IntArg(result), height);
#endif
}

static inline Vec4IntResult SOFTRAST_CALL GetTexelCoordinatesQuadS(int level, float in_s, int &frac_u, const SamplerID &samplerID) {
	int width = samplerID.cached.sizes[level].w;

	// The GE truncates to 1/16 texel, toward zero (gpu/probe exp57).
	int base_u = TexelFixed(in_s * width * 16) - 8;
	frac_u = base_u & 0x0F;
	base_u >>= 4;

	// Need to generate and individually wrap/clamp the four sample coordinates. Ugh.
	return ApplyTexelClampQuadS(samplerID.clampS, base_u, width);
}

static inline Vec4IntResult SOFTRAST_CALL GetTexelCoordinatesQuadT(int level, float in_t, int &frac_v, const SamplerID &samplerID) {
	int height = samplerID.cached.sizes[level].h;

	int base_v = TexelFixed(in_t * height * 16) - 8;
	frac_v = base_v & 0x0F;
	base_v >>= 4;

	// Need to generate and individually wrap/clamp the four sample coordinates. Ugh.
	return ApplyTexelClampQuadT(samplerID.clampT, base_v, height);
}

static Vec4IntResult SOFTRAST_CALL SampleLinearLevel(float s, float t, const u8 *const *tptr, const uint16_t *bufw, int texlevel, const SamplerID &samplerID) {
	int frac_u, frac_v;
	const Vec4<int> u = GetTexelCoordinatesQuadS(texlevel, s, frac_u, samplerID);
	const Vec4<int> v = GetTexelCoordinatesQuadT(texlevel, t, frac_v, samplerID);
	Nearest4 c = SampleNearest<4>(u.AsArray(), v.AsArray(), tptr[0], bufw[0], texlevel, samplerID);
#ifdef _M_SSE
	__m128i zero = _mm_setzero_si128();
	__m128i samples = _mm_loadu_si128((const __m128i*)(c.v));
	__m128i top = _mm_unpacklo_epi8(samples, zero);
	__m128i bot = _mm_unpackhi_epi8(samples, zero);
	// I just a want reasonably efficient
	// __m128i mul_u = _mm_setr_epi16(0x10 - frac_u, 0x10 - frac_u, 0x10 - frac_u, 0x10 - frac_u, frac_u, frac_u, frac_u, frac_u);
	// GCC/clang do something decent for that, MSVC - not so much.
	// Hence this. (0x10 - frac_u) is expressed as (frac_u ^ 0xF) + 1,
	// which REQUIRES 0 <= frac_u < 0x10.
	__m128i mul_u =	_mm_set1_epi16(frac_u);
	mul_u = _mm_xor_si128(mul_u, _mm_setr_epi16(0xF, 0xF, 0xF, 0xF, 0x0, 0x0, 0x0, 0x0));
	mul_u = _mm_add_epi16(mul_u, _mm_setr_epi16(0x1, 0x1, 0x1, 0x1, 0x0, 0x0, 0x0, 0x0));
	// Like the GE: horizontal lerps truncated to 8 bits, then the vertical one (gpu/probe exp52).
	top = _mm_mullo_epi16(top, mul_u);
	bot = _mm_mullo_epi16(bot, mul_u);
	top = _mm_srli_epi16(_mm_add_epi16(top, _mm_shuffle_epi32(top, _MM_SHUFFLE(3, 2, 3, 2))), 4);
	bot = _mm_srli_epi16(_mm_add_epi16(bot, _mm_shuffle_epi32(bot, _MM_SHUFFLE(3, 2, 3, 2))), 4);
	top = _mm_mullo_epi16(top, _mm_set1_epi16(0x10 - frac_v));
	bot = _mm_mullo_epi16(bot, _mm_set1_epi16(frac_v));
	__m128i sum = _mm_srli_epi16(_mm_add_epi16(top, bot), 4);
	sum = _mm_unpacklo_epi16(sum, zero);
	return sum;
#else
	Vec4<int> texcolor_tl = Vec4<int>::FromRGBA(c.v[0]);
	Vec4<int> texcolor_tr = Vec4<int>::FromRGBA(c.v[1]);
	Vec4<int> texcolor_bl = Vec4<int>::FromRGBA(c.v[2]);
	Vec4<int> texcolor_br = Vec4<int>::FromRGBA(c.v[3]);
	// Like the GE: horizontal lerps truncated to 8 bits, then the vertical one (gpu/probe exp52).
	Vec4<int> top = (texcolor_tl * (0x10 - frac_u) + texcolor_tr * frac_u) >> 4;
	Vec4<int> bot = (texcolor_bl * (0x10 - frac_u) + texcolor_br * frac_u) >> 4;
	return ToVec4IntResult((top * (0x10 - frac_v) + bot * frac_v) >> 4);
#endif
}

static Vec4IntResult SOFTRAST_CALL SampleLinear(float s, float t, Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int texlevel, int levelFrac, const SamplerID &samplerID) {
	Vec4<int> c0 = SampleLinearLevel(s, t, tptr, bufw, texlevel, samplerID);
	if (levelFrac) {
		const Vec4<int> c1 = SampleLinearLevel(s, t, tptr + 1, bufw + 1, texlevel + 1, samplerID);
		c0 = (c1 * levelFrac + c0 * (16 - levelFrac)) >> 4;
	}
	return GetTextureFunctionOutput(prim_color, ToVec4IntArg(c0), samplerID);
}

// Where there's no JIT: SampleLinear specialized for the texture format, swizzling and CLUT format, so
// those aren't checked per texel. The filter works on all four channels at once, as 16-bit lanes of a
// uint64_t (a channel times a weight of at most 16 fits).
static inline uint64_t SpreadRGBA(uint32_t c) {
	uint64_t x = c;
	x = (x | (x << 16)) & 0x0000FFFF0000FFFFULL;
	return (x | (x << 8)) & 0x00FF00FF00FF00FFULL;
}

// (a * (16 - f) + b * f) >> 4 per channel.
static inline uint64_t LerpSpread(uint64_t a, uint64_t b, int f) {
	return ((a * (uint64_t)(16 - f) + b * (uint64_t)f) >> 4) & 0x00FF00FF00FF00FFULL;
}

template <GEPaletteFormat clutFmt>
static inline uint32_t LookupClutT(uint32_t index, const SamplerID &samplerID) {
	switch (clutFmt) {
	case GE_CMODE_16BIT_BGR5650: return RGB565ToRGBA8888(samplerID.cached.clut16[index]);
	case GE_CMODE_16BIT_ABGR5551: return RGBA5551ToRGBA8888(samplerID.cached.clut16[index]);
	case GE_CMODE_16BIT_ABGR4444: return RGBA4444ToRGBA8888(samplerID.cached.clut16[index]);
	default: return samplerID.cached.clut32[index];
	}
}

template <GETextureFormat fmt>
static constexpr uint32_t TexelBitsT() {
	switch (fmt) {
	case GE_TFMT_8888: case GE_TFMT_CLUT32: return 32;
	case GE_TFMT_CLUT8: return 8;
	case GE_TFMT_CLUT4: return 4;
	default: return 16;
	}
}

// A texel's byte offset is the sum of one for its row and one for its column (as GetPixelDataOffset), so
// the four texels of a bilinear sample only need two of each.
template <uint32_t bits, bool swizzled>
static inline uint32_t RowOffsetT(int bufw, int v) {
	if (!swizzled)
		return v * (bufw * bits >> 3);
	// Blocks of 16 bytes by 8 rows.
	return (v >> 3) * ((bufw * bits / 32) * 32) + (v & 7) * 16;
}

template <uint32_t bits, bool swizzled>
static inline uint32_t ColumnOffsetT(int u) {
	const uint32_t b = u * bits >> 3;
	return swizzled ? (b >> 4) * 128 + (b & 15) : b;
}

// The texel at p (u picks the nibble of CLUT4). clutOffset is where the level's palette starts.
template <GETextureFormat fmt, GEPaletteFormat clutFmt>
static inline uint32_t ReadTexelT(const u8 *p, int u, uint32_t clutOffset, const SamplerID &samplerID) {
	switch (fmt) {
	case GE_TFMT_4444: return RGBA4444ToRGBA8888(*(const u16 *)p);
	case GE_TFMT_5551: return RGBA5551ToRGBA8888(*(const u16 *)p);
	case GE_TFMT_5650: return RGB565ToRGBA8888(*(const u16 *)p);
	case GE_TFMT_8888: return *(const u32 *)p;
	case GE_TFMT_CLUT32: return LookupClutT<clutFmt>(TransformClutIndex(p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24), samplerID) + clutOffset, samplerID);
	case GE_TFMT_CLUT16: return LookupClutT<clutFmt>(TransformClutIndex(p[0] + (p[1] << 8), samplerID) + clutOffset, samplerID);
	case GE_TFMT_CLUT8: return LookupClutT<clutFmt>(TransformClutIndex(*p, samplerID) + clutOffset, samplerID);
	case GE_TFMT_CLUT4: return LookupClutT<clutFmt>(TransformClutIndex((u & 1) ? (*p >> 4) : (*p & 0xF), samplerID) + clutOffset, samplerID);
	default: return 0;
	}
}

template <GETextureFormat fmt>
static inline uint32_t ReadDXTTexelT(const u8 *src, int bufw, int u, int v) {
	switch (fmt) {
	case GE_TFMT_DXT1: return GetDXT1Texel((const DXT1Block *)src + (v >> 2) * (bufw >> 2) + (u >> 2), u & 3, v & 3);
	case GE_TFMT_DXT3: return GetDXT3Texel((const DXT3Block *)src + (v >> 2) * (bufw >> 2) + (u >> 2), u & 3, v & 3);
	default: return GetDXT5Texel((const DXT5Block *)src + (v >> 2) * (bufw >> 2) + (u >> 2), u & 3, v & 3);
	}
}

// The two texel coordinates along one axis (as ApplyTexelClampQuad wraps or clamps them) and the fraction.
static inline void TexelPairT(float st, int size, bool clamp, int &c0, int &c1, int &frac) {
	int base = TexelFixed(st * size * 16) - 8;
	frac = base & 0x0F;
	base >>= 4;
	if (clamp) {
		const int hi = size > 512 ? 511 : size - 1;
		c0 = std::max(std::min(base, hi), 0);
		c1 = std::max(std::min(base + 1, hi), 0);
	} else {
		const int mask = (size - 1) & 511;
		c0 = base & mask;
		c1 = (base + 1) & mask;
	}
}

template <GETextureFormat fmt, bool swizzled, GEPaletteFormat clutFmt>
static inline uint64_t SampleLinearLevelT(float s, float t, const u8 *tptr, int bufw, int level, const SamplerID &samplerID) {
	if (!tptr)
		return 0;
	int u0, u1, v0, v1, fracU, fracV;
	TexelPairT(s, samplerID.cached.sizes[level].w, samplerID.clampS, u0, u1, fracU);
	TexelPairT(t, samplerID.cached.sizes[level].h, samplerID.clampT, v0, v1, fracV);
	uint64_t tl, tr, bl, br;
	if constexpr (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5) {
		tl = SpreadRGBA(ReadDXTTexelT<fmt>(tptr, bufw, u0, v0));
		tr = SpreadRGBA(ReadDXTTexelT<fmt>(tptr, bufw, u1, v0));
		bl = SpreadRGBA(ReadDXTTexelT<fmt>(tptr, bufw, u0, v1));
		br = SpreadRGBA(ReadDXTTexelT<fmt>(tptr, bufw, u1, v1));
	} else {
		constexpr uint32_t bits = TexelBitsT<fmt>();
		// Levels past the first have their own palette unless it's shared: 16 entries on for CLUT4, 256 on for
		// CLUT8 at odd levels.
		uint32_t clutOffset = 0;
		if ((fmt == GE_TFMT_CLUT4 || fmt == GE_TFMT_CLUT8) && !samplerID.useSharedClut)
			clutOffset = fmt == GE_TFMT_CLUT4 ? level * 16 : (level & 1) * 256;
		const u8 *row0 = tptr + RowOffsetT<bits, swizzled>(bufw, v0);
		const u8 *row1 = tptr + RowOffsetT<bits, swizzled>(bufw, v1);
		const uint32_t col0 = ColumnOffsetT<bits, swizzled>(u0);
		const uint32_t col1 = ColumnOffsetT<bits, swizzled>(u1);
		tl = SpreadRGBA(ReadTexelT<fmt, clutFmt>(row0 + col0, u0, clutOffset, samplerID));
		tr = SpreadRGBA(ReadTexelT<fmt, clutFmt>(row0 + col1, u1, clutOffset, samplerID));
		bl = SpreadRGBA(ReadTexelT<fmt, clutFmt>(row1 + col0, u0, clutOffset, samplerID));
		br = SpreadRGBA(ReadTexelT<fmt, clutFmt>(row1 + col1, u1, clutOffset, samplerID));
	}
	// Like the GE: horizontal lerps truncated to 8 bits, then the vertical one (gpu/probe exp52).
	return LerpSpread(LerpSpread(tl, tr, fracU), LerpSpread(bl, br, fracU), fracV);
}

template <GETextureFormat fmt, bool swizzled, GEPaletteFormat clutFmt>
static Vec4IntResult SOFTRAST_CALL SampleLinearT(float s, float t, Vec4IntArg prim_color, const u8 *const *tptr, const uint16_t *bufw, int texlevel, int levelFrac, const SamplerID &samplerID) {
	uint64_t c = SampleLinearLevelT<fmt, swizzled, clutFmt>(s, t, tptr[0], bufw[0], texlevel, samplerID);
	if (levelFrac) {
		const uint64_t c1 = SampleLinearLevelT<fmt, swizzled, clutFmt>(s, t, tptr[1], bufw[1], texlevel + 1, samplerID);
		c = LerpSpread(c, c1, levelFrac);
	}
	const Vec4<int> texcolor((int)(c & 0xFFFF), (int)((c >> 16) & 0xFFFF), (int)((c >> 32) & 0xFFFF), (int)(c >> 48));
	return GetTextureFunctionOutput(prim_color, ToVec4IntArg(texcolor), samplerID);
}

// TexelPairT for four pixels at once.
static inline void TexelPairs4(Vec4F32 st, int size, bool clamp, Vec4S32 &c0, Vec4S32 &c1, Vec4S32 &frac) {
	const Vec4F32 f = st * Vec4F32::Splat((float)size) * Vec4F32::Splat(16.0f);
	// TexelFixed: INT_MIN out of int range (and for NaN). Those lanes are zeroed before the conversion.
	const Vec4S32 inRange = f.CompareLt(Vec4F32::Splat(2147483648.0f)).AndNot(f.CompareLt(Vec4F32::Splat(-2147483648.0f)));
	const Vec4S32 fixed = Vec4S32FromF32(f & inRange) | Vec4S32::Splat(INT_MIN).AndNot(inRange);
	const Vec4S32 base = fixed - Vec4S32::Splat(8);
	frac = base & Vec4S32::Splat(0x0F);
	const Vec4S32 b0 = base.Shr<4>();
	const Vec4S32 b1 = b0 + Vec4S32::Splat(1);
	if (clamp) {
		const Vec4S32 hi = Vec4S32::Splat(size > 512 ? 511 : size - 1);
		c0 = b0.Min(hi).Max(Vec4S32::Zero());
		c1 = b1.Min(hi).Max(Vec4S32::Zero());
	} else {
		const Vec4S32 mask = Vec4S32::Splat((size - 1) & 511);
		c0 = b0 & mask;
		c1 = b1 & mask;
	}
}

// RowOffsetT and ColumnOffsetT for four texels at once.
template <uint32_t bits, bool swizzled>
static inline Vec4S32 RowOffsets4(int bufw, Vec4S32 v) {
	if (!swizzled)
		return v.Mul(Vec4S32::Splat(bufw * bits >> 3));
	return v.Shr<3>().Mul(Vec4S32::Splat((bufw * bits / 32) * 32)) + (v & Vec4S32::Splat(7)).Shl<4>();
}

template <uint32_t bits, bool swizzled>
static inline Vec4S32 ColumnOffsets4(Vec4S32 u) {
	Vec4S32 b;
	switch (bits) {
	case 32: b = u.Shl<2>(); break;
	case 16: b = u.Shl<1>(); break;
	case 8: b = u; break;
	default: b = u.Shr<1>(); break;
	}
	return swizzled ? b.Shr<4>().Shl<7>() + (b & Vec4S32::Splat(15)) : b;
}

// SampleLinearLevelT for four pixels at the same level: the coordinates and offsets in vector lanes, the
// texel reads one by one.
template <GETextureFormat fmt, bool swizzled, GEPaletteFormat clutFmt>
static inline void SampleLinearLevel4T(Vec4F32 s, Vec4F32 t, const u8 *tptr, int bufw, int level, const SamplerID &samplerID, uint64_t out[4]) {
	if (!tptr) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}
	Vec4S32 u0, u1, v0, v1, fracU, fracV;
	TexelPairs4(s, samplerID.cached.sizes[level].w, samplerID.clampS, u0, u1, fracU);
	TexelPairs4(t, samplerID.cached.sizes[level].h, samplerID.clampT, v0, v1, fracV);
	alignas(16) int us0[4], us1[4], fu[4], fv[4];
	u0.Store(us0);
	u1.Store(us1);
	fracU.Store(fu);
	fracV.Store(fv);
	uint32_t texels[4][4];
	if constexpr (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5) {
		alignas(16) int vs0[4], vs1[4];
		v0.Store(vs0);
		v1.Store(vs1);
		for (int i = 0; i < 4; ++i) {
			texels[i][0] = ReadDXTTexelT<fmt>(tptr, bufw, us0[i], vs0[i]);
			texels[i][1] = ReadDXTTexelT<fmt>(tptr, bufw, us1[i], vs0[i]);
			texels[i][2] = ReadDXTTexelT<fmt>(tptr, bufw, us0[i], vs1[i]);
			texels[i][3] = ReadDXTTexelT<fmt>(tptr, bufw, us1[i], vs1[i]);
		}
	} else {
		constexpr uint32_t bits = TexelBitsT<fmt>();
		uint32_t clutOffset = 0;
		if ((fmt == GE_TFMT_CLUT4 || fmt == GE_TFMT_CLUT8) && !samplerID.useSharedClut)
			clutOffset = fmt == GE_TFMT_CLUT4 ? level * 16 : (level & 1) * 256;
		const Vec4S32 row0 = RowOffsets4<bits, swizzled>(bufw, v0), row1 = RowOffsets4<bits, swizzled>(bufw, v1);
		const Vec4S32 col0 = ColumnOffsets4<bits, swizzled>(u0), col1 = ColumnOffsets4<bits, swizzled>(u1);
		alignas(16) int tl[4], tr[4], bl[4], br[4];
		(row0 + col0).Store(tl);
		(row0 + col1).Store(tr);
		(row1 + col0).Store(bl);
		(row1 + col1).Store(br);
		for (int i = 0; i < 4; ++i) {
			texels[i][0] = ReadTexelT<fmt, clutFmt>(tptr + (uint32_t)tl[i], us0[i], clutOffset, samplerID);
			texels[i][1] = ReadTexelT<fmt, clutFmt>(tptr + (uint32_t)tr[i], us1[i], clutOffset, samplerID);
			texels[i][2] = ReadTexelT<fmt, clutFmt>(tptr + (uint32_t)bl[i], us0[i], clutOffset, samplerID);
			texels[i][3] = ReadTexelT<fmt, clutFmt>(tptr + (uint32_t)br[i], us1[i], clutOffset, samplerID);
		}
	}
	for (int i = 0; i < 4; ++i) {
		// Like the GE: horizontal lerps truncated to 8 bits, then the vertical one (gpu/probe exp52).
		const uint64_t top = LerpSpread(SpreadRGBA(texels[i][0]), SpreadRGBA(texels[i][1]), fu[i]);
		const uint64_t bot = LerpSpread(SpreadRGBA(texels[i][2]), SpreadRGBA(texels[i][3]), fu[i]);
		out[i] = LerpSpread(top, bot, fv[i]);
	}
}

// GetTextureFunctionOutput for four pixels, one per lane: prim and tex are R, G, B and A (all 0 to 255).
static inline void TextureFunction4(const Vec4S32 prim[4], const Vec4S32 tex[4], const SamplerID &samplerID, Vec4S32 out[4]) {
	const bool rgba = samplerID.useTextureAlpha;
	const bool doubling = samplerID.useColorDoubling;
	const Vec4S32 one = Vec4S32::Splat(1), c255 = Vec4S32::Splat(255);
	// The alpha blended the common way: (prim + 1) * tex / 256.
	const Vec4S32 modulatedA = rgba ? (prim[3] + one).Mul(tex[3]).Shr<8>() : prim[3];
	switch (samplerID.TexFunc()) {
	case GE_TEXFUNC_MODULATE:
		for (int c = 0; c < 3; ++c)
			out[c] = (prim[c] + one).Mul(doubling ? tex[c].Shl<1>() : tex[c]).Shr<8>();
		out[3] = modulatedA;
		break;

	case GE_TEXFUNC_DECAL:
		if (rgba) {
			// Both colors are boosted here, making the alpha have more weight.
			const Vec4S32 t = tex[3], invt = c255 - tex[3];
			for (int c = 0; c < 3; ++c) {
				const Vec4S32 sum = (prim[c] + one).Mul(invt) + (tex[c] + one).Mul(t);
				out[c] = doubling ? sum.Shr<7>() : sum.Shr<8>();
			}
		} else {
			for (int c = 0; c < 3; ++c)
				out[c] = doubling ? tex[c].Shl<1>() : tex[c];
		}
		out[3] = prim[3];
		break;

	case GE_TEXFUNC_BLEND:
	{
		// Unlike the others (and even alpha), this one always rounds up.
		const uint32_t env = samplerID.cached.texBlendColor;
		for (int c = 0; c < 3; ++c) {
			const Vec4S32 sum = (c255 - tex[c]).Mul(prim[c]) + tex[c].Mul(Vec4S32::Splat((env >> (8 * c)) & 0xFF)) + c255;
			out[c] = doubling ? sum.Shr<7>() : sum.Shr<8>();
		}
		out[3] = modulatedA;
		break;
	}

	case GE_TEXFUNC_REPLACE:
		for (int c = 0; c < 3; ++c)
			out[c] = doubling ? tex[c].Shl<1>() : tex[c];
		out[3] = rgba ? tex[3] : prim[3];
		break;

	default:
		// ADD and the unknown ones.
		for (int c = 0; c < 3; ++c)
			out[c] = doubling ? (prim[c] + tex[c]).Shl<1>() : prim[c] + tex[c];
		out[3] = modulatedA;
		break;
	}
}

template <GETextureFormat fmt, bool swizzled, GEPaletteFormat clutFmt>
static void SOFTRAST_CALL SampleLinearQuadT(const float *s, const float *t, const int *level, const int *levelFrac, int active, const u8 *const *texptr, const uint16_t *texbufw, int *colors, int colorStride, const SamplerID &samplerID) {
	if (!active)
		return;
	uint64_t c[4];
	// Usually all at one level (a span of a triangle always is): then in vector lanes.
	const int first = active & 1 ? 0 : active & 2 ? 1 : active & 4 ? 2 : 3;
	bool sameLevel = true;
	for (int i = first + 1; i < 4; ++i) {
		if ((active & (1 << i)) && (level[i] != level[first] || levelFrac[i] != levelFrac[first]))
			sameLevel = false;
	}
	if (sameLevel) {
		const int l = level[first];
		const Vec4F32 sv = Vec4F32::Load(s), tv = Vec4F32::Load(t);
		SampleLinearLevel4T<fmt, swizzled, clutFmt>(sv, tv, texptr[l], texbufw[l], l, samplerID, c);
		if (levelFrac[first]) {
			uint64_t c1[4];
			SampleLinearLevel4T<fmt, swizzled, clutFmt>(sv, tv, texptr[l + 1], texbufw[l + 1], l + 1, samplerID, c1);
			for (int i = 0; i < 4; ++i)
				c[i] = LerpSpread(c[i], c1[i], levelFrac[first]);
		}
		// The texture function, in lanes too.
		alignas(16) int texLanes[4][4];
		for (int i = 0; i < 4; ++i) {
			for (int ch = 0; ch < 4; ++ch)
				texLanes[ch][i] = (int)((c[i] >> (16 * ch)) & 0xFFFF);
		}
		Vec4S32 prim[4], tex[4], out[4];
		for (int ch = 0; ch < 4; ++ch) {
			prim[ch] = Vec4S32::Load(colors + ch * colorStride);
			tex[ch] = Vec4S32::Load(texLanes[ch]);
		}
		TextureFunction4(prim, tex, samplerID, out);
		alignas(16) static const int laneBits[4] = { 1, 2, 4, 8 };
		const Vec4S32 keep = (Vec4S32::Splat(active) & Vec4S32::LoadAligned(laneBits)).CompareEq(Vec4S32::Zero());
		for (int ch = 0; ch < 4; ++ch)
			(out[ch].AndNot(keep) | (prim[ch] & keep)).Store(colors + ch * colorStride);
		return;
	}

	// The four samples' loads and arithmetic are independent, so they can overlap.
	for (int i = 0; i < 4; ++i) {
		if (!(active & (1 << i)))
			continue;
		const int l = level[i];
		c[i] = SampleLinearLevelT<fmt, swizzled, clutFmt>(s[i], t[i], texptr[l], texbufw[l], l, samplerID);
		if (levelFrac[i])
			c[i] = LerpSpread(c[i], SampleLinearLevelT<fmt, swizzled, clutFmt>(s[i], t[i], texptr[l + 1], texbufw[l + 1], l + 1, samplerID), levelFrac[i]);
	}
	for (int i = 0; i < 4; ++i) {
		if (!(active & (1 << i)))
			continue;
		const Vec4<int> texcolor((int)(c[i] & 0xFFFF), (int)((c[i] >> 16) & 0xFFFF), (int)((c[i] >> 32) & 0xFFFF), (int)(c[i] >> 48));
		const Vec4<int> prim(colors[i], colors[colorStride + i], colors[2 * colorStride + i], colors[3 * colorStride + i]);
		const Vec4<int> out = GetTextureFunctionOutput(ToVec4IntArg(prim), ToVec4IntArg(texcolor), samplerID);
		for (int ch = 0; ch < 4; ++ch)
			colors[ch * colorStride + i] = out[ch];
	}
}

template <GETextureFormat fmt, bool swizzled>
static LinearQuadFunc PickLinearQuadClut(GEPaletteFormat clutFmt) {
	switch (clutFmt) {
	case GE_CMODE_16BIT_BGR5650: return &SampleLinearQuadT<fmt, swizzled, GE_CMODE_16BIT_BGR5650>;
	case GE_CMODE_16BIT_ABGR5551: return &SampleLinearQuadT<fmt, swizzled, GE_CMODE_16BIT_ABGR5551>;
	case GE_CMODE_16BIT_ABGR4444: return &SampleLinearQuadT<fmt, swizzled, GE_CMODE_16BIT_ABGR4444>;
	default: return &SampleLinearQuadT<fmt, swizzled, GE_CMODE_32BIT_ABGR8888>;
	}
}

template <GETextureFormat fmt>
static LinearQuadFunc PickLinearQuad(const SamplerID &id) {
	switch (fmt) {
	case GE_TFMT_CLUT4: case GE_TFMT_CLUT8: case GE_TFMT_CLUT16: case GE_TFMT_CLUT32:
		return id.swizzle ? PickLinearQuadClut<fmt, true>(id.ClutFmt()) : PickLinearQuadClut<fmt, false>(id.ClutFmt());
	case GE_TFMT_DXT1: case GE_TFMT_DXT3: case GE_TFMT_DXT5:
		return &SampleLinearQuadT<fmt, false, GE_CMODE_32BIT_ABGR8888>;
	default:
		return id.swizzle ? &SampleLinearQuadT<fmt, true, GE_CMODE_32BIT_ABGR8888> : &SampleLinearQuadT<fmt, false, GE_CMODE_32BIT_ABGR8888>;
	}
}

LinearQuadFunc GetLinearQuadFunc(const SamplerID &id, LinearFunc linear) {
	if (linear != GetLinearFallback(id))
		return nullptr;
	switch (id.TexFmt()) {
	case GE_TFMT_5650: return PickLinearQuad<GE_TFMT_5650>(id);
	case GE_TFMT_5551: return PickLinearQuad<GE_TFMT_5551>(id);
	case GE_TFMT_4444: return PickLinearQuad<GE_TFMT_4444>(id);
	case GE_TFMT_8888: return PickLinearQuad<GE_TFMT_8888>(id);
	case GE_TFMT_CLUT4: return PickLinearQuad<GE_TFMT_CLUT4>(id);
	case GE_TFMT_CLUT8: return PickLinearQuad<GE_TFMT_CLUT8>(id);
	case GE_TFMT_CLUT16: return PickLinearQuad<GE_TFMT_CLUT16>(id);
	case GE_TFMT_CLUT32: return PickLinearQuad<GE_TFMT_CLUT32>(id);
	case GE_TFMT_DXT1: return PickLinearQuad<GE_TFMT_DXT1>(id);
	case GE_TFMT_DXT3: return PickLinearQuad<GE_TFMT_DXT3>(id);
	case GE_TFMT_DXT5: return PickLinearQuad<GE_TFMT_DXT5>(id);
	default: return nullptr;
	}
}

template <GETextureFormat fmt, bool swizzled>
static LinearFunc PickLinearClut(GEPaletteFormat clutFmt) {
	switch (clutFmt) {
	case GE_CMODE_16BIT_BGR5650: return &SampleLinearT<fmt, swizzled, GE_CMODE_16BIT_BGR5650>;
	case GE_CMODE_16BIT_ABGR5551: return &SampleLinearT<fmt, swizzled, GE_CMODE_16BIT_ABGR5551>;
	case GE_CMODE_16BIT_ABGR4444: return &SampleLinearT<fmt, swizzled, GE_CMODE_16BIT_ABGR4444>;
	default: return &SampleLinearT<fmt, swizzled, GE_CMODE_32BIT_ABGR8888>;
	}
}

template <GETextureFormat fmt>
static LinearFunc PickLinearDirect(bool swizzled) {
	return swizzled ? &SampleLinearT<fmt, true, GE_CMODE_32BIT_ABGR8888> : &SampleLinearT<fmt, false, GE_CMODE_32BIT_ABGR8888>;
}

template <GETextureFormat fmt>
static LinearFunc PickLinearIndexed(const SamplerID &id) {
	return id.swizzle ? PickLinearClut<fmt, true>(id.ClutFmt()) : PickLinearClut<fmt, false>(id.ClutFmt());
}

static LinearFunc GetLinearFallback(const SamplerID &id) {
	switch (id.TexFmt()) {
	case GE_TFMT_5650: return PickLinearDirect<GE_TFMT_5650>(id.swizzle);
	case GE_TFMT_5551: return PickLinearDirect<GE_TFMT_5551>(id.swizzle);
	case GE_TFMT_4444: return PickLinearDirect<GE_TFMT_4444>(id.swizzle);
	case GE_TFMT_8888: return PickLinearDirect<GE_TFMT_8888>(id.swizzle);
	case GE_TFMT_CLUT4: return PickLinearIndexed<GE_TFMT_CLUT4>(id);
	case GE_TFMT_CLUT8: return PickLinearIndexed<GE_TFMT_CLUT8>(id);
	case GE_TFMT_CLUT16: return PickLinearIndexed<GE_TFMT_CLUT16>(id);
	case GE_TFMT_CLUT32: return PickLinearIndexed<GE_TFMT_CLUT32>(id);
	case GE_TFMT_DXT1: return &SampleLinearT<GE_TFMT_DXT1, false, GE_CMODE_32BIT_ABGR8888>;
	case GE_TFMT_DXT3: return &SampleLinearT<GE_TFMT_DXT3, false, GE_CMODE_32BIT_ABGR8888>;
	case GE_TFMT_DXT5: return &SampleLinearT<GE_TFMT_DXT5, false, GE_CMODE_32BIT_ABGR8888>;
	default: return &SampleLinear;
	}
}

};
