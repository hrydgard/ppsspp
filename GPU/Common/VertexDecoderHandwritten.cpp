#include "Common/CommonTypes.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Core/HDRemaster.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/GPUState.h"

#include "Common/Math/CrossSIMD.h"
#include "Common/Math/SIMDHeaders.h"

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

// This is the first GoW one. TODO: Do two verts at a time.
// God of War.
void VtxDec_Tu16_C8888_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	// Input: 4 + 4 + 4 + 12 = 24 bytes
	struct GOWVTX {
		union {
			struct {
				u16 u;
				u16 v;
			};
			u32 packed_uv;
		};
		u32 col;
		u32 nrm;  // s8 x3, padding
		float x;
		float y;
		float z;
	};
	// NOTE: This might be different for different vertex formats.
	// Output: 8 + 4 + 4 + 12 = 28 bytes.
	struct OutVTX {
		float u;
		float v;
		u32 col;
		u32 nrm;
		float x;
		float y;
		float z;
	};
	const GOWVTX *src = (const GOWVTX *)srcp;
	OutVTX *dst = (OutVTX *)dstp;
	// The HD Remasters double the texture coordinates, see Step_TcU16DoublePrescale.
	const float uvDiv = g_DoubleTextureCoordinates ? (1.0f / 16384.0f) : (1.0f / 32768.0f);
	float uscale = uvScaleOffset->uScale * uvDiv;
	float vscale = uvScaleOffset->vScale * uvDiv;
	float uoff = uvScaleOffset->uOff;
	float voff = uvScaleOffset->vOff;

	u32 alpha = 0xFFFFFFFF;

	// The position is loaded together with the normal, as lanes 1-3. A NaN or infinite coordinate has
	// to come out finite, so that the viewport scale can zero it later like the PSP does. Which finite
	// value doesn't matter, so any lane with an all-ones exponent is zeroed. Lane 0 (the normal) is
	// left alone: its mask is 0, which never equals the exponent.
	alignas(16) static const u32 expMask[4] = { 0, 0x7F800000, 0x7F800000, 0x7F800000 };

#if PPSSPP_ARCH(SSE2)
	__m128 uvOff = _mm_setr_ps(uoff, voff, uoff, voff);
	__m128 uvScale = _mm_setr_ps(uscale, vscale, uscale, vscale);
	const __m128i posExpMask = _mm_load_si128((const __m128i *)expMask);
	const __m128i expAllOnes = _mm_set1_epi32(0x7F800000);
	for (int i = 0; i < count; i++) {
		__m128i uv = _mm_set1_epi32(src[i].packed_uv);
		__m128 fuv = _mm_cvtepi32_ps(_mm_unpacklo_epi16(uv, _mm_setzero_si128()));
		__m128 finalUV = _mm_add_ps(_mm_mul_ps(fuv, uvScale), uvOff);
		const u32 col = src[i].col;
		alpha &= col;
		__m128i nrmpos = _mm_loadu_si128((const __m128i *)&src[i].nrm);
		nrmpos = _mm_andnot_si128(_mm_cmpeq_epi32(_mm_and_si128(nrmpos, posExpMask), expAllOnes), nrmpos);
		_mm_store_sd((double *)&dst[i].u, _mm_castps_pd(finalUV));
		dst[i].col = col;
		_mm_storeu_si128((__m128i *)&dst[i].nrm, nrmpos);
	}

