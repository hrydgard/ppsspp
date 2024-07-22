#include "Common/CommonTypes.h"
#include "Common/Data/Convert/ColorConv.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/GPUState.h"

#ifdef _M_SSE
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// Candidates for hand-writing
// (found using our custom Very Sleepy).
// GPU::P:_f_N:_s8_C:_8888_T:_u16__(24b)_040001BE  (5%+ of God of War execution)
// GPU::P:_f_N:_s8_C:_8888_T:_u16_W:_f_(1x)__(28b)_040007BE (1%+ of God of War execution)

// Tekken 6:
// (found using the vertex counter that's active in _DEBUG)
// [04000111] P: s16 C: 565 T: u8  (10b) (736949)    // Also in Midnight Club

// Wipeout Pure:
// [0400013f] P: s16 N: s8 C: 8888 T: f  (24b) (1495430)

// Flatout:
// [04000122] P: s16 N: s8 T: u16  (14b) (3901754)
// [04000116] P: s16 C: 5551 T: u16  (12b) (2225841)

// Test drive:
// [05000100] P: s16  (6b) (2827872)
// [050011ff] P: f N: f C: 8888 T: f I: u16  (36b) (3812112)

// Burnout Dominator:
// [04000122] P: s16 N: s8 T: u16  (14b) (1710813)
// [04000116] P: s16 C: 5551 T: u16  (12b) (7688298)

// This is the first GoW one.
void VtxDec_Tu16_C8888_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct GOWVTX {
		union {
			struct {
				u16 u;
				u16 v;
			};
			u32 packed_uv;
		};
		u32 packed_normal;
		u32 col;
		float x;
		float y;
		float z;
	};
	// NOTE: This might be different for different vertex formats.
	struct OutVTX {
		float u;
		float v;
		u32 packed_normal;
		uint32_t col;
		float x;
		float y;
		float z;
	};
	const GOWVTX *src = (const GOWVTX *)srcp;
	OutVTX *dst = (OutVTX *)dstp;
	float uscale = uvScaleOffset->uScale * (1.0f / 32768.0f);
	float vscale = uvScaleOffset->vScale * (1.0f / 32768);
	float uoff = uvScaleOffset->uOff;
	float voff = uvScaleOffset->vOff;

	u32 alpha = 0xFFFFFFFF;

#if PPSSPP_ARCH(SSE2)
	__m128 uvOff = _mm_setr_ps(uoff, voff, uoff, voff);
	__m128 uvScale = _mm_setr_ps(uscale, vscale, uscale, vscale);
	__m128i alphaMask = _mm_set1_epi32(0xFFFFFFFF);
	for (int i = 0; i < count; i++) {
		__m128i uv = _mm_set1_epi32(src[i].packed_uv);
		__m128 fuv = _mm_cvtepi32_ps(_mm_unpacklo_epi16(uv, _mm_setzero_si128()));
		__m128 finalUV = _mm_add_ps(_mm_mul_ps(fuv, uvScale), uvOff);
		u32 normal = src[i].packed_normal;
		__m128i colpos = _mm_loadu_si128((const __m128i *)&src[i].col);
		_mm_store_sd((double *)&dst[i].u, _mm_castps_pd(finalUV));
		dst[i].packed_normal = normal;
		_mm_storeu_si128((__m128i *)&dst[i].col, colpos);
		alphaMask = _mm_and_si128(alphaMask, colpos);
	}
	alpha = _mm_cvtsi128_si32(alphaMask);

#elif PPSSPP_ARCH(ARM_NEON)
	float32x2_t uvScale = vmul_f32(vld1_f32(&uvScaleOffset->uScale), vdup_n_f32(1.0f / 32768.0f));
	float32x2_t uvOff = vld1_f32(&uvScaleOffset->uOff);
	uint32x4_t alphaMask = vdupq_n_u32(0xFFFFFFFF);
	for (int i = 0; i < count; i++) {
		uint16x4_t uv = vld1_u16(&src[i].u);  // TODO: We only need the first two lanes, maybe there's a better way?
		uint32x2_t fuv = vget_low_u32(vmovl_u16(uv));  // Only using the first two lanes
		float32x2_t finalUV = vadd_f32(vmul_f32(vcvt_f32_u32(fuv), uvScale), uvOff);
		u32 normal = src[i].packed_normal;
		uint32x4_t colpos = vld1q_u32((const u32 *)&src[i].col);
		alphaMask = vandq_u32(alphaMask, colpos);
		vst1_f32(&dst[i].u, finalUV);
		dst[i].packed_normal = normal;
		vst1q_u32(&dst[i].col, colpos);
	}
	alpha = vgetq_lane_u32(alphaMask, 0);
