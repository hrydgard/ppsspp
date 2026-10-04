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

#include "ppsspp_config.h"
#include <mutex>
#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Math/CrossSIMD.h"
#include "Core/Config.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/SoftGpu.h"

using namespace Math3D;

namespace Rasterizer {

std::mutex jitCacheLock;
PixelJitCache *jitCache = nullptr;

void Init() {
	jitCache = new PixelJitCache();
}

void FlushJit() {
	jitCache->Flush();
}

int JitClearGeneration() {
	return PixelJitCache::ClearGeneration();
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

static inline u8 GetPixelStencil(GEBufferFormat fmt, int fbStride, int x, int y) {
	if (fmt == GE_FORMAT_565) {
		// Always treated as 0 for comparison purposes.
		return 0;
	} else if (fmt == GE_FORMAT_5551) {
		return ((fb.Get16(x, y, fbStride) & 0x8000) != 0) ? 0xFF : 0;
	} else if (fmt == GE_FORMAT_4444) {
		return Convert4To8(fb.Get16(x, y, fbStride) >> 12);
	} else {
		return fb.Get32(x, y, fbStride) >> 24;
	}
}

static inline void SetPixelStencil(GEBufferFormat fmt, int fbStride, uint32_t targetWriteMask, int x, int y, u8 value) {
	if (fmt == GE_FORMAT_565) {
		// Do nothing
	} else if (fmt == GE_FORMAT_5551) {
		if ((targetWriteMask & 0x8000) == 0) {
			u16 pixel = fb.Get16(x, y, fbStride) & ~0x8000;
			pixel |= (value & 0x80) << 8;
			fb.Set16(x, y, fbStride, pixel);
		}
	} else if (fmt == GE_FORMAT_4444) {
		const u16 write_mask = targetWriteMask | 0x0FFF;
		u16 pixel = fb.Get16(x, y, fbStride) & write_mask;
		pixel |= ((u16)value << 8) & ~write_mask;
		fb.Set16(x, y, fbStride, pixel);
	} else {
		const u32 write_mask = targetWriteMask | 0x00FFFFFF;
		u32 pixel = fb.Get32(x, y, fbStride) & write_mask;
		pixel |= ((u32)value << 24) & ~write_mask;
		fb.Set32(x, y, fbStride, pixel);
	}
}

static inline u16 GetPixelDepth(int x, int y, int stride) {
	return depthbuf.Get16(x, y, stride);
}

static inline void SetPixelDepth(int x, int y, int stride, u16 value) {
	depthbuf.Set16(x, y, stride, value);
}

// NOTE: These likely aren't endian safe
// The depths of a span's four pixels from x.
static inline void ReadSpanDepth(int x, int y, int stride, int out[4]) {
	if (depthbuf.Contiguous4(x, y, stride)) {
		const u16 *zp = depthbuf.Get16Ptr(x, y, stride);
		for (int i = 0; i < 4; ++i)
			out[i] = zp[i];
	} else {
		for (int i = 0; i < 4; ++i)
			out[i] = depthbuf.Get16(x + i, y, stride);
	}
}

static inline u32 GetPixelColor(GEBufferFormat fmt, int fbStride, int x, int y) {
	switch (fmt) {
	case GE_FORMAT_565:
		// A should be zero for the purposes of alpha blending.
		return RGB565ToRGBA8888(fb.Get16(x, y, fbStride)) & 0x00FFFFFF;

	case GE_FORMAT_5551:
		return RGBA5551ToRGBA8888(fb.Get16(x, y, fbStride));

	case GE_FORMAT_4444:
		return RGBA4444ToRGBA8888(fb.Get16(x, y, fbStride));

	case GE_FORMAT_8888:
		return fb.Get32(x, y, fbStride);

	default:
		return 0;
	}
}

static inline void SetPixelColor(GEBufferFormat fmt, int fbStride, int x, int y, u32 value, u32 old_value, u32 targetWriteMask) {
	switch (fmt) {
	case GE_FORMAT_565:
		value = RGBA8888ToRGB565(value);
		if (targetWriteMask != 0) {
			old_value = RGBA8888ToRGB565(old_value);
			value = (value & ~targetWriteMask) | (old_value & targetWriteMask);
		}
		fb.Set16(x, y, fbStride, value);
		break;

	case GE_FORMAT_5551:
		value = RGBA8888ToRGBA5551(value);
		if (targetWriteMask != 0) {
			old_value = RGBA8888ToRGBA5551(old_value);
			value = (value & ~targetWriteMask) | (old_value & targetWriteMask);
		}
		fb.Set16(x, y, fbStride, value);
		break;

	case GE_FORMAT_4444:
		value = RGBA8888ToRGBA4444(value);
		if (targetWriteMask != 0) {
			old_value = RGBA8888ToRGBA4444(old_value);
			value = (value & ~targetWriteMask) | (old_value & targetWriteMask);
		}
		fb.Set16(x, y, fbStride, value);
		break;

	case GE_FORMAT_8888:
		value = (value & ~targetWriteMask) | (old_value & targetWriteMask);
		fb.Set32(x, y, fbStride, value);
		break;

	default:
		break;
	}
}

static inline bool AlphaTestPassed(const PixelFuncID &pixelID, int alpha) {
	const u8 ref = pixelID.alphaTestRef;
	if (pixelID.hasAlphaTestMask)
		alpha &= pixelID.cached.alphaTestMask;

	switch (pixelID.AlphaTestFunc()) {
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

static inline bool ColorTestPassed(const PixelFuncID &pixelID, const Vec3<int> &color) {
	const u32 mask = pixelID.cached.colorTestMask;
	const u32 c = color.ToRGB() & mask;
	const u32 ref = pixelID.cached.colorTestRef;
	switch (pixelID.cached.colorTestFunc) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return c == ref;

	case GE_COMP_NOTEQUAL:
		return c != ref;

	default:
		return true;
	}
}

static inline bool StencilTestPassed(const PixelFuncID &pixelID, u8 stencil) {
	if (pixelID.hasStencilTestMask)
		stencil &= pixelID.cached.stencilTestMask;
	u8 ref = pixelID.stencilTestRef;
	switch (pixelID.StencilTestFunc()) {
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

static inline u8 ApplyStencilOp(GEBufferFormat fmt, uint8_t stencilReplace, GEStencilOp op, u8 old_stencil) {
	switch (op) {
	case GE_STENCILOP_KEEP:
		return old_stencil;

	case GE_STENCILOP_ZERO:
		return 0;

	case GE_STENCILOP_REPLACE:
		return stencilReplace;

	case GE_STENCILOP_INVERT:
		return ~old_stencil;

	case GE_STENCILOP_INCR:
		switch (fmt) {
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
		switch (fmt) {
		case GE_FORMAT_4444:
			if (old_stencil >= 0x10)
				return old_stencil - 0x10;
			break;
		case GE_FORMAT_5551:
			return 0;
		default:
			if (old_stencil != 0)
				return old_stencil - 1;
			return old_stencil;
		}
		break;
	}

	return old_stencil;
}

static inline bool DepthTestPassed(GEComparison func, int x, int y, int stride, u16 z) {
	u16 reference_z = GetPixelDepth(x, y, stride);

	switch (func) {
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

bool CheckDepthTestPassed(GEComparison func, int x, int y, int stride, u16 z) {
	return DepthTestPassed(func, x, y, stride, z);
}

static inline u32 ApplyLogicOp(GELogicOp op, u32 old_color, u32 new_color) {
	// All of the operations here intentionally preserve alpha/stencil.
	switch (op) {
	case GE_LOGIC_CLEAR:
		new_color &= 0xFF000000;
		break;

	case GE_LOGIC_AND:
		new_color = new_color & (old_color | 0xFF000000);
		break;

	case GE_LOGIC_AND_REVERSE:
		new_color = new_color & (~old_color | 0xFF000000);
		break;

	case GE_LOGIC_COPY:
		// No change to new_color.
		break;

	case GE_LOGIC_AND_INVERTED:
		new_color = (~new_color & (old_color & 0x00FFFFFF)) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_NOOP:
		new_color = (old_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_XOR:
		new_color = new_color ^ (old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_OR:
		new_color = new_color | (old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_NOR:
		new_color = (~(new_color | old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_EQUIV:
		new_color = (~(new_color ^ old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_INVERTED:
		new_color = (~old_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_OR_REVERSE:
		new_color = new_color | (~old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_COPY_INVERTED:
		new_color = (~new_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_OR_INVERTED:
		new_color = ((~new_color | old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_NAND:
		new_color = (~(new_color & old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_SET:
		new_color |= 0x00FFFFFF;
		break;
	}

	return new_color;
}

static inline Vec3<int> GetSourceFactor(PixelBlendFactor factor, const Vec4<int> &source, const Vec4<int> &dst, uint32_t fix) {
	switch (factor) {
	case PixelBlendFactor::OTHERCOLOR:
		return dst.rgb();

	case PixelBlendFactor::INVOTHERCOLOR:
		return Vec3<int>::AssignToAll(255) - dst.rgb();

	case PixelBlendFactor::SRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3)));
#elif PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vdupq_laneq_s32(source.ivec, 3));
#else
		return Vec3<int>::AssignToAll(source.a());
#endif

	case PixelBlendFactor::INVSRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_sub_epi32(_mm_set1_epi32(255), _mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3))));
#elif PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vsubq_s32(vdupq_n_s32(255), vdupq_laneq_s32(source.ivec, 3)));
#else
		return Vec3<int>::AssignToAll(255 - source.a());
#endif

	case PixelBlendFactor::DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case PixelBlendFactor::INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case PixelBlendFactor::DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source.a());

	case PixelBlendFactor::DOUBLEINVSRCALPHA:
		// Can be negative, see SignedBlendingResult().
		return Vec3<int>::AssignToAll(255 - 2 * source.a());

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - 2 * dst.a());

	case PixelBlendFactor::FIX:
	default:
		// All other dest factors (> 10) are treated as FIXA.
		return Vec3<int>::FromRGB(fix);

	case PixelBlendFactor::ZERO:
		return Vec3<int>::AssignToAll(0);

	case PixelBlendFactor::ONE:
		return Vec3<int>::AssignToAll(255);
	}
}

static inline Vec3<int> GetDestFactor(PixelBlendFactor factor, const Vec4<int> &source, const Vec4<int> &dst, uint32_t fix) {
	switch (factor) {
	case PixelBlendFactor::OTHERCOLOR:
		return source.rgb();

	case PixelBlendFactor::INVOTHERCOLOR:
		return Vec3<int>::AssignToAll(255) - source.rgb();

	case PixelBlendFactor::SRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3)));
#elif PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vdupq_laneq_s32(source.ivec, 3));
#else
		return Vec3<int>::AssignToAll(source.a());
#endif

	case PixelBlendFactor::INVSRCALPHA:
#if defined(_M_SSE)
		return Vec3<int>(_mm_sub_epi32(_mm_set1_epi32(255), _mm_shuffle_epi32(source.ivec, _MM_SHUFFLE(3, 3, 3, 3))));
#elif PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vsubq_s32(vdupq_n_s32(255), vdupq_laneq_s32(source.ivec, 3)));
#else
		return Vec3<int>::AssignToAll(255 - source.a());
#endif

	case PixelBlendFactor::DSTALPHA:
		return Vec3<int>::AssignToAll(dst.a());

	case PixelBlendFactor::INVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - dst.a());

	case PixelBlendFactor::DOUBLESRCALPHA:
		return Vec3<int>::AssignToAll(2 * source.a());

	case PixelBlendFactor::DOUBLEINVSRCALPHA:
		// Can be negative, see SignedBlendingResult().
		return Vec3<int>::AssignToAll(255 - 2 * source.a());

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - 2 * dst.a());

	case PixelBlendFactor::FIX:
	default:
		// All other dest factors (> 10) are treated as FIXB.
		return Vec3<int>::FromRGB(fix);

	case PixelBlendFactor::ZERO:
		return Vec3<int>::AssignToAll(0);

	case PixelBlendFactor::ONE:
		return Vec3<int>::AssignToAll(255);
	}
}

// Removed inline here - it was never chosen to be inlined by the compiler anyway, too complex.
static bool IsSignedBlendFactor(PixelBlendFactor factor) {
	return factor == PixelBlendFactor::DOUBLEINVSRCALPHA || factor == PixelBlendFactor::DOUBLEINVDSTALPHA;
}

// 255 - 2a goes negative for a >= 128, and then the term subtracts: -(((2c + 1) * (2|f| + 1)) >> 10)
// (gpu/probe exp116, all factors and equations exact).
static Vec3<int> SignedBlendingResult(const PixelFuncID &pixelID, const Vec3<int> &srcfactor, const Vec3<int> &dstfactor, const Vec4<int> &source, const Vec4<int> &dst) {
	auto term = [](int c, int f) {
		const int t = ((2 * c + 1) * (2 * std::abs(f) + 1)) >> 10;
		return f < 0 ? -t : t;
	};
	Vec3<int> result;
	for (int i = 0; i < 3; ++i) {
		const int s = term(source[i], srcfactor[i]);
		const int d = term(dst[i], dstfactor[i]);
		switch (pixelID.AlphaBlendEq()) {
		case GE_BLENDMODE_MUL_AND_SUBTRACT: result[i] = s - d; break;
		case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE: result[i] = d - s; break;
		default: result[i] = s + d; break;
		}
	}
	return result;
}

static Vec3<int> AlphaBlendingResult(const PixelFuncID &pixelID, const Vec4<int> &source, const Vec4<int> &dst) {
	// Note: These factors can go above 255 when doubling, and below 0 for the doubled inverses.
	Vec3<int> srcfactor = GetSourceFactor(pixelID.AlphaBlendSrc(), source, dst, pixelID.cached.alphaBlendSrc);
	Vec3<int> dstfactor = GetDestFactor(pixelID.AlphaBlendDst(), source, dst, pixelID.cached.alphaBlendDst);
	const GEBlendMode eq = pixelID.AlphaBlendEq();
	if ((IsSignedBlendFactor(pixelID.AlphaBlendSrc()) || IsSignedBlendFactor(pixelID.AlphaBlendDst())) &&
		(eq == GE_BLENDMODE_MUL_AND_ADD || eq == GE_BLENDMODE_MUL_AND_SUBTRACT || eq == GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE)) {
		return SignedBlendingResult(pixelID, srcfactor, dstfactor, source, dst);
	}

	switch (pixelID.AlphaBlendEq()) {
	case GE_BLENDMODE_MUL_AND_ADD:
	{
#if defined(_M_SSE)
		// We switch to 16 bit to use mulhi, and we use 4 bits of decimal to make the 16 bit shift free.
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_adds_epi16(s, d), _mm_setzero_si128()));
#elif PPSSPP_ARCH(ARM64_NEON)
		const int32x4_t half = vdupq_n_s32(1);

		const int32x4_t srgb = vaddq_s32(vshlq_n_s32(source.ivec, 1), half);
		const int32x4_t sf = vaddq_s32(vshlq_n_s32(srcfactor.ivec, 1), half);
		const int32x4_t s = vshrq_n_s32(vmulq_s32(srgb, sf), 10);

		const int32x4_t drgb = vaddq_s32(vshlq_n_s32(dst.ivec, 1), half);
		const int32x4_t df = vaddq_s32(vshlq_n_s32(dstfactor.ivec, 1), half);
		const int32x4_t d = vshrq_n_s32(vmulq_s32(drgb, df), 10);

		return Vec3<int>(vaddq_s32(s, d));
#else
		static constexpr Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return lhs + rhs;
#endif
	}

	case GE_BLENDMODE_MUL_AND_SUBTRACT:
	{
#if defined(_M_SSE)
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_max_epi16(_mm_subs_epi16(s, d), _mm_setzero_si128()), _mm_setzero_si128()));
#elif PPSSPP_ARCH(ARM64_NEON)
		const int32x4_t half = vdupq_n_s32(1);

		const int32x4_t srgb = vaddq_s32(vshlq_n_s32(source.ivec, 1), half);
		const int32x4_t sf = vaddq_s32(vshlq_n_s32(srcfactor.ivec, 1), half);
		const int32x4_t s = vshrq_n_s32(vmulq_s32(srgb, sf), 10);

		const int32x4_t drgb = vaddq_s32(vshlq_n_s32(dst.ivec, 1), half);
		const int32x4_t df = vaddq_s32(vshlq_n_s32(dstfactor.ivec, 1), half);
		const int32x4_t d = vshrq_n_s32(vmulq_s32(drgb, df), 10);

		return Vec3<int>(vqsubq_s32(s, d));
#else
		static constexpr Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return lhs - rhs;
#endif
	}

	case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
	{
#if defined(_M_SSE)
		const __m128i half = _mm_set1_epi16(1 << 3);

		const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(source.ivec, source.ivec), 4), half);
		const __m128i sf = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(srcfactor.ivec, srcfactor.ivec), 4), half);
		const __m128i s = _mm_mulhi_epi16(srgb, sf);

		const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dst.ivec, dst.ivec), 4), half);
		const __m128i df = _mm_add_epi16(_mm_slli_epi16(_mm_packs_epi32(dstfactor.ivec, dstfactor.ivec), 4), half);
		const __m128i d = _mm_mulhi_epi16(drgb, df);