#elif PPSSPP_ARCH(ARM_NEON)
	const float scaleArr[2] = { uscale, vscale };
	float32x2_t uvScale = vld1_f32(scaleArr);
	float32x2_t uvOff = vld1_f32(&uvScaleOffset->uOff);
	const uint32x4_t posExpMask = vld1q_u32(expMask);
	const uint32x4_t expAllOnes = vdupq_n_u32(0x7F800000);
	for (int i = 0; i < count; i++) {
		uint16x4_t uv = vld1_u16(&src[i].u);  // TODO: We only need the first two lanes, maybe there's a better way?
		uint32x2_t fuv = vget_low_u32(vmovl_u16(uv));  // Only using the first two lanes
#if PPSSPP_ARCH(ARM64_NEON)
		// Fused, like the arm64 JIT and the steps as the compiler builds them.
		float32x2_t finalUV = vfma_f32(uvOff, vcvt_f32_u32(fuv), uvScale);
#else
		float32x2_t finalUV = vadd_f32(vmul_f32(vcvt_f32_u32(fuv), uvScale), uvOff);
#endif
		const u32 col = src[i].col;
		alpha &= col;
		uint32x4_t nrmpos = vld1q_u32(&src[i].nrm);
		nrmpos = vbicq_u32(nrmpos, vceqq_u32(vandq_u32(nrmpos, posExpMask), expAllOnes));
		vst1_f32(&dst[i].u, finalUV);
		dst[i].col = col;
		vst1q_u32(&dst[i].nrm, nrmpos);
	}
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
		dst[i].nrm = src[i].nrm;
		dst[i].u = u;
		dst[i].v = v;
		dst[i].x = x;
		dst[i].y = y;
		dst[i].z = z;
	}
#endif
	// Only cleared, like the steps do: the draws before in the batch count too.
	if ((alpha >> 24) != 0xFF)
		gstate_c.vertexFullAlpha = false;
}

void VtxDec_Tu8_C5551_Ps16(const u8 *srcp, u8 *dstp, int numVerts, const UVScale *uvScaleOffset) {
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
	int count = numVerts;

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
	while (count >= 2) {
		__m128i pos0 = _mm_loadl_epi64((const __m128i *) & src[0].x);
		__m128i pos1 = _mm_loadl_epi64((const __m128i *) & src[1].x);
		// Translate UV, combined. TODO: Can possibly shuffle UV and col together here
		uint32_t uv0 = (uint32_t)src[0].uv | ((uint32_t)src[1].uv << 16);
		uint64_t col0 = (uint64_t)src[0].col | ((uint64_t)src[1].col << 32);
		__m128i pos0_32 = _mm_srai_epi32(_mm_unpacklo_epi16(pos0, pos0), 16);
		__m128i pos1_32 = _mm_srai_epi32(_mm_unpacklo_epi16(pos1, pos1), 16);
		__m128 pos0_ext = _mm_mul_ps(_mm_cvtepi32_ps(pos0_32), posScale);
		__m128 pos1_ext = _mm_mul_ps(_mm_cvtepi32_ps(pos1_32), posScale);

		__m128i uv8 = _mm_cvtsi32_si128(uv0);
		__m128i uv16 = _mm_unpacklo_epi8(uv8, uv8);
		__m128i uv32 = _mm_srli_epi32(_mm_unpacklo_epi16(uv16, uv16), 24);
		__m128d uvf = _mm_castps_pd(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(uv32), uvScale), uvOff));
		alpha &= col0;

		// Combined 5551 -> 8888 RGBA. Nasty.
#if PPSSPP_ARCH(AMD64)
		__m128i col = _mm_cvtsi64_si128(col0);
#else
		__m128i col = _mm_set1_epi64x(col0);
#endif
		__m128i r = _mm_slli_epi32(_mm_and_si128(col, rmask), 8 - 5);
		__m128i g = _mm_slli_epi32(_mm_and_si128(col, gmask), 16 - 10);
		__m128i b = _mm_slli_epi32(_mm_and_si128(col, bmask), 24 - 15);
		__m128i a = _mm_srai_epi32(_mm_slli_epi32(_mm_and_si128(col, amask), 16), 7);
		col = _mm_or_si128(_mm_or_si128(r, g), b);
		col = _mm_or_si128(col, _mm_and_si128(_mm_srli_epi32(col, 5), lowbits));
		col = _mm_or_si128(col, a);

		// TODO: Mix into fewer stores.
		_mm_storeu_ps(&dst[0].x, pos0_ext);
		_mm_storeu_ps(&dst[1].x, pos1_ext);
		_mm_storel_pd((double *)&dst[0].u, uvf);
		_mm_storeh_pd((double *)&dst[1].u, uvf);