#else
	for (int i = 0; i < count; i++) {
		float u = src[i].u * uscale + uoff;
		float v = src[i].v * vscale + voff;
		uint32_t color = src[i].col;
		alpha &= color;
		float x = src[i].x;
		float y = src[i].y;
		float z = src[i].z;
		dst[i].col = color;
		dst[i].packed_normal = src[i].packed_normal;
		dst[i].u = u;
		dst[i].v = v;
		dst[i].x = x;
		dst[i].y = y;
		dst[i].z = z;
	}
#endif
	gstate_c.vertexFullAlpha = (alpha >> 24) == 0xFF;
}

void VtxDec_Tu8_C5551_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct GTAVTX {
		union {
			struct {
				u8 u;
				u8 v;
			};
			u16 uv;
		};
		u16 col;
		s16 x;
		s16 y;
		s16 z;
	};
	// NOTE: This might be different for different vertex formats.
	struct OutVTX {
		float u;
		float v;
		uint32_t col;
		float x;
		float y;
		float z;
	};
	const GTAVTX *src = (const GTAVTX *)srcp;
	OutVTX *dst = (OutVTX *)dstp;
	float uscale = uvScaleOffset->uScale * (1.0f / 128.0f);
	float vscale = uvScaleOffset->vScale * (1.0f / 128.0f);
	float uoff = uvScaleOffset->uOff;
	float voff = uvScaleOffset->vOff;

	uint64_t alpha = 0xFFFFFFFFFFFFFFFFULL;

#if PPSSPP_ARCH(SSE2)
	__m128 uvOff = _mm_setr_ps(uoff, voff, uoff, voff);
	__m128 uvScale = _mm_setr_ps(uscale, vscale, uscale, vscale);
	__m128 posScale = _mm_set1_ps(1.0f / 32768.0f);
	__m128i rmask = _mm_set1_epi32(0x001F);
	__m128i gmask = _mm_set1_epi32(0x03E0);
	__m128i bmask = _mm_set1_epi32(0x7c00);
	__m128i amask = _mm_set1_epi32(0x8000);
	__m128i lowbits = _mm_set1_epi32(0x00070707);

	// Two vertices at a time, we can share some calculations.
	// It's OK to accidentally decode an extra vertex.
	for (int i = 0; i < count; i += 2) {
		__m128i pos0 = _mm_loadl_epi64((const __m128i *) & src[i].x);
		__m128i pos1 = _mm_loadl_epi64((const __m128i *) & src[i + 1].x);
		// Translate UV, combined. TODO: Can possibly shuffle UV and col together here
		uint32_t uv0 = (uint32_t)src[i].uv | ((uint32_t)src[i + 1].uv << 16);
		uint64_t col0 = (uint64_t)src[i].col | ((uint64_t)src[i + 1].col << 32);
		__m128i pos0_32 = _mm_srai_epi32(_mm_unpacklo_epi16(pos0, pos0), 16);
		__m128i pos1_32 = _mm_srai_epi32(_mm_unpacklo_epi16(pos1, pos1), 16);
		__m128 pos0_ext = _mm_mul_ps(_mm_cvtepi32_ps(pos0_32), posScale);
		__m128 pos1_ext = _mm_mul_ps(_mm_cvtepi32_ps(pos1_32), posScale);

		__m128i uv8 = _mm_set1_epi32(uv0);
		__m128i uv16 = _mm_unpacklo_epi8(uv8, uv8);
		__m128i uv32 = _mm_srli_epi32(_mm_unpacklo_epi16(uv16, uv16), 24);
		__m128d uvf = _mm_castps_pd(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(uv32), uvScale), uvOff));
		alpha &= col0;

		// Combined RGBA
		__m128i col = _mm_set1_epi64x(col0);
		__m128i r = _mm_slli_epi32(_mm_and_si128(col, rmask), 8 - 5);
		__m128i g = _mm_slli_epi32(_mm_and_si128(col, gmask), 16 - 10);
		__m128i b = _mm_slli_epi32(_mm_and_si128(col, bmask), 24 - 15);
		__m128i a = _mm_srai_epi32(_mm_slli_epi32(_mm_and_si128(col, amask), 16), 7);
		col = _mm_or_si128(_mm_or_si128(r, g), b);
		col = _mm_or_si128(col, _mm_and_si128(_mm_srli_epi32(col, 5), lowbits));
		col = _mm_or_si128(col, a);

		// TODO: Mix into fewer stores.
		_mm_storeu_ps(&dst[i].x, pos0_ext);
		_mm_storeu_ps(&dst[i + 1].x, pos1_ext);
		_mm_storel_pd((double *)&dst[i].u, uvf);
		_mm_storeh_pd((double *)&dst[i + 1].u, uvf);
		dst[i].col = _mm_cvtsi128_si32(col);
		dst[i + 1].col = _mm_cvtsi128_si32(_mm_shuffle_epi32(col, _MM_SHUFFLE(1, 1, 1, 1)));
	}

	alpha = alpha & (alpha >> 32);