		return Vec3<int>(_mm_unpacklo_epi16(_mm_max_epi16(_mm_subs_epi16(d, s), _mm_setzero_si128()), _mm_setzero_si128()));
#elif PPSSPP_ARCH(ARM64_NEON)
		const int32x4_t half = vdupq_n_s32(1);

		const int32x4_t srgb = vaddq_s32(vshlq_n_s32(source.ivec, 1), half);
		const int32x4_t sf = vaddq_s32(vshlq_n_s32(srcfactor.ivec, 1), half);
		const int32x4_t s = vshrq_n_s32(vmulq_s32(srgb, sf), 10);

		const int32x4_t drgb = vaddq_s32(vshlq_n_s32(dst.ivec, 1), half);
		const int32x4_t df = vaddq_s32(vshlq_n_s32(dstfactor.ivec, 1), half);
		const int32x4_t d = vshrq_n_s32(vmulq_s32(drgb, df), 10);

		return Vec3<int>(vqsubq_s32(d, s));
#else
		static constexpr Vec3<int> half = Vec3<int>::AssignToAll(1);
		Vec3<int> lhs = ((source.rgb() * 2 + half) * (srcfactor * 2 + half)) / 1024;
		Vec3<int> rhs = ((dst.rgb() * 2 + half) * (dstfactor * 2 + half)) / 1024;
		return rhs - lhs;
#endif
	}

	case GE_BLENDMODE_MIN:
#if PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vminq_s32(source.ivec, dst.ivec));
#else
		return Vec3<int>(std::min(source.r(), dst.r()),
			std::min(source.g(), dst.g()),
			std::min(source.b(), dst.b()));
#endif

	case GE_BLENDMODE_MAX:
#if PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vmaxq_s32(source.ivec, dst.ivec));
#else
		return Vec3<int>(std::max(source.r(), dst.r()),
			std::max(source.g(), dst.g()),
			std::max(source.b(), dst.b()));
#endif

	case GE_BLENDMODE_ABSDIFF:
#if PPSSPP_ARCH(ARM64_NEON)
		return Vec3<int>(vabdq_s32(source.ivec, dst.ivec));
#else
		return Vec3<int>(::abs(source.r() - dst.r()),
			::abs(source.g() - dst.g()),
			::abs(source.b() - dst.b()));
#endif

	default:
		return source.rgb();
	}
}

template <bool clearMode, GEBufferFormat fbFormat>
static __forceinline void DrawPixelInline(int x, int y, int z, int fog, Vec4IntArg color_in, const PixelFuncID &pixelID) {
	Vec4<int> prim_color = Vec4<int>(color_in).Clamp(0, 255);
	// Depth range test - applied in clear mode, if not through mode.
	if (pixelID.applyDepthRange && !pixelID.earlyZChecks)
		if (z < pixelID.cached.minz || z > pixelID.cached.maxz)
			return;

	if (pixelID.AlphaTestFunc() != GE_COMP_ALWAYS && !clearMode)
		if (!AlphaTestPassed(pixelID, prim_color.a()))
			return;

	// Fog is applied prior to color test.
	if (pixelID.applyFog && !clearMode) {
		Vec3<int> fogColor = Vec3<int>::FromRGB(pixelID.cached.fogColor);
		// This is very similar to the BLEND texfunc, and simply always rounds up.
		static constexpr Vec3<int> roundup = Vec3<int>::AssignToAll(255);
		fogColor = (prim_color.rgb() * fog + fogColor * (255 - fog) + roundup) / 256;
		prim_color.r() = fogColor.r();
		prim_color.g() = fogColor.g();
		prim_color.b() = fogColor.b();
	}

	if (pixelID.colorTest && !clearMode)
		if (!ColorTestPassed(pixelID, prim_color.rgb()))
			return;

	// In clear mode, it uses the alpha color as stencil.
	uint32_t targetWriteMask = pixelID.applyColorWriteMask ? pixelID.cached.colorWriteMask : 0;
	u8 stencil = clearMode ? prim_color.a() : GetPixelStencil(fbFormat, pixelID.cached.framebufStride, x, y);
	if (clearMode) {
		if (pixelID.DepthClear())
			SetPixelDepth(x, y, pixelID.cached.depthbufStride, z);
	} else if (pixelID.stencilTest) {
		const uint8_t stencilReplace = pixelID.hasStencilTestMask ? pixelID.cached.stencilRef : pixelID.stencilTestRef;
		if (!StencilTestPassed(pixelID, stencil)) {
			stencil = ApplyStencilOp(fbFormat, stencilReplace, pixelID.SFail(), stencil);
			SetPixelStencil(fbFormat, pixelID.cached.framebufStride, targetWriteMask, x, y, stencil);
			return;
		}

		// Also apply depth at the same time.  If disabled, same as passing.
		if (!pixelID.earlyZChecks && pixelID.DepthTestFunc() != GE_COMP_ALWAYS && !DepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z)) {
			stencil = ApplyStencilOp(fbFormat, stencilReplace, pixelID.ZFail(), stencil);
			SetPixelStencil(fbFormat, pixelID.cached.framebufStride, targetWriteMask, x, y, stencil);
			return;
		}

		stencil = ApplyStencilOp(fbFormat, stencilReplace, pixelID.ZPass(), stencil);
	} else if (!pixelID.earlyZChecks) {
		if (pixelID.DepthTestFunc() != GE_COMP_ALWAYS && !DepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z)) {
			return;
		}
	}

	if (pixelID.depthWrite && !clearMode)
		SetPixelDepth(x, y, pixelID.cached.depthbufStride, z);

	const u32 old_color = GetPixelColor(fbFormat, pixelID.cached.framebufStride, x, y);
	u32 new_color;

	// Dithering happens before the logic op and regardless of framebuffer format or clear mode.
	// We do it while alpha blending because it happens before clamping.
	if (pixelID.alphaBlend && !clearMode) {
		const Vec4<int> dst = Vec4<int>::FromRGBA(old_color);
		Vec3<int> blended = AlphaBlendingResult(pixelID, prim_color, dst);
		if (pixelID.dithering) {
			blended += Vec3<int>::AssignToAll(pixelID.cached.ditherMatrix[(y & 3) * 4 + (x & 3)]);
		}

		// ToRGB() always automatically clamps.
		new_color = blended.ToRGB();
		new_color |= stencil << 24;
	} else {
		if (pixelID.dithering) {
			// We'll discard alpha anyway.
			prim_color += Vec4<int>::AssignToAll(pixelID.cached.ditherMatrix[(y & 3) * 4 + (x & 3)]);
		}

#if defined(_M_SSE) || PPSSPP_ARCH(ARM64_NEON)
		new_color = Vec3<int>(prim_color.ivec).ToRGB();
		new_color |= stencil << 24;
#else
		new_color = Vec4<int>(prim_color.r(), prim_color.g(), prim_color.b(), stencil).ToRGBA();
#endif
	}

	// Logic ops are applied after blending (if blending is enabled.)
	if (pixelID.applyLogicOp && !clearMode) {
		// Logic ops don't affect stencil, which happens inside ApplyLogicOp.
		new_color = ApplyLogicOp(pixelID.cached.logicOp, old_color, new_color);
	}

	if (clearMode) {
		if (!pixelID.ColorClear())
			new_color = (new_color & 0xFF000000) | (old_color & 0x00FFFFFF);
		if (!pixelID.StencilClear())
			new_color = (new_color & 0x00FFFFFF) | (old_color & 0xFF000000);
	}

	SetPixelColor(fbFormat, pixelID.cached.framebufStride, x, y, new_color, old_color, targetWriteMask);
}