#if PPSSPP_ARCH(AMD64)
		u64 colors = _mm_cvtsi128_si64(col);
		dst[0].col = colors;
		dst[1].col = colors >> 32;
#else
		dst[0].col = _mm_cvtsi128_si32(col);
		dst[1].col = _mm_cvtsi128_si32(_mm_shuffle_epi32(col, _MM_SHUFFLE(1, 1, 1, 1)));
#endif
		src += 2;
		dst += 2;
		count -= 2;
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
	// Doing four vertices at a time might be even better, can share more of the pesky color format conversion.
	while (count >= 2) {
		int16x4_t pos0 = vld1_s16(&src[0].x);
		int16x4_t pos1 = vld1_s16(&src[1].x);
		// Translate UV, combined. TODO: Can possibly shuffle UV and col together here
		uint32_t uv0 = (uint32_t)src[0].uv | ((uint32_t)src[1].uv << 16);
		uint64_t col0 = (uint64_t)src[0].col | ((uint64_t)src[1].col << 32);
		int32x4_t pos0_32 = vmovl_s16(pos0);
		int32x4_t pos1_32 = vmovl_s16(pos1);
		float32x4_t pos0_ext = vmulq_f32(vcvtq_f32_s32(pos0_32), posScale);
		float32x4_t pos1_ext = vmulq_f32(vcvtq_f32_s32(pos1_32), posScale);

		uint64x1_t uv8_one = vdup_n_u64(uv0);
		uint8x8_t uv8 = vreinterpret_u8_u64(uv8_one);
		uint16x4_t uv16 = vget_low_u16(vmovl_u8(uv8));
		uint32x4_t uv32 = vmovl_u16(uv16);
#if PPSSPP_ARCH(ARM64_NEON)
		float32x4_t uvf = vfmaq_f32(uvOffset, vcvtq_f32_u32(uv32), uvScale);
#else
		float32x4_t uvf = vaddq_f32(vmulq_f32(vcvtq_f32_u32(uv32), uvScale), uvOffset);
#endif

		alpha &= col0;

		// Combined RGBA
		uint32x2_t col = vreinterpret_u32_u64(vdup_n_u64(col0));
		uint32x2_t r = vshl_n_u32(vand_u32(col, rmask), 8 - 5);
		uint32x2_t g = vshl_n_u32(vand_u32(col, gmask), 16 - 10);
		uint32x2_t b = vshl_n_u32(vand_u32(col, bmask), 24 - 15);
		int32x2_t a_shifted = vshr_n_s32(vreinterpret_s32_u32(vshl_n_u32(vand_u32(col, amask), 16)), 7);
		uint32x2_t a = vreinterpret_u32_s32(a_shifted);
		col = vorr_u32(vorr_u32(r, g), b);
		col = vorr_u32(col, vand_u32(vshr_n_u32(col, 5), lowbits));
		col = vorr_u32(col, a);

		// TODO: Mix into fewer stores.
		vst1q_f32(&dst[0].x, pos0_ext);
		vst1q_f32(&dst[1].x, pos1_ext);
		vst1_f32(&dst[0].u, vget_low_f32(uvf));
		vst1_f32(&dst[1].u, vget_high_f32(uvf));
		dst[0].col = vget_lane_u32(col, 0);
		dst[1].col = vget_lane_u32(col, 1);

		dst += 2;
		src += 2;
		count -= 2;
	}

	alpha = alpha & (alpha >> 32);

#endif

	while (count > 0) {
		float u = src->u * uscale + uoff;
		float v = src->v * vscale + voff;
		alpha &= src->col;
		uint32_t color = RGBA5551ToRGBA8888(src->col);
		float x = src->x * (1.0f / 32768.0f);
		float y = src->y * (1.0f / 32768.0f);
		float z = src->z * (1.0f / 32768.0f);
		dst->col = color;
		dst->u = u;
		dst->v = v;
		dst->x = x;
		dst->y = y;
		dst->z = z;
		src++;
		dst++;
		count--;
	}

	if (!((alpha >> 15) & 1))
		gstate_c.vertexFullAlpha = false;
}

