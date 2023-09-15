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
#include "Core/Config.h"
#include "GPU/GPUState.h"
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
		return Vec3<int>::AssignToAll(255 - std::min(2 * source.a(), 255));

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * dst.a(), 255));

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
		return Vec3<int>::AssignToAll(255 - std::min(2 * source.a(), 255));

	case PixelBlendFactor::DOUBLEDSTALPHA:
		return Vec3<int>::AssignToAll(2 * dst.a());

	case PixelBlendFactor::DOUBLEINVDSTALPHA:
		return Vec3<int>::AssignToAll(255 - std::min(2 * dst.a(), 255));

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
static Vec3<int> AlphaBlendingResult(const PixelFuncID &pixelID, const Vec4<int> &source, const Vec4<int> &dst) {
	// Note: These factors cannot go below 0, but they can go above 255 when doubling.
	Vec3<int> srcfactor = GetSourceFactor(pixelID.AlphaBlendSrc(), source, dst, pixelID.cached.alphaBlendSrc);
	Vec3<int> dstfactor = GetDestFactor(pixelID.AlphaBlendDst(), source, dst, pixelID.cached.alphaBlendDst);

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
void SOFTRAST_CALL DrawSinglePixel(int x, int y, int z, int fog, Vec4IntArg color_in, const PixelFuncID &pixelID) {
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

SingleFunc GetSingleFunc(const PixelFuncID &id, BinManager *binner) {
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

SingleFunc PixelJitCache::GetSingle(const PixelFuncID &id, BinManager *binner) {
	if (!g_Config.bSoftwareRenderingJit)
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