template <bool clearMode, GEBufferFormat fbFormat>
void SOFTRAST_CALL DrawSinglePixel(int x, int y, int z, int fog, Vec4IntArg color_in, const PixelFuncID &pixelID) {
	DrawPixelInline<clearMode, fbFormat>(x, y, z, fog, color_in, pixelID);
}

// A framebuffer pixel as stored, from its 8 bit channels, as SetPixelColor converts them.
template <GEBufferFormat fbFormat>
static inline Vec4S32 PackSpanColor(Vec4S32 r, Vec4S32 g, Vec4S32 b, Vec4S32 a) {
	switch (fbFormat) {
	case GE_FORMAT_565: return r.Shr<3>() | g.Shr<2>().Shl<5>() | b.Shr<3>().Shl<11>();
	case GE_FORMAT_5551: return r.Shr<3>() | g.Shr<3>().Shl<5>() | b.Shr<3>().Shl<10>() | a.Shr<7>().Shl<15>();
	case GE_FORMAT_4444: return r.Shr<4>() | g.Shr<4>().Shl<4>() | b.Shr<4>().Shl<8>() | a.Shr<4>().Shl<12>();
	default: return r | g.Shl<8>() | b.Shl<16>() | a.Shl<24>();
	}
}

// The 8 bit channels of a stored pixel, as GetPixelColor expands them (565 has alpha 0).
template <GEBufferFormat fbFormat>
static inline void UnpackSpanColor(Vec4S32 raw, Vec4S32 ch[4]) {
	const Vec4S32 m5 = Vec4S32::Splat(0x1F), m4 = Vec4S32::Splat(0xF);
	auto from5 = [](Vec4S32 v) { return v.Shl<3>() | v.Shr<2>(); };
	auto from4 = [](Vec4S32 v) { return v.Shl<4>() | v; };
	switch (fbFormat) {
	case GE_FORMAT_565: {
		const Vec4S32 g6 = raw.Shr<5>() & Vec4S32::Splat(0x3F);
		ch[0] = from5(raw & m5);
		ch[1] = g6.Shl<2>() | g6.Shr<4>();
		ch[2] = from5(raw.Shr<11>() & m5);
		ch[3] = Vec4S32::Zero();
		break;
	}
	case GE_FORMAT_5551:
		ch[0] = from5(raw & m5);
		ch[1] = from5(raw.Shr<5>() & m5);
		ch[2] = from5(raw.Shr<10>() & m5);
		ch[3] = raw.Shl<16>().Shr<31>() & Vec4S32::Splat(0xFF);
		break;
	case GE_FORMAT_4444:
		ch[0] = from4(raw & m4);
		ch[1] = from4(raw.Shr<4>() & m4);
		ch[2] = from4(raw.Shr<8>() & m4);
		ch[3] = from4(raw.Shr<12>() & m4);
		break;
	default: {
		const Vec4S32 m8 = Vec4S32::Splat(0xFF);
		ch[0] = raw & m8;
		ch[1] = raw.Shr<8>() & m8;
		ch[2] = raw.Shr<16>() & m8;
		ch[3] = raw.Shr<24>() & m8;
		break;
	}
	}
}