// Converting UV without a prescale is the same as with a scale of 1 and no offset: the division by a power
// of two in the scale is exact either way.
static const UVScale identityUVScale = { 1.0f, 1.0f, 0.0f, 0.0f };

void VtxDec_Tu16_C8888_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	VtxDec_Tu16_C8888_Pfloat(srcp, dstp, count, &identityUVScale);
}

void VtxDec_Tu8_C5551_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	VtxDec_Tu8_C5551_Ps16(srcp, dstp, count, &identityUVScale);
}


// The ones below convert like the steps, so the results are the same. With prescale false, they're for the
// UV generation modes that don't prescale (float UV is then copied as is, -0 included).

template <bool prescale>
static void DecodeTu8_C565_Ns16_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct InVtx {
		u8 u, v;
		u16 col;
		s16 nrm[3];
		float pos[3];
	};
	struct OutVtx {
		float u, v;
		u32 col;
		s16 nrm[4];
		float pos[3];
	};
	const InVtx *src = (const InVtx *)srcp;
	OutVtx *dst = (OutVtx *)dstp;
	const UVScale uv = *uvScaleOffset;
	for (int i = 0; i < count; i++) {
		const float u = src[i].u * (1.0f / 128.0f), v = src[i].v * (1.0f / 128.0f);
		dst[i].u = prescale ? PrescaleUV(u, uv.uScale, uv.uOff) : u;
		dst[i].v = prescale ? PrescaleUV(v, uv.vScale, uv.vOff) : v;
		dst[i].col = RGB565ToRGBA8888(src[i].col);
		dst[i].nrm[0] = src[i].nrm[0];
		dst[i].nrm[1] = src[i].nrm[1];
		dst[i].nrm[2] = src[i].nrm[2];
		dst[i].nrm[3] = 0;
		// Like Step_PosFloat, which reads and writes four floats.
		Vec4F32::Load(src[i].pos).CleanNaNInfs().Store(dst[i].pos);
	}
}

// ATV Offroad Fury: Blazin' Trails.
void VtxDec_Tu8_C565_Ns16_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTu8_C565_Ns16_Pfloat<true>(srcp, dstp, count, uvScaleOffset);
}
void VtxDec_Tu8_C565_Ns16_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTu8_C565_Ns16_Pfloat<false>(srcp, dstp, count, uvScaleOffset);
}

template <bool prescale>
static void DecodeTfloat_C8888_Ns8_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct InVtx {
		float uv[2];
		u32 col;
		u32 nrm;  // s8 x3, padding
		s16 pos[3];
		u16 pad;
	};
	struct OutVtx {
		float uv[2];
		u32 col;
		u32 nrm;  // s8 x3, 0
		float pos[3];
	};
	const InVtx *src = (const InVtx *)srcp;
	OutVtx *dst = (OutVtx *)dstp;
	const Vec4F32 uvScale = Vec4F32::Load2(&uvScaleOffset->uScale);
	const Vec4F32 uvOff = Vec4F32::Load2(&uvScaleOffset->uOff);
	const Vec4F32 posScale = Vec4F32::Splat(1.0f / 32768.0f);
	u32 alpha = 0xFFFFFFFF;
	for (int i = 0; i < count; i++) {
		Vec4F32 uv = Vec4F32::Load2(src[i].uv);
		if (prescale)
			uv = MulAdd(uv, uvScale, uvOff);  // Fused where PrescaleUV() is.
		uv.Store2(dst[i].uv);
		const u32 col = src[i].col;
		alpha &= col;
		dst[i].col = col;
		dst[i].nrm = src[i].nrm & 0x00FFFFFF;
		// Writes four floats, the last over the next vertex (or the slack after the buffer).
		(Vec4F32::LoadConvertS16(src[i].pos) * posScale).Store(dst[i].pos);
	}
	if ((alpha >> 24) != 0xFF)
		gstate_c.vertexFullAlpha = false;
}