#elif PPSSPP_ARCH(ARM_NEON)

	float32x4_t uvScaleOff = vld1q_f32(&uvScaleOffset->uScale);
	float32x4_t uvScale = vmulq_f32(vcombine_f32(vget_low_f32(uvScaleOff), vget_low_f32(uvScaleOff)), vdupq_n_f32(1.0f / 128.0f));
	float32x4_t uvOffset = vcombine_f32(vget_high_f32(uvScaleOff), vget_high_f32(uvScaleOff));
	float32x4_t posScale = vdupq_n_f32(1.0f / 32768.0f);
	uint32x2_t rmask = vdup_n_u32(0x001F);
	uint32x2_t gmask = vdup_n_u32(0x03E0);
	uint32x2_t bmask = vdup_n_u32(0x7c00);
	uint32x2_t amask = vdup_n_u32(0x8000);
	uint32x2_t lowbits = vdup_n_u32(0x00070707);

	// Two vertices at a time, we can share some calculations.
	// It's OK to accidentally decode an extra vertex.
	// Doing four vertices at a time might be even better, can share more of the pesky color format conversion.
	for (int i = 0; i < count; i += 2) {
		int16x4_t pos0 = vld1_s16(&src[i].x);
		int16x4_t pos1 = vld1_s16(&src[i + 1].x);
		// Translate UV, combined. TODO: Can possibly shuffle UV and col together here
		uint32_t uv0 = (uint32_t)src[i].uv | ((uint32_t)src[i + 1].uv << 16);
		uint64_t col0 = (uint64_t)src[i].col | ((uint64_t)src[i + 1].col << 32);
		int32x4_t pos0_32 = vmovl_s16(pos0);
		int32x4_t pos1_32 = vmovl_s16(pos1);
		float32x4_t pos0_ext = vmulq_f32(vcvtq_f32_s32(pos0_32), posScale);
		float32x4_t pos1_ext = vmulq_f32(vcvtq_f32_s32(pos1_32), posScale);

		uint64x1_t uv8_one = vdup_n_u64(uv0);
		uint8x8_t uv8 = vreinterpret_u8_u64(uv8_one);
		uint16x4_t uv16 = vget_low_u16(vmovl_u8(uv8));
		uint32x4_t uv32 = vmovl_u16(uv16);
		float32x4_t uvf = vaddq_f32(vmulq_f32(vcvtq_f32_u32(uv32), uvScale), uvOffset);

		alpha &= col0;

		// Combined RGBA
		uint32x2_t col = vreinterpret_u32_u64(vdup_n_u64(col0));
		uint32x2_t r = vshl_n_u32(vand_u32(col, rmask), 8 - 5);
		uint32x2_t g = vshl_n_u32(vand_u32(col, gmask), 16 - 10);
		uint32x2_t b = vshl_n_u32(vand_u32(col, bmask), 24 - 15);
		int32x2_t a_shifted = vshr_n_s32(vreinterpret_s32_u32(vshl_n_u32(vand_u32(col, amask), 16)), 7);
		uint32x2_t a = vreinterpret_u32_s32(a_shifted);
		col = vorr_u32(vorr_u32(r, g), b);
		col = vorr_u32(col, vand_u32(vshl_n_u32(col, 5), lowbits));
		col = vorr_u32(col, a);

		// TODO: Mix into fewer stores.
		vst1q_f32(&dst[i].x, pos0_ext);
		vst1q_f32(&dst[i + 1].x, pos1_ext);
		vst1_f32(&dst[i].u, vget_low_f32(uvf));
		vst1_f32(&dst[i + 1].u, vget_high_f32(uvf));
		dst[i].col = vget_lane_u32(col, 0);
		dst[i + 1].col = vget_lane_u32(col, 1);
	}

	alpha = alpha & (alpha >> 32);

#else

	for (int i = 0; i < count; i++) {
		float u = src[i].u * uscale + uoff;
		float v = src[i].v * vscale + voff;
		alpha &= src[i].col;
		uint32_t color = RGBA5551ToRGBA8888(src[i].col);
		float x = src[i].x * (1.0f / 32768.0f);
		float y = src[i].y * (1.0f / 32768.0f);
		float z = src[i].z * (1.0f / 32768.0f);
		dst[i].col = color;
		dst[i].u = u;
		dst[i].v = v;
		dst[i].x = x;
		dst[i].y = y;
		dst[i].z = z;
	}

#endif

	gstate_c.vertexFullAlpha = (alpha >> 15) & 1;
}