// ApplyStencilOp per lane.
template <GEBufferFormat fbFormat>
static inline Vec4S32 SpanStencilOp(GEStencilOp op, Vec4S32 old, Vec4S32 replace) {
	const Vec4S32 c255 = Vec4S32::Splat(255);
	switch (op) {
	case GE_STENCILOP_KEEP: return old;
	case GE_STENCILOP_ZERO: return Vec4S32::Zero();
	case GE_STENCILOP_REPLACE: return replace;
	case GE_STENCILOP_INVERT: return old ^ c255;
	case GE_STENCILOP_INCR:
		switch (fbFormat) {
		case GE_FORMAT_8888: return (old + Vec4S32::Splat(1)).Min(c255);
		case GE_FORMAT_5551: return c255;
		case GE_FORMAT_4444: {
			const Vec4S32 below = old.CompareLt(Vec4S32::Splat(0xF0));
			return old + (Vec4S32::Splat(0x10) & below);
		}
		default: return old;
		}
	case GE_STENCILOP_DECR:
		switch (fbFormat) {
		case GE_FORMAT_4444: {
			const Vec4S32 above = old.CompareGt(Vec4S32::Splat(0x0F));
			return old - (Vec4S32::Splat(0x10) & above);
		}
		case GE_FORMAT_5551: return Vec4S32::Zero();
		default: return (old - Vec4S32::Splat(1)).Max(Vec4S32::Zero());
		}
	default: return old;
	}
}

// What DrawSpanVector's blending does: none, source alpha over the destination, or as the state says.
enum class SpanBlend {
	NONE,
	SRC_ALPHA,
	GENERIC,
};

// Whether DrawSpanVector can draw the state: everything but logic ops and the signed blend factors.
static bool SpanVectorHandles(const PixelFuncID &pixelID) {
	if (pixelID.clearMode)
		return true;
	if (pixelID.applyLogicOp)
		return false;
	return !(pixelID.alphaBlend && (IsSignedBlendFactor(pixelID.AlphaBlendSrc()) || IsSignedBlendFactor(pixelID.AlphaBlendDst())));
}

static SpanBlend SpanBlendFor(const PixelFuncID &pixelID) {
	if (pixelID.clearMode || !pixelID.alphaBlend)
		return SpanBlend::NONE;
	if (pixelID.AlphaBlendSrc() == PixelBlendFactor::SRCALPHA && pixelID.AlphaBlendDst() == PixelBlendFactor::INVSRCALPHA && pixelID.AlphaBlendEq() == GE_BLENDMODE_MUL_AND_ADD)
		return SpanBlend::SRC_ALPHA;
	return SpanBlend::GENERIC;
}