// Wipeout Pure.
void VtxDec_Tfloat_C8888_Ns8_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTfloat_C8888_Ns8_Ps16<true>(srcp, dstp, count, uvScaleOffset);
}
void VtxDec_Tfloat_C8888_Ns8_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTfloat_C8888_Ns8_Ps16<false>(srcp, dstp, count, uvScaleOffset);
}

template <bool prescale>
static void DecodeTu16_C4444_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct InVtx {
		u16 u, v;
		u16 col;
		s16 pos[3];
	};
	struct OutVtx {
		float u, v;
		u32 col;
		float pos[3];
	};
	const InVtx *src = (const InVtx *)srcp;
	OutVtx *dst = (OutVtx *)dstp;
	const UVScale uv = *uvScaleOffset;
	// The HD Remasters double the texture coordinates, see Step_TcU16DoublePrescale.
	const float uvDiv = g_DoubleTextureCoordinates ? (1.0f / 16384.0f) : (1.0f / 32768.0f);
	const Vec4F32 posScale = Vec4F32::Splat(1.0f / 32768.0f);
	u32 alpha = 0xFFFF;
	for (int i = 0; i < count; i++) {
		const float u = src[i].u * uvDiv, v = src[i].v * uvDiv;
		dst[i].u = prescale ? PrescaleUV(u, uv.uScale, uv.uOff) : u;
		dst[i].v = prescale ? PrescaleUV(v, uv.vScale, uv.vOff) : v;
		const u16 col = src[i].col;
		alpha &= col;
		dst[i].col = RGBA4444ToRGBA8888(col);
		if (i + 1 < count) {
			// Reads one s16 into the next vertex, and writes four floats, the last over the next vertex.
			(Vec4F32::LoadConvertS16(src[i].pos) * posScale).Store(dst[i].pos);
		} else {
			dst[i].pos[0] = src[i].pos[0] * (1.0f / 32768.0f);
			dst[i].pos[1] = src[i].pos[1] * (1.0f / 32768.0f);
			dst[i].pos[2] = src[i].pos[2] * (1.0f / 32768.0f);
		}
	}
	if ((alpha >> 12) != 0xF)
		gstate_c.vertexFullAlpha = false;
}

// Killzone: Liberation, Gods Eater Burst.
void VtxDec_Tu16_C4444_Ps16(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTu16_C4444_Ps16<true>(srcp, dstp, count, uvScaleOffset);
}
void VtxDec_Tu16_C4444_Ps16_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeTu16_C4444_Ps16<false>(srcp, dstp, count, uvScaleOffset);
}

template <bool prescale>
static void DecodeW1float_Tu8_C4444_Ns8_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	struct InVtx {
		float w;
		u8 u, v;
		u16 col;
		s8 nrm[3];
		u8 pad;
		float pos[3];
	};
	struct OutVtx {
		float u, v;
		u32 col;
		float nrm[3];
		float pos[3];
	};
	const InVtx *src = (const InVtx *)srcp;
	OutVtx *dst = (OutVtx *)dstp;
	const UVScale uv = *uvScaleOffset;
	// The skin matrix is what ComputeSkinMatrix() makes (0 + bone * weight, for each element), kept as
	// the columns Vec3ByMatrix43() and Norm3ByMatrix43() load, so they come straight from the bone
	// matrix's. Lane 3 ends up only in values overwritten later. Those functions multiply and add in
	// the order below, unfused, so the results are the same.
	const float *bone = gstate.boneMatrix;
	const Vec4F32 bone0 = Vec4F32::Load(bone), bone1 = Vec4F32::Load(bone + 3), bone2 = Vec4F32::Load(bone + 6), bone3 = Vec4F32::Load(bone + 9);
	Vec4F32 col0 = Vec4F32::Zero(), col1 = Vec4F32::Zero(), col2 = Vec4F32::Zero(), col3 = Vec4F32::Zero();
	u32 skinWeightBits = 0;
	bool haveSkin = false;
	u32 alpha = 0xFFFF;
	for (int i = 0; i < count; i++) {
		const float u = src[i].u * (1.0f / 128.0f), v = src[i].v * (1.0f / 128.0f);
		dst[i].u = prescale ? PrescaleUV(u, uv.uScale, uv.uOff) : u;
		dst[i].v = prescale ? PrescaleUV(v, uv.vScale, uv.vOff) : v;
		const u16 col = src[i].col;
		alpha &= col;
		dst[i].col = RGBA4444ToRGBA8888(col);

		u32 weightBits;
		memcpy(&weightBits, &src[i].w, sizeof(weightBits));
		if (!haveSkin || weightBits != skinWeightBits) {
			const float w = src[i].w;
			const Vec4F32 zero = Vec4F32::Zero();
			if (w != 0.0f) {
				const Vec4F32 wv = Vec4F32::Splat(w);
				col0 = zero + bone0 * wv;
				col1 = zero + bone1 * wv;
				col2 = zero + bone2 * wv;
				col3 = zero + bone3 * wv;
			} else {
				col0 = col1 = col2 = col3 = zero;
			}
			skinWeightBits = weightBits;
			haveSkin = true;
		}
		// Each writes four floats, the last over the next field or vertex (or the slack after the buffer).
#if PPSSPP_ARCH(ARM64_NEON)
		// By lane, like Vec3ByMatrix43Internal() here. Reads the padding after the normal and the float after
		// the position (the next vertex's weight, or the bytes after the last), which go to lane 3 only.
		uint32_t nrmBits;
		memcpy(&nrmBits, src[i].nrm, sizeof(nrmBits));
		const int16x8_t n16 = vmovl_s8(vreinterpret_s8_u32(vdup_n_u32(nrmBits)));
		const float32x4_t n = vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(n16))), 1.0f / 128.0f);
		const float32x4_t nrm = vaddq_f32(vaddq_f32(vmulq_laneq_f32(col0.v, n, 0), vmulq_laneq_f32(col1.v, n, 1)), vmulq_laneq_f32(col2.v, n, 2));
		vst1q_f32(dst[i].nrm, nrm);
		const float32x4_t p = vld1q_f32(src[i].pos);
		const float32x4_t pos = vaddq_f32(vaddq_f32(vmulq_laneq_f32(col0.v, p, 0), vmulq_laneq_f32(col1.v, p, 1)), vaddq_f32(vmulq_laneq_f32(col2.v, p, 2), col3.v));
		vst1q_f32(dst[i].pos, pos);
#else
		Vec4F32 nrm = (col0 * Vec4F32::Splat(src[i].nrm[0] * (1.0f / 128.0f)) + col1 * Vec4F32::Splat(src[i].nrm[1] * (1.0f / 128.0f))) +
			col2 * Vec4F32::Splat(src[i].nrm[2] * (1.0f / 128.0f));
		nrm.Store(dst[i].nrm);
		Vec4F32 pos = (col0 * Vec4F32::Splat(src[i].pos[0]) + col1 * Vec4F32::Splat(src[i].pos[1])) +
			(col2 * Vec4F32::Splat(src[i].pos[2]) + col3);
		pos.Store(dst[i].pos);
#endif
	}
	if ((alpha >> 12) != 0xF)
		gstate_c.vertexFullAlpha = false;
}

// God of War, skinned with one weight.
void VtxDec_W1float_Tu8_C4444_Ns8_Pfloat(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeW1float_Tu8_C4444_Ns8_Pfloat<true>(srcp, dstp, count, uvScaleOffset);
}
void VtxDec_W1float_Tu8_C4444_Ns8_Pfloat_NoPrescale(const u8 *srcp, u8 *dstp, int count, const UVScale *uvScaleOffset) {
	DecodeW1float_Tu8_C4444_Ns8_Pfloat<false>(srcp, dstp, count, uvScaleOffset);
}