// The pixels of a span one per vector lane, as DrawPixelInline does them one at a time, for a state
// SpanVectorHandles and whose blending is blend.
template <bool clearMode, GEBufferFormat fbFormat, SpanBlend blend>
static inline void DrawSpanVector(int x, int y, const int *maskIn, const int *zIn, const int *fogIn, const int *colors, int colorStride, const PixelFuncID &pixelID) {
	const Vec4S32 zero = Vec4S32::Zero();
	const Vec4S32 c255 = Vec4S32::Splat(255);
	const Vec4S32 allOnes = Vec4S32::Splat(-1);
	auto clamp255 = [&](Vec4S32 v) {
		return v.Max(zero).Min(c255);
	};
	auto select = [](Vec4S32 m, Vec4S32 a, Vec4S32 b) {
		return (a & m) | b.AndNot(m);
	};
	Vec4S32 r = clamp255(Vec4S32::Load(colors)), g = clamp255(Vec4S32::Load(colors + colorStride)), b = clamp255(Vec4S32::Load(colors + 2 * colorStride));
	const Vec4S32 a = clamp255(Vec4S32::Load(colors + 3 * colorStride));
	const Vec4S32 z = Vec4S32::Load(zIn);
	// -1 for the pixels that aren't drawn.
	Vec4S32 dead = Vec4S32::Load(maskIn).Shr<31>();

	if (pixelID.applyDepthRange && !pixelID.earlyZChecks)
		dead = dead | z.CompareLt(Vec4S32::Splat(pixelID.cached.minz)) | z.CompareGt(Vec4S32::Splat(pixelID.cached.maxz));

	auto compare = [&](GEComparison func, Vec4S32 v, Vec4S32 ref) {
		// -1 where v func ref fails.
		switch (func) {
		case GE_COMP_NEVER: return allOnes;
		case GE_COMP_EQUAL: return v.CompareEq(ref) ^ allOnes;
		case GE_COMP_NOTEQUAL: return v.CompareEq(ref);
		case GE_COMP_LESS: return v.CompareLt(ref) ^ allOnes;
		case GE_COMP_LEQUAL: return v.CompareGt(ref);
		case GE_COMP_GREATER: return v.CompareGt(ref) ^ allOnes;
		case GE_COMP_GEQUAL: return v.CompareLt(ref);
		default: return zero;
		}
	};

	if constexpr (!clearMode) {
		if (pixelID.AlphaTestFunc() != GE_COMP_ALWAYS) {
			const Vec4S32 av = pixelID.hasAlphaTestMask ? a & Vec4S32::Splat(pixelID.cached.alphaTestMask) : a;
			dead = dead | compare(pixelID.AlphaTestFunc(), av, Vec4S32::Splat(pixelID.alphaTestRef));
		}

		if (pixelID.applyFog) {
			// Like the BLEND texfunc, always rounding up.
			const Vec4S32 f = Vec4S32::Load(fogIn), invF = c255 - f;
			const uint32_t fc = pixelID.cached.fogColor;
			r = (r.Mul(f) + Vec4S32::Splat(fc & 0xFF).Mul(invF) + c255).Shr<8>();
			g = (g.Mul(f) + Vec4S32::Splat((fc >> 8) & 0xFF).Mul(invF) + c255).Shr<8>();
			b = (b.Mul(f) + Vec4S32::Splat((fc >> 16) & 0xFF).Mul(invF) + c255).Shr<8>();
		}

		if (pixelID.colorTest) {
			const Vec4S32 rgb = (r | g.Shl<8>() | b.Shl<16>()) & Vec4S32::Splat(pixelID.cached.colorTestMask);
			const Vec4S32 ref = Vec4S32::Splat(pixelID.cached.colorTestRef);
			switch (pixelID.cached.colorTestFunc) {
			case GE_COMP_NEVER: dead = allOnes; break;
			case GE_COMP_EQUAL: dead = dead | (rgb.CompareEq(ref) ^ allOnes); break;
			case GE_COMP_NOTEQUAL: dead = dead | rgb.CompareEq(ref); break;
			default: break;
			}
		}
	}

	const int fbStride = pixelID.cached.framebufStride;
	const int depthStride = pixelID.cached.depthbufStride;
	alignas(16) int old[4];
	if (fbFormat == GE_FORMAT_8888) {
		memcpy(old, fb.Get32Ptr(x, y, fbStride), sizeof(old));
	} else {
		const u16 *p = fb.Get16Ptr(x, y, fbStride);
		for (int i = 0; i < 4; ++i)
			old[i] = p[i];
	}
	const Vec4S32 oldRaw = Vec4S32::Load(old);
	// The old channels; the alpha is also the stencil (GetPixelStencil).
	Vec4S32 dst[4];
	UnpackSpanColor<fbFormat>(oldRaw, dst);

	// Lanes that only get a new stencil (stencil or depth test failed), and the stencil written.
	Vec4S32 stencilOnly = zero;
	Vec4S32 stencil = clearMode ? a : dst[3];
	if (!clearMode && pixelID.stencilTest) {
		const Vec4S32 sv = pixelID.hasStencilTestMask ? dst[3] & Vec4S32::Splat(pixelID.cached.stencilTestMask) : dst[3];
		const Vec4S32 replace = Vec4S32::Splat(pixelID.hasStencilTestMask ? pixelID.cached.stencilRef : pixelID.stencilTestRef);
		// The test is ref func stencil.
		const Vec4S32 sfail = compare(pixelID.StencilTestFunc(), Vec4S32::Splat(pixelID.stencilTestRef), sv).AndNot(dead);
		Vec4S32 zfail = zero;
		if (!pixelID.earlyZChecks && pixelID.DepthTestFunc() != GE_COMP_ALWAYS) {
			alignas(16) int ref[4];
			ReadSpanDepth(x, y, depthStride, ref);
			zfail = compare(pixelID.DepthTestFunc(), z, Vec4S32::Load(ref)).AndNot(dead | sfail);
		}
		const Vec4S32 old = dst[3];
		stencil = select(sfail, SpanStencilOp<fbFormat>(pixelID.SFail(), old, replace),
			select(zfail, SpanStencilOp<fbFormat>(pixelID.ZFail(), old, replace), SpanStencilOp<fbFormat>(pixelID.ZPass(), old, replace)));
		stencilOnly = sfail | zfail;
	} else if (!clearMode && !pixelID.earlyZChecks && pixelID.DepthTestFunc() != GE_COMP_ALWAYS) {
		alignas(16) int ref[4];
		ReadSpanDepth(x, y, depthStride, ref);
		dead = dead | compare(pixelID.DepthTestFunc(), z, Vec4S32::Load(ref));
	}

	alignas(16) int deadLanes[4];
	dead.Store(deadLanes);
	if ((deadLanes[0] & deadLanes[1] & deadLanes[2] & deadLanes[3]) != 0)
		return;
	// Lanes whose color is written.
	Vec4S32 colorDead = dead | stencilOnly;
	alignas(16) int colorDeadLanes[4];
	colorDead.Store(colorDeadLanes);

	if (clearMode ? pixelID.DepthClear() : pixelID.depthWrite) {
		if (depthbuf.Contiguous4(x, y, depthStride)) {
			u16 *zp = depthbuf.Get16Ptr(x, y, depthStride);
			for (int i = 0; i < 4; ++i) {
				if (!colorDeadLanes[i])
					zp[i] = (u16)zIn[i];
			}
		} else {
			for (int i = 0; i < 4; ++i) {
				if (!colorDeadLanes[i])
					depthbuf.Set16(x + i, y, depthStride, (u16)zIn[i]);
			}
		}
	}

	if constexpr (blend == SpanBlend::SRC_ALPHA) {
		const Vec4S32 one = Vec4S32::Splat(1);
		auto term = [&](Vec4S32 v, Vec4S32 f) {
			return (v.Shl<1>() + one).Mul(f.Shl<1>() + one).Shr<10>();
		};
		const Vec4S32 invA = c255 - a;
		r = term(r, a) + term(dst[0], invA);
		g = term(g, a) + term(dst[1], invA);
		b = term(b, a) + term(dst[2], invA);
	} else if constexpr (blend == SpanBlend::GENERIC) {
		auto factor = [&](PixelBlendFactor f, const Vec4S32 other[3], Vec4S32 srcA, Vec4S32 dstA, uint32_t fix, Vec4S32 out[3]) {
			switch (f) {
			case PixelBlendFactor::OTHERCOLOR: for (int c = 0; c < 3; ++c) out[c] = other[c]; break;
			case PixelBlendFactor::INVOTHERCOLOR: for (int c = 0; c < 3; ++c) out[c] = c255 - other[c]; break;
			case PixelBlendFactor::SRCALPHA: out[0] = out[1] = out[2] = srcA; break;
			case PixelBlendFactor::INVSRCALPHA: out[0] = out[1] = out[2] = c255 - srcA; break;
			case PixelBlendFactor::DSTALPHA: out[0] = out[1] = out[2] = dstA; break;
			case PixelBlendFactor::INVDSTALPHA: out[0] = out[1] = out[2] = c255 - dstA; break;
			case PixelBlendFactor::DOUBLESRCALPHA: out[0] = out[1] = out[2] = srcA.Shl<1>(); break;
			case PixelBlendFactor::DOUBLEDSTALPHA: out[0] = out[1] = out[2] = dstA.Shl<1>(); break;
			case PixelBlendFactor::ZERO: out[0] = out[1] = out[2] = zero; break;
			case PixelBlendFactor::ONE: out[0] = out[1] = out[2] = c255; break;
			default:
				// FIX, and everything above it.
				out[0] = Vec4S32::Splat(fix & 0xFF);
				out[1] = Vec4S32::Splat((fix >> 8) & 0xFF);
				out[2] = Vec4S32::Splat((fix >> 16) & 0xFF);
				break;
			}
		};
		const Vec4S32 src[3] = { r, g, b };
		Vec4S32 sf[3], df[3];
		// The source factor's "other" color is the destination's, and the other way around.
		factor(pixelID.AlphaBlendSrc(), dst, a, dst[3], pixelID.cached.alphaBlendSrc, sf);
		factor(pixelID.AlphaBlendDst(), src, a, dst[3], pixelID.cached.alphaBlendDst, df);
		const Vec4S32 one = Vec4S32::Splat(1);
		Vec4S32 out[3];
		for (int c = 0; c < 3; ++c) {
			auto term = [&](Vec4S32 v, Vec4S32 f) {
				return (v.Shl<1>() + one).Mul(f.Shl<1>() + one).Shr<10>();
			};
			switch (pixelID.AlphaBlendEq()) {
			case GE_BLENDMODE_MUL_AND_ADD: out[c] = term(src[c], sf[c]) + term(dst[c], df[c]); break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT: out[c] = term(src[c], sf[c]) - term(dst[c], df[c]); break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE: out[c] = term(dst[c], df[c]) - term(src[c], sf[c]); break;
			case GE_BLENDMODE_MIN: out[c] = src[c].Min(dst[c]); break;
			case GE_BLENDMODE_MAX: out[c] = src[c].Max(dst[c]); break;
			case GE_BLENDMODE_ABSDIFF: out[c] = (src[c] - dst[c]).Max(dst[c] - src[c]); break;
			default: out[c] = src[c]; break;
			}
		}
		r = out[0];
		g = out[1];
		b = out[2];
	}
	if (pixelID.dithering) {
		alignas(16) int dither[4];
		for (int i = 0; i < 4; ++i)
			dither[i] = pixelID.cached.ditherMatrix[(y & 3) * 4 + ((x + i) & 3)];
		const Vec4S32 dv = Vec4S32::Load(dither);
		r = r + dv;
		g = g + dv;
		b = b + dv;
	}
	r = clamp255(r);
	g = clamp255(g);
	b = clamp255(b);
	if constexpr (clearMode) {
		if (!pixelID.ColorClear()) {
			r = dst[0];
			g = dst[1];
			b = dst[2];
		}
		if (!pixelID.StencilClear())
			stencil = dst[3];
	} else {
		// Where only the stencil changes, the old color.
		r = select(stencilOnly, dst[0], r);
		g = select(stencilOnly, dst[1], g);
		b = select(stencilOnly, dst[2], b);
	}
	Vec4S32 value = PackSpanColor<fbFormat>(r, g, b, stencil);
	if (pixelID.applyColorWriteMask) {
		const Vec4S32 writeMask = Vec4S32::Splat((int)pixelID.cached.colorWriteMask);
		value = value.AndNot(writeMask) | (oldRaw & writeMask);
	}

	alignas(16) int out[4];
	value.Store(out);
	// Only the drawn pixels: a span is aligned in screen coordinates, so with an offset it can reach into a
	// tile another thread draws. And a depth buffer can overlap the colors (Wipeout's bloom).
	const bool allLive = (deadLanes[0] | deadLanes[1] | deadLanes[2] | deadLanes[3]) == 0;
	if (fbFormat == GE_FORMAT_8888) {
		u32 *p = fb.Get32Ptr(x, y, fbStride);
		if (allLive) {
			memcpy(p, out, sizeof(out));
		} else {
			for (int i = 0; i < 4; ++i) {
				if (!deadLanes[i])
					p[i] = (u32)out[i];
			}
		}
	} else {
		u16 *p = fb.Get16Ptr(x, y, fbStride);
		for (int i = 0; i < 4; ++i) {
			if (!deadLanes[i])
				p[i] = (u16)out[i];
		}
	}
}

template <bool clearMode, GEBufferFormat fbFormat, SpanBlend blend>
static void SOFTRAST_CALL DrawSpanPixels(int x, int y, const int *mask, const int *z, const int *fog, const int *colors, int colorStride, const PixelFuncID &pixelID) {
	DrawSpanVector<clearMode, fbFormat, blend>(x, y, mask, z, fog, colors, colorStride, pixelID);
}

// A pixel at a time, for what DrawSpanVector doesn't handle.
template <bool clearMode, GEBufferFormat fbFormat>
static void SOFTRAST_CALL DrawSpanScalar(int x, int y, const int *mask, const int *z, const int *fog, const int *colors, int colorStride, const PixelFuncID &pixelID) {
	for (int i = 0; i < 4; ++i) {
		if (mask[i] >= 0) {
			const Vec4<int> color(colors[i], colors[colorStride + i], colors[2 * colorStride + i], colors[3 * colorStride + i]);
			DrawPixelInline<clearMode, fbFormat>(x + i, y, z[i], fog[i], ToVec4IntArg(color), pixelID);
		}
	}
}

template <GEBufferFormat fbFormat>
static SpanFunc PickSpanFunc(const PixelFuncID &id) {
	if (id.clearMode)
		return &DrawSpanPixels<true, fbFormat, SpanBlend::NONE>;
	if (!SpanVectorHandles(id))
		return &DrawSpanScalar<false, fbFormat>;
	switch (SpanBlendFor(id)) {
	case SpanBlend::NONE: return &DrawSpanPixels<false, fbFormat, SpanBlend::NONE>;
	case SpanBlend::SRC_ALPHA: return &DrawSpanPixels<false, fbFormat, SpanBlend::SRC_ALPHA>;
	default: return &DrawSpanPixels<false, fbFormat, SpanBlend::GENERIC>;
	}
}

SpanFunc GetSpanFunc(const PixelFuncID &id, SingleFunc single) {
	if (single != PixelJitCache::GenericSingle(id))
		return nullptr;
	switch (id.fbFormat) {
	case GE_FORMAT_565: return PickSpanFunc<GE_FORMAT_565>(id);
	case GE_FORMAT_5551: return PickSpanFunc<GE_FORMAT_5551>(id);
	case GE_FORMAT_4444: return PickSpanFunc<GE_FORMAT_4444>(id);
	default: return PickSpanFunc<GE_FORMAT_8888>(id);
	}
}

SingleFunc GetSingleFunc(const PixelFuncID &id, BinManager *binner) {
	// The jit clamps blend factors at 0.
	if (id.alphaBlend && (IsSignedBlendFactor(id.AlphaBlendSrc()) || IsSignedBlendFactor(id.AlphaBlendDst())))
		return jitCache->GenericSingle(id);
	SingleFunc jitted = jitCache->GetSingle(id, binner);
	if (jitted) {
		return jitted;
	}

	return jitCache->GenericSingle(id);
}

SingleFunc PixelJitCache::GenericSingle(const PixelFuncID &id) {
	if (id.clearMode) {
		switch (id.fbFormat) {
		case GE_FORMAT_565:
			return &DrawSinglePixel<true, GE_FORMAT_565>;
		case GE_FORMAT_5551:
			return &DrawSinglePixel<true, GE_FORMAT_5551>;
		case GE_FORMAT_4444:
			return &DrawSinglePixel<true, GE_FORMAT_4444>;
		case GE_FORMAT_8888:
			return &DrawSinglePixel<true, GE_FORMAT_8888>;
		}
	}
	switch (id.fbFormat) {
	case GE_FORMAT_565:
		return &DrawSinglePixel<false, GE_FORMAT_565>;
	case GE_FORMAT_5551:
		return &DrawSinglePixel<false, GE_FORMAT_5551>;
	case GE_FORMAT_4444:
		return &DrawSinglePixel<false, GE_FORMAT_4444>;
	case GE_FORMAT_8888:
		return &DrawSinglePixel<false, GE_FORMAT_8888>;
	}
	_assert_(false);
	return nullptr;
}

thread_local PixelJitCache::LastCache PixelJitCache::lastSingle_;
int PixelJitCache::clearGen_ = 0;

// 256k should be plenty of space for plenty of variations.
PixelJitCache::PixelJitCache() : CodeBlock(1024 * 64 * 4), cache_(64) {
	lastSingle_.gen = -1;
	clearGen_++;
}

void PixelJitCache::Clear() {
	clearGen_++;
	CodeBlock::Clear();
	cache_.Clear();
	addresses_.clear();

	constBlendHalf_11_4s_ = nullptr;
	constBlendInvert_11_4s_ = nullptr;
}

std::string PixelJitCache::DescribeCodePtr(const u8 *ptr) {
	constexpr bool USE_IDS = false;
	ptrdiff_t dist = 0x7FFFFFFF;
	if (USE_IDS) {
		PixelFuncID found{};
		for (const auto &it : addresses_) {
			ptrdiff_t it_dist = ptr - it.second;
			if (it_dist >= 0 && it_dist < dist) {
				found = it.first;
				dist = it_dist;
			}
		}

		return DescribePixelFuncID(found);
	}

	return CodeBlock::DescribeCodePtr(ptr);
}

void PixelJitCache::Flush() {
	std::unique_lock<std::mutex> guard(jitCacheLock);
	for (const auto &queued : compileQueue_) {
		// Might've been compiled after enqueue, but before now.
		size_t queuedKey = std::hash<PixelFuncID>()(queued);
		if (!cache_.ContainsKey(queuedKey))
			Compile(queued);
	}
	compileQueue_.clear();
}

// Without a backend nothing ever compiles, and a lookup would flush the binner for nothing.
#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
static constexpr bool HAS_PIXEL_JIT = true;
#else
static constexpr bool HAS_PIXEL_JIT = false;
#endif

SingleFunc PixelJitCache::GetSingle(const PixelFuncID &id, BinManager *binner) {
	if (!HAS_PIXEL_JIT || !g_Config.bSoftwareRenderingJit)
		return nullptr;

	const size_t key = std::hash<PixelFuncID>()(id);
	if (lastSingle_.Match(key, clearGen_))
		return lastSingle_.func;

	std::unique_lock<std::mutex> guard(jitCacheLock);
	SingleFunc singleFunc;
	if (cache_.Get(key, &singleFunc)) {
		lastSingle_.Set(key, singleFunc, clearGen_);
		return singleFunc;
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
		size_t queuedKey = std::hash<PixelFuncID>()(queued);
		if (!cache_.ContainsKey(queuedKey))
			Compile(queued);
	}
	compileQueue_.clear();

	// Might've been in the queue.
	if (!cache_.ContainsKey(key))
		Compile(id);

	if (cache_.Get(key, &singleFunc)) {
		lastSingle_.Set(key, singleFunc, clearGen_);
		return singleFunc;
	} else {
		return nullptr;
	}
}

void PixelJitCache::Compile(const PixelFuncID &id) {
	// x64 is typically 200-500 bytes, but let's be safe.
	if (GetSpaceLeft() < 65536) {
		Clear();
	}

#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	addresses_[id] = GetCodePointer();
	SingleFunc func = CompileSingle(id);
	cache_.Insert(std::hash<PixelFuncID>()(id), func);
#endif
}

void ComputePixelBlendState(PixelBlendState &state, const PixelFuncID &id) {
	switch (id.AlphaBlendEq()) {
	case GE_BLENDMODE_MUL_AND_ADD:
	case GE_BLENDMODE_MUL_AND_SUBTRACT:
	case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
		state.usesFactors = true;
		break;

	case GE_BLENDMODE_MIN:
	case GE_BLENDMODE_MAX:
	case GE_BLENDMODE_ABSDIFF:
		break;
	}

	if (state.usesFactors) {
		switch (id.AlphaBlendSrc()) {
		case PixelBlendFactor::DSTALPHA:
		case PixelBlendFactor::INVDSTALPHA:
		case PixelBlendFactor::DOUBLEDSTALPHA:
		case PixelBlendFactor::DOUBLEINVDSTALPHA:
			state.usesDstAlpha = true;
			break;

		case PixelBlendFactor::OTHERCOLOR:
		case PixelBlendFactor::INVOTHERCOLOR:
			state.dstColorAsFactor = true;
			break;

		case PixelBlendFactor::SRCALPHA:
		case PixelBlendFactor::INVSRCALPHA:
		case PixelBlendFactor::DOUBLESRCALPHA:
		case PixelBlendFactor::DOUBLEINVSRCALPHA:
			state.srcColorAsFactor = true;
			break;

		default:
			break;
		}

		switch (id.AlphaBlendDst()) {
		case PixelBlendFactor::INVSRCALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == PixelBlendFactor::SRCALPHA;
			state.srcColorAsFactor = true;
			break;

		case PixelBlendFactor::DOUBLEINVSRCALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == PixelBlendFactor::DOUBLESRCALPHA;
			state.srcColorAsFactor = true;
			break;

		case PixelBlendFactor::DSTALPHA:
			state.usesDstAlpha = true;
			break;

		case PixelBlendFactor::INVDSTALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == PixelBlendFactor::DSTALPHA;
			state.usesDstAlpha = true;
			break;

		case PixelBlendFactor::DOUBLEDSTALPHA:
			state.usesDstAlpha = true;
			break;

		case PixelBlendFactor::DOUBLEINVDSTALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == PixelBlendFactor::DOUBLEDSTALPHA;
			state.usesDstAlpha = true;
			break;

		case PixelBlendFactor::OTHERCOLOR:
		case PixelBlendFactor::INVOTHERCOLOR:
			state.srcColorAsFactor = true;
			break;

		case PixelBlendFactor::SRCALPHA:
		case PixelBlendFactor::DOUBLESRCALPHA:
			state.srcColorAsFactor = true;
			break;

		case PixelBlendFactor::ZERO:
			state.readsDstPixel = state.dstColorAsFactor || state.usesDstAlpha;
			break;

		default:
			break;
		}
	}
}

};
