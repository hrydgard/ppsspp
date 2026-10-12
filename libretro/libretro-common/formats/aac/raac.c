/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (raac.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* MPEG-4 AAC-LC decoder (ISO/IEC 14496-3 clause 4). See raac.h for
 * the supported scope. The decode path per access unit: parse the
 * raw_data_block's channel elements (ics_info, section data, scale
 * factors, pulse, TNS, spectral Huffman data), undo channel tools
 * (M/S, intensity, PNS), inverse-quantise (x^(4/3) times the scale
 * factor gain), apply TNS synthesis filtering, and run the inverse
 * MDCT filterbank (via an FFT) with KBD/sine windowing and 50 percent
 * overlap-add. Spec-defined constant tables (Huffman codebooks,
 * scalefactor band offsets, TNS band limits) are transcribed from
 * ISO/IEC 14496-3; windows, twiddles and TNS coefficient maps are
 * computed at open from their defining formulas.
 *
 * What it implements: AAC-LC (audio object type 2) raw access units,
 * channel configurations 1 through 7 and configuration 0 with an
 * embedded PCE (up to eight output channels, interleaved in element
 * transmission order), explicit as well as indexed sampling
 * frequencies, M/S and intensity stereo, PNS, TNS, coupling channel
 * elements (dependently and independently switched, up to four
 * concurrent tags), in-stream PCE and DSE elements (skipped), and
 * both the s16 and f32 output paths from a shared float synthesis
 * pipeline.
 *
 * What it does not implement: other object types (Main, SSR, LTP,
 * HE-AAC's SBR/PS - configurations other than plain LC are refused
 * at open, as is the 960-sample frame variant), downmixing,
 * ADTS/LATM/LOAS framing (the caller supplies the
 * AudioSpecificConfig and raw access units, as rmp4 does), and
 * encoding. */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <retro_inline.h>
#include <formats/raac.h>

/* Four-float vectors for the FFT: SSE2 or NEON where the target has
 * them, chosen at compile time, plain C otherwise or with
 * RAAC_NO_SIMD. */
#if defined(RAAC_NO_SIMD)
#define RAAC_V_SCALAR 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
typedef __m128 raac_v;
#define RAAC_LD(p)      _mm_loadu_ps(p)
#define RAAC_ST(p, v)   _mm_storeu_ps((p), (v))
#define RAAC_ADD(a, b)  _mm_add_ps((a), (b))
#define RAAC_SUB(a, b)  _mm_sub_ps((a), (b))
#define RAAC_MUL(a, b)  _mm_mul_ps((a), (b))
#define RAAC_SET1(x)    _mm_set1_ps(x)
#define RAAC_REV(v)     _mm_shuffle_ps((v), (v), _MM_SHUFFLE(0, 1, 2, 3))
#define RAAC_ZIPLO(a, b) _mm_unpacklo_ps((a), (b))
#define RAAC_ZIPHI(a, b) _mm_unpackhi_ps((a), (b))
#define RAAC_EVEN(a, b) _mm_shuffle_ps((a), (b), _MM_SHUFFLE(2, 0, 2, 0))
#define RAAC_ODD(a, b)  _mm_shuffle_ps((a), (b), _MM_SHUFFLE(3, 1, 3, 1))
/* Eight samples to s16 as the scalar path does it: anything outside
 * +-1e9 (and NaN) to zero, +-0.5 by sign, clamped, truncated */
static INLINE void raac_v_st_s16x8(int16_t *dst, raac_v a, raac_v b)
{
   const __m128 big = _mm_set1_ps(1e9f), nbig = _mm_set1_ps(-1e9f);
   const __m128 half = _mm_set1_ps(0.5f), sgn = _mm_set1_ps(-0.0f);
   const __m128 lo = _mm_set1_ps(-32768.0f), hi = _mm_set1_ps(32767.0f);
   a = _mm_and_ps(a, _mm_and_ps(_mm_cmplt_ps(a, big), _mm_cmpgt_ps(a, nbig)));
   b = _mm_and_ps(b, _mm_and_ps(_mm_cmplt_ps(b, big), _mm_cmpgt_ps(b, nbig)));
   a = _mm_add_ps(a, _mm_or_ps(half, _mm_and_ps(a, sgn)));
   b = _mm_add_ps(b, _mm_or_ps(half, _mm_and_ps(b, sgn)));
   a = _mm_min_ps(_mm_max_ps(a, lo), hi);
   b = _mm_min_ps(_mm_max_ps(b, lo), hi);
   _mm_storeu_si128((__m128i*)dst,
         _mm_packs_epi32(_mm_cvttps_epi32(a), _mm_cvttps_epi32(b)));
}
#define RAAC_HAVE_S16 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
typedef float32x4_t raac_v;
#define RAAC_LD(p)      vld1q_f32(p)
#define RAAC_ST(p, v)   vst1q_f32((p), (v))
#define RAAC_ADD(a, b)  vaddq_f32((a), (b))
#define RAAC_SUB(a, b)  vsubq_f32((a), (b))
#define RAAC_MUL(a, b)  vmulq_f32((a), (b))
#define RAAC_SET1(x)    vdupq_n_f32(x)
static INLINE raac_v raac_v_rev(raac_v v)
{
   v = vrev64q_f32(v);
   return vcombine_f32(vget_high_f32(v), vget_low_f32(v));
}
static INLINE raac_v raac_v_ziplo(raac_v a, raac_v b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[0];
}
static INLINE raac_v raac_v_ziphi(raac_v a, raac_v b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[1];
}
#define RAAC_REV(v)      raac_v_rev(v)
#define RAAC_ZIPLO(a, b) raac_v_ziplo((a), (b))
#define RAAC_ZIPHI(a, b) raac_v_ziphi((a), (b))
static INLINE raac_v raac_v_even(raac_v a, raac_v b)
{
   float32x4x2_t z = vuzpq_f32(a, b);
   return z.val[0];
}
static INLINE raac_v raac_v_odd(raac_v a, raac_v b)
{
   float32x4x2_t z = vuzpq_f32(a, b);
   return z.val[1];
}
#define RAAC_EVEN(a, b) raac_v_even((a), (b))
#define RAAC_ODD(a, b)  raac_v_odd((a), (b))
static INLINE int32x4_t raac_v_s32(raac_v a)
{
   uint32x4_t ok   = vandq_u32(vcltq_f32(a, vdupq_n_f32(1e9f)),
         vcgtq_f32(a, vdupq_n_f32(-1e9f)));
   uint32x4_t neg  = vcltq_f32(a, vdupq_n_f32(0.0f));
   a = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), ok));
   a = vaddq_f32(a, vbslq_f32(neg, vdupq_n_f32(-0.5f), vdupq_n_f32(0.5f)));
   a = vminq_f32(vmaxq_f32(a, vdupq_n_f32(-32768.0f)), vdupq_n_f32(32767.0f));
   return vcvtq_s32_f32(a);
}
static INLINE void raac_v_st_s16x8(int16_t *dst, raac_v a, raac_v b)
{
   vst1q_s16(dst, vcombine_s16(vmovn_s32(raac_v_s32(a)),
            vmovn_s32(raac_v_s32(b))));
}
#define RAAC_HAVE_S16 1
#else
#define RAAC_V_SCALAR 1
#endif
#ifdef RAAC_V_SCALAR
typedef struct { float f[4]; } raac_v;
static INLINE raac_v raac_v_ld(const float *p)
{ raac_v v; v.f[0] = p[0]; v.f[1] = p[1]; v.f[2] = p[2]; v.f[3] = p[3]; return v; }
static INLINE void raac_v_st(float *p, raac_v v)
{ p[0] = v.f[0]; p[1] = v.f[1]; p[2] = v.f[2]; p[3] = v.f[3]; }
static INLINE raac_v raac_v_add(raac_v a, raac_v b)
{ a.f[0] += b.f[0]; a.f[1] += b.f[1]; a.f[2] += b.f[2]; a.f[3] += b.f[3]; return a; }
static INLINE raac_v raac_v_sub(raac_v a, raac_v b)
{ a.f[0] -= b.f[0]; a.f[1] -= b.f[1]; a.f[2] -= b.f[2]; a.f[3] -= b.f[3]; return a; }
static INLINE raac_v raac_v_mul(raac_v a, raac_v b)
{ a.f[0] *= b.f[0]; a.f[1] *= b.f[1]; a.f[2] *= b.f[2]; a.f[3] *= b.f[3]; return a; }
#define RAAC_LD(p)      raac_v_ld(p)
#define RAAC_ST(p, v)   raac_v_st((p), (v))
#define RAAC_ADD(a, b)  raac_v_add((a), (b))
#define RAAC_SUB(a, b)  raac_v_sub((a), (b))
#define RAAC_MUL(a, b)  raac_v_mul((a), (b))
static INLINE raac_v raac_v_set4(float a, float b, float c, float e)
{ raac_v v; v.f[0] = a; v.f[1] = b; v.f[2] = c; v.f[3] = e; return v; }
#define RAAC_SET1(x)     raac_v_set4((x), (x), (x), (x))
#define RAAC_REV(v)      raac_v_set4((v).f[3], (v).f[2], (v).f[1], (v).f[0])
#define RAAC_ZIPLO(a, b) raac_v_set4((a).f[0], (b).f[0], (a).f[1], (b).f[1])
#define RAAC_ZIPHI(a, b) raac_v_set4((a).f[2], (b).f[2], (a).f[3], (b).f[3])
#define RAAC_EVEN(a, b) raac_v_set4((a).f[0], (a).f[2], (b).f[0], (b).f[2])
#define RAAC_ODD(a, b)  raac_v_set4((a).f[1], (a).f[3], (b).f[1], (b).f[3])
#endif
#ifdef RAAC_SBR_TRACE
#include <stdio.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RAAC_FRAME     1024
#define RAAC_MAX_CH    8
#define RAAC_MAX_SFB   51
#define RAAC_MAX_WIN   8
#define RAAC_MAX_CCE   4   /* concurrently active coupling tags      */
#define RAAC_CCE_TARG  8   /* num_coupled_elements is 3 bits, plus 1 */
#define RAAC_CCE_GAIN  16  /* gain lists: 8 CPE targets, two apiece  */

/* quantised spectrum values live in -8191..8191 (13 bit + escape) */
#define RAAC_ESC_BOOK  11

/* section codebook meanings */
#define RAAC_CB_ZERO       0
#define RAAC_CB_INTENSITY2 14
#define RAAC_CB_INTENSITY  15
#define RAAC_CB_NOISE      13

static const uint16_t raac_hcb1_code[81] = {
   0x07f8, 0x01f1, 0x07fd, 0x03f5, 0x0068, 0x03f0, 0x07f7, 0x01ec,
   0x07f5, 0x03f1, 0x0072, 0x03f4, 0x0074, 0x0011, 0x0076, 0x01eb,
   0x006c, 0x03f6, 0x07fc, 0x01e1, 0x07f1, 0x01f0, 0x0061, 0x01f6,
   0x07f2, 0x01ea, 0x07fb, 0x01f2, 0x0069, 0x01ed, 0x0077, 0x0017,
   0x006f, 0x01e6, 0x0064, 0x01e5, 0x0067, 0x0015, 0x0062, 0x0012,
   0x0000, 0x0014, 0x0065, 0x0016, 0x006d, 0x01e9, 0x0063, 0x01e4,
   0x006b, 0x0013, 0x0071, 0x01e3, 0x0070, 0x01f3, 0x07fe, 0x01e7,
   0x07f3, 0x01ef, 0x0060, 0x01ee, 0x07f0, 0x01e2, 0x07fa, 0x03f3,
   0x006a, 0x01e8, 0x0075, 0x0010, 0x0073, 0x01f4, 0x006e, 0x03f7,
   0x07f6, 0x01e0, 0x07f9, 0x03f2, 0x0066, 0x01f5, 0x07ff, 0x01f7,
   0x07f4
};
static const uint8_t raac_hcb1_bits[81] = {
   11, 9, 11, 10, 7, 10, 11, 9, 11, 10, 7, 10, 7, 5, 7, 9,
   7, 10, 11, 9, 11, 9, 7, 9, 11, 9, 11, 9, 7, 9, 7, 5,
   7, 9, 7, 9, 7, 5, 7, 5, 1, 5, 7, 5, 7, 9, 7, 9,
   7, 5, 7, 9, 7, 9, 11, 9, 11, 9, 7, 9, 11, 9, 11, 10,
   7, 9, 7, 5, 7, 9, 7, 10, 11, 9, 11, 10, 7, 9, 11, 9,
   11
};
static const uint16_t raac_hcb2_code[81] = {
   0x01f3, 0x006f, 0x01fd, 0x00eb, 0x0023, 0x00ea, 0x01f7, 0x00e8,
   0x01fa, 0x00f2, 0x002d, 0x0070, 0x0020, 0x0006, 0x002b, 0x006e,
   0x0028, 0x00e9, 0x01f9, 0x0066, 0x00f8, 0x00e7, 0x001b, 0x00f1,
   0x01f4, 0x006b, 0x01f5, 0x00ec, 0x002a, 0x006c, 0x002c, 0x000a,
   0x0027, 0x0067, 0x001a, 0x00f5, 0x0024, 0x0008, 0x001f, 0x0009,
   0x0000, 0x0007, 0x001d, 0x000b, 0x0030, 0x00ef, 0x001c, 0x0064,
   0x001e, 0x000c, 0x0029, 0x00f3, 0x002f, 0x00f0, 0x01fc, 0x0071,
   0x01f2, 0x00f4, 0x0021, 0x00e6, 0x00f7, 0x0068, 0x01f8, 0x00ee,
   0x0022, 0x0065, 0x0031, 0x0002, 0x0026, 0x00ed, 0x0025, 0x006a,
   0x01fb, 0x0072, 0x01fe, 0x0069, 0x002e, 0x00f6, 0x01ff, 0x006d,
   0x01f6
};
static const uint8_t raac_hcb2_bits[81] = {
   9, 7, 9, 8, 6, 8, 9, 8, 9, 8, 6, 7, 6, 5, 6, 7,
   6, 8, 9, 7, 8, 8, 6, 8, 9, 7, 9, 8, 6, 7, 6, 5,
   6, 7, 6, 8, 6, 5, 6, 5, 3, 5, 6, 5, 6, 8, 6, 7,
   6, 5, 6, 8, 6, 8, 9, 7, 9, 8, 6, 8, 8, 7, 9, 8,
   6, 7, 6, 4, 6, 8, 6, 7, 9, 7, 9, 7, 6, 8, 9, 7,
   9
};
static const uint16_t raac_hcb3_code[81] = {
   0x0000, 0x0009, 0x00ef, 0x000b, 0x0019, 0x00f0, 0x01eb, 0x01e6,
   0x03f2, 0x000a, 0x0035, 0x01ef, 0x0034, 0x0037, 0x01e9, 0x01ed,
   0x01e7, 0x03f3, 0x01ee, 0x03ed, 0x1ffa, 0x01ec, 0x01f2, 0x07f9,
   0x07f8, 0x03f8, 0x0ff8, 0x0008, 0x0038, 0x03f6, 0x0036, 0x0075,
   0x03f1, 0x03eb, 0x03ec, 0x0ff4, 0x0018, 0x0076, 0x07f4, 0x0039,
   0x0074, 0x03ef, 0x01f3, 0x01f4, 0x07f6, 0x01e8, 0x03ea, 0x1ffc,
   0x00f2, 0x01f1, 0x0ffb, 0x03f5, 0x07f3, 0x0ffc, 0x00ee, 0x03f7,
   0x7ffe, 0x01f0, 0x07f5, 0x7ffd, 0x1ffb, 0x3ffa, 0xffff, 0x00f1,
   0x03f0, 0x3ffc, 0x01ea, 0x03ee, 0x3ffb, 0x0ff6, 0x0ffa, 0x7ffc,
   0x07f2, 0x0ff5, 0xfffe, 0x03f4, 0x07f7, 0x7ffb, 0x0ff7, 0x0ff9,
   0x7ffa
};
static const uint8_t raac_hcb3_bits[81] = {
   1, 4, 8, 4, 5, 8, 9, 9, 10, 4, 6, 9, 6, 6, 9, 9,
   9, 10, 9, 10, 13, 9, 9, 11, 11, 10, 12, 4, 6, 10, 6, 7,
   10, 10, 10, 12, 5, 7, 11, 6, 7, 10, 9, 9, 11, 9, 10, 13,
   8, 9, 12, 10, 11, 12, 8, 10, 15, 9, 11, 15, 13, 14, 16, 8,
   10, 14, 9, 10, 14, 12, 12, 15, 11, 12, 16, 10, 11, 15, 12, 12,
   15
};
static const uint16_t raac_hcb4_code[81] = {
   0x0007, 0x0016, 0x00f6, 0x0018, 0x0008, 0x00ef, 0x01ef, 0x00f3,
   0x07f8, 0x0019, 0x0017, 0x00ed, 0x0015, 0x0001, 0x00e2, 0x00f0,
   0x0070, 0x03f0, 0x01ee, 0x00f1, 0x07fa, 0x00ee, 0x00e4, 0x03f2,
   0x07f6, 0x03ef, 0x07fd, 0x0005, 0x0014, 0x00f2, 0x0009, 0x0004,
   0x00e5, 0x00f4, 0x00e8, 0x03f4, 0x0006, 0x0002, 0x00e7, 0x0003,
   0x0000, 0x006b, 0x00e3, 0x0069, 0x01f3, 0x00eb, 0x00e6, 0x03f6,
   0x006e, 0x006a, 0x01f4, 0x03ec, 0x01f0, 0x03f9, 0x00f5, 0x00ec,
   0x07fb, 0x00ea, 0x006f, 0x03f7, 0x07f9, 0x03f3, 0x0fff, 0x00e9,
   0x006d, 0x03f8, 0x006c, 0x0068, 0x01f5, 0x03ee, 0x01f2, 0x07f4,
   0x07f7, 0x03f1, 0x0ffe, 0x03ed, 0x01f1, 0x07f5, 0x07fe, 0x03f5,
   0x07fc
};
static const uint8_t raac_hcb4_bits[81] = {
   4, 5, 8, 5, 4, 8, 9, 8, 11, 5, 5, 8, 5, 4, 8, 8,
   7, 10, 9, 8, 11, 8, 8, 10, 11, 10, 11, 4, 5, 8, 4, 4,
   8, 8, 8, 10, 4, 4, 8, 4, 4, 7, 8, 7, 9, 8, 8, 10,
   7, 7, 9, 10, 9, 10, 8, 8, 11, 8, 7, 10, 11, 10, 12, 8,
   7, 10, 7, 7, 9, 10, 9, 11, 11, 10, 12, 10, 9, 11, 11, 10,
   11
};
static const uint16_t raac_hcb5_code[81] = {
   0x1fff, 0x0ff7, 0x07f4, 0x07e8, 0x03f1, 0x07ee, 0x07f9, 0x0ff8,
   0x1ffd, 0x0ffd, 0x07f1, 0x03e8, 0x01e8, 0x00f0, 0x01ec, 0x03ee,
   0x07f2, 0x0ffa, 0x0ff4, 0x03ef, 0x01f2, 0x00e8, 0x0070, 0x00ec,
   0x01f0, 0x03ea, 0x07f3, 0x07eb, 0x01eb, 0x00ea, 0x001a, 0x0008,
   0x0019, 0x00ee, 0x01ef, 0x07ed, 0x03f0, 0x00f2, 0x0073, 0x000b,
   0x0000, 0x000a, 0x0071, 0x00f3, 0x07e9, 0x07ef, 0x01ee, 0x00ef,
   0x0018, 0x0009, 0x001b, 0x00eb, 0x01e9, 0x07ec, 0x07f6, 0x03eb,
   0x01f3, 0x00ed, 0x0072, 0x00e9, 0x01f1, 0x03ed, 0x07f7, 0x0ff6,
   0x07f0, 0x03e9, 0x01ed, 0x00f1, 0x01ea, 0x03ec, 0x07f8, 0x0ff9,
   0x1ffc, 0x0ffc, 0x0ff5, 0x07ea, 0x03f3, 0x03f2, 0x07f5, 0x0ffb,
   0x1ffe
};
static const uint8_t raac_hcb5_bits[81] = {
   13, 12, 11, 11, 10, 11, 11, 12, 13, 12, 11, 10, 9, 8, 9, 10,
   11, 12, 12, 10, 9, 8, 7, 8, 9, 10, 11, 11, 9, 8, 5, 4,
   5, 8, 9, 11, 10, 8, 7, 4, 1, 4, 7, 8, 11, 11, 9, 8,
   5, 4, 5, 8, 9, 11, 11, 10, 9, 8, 7, 8, 9, 10, 11, 12,
   11, 10, 9, 8, 9, 10, 11, 12, 13, 12, 12, 11, 10, 10, 11, 12,
   13
};
static const uint16_t raac_hcb6_code[81] = {
   0x07fe, 0x03fd, 0x01f1, 0x01eb, 0x01f4, 0x01ea, 0x01f0, 0x03fc,
   0x07fd, 0x03f6, 0x01e5, 0x00ea, 0x006c, 0x0071, 0x0068, 0x00f0,
   0x01e6, 0x03f7, 0x01f3, 0x00ef, 0x0032, 0x0027, 0x0028, 0x0026,
   0x0031, 0x00eb, 0x01f7, 0x01e8, 0x006f, 0x002e, 0x0008, 0x0004,
   0x0006, 0x0029, 0x006b, 0x01ee, 0x01ef, 0x0072, 0x002d, 0x0002,
   0x0000, 0x0003, 0x002f, 0x0073, 0x01fa, 0x01e7, 0x006e, 0x002b,
   0x0007, 0x0001, 0x0005, 0x002c, 0x006d, 0x01ec, 0x01f9, 0x00ee,
   0x0030, 0x0024, 0x002a, 0x0025, 0x0033, 0x00ec, 0x01f2, 0x03f8,
   0x01e4, 0x00ed, 0x006a, 0x0070, 0x0069, 0x0074, 0x00f1, 0x03fa,
   0x07ff, 0x03f9, 0x01f6, 0x01ed, 0x01f8, 0x01e9, 0x01f5, 0x03fb,
   0x07fc
};
static const uint8_t raac_hcb6_bits[81] = {
   11, 10, 9, 9, 9, 9, 9, 10, 11, 10, 9, 8, 7, 7, 7, 8,
   9, 10, 9, 8, 6, 6, 6, 6, 6, 8, 9, 9, 7, 6, 4, 4,
   4, 6, 7, 9, 9, 7, 6, 4, 4, 4, 6, 7, 9, 9, 7, 6,
   4, 4, 4, 6, 7, 9, 9, 8, 6, 6, 6, 6, 6, 8, 9, 10,
   9, 8, 7, 7, 7, 7, 8, 10, 11, 10, 9, 9, 9, 9, 9, 10,
   11
};
static const uint16_t raac_hcb7_code[64] = {
   0x0000, 0x0005, 0x0037, 0x0074, 0x00f2, 0x01eb, 0x03ed, 0x07f7,
   0x0004, 0x000c, 0x0035, 0x0071, 0x00ec, 0x00ee, 0x01ee, 0x01f5,
   0x0036, 0x0034, 0x0072, 0x00ea, 0x00f1, 0x01e9, 0x01f3, 0x03f5,
   0x0073, 0x0070, 0x00eb, 0x00f0, 0x01f1, 0x01f0, 0x03ec, 0x03fa,
   0x00f3, 0x00ed, 0x01e8, 0x01ef, 0x03ef, 0x03f1, 0x03f9, 0x07fb,
   0x01ed, 0x00ef, 0x01ea, 0x01f2, 0x03f3, 0x03f8, 0x07f9, 0x07fc,
   0x03ee, 0x01ec, 0x01f4, 0x03f4, 0x03f7, 0x07f8, 0x0ffd, 0x0ffe,
   0x07f6, 0x03f0, 0x03f2, 0x03f6, 0x07fa, 0x07fd, 0x0ffc, 0x0fff
};
static const uint8_t raac_hcb7_bits[64] = {
   1, 3, 6, 7, 8, 9, 10, 11, 3, 4, 6, 7, 8, 8, 9, 9,
   6, 6, 7, 8, 8, 9, 9, 10, 7, 7, 8, 8, 9, 9, 10, 10,
   8, 8, 9, 9, 10, 10, 10, 11, 9, 8, 9, 9, 10, 10, 11, 11,
   10, 9, 9, 10, 10, 11, 12, 12, 11, 10, 10, 10, 11, 11, 12, 12
};
static const uint16_t raac_hcb8_code[64] = {
   0x000e, 0x0005, 0x0010, 0x0030, 0x006f, 0x00f1, 0x01fa, 0x03fe,
   0x0003, 0x0000, 0x0004, 0x0012, 0x002c, 0x006a, 0x0075, 0x00f8,
   0x000f, 0x0002, 0x0006, 0x0014, 0x002e, 0x0069, 0x0072, 0x00f5,
   0x002f, 0x0011, 0x0013, 0x002a, 0x0032, 0x006c, 0x00ec, 0x00fa,
   0x0071, 0x002b, 0x002d, 0x0031, 0x006d, 0x0070, 0x00f2, 0x01f9,
   0x00ef, 0x0068, 0x0033, 0x006b, 0x006e, 0x00ee, 0x00f9, 0x03fc,
   0x01f8, 0x0074, 0x0073, 0x00ed, 0x00f0, 0x00f6, 0x01f6, 0x01fd,
   0x03fd, 0x00f3, 0x00f4, 0x00f7, 0x01f7, 0x01fb, 0x01fc, 0x03ff
};
static const uint8_t raac_hcb8_bits[64] = {
   5, 4, 5, 6, 7, 8, 9, 10, 4, 3, 4, 5, 6, 7, 7, 8,
   5, 4, 4, 5, 6, 7, 7, 8, 6, 5, 5, 6, 6, 7, 8, 8,
   7, 6, 6, 6, 7, 7, 8, 9, 8, 7, 6, 7, 7, 8, 8, 10,
   9, 7, 7, 8, 8, 8, 9, 9, 10, 8, 8, 8, 9, 9, 9, 10
};
static const uint16_t raac_hcb9_code[169] = {
   0x0000, 0x0005, 0x0037, 0x00e7, 0x01de, 0x03ce, 0x03d9, 0x07c8,
   0x07cd, 0x0fc8, 0x0fdd, 0x1fe4, 0x1fec, 0x0004, 0x000c, 0x0035,
   0x0072, 0x00ea, 0x00ed, 0x01e2, 0x03d1, 0x03d3, 0x03e0, 0x07d8,
   0x0fcf, 0x0fd5, 0x0036, 0x0034, 0x0071, 0x00e8, 0x00ec, 0x01e1,
   0x03cf, 0x03dd, 0x03db, 0x07d0, 0x0fc7, 0x0fd4, 0x0fe4, 0x00e6,
   0x0070, 0x00e9, 0x01dd, 0x01e3, 0x03d2, 0x03dc, 0x07cc, 0x07ca,
   0x07de, 0x0fd8, 0x0fea, 0x1fdb, 0x01df, 0x00eb, 0x01dc, 0x01e6,
   0x03d5, 0x03de, 0x07cb, 0x07dd, 0x07dc, 0x0fcd, 0x0fe2, 0x0fe7,
   0x1fe1, 0x03d0, 0x01e0, 0x01e4, 0x03d6, 0x07c5, 0x07d1, 0x07db,
   0x0fd2, 0x07e0, 0x0fd9, 0x0feb, 0x1fe3, 0x1fe9, 0x07c4, 0x01e5,
   0x03d7, 0x07c6, 0x07cf, 0x07da, 0x0fcb, 0x0fda, 0x0fe3, 0x0fe9,
   0x1fe6, 0x1ff3, 0x1ff7, 0x07d3, 0x03d8, 0x03e1, 0x07d4, 0x07d9,
   0x0fd3, 0x0fde, 0x1fdd, 0x1fd9, 0x1fe2, 0x1fea, 0x1ff1, 0x1ff6,
   0x07d2, 0x03d4, 0x03da, 0x07c7, 0x07d7, 0x07e2, 0x0fce, 0x0fdb,
   0x1fd8, 0x1fee, 0x3ff0, 0x1ff4, 0x3ff2, 0x07e1, 0x03df, 0x07c9,
   0x07d6, 0x0fca, 0x0fd0, 0x0fe5, 0x0fe6, 0x1feb, 0x1fef, 0x3ff3,
   0x3ff4, 0x3ff5, 0x0fe0, 0x07ce, 0x07d5, 0x0fc6, 0x0fd1, 0x0fe1,
   0x1fe0, 0x1fe8, 0x1ff0, 0x3ff1, 0x3ff8, 0x3ff6, 0x7ffc, 0x0fe8,
   0x07df, 0x0fc9, 0x0fd7, 0x0fdc, 0x1fdc, 0x1fdf, 0x1fed, 0x1ff5,
   0x3ff9, 0x3ffb, 0x7ffd, 0x7ffe, 0x1fe7, 0x0fcc, 0x0fd6, 0x0fdf,
   0x1fde, 0x1fda, 0x1fe5, 0x1ff2, 0x3ffa, 0x3ff7, 0x3ffc, 0x3ffd,
   0x7fff
};
static const uint8_t raac_hcb9_bits[169] = {
   1, 3, 6, 8, 9, 10, 10, 11, 11, 12, 12, 13, 13, 3, 4, 6,
   7, 8, 8, 9, 10, 10, 10, 11, 12, 12, 6, 6, 7, 8, 8, 9,
   10, 10, 10, 11, 12, 12, 12, 8, 7, 8, 9, 9, 10, 10, 11, 11,
   11, 12, 12, 13, 9, 8, 9, 9, 10, 10, 11, 11, 11, 12, 12, 12,
   13, 10, 9, 9, 10, 11, 11, 11, 12, 11, 12, 12, 13, 13, 11, 9,
   10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 11, 10, 10, 11, 11,
   12, 12, 13, 13, 13, 13, 13, 13, 11, 10, 10, 11, 11, 11, 12, 12,
   13, 13, 14, 13, 14, 11, 10, 11, 11, 12, 12, 12, 12, 13, 13, 14,
   14, 14, 12, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 12,
   11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 15, 15, 13, 12, 12, 12,
   13, 13, 13, 13, 14, 14, 14, 14, 15
};
static const uint16_t raac_hcb10_code[169] = {
   0x0022, 0x0008, 0x001d, 0x0026, 0x005f, 0x00d3, 0x01cf, 0x03d0,
   0x03d7, 0x03ed, 0x07f0, 0x07f6, 0x0ffd, 0x0007, 0x0000, 0x0001,
   0x0009, 0x0020, 0x0054, 0x0060, 0x00d5, 0x00dc, 0x01d4, 0x03cd,
   0x03de, 0x07e7, 0x001c, 0x0002, 0x0006, 0x000c, 0x001e, 0x0028,
   0x005b, 0x00cd, 0x00d9, 0x01ce, 0x01dc, 0x03d9, 0x03f1, 0x0025,
   0x000b, 0x000a, 0x000d, 0x0024, 0x0057, 0x0061, 0x00cc, 0x00dd,
   0x01cc, 0x01de, 0x03d3, 0x03e7, 0x005d, 0x0021, 0x001f, 0x0023,
   0x0027, 0x0059, 0x0064, 0x00d8, 0x00df, 0x01d2, 0x01e2, 0x03dd,
   0x03ee, 0x00d1, 0x0055, 0x0029, 0x0056, 0x0058, 0x0062, 0x00ce,
   0x00e0, 0x00e2, 0x01da, 0x03d4, 0x03e3, 0x07eb, 0x01c9, 0x005e,
   0x005a, 0x005c, 0x0063, 0x00ca, 0x00da, 0x01c7, 0x01ca, 0x01e0,
   0x03db, 0x03e8, 0x07ec, 0x01e3, 0x00d2, 0x00cb, 0x00d0, 0x00d7,
   0x00db, 0x01c6, 0x01d5, 0x01d8, 0x03ca, 0x03da, 0x07ea, 0x07f1,
   0x01e1, 0x00d4, 0x00cf, 0x00d6, 0x00de, 0x00e1, 0x01d0, 0x01d6,
   0x03d1, 0x03d5, 0x03f2, 0x07ee, 0x07fb, 0x03e9, 0x01cd, 0x01c8,
   0x01cb, 0x01d1, 0x01d7, 0x01df, 0x03cf, 0x03e0, 0x03ef, 0x07e6,
   0x07f8, 0x0ffa, 0x03eb, 0x01dd, 0x01d3, 0x01d9, 0x01db, 0x03d2,
   0x03cc, 0x03dc, 0x03ea, 0x07ed, 0x07f3, 0x07f9, 0x0ff9, 0x07f2,
   0x03ce, 0x01e4, 0x03cb, 0x03d8, 0x03d6, 0x03e2, 0x03e5, 0x07e8,
   0x07f4, 0x07f5, 0x07f7, 0x0ffb, 0x07fa, 0x03ec, 0x03df, 0x03e1,
   0x03e4, 0x03e6, 0x03f0, 0x07e9, 0x07ef, 0x0ff8, 0x0ffe, 0x0ffc,
   0x0fff
};
static const uint8_t raac_hcb10_bits[169] = {
   6, 5, 6, 6, 7, 8, 9, 10, 10, 10, 11, 11, 12, 5, 4, 4,
   5, 6, 7, 7, 8, 8, 9, 10, 10, 11, 6, 4, 5, 5, 6, 6,
   7, 8, 8, 9, 9, 10, 10, 6, 5, 5, 5, 6, 7, 7, 8, 8,
   9, 9, 10, 10, 7, 6, 6, 6, 6, 7, 7, 8, 8, 9, 9, 10,
   10, 8, 7, 6, 7, 7, 7, 8, 8, 8, 9, 10, 10, 11, 9, 7,
   7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 9, 8, 8, 8, 8,
   8, 9, 9, 9, 10, 10, 11, 11, 9, 8, 8, 8, 8, 8, 9, 9,
   10, 10, 10, 11, 11, 10, 9, 9, 9, 9, 9, 9, 10, 10, 10, 11,
   11, 12, 10, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 12, 11,
   10, 9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 12, 11, 10, 10, 10,
   10, 10, 10, 11, 11, 12, 12, 12, 12
};
static const uint16_t raac_hcb11_code[289] = {
   0x0000, 0x0006, 0x0019, 0x003d, 0x009c, 0x00c6, 0x01a7, 0x0390,
   0x03c2, 0x03df, 0x07e6, 0x07f3, 0x0ffb, 0x07ec, 0x0ffa, 0x0ffe,
   0x038e, 0x0005, 0x0001, 0x0008, 0x0014, 0x0037, 0x0042, 0x0092,
   0x00af, 0x0191, 0x01a5, 0x01b5, 0x039e, 0x03c0, 0x03a2, 0x03cd,
   0x07d6, 0x00ae, 0x0017, 0x0007, 0x0009, 0x0018, 0x0039, 0x0040,
   0x008e, 0x00a3, 0x00b8, 0x0199, 0x01ac, 0x01c1, 0x03b1, 0x0396,
   0x03be, 0x03ca, 0x009d, 0x003c, 0x0015, 0x0016, 0x001a, 0x003b,
   0x0044, 0x0091, 0x00a5, 0x00be, 0x0196, 0x01ae, 0x01b9, 0x03a1,
   0x0391, 0x03a5, 0x03d5, 0x0094, 0x009a, 0x0036, 0x0038, 0x003a,
   0x0041, 0x008c, 0x009b, 0x00b0, 0x00c3, 0x019e, 0x01ab, 0x01bc,
   0x039f, 0x038f, 0x03a9, 0x03cf, 0x0093, 0x00bf, 0x003e, 0x003f,
   0x0043, 0x0045, 0x009e, 0x00a7, 0x00b9, 0x0194, 0x01a2, 0x01ba,
   0x01c3, 0x03a6, 0x03a7, 0x03bb, 0x03d4, 0x009f, 0x01a0, 0x008f,
   0x008d, 0x0090, 0x0098, 0x00a6, 0x00b6, 0x00c4, 0x019f, 0x01af,
   0x01bf, 0x0399, 0x03bf, 0x03b4, 0x03c9, 0x03e7, 0x00a8, 0x01b6,
   0x00ab, 0x00a4, 0x00aa, 0x00b2, 0x00c2, 0x00c5, 0x0198, 0x01a4,
   0x01b8, 0x038c, 0x03a4, 0x03c4, 0x03c6, 0x03dd, 0x03e8, 0x00ad,
   0x03af, 0x0192, 0x00bd, 0x00bc, 0x018e, 0x0197, 0x019a, 0x01a3,
   0x01b1, 0x038d, 0x0398, 0x03b7, 0x03d3, 0x03d1, 0x03db, 0x07dd,
   0x00b4, 0x03de, 0x01a9, 0x019b, 0x019c, 0x01a1, 0x01aa, 0x01ad,
   0x01b3, 0x038b, 0x03b2, 0x03b8, 0x03ce, 0x03e1, 0x03e0, 0x07d2,
   0x07e5, 0x00b7, 0x07e3, 0x01bb, 0x01a8, 0x01a6, 0x01b0, 0x01b2,
   0x01b7, 0x039b, 0x039a, 0x03ba, 0x03b5, 0x03d6, 0x07d7, 0x03e4,
   0x07d8, 0x07ea, 0x00ba, 0x07e8, 0x03a0, 0x01bd, 0x01b4, 0x038a,
   0x01c4, 0x0392, 0x03aa, 0x03b0, 0x03bc, 0x03d7, 0x07d4, 0x07dc,
   0x07db, 0x07d5, 0x07f0, 0x00c1, 0x07fb, 0x03c8, 0x03a3, 0x0395,
   0x039d, 0x03ac, 0x03ae, 0x03c5, 0x03d8, 0x03e2, 0x03e6, 0x07e4,
   0x07e7, 0x07e0, 0x07e9, 0x07f7, 0x0190, 0x07f2, 0x0393, 0x01be,
   0x01c0, 0x0394, 0x0397, 0x03ad, 0x03c3, 0x03c1, 0x03d2, 0x07da,
   0x07d9, 0x07df, 0x07eb, 0x07f4, 0x07fa, 0x0195, 0x07f8, 0x03bd,
   0x039c, 0x03ab, 0x03a8, 0x03b3, 0x03b9, 0x03d0, 0x03e3, 0x03e5,
   0x07e2, 0x07de, 0x07ed, 0x07f1, 0x07f9, 0x07fc, 0x0193, 0x0ffd,
   0x03dc, 0x03b6, 0x03c7, 0x03cc, 0x03cb, 0x03d9, 0x03da, 0x07d3,
   0x07e1, 0x07ee, 0x07ef, 0x07f5, 0x07f6, 0x0ffc, 0x0fff, 0x019d,
   0x01c2, 0x00b5, 0x00a1, 0x0096, 0x0097, 0x0095, 0x0099, 0x00a0,
   0x00a2, 0x00ac, 0x00a9, 0x00b1, 0x00b3, 0x00bb, 0x00c0, 0x018f,
   0x0004
};
static const uint8_t raac_hcb11_bits[289] = {
   4, 5, 6, 7, 8, 8, 9, 10, 10, 10, 11, 11, 12, 11, 12, 12,
   10, 5, 4, 5, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10,
   11, 8, 6, 5, 5, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10,
   10, 10, 8, 7, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10,
   10, 10, 10, 8, 8, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9,
   10, 10, 10, 10, 8, 8, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9,
   9, 10, 10, 10, 10, 8, 9, 8, 8, 8, 8, 8, 8, 8, 9, 9,
   9, 10, 10, 10, 10, 10, 8, 9, 8, 8, 8, 8, 8, 8, 9, 9,
   9, 10, 10, 10, 10, 10, 10, 8, 10, 9, 8, 8, 9, 9, 9, 9,
   9, 10, 10, 10, 10, 10, 10, 11, 8, 10, 9, 9, 9, 9, 9, 9,
   9, 10, 10, 10, 10, 10, 10, 11, 11, 8, 11, 9, 9, 9, 9, 9,
   9, 10, 10, 10, 10, 10, 11, 10, 11, 11, 8, 11, 10, 9, 9, 10,
   9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 8, 11, 10, 10, 10,
   10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 9, 11, 10, 9,
   9, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 9, 11, 10,
   10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 9, 12,
   10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 12, 12, 9,
   9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9,
   5
};
static const uint16_t raac_hcb_size[11] = {81,81,81,81,81,81,64,64,169,169,289};
static const uint16_t *const raac_hcb_code[11] = {
   raac_hcb1_code, raac_hcb2_code, raac_hcb3_code, raac_hcb4_code, raac_hcb5_code, raac_hcb6_code, raac_hcb7_code, raac_hcb8_code, raac_hcb9_code, raac_hcb10_code, raac_hcb11_code
};
static const uint8_t *const raac_hcb_bits[11] = {
   raac_hcb1_bits, raac_hcb2_bits, raac_hcb3_bits, raac_hcb4_bits, raac_hcb5_bits, raac_hcb6_bits, raac_hcb7_bits, raac_hcb8_bits, raac_hcb9_bits, raac_hcb10_bits, raac_hcb11_bits
};
static const uint32_t raac_sf_code[121] = {
   0x3ffe8, 0x3ffe6, 0x3ffe7, 0x3ffe5, 0x7fff5, 0x7fff1, 0x7ffed, 0x7fff6,
   0x7ffee, 0x7ffef, 0x7fff0, 0x7fffc, 0x7fffd, 0x7ffff, 0x7fffe, 0x7fff7,
   0x7fff8, 0x7fffb, 0x7fff9, 0x3ffe4, 0x7fffa, 0x3ffe3, 0x1ffef, 0x1fff0,
   0x0fff5, 0x1ffee, 0x0fff2, 0x0fff3, 0x0fff4, 0x0fff1, 0x07ff6, 0x07ff7,
   0x03ff9, 0x03ff5, 0x03ff7, 0x03ff3, 0x03ff6, 0x03ff2, 0x01ff7, 0x01ff5,
   0x00ff9, 0x00ff7, 0x00ff6, 0x007f9, 0x00ff4, 0x007f8, 0x003f9, 0x003f7,
   0x003f5, 0x001f8, 0x001f7, 0x000fa, 0x000f8, 0x000f6, 0x00079, 0x0003a,
   0x00038, 0x0001a, 0x0000b, 0x00004, 0x00000, 0x0000a, 0x0000c, 0x0001b,
   0x00039, 0x0003b, 0x00078, 0x0007a, 0x000f7, 0x000f9, 0x001f6, 0x001f9,
   0x003f4, 0x003f6, 0x003f8, 0x007f5, 0x007f4, 0x007f6, 0x007f7, 0x00ff5,
   0x00ff8, 0x01ff4, 0x01ff6, 0x01ff8, 0x03ff8, 0x03ff4, 0x0fff0, 0x07ff4,
   0x0fff6, 0x07ff5, 0x3ffe2, 0x7ffd9, 0x7ffda, 0x7ffdb, 0x7ffdc, 0x7ffdd,
   0x7ffde, 0x7ffd8, 0x7ffd2, 0x7ffd3, 0x7ffd4, 0x7ffd5, 0x7ffd6, 0x7fff2,
   0x7ffdf, 0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb, 0x7ffe6, 0x7ffe0,
   0x7ffe1, 0x7ffe2, 0x7ffe3, 0x7ffe4, 0x7ffe5, 0x7ffd7, 0x7ffec, 0x7fff4,
   0x7fff3
};
static const uint8_t raac_sf_bits[121] = {
   18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 18, 19, 18, 17, 17, 16, 17, 16, 16, 16, 16, 15, 15,
   14, 14, 14, 14, 14, 14, 13, 13, 12, 12, 12, 11, 12, 11, 10, 10,
   10, 9, 9, 8, 8, 8, 7, 6, 6, 5, 4, 3, 1, 4, 4, 5,
   6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11, 12,
   12, 13, 13, 13, 14, 14, 16, 15, 16, 15, 18, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19, 19, 19, 19
};
static const uint16_t raac_swb_offset_1024_16[44] = {
   0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88,
   100, 112, 124, 136, 148, 160, 172, 184, 196, 212, 228, 244,
   260, 280, 300, 320, 344, 368, 396, 424, 456, 492, 532, 572,
   616, 664, 716, 772, 832, 896, 960, 1024
};
static const uint16_t raac_swb_offset_1024_24[48] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
   52, 60, 68, 76, 84, 92, 100, 108, 116, 124, 136, 148,
   160, 172, 188, 204, 220, 240, 260, 284, 308, 336, 364, 396,
   432, 468, 508, 552, 600, 652, 704, 768, 832, 896, 960, 1024
};
static const uint16_t raac_swb_offset_1024_32[52] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48,
   56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 160, 176,
   196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
   544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896,
   928, 960, 992, 1024
};
static const uint16_t raac_swb_offset_1024_48[50] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48,
   56, 64, 72, 80, 88, 96, 108, 120, 132, 144, 160, 176,
   196, 216, 240, 264, 292, 320, 352, 384, 416, 448, 480, 512,
   544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896,
   928, 1024
};
static const uint16_t raac_swb_offset_1024_64[48] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
   48, 52, 56, 64, 72, 80, 88, 100, 112, 124, 140, 156,
   172, 192, 216, 240, 268, 304, 344, 384, 424, 464, 504, 544,
   584, 624, 664, 704, 744, 784, 824, 864, 904, 944, 984, 1024
};
static const uint16_t raac_swb_offset_1024_8[41] = {
   0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132,
   144, 156, 172, 188, 204, 220, 236, 252, 268, 288, 308, 328,
   348, 372, 396, 420, 448, 476, 508, 544, 580, 620, 664, 712,
   764, 820, 880, 944, 1024
};
static const uint16_t raac_swb_offset_1024_96[42] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
   48, 52, 56, 64, 72, 80, 88, 96, 108, 120, 132, 144,
   156, 172, 188, 212, 240, 276, 320, 384, 448, 512, 576, 640,
   704, 768, 832, 896, 960, 1024
};
static const uint16_t *const raac_swb_offset_1024[13] = {
   raac_swb_offset_1024_96, raac_swb_offset_1024_96, raac_swb_offset_1024_64,
   raac_swb_offset_1024_48, raac_swb_offset_1024_48, raac_swb_offset_1024_32,
   raac_swb_offset_1024_24, raac_swb_offset_1024_24, raac_swb_offset_1024_16,
   raac_swb_offset_1024_16, raac_swb_offset_1024_16, raac_swb_offset_1024_8,
   raac_swb_offset_1024_8
};
static const uint16_t raac_swb_offset_128_16[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 60,
   72, 88, 108, 128
};
static const uint16_t raac_swb_offset_128_24[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 64,
   76, 92, 108, 128
};
static const uint16_t raac_swb_offset_128_48[15] = {
   0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80,
   96, 112, 128
};
static const uint16_t raac_swb_offset_128_96[13] = {
   0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 92, 128
};
/* ===== 960-sample frame band tables (14496-3 tables 4.140+) =====
 * The 48 kHz long table also serves 32 kHz and the 96 kHz short table
 * also serves 64 kHz: the layouts coincide, as in the reference
 * tables, so the duplicates are folded. */
static const uint16_t raac_swb_offset_960_96[41] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72,
   80, 88, 96, 108, 120, 132, 144, 156, 172, 188, 212, 240, 276, 320,
   384, 448, 512, 576, 640, 704, 768, 832, 896, 960
};
static const uint16_t raac_swb_offset_960_64[47] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72,
   80, 88, 100, 112, 124, 140, 156, 172, 192, 216, 240, 268, 304,
   344, 384, 424, 464, 504, 544, 584, 624, 664, 704, 744, 784, 824,
   864, 904, 944, 960
};
static const uint16_t raac_swb_offset_960_48[50] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 88,
   96, 108, 120, 132, 144, 160, 176, 196, 216, 240, 264, 292, 320,
   352, 384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704, 736,
   768, 800, 832, 864, 896, 928, 960
};
static const uint16_t raac_swb_offset_960_24[47] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 52, 60, 68, 76, 84,
   92, 100, 108, 116, 124, 136, 148, 160, 172, 188, 204, 220, 240,
   260, 284, 308, 336, 364, 396, 432, 468, 508, 552, 600, 652, 704,
   768, 832, 896, 960
};
static const uint16_t raac_swb_offset_960_16[43] = {
   0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 100, 112, 124, 136,
   148, 160, 172, 184, 196, 212, 228, 244, 260, 280, 300, 320, 344,
   368, 396, 424, 456, 492, 532, 572, 616, 664, 716, 772, 832, 896,
   960
};
static const uint16_t raac_swb_offset_960_8[41] = {
   0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 172,
   188, 204, 220, 236, 252, 268, 288, 308, 328, 348, 372, 396, 420,
   448, 476, 508, 544, 580, 620, 664, 712, 764, 820, 880, 944, 960
};
static const uint16_t *const raac_swb_offset_960[13] = {
   raac_swb_offset_960_96, raac_swb_offset_960_96, raac_swb_offset_960_64,
   raac_swb_offset_960_48, raac_swb_offset_960_48, raac_swb_offset_960_48,
   raac_swb_offset_960_24, raac_swb_offset_960_24, raac_swb_offset_960_16,
   raac_swb_offset_960_16, raac_swb_offset_960_16, raac_swb_offset_960_8,
   raac_swb_offset_960_8
};
static const uint16_t raac_swb_offset_120_96[13] = {
   0, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 92, 120
};
static const uint16_t raac_swb_offset_120_48[15] = {
   0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80, 96, 112, 120
};
static const uint16_t raac_swb_offset_120_24[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 64, 76, 92, 108, 120
};
static const uint16_t raac_swb_offset_120_16[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 60, 72, 88, 108, 120
};
static const uint16_t raac_swb_offset_120_8[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 60, 72, 88, 108, 120
};
static const uint16_t *const raac_swb_offset_120[13] = {
   raac_swb_offset_120_96, raac_swb_offset_120_96, raac_swb_offset_120_96,
   raac_swb_offset_120_48, raac_swb_offset_120_48, raac_swb_offset_120_48,
   raac_swb_offset_120_24, raac_swb_offset_120_24, raac_swb_offset_120_16,
   raac_swb_offset_120_16, raac_swb_offset_120_16, raac_swb_offset_120_8,
   raac_swb_offset_120_8
};
static const uint8_t raac_num_swb_960[13] = {
   40, 40, 46, 49, 49, 49, 46, 46, 42, 42, 42, 40, 40
};
static const uint8_t raac_num_swb_120[13] = {
   12, 12, 12, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15
};

static const uint16_t raac_swb_offset_128_8[16] = {
   0, 4, 8, 12, 16, 20, 24, 28, 36, 44, 52, 60, 72, 88, 108, 128
};
static const uint16_t *const raac_swb_offset_128[13] = {
   raac_swb_offset_128_96, raac_swb_offset_128_96, raac_swb_offset_128_96,
   raac_swb_offset_128_48, raac_swb_offset_128_48, raac_swb_offset_128_48,
   raac_swb_offset_128_24, raac_swb_offset_128_24, raac_swb_offset_128_16,
   raac_swb_offset_128_16, raac_swb_offset_128_16, raac_swb_offset_128_8,
   raac_swb_offset_128_8
};
static const uint8_t raac_num_swb_1024[13] = {
   41, 41, 47, 49, 49, 51, 47, 47, 43, 43, 43, 40, 40
};
static const uint8_t raac_num_swb_128[13] = {
   12, 12, 12, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15
};
/* TNS band limits: the 1024/128 tables also serve 960-sample frames,
 * matching the dominant deployed decoder rather than the separate
 * (near-identical) 960 columns of the reference tables. */
static const uint8_t raac_tns_max_bands_1024[13] = {
   31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39, 39
};
static const uint8_t raac_tns_max_bands_128[13] = {
   9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14
};

static const unsigned raac_sample_rates[13] = {
   96000, 88200, 64000, 48000, 44100, 32000,
   24000, 22050, 16000, 12000, 11025, 8000, 7350
};

/* ===== bit reader (MSB first) ===== */

typedef struct
{
   const uint8_t *buf;
   size_t         size;
   size_t         pos;      /* bit position    */
   int            err;      /* ran off the end */
} raac_bits;

static void raac_bits_init(raac_bits *b, const uint8_t *buf, size_t size)
{
   b->buf  = buf;
   b->size = size;
   b->pos  = 0;
   b->err  = 0;
}

/* Up to 32 bits from the read position, MSB first, zeroes past the end */
static INLINE uint32_t raac_peek32(const raac_bits *b)
{
   size_t   byte = b->pos >> 3;
   unsigned sh   = (unsigned)(b->pos & 7);
   if (byte + 8 <= b->size)
   {
      /* eight bytes big-endian: compilers make it one load and a swap */
      const uint8_t *q = b->buf + byte;
      uint64_t v = ((uint64_t)q[0] << 56) | ((uint64_t)q[1] << 48)
         | ((uint64_t)q[2] << 40) | ((uint64_t)q[3] << 32)
         | ((uint64_t)q[4] << 24) | ((uint64_t)q[5] << 16)
         | ((uint64_t)q[6] << 8)  |  (uint64_t)q[7];
      return (uint32_t)((v << sh) >> 32);
   }
   else
   {
      uint64_t v = 0;
      unsigned k;
      for (k = 0; k < 5; k++)
         v = (v << 8) | (byte + k < b->size ? b->buf[byte + k] : 0u);
      return (uint32_t)(v >> (8 - sh));
   }
}

/* 64 bits from bit pos on, MSB first, zeroes past the end; at least 57
 * of them are from the stream while it lasts */
static INLINE uint64_t raac_peek64(const raac_bits *b, size_t pos)
{
   size_t   byte = pos >> 3;
   uint64_t v;
   if (byte + 8 <= b->size)
   {
      const uint8_t *q = b->buf + byte;
      v = ((uint64_t)q[0] << 56) | ((uint64_t)q[1] << 48)
        | ((uint64_t)q[2] << 40) | ((uint64_t)q[3] << 32)
        | ((uint64_t)q[4] << 24) | ((uint64_t)q[5] << 16)
        | ((uint64_t)q[6] << 8)  |  (uint64_t)q[7];
   }
   else
   {
      unsigned k;
      v = 0;
      for (k = 0; k < 8; k++)
         v = (v << 8) | (byte + k < b->size ? b->buf[byte + k] : 0u);
   }
   return v << (pos & 7);
}

static INLINE uint32_t raac_getbits(raac_bits *b, int n)
{
   uint32_t v;
   if (n <= 0)
      return 0;
   if (b->pos + (size_t)n > b->size * 8)
   {
      /* as far as the data goes, then the error */
      b->pos = b->size * 8;
      b->err = 1;
      return 0;
   }
   v       = raac_peek32(b) >> (32 - n);
   b->pos += (size_t)n;
   return v;
}

/* ===== Huffman decoding =====
 *
 * Two-level lookup: the next RAAC_HUFF_ROOT bits index a root table
 * whose entry is either the symbol and its length, or, for the few
 * prefixes that start longer codes, a subtable indexed by the bits
 * after them. Entries: a symbol as (symbol << 8) | length; a subtable
 * as RAAC_HUFF_SUB | (offset << 5) | bits; 0 for a code that does not
 * exist. Built once at open. */

#define RAAC_HUFF_ROOT     9
#define RAAC_HUFF_SUB      0x80000000u
#define RAAC_HUFF_SPEC_CAP ((1 << RAAC_HUFF_ROOT) + 256)
#define RAAC_HUFF_SF_CAP   ((1 << RAAC_HUFF_ROOT) + 1088)

typedef struct
{
   uint32_t *lut;
} raac_huff;

/* What a spectral codebook's symbol stands for, unpacked at build time
 * so the decode loop reads it straight from the table: quadruple books
 * (1..4) four 4-bit two's complement values from bit 12 down and the
 * count of nonzero ones at bit 16; pair books (5..11) two 6-bit values
 * at bits 6 and 0 and the count at bit 12. cb 0 (scalefactors) keeps
 * the symbol. */
static uint32_t raac_huff_payload(int cb, int i)
{
   int      q[4], j, n, nz = 0;
   uint32_t p = 0;
   if (cb == 0)
      return (uint32_t)i;
   if (cb <= 4)
   {
      int off = cb <= 2 ? 1 : 0;
      q[0] = i / 27 % 3 - off;
      q[1] = i / 9  % 3 - off;
      q[2] = i / 3  % 3 - off;
      q[3] = i      % 3 - off;
      n    = 4;
   }
   else if (cb <= 6)
   {
      q[0] = i / 9 - 4;
      q[1] = i % 9 - 4;
      n    = 2;
   }
   else
   {
      int lav = (cb <= 8) ? 8 : (cb <= 10 ? 13 : 17);
      q[0] = i / lav;
      q[1] = i % lav;
      n    = 2;
   }
   for (j = 0; j < n; j++)
      if (q[j])
         nz++;
   if (n == 4)
   {
      for (j = 0; j < 4; j++)
         p |= ((uint32_t)q[j] & 15u) << (12 - 4 * j);
      return p | ((uint32_t)nz << 16);
   }
   return (((uint32_t)q[0] & 63u) << 6) | ((uint32_t)q[1] & 63u)
      | ((uint32_t)nz << 12);
}

/* code[] as 16- or 32-bit codes, one of the two given; cb as for
 * raac_huff_payload */
static int raac_huff_build(raac_huff *h, uint32_t *lut, size_t cap,
      const uint16_t *code16, const uint32_t *code32,
      const uint8_t *bits, int n, int cb)
{
   size_t   used = (size_t)1 << RAAC_HUFF_ROOT;
   uint8_t  sub[1 << RAAC_HUFF_ROOT];
   int      i;
   h->lut = lut;
   memset(lut, 0, cap * sizeof(*lut));
   memset(sub, 0, sizeof(sub));
   /* the subtable each long-code prefix needs: its longest code */
   for (i = 0; i < n; i++)
      if (bits[i] > RAAC_HUFF_ROOT)
      {
         uint32_t c   = code16 ? code16[i] : code32[i];
         unsigned pre = (unsigned)(c >> (bits[i] - RAAC_HUFF_ROOT));
         unsigned sb  = bits[i] - RAAC_HUFF_ROOT;
         if (sb > sub[pre])
            sub[pre] = sb;
      }
   for (i = 0; i < (1 << RAAC_HUFF_ROOT); i++)
      if (sub[i])
      {
         if (used + ((size_t)1 << sub[i]) > cap)
            return -1;
         lut[i] = RAAC_HUFF_SUB | ((uint32_t)used << 5) | sub[i];
         used  += (size_t)1 << sub[i];
      }
   for (i = 0; i < n; i++)
   {
      uint32_t c = code16 ? code16[i] : code32[i];
      uint32_t e = (raac_huff_payload(cb, i) << 8) | bits[i];
      uint32_t k, fill;
      if (bits[i] <= RAAC_HUFF_ROOT)
      {
         uint32_t pre = c << (RAAC_HUFF_ROOT - bits[i]);
         fill = 1u << (RAAC_HUFF_ROOT - bits[i]);
         for (k = 0; k < fill; k++)
            lut[pre + k] = e;
      }
      else
      {
         uint32_t root = lut[c >> (bits[i] - RAAC_HUFF_ROOT)];
         uint32_t sb   = root & 31;
         uint32_t off  = (root >> 5) & 0x3FFFFFF;
         uint32_t rest = bits[i] - RAAC_HUFF_ROOT;
         uint32_t low  = c & ((1u << rest) - 1);
         fill = 1u << (sb - rest);
         for (k = 0; k < fill; k++)
            lut[off + (low << (sb - rest)) + k] = e;
      }
   }
   return 0;
}

static INLINE int raac_huff_decode(raac_bits *b, const raac_huff *h)
{
   uint32_t peek = raac_peek32(b);
   uint32_t e    = h->lut[peek >> (32 - RAAC_HUFF_ROOT)];
   uint32_t len;
   if (e & RAAC_HUFF_SUB)
      e = h->lut[((e >> 5) & 0x3FFFFFF)
         + ((peek << RAAC_HUFF_ROOT) >> (32 - (e & 31)))];
   len = e & 0xFF;
   if (!len || b->pos + len > b->size * 8)
   {
      b->err = 1;
      return -1;
   }
   b->pos += len;
   return (int)(e >> 8);
}

#define raac_huff_decode_sf(b, h) raac_huff_decode((b), (h))

/* The entry for the code at the top of the 64-bit cc */
#define RAAC_HUFF_LOOKUP(e, h, cc) do { \
   (e) = (h)->lut[(cc) >> (64 - RAAC_HUFF_ROOT)]; \
   if ((e) & RAAC_HUFF_SUB) \
      (e) = (h)->lut[(((e) >> 5) & 0x3FFFFFF) \
         + (uint32_t)(((cc) << RAAC_HUFF_ROOT) >> (64 - ((e) & 31)))]; \
} while (0)

#define RAAC_SEXT4(v) ((int)(((unsigned)(v) & 15u) ^ 8u) - 8)
#define RAAC_SEXT6(v) ((int)(((unsigned)(v) & 63u) ^ 32u) - 32)

/* ===== per-channel and decoder state ===== */

typedef struct
{
   /* ics_info */
   int      window_sequence;      /* 0 long,1 start,2 eight-short,3 stop */
   int      window_shape;         /* 0 sine, 1 KBD                       */
   int      max_sfb;
   int      num_windows;          /* 1 or 8                              */
   int      num_window_groups;
   int      group_len[RAAC_MAX_WIN];
   /* per (group, sfb) */
   uint8_t  band_cb[RAAC_MAX_WIN][RAAC_MAX_SFB];
   int      sf[RAAC_MAX_WIN][RAAC_MAX_SFB];    /* scalefactor / IS pos /
                                                * noise energy          */
   /* tns */
   int      tns_present;
   int      tns_n_filt[RAAC_MAX_WIN];
   int      tns_length[RAAC_MAX_WIN][3];
   int      tns_order[RAAC_MAX_WIN][3];
   int      tns_direction[RAAC_MAX_WIN][3];
   float    tns_coef[RAAC_MAX_WIN][3][20];
   /* spectra */
   float    coef[RAAC_FRAME];
   float    overlap[RAAC_FRAME];               /* second half of the
                                                * previous frame        */
   int      prev_window_shape;
} raac_ch;

/* one coupling channel, bound to an element instance tag. The
 * spectral state persists across frames so time-domain (independently
 * switched) coupling keeps filterbank overlap continuity. */
typedef struct
{
   int      in_use;                    /* slot bound to a tag         */
   int      tag;
   int      present;                   /* parsed in the current frame */
   int      point;                     /* 0 before target TNS,
                                        * 1 between TNS and IMDCT,
                                        * 3 after IMDCT, time domain  */
   int      ntarg;
   uint8_t  targ_is_cpe[RAAC_CCE_TARG];
   uint8_t  targ_tag[RAAC_CCE_TARG];
   uint8_t  targ_sel[RAAC_CCE_TARG];   /* SCE fixed 2; CPE 0..3       */
   float    gain[RAAC_CCE_GAIN][RAAC_MAX_WIN * RAAC_MAX_SFB];
   raac_ch  ch;                        /* the coupling channel        */
   float    time[RAAC_FRAME];          /* CC synthesis for time-
                                        * domain coupling             */
} raac_cce;

/* one decoded channel element of the current access unit, for
 * resolving coupling targets (type, tag) to output channels */
typedef struct
{
   uint8_t  kind;                      /* 0 SCE, 1 CPE, 3 LFE         */
   uint8_t  tag;
   uint8_t  ch;                        /* first claimed channel       */
} raac_elem;

/* the SBR noise phase table (14496-3 table 4.A.88) */
static const float raac_sbr_noise[512][2] = {
   {-0.99948153278296f, -0.59483417516607f},
   {0.97113454393991f, -0.67528515225647f},
   {0.14130051758487f, -0.95090983575689f},
   {-0.47005496701697f, -0.37340549728647f},
   {0.80705063769351f, 0.29653668284408f},
   {-0.38981478896926f, 0.89572605717087f},
   {-0.01053049862020f, -0.66959058036166f},
   {-0.91266367957293f, -0.11522938140034f},
   {0.54840422910309f, 0.75221367176302f},
   {0.40009252867955f, -0.98929400334421f},
   {-0.99867974711855f, -0.88147068645358f},
   {-0.95531076805040f, 0.90908757154593f},
   {-0.45725933317144f, -0.56716323646760f},
   {-0.72929675029275f, -0.98008272727324f},
   {0.75622801399036f, 0.20950329995549f},
   {0.07069442601050f, -0.78247898470706f},
   {0.74496252926055f, -0.91169004445807f},
   {-0.96440182703856f, -0.94739918296622f},
   {0.30424629369539f, -0.49438267012479f},
   {0.66565033746925f, 0.64652935542491f},
   {0.91697008020594f, 0.17514097332009f},
   {-0.70774918760427f, 0.52548653416543f},
   {-0.70051415345560f, -0.45340028808763f},
   {-0.99496513054797f, -0.90071908066973f},
   {0.98164490790123f, -0.77463155528697f},
   {-0.54671580548181f, -0.02570928536004f},
   {-0.01689629065389f, 0.00287506445732f},
   {-0.86110349531986f, 0.42548583726477f},
   {-0.98892980586032f, -0.87881132267556f},
   {0.51756627678691f, 0.66926784710139f},
   {-0.99635026409640f, -0.58107730574765f},
   {-0.99969370862163f, 0.98369989360250f},
   {0.55266258627194f, 0.59449057465591f},
   {0.34581177741673f, 0.94879421061866f},
   {0.62664209577999f, -0.74402970906471f},
   {-0.77149701404973f, -0.33883658042801f},
   {-0.91592244254432f, 0.03687901376713f},
   {-0.76285492357887f, -0.91371867919124f},
   {0.79788337195331f, -0.93180971199849f},
   {0.54473080610200f, -0.11919206037186f},
   {-0.85639281671058f, 0.42429854760451f},
   {-0.92882402971423f, 0.27871809078609f},
   {-0.11708371046774f, -0.99800843444966f},
   {0.21356749817493f, -0.90716295627033f},
   {-0.76191692573909f, 0.99768118356265f},
   {0.98111043100884f, -0.95854459734407f},
   {-0.85913269895572f, 0.95766566168880f},
   {-0.93307242253692f, 0.49431757696466f},
   {0.30485754879632f, -0.70540034357529f},
   {0.85289650925190f, 0.46766131791044f},
   {0.91328082618125f, -0.99839597361769f},
   {-0.05890199924154f, 0.70741827819497f},
   {0.28398686150148f, 0.34633555702188f},
   {0.95258164539612f, -0.54893416026939f},
   {-0.78566324168507f, -0.75568541079691f},
   {-0.95789495447877f, -0.20423194696966f},
   {0.82411158711197f, 0.96654618432562f},
   {-0.65185446735885f, -0.88734990773289f},
   {-0.93643603134666f, 0.99870790442385f},
   {0.91427159529618f, -0.98290505544444f},
   {-0.70395684036886f, 0.58796798221039f},
   {0.00563771969365f, 0.61768196727244f},
   {0.89065051931895f, 0.52783352697585f},
   {-0.68683707712762f, 0.80806944710339f},
   {0.72165342518718f, -0.69259857349564f},
   {-0.62928247730667f, 0.13627037407335f},
   {0.29938434065514f, -0.46051329682246f},
   {-0.91781958879280f, -0.74012716684186f},
   {0.99298717043688f, 0.40816610075661f},
   {0.82368298622748f, -0.74036047190173f},
   {-0.98512833386833f, -0.99972330709594f},
   {-0.95915368242257f, -0.99237800466040f},
   {-0.21411126572790f, -0.93424819052545f},
   {-0.68821476106884f, -0.26892306315457f},
   {0.91851997982317f, 0.09358228901785f},
   {-0.96062769559127f, 0.36099095133739f},
   {0.51646184922287f, -0.71373332873917f},
   {0.61130721139669f, 0.46950141175917f},
   {0.47336129371299f, -0.27333178296162f},
   {0.90998308703519f, 0.96715662938132f},
   {0.44844799194357f, 0.99211574628306f},
   {0.66614891079092f, 0.96590176169121f},
   {0.74922239129237f, -0.89879858826087f},
   {-0.99571588506485f, 0.52785521494349f},
   {0.97401082477563f, -0.16855870075190f},
   {0.72683747733879f, -0.48060774432251f},
   {0.95432193457128f, 0.68849603408441f},
   {-0.72962208425191f, -0.76608443420917f},
   {-0.85359479233537f, 0.88738125901579f},
   {-0.81412430338535f, -0.97480768049637f},
   {-0.87930772356786f, 0.74748307690436f},
   {-0.71573331064977f, -0.98570608178923f},
   {0.83524300028228f, 0.83702537075163f},
   {-0.48086065601423f, -0.98848504923531f},
   {0.97139128574778f, 0.80093621198236f},
   {0.51992825347895f, 0.80247631400510f},
   {-0.00848591195325f, -0.76670128000486f},
   {-0.70294374303036f, 0.55359910445577f},
   {-0.95894428168140f, -0.43265504344783f},
   {0.97079252950321f, 0.09325857238682f},
   {-0.92404293670797f, 0.85507704027855f},
   {-0.69506469500450f, 0.98633412625459f},
   {0.26559203620024f, 0.73314307966524f},
   {0.28038443336943f, 0.14537913654427f},
   {-0.74138124825523f, 0.99310339807762f},
   {-0.01752795995444f, -0.82616635284178f},
   {-0.55126773094930f, -0.98898543862153f},
   {0.97960898850996f, -0.94021446752851f},
   {-0.99196309146936f, 0.67019017358456f},
   {-0.67684928085260f, 0.12631491649378f},
   {0.09140039465500f, -0.20537731453108f},
   {-0.71658965751996f, -0.97788200391224f},
   {0.81014640078925f, 0.53722648362443f},
   {0.40616991671205f, -0.26469008598449f},
   {-0.67680188682972f, 0.94502052337695f},
   {0.86849774348749f, -0.18333598647899f},
   {-0.99500381284851f, -0.02634122068550f},
   {0.84329189340667f, 0.10406957462213f},
   {-0.09215968531446f, 0.69540012101253f},
   {0.99956173327206f, -0.12358542001404f},
   {-0.79732779473535f, -0.91582524736159f},
   {0.96349973642406f, 0.96640458041000f},
   {-0.79942778496547f, 0.64323902822857f},
   {-0.11566039853896f, 0.28587846253726f},
   {-0.39922954514662f, 0.94129601616966f},
   {0.99089197565987f, -0.92062625581587f},
   {0.28631285179909f, -0.91035047143603f},
   {-0.83302725605608f, -0.67330410892084f},
   {0.95404443402072f, 0.49162765398743f},
   {-0.06449863579434f, 0.03250560813135f},
   {-0.99575054486311f, 0.42389784469507f},
   {-0.65501142790847f, 0.82546114655624f},
   {-0.81254441908887f, -0.51627234660629f},
   {-0.99646369485481f, 0.84490533520752f},
   {0.00287840603348f, 0.64768261158166f},
   {0.70176989408455f, -0.20453028573322f},
   {0.96361882270190f, 0.40706967140989f},
   {-0.68883758192426f, 0.91338958840772f},
   {-0.34875585502238f, 0.71472290693300f},
   {0.91980081243087f, 0.66507455644919f},
   {-0.99009048343881f, 0.85868021604848f},
   {0.68865791458395f, 0.55660316809678f},
   {-0.99484402129368f, -0.20052559254934f},
   {0.94214511408023f, -0.99696425367461f},
   {-0.67414626793544f, 0.49548221180078f},
   {-0.47339353684664f, -0.85904328834047f},
   {0.14323651387360f, -0.94145598222488f},
   {-0.29268293575672f, 0.05759224927952f},
   {0.43793861458754f, -0.78904969892724f},
   {-0.36345126374441f, 0.64874435357162f},
   {-0.08750604656825f, 0.97686944362527f},
   {-0.96495267812511f, -0.53960305946511f},
   {0.55526940659947f, 0.78891523734774f},
   {0.73538215752630f, 0.96452072373404f},
   {-0.30889773919437f, -0.80664389776860f},
   {0.03574995626194f, -0.97325616900959f},
   {0.98720684660488f, 0.48409133691962f},
   {-0.81689296271203f, -0.90827703628298f},
   {0.67866860118215f, 0.81284503870856f},
   {-0.15808569732583f, 0.85279555024382f},
   {0.80723395114371f, -0.24717418514605f},
   {0.47788757329038f, -0.46333147839295f},
   {0.96367554763201f, 0.38486749303242f},
   {-0.99143875716818f, -0.24945277239809f},
   {0.83081876925833f, -0.94780851414763f},
   {-0.58753191905341f, 0.01290772389163f},
   {0.95538108220960f, -0.85557052096538f},
   {-0.96490920476211f, -0.64020970923102f},
   {-0.97327101028521f, 0.12378128133110f},
   {0.91400366022124f, 0.57972471346930f},
   {-0.99925837363824f, 0.71084847864067f},
   {-0.86875903507313f, -0.20291699203564f},
   {-0.26240034795124f, -0.68264554369108f},
   {-0.24664412953388f, -0.87642273115183f},
   {0.02416275806869f, 0.27192914288905f},
   {0.82068619590515f, -0.85087787994476f},
   {0.88547373760759f, -0.89636802901469f},
   {-0.18173078152226f, -0.26152145156800f},
   {0.09355476558534f, 0.54845123045604f},
   {-0.54668414224090f, 0.95980774020221f},
   {0.37050990604091f, -0.59910140383171f},
   {-0.70373594262891f, 0.91227665827081f},
   {-0.34600785879594f, -0.99441426144200f},
   {-0.68774481731008f, -0.30238837956299f},
   {-0.26843291251234f, 0.83115668004362f},
   {0.49072334613242f, -0.45359708737775f},
   {0.38975993093975f, 0.95515358099121f},
   {-0.97757125224150f, 0.05305894580606f},
   {-0.17325552859616f, -0.92770672250494f},
   {0.99948035025744f, 0.58285545563426f},
   {-0.64946246527458f, 0.68645507104960f},
   {-0.12016920576437f, -0.57147322153312f},
   {-0.58947456517751f, -0.34847132454388f},
   {-0.41815140454465f, 0.16276422358861f},
   {0.99885650204884f, 0.11136095490444f},
   {-0.56649614128386f, -0.90494866361587f},
   {0.94138021032330f, 0.35281916733018f},
   {-0.75725076534641f, 0.53650549640587f},
   {0.20541973692630f, -0.94435144369918f},
   {0.99980371023351f, 0.79835913565599f},
   {0.29078277605775f, 0.35393777921520f},
   {-0.62858772103030f, 0.38765693387102f},
   {0.43440904467688f, -0.98546330463232f},
   {-0.98298583762390f, 0.21021524625209f},
   {0.19513029146934f, -0.94239832251867f},
   {-0.95476662400101f, 0.98364554179143f},
   {0.93379635304810f, -0.70881994583682f},
   {-0.85235410573336f, -0.08342347966410f},
   {-0.86425093011245f, -0.45795025029466f},
   {0.38879779059045f, 0.97274429344593f},
   {0.92045124735495f, -0.62433652524220f},
   {0.89162532251878f, 0.54950955570563f},
   {-0.36834336949252f, 0.96458298020975f},
   {0.93891760988045f, -0.89968353740388f},
   {0.99267657565094f, -0.03757034316958f},
   {-0.94063471614176f, 0.41332338538963f},
   {0.99740224117019f, -0.16830494996370f},
   {-0.35899413170555f, -0.46633226649613f},
   {0.05237237274947f, -0.25640361602661f},
   {0.36703583957424f, -0.38653265641875f},
   {0.91653180367913f, -0.30587628726597f},
   {0.69000803499316f, 0.90952171386132f},
   {-0.38658751133527f, 0.99501571208985f},
   {-0.29250814029851f, 0.37444994344615f},
   {-0.60182204677608f, 0.86779651036123f},
   {-0.97418588163217f, 0.96468523666475f},
   {0.88461574003963f, 0.57508405276414f},
   {0.05198933055162f, 0.21269661669964f},
   {-0.53499621979720f, 0.97241553731237f},
   {-0.49429560226497f, 0.98183865291903f},
   {-0.98935142339139f, -0.40249159006933f},
   {-0.98081380091130f, -0.72856895534041f},
   {-0.27338148835532f, 0.99950922447209f},
   {0.06310802338302f, -0.54539587529618f},
   {-0.20461677199539f, -0.14209977628489f},
   {0.66223843141647f, 0.72528579940326f},
   {-0.84764345483665f, 0.02372316801261f},
   {-0.89039863483811f, 0.88866581484602f},
   {0.95903308477986f, 0.76744927173873f},
   {0.73504123909879f, -0.03747203173192f},
   {-0.31744434966056f, -0.36834111883652f},
   {-0.34110827591623f, 0.40211222807691f},
   {0.47803883714199f, -0.39423219786288f},
   {0.98299195879514f, 0.01989791390047f},
   {-0.30963073129751f, -0.18076720599336f},
   {0.99992588229018f, -0.26281872094289f},
   {-0.93149731080767f, -0.98313162570490f},
   {0.99923472302773f, -0.80142993767554f},
   {-0.26024169633417f, -0.75999759855752f},
   {-0.35712514743563f, 0.19298963768574f},
   {-0.99899084509530f, 0.74645156992493f},
   {0.86557171579452f, 0.55593866696299f},
   {0.33408042438752f, 0.86185953874709f},
   {0.99010736374716f, 0.04602397576623f},
   {-0.66694269691195f, -0.91643611810148f},
   {0.64016792079480f, 0.15649530836856f},
   {0.99570534804836f, 0.45844586038111f},
   {-0.63431466947340f, 0.21079116459234f},
   {-0.07706847005931f, -0.89581437101329f},
   {0.98590090577724f, 0.88241721133981f},
   {0.80099335254678f, -0.36851896710853f},
   {0.78368131392666f, 0.45506999802597f},
   {0.08707806671691f, 0.80938994918745f},
   {-0.86811883080712f, 0.39347308654705f},
   {-0.39466529740375f, -0.66809432114456f},
   {0.97875325649683f, -0.72467840967746f},
   {-0.95038560288864f, 0.89563219587625f},
   {0.17005239424212f, 0.54683053962658f},
   {-0.76910792026848f, -0.96226617549298f},
   {0.99743281016846f, 0.42697157037567f},
   {0.95437383549973f, 0.97002324109952f},
   {0.99578905365569f, -0.54106826257356f},
   {0.28058259829990f, -0.85361420634036f},
   {0.85256524470573f, -0.64567607735589f},
   {-0.50608540105128f, -0.65846015480300f},
   {-0.97210735183243f, -0.23095213067791f},
   {0.95424048234441f, -0.99240147091219f},
   {-0.96926570524023f, 0.73775654896574f},
   {0.30872163214726f, 0.41514960556126f},
   {-0.24523839572639f, 0.63206633394807f},
   {-0.33813265086024f, -0.38661779441897f},
   {-0.05826828420146f, -0.06940774188029f},
   {-0.22898461455054f, 0.97054853316316f},
   {-0.18509915019881f, 0.47565762892084f},
   {-0.10488238045009f, -0.87769947402394f},
   {-0.71886586182037f, 0.78030982480538f},
   {0.99793873738654f, 0.90041310491497f},
   {0.57563307626120f, -0.91034337352097f},
   {0.28909646383717f, 0.96307783970534f},
   {0.42188998312520f, 0.48148651230437f},
   {0.93335049681047f, -0.43537023883588f},
   {-0.97087374418267f, 0.86636445711364f},
   {0.36722871286923f, 0.65291654172961f},
   {-0.81093025665696f, 0.08778370229363f},
   {-0.26240603062237f, -0.92774095379098f},
   {0.83996497984604f, 0.55839849139647f},
   {-0.99909615720225f, -0.96024605713970f},
   {0.74649464155061f, 0.12144893606462f},
   {-0.74774595569805f, -0.26898062008959f},
   {0.95781667469567f, -0.79047927052628f},
   {0.95472308713099f, -0.08588776019550f},
   {0.48708332746299f, 0.99999041579432f},
   {0.46332038247497f, 0.10964126185063f},
   {-0.76497004940162f, 0.89210929242238f},
   {0.57397389364339f, 0.35289703373760f},
   {0.75374316974495f, 0.96705214651335f},
   {-0.59174397685714f, -0.89405370422752f},
   {0.75087906691890f, -0.29612672982396f},
   {-0.98607857336230f, 0.25034911730023f},
   {-0.40761056640505f, -0.90045573444695f},
   {0.66929266740477f, 0.98629493401748f},
   {-0.97463695257310f, -0.00190223301301f},
   {0.90145509409859f, 0.99781390365446f},
   {-0.87259289048043f, 0.99233587353666f},
   {-0.91529461447692f, -0.15698707534206f},
   {-0.03305738840705f, -0.37205262859764f},
   {0.07223051368337f, -0.88805001733626f},
   {0.99498012188353f, 0.97094358113387f},
   {-0.74904939500519f, 0.99985483641521f},
   {0.04585228574211f, 0.99812337444082f},
   {-0.89054954257993f, -0.31791913188064f},
   {-0.83782144651251f, 0.97637632547466f},
   {0.33454804933804f, -0.86231516800408f},
   {-0.99707579362824f, 0.93237990079441f},
   {-0.22827527843994f, 0.18874759397997f},
   {0.67248046289143f, -0.03646211390569f},
   {-0.05146538187944f, -0.92599700120679f},
   {0.99947295749905f, 0.93625229707912f},
   {0.66951124390363f, 0.98905825623893f},
   {-0.99602956559179f, -0.44654715757688f},
   {0.82104905483590f, 0.99540741724928f},
   {0.99186510988782f, 0.72023001312947f},
   {-0.65284592392918f, 0.52186723253637f},
   {0.93885443798188f, -0.74895312615259f},
   {0.96735248738388f, 0.90891816978629f},
   {-0.22225968841114f, 0.57124029781228f},
   {-0.44132783753414f, -0.92688840659280f},
   {-0.85694974219574f, 0.88844532719844f},
   {0.91783042091762f, -0.46356892383970f},
   {0.72556974415690f, -0.99899555770747f},
   {-0.99711581834508f, 0.58211560180426f},
   {0.77638976371966f, 0.94321834873819f},
   {0.07717324253925f, 0.58638399856595f},
   {-0.56049829194163f, 0.82522301569036f},
   {0.98398893639988f, 0.39467440420569f},
   {0.47546946844938f, 0.68613044836811f},
   {0.65675089314631f, 0.18331637134880f},
   {0.03273375457980f, -0.74933109564108f},
   {-0.38684144784738f, 0.51337349030406f},
   {-0.97346267944545f, -0.96549364384098f},
   {-0.53282156061942f, -0.91423265091354f},
   {0.99817310731176f, 0.61133572482148f},
   {-0.50254500772635f, -0.88829338134294f},
   {0.01995873238855f, 0.85223515096765f},
   {0.99930381973804f, 0.94578896296649f},
   {0.82907767600783f, -0.06323442598128f},
   {-0.58660709669728f, 0.96840773806582f},
   {-0.17573736667267f, -0.48166920859485f},
   {0.83434292401346f, -0.13023450646997f},
   {0.05946491307025f, 0.20511047074866f},
   {0.81505484574602f, -0.94685947861369f},
   {-0.44976380954860f, 0.40894572671545f},
   {-0.89746474625671f, 0.99846578838537f},
   {0.39677256130792f, -0.74854668609359f},
   {-0.07588948563079f, 0.74096214084170f},
   {0.76343198951445f, 0.41746629422634f},
   {-0.74490104699626f, 0.94725911744610f},
   {0.64880119792759f, 0.41336660830571f},
   {0.62319537462542f, -0.93098313552599f},
   {0.42215817594807f, -0.07712787385208f},
   {0.02704554141885f, -0.05417518053666f},
   {0.80001773566818f, 0.91542195141039f},
   {-0.79351832348816f, -0.36208897989136f},
   {0.63872359151636f, 0.08128252493444f},
   {0.52890520960295f, 0.60048872455592f},
   {0.74238552914587f, 0.04491915291044f},
   {0.99096131449250f, -0.19451182854402f},
   {-0.80412329643109f, -0.88513818199457f},
   {-0.64612616129736f, 0.72198674804544f},
   {0.11657770663191f, -0.83662833815041f},
   {-0.95053182488101f, -0.96939905138082f},
   {-0.62228872928622f, 0.82767262846661f},
   {0.03004475787316f, -0.99738896333384f},
   {-0.97987214341034f, 0.36526129686425f},
   {-0.99986980746200f, -0.36021610299715f},
   {0.89110648599879f, -0.97894250343044f},
   {0.10407960510582f, 0.77357793811619f},
   {0.95964737821728f, -0.35435818285502f},
   {0.50843233159162f, 0.96107691266205f},
   {0.17006334670615f, -0.76854025314829f},
   {0.25872675063360f, 0.99893303933816f},
   {-0.01115998681937f, 0.98496019742444f},
   {-0.79598702973261f, 0.97138411318894f},
   {-0.99264708948101f, -0.99542822402536f},
   {-0.99829663752818f, 0.01877138824311f},
   {-0.70801016548184f, 0.33680685948117f},
   {-0.70467057786826f, 0.93272777501857f},
   {0.99846021905254f, -0.98725746254433f},
   {-0.63364968534650f, -0.16473594423746f},
   {-0.16258217500792f, -0.95939125400802f},
   {-0.43645594360633f, -0.94805030113284f},
   {-0.99848471702976f, 0.96245166923809f},
   {-0.16796458968998f, -0.98987511890470f},
   {-0.87979225745213f, -0.71725725041680f},
   {0.44183099021786f, -0.93568974498761f},
   {0.93310180125532f, -0.99913308068246f},
   {-0.93941931782002f, -0.56409379640356f},
   {-0.88590003188677f, 0.47624600491382f},
   {0.99971463703691f, -0.83889954253462f},
   {-0.75376385639978f, 0.00814643438625f},
   {0.93887685615875f, -0.11284528204636f},
   {0.85126435782309f, 0.52349251543547f},
   {0.39701421446381f, 0.81779634174316f},
   {-0.37024464187437f, -0.87071656222959f},
   {-0.36024828242896f, 0.34655735648287f},
   {-0.93388812549209f, -0.84476541096429f},
   {-0.65298804552119f, -0.18439575450921f},
   {0.11960319006843f, 0.99899346780168f},
   {0.94292565553160f, 0.83163906518293f},
   {0.75081145286948f, -0.35533223142265f},
   {0.56721979748394f, -0.24076836414499f},
   {0.46857766746029f, -0.30140233457198f},
   {0.97312313923635f, -0.99548191630031f},
   {-0.38299976567017f, 0.98516909715427f},
   {0.41025800019463f, 0.02116736935734f},
   {0.09638062008048f, 0.04411984381457f},
   {-0.85283249275397f, 0.91475563922421f},
   {0.88866808958124f, -0.99735267083226f},
   {-0.48202429536989f, -0.96805608884164f},
   {0.27572582416567f, 0.58634753335832f},
   {-0.65889129659168f, 0.58835634138583f},
   {0.98838086953732f, 0.99994349600236f},
   {-0.20651349620689f, 0.54593044066355f},
   {-0.62126416356920f, -0.59893681700392f},
   {0.20320105410437f, -0.86879180355289f},
   {-0.97790548600584f, 0.96290806999242f},
   {0.11112534735126f, 0.21484763313301f},
   {-0.41368337314182f, 0.28216837680365f},
   {0.24133038992960f, 0.51294362630238f},
   {-0.66393410674885f, -0.08249679629081f},
   {-0.53697829178752f, -0.97649903936228f},
   {-0.97224737889348f, 0.22081333579837f},
   {0.87392477144549f, -0.12796173740361f},
   {0.19050361015753f, 0.01602615387195f},
   {-0.46353441212724f, -0.95249041539006f},
   {-0.07064096339021f, -0.94479803205886f},
   {-0.92444085484466f, -0.10457590187436f},
   {-0.83822593578728f, -0.01695043208885f},
   {0.75214681811150f, -0.99955681042665f},
   {-0.42102998829339f, 0.99720941999394f},
   {-0.72094786237696f, -0.35008961934255f},
   {0.78843311019251f, 0.52851398958271f},
   {0.97394027897442f, -0.26695944086561f},
   {0.99206463477946f, -0.57010120849429f},
   {0.76789609461795f, -0.76519356730966f},
   {-0.82002421836409f, -0.73530179553767f},
   {0.81924990025724f, 0.99698425250579f},
   {-0.26719850873357f, 0.68903369776193f},
   {-0.43311260380975f, 0.85321815947490f},
   {0.99194979673836f, 0.91876249766422f},
   {-0.80692001248487f, -0.32627540663214f},
   {0.43080003649976f, -0.21919095636638f},
   {0.67709491937357f, -0.95478075822906f},
   {0.56151770568316f, -0.70693811747778f},
   {0.10831862810749f, -0.08628837174592f},
   {0.91229417540436f, -0.65987351408410f},
   {-0.48972893932274f, 0.56289246362686f},
   {-0.89033658689697f, -0.71656563987082f},
   {0.65269447475094f, 0.65916004833932f},
   {0.67439478141121f, -0.81684380846796f},
   {-0.47770832416973f, -0.16789556203025f},
   {-0.99715979260878f, -0.93565784007648f},
   {-0.90889593602546f, 0.62034397054380f},
   {-0.06618622548177f, -0.23812217221359f},
   {0.99430266919728f, 0.18812555317553f},
   {0.97686402381843f, -0.28664534366620f},
   {0.94813650221268f, -0.97506640027128f},
   {-0.95434497492853f, -0.79607978501983f},
   {-0.49104783137150f, 0.32895214359663f},
   {0.99881175120751f, 0.88993983831354f},
   {0.50449166760303f, -0.85995072408434f},
   {0.47162891065108f, -0.18680204049569f},
   {-0.62081581361840f, 0.75000676218956f},
   {-0.43867015250812f, 0.99998069244322f},
   {0.98630563232075f, -0.53578899600662f},
   {-0.61510362277374f, -0.89515019899997f},
   {-0.03841517601843f, -0.69888815681179f},
   {-0.30102157304644f, -0.07667808922205f},
   {0.41881284182683f, 0.02188098922282f},
   {-0.86135454941237f, 0.98947480909359f},
   {0.67226861393788f, -0.13494389011014f},
   {-0.70737398842068f, -0.76547349325992f},
   {0.94044946687963f, 0.09026201157416f},
   {-0.82386352534327f, 0.08924768823676f},
   {-0.32070666698656f, 0.50143421908753f},
   {0.57593163224487f, -0.98966422921509f},
   {-0.36326018419965f, 0.07440243123228f},
   {0.99979044674350f, -0.14130287347405f},
   {-0.92366023326932f, -0.97979298068180f},
   {-0.44607178518598f, -0.54233252016394f},
   {0.44226800932956f, 0.71326756742752f},
   {0.03671907158312f, 0.63606389366675f},
   {0.52175424682195f, -0.85396826735705f},
   {-0.94701139690956f, -0.01826348194255f},
   {-0.98759606946049f, 0.82288714303073f},
   {0.87434794743625f, 0.89399495655433f},
   {-0.93412041758744f, 0.41374052024363f},
   {0.96063943315511f, 0.93116709541280f},
   {0.97534253457837f, 0.86150930812689f},
   {0.99642466504163f, 0.70190043427512f},
   {-0.94705089665984f, -0.29580042814306f},
   {0.91599807087376f, -0.98147830385781f}
};

/* ===== SBR QMF bank ===== */
/* the 640-tap QMF prototype filter (14496-3 table 4.A.87) in its
 * even-symmetric extension with the spec's two sign-flipped taps
 * (indices 384 and 512) applied, as the explicit filterbank
 * formulation requires; the 32-band analysis of the core signal
 * uses every other tap */
static const float raac_qmf_window[640] = {
   0.0000000000f, -0.0005525286f, -0.0005617692f, -0.0004947518f,
   -0.0004875227f, -0.0004893791f, -0.0005040714f, -0.0005226564f,
   -0.0005466565f, -0.0005677802f, -0.0005870930f, -0.0006132747f,
   -0.0006312493f, -0.0006540333f, -0.0006777690f, -0.0006941614f,
   -0.0007157736f, -0.0007255043f, -0.0007440941f, -0.0007490598f,
   -0.0007681371f, -0.0007724848f, -0.0007834332f, -0.0007779869f,
   -0.0007803664f, -0.0007801449f, -0.0007757977f, -0.0007630793f,
   -0.0007530001f, -0.0007319357f, -0.0007215391f, -0.0006917937f,
   -0.0006650415f, -0.0006341594f, -0.0005946118f, -0.0005564576f,
   -0.0005145572f, -0.0004606325f, -0.0004095121f, -0.0003501175f,
   -0.0002896981f, -0.0002098337f, -0.0001446380f, -0.0000617334f,
   0.0000134949f, 0.0001094383f, 0.0002043017f, 0.0002949531f, 0.0004026540f,
   0.0005107388f, 0.0006239376f, 0.0007458025f, 0.0008608443f, 0.0009885988f,
   0.0011250155f, 0.0012577884f, 0.0013902494f, 0.0015443219f, 0.0016868083f,
   0.0018348265f, 0.0019841140f, 0.0021461583f, 0.0023017254f, 0.0024625616f,
   0.0026201758f, 0.0027870464f, 0.0029469447f, 0.0031125420f, 0.0032739613f,
   0.0034418874f, 0.0036008268f, 0.0037603922f, 0.0039207432f, 0.0040819753f,
   0.0042264269f, 0.0043730719f, 0.0045209852f, 0.0046606460f, 0.0047932560f,
   0.0049137603f, 0.0050393022f, 0.0051407353f, 0.0052461166f, 0.0053471681f,
   0.0054196775f, 0.0054876040f, 0.0055475714f, 0.0055938023f, 0.0056220643f,
   0.0056455196f, 0.0056389199f, 0.0056266114f, 0.0055917128f, 0.0055404363f,
   0.0054753783f, 0.0053838975f, 0.0052715758f, 0.0051382275f, 0.0049839687f,
   0.0048109469f, 0.0046039530f, 0.0043801861f, 0.0041251642f, 0.0038456408f,
   0.0035401246f, 0.0032091885f, 0.0028446757f, 0.0024508540f, 0.0020274176f,
   0.0015784682f, 0.0010902329f, 0.0005832264f, 0.0000276045f,
   -0.0005464280f, -0.0011568135f, -0.0018039472f, -0.0024826723f,
   -0.0031933778f, -0.0039401124f, -0.0047222596f, -0.0055337211f,
   -0.0063792293f, -0.0072615816f, -0.0081798233f, -0.0091325329f,
   -0.0101150215f, -0.0111315548f, -0.0121849995f, 0.0132718220f,
   0.0143904666f, 0.0155405553f, 0.0167324712f, 0.0179433381f, 0.0191872431f,
   0.0204531793f, 0.0217467550f, 0.0230680169f, 0.0244160992f, 0.0257875847f,
   0.0271859429f, 0.0286072173f, 0.0300502657f, 0.0315017608f, 0.0329754081f,
   0.0344620948f, 0.0359697560f, 0.0374812850f, 0.0390053679f, 0.0405349170f,
   0.0420649094f, 0.0436097542f, 0.0451488405f, 0.0466843027f, 0.0482165720f,
   0.0497385755f, 0.0512556155f, 0.0527630746f, 0.0542452768f, 0.0557173648f,
   0.0571616450f, 0.0585915683f, 0.0599837480f, 0.0613455171f, 0.0626857808f,
   0.0639715898f, 0.0652247106f, 0.0664367512f, 0.0676075985f, 0.0687043828f,
   0.0697630244f, 0.0707628710f, 0.0717002673f, 0.0725682583f, 0.0733620255f,
   0.0741003642f, 0.0747452558f, 0.0753137336f, 0.0758008358f, 0.0761992479f,
   0.0764992170f, 0.0767093490f, 0.0768173975f, 0.0768230011f, 0.0767204924f,
   0.0765050718f, 0.0761748321f, 0.0757305756f, 0.0751576255f, 0.0744664394f,
   0.0736406005f, 0.0726774642f, 0.0715826364f, 0.0703533073f, 0.0689664013f,
   0.0674525021f, 0.0657690668f, 0.0639444805f, 0.0619602779f, 0.0598166570f,
   0.0575152691f, 0.0550460034f, 0.0524093821f, 0.0495978676f, 0.0466303305f,
   0.0434768782f, 0.0401458278f, 0.0366418116f, 0.0329583930f, 0.0290824006f,
   0.0250307561f, 0.0207997072f, 0.0163701258f, 0.0117623832f, 0.0069636862f,
   0.0019765601f, -0.0032086896f, -0.0085711749f, -0.0141288827f,
   -0.0198834129f, -0.0258227288f, -0.0319531274f, -0.0382776572f,
   -0.0447806821f, -0.0514804176f, -0.0583705326f, -0.0654409853f,
   -0.0726943300f, -0.0801372934f, -0.0877547536f, -0.0955533352f,
   -0.1035329531f, -0.1116826931f, -0.1200077984f, -0.1285002850f,
   -0.1371551761f, -0.1459766491f, -0.1549607071f, -0.1640958855f,
   -0.1733808172f, -0.1828172548f, -0.1923966745f, -0.2021250176f,
   -0.2119735853f, -0.2219652696f, -0.2320690870f, -0.2423016884f,
   -0.2526480309f, -0.2631053299f, -0.2736634040f, -0.2843214189f,
   -0.2950716717f, -0.3059098575f, -0.3168278913f, -0.3278113727f,
   -0.3388722693f, -0.3499914122f, 0.3611589903f, 0.3723795546f,
   0.3836350013f, 0.3949211761f, 0.4062317676f, 0.4175696896f, 0.4289119920f,
   0.4402553754f, 0.4515996535f, 0.4629308085f, 0.4742453214f, 0.4855253091f,
   0.4967708254f, 0.5079817500f, 0.5191234970f, 0.5302240895f, 0.5412553448f,
   0.5522051258f, 0.5630789140f, 0.5738524131f, 0.5845403235f, 0.5951123086f,
   0.6055783538f, 0.6159109932f, 0.6261242695f, 0.6361980107f, 0.6461269695f,
   0.6559016302f, 0.6655139880f, 0.6749663190f, 0.6842353293f, 0.6933282376f,
   0.7022388719f, 0.7109410426f, 0.7194462634f, 0.7277448900f, 0.7358211758f,
   0.7436827863f, 0.7513137456f, 0.7587080760f, 0.7658674865f, 0.7727780881f,
   0.7794287519f, 0.7858353120f, 0.7919735841f, 0.7978466413f, 0.8034485751f,
   0.8087695004f, 0.8138191270f, 0.8185776004f, 0.8230419890f, 0.8272275347f,
   0.8311038457f, 0.8346937361f, 0.8379717337f, 0.8409541392f, 0.8436238281f,
   0.8459818469f, 0.8480315777f, 0.8497805198f, 0.8511971524f, 0.8523047035f,
   0.8531020949f, 0.8535720573f, 0.8537385600f, 0.8535720573f, 0.8531020949f,
   0.8523047035f, 0.8511971524f, 0.8497805198f, 0.8480315777f, 0.8459818469f,
   0.8436238281f, 0.8409541392f, 0.8379717337f, 0.8346937361f, 0.8311038457f,
   0.8272275347f, 0.8230419890f, 0.8185776004f, 0.8138191270f, 0.8087695004f,
   0.8034485751f, 0.7978466413f, 0.7919735841f, 0.7858353120f, 0.7794287519f,
   0.7727780881f, 0.7658674865f, 0.7587080760f, 0.7513137456f, 0.7436827863f,
   0.7358211758f, 0.7277448900f, 0.7194462634f, 0.7109410426f, 0.7022388719f,
   0.6933282376f, 0.6842353293f, 0.6749663190f, 0.6655139880f, 0.6559016302f,
   0.6461269695f, 0.6361980107f, 0.6261242695f, 0.6159109932f, 0.6055783538f,
   0.5951123086f, 0.5845403235f, 0.5738524131f, 0.5630789140f, 0.5522051258f,
   0.5412553448f, 0.5302240895f, 0.5191234970f, 0.5079817500f, 0.4967708254f,
   0.4855253091f, 0.4742453214f, 0.4629308085f, 0.4515996535f, 0.4402553754f,
   0.4289119920f, 0.4175696896f, 0.4062317676f, 0.3949211761f, 0.3836350013f,
   0.3723795546f, -0.3611589903f, -0.3499914122f, -0.3388722693f,
   -0.3278113727f, -0.3168278913f, -0.3059098575f, -0.2950716717f,
   -0.2843214189f, -0.2736634040f, -0.2631053299f, -0.2526480309f,
   -0.2423016884f, -0.2320690870f, -0.2219652696f, -0.2119735853f,
   -0.2021250176f, -0.1923966745f, -0.1828172548f, -0.1733808172f,
   -0.1640958855f, -0.1549607071f, -0.1459766491f, -0.1371551761f,
   -0.1285002850f, -0.1200077984f, -0.1116826931f, -0.1035329531f,
   -0.0955533352f, -0.0877547536f, -0.0801372934f, -0.0726943300f,
   -0.0654409853f, -0.0583705326f, -0.0514804176f, -0.0447806821f,
   -0.0382776572f, -0.0319531274f, -0.0258227288f, -0.0198834129f,
   -0.0141288827f, -0.0085711749f, -0.0032086896f, 0.0019765601f,
   0.0069636862f, 0.0117623832f, 0.0163701258f, 0.0207997072f, 0.0250307561f,
   0.0290824006f, 0.0329583930f, 0.0366418116f, 0.0401458278f, 0.0434768782f,
   0.0466303305f, 0.0495978676f, 0.0524093821f, 0.0550460034f, 0.0575152691f,
   0.0598166570f, 0.0619602779f, 0.0639444805f, 0.0657690668f, 0.0674525021f,
   0.0689664013f, 0.0703533073f, 0.0715826364f, 0.0726774642f, 0.0736406005f,
   0.0744664394f, 0.0751576255f, 0.0757305756f, 0.0761748321f, 0.0765050718f,
   0.0767204924f, 0.0768230011f, 0.0768173975f, 0.0767093490f, 0.0764992170f,
   0.0761992479f, 0.0758008358f, 0.0753137336f, 0.0747452558f, 0.0741003642f,
   0.0733620255f, 0.0725682583f, 0.0717002673f, 0.0707628710f, 0.0697630244f,
   0.0687043828f, 0.0676075985f, 0.0664367512f, 0.0652247106f, 0.0639715898f,
   0.0626857808f, 0.0613455171f, 0.0599837480f, 0.0585915683f, 0.0571616450f,
   0.0557173648f, 0.0542452768f, 0.0527630746f, 0.0512556155f, 0.0497385755f,
   0.0482165720f, 0.0466843027f, 0.0451488405f, 0.0436097542f, 0.0420649094f,
   0.0405349170f, 0.0390053679f, 0.0374812850f, 0.0359697560f, 0.0344620948f,
   0.0329754081f, 0.0315017608f, 0.0300502657f, 0.0286072173f, 0.0271859429f,
   0.0257875847f, 0.0244160992f, 0.0230680169f, 0.0217467550f, 0.0204531793f,
   0.0191872431f, 0.0179433381f, 0.0167324712f, 0.0155405553f, 0.0143904666f,
   -0.0132718220f, -0.0121849995f, -0.0111315548f, -0.0101150215f,
   -0.0091325329f, -0.0081798233f, -0.0072615816f, -0.0063792293f,
   -0.0055337211f, -0.0047222596f, -0.0039401124f, -0.0031933778f,
   -0.0024826723f, -0.0018039472f, -0.0011568135f, -0.0005464280f,
   0.0000276045f, 0.0005832264f, 0.0010902329f, 0.0015784682f, 0.0020274176f,
   0.0024508540f, 0.0028446757f, 0.0032091885f, 0.0035401246f, 0.0038456408f,
   0.0041251642f, 0.0043801861f, 0.0046039530f, 0.0048109469f, 0.0049839687f,
   0.0051382275f, 0.0052715758f, 0.0053838975f, 0.0054753783f, 0.0055404363f,
   0.0055917128f, 0.0056266114f, 0.0056389199f, 0.0056455196f, 0.0056220643f,
   0.0055938023f, 0.0055475714f, 0.0054876040f, 0.0054196775f, 0.0053471681f,
   0.0052461166f, 0.0051407353f, 0.0050393022f, 0.0049137603f, 0.0047932560f,
   0.0046606460f, 0.0045209852f, 0.0043730719f, 0.0042264269f, 0.0040819753f,
   0.0039207432f, 0.0037603922f, 0.0036008268f, 0.0034418874f, 0.0032739613f,
   0.0031125420f, 0.0029469447f, 0.0027870464f, 0.0026201758f, 0.0024625616f,
   0.0023017254f, 0.0021461583f, 0.0019841140f, 0.0018348265f, 0.0016868083f,
   0.0015443219f, 0.0013902494f, 0.0012577884f, 0.0011250155f, 0.0009885988f,
   0.0008608443f, 0.0007458025f, 0.0006239376f, 0.0005107388f, 0.0004026540f,
   0.0002949531f, 0.0002043017f, 0.0001094383f, 0.0000134949f,
   -0.0000617334f, -0.0001446380f, -0.0002098337f, -0.0002896981f,
   -0.0003501175f, -0.0004095121f, -0.0004606325f, -0.0005145572f,
   -0.0005564576f, -0.0005946118f, -0.0006341594f, -0.0006650415f,
   -0.0006917937f, -0.0007215391f, -0.0007319357f, -0.0007530001f,
   -0.0007630793f, -0.0007757977f, -0.0007801449f, -0.0007803664f,
   -0.0007779869f, -0.0007834332f, -0.0007724848f, -0.0007681371f,
   -0.0007490598f, -0.0007440941f, -0.0007255043f, -0.0007157736f,
   -0.0006941614f, -0.0006777690f, -0.0006540333f, -0.0006312493f,
   -0.0006132747f, -0.0005870930f, -0.0005677802f, -0.0005466565f,
   -0.0005226564f, -0.0005040714f, -0.0004893791f, -0.0004875227f,
   -0.0004947518f, -0.0005617692f, -0.0005525286f
};

#define RAAC_MAX_SBR 4  /* concurrently tracked SBR-bearing elements */

/* per-channel SBR frame data: envelope grid, delta flags, inverse
 * filtering modes and the quantized envelope/noise scalefactors,
 * with row 0 carrying the previous frame's trailing values */
typedef struct
{
   unsigned bs_num_env;
   unsigned bs_num_noise;
   uint8_t  bs_freq_res[7];
   uint8_t  bs_frame_class;
   uint8_t  bs_amp_res;
   uint8_t  bs_df_env[5];
   uint8_t  bs_df_noise[2];
   uint8_t  bs_invf_mode[2][5];
   uint8_t  bs_add_harmonic_flag;
   uint8_t  bs_add_harmonic[48];
   int      t_env[8];
   int      t_env_num_env_old;
   int      t_q[3];
   int      e_a[2];
   int      env_facs_q[6][48];
   int      noise_facs_q[3][5];
   float    bw[5];            /* chirp factors, smoothed per frame */
   uint8_t  s_indexmapped[8][48];
   int      f_indexnoise;
   int      f_indexsine;
} raac_sbr_ch;

/* per-output-channel QMF bank state, allocated only when the doubled
 * output rate is active: analysis history over the core signal, the
 * synthesis overlap ring, and this channel's upsampled frame */
#define RAAC_QMF_RING 32    /* slots between history moves */
typedef struct
{
   /* the analysis window and the synthesis overlap slide through
    * longer buffers and move back once every RAAC_QMF_RING slots */
   float x[320 + 32 * (RAAC_QMF_RING - 1)];
   float v[1280 + 128 * (RAAC_QMF_RING - 1)];
   int   xring, vring;        /* slots since the last move           */
   float W[2][32][32][2];     /* analysis output, previous and
                               * current frame (slot, band, re/im) */
   float Y[2][38][64][2];     /* adjusted high band, previous and
                               * current frame                      */
   float g_temp[42][48];      /* gain and noise-level histories for
                               * the four-slot smoothing filter     */
   float q_temp[42][48];
   float out[2 * RAAC_FRAME];
} raac_qmf_ch;

/* frame-local SBR scratch shared across channels (one channel is
 * processed at a time), allocated with the QMF state */
typedef struct
{
   float X_low[32][40][2];    /* 8 history + 32 current slots */
   float X_high[64][40][2];
   float alpha0[32][2];
   float alpha1[32][2];
   float envf[2][6][48];      /* dequantised envelope and noise     */
   float noisef[2][3][5];     /*   scalefactors, both element chs   */
   float e_origmapped[7][48];
   float q_mapped[7][48];
   uint8_t s_mapped[7][48];
   float e_curr[7][48];
   float X[2][38][64];        /* the assembled synthesis matrix     */
} raac_sbr_scratch;

/* one SBR decoder bound to a channel element (type, tag). Stage one
 * of the SBR effort: the complete bitstream layer and frequency band
 * tables; synthesis lands with the QMF/HF stages. */
typedef struct
{
   int      in_use;
   int      is_cpe;
   int      tag;
   int      start;             /* a header has arrived               */
   int      reset;
   unsigned sample_rate;       /* the SBR rate: twice the core       */
   /* sbr_header */
   uint8_t  bs_amp_res_hdr;
   uint8_t  bs_start_freq, bs_stop_freq, bs_xover_band;
   uint8_t  bs_freq_scale, bs_alter_scale, bs_noise_bands;
   uint8_t  bs_limiter_bands, bs_limiter_gains;
   uint8_t  bs_interpol_freq, bs_smoothing_mode;
   uint8_t  bs_coupling;
   /* frequency band tables (14496-3 4.6.18.3) */
   int      k0, k1, k2;
   int      kx[2], m[2];       /* crossover and band count, [0] holds
                                * the previous frame's values        */
   int      n_master;
   int      n[2];              /* N_low, N_high                      */
   int      n_q, n_lim;
   uint16_t f_master[49];
   uint16_t f_tablehigh[49];
   uint16_t f_tablelow[25];
   uint16_t f_tablenoise[6];
   uint16_t f_tablelim[30];
   int      num_patches;
   uint8_t  patch_start_subband[6];
   uint8_t  patch_num_subbands[6];
   /* gain-calculation outputs persist per element, shared by both
    * channels, deliberately: limiter tables do not always reach
    * kx + M (the warp thinning can consume the final border), and
    * the uncovered top bands then reuse whatever these arrays last
    * held. The reference decoder keeps these in its per-element
    * context, so matching its output on such geometries requires
    * matching its staleness pattern exactly. */
   float    q_m[7][48];
   float    s_m[7][48];
   float    gain[7][48];
   raac_sbr_ch d[2];
} raac_sbr;

struct raac
{
   unsigned sample_rate;
   unsigned channels;
   int      sfi;                  /* sampling frequency index            */
   size_t   used;                 /* bytes the last unit took            */
   raac_ch  ch[RAAC_MAX_CH];
   raac_cce cce[RAAC_MAX_CCE];
   raac_sbr sbr[RAAC_MAX_SBR];
   raac_huff     spec[11];
   raac_huff     sfh;
   uint32_t      huff_lut[11 * RAAC_HUFF_SPEC_CAP + RAAC_HUFF_SF_CAP];
   float         iq43[8192];                   /* |q|^(4/3)              */
   float         iq43s[33];                    /* the same, signed, for
                                                * q in -16..16          */
   float         sf_gain[256];                 /* 2^((sf - 100) / 4)     */
   /* windows */
   float    kbd_long[2048];
   float    kbd_short[256];
   float    sine_long[2048];
   float    sine_short[256];
   /* FFT twiddles for the two IMDCT sizes (N/4-point complex FFT) */
   float    tw512_re[1024], tw512_im[1024];    /* pre+post twiddles 2048 */
   float    tw64_re[128],  tw64_im[128];       /* pre+post twiddles 256  */
   float    fft_re[512], fft_im[512];          /* scratch                */
   float    fft2_re[512], fft2_im[512];        /* FFT output             */
   uint16_t fft_rev[512];                      /* 9-bit reversal         */
   float    fft_twr[512], fft_twi[512];        /* per-pass twiddle runs  */
   unsigned frame_len;                          /* 1024, or 960 when the
                                                * short frame length is
                                                * signalled at open     */
   float    mr_re[1280], mr_im[1280];          /* mixed-radix FFT output
                                                * and recursion scratch  */
   float    w480_re[480], w480_im[480];        /* roots of unity for the
                                                * 960-mode transforms    */
   float    w60_re[60], w60_im[60];
   float    tw32_re[64], tw32_im[64];        /* SBR QMF transforms   */
   int      sbr_mode;         /* doubled output rate active (fixed at
                               * open: explicit SBR signaling, or the
                               * implicit 16-24 kHz window) */
   unsigned out_rate;         /* rate reported to the caller */
   raac_qmf_ch *qmf;          /* one per output channel when sbr_mode */
   raac_sbr_scratch *sbx;     /* shared HF scratch, same lifetime    */
   int8_t   sbr_map[RAAC_MAX_CH];    /* this frame: SBR slot per
                                      * output channel, -1 if none  */
   int8_t   sbr_map_ch[RAAC_MAX_CH]; /* 0/1: which element channel  */
   float    pcm[RAAC_MAX_CH][RAAC_FRAME];      /* per-frame synthesis
                                                * scratch: too large for
                                                * the stack at 8 ch     */
   /* transform scratch, for the same reason: one channel is processed
    * at a time and none of these outlive their call, but as locals
    * they put multiple KiB on the stack, and the threads that run a
    * decoder on PSP and GX get 8 KiB in total                        */
   float    imdct_v[RAAC_FRAME];               /* IMDCT quarter scatter */
   float    fb_win[2 * RAAC_FRAME];            /* filterbank: transform
                                                * output, then windowed
                                                * in place              */
   float    fb_short[256];                     /* one short transform   */
   float    qmf_z[320];                        /* QMF analysis fold     */
   float    qmf_z64[64], qmf_m[64];            /* analysis transform    */
   float    qmf_xim[64];                       /* synthesis transform   */
   float    qmf_m0[64], qmf_m1[64];
   int      quant[RAAC_FRAME];                 /* quantised spectrum    */
   uint32_t noise_state;                        /* PNS LCG               */
};

/* ===== windows and twiddles ===== */

static double raac_bessel_i0(double x)
{
   double sum = 1.0, term = 1.0;
   int    k;
   for (k = 1; k < 64; k++)
   {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum  += term;
      if (term < 1e-21 * sum)
         break;
   }
   return sum;
}

/* Kaiser-Bessel derived window (14496-3 4.6.11.3.2): alpha 4 for the
 * long window, 6 for the short. */
static void raac_kbd_window(float *w, int n, double alpha)
{
   double *kern = (double*)malloc(sizeof(double) * (size_t)(n / 2 + 1));
   double  sum = 0.0, acc = 0.0;
   int     i;
   if (!kern)
   {
      for (i = 0; i < n; i++)
         w[i] = 1.0f;   /* degraded but functional on OOM */
      return;
   }
   for (i = 0; i <= n / 2; i++)
   {
      /* the kernel is a Kaiser window over the half length: its own
       * argument spans -1..1 across n/2 points, so 4i/n - 1 */
      double t = 4.0 * i / n - 1.0;
      kern[i]  = raac_bessel_i0(M_PI * alpha * sqrt(1.0 - t * t));
      sum     += kern[i];
   }
   for (i = 0; i < n / 2; i++)
   {
      acc += kern[i];
      w[i] = (float)sqrt(acc / (sum));
   }
   free(kern);
   for (i = n / 2; i < n; i++)
      w[i] = w[n - 1 - i];
}

static void raac_sine_window(float *w, int n)
{
   int i;
   for (i = 0; i < n; i++)
      w[i] = (float)sin(M_PI / n * (i + 0.5));
}

/* ===== IMDCT via N/4-point complex FFT =====
 *
 * Standard formulation: pre-twiddle the N/2 spectral pairs, run an
 * N/4 complex FFT, post-twiddle, and scatter the quarters with the
 * MDCT symmetries. Output is the full N time samples of this frame's
 * windowed contribution before overlap-add (scale 2/N folded in). */

/* Forward complex FFT, split real and imaginary arrays, n a power of
 * two from 8 to RAAC_FFT_MAX, out of place. The first pass reads the
 * input in bit-reversed order and does the first two radix-2 passes
 * together (their twiddles are 1 and -i); the rest go two at a time as
 * radix-2^2 butterflies, four of them at once, and a last single pass
 * when the count is odd. The radix-2 pass of length len has its
 * twiddles at a->fft_tw*[len / 2 - 4 ..]. */
#define RAAC_FFT_MAX   512
#define RAAC_FFT_BITS  9
static void raac_fft(const raac_t *a, const float *in_re, const float *in_im,
      float *re, float *im, int n)
{
   int i, len, shift = 0;
   while ((RAAC_FFT_MAX >> shift) > n)
      shift++;
   for (i = 0; i < n; i += 4)
   {
      int   j0 = a->fft_rev[i] >> shift,     j1 = a->fft_rev[i + 1] >> shift;
      int   j2 = a->fft_rev[i + 2] >> shift, j3 = a->fft_rev[i + 3] >> shift;
      float b0r = in_re[j0] + in_re[j1], b0i = in_im[j0] + in_im[j1];
      float b1r = in_re[j0] - in_re[j1], b1i = in_im[j0] - in_im[j1];
      float b2r = in_re[j2] + in_re[j3], b2i = in_im[j2] + in_im[j3];
      float b3r = in_re[j2] - in_re[j3], b3i = in_im[j2] - in_im[j3];
      re[i]     = b0r + b2r; im[i]     = b0i + b2i;
      re[i + 2] = b0r - b2r; im[i + 2] = b0i - b2i;
      re[i + 1] = b1r + b3i; im[i + 1] = b1i - b3r;
      re[i + 3] = b1r - b3i; im[i + 3] = b1i + b3r;
   }
   for (len = 8; len <= n; len <<= 2)
   {
      if (len * 2 <= n)
      {
         /* passes len and 2 * len: with q = len / 2, points k, k + q,
          * k + 2q, k + 3q of each run of 4q */
         int          q   = len >> 1, s, k;
         const float *w1r = a->fft_twr + q - 4, *w1i = a->fft_twi + q - 4;
         const float *w2r = a->fft_twr + len - 4, *w2i = a->fft_twi + len - 4;
         for (s = 0; s < n; s += 4 * q)
         {
            float *r0 = re + s, *i0 = im + s;
            for (k = 0; k < q; k += 4)
            {
               raac_v ar = RAAC_LD(r0 + k),         ai = RAAC_LD(i0 + k);
               raac_v br = RAAC_LD(r0 + k + q),     bi = RAAC_LD(i0 + k + q);
               raac_v cr = RAAC_LD(r0 + k + 2 * q), ci = RAAC_LD(i0 + k + 2 * q);
               raac_v dr = RAAC_LD(r0 + k + 3 * q), di = RAAC_LD(i0 + k + 3 * q);
               raac_v ur = RAAC_LD(w1r + k), ui = RAAC_LD(w1i + k);
               raac_v vr = RAAC_LD(w2r + k), vi = RAAC_LD(w2i + k);
               raac_v xr, xi, b0r, b0i, b1r, b1i, b2r, b2i, b3r, b3i;
               /* first pass: (a, b) and (c, d) by u */
               xr  = RAAC_SUB(RAAC_MUL(br, ur), RAAC_MUL(bi, ui));
               xi  = RAAC_ADD(RAAC_MUL(br, ui), RAAC_MUL(bi, ur));
               b0r = RAAC_ADD(ar, xr); b0i = RAAC_ADD(ai, xi);
               b1r = RAAC_SUB(ar, xr); b1i = RAAC_SUB(ai, xi);
               xr  = RAAC_SUB(RAAC_MUL(dr, ur), RAAC_MUL(di, ui));
               xi  = RAAC_ADD(RAAC_MUL(dr, ui), RAAC_MUL(di, ur));
               b2r = RAAC_ADD(cr, xr); b2i = RAAC_ADD(ci, xi);
               b3r = RAAC_SUB(cr, xr); b3i = RAAC_SUB(ci, xi);
               /* second pass: (b0, b2) by v, (b1, b3) by v times -i */
               xr  = RAAC_SUB(RAAC_MUL(b2r, vr), RAAC_MUL(b2i, vi));
               xi  = RAAC_ADD(RAAC_MUL(b2r, vi), RAAC_MUL(b2i, vr));
               RAAC_ST(r0 + k,         RAAC_ADD(b0r, xr));
               RAAC_ST(i0 + k,         RAAC_ADD(b0i, xi));
               RAAC_ST(r0 + k + 2 * q, RAAC_SUB(b0r, xr));
               RAAC_ST(i0 + k + 2 * q, RAAC_SUB(b0i, xi));
               xr  = RAAC_SUB(RAAC_MUL(b3r, vr), RAAC_MUL(b3i, vi));
               xi  = RAAC_ADD(RAAC_MUL(b3r, vi), RAAC_MUL(b3i, vr));
               /* (xr + i xi) * -i = xi - i xr */
               RAAC_ST(r0 + k + q,     RAAC_ADD(b1r, xi));
               RAAC_ST(i0 + k + q,     RAAC_SUB(b1i, xr));
               RAAC_ST(r0 + k + 3 * q, RAAC_SUB(b1r, xi));
               RAAC_ST(i0 + k + 3 * q, RAAC_ADD(b1i, xr));
            }
         }
      }
      else
      {
         int          half = len >> 1, s, k;
         const float *wr   = a->fft_twr + half - 4;
         const float *wi   = a->fft_twi + half - 4;
         for (s = 0; s < n; s += len)
         {
            float *ar = re + s, *ai = im + s;
            float *cr = re + s + half, *ci = im + s + half;
            for (k = 0; k < half; k += 4)
            {
               raac_v vwr = RAAC_LD(wr + k), vwi = RAAC_LD(wi + k);
               raac_v vcr = RAAC_LD(cr + k), vci = RAAC_LD(ci + k);
               raac_v var = RAAC_LD(ar + k), vai = RAAC_LD(ai + k);
               raac_v xr  = RAAC_SUB(RAAC_MUL(vcr, vwr), RAAC_MUL(vci, vwi));
               raac_v xi  = RAAC_ADD(RAAC_MUL(vcr, vwi), RAAC_MUL(vci, vwr));
               RAAC_ST(cr + k, RAAC_SUB(var, xr));
               RAAC_ST(ci + k, RAAC_SUB(vai, xi));
               RAAC_ST(ar + k, RAAC_ADD(var, xr));
               RAAC_ST(ai + k, RAAC_ADD(vai, xi));
            }
         }
      }
   }
}

static void raac_fft_tables(raac_t *a)
{
   int i, len;
   for (i = 0; i < RAAC_FFT_MAX; i++)
   {
      int r = 0, k;
      for (k = 0; k < RAAC_FFT_BITS; k++)
         if (i & (1 << k))
            r |= 1 << (RAAC_FFT_BITS - 1 - k);
      a->fft_rev[i] = (uint16_t)r;
   }
   for (len = 8; len <= RAAC_FFT_MAX; len <<= 1)
   {
      int half = len >> 1, k;
      for (k = 0; k < half; k++)
      {
         double ang = -2.0 * M_PI * k / len;
         a->fft_twr[half - 4 + k] = (float)cos(ang);
         a->fft_twi[half - 4 + k] = (float)sin(ang);
      }
   }
}

/* Mixed-radix complex FFT for the 960-frame transforms, whose cores
 * are 480 = 2^5*3*5 and 60 = 2^2*3*5 points. Recursive decimation in
 * time over radices 5/3/2, out-of-place per level into caller scratch,
 * with a shared full-size root-of-unity table (every sub-length
 * divides the root length, so twiddle indices stay integral). The
 * radix-2 transform above keeps the 1024-frame path unchanged. */
static void raac_fft_mr(const float *in_re, const float *in_im,
      float *out_re, float *out_im, int n, int stride,
      const float *w_re, const float *w_im, int root,
      float *sc_re, float *sc_im)
{
   int r, m, q, j, k;
   if (n == 1)
   {
      out_re[0] = in_re[0];
      out_im[0] = in_im[0];
      return;
   }
   r = (n % 5 == 0) ? 5 : ((n % 3 == 0) ? 3 : 2);
   m = n / r;
   for (q = 0; q < r; q++)
      raac_fft_mr(in_re + q * stride, in_im + q * stride,
            sc_re + q * m, sc_im + q * m, m, stride * r,
            w_re, w_im, root, sc_re + n, sc_im + n);
   for (j = 0; j < r; j++)
      for (k = 0; k < m; k++)
      {
         int   idx  = j * m + k;
         int   step = root / n;
         float ar = 0.0f, ai = 0.0f;
         for (q = 0; q < r; q++)
         {
            int   tw = (idx * q * step) % root;
            float wr = w_re[tw], wi = w_im[tw];
            float xr = sc_re[q * m + k], xi = sc_im[q * m + k];
            ar += xr * wr - xi * wi;
            ai += xr * wi + xi * wr;
         }
         out_re[idx] = ar;
         out_im[idx] = ai;
      }
}

/* in: N/2 spectral coefficients X, out: N time samples (2/N folded in).
 * Derivation: y is a signed/mirrored rearrangement of the length-N/2
 * DCT-IV of X, and the DCT-IV runs on an N/4-point complex FFT with
 * exp(-j*pi*(4k+1)/(4M)) pre- and exp(-j*pi*k/M) post-twiddles; both
 * identities were verified numerically to machine precision against
 * the defining cosine sum before this implementation. */
static void raac_imdct(raac_t *a, const float *x, float *out, int n)
{
   int    n4 = n / 4, n2 = n / 2;
   float *fre = a->fft_re, *fim = a->fft_im;
   /* the twiddle arrays hold this instance's frame-length variant:
    * 512/64-point tables in 1024 mode, 480/60-point in 960 mode */
   int    lng = (n >= 1920);
   const float *pr  = lng ? a->tw512_re : a->tw64_re;
   const float *pi_ = lng ? a->tw512_im : a->tw64_im;
   const float *qr  = lng ? a->tw512_re + n4 : a->tw64_re + n4;
   const float *qi  = lng ? a->tw512_im + n4 : a->tw64_im + n4;
   float *v   = a->imdct_v;
   int    k;

   if (!(n4 & 3))
      for (k = 0; k < n4; k += 4)
      {
         /* x[2k] for four k, and x[n2 - 1 - 2k]: the odd ones of the
          * eight below, turned around */
         const float *lo = x + 2 * k, *hi = x + n2 - 8 - 2 * k;
         raac_v xr = RAAC_EVEN(RAAC_LD(lo), RAAC_LD(lo + 4));
         raac_v xi = RAAC_REV(RAAC_ODD(RAAC_LD(hi), RAAC_LD(hi + 4)));
         raac_v wr = RAAC_LD(pr + k), wi = RAAC_LD(pi_ + k);
         RAAC_ST(fre + k, RAAC_SUB(RAAC_MUL(xr, wr), RAAC_MUL(xi, wi)));
         RAAC_ST(fim + k, RAAC_ADD(RAAC_MUL(xr, wi), RAAC_MUL(xi, wr)));
      }
   else
      for (k = 0; k < n4; k++)
      {
         float xr = x[2 * k];
         float xi = x[n2 - 1 - 2 * k];
         fre[k] = xr * pr[k] - xi * pi_[k];
         fim[k] = xr * pi_[k] + xi * pr[k];
      }
   if ((n4 & (n4 - 1)) == 0)
   {
      raac_fft(a, fre, fim, a->fft2_re, a->fft2_im, n4);
      fre = a->fft2_re;
      fim = a->fft2_im;
   }
   else
   {
      const float *wr = (n4 == 480) ? a->w480_re : a->w60_re;
      const float *wi = (n4 == 480) ? a->w480_im : a->w60_im;
      raac_fft_mr(fre, fim, a->mr_re, a->mr_im, n4, 1, wr, wi, n4,
            a->mr_re + n4, a->mr_im + n4);
      memcpy(fre, a->mr_re, sizeof(float) * (size_t)n4);
      memcpy(fim, a->mr_im, sizeof(float) * (size_t)n4);
   }
   if (!(n4 & 3))
   {
      /* post-twiddle in place, then v[2k] = yr[k] and
       * v[2k + 1] = -yi[n4 - 1 - k] */
      for (k = 0; k < n4; k += 4)
      {
         raac_v fr = RAAC_LD(fre + k), fi = RAAC_LD(fim + k);
         raac_v wr = RAAC_LD(qr + k),  wi = RAAC_LD(qi + k);
         RAAC_ST(fre + k, RAAC_SUB(RAAC_MUL(fr, wr), RAAC_MUL(fi, wi)));
         RAAC_ST(fim + k, RAAC_ADD(RAAC_MUL(fr, wi), RAAC_MUL(fi, wr)));
      }
      for (k = 0; k < n4; k += 4)
      {
         raac_v yr = RAAC_LD(fre + k);
         raac_v yi = RAAC_REV(RAAC_LD(fim + n4 - 4 - k));
         yi = RAAC_SUB(RAAC_SET1(0.0f), yi);
         RAAC_ST(v + 2 * k,     RAAC_ZIPLO(yr, yi));
         RAAC_ST(v + 2 * k + 4, RAAC_ZIPHI(yr, yi));
      }
   }
   else
      for (k = 0; k < n4; k++)
      {
         float yr = fre[k] * qr[k] - fim[k] * qi[k];
         float yi = fre[k] * qi[k] + fim[k] * qr[k];
         v[2 * k]           =  yr;
         v[n2 - 1 - 2 * k]  = -yi;
      }
   if (!(n4 & 3))
   {
      /* the same scatter four at a time; reversed runs load the four
       * below and turn them around */
      raac_v s  = RAAC_SET1(2.0f / n);
      raac_v ns = RAAC_SET1(-2.0f / n);
      for (k = 0; k < n4; k += 4)
      {
         RAAC_ST(out + k,           RAAC_MUL(RAAC_LD(v + n4 + k), s));
         RAAC_ST(out + n4 + k,      RAAC_MUL(RAAC_REV(RAAC_LD(v + n2 - 4 - k)), ns));
         RAAC_ST(out + n2 + k,      RAAC_MUL(RAAC_REV(RAAC_LD(v + n4 - 4 - k)), ns));
         RAAC_ST(out + n2 + n4 + k, RAAC_MUL(RAAC_LD(v + k), ns));
      }
      return;
   }
   for (k = 0; k < n4; k++)
   {
      float s = 2.0f / n;
      out[k]           =  v[n4 + k] * s;
      out[n4 + k]      = -v[n2 - 1 - k] * s;
      out[n2 + k]      = -v[n4 - 1 - k] * s;
      out[n2 + n4 + k] = -v[k] * s;
   }
}

/* twr/twi hold the pre-twiddles in [0..n4) and the post-twiddles in
 * [n4..n2): pre exp(-j*pi*(4k+1)/(4M)), post exp(-j*pi*k/M), M = n/2. */
static void raac_make_twiddles(float *twr, float *twi, int n4, int n)
{
   int    m = n / 2;
   int    k;
   for (k = 0; k < n4; k++)
   {
      double pre  = -M_PI * (4.0 * k + 1.0) / (4.0 * m);
      double post = -M_PI * k / m;
      twr[k]      = (float)cos(pre);
      twi[k]      = (float)sin(pre);
      twr[n4 + k] = (float)cos(post);
      twi[n4 + k] = (float)sin(post);
   }
}

/* ===== syntax: ics_info, sections, scale factors, pulse, tns ===== */

static const uint16_t *raac_swb_off(const raac_t *a, int short_win)
{
   if (a->frame_len == 960)
      return short_win ? raac_swb_offset_120[a->sfi]
                       : raac_swb_offset_960[a->sfi];
   return short_win ? raac_swb_offset_128[a->sfi]
                    : raac_swb_offset_1024[a->sfi];
}

static int raac_num_swb(const raac_t *a, int short_win)
{
   if (a->frame_len == 960)
      return short_win ? raac_num_swb_120[a->sfi]
                       : raac_num_swb_960[a->sfi];
   return short_win ? raac_num_swb_128[a->sfi]
                    : raac_num_swb_1024[a->sfi];
}

static int raac_ics_info(raac_t *a, raac_bits *b, raac_ch *c)
{
   int short_win;
   raac_getbits(b, 1); /* ics_reserved              */
   c->window_sequence = (int)raac_getbits(b, 2);
   c->window_shape    = (int)raac_getbits(b, 1);
   short_win = (c->window_sequence == 2);
   if (short_win)
   {
      uint32_t grouping;
      int g, w;
      c->max_sfb  = (int)raac_getbits(b, 4);
      grouping    = raac_getbits(b, 7);
      c->num_windows       = 8;
      c->num_window_groups = 1;
      c->group_len[0]      = 1;
      for (w = 0; w < 7; w++)
      {
         if (grouping & (0x40u >> w))
            c->group_len[c->num_window_groups - 1]++;
         else
         {
            c->num_window_groups++;
            c->group_len[c->num_window_groups - 1] = 1;
         }
      }
      (void)g;
   }
   else
   {
      c->max_sfb = (int)raac_getbits(b, 6);
      if (raac_getbits(b, 1)) /* predictor_data_present    */
         return -1; /* Main/LTP tools: out of scope */
      c->num_windows       = 1;
      c->num_window_groups = 1;
      c->group_len[0]      = 1;
   }
   if (c->max_sfb > raac_num_swb(a, short_win))
      return -1;
   return b->err ? -1 : 0;
}

static int raac_section_data(raac_t *a, raac_bits *b, raac_ch *c)
{
   int sect_bits = (c->window_sequence == 2) ? 3 : 5;
   int esc       = (1 << sect_bits) - 1;
   int g;
   for (g = 0; g < c->num_window_groups; g++)
   {
      int k = 0;
      while (k < c->max_sfb)
      {
         int cb = (int)raac_getbits(b, 4);
         int len = 0, l;
         if (cb == 12)             /* reserved codebook               */
            return -1;
         do
         {
            l    = (int)raac_getbits(b, sect_bits);
            len += l;
         } while (l == esc);
         if (k + len > c->max_sfb || b->err)
            return -1;
         for (l = 0; l < len; l++)
            c->band_cb[g][k + l] = (uint8_t)cb;
         k += len;
      }
   }
   return 0;
}

static int raac_scale_factor_data(raac_t *a, raac_bits *b, raac_ch *c,
      int global_gain)
{
   int sf = global_gain;
   int is_pos = 0;
   int noise = global_gain - 90;
   int noise_first = 1;
   int g, k;
   for (g = 0; g < c->num_window_groups; g++)
      for (k = 0; k < c->max_sfb; k++)
      {
         int cb = c->band_cb[g][k];
         if (cb == RAAC_CB_ZERO)
         {
            c->sf[g][k] = 0;
            continue;
         }
         if (cb == RAAC_CB_INTENSITY || cb == RAAC_CB_INTENSITY2)
         {
            int d = raac_huff_decode_sf(b, &a->sfh);
            if (d < 0)
               return -1;
            is_pos     += d - 60;
            c->sf[g][k] = is_pos;
            continue;
         }
         if (cb == RAAC_CB_NOISE)
         {
            if (noise_first)
            {
               noise      += (int)raac_getbits(b, 9) - 256;
               noise_first = 0;
            }
            else
            {
               int d = raac_huff_decode_sf(b, &a->sfh);
               if (d < 0)
                  return -1;
               noise += d - 60;
            }
            if (noise < -100) noise = -100;
            if (noise >  155) noise =  155;
            c->sf[g][k] = noise;
            continue;
         }
         {
            int d = raac_huff_decode_sf(b, &a->sfh);
            if (d < 0)
               return -1;
            sf += d - 60;
            if (sf < 0 || sf > 255)
               return -1;
            c->sf[g][k] = sf;
         }
      }
   return b->err ? -1 : 0;
}

typedef struct
{
   int pulse_present;
   int number;
   int start_sfb;
   int offset[4];
   int amp[4];
} raac_pulse;

static int raac_pulse_data(raac_bits *b, raac_pulse *p)
{
   int i;
   p->number    = (int)raac_getbits(b, 2) + 1;
   p->start_sfb = (int)raac_getbits(b, 6);
   for (i = 0; i < p->number; i++)
   {
      p->offset[i] = (int)raac_getbits(b, 5);
      p->amp[i]    = (int)raac_getbits(b, 4);
   }
   return b->err ? -1 : 0;
}

static int raac_tns_data(raac_t *a, raac_bits *b, raac_ch *c)
{
   int short_win = (c->window_sequence == 2);
   int w;
   for (w = 0; w < c->num_windows; w++)
   {
      int nf = (int)raac_getbits(b, short_win ? 1 : 2);
      int coef_res = 0, f;
      c->tns_n_filt[w] = nf;
      if (nf)
         coef_res = (int)raac_getbits(b, 1);
      for (f = 0; f < nf; f++)
      {
         int length = (int)raac_getbits(b, short_win ? 4 : 6);
         int order  = (int)raac_getbits(b, short_win ? 3 : 5);
         int i;
         c->tns_length[w][f] = length;
         c->tns_order[w][f]  = order;
         if (order > 20)
            return -1;
         if (order)
         {
            int direction = (int)raac_getbits(b, 1);
            int compress  = (int)raac_getbits(b, 1);
            int coef_bits = coef_res + 3 - compress;
            /* inverse quantisation per 14496-3 tns_decode_coef:
             * negative codes count down from the top of the range */
            double iqfac  = ((1 << (coef_res + 2)) - 0.5) / (M_PI / 2.0);
            double iqfac_m = ((1 << (coef_res + 2)) + 0.5) / (M_PI / 2.0);
            c->tns_direction[w][f] = direction;
            for (i = 0; i < order; i++)
            {
               int v = (int)raac_getbits(b, coef_bits);
               int s = v & (1 << (coef_bits - 1)) ? v - (1 << coef_bits) : v;
               c->tns_coef[w][f][i] = (float)sin(s /
                     (s >= 0 ? iqfac : iqfac_m));
            }
         }
      }
   }
   return b->err ? -1 : 0;
}

/* ===== spectral data ===== */

#define RAAC_IQ_TAB 8192   /* |q| below this from the table: 8191 is
                            * the largest an escape codes */


/* 2^(0.25 * (sf - 100)), sf as the bitstream holds it (0..255) */
static float raac_sf_gain(const raac_t *a, int sf)
{
   if ((unsigned)sf < 256u)
      return a->sf_gain[sf];
   return (float)pow(2.0, 0.25 * (sf - 100));
}

/* |q|^(4/3) * gain with q's sign */
static INLINE float raac_dq(const raac_t *a, int q, float gain)
{
   int   m = q < 0 ? -q : q;
   float v = m < RAAC_IQ_TAB ? a->iq43[m] : (float)pow((double)m, 4.0 / 3.0);
   return (q < 0 ? -v : v) * gain;
}

/* Decodes the quantised spectrum into quant[], or, given coef, straight
 * into the scaled spectrum there (what raac_dequant would make of it)
 * when nothing needs the quantised values first */
static int raac_spectral_data(raac_t *a, raac_bits *b, raac_ch *c,
      int quant[RAAC_FRAME], float *coef)
{
   const uint16_t *swb = raac_swb_off(a, c->window_sequence == 2);
   int g, win_base = 0;
   if (coef)
      memset(coef, 0, sizeof(float) * RAAC_FRAME);
   else
      memset(quant, 0, sizeof(int) * RAAC_FRAME);
   for (g = 0; g < c->num_window_groups; g++)
   {
      int glen = c->group_len[g];
      int k;
      for (k = 0; k < c->max_sfb; k++)
      {
         int cb = c->band_cb[g][k];
         int w;
         if (cb == RAAC_CB_ZERO || cb >= RAAC_CB_NOISE)
            continue;
         for (w = 0; w < glen; w++)
         {
            int lo = swb[k], hi = swb[k + 1];
            int base = win_base + w * 128;
            int i;
            const raac_huff *h  = &a->spec[cb - 1];
            int             *qo = quant + base;
            float           *co = coef ? coef + base : NULL;
            float            gn = coef ? raac_sf_gain(a, c->sf[g][k]) : 0.0f;
            /* |q|^(4/3) with q's sign for the values a codeword itself
             * carries, -16..16 */
            const float     *sq = a->iq43s + 16;
            /* the bits ahead in a register: bp is the position of cc's
             * top bit, avail how many of cc's bits are the stream's */
            size_t           bp    = b->pos;
            uint64_t         cc    = raac_peek64(b, bp);
            unsigned         avail = 57;
            if (cb <= 4)
            {
               int sgn = cb > 2;
               for (i = lo; i < hi; i += 4)
               {
                  uint32_t e, len;
                  int      p, q0, q1, q2, q3;
                  if (avail < 24)
                  {
                     cc    = raac_peek64(b, bp);
                     avail = 57;
                  }
                  RAAC_HUFF_LOOKUP(e, h, cc);
                  len = e & 0xFF;
                  if (!len)
                  {
                     b->err = 1;
                     return -1;
                  }
                  cc   <<= len;
                  avail -= len;
                  bp    += len;
                  p  = (int)(e >> 8);
                  q0 = RAAC_SEXT4(p >> 12);
                  q1 = RAAC_SEXT4(p >> 8);
                  q2 = RAAC_SEXT4(p >> 4);
                  q3 = RAAC_SEXT4(p);
                  if (sgn)
                  {
                     /* one sign bit for each nonzero value, in order,
                      * applied without branches */
                     unsigned nz = (unsigned)p >> 16;
                     uint32_t sb = (uint32_t)(cc >> 32), t, x;
                     t = q0 != 0; x = (sb >> 31) & t; q0 = (q0 ^ -(int)x) + (int)x; sb <<= t;
                     t = q1 != 0; x = (sb >> 31) & t; q1 = (q1 ^ -(int)x) + (int)x; sb <<= t;
                     t = q2 != 0; x = (sb >> 31) & t; q2 = (q2 ^ -(int)x) + (int)x; sb <<= t;
                     t = q3 != 0; x = (sb >> 31) & t; q3 = (q3 ^ -(int)x) + (int)x;
                     cc   <<= nz;
                     avail -= nz;
                     bp    += nz;
                  }
                  if (co)
                  {
                     co[i]     = sq[q0] * gn;
                     co[i + 1] = sq[q1] * gn;
                     co[i + 2] = sq[q2] * gn;
                     co[i + 3] = sq[q3] * gn;
                  }
                  else
                  {
                     qo[i]     = q0;
                     qo[i + 1] = q1;
                     qo[i + 2] = q2;
                     qo[i + 3] = q3;
                  }
               }
            }
            else
            {
               int sgn = cb > 6;
               for (i = lo; i < hi; i += 2)
               {
                  uint32_t e, len;
                  int      p, q0, q1;
                  if (avail < 24)
                  {
                     cc    = raac_peek64(b, bp);
                     avail = 57;
                  }
                  RAAC_HUFF_LOOKUP(e, h, cc);
                  len = e & 0xFF;
                  if (!len)
                  {
                     b->err = 1;
                     return -1;
                  }
                  cc   <<= len;
                  avail -= len;
                  bp    += len;
                  p  = (int)(e >> 8);
                  q0 = RAAC_SEXT6(p >> 6);
                  q1 = RAAC_SEXT6(p);
                  if (sgn)
                  {
                     unsigned nz = (unsigned)p >> 12;
                     uint32_t sb = (uint32_t)(cc >> 32), t, x;
                     t = q0 != 0; x = (sb >> 31) & t; q0 = (q0 ^ -(int)x) + (int)x; sb <<= t;
                     t = q1 != 0; x = (sb >> 31) & t; q1 = (q1 ^ -(int)x) + (int)x;
                     cc   <<= nz;
                     avail -= nz;
                     bp    += nz;
                  }
                  if (cb == RAAC_ESC_BOOK
                        && (q0 == 16 || q0 == -16 || q1 == 16 || q1 == -16))
                  {
                     /* escapes: back to the plain reader */
                     int q[2], j;
                     q[0]   = q0;
                     q[1]   = q1;
                     b->pos = bp;
                     for (j = 0; j < 2; j++)
                        if (q[j] == 16 || q[j] == -16)
                        {
                           int nb = 4;
                           int sign = q[j] < 0 ? -1 : 1;
                           int esc;
                           while (raac_getbits(b, 1))
                              nb++;
                           if (nb > 21 || b->err)
                              return -1;
                           esc  = (int)((1u << nb)
                                | raac_getbits(b, nb));
                           q[j] = sign * esc;
                        }
                     q0    = q[0];
                     q1    = q[1];
                     bp    = b->pos;
                     cc    = raac_peek64(b, bp);
                     avail = 57;
                     if (co)
                     {
                        co[i]     = raac_dq(a, q0, gn);
                        co[i + 1] = raac_dq(a, q1, gn);
                     }
                     else
                     {
                        qo[i]     = q0;
                        qo[i + 1] = q1;
                     }
                     continue;
                  }
                  if (co)
                  {
                     co[i]     = sq[q0] * gn;
                     co[i + 1] = sq[q1] * gn;
                  }
                  else
                  {
                     qo[i]     = q0;
                     qo[i + 1] = q1;
                  }
               }
            }
            if (bp > b->size * 8)
            {
               b->pos = b->size * 8;
               b->err = 1;
               return -1;
            }
            b->pos = bp;
         }
      }
      win_base += glen * 128;
   }
   return b->err ? -1 : 0;
}

/* ===== channel decode: dequant, tools, filterbank ===== */


/* quantised -> scaled spectrum, in window-interleaved layout for
 * short sequences (as spectral_data stored it: groups x windows x 128) */
static void raac_dequant(raac_t *a, raac_ch *c, const int quant[RAAC_FRAME])
{
   const uint16_t *swb = raac_swb_off(a, c->window_sequence == 2);
   int g, win_base = 0;
   memset(c->coef, 0, sizeof(c->coef));
   for (g = 0; g < c->num_window_groups; g++)
   {
      int glen = c->group_len[g], k;
      for (k = 0; k < c->max_sfb; k++)
      {
         int cb = c->band_cb[g][k];
         float gain;
         int w;
         if (cb == RAAC_CB_ZERO || cb >= RAAC_CB_NOISE)
            continue;
         gain = raac_sf_gain(a, c->sf[g][k]);
         for (w = 0; w < glen; w++)
         {
            int base = win_base + w * 128;
            int i;
            for (i = swb[k]; i < swb[k + 1]; i++)
            {
               int   q = quant[base + i];
               int   m = q < 0 ? -q : q;
               float v = m < RAAC_IQ_TAB ? a->iq43[m]
                                         : (float)pow((double)m, 4.0 / 3.0);
               c->coef[base + i] = (q < 0 ? -v : v) * gain;
            }
         }
      }
      win_base += glen * ((c->window_sequence == 2) ? 128 : 1024);
   }
}

/* PNS: fill noise bands with unit-energy noise at the signalled level.
 * The spec leaves the generator free; a fixed LCG keeps it stable. */
static void raac_pns(raac_t *a, raac_ch *c)
{
   const uint16_t *swb = raac_swb_off(a, c->window_sequence == 2);
   int g, win_base = 0;
   for (g = 0; g < c->num_window_groups; g++)
   {
      int glen = c->group_len[g], k;
      for (k = 0; k < c->max_sfb; k++)
      {
         int w;
         if (c->band_cb[g][k] != RAAC_CB_NOISE)
            continue;
         for (w = 0; w < glen; w++)
         {
            int base = win_base + w * 128;
            int lo = swb[k], hi = swb[k + 1];
            float energy = 0.0f, scale;
            int i;
            for (i = lo; i < hi; i++)
            {
               a->noise_state = a->noise_state * 1664525u + 1013904223u;
               c->coef[base + i] =
                     (float)(int32_t)a->noise_state * (1.0f / 2147483648.0f);
               energy += c->coef[base + i] * c->coef[base + i];
            }
            scale = (float)(pow(2.0, 0.25 * c->sf[g][k])
                  / sqrt(energy > 0 ? energy : 1.0f));
            for (i = lo; i < hi; i++)
               c->coef[base + i] *= scale;
         }
      }
      win_base += glen * ((c->window_sequence == 2) ? 128 : 1024);
   }
}

/* M/S: mid/side -> left/right on bands both channels code normally */
static void raac_ms_stereo(raac_t *a, raac_ch *l, raac_ch *r,
      int ms_mask_all, uint8_t ms_used[RAAC_MAX_WIN][RAAC_MAX_SFB])
{
   const uint16_t *swb = raac_swb_off(a, l->window_sequence == 2);
   int g, win_base = 0;
   for (g = 0; g < l->num_window_groups; g++)
   {
      int glen = l->group_len[g], k;
      for (k = 0; k < l->max_sfb; k++)
      {
         int lcb = l->band_cb[g][k], rcb = r->band_cb[g][k];
         int w;
         if (!(ms_mask_all || ms_used[g][k]))
            continue;
         if (lcb >= RAAC_CB_NOISE || rcb >= RAAC_CB_NOISE)
            continue;   /* intensity/noise bands are not M/S           */
         for (w = 0; w < glen; w++)
         {
            int    base = win_base + w * 128;
            int    i    = swb[k], end = swb[k + 1];
            float *lc   = l->coef + base, *rc = r->coef + base;
            /* four at a time, then any odd end of a band */
            for (; i + 4 <= end; i += 4)
            {
               raac_v m = RAAC_LD(lc + i), s = RAAC_LD(rc + i);
               RAAC_ST(lc + i, RAAC_ADD(m, s));
               RAAC_ST(rc + i, RAAC_SUB(m, s));
            }
            for (; i < end; i++)
            {
               float m = lc[i], s = rc[i];
               lc[i] = m + s;
               rc[i] = m - s;
            }
         }
      }
      win_base += glen * ((l->window_sequence == 2) ? 128 : 1024);
   }
}

/* intensity stereo: right-channel bands coded 14/15 take the left
 * spectrum scaled by 0.5^(is_pos/4); codebook 14 flips the sign, and
 * an M/S flag on the band flips it again. */
static void raac_intensity(raac_t *a, raac_ch *l, raac_ch *r,
      int ms_mask_all, uint8_t ms_used[RAAC_MAX_WIN][RAAC_MAX_SFB])
{
   const uint16_t *swb = raac_swb_off(a, r->window_sequence == 2);
   int g, win_base = 0;
   for (g = 0; g < r->num_window_groups; g++)
   {
      int glen = r->group_len[g], k;
      for (k = 0; k < r->max_sfb; k++)
      {
         int cb = r->band_cb[g][k];
         float dir, scale;
         int w;
         if (cb != RAAC_CB_INTENSITY && cb != RAAC_CB_INTENSITY2)
            continue;
         dir = (cb == RAAC_CB_INTENSITY) ? 1.0f : -1.0f;
         if (ms_mask_all || ms_used[g][k])
            dir = -dir;
         scale = dir * (float)pow(0.5, 0.25 * r->sf[g][k]);
         for (w = 0; w < glen; w++)
         {
            int base = win_base + w * 128;
            int i;
            for (i = swb[k]; i < swb[k + 1]; i++)
               r->coef[base + i] = l->coef[base + i] * scale;
         }
      }
      win_base += glen * ((r->window_sequence == 2) ? 128 : 1024);
   }
}

/* TNS synthesis: all-pole filtering of the spectrum over each
 * signalled band range (14496-3 4.6.9) */
static void raac_tns_apply(raac_t *a, raac_ch *c)
{
   int short_win = (c->window_sequence == 2);
   const uint16_t *swb = raac_swb_off(a, short_win);
   int num_swb = raac_num_swb(a, short_win);
   int tns_max = short_win ? raac_tns_max_bands_128[a->sfi]
                           : raac_tns_max_bands_1024[a->sfi];
   int mmm = tns_max < c->max_sfb ? tns_max : c->max_sfb;
   int w;
   if (!c->tns_present)
      return;
   for (w = 0; w < c->num_windows; w++)
   {
      int bottom = num_swb;
      int f;
      for (f = 0; f < c->tns_n_filt[w]; f++)
      {
         int top    = bottom;
         int order  = c->tns_order[w][f];
         int start, end, size, inc, i;
         float lpc[21];
         bottom = top - c->tns_length[w][f];
         if (bottom < 0)
            bottom = 0;
         if (!order)
            continue;
         /* reflection coefficients -> direct form (Levinson step) */
         {
            float tmp[21];
            int m, ii;
            lpc[0] = 1.0f;
            for (m = 1; m <= order; m++)
            {
               float rc = c->tns_coef[w][f][m - 1];
               for (ii = 1; ii < m; ii++)
                  tmp[ii] = lpc[ii] + rc * lpc[m - ii];
               for (ii = 1; ii < m; ii++)
                  lpc[ii] = tmp[ii];
               lpc[m] = rc;
            }
         }
         start = swb[bottom < mmm ? bottom : mmm];
         end   = swb[top    < mmm ? top    : mmm];
         size  = end - start;
         if (size <= 0)
            continue;
         start += w * 128;
         if (c->tns_direction[w][f])
         {
            start = start + size - 1;
            inc   = -1;
         }
         else
            inc = 1;
         for (i = 0; i < size; i++)
         {
            float v = c->coef[start];
            int j;
            for (j = 1; j <= order && j <= i; j++)
               v -= lpc[j] * c->coef[start - j * inc];
            c->coef[start] = v;
            start += inc;
         }
      }
   }
}

/* ===== filterbank: IMDCT + windowing + overlap-add (4.6.11) ===== */

static void raac_filterbank(raac_t *a, raac_ch *c, float out[RAAC_FRAME])
{
   const float *long_cur  = c->window_shape ? a->kbd_long  : a->sine_long;
   const float *shrt_cur  = c->window_shape ? a->kbd_short : a->sine_short;
   const float *long_prev = c->prev_window_shape ? a->kbd_long  : a->sine_long;
   const float *shrt_prev = c->prev_window_shape ? a->kbd_short : a->sine_short;
   /* frame geometry: everything below derives from the frame length
    * (1024: 2048/256-point transforms, flat run 448, short hop 128;
    * 960: 1920/240, flat run 420, hop 120) */
   int   L    = (int)a->frame_len;
   int   Ls   = L >> 3;
   int   nl   = L * 2;
   int   ns   = Ls * 2;
   int   flat = (L - Ls) / 2;
   /* the transform output is windowed in place: every window store
    * below reads the same index it writes                          */
   float *buf = a->fb_win;
   int   i;

   if (c->window_sequence != 2)
   {
      raac_imdct(a, c->coef, buf, nl);
      /* first half: window with the previous frame's trailing shape */
      switch (c->window_sequence)
      {
         case 0:  /* only long  */
         case 1:  /* long start */
            for (i = 0; i + 4 <= L; i += 4)
               RAAC_ST(buf + i, RAAC_MUL(RAAC_LD(buf + i),
                        RAAC_LD(long_prev + i)));
            for (; i < L; i++)
               buf[i] = buf[i] * long_prev[i];
            break;
         default: /* long stop: zero head, short rise, then a flat
                   * run that the in-place windowing leaves alone   */
            for (i = 0; i < flat; i++)
               buf[i] = 0.0f;
            for (i = 0; i < Ls; i++)
               buf[flat + i] = buf[flat + i] * shrt_prev[i];
            break;
      }
      /* second half: this frame's trailing shape */
      switch (c->window_sequence)
      {
         case 0:  /* long tail */
         case 3:
            for (i = 0; i + 4 <= L; i += 4)
               RAAC_ST(buf + L + i, RAAC_MUL(RAAC_LD(buf + L + i),
                        RAAC_LD(long_cur + L + i)));
            for (; i < L; i++)
               buf[L + i] = buf[L + i] * long_cur[L + i];
            break;
         default: /* long start: a flat run kept as it stands, then
                   * a short fall and a zero tail                   */
            for (i = 0; i < Ls; i++)
               buf[L + flat + i] = buf[L + flat + i] * shrt_cur[Ls + i];
            for (i = L + flat + Ls; i < nl; i++)
               buf[i] = 0.0f;
            break;
      }
   }
   else
   {
      /* eight short windows, hop Ls, starting at the flat offset. The
       * spectral hop stays 128 for both frame lengths (960-frame short
       * windows carry 120 coefficients in 128-wide slots). */
      float *sbuf = a->fb_short;
      int    w;
      memset(buf, 0, sizeof(a->fb_win));
      for (w = 0; w < 8; w++)
      {
         raac_imdct(a, c->coef + w * 128, sbuf, ns);
         for (i = 0; i < ns; i++)
         {
            const float *head = (w == 0) ? shrt_prev : shrt_cur;
            float win = (i < Ls) ? head[i] : shrt_cur[i];
            buf[flat + w * Ls + i] += sbuf[i] * win;
         }
      }
   }

   for (i = 0; i + 4 <= L; i += 4)
      RAAC_ST(out + i, RAAC_ADD(RAAC_LD(buf + i), RAAC_LD(c->overlap + i)));
   for (; i < L; i++)
      out[i] = buf[i] + c->overlap[i];
   memcpy(c->overlap, buf + L, sizeof(float) * (size_t)L);
   c->prev_window_shape = c->window_shape;
}

/* ===== element decode ===== */

static int raac_decode_ics(raac_t *a, raac_bits *b, raac_ch *c,
      int common_window)
{
   int       *quant = a->quant;
   raac_pulse pulse;
   int global_gain = (int)raac_getbits(b, 8);
   pulse.pulse_present = 0;
   pulse.number        = 0;
   if (!common_window)
      if (raac_ics_info(a, b, c) < 0)
         return -1;
   if (raac_section_data(a, b, c) < 0)
      return -1;
   if (raac_scale_factor_data(a, b, c, global_gain) < 0)
      return -1;
   if (raac_getbits(b, 1)) /* pulse_data_present */
   {
      if (c->window_sequence == 2)
         return -1;                  /* pulse is long-window only     */
      pulse.pulse_present = 1;
      if (raac_pulse_data(b, &pulse) < 0)
         return -1;
   }
   c->tns_present = (int)raac_getbits(b, 1);
   if (c->tns_present)
      if (raac_tns_data(a, b, c) < 0)
         return -1;
   if (raac_getbits(b, 1)) /* gain_control_data_present     */
      return -1; /* SSR tool: out of scope */
   /* without pulses nothing touches the quantised values: decode the
    * spectrum scaled */
   if (!pulse.pulse_present)
      return raac_spectral_data(a, b, c, quant, c->coef);
   if (raac_spectral_data(a, b, c, quant, NULL) < 0)
      return -1;
   if (pulse.pulse_present)
   {
      const uint16_t *swb = raac_swb_off(a, 0);
      int k, i;
      if (pulse.start_sfb > raac_num_swb(a, 0))
         return -1;
      k = swb[pulse.start_sfb];
      for (i = 0; i < pulse.number; i++)
      {
         k += pulse.offset[i];
         if (k >= RAAC_FRAME)
            return -1;
         if (quant[k] > 0)
            quant[k] += pulse.amp[i];
         else
            quant[k] -= pulse.amp[i];   /* zero takes the negative sign,
                                         * matching deployed decoders   */
      }
   }
   raac_dequant(a, c, quant);
   return 0;
}

/* Decode one individual_channel_stream and reconstruct its spectrum
 * (the caller consumes the element_instance_tag). TNS synthesis is
 * deferred to the frame tail so spectral coupling can be applied on
 * either side of it. Also decodes the coupling channel inside a CCE,
 * which shares this exact syntax. */
static int raac_decode_sce(raac_t *a, raac_bits *b, raac_ch *c)
{
   if (raac_decode_ics(a, b, c, 0) < 0)
      return -1;
   raac_pns(a, c);
   return 0;
}

static int raac_decode_cpe(raac_t *a, raac_bits *b, raac_ch *l, raac_ch *r)
{
   uint8_t  ms_used[RAAC_MAX_WIN][RAAC_MAX_SFB];
   int      ms_mask = 0;
   int      common;
   common = (int)raac_getbits(b, 1);
   memset(ms_used, 0, sizeof(ms_used));
   if (common)
   {
      if (raac_ics_info(a, b, l) < 0)
         return -1;
      memcpy(&r->window_sequence, &l->window_sequence,
            (char*)&l->band_cb - (char*)&l->window_sequence);
      ms_mask = (int)raac_getbits(b, 2);
      if (ms_mask == 3)
         return -1;
      if (ms_mask == 1)
      {
         int g, k;
         for (g = 0; g < l->num_window_groups; g++)
            for (k = 0; k < l->max_sfb; k++)
               ms_used[g][k] = (uint8_t)raac_getbits(b, 1);
      }
   }
   if (raac_decode_ics(a, b, l, common) < 0)
      return -1;
   if (raac_decode_ics(a, b, r, common) < 0)
      return -1;
   raac_pns(a, l);
   raac_pns(a, r);
   if (common)
      raac_ms_stereo(a, l, r, ms_mask == 2, ms_used);
   raac_intensity(a, l, r, ms_mask == 2, ms_used);
   return 0;
}

/* gain step per coded gain word: 2^(1/8), 2^(1/4), 2^(1/2), 2 */
static const float raac_cce_scale[4] =
{
   1.09050773266525765921f, 1.18920711500272106672f,
   1.41421356237309504880f, 2.0f
};

/* coupling_channel_element (14496-3 4.4.2.2, table 4.8). Parses the
 * target list, the coupling channel's own individual_channel_stream,
 * and the gain element lists. Gain semantics track the reference
 * behaviour of deployed decoders: the first list is unity and sends
 * no bits; per-band lists DPCM-accumulate in the coded domain, a
 * delta of zero keeps the previous band's cached gain, and with
 * gain_element_sign set the accumulated word carries the sign in its
 * LSB with the magnitude above it. */
static int raac_decode_cce(raac_t *a, raac_bits *b, raac_cce *cc)
{
   int   num_gain = 0;
   int   c, g, sfb;
   int   sign, ind_sw;
   float scale;

   ind_sw    = (int)raac_getbits(b, 1);
   cc->point = 2 * ind_sw;
   cc->ntarg = (int)raac_getbits(b, 3) + 1;
   for (c = 0; c < cc->ntarg; c++)
   {
      num_gain++;
      cc->targ_is_cpe[c] = (uint8_t)raac_getbits(b, 1);
      cc->targ_tag[c]    = (uint8_t)raac_getbits(b, 4);
      if (cc->targ_is_cpe[c])
      {
         cc->targ_sel[c] = (uint8_t)raac_getbits(b, 2);
         if (cc->targ_sel[c] == 3)
            num_gain++;
      }
      else
         cc->targ_sel[c] = 2;
   }
   /* cc_domain; independently switched coupling is always applied in
    * the time domain, folding both flags into one coupling point */
   cc->point += (raac_getbits(b, 1) || ind_sw) ? 1 : 0;

   sign  = (int)raac_getbits(b, 1);
   scale = raac_cce_scale[raac_getbits(b, 2)];

   if (raac_decode_sce(a, b, &cc->ch) < 0)
      return -1;

   for (c = 0; c < num_gain; c++)
   {
      int   idx  = 0;
      int   cge  = 1;
      int   gain = 0;
      float gain_cache = 1.0f;
      if (c)
      {
         cge = (cc->point == 3) ? 1 : (int)raac_getbits(b, 1);
         if (cge)
         {
            gain = raac_huff_decode_sf(b, &a->sfh);
            if (gain < 0)
               return -1;
            gain -= 60;
         }
         gain_cache = (float)pow((double)scale, (double)-gain);
      }
      if (cc->point == 3)
         cc->gain[c][0] = gain_cache;
      else
      {
         for (g = 0; g < cc->ch.num_window_groups; g++)
            for (sfb = 0; sfb < cc->ch.max_sfb; sfb++, idx++)
            {
               if (cc->ch.band_cb[g][sfb] == RAAC_CB_ZERO)
                  continue;
               if (!cge)
               {
                  int t = raac_huff_decode_sf(b, &a->sfh);
                  if (t < 0)
                     return -1;
                  t -= 60;
                  if (t)
                  {
                     int s = 1;
                     t = gain += t;
                     if (sign)
                     {
                        s  -= 2 * (t & 1);
                        t >>= 1;
                     }
                     gain_cache = (float)pow((double)scale,
                           (double)-t) * (float)s;
                  }
               }
               cc->gain[c][idx] = gain_cache;
            }
      }
   }
   return b->err ? -1 : 0;
}

/* Spectral-domain coupling: add one gain element list's scaling of
 * the coupling channel into a target channel, walking the coupling
 * channel's own band layout. The layouts are expected to agree; if a
 * hostile stream disagrees the adds still stay inside the frame. */
static void raac_cce_add_spec(const raac_t *a, raac_cce *cc, raac_ch *t,
      int list)
{
   const raac_ch  *c   = &cc->ch;
   const uint16_t *swb = raac_swb_off(a, c->window_sequence == 2);
   int g, idx = 0, win_base = 0;
   for (g = 0; g < c->num_window_groups; g++)
   {
      int glen = c->group_len[g], sfb;
      for (sfb = 0; sfb < c->max_sfb; sfb++, idx++)
      {
         if (c->band_cb[g][sfb] != RAAC_CB_ZERO)
         {
            float gain = cc->gain[list][idx];
            int w;
            for (w = 0; w < glen; w++)
            {
               int base = win_base + w * 128;
               int i;
               for (i = swb[sfb]; i < swb[sfb + 1]; i++)
                  t->coef[base + i] += gain * c->coef[base + i];
            }
         }
      }
      win_base += glen * ((c->window_sequence == 2) ? 128 : 1024);
   }
}

/* Walk one CCE's target list for a decoded element, keeping the gain
 * list index in step with unmatched targets, and apply the coupling
 * at the requested point to the element's channel(s). ch_select: 2
 * couples the first (or only) channel, 1 the second, 0 both from a
 * shared list, 3 both from separate lists. */
static void raac_cce_couple(raac_t *a, raac_cce *cc, int point,
      int is_cpe, int tag, unsigned ch0, float *pcm0, float *pcm1)
{
   int c, index = 0;
   if (!cc->present || cc->point != point)
      return;
   for (c = 0; c < cc->ntarg; c++)
   {
      if ((int)cc->targ_is_cpe[c] == is_cpe && (int)cc->targ_tag[c] == tag)
      {
         if (cc->targ_sel[c] != 1)
         {
            if (point == 3)
            {
               float gain = cc->gain[index][0];
               int i;
               for (i = 0; i < (int)a->frame_len; i++)
                  pcm0[i] += gain * cc->time[i];
            }
            else
               raac_cce_add_spec(a, cc, &a->ch[ch0], index);
            if (cc->targ_sel[c] != 0)
               index++;
         }
         if (cc->targ_sel[c] != 2)
         {
            if (point == 3)
            {
               float gain = cc->gain[index][0];
               int i;
               for (i = 0; i < (int)a->frame_len; i++)
                  pcm1[i] += gain * cc->time[i];
            }
            else
               raac_cce_add_spec(a, cc, &a->ch[ch0 + 1], index);
            index++;
         }
      }
      else
         index += 1 + (cc->targ_sel[c] == 3);
   }
}

/* ===== SBR huffman codebooks (14496-3 tables 4.A.75+): symbol i
 * decodes to i - lav, with lav per book below ===== */

static const uint8_t raac_t_sbr_env_1_5dB_bits[121] = {
   18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 17, 18,
   16, 17, 18, 17, 16, 16, 16, 16, 15, 14, 14, 13, 13, 12, 11, 10, 9, 8, 7,
   6, 5, 4, 3, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 14, 15, 16, 17, 16,
   19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19, 19, 19
};

static const uint32_t raac_t_sbr_env_1_5dB_code[121] = {
   0x3ffd6, 0x3ffd7, 0x3ffd8, 0x3ffd9, 0x3ffda, 0x3ffdb, 0x7ffb8, 0x7ffb9,
   0x7ffba, 0x7ffbb, 0x7ffbc, 0x7ffbd, 0x7ffbe, 0x7ffbf, 0x7ffc0, 0x7ffc1,
   0x7ffc2, 0x7ffc3, 0x7ffc4, 0x7ffc5, 0x7ffc6, 0x7ffc7, 0x7ffc8, 0x7ffc9,
   0x7ffca, 0x7ffcb, 0x7ffcc, 0x7ffcd, 0x7ffce, 0x7ffcf, 0x7ffd0, 0x7ffd1,
   0x7ffd2, 0x7ffd3, 0x1ffe6, 0x3ffd4, 0x0fff0, 0x1ffe9, 0x3ffd5, 0x1ffe7,
   0x0fff1, 0x0ffec, 0x0ffed, 0x0ffee, 0x07ff4, 0x03ff9, 0x03ff7, 0x01ffa,
   0x01ff9, 0x00ffb, 0x007fc, 0x003fc, 0x001fd, 0x000fd, 0x0007d, 0x0003d,
   0x0001d, 0x0000d, 0x00005, 0x00001, 0x00000, 0x00004, 0x0000c, 0x0001c,
   0x0003c, 0x0007c, 0x000fc, 0x001fc, 0x003fd, 0x00ffa, 0x01ff8, 0x03ff6,
   0x03ff8, 0x07ff5, 0x0ffef, 0x1ffe8, 0x0fff2, 0x7ffd4, 0x7ffd5, 0x7ffd6,
   0x7ffd7, 0x7ffd8, 0x7ffd9, 0x7ffda, 0x7ffdb, 0x7ffdc, 0x7ffdd, 0x7ffde,
   0x7ffdf, 0x7ffe0, 0x7ffe1, 0x7ffe2, 0x7ffe3, 0x7ffe4, 0x7ffe5, 0x7ffe6,
   0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb, 0x7ffec, 0x7ffed, 0x7ffee,
   0x7ffef, 0x7fff0, 0x7fff1, 0x7fff2, 0x7fff3, 0x7fff4, 0x7fff5, 0x7fff6,
   0x7fff7, 0x7fff8, 0x7fff9, 0x7fffa, 0x7fffb, 0x7fffc, 0x7fffd, 0x7fffe,
   0x7ffff
};

static const uint8_t raac_f_sbr_env_1_5dB_bits[121] = {
   19, 19, 20, 20, 20, 20, 20, 20, 20, 19, 20, 20, 20, 20, 19, 20, 19, 19,
   20, 18, 20, 20, 20, 19, 20, 20, 20, 19, 20, 19, 18, 19, 18, 18, 17, 18,
   17, 17, 17, 16, 16, 16, 15, 15, 14, 13, 13, 12, 12, 11, 10, 9, 9, 8, 7, 6,
   5, 4, 3, 2, 2, 3, 4, 5, 6, 8, 8, 9, 10, 11, 11, 11, 12, 12, 13, 13, 14,
   14, 16, 16, 17, 17, 18, 18, 18, 18, 18, 18, 18, 20, 19, 20, 20, 20, 20,
   20, 20, 19, 20, 20, 20, 20, 19, 20, 18, 20, 20, 19, 19, 20, 20, 20, 20,
   20, 20, 20, 20, 20, 20, 20, 20
};

static const uint32_t raac_f_sbr_env_1_5dB_code[121] = {
   0x7ffe7, 0x7ffe8, 0xfffd2, 0xfffd3, 0xfffd4, 0xfffd5, 0xfffd6, 0xfffd7,
   0xfffd8, 0x7ffda, 0xfffd9, 0xfffda, 0xfffdb, 0xfffdc, 0x7ffdb, 0xfffdd,
   0x7ffdc, 0x7ffdd, 0xfffde, 0x3ffe4, 0xfffdf, 0xfffe0, 0xfffe1, 0x7ffde,
   0xfffe2, 0xfffe3, 0xfffe4, 0x7ffdf, 0xfffe5, 0x7ffe0, 0x3ffe8, 0x7ffe1,
   0x3ffe0, 0x3ffe9, 0x1ffef, 0x3ffe5, 0x1ffec, 0x1ffed, 0x1ffee, 0x0fff4,
   0x0fff3, 0x0fff0, 0x07ff7, 0x07ff6, 0x03ffa, 0x01ffa, 0x01ff9, 0x00ffa,
   0x00ff8, 0x007f9, 0x003fb, 0x001fc, 0x001fa, 0x000fb, 0x0007c, 0x0003c,
   0x0001c, 0x0000c, 0x00005, 0x00001, 0x00000, 0x00004, 0x0000d, 0x0001d,
   0x0003d, 0x000fa, 0x000fc, 0x001fb, 0x003fa, 0x007f8, 0x007fa, 0x007fb,
   0x00ff9, 0x00ffb, 0x01ff8, 0x01ffb, 0x03ff8, 0x03ff9, 0x0fff1, 0x0fff2,
   0x1ffea, 0x1ffeb, 0x3ffe1, 0x3ffe2, 0x3ffea, 0x3ffe3, 0x3ffe6, 0x3ffe7,
   0x3ffeb, 0xfffe6, 0x7ffe2, 0xfffe7, 0xfffe8, 0xfffe9, 0xfffea, 0xfffeb,
   0xfffec, 0x7ffe3, 0xfffed, 0xfffee, 0xfffef, 0xffff0, 0x7ffe4, 0xffff1,
   0x3ffec, 0xffff2, 0xffff3, 0x7ffe5, 0x7ffe6, 0xffff4, 0xffff5, 0xffff6,
   0xffff7, 0xffff8, 0xffff9, 0xffffa, 0xffffb, 0xffffc, 0xffffd, 0xffffe,
   0xfffff
};

static const uint8_t raac_t_sbr_env_bal_1_5dB_bits[49] = {
   16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
   12, 11, 9, 7, 5, 3, 1, 2, 4, 6, 8, 11, 12, 15, 16, 16, 16, 16, 16, 16, 16,
   17, 17, 17, 17, 17, 17, 17, 17, 17, 17
};

static const uint32_t raac_t_sbr_env_bal_1_5dB_code[49] = {
   0x0ffe4, 0x0ffe5, 0x0ffe6, 0x0ffe7, 0x0ffe8, 0x0ffe9, 0x0ffea, 0x0ffeb,
   0x0ffec, 0x0ffed, 0x0ffee, 0x0ffef, 0x0fff0, 0x0fff1, 0x0fff2, 0x0fff3,
   0x0fff4, 0x0ffe2, 0x00ffc, 0x007fc, 0x001fe, 0x0007e, 0x0001e, 0x00006,
   0x00000, 0x00002, 0x0000e, 0x0003e, 0x000fe, 0x007fd, 0x00ffd, 0x07ff0,
   0x0ffe3, 0x0fff5, 0x0fff6, 0x0fff7, 0x0fff8, 0x0fff9, 0x0fffa, 0x1fff6,
   0x1fff7, 0x1fff8, 0x1fff9, 0x1fffa, 0x1fffb, 0x1fffc, 0x1fffd, 0x1fffe,
   0x1ffff
};

static const uint8_t raac_f_sbr_env_bal_1_5dB_bits[49] = {
   18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 16, 17, 14,
   11, 11, 8, 7, 4, 2, 1, 3, 5, 6, 9, 11, 12, 15, 16, 18, 18, 18, 18, 18, 18,
   18, 18, 18, 18, 18, 18, 18, 18, 19, 19
};

static const uint32_t raac_f_sbr_env_bal_1_5dB_code[49] = {
   0x3ffe2, 0x3ffe3, 0x3ffe4, 0x3ffe5, 0x3ffe6, 0x3ffe7, 0x3ffe8, 0x3ffe9,
   0x3ffea, 0x3ffeb, 0x3ffec, 0x3ffed, 0x3ffee, 0x3ffef, 0x3fff0, 0x0fff7,
   0x1fff0, 0x03ffc, 0x007fe, 0x007fc, 0x000fe, 0x0007e, 0x0000e, 0x00002,
   0x00000, 0x00006, 0x0001e, 0x0003e, 0x001fe, 0x007fd, 0x00ffe, 0x07ffa,
   0x0fff6, 0x3fff1, 0x3fff2, 0x3fff3, 0x3fff4, 0x3fff5, 0x3fff6, 0x3fff7,
   0x3fff8, 0x3fff9, 0x3fffa, 0x3fffb, 0x3fffc, 0x3fffd, 0x3fffe, 0x7fffe,
   0x7ffff
};

static const uint8_t raac_t_sbr_env_3_0dB_bits[63] = {
   18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 17,
   16, 16, 16, 14, 14, 14, 13, 12, 11, 8, 6, 4, 2, 1, 3, 5, 7, 9, 11, 13, 14,
   14, 15, 16, 17, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   19, 19, 19, 19, 19, 19
};

static const uint32_t raac_t_sbr_env_3_0dB_code[63] = {
   0x3ffed, 0x3ffee, 0x7ffde, 0x7ffdf, 0x7ffe0, 0x7ffe1, 0x7ffe2, 0x7ffe3,
   0x7ffe4, 0x7ffe5, 0x7ffe6, 0x7ffe7, 0x7ffe8, 0x7ffe9, 0x7ffea, 0x7ffeb,
   0x7ffec, 0x1fff4, 0x0fff7, 0x0fff9, 0x0fff8, 0x03ffb, 0x03ffa, 0x03ff8,
   0x01ffa, 0x00ffc, 0x007fc, 0x000fe, 0x0003e, 0x0000e, 0x00002, 0x00000,
   0x00006, 0x0001e, 0x0007e, 0x001fe, 0x007fd, 0x01ffb, 0x03ff9, 0x03ffc,
   0x07ffa, 0x0fff6, 0x1fff5, 0x3ffec, 0x7ffed, 0x7ffee, 0x7ffef, 0x7fff0,
   0x7fff1, 0x7fff2, 0x7fff3, 0x7fff4, 0x7fff5, 0x7fff6, 0x7fff7, 0x7fff8,
   0x7fff9, 0x7fffa, 0x7fffb, 0x7fffc, 0x7fffd, 0x7fffe, 0x7ffff
};

static const uint8_t raac_f_sbr_env_3_0dB_bits[63] = {
   20, 20, 20, 20, 20, 20, 20, 18, 19, 19, 19, 19, 18, 18, 20, 19, 17, 18,
   17, 16, 16, 15, 14, 12, 11, 10, 9, 8, 6, 4, 2, 1, 3, 5, 8, 9, 10, 11, 12,
   13, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 20, 19, 19, 19, 20, 19, 19,
   20, 20, 20, 20, 20, 20
};

static const uint32_t raac_f_sbr_env_3_0dB_code[63] = {
   0xffff0, 0xffff1, 0xffff2, 0xffff3, 0xffff4, 0xffff5, 0xffff6, 0x3fff3,
   0x7fff5, 0x7ffee, 0x7ffef, 0x7fff6, 0x3fff4, 0x3fff2, 0xffff7, 0x7fff0,
   0x1fff5, 0x3fff0, 0x1fff4, 0x0fff7, 0x0fff6, 0x07ff8, 0x03ffb, 0x00ffd,
   0x007fd, 0x003fd, 0x001fd, 0x000fd, 0x0003e, 0x0000e, 0x00002, 0x00000,
   0x00006, 0x0001e, 0x000fc, 0x001fc, 0x003fc, 0x007fc, 0x00ffc, 0x01ffc,
   0x03ffa, 0x07ff9, 0x07ffa, 0x0fff8, 0x0fff9, 0x1fff6, 0x1fff7, 0x3fff5,
   0x3fff6, 0x3fff1, 0xffff8, 0x7fff1, 0x7fff2, 0x7fff3, 0xffff9, 0x7fff7,
   0x7fff4, 0xffffa, 0xffffb, 0xffffc, 0xffffd, 0xffffe, 0xfffff
};

static const uint8_t raac_t_sbr_env_bal_3_0dB_bits[25] = {
   13, 13, 13, 13, 13, 13, 13, 12, 8, 7, 4, 3, 1, 2, 5, 6, 9, 13, 13, 13, 13,
   13, 13, 14, 14
};

static const uint32_t raac_t_sbr_env_bal_3_0dB_code[25] = {
   0x1ff2, 0x1ff3, 0x1ff4, 0x1ff5, 0x1ff6, 0x1ff7, 0x1ff8, 0x0ff8, 0x00fe,
   0x007e, 0x000e, 0x0006, 0x0000, 0x0002, 0x001e, 0x003e, 0x01fe, 0x1ff9,
   0x1ffa, 0x1ffb, 0x1ffc, 0x1ffd, 0x1ffe, 0x3ffe, 0x3fff
};

static const uint8_t raac_f_sbr_env_bal_3_0dB_bits[25] = {
   13, 13, 13, 13, 13, 14, 14, 11, 8, 7, 4, 2, 1, 3, 5, 6, 9, 12, 13, 14, 14,
   14, 14, 14, 14
};

static const uint32_t raac_f_sbr_env_bal_3_0dB_code[25] = {
   0x1ff7, 0x1ff8, 0x1ff9, 0x1ffa, 0x1ffb, 0x3ff8, 0x3ff9, 0x07fc, 0x00fe,
   0x007e, 0x000e, 0x0002, 0x0000, 0x0006, 0x001e, 0x003e, 0x01fe, 0x0ffa,
   0x1ff6, 0x3ffa, 0x3ffb, 0x3ffc, 0x3ffd, 0x3ffe, 0x3fff
};

static const uint8_t raac_t_sbr_noise_3_0dB_bits[63] = {
   13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
   13, 13, 13, 13, 13, 13, 13, 13, 11, 8, 6, 4, 3, 1, 2, 5, 8, 10, 13, 13,
   13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
   13, 13, 13, 13, 13, 14, 14
};

static const uint32_t raac_t_sbr_noise_3_0dB_code[63] = {
   0x1fce, 0x1fcf, 0x1fd0, 0x1fd1, 0x1fd2, 0x1fd3, 0x1fd4, 0x1fd5, 0x1fd6,
   0x1fd7, 0x1fd8, 0x1fd9, 0x1fda, 0x1fdb, 0x1fdc, 0x1fdd, 0x1fde, 0x1fdf,
   0x1fe0, 0x1fe1, 0x1fe2, 0x1fe3, 0x1fe4, 0x1fe5, 0x1fe6, 0x1fe7, 0x07f2,
   0x00fd, 0x003e, 0x000e, 0x0006, 0x0000, 0x0002, 0x001e, 0x00fc, 0x03f8,
   0x1fcc, 0x1fe8, 0x1fe9, 0x1fea, 0x1feb, 0x1fec, 0x1fcd, 0x1fed, 0x1fee,
   0x1fef, 0x1ff0, 0x1ff1, 0x1ff2, 0x1ff3, 0x1ff4, 0x1ff5, 0x1ff6, 0x1ff7,
   0x1ff8, 0x1ff9, 0x1ffa, 0x1ffb, 0x1ffc, 0x1ffd, 0x1ffe, 0x3ffe, 0x3fff
};

static const uint8_t raac_t_sbr_noise_bal_3_0dB_bits[25] = {
   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 5, 2, 1, 3, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

static const uint32_t raac_t_sbr_noise_bal_3_0dB_code[25] = {
   0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0x1c, 0x02,
   0x00, 0x06, 0x3a, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
   0xff
};

/* largest absolute value per book, in table order: decoded symbol i
 * means value i - lav */
static const int raac_sbr_lav[10] =
{
   60, 60, 24, 24, 31, 31, 12, 12, 31, 12
};

/* the ten books in lav order; codes reach 19 bits so these decode
 * by a length-walking scan rather than the 16-bit fast tables - SBR
 * side data is a couple hundred symbols per frame at most */
typedef struct
{
   const uint32_t *code;
   const uint8_t  *bits;
   int             n;
} raac_sbr_book;

static const raac_sbr_book raac_sbr_books[10] =
{
   { raac_t_sbr_env_1_5dB_code,       raac_t_sbr_env_1_5dB_bits,       121 },
   { raac_f_sbr_env_1_5dB_code,       raac_f_sbr_env_1_5dB_bits,       121 },
   { raac_t_sbr_env_bal_1_5dB_code,   raac_t_sbr_env_bal_1_5dB_bits,    49 },
   { raac_f_sbr_env_bal_1_5dB_code,   raac_f_sbr_env_bal_1_5dB_bits,    49 },
   { raac_t_sbr_env_3_0dB_code,       raac_t_sbr_env_3_0dB_bits,        63 },
   { raac_f_sbr_env_3_0dB_code,       raac_f_sbr_env_3_0dB_bits,        63 },
   { raac_t_sbr_env_bal_3_0dB_code,   raac_t_sbr_env_bal_3_0dB_bits,    25 },
   { raac_f_sbr_env_bal_3_0dB_code,   raac_f_sbr_env_bal_3_0dB_bits,    25 },
   { raac_t_sbr_noise_3_0dB_code,     raac_t_sbr_noise_3_0dB_bits,      63 },
   { raac_t_sbr_noise_bal_3_0dB_code, raac_t_sbr_noise_bal_3_0dB_bits,  25 }
};

static int raac_sbr_huff(raac_bits *b, const raac_sbr_book *h)
{
   uint32_t acc = 0;
   unsigned len = 0;
   while (len < 24 && !b->err)
   {
      int i;
      acc = (acc << 1) | raac_getbits(b, 1);
      len++;
      for (i = 0; i < h->n; i++)
         if (h->bits[i] == len && h->code[i] == acc)
            return i;
   }
   return -1;
}

/* QMF subband offsets of the start-frequency index, by SBR rate
 * class (16000 / 22050 / 24000 / 32000 / 44100-64000 / above) */
static const int8_t raac_sbr_offset[6][16] =
{
   { -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2,  3,  4,  5,  6,  7 },
   { -5, -4, -3, -2, -1,  0,  1,  2, 3, 4, 5,  6,  7,  9, 11, 13 },
   { -5, -3, -2, -1,  0,  1,  2,  3, 4, 5, 6,  7,  9, 11, 13, 16 },
   { -6, -4, -2, -1,  0,  1,  2,  3, 4, 5, 6,  7,  9, 11, 13, 16 },
   { -4, -2, -1,  0,  1,  2,  3,  4, 5, 6, 7,  9, 11, 13, 16, 20 },
   { -2, -1,  0,  1,  2,  3,  4,  5, 6, 7, 9, 11, 13, 16, 20, 24 }
};

static void raac_sbr_turnoff(raac_sbr *s)
{
   s->start = 0;
}

static int raac_sbr_cmp_u16(const void *a, const void *b)
{
   return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

static int raac_sbr_cmp_s16(const void *a, const void *b)
{
   return (int)*(const int16_t *)a - (int)*(const int16_t *)b;
}

/* geometric band split with cumulative rounding, per the reference
 * float implementation of the spec's band helper */
static void raac_sbr_make_bands(int16_t *bands, int start, int stop,
      int num_bands)
{
   int    k, previous, present;
   double base, prod;
   base     = pow((double)stop / start, 1.0 / num_bands);
   prod     = start;
   previous = start;
   for (k = 0; k < num_bands - 1; k++)
   {
      prod    *= base;
      present  = (int)floor(prod + 0.5);
      bands[k] = (int16_t)(present - previous);
      previous = present;
   }
   bands[num_bands - 1] = (int16_t)(stop - previous);
}

static int raac_sbr_in_table(const int16_t *table, int n, int needle)
{
   int i;
   for (i = 0; i < n; i++)
      if (table[i] == needle)
         return 1;
   return 0;
}

/* limiter band table: low-resolution borders merged with patch
 * borders, thinned to the signalled limiter density */
static void raac_sbr_make_f_tablelim(raac_sbr *s)
{
   int k;
   if (s->bs_limiter_bands > 0)
   {
      static const double warped[3] =
      {
         1.32715174233856803909, /* 2^(0.49/1.2) */
         1.18509277094158210129, /* 2^(0.49/2)   */
         1.11987160404675912501  /* 2^(0.49/3)   */
      };
      double    dens = warped[s->bs_limiter_bands - 1];
      int16_t   patch_borders[7];
      uint16_t *in  = s->f_tablelim + 1;
      uint16_t *out = s->f_tablelim;
      patch_borders[0] = (int16_t)s->kx[1];
      for (k = 1; k <= s->num_patches; k++)
         patch_borders[k] = (int16_t)(patch_borders[k - 1] +
               s->patch_num_subbands[k - 1]);
      memcpy(s->f_tablelim, s->f_tablelow,
            (size_t)(s->n[0] + 1) * sizeof(uint16_t));
      if (s->num_patches > 1)
      {
         for (k = 0; k < s->num_patches - 1; k++)
            s->f_tablelim[s->n[0] + 1 + k] = (uint16_t)patch_borders[k + 1];
      }
      qsort(s->f_tablelim, (size_t)(s->num_patches + s->n[0]),
            sizeof(uint16_t), raac_sbr_cmp_u16);
      s->n_lim = s->n[0] + s->num_patches - 1;
      while (out < s->f_tablelim + s->n_lim)
      {
         if ((double)*in >= *out * dens)
            *++out = *in++;
         else if (*in == *out ||
               !raac_sbr_in_table(patch_borders, s->num_patches + 1, *in))
         {
            in++;
            s->n_lim--;
         }
         else if (!raac_sbr_in_table(patch_borders, s->num_patches + 1, *out))
         {
            *out = *in++;
            s->n_lim--;
         }
         else
            *++out = *in++;
      }
   }
   else
   {
      s->f_tablelim[0] = s->f_tablelow[0];
      s->f_tablelim[1] = s->f_tablelow[s->n[0]];
      s->n_lim         = 1;
   }
}

/* master frequency band table (14496-3 4.6.18.3.2) */
static int raac_sbr_make_f_master(raac_sbr *s)
{
   unsigned      start_min, stop_min;
   unsigned      max_qmf_subbands;
   int           k;
   const int8_t *offs;
   unsigned      temp;
   int16_t       stop_dk[13];

   switch (s->sample_rate)
   {
      case 16000: offs = raac_sbr_offset[0]; break;
      case 22050: offs = raac_sbr_offset[1]; break;
      case 24000: offs = raac_sbr_offset[2]; break;
      case 32000: offs = raac_sbr_offset[3]; break;
      case 44100: case 48000: case 64000:
         offs = raac_sbr_offset[4]; break;
      case 88200: case 96000: case 128000: case 176400: case 192000:
         offs = raac_sbr_offset[5]; break;
      default:
         return -1;
   }
   if (s->sample_rate < 32000)
      temp = 3000;
   else if (s->sample_rate < 64000)
      temp = 4000;
   else
      temp = 5000;
   start_min = ((temp << 7) + (s->sample_rate >> 1)) / s->sample_rate;
   stop_min  = ((temp << 8) + (s->sample_rate >> 1)) / s->sample_rate;

   s->k0 = (int)start_min + offs[s->bs_start_freq];

   if (s->bs_stop_freq < 14)
   {
      s->k2 = (int)stop_min;
      raac_sbr_make_bands(stop_dk, (int)stop_min, 64, 13);
      qsort(stop_dk, 13, sizeof(int16_t), raac_sbr_cmp_s16);
      for (k = 0; k < s->bs_stop_freq; k++)
         s->k2 += stop_dk[k];
   }
   else if (s->bs_stop_freq == 14)
      s->k2 = 2 * s->k0;
   else
      s->k2 = 3 * s->k0;
   if (s->k2 > 64)
      s->k2 = 64;

   if (s->sample_rate <= 32000)
      max_qmf_subbands = 48;
   else if (s->sample_rate == 44100)
      max_qmf_subbands = 35;
   else
      max_qmf_subbands = 32;
   if (s->k0 < 0 || (unsigned)(s->k2 - s->k0) > max_qmf_subbands)
      return -1;

   if (!s->bs_freq_scale)
   {
      int dk = s->bs_alter_scale + 1;
      int k2diff;
      s->n_master = ((s->k2 - s->k0 + (dk & 2)) >> dk) << 1;
      if (s->n_master <= 0 || s->n_master > 48 ||
            s->bs_xover_band >= s->n_master)
         return -1;
      for (k = 1; k <= s->n_master; k++)
         s->f_master[k] = (uint16_t)dk;
      k2diff = s->k2 - s->k0 - s->n_master * dk;
      if (k2diff < 0)
      {
         s->f_master[1]--;
         s->f_master[2] = (uint16_t)(s->f_master[2] - (k2diff < -1));
      }
      else if (k2diff)
         s->f_master[s->n_master]++;
      s->f_master[0] = (uint16_t)s->k0;
      for (k = 1; k <= s->n_master; k++)
         s->f_master[k] = (uint16_t)(s->f_master[k] + s->f_master[k - 1]);
   }
   else
   {
      int     half_bands = 7 - s->bs_freq_scale;
      int     two_regions, num_bands_0;
      int     vdk0_max, vdk1_min;
      int16_t vk0[49];
      if (49 * s->k2 > 110 * s->k0)
      {
         two_regions = 1;
         s->k1       = 2 * s->k0;
      }
      else
      {
         two_regions = 0;
         s->k1       = s->k2;
      }
      num_bands_0 = (int)floor(half_bands *
            (log((double)s->k1 / s->k0) / log(2.0)) + 0.5) * 2;
      if (num_bands_0 <= 0 || num_bands_0 > 48)
         return -1;
      vk0[0] = 0;
      raac_sbr_make_bands(vk0 + 1, s->k0, s->k1, num_bands_0);
      qsort(vk0 + 1, (size_t)num_bands_0, sizeof(int16_t),
            raac_sbr_cmp_s16);
      vdk0_max = vk0[num_bands_0];
      vk0[0]   = (int16_t)s->k0;
      for (k = 1; k <= num_bands_0; k++)
      {
         if (vk0[k] <= 0)
            return -1;
         vk0[k] = (int16_t)(vk0[k] + vk0[k - 1]);
      }
      if (two_regions)
      {
         int16_t vk1[49];
         double  invwarp = s->bs_alter_scale ? (1.0 / 1.3) : 1.0;
         int     num_bands_1 = (int)floor(half_bands * invwarp *
               (log((double)s->k2 / s->k1) / log(2.0)) + 0.5) * 2;
         if (num_bands_1 < 0 || num_bands_0 + num_bands_1 > 48)
            return -1;
         raac_sbr_make_bands(vk1 + 1, s->k1, s->k2, num_bands_1);
         vdk1_min = vk1[1];
         for (k = 2; k <= num_bands_1; k++)
            if (vk1[k] < vdk1_min)
               vdk1_min = vk1[k];
         if (vdk1_min < vdk0_max)
         {
            int change;
            qsort(vk1 + 1, (size_t)num_bands_1, sizeof(int16_t),
                  raac_sbr_cmp_s16);
            change = vdk0_max - vk1[1];
            if (change > (vk1[num_bands_1] - vk1[1]) >> 1)
               change = (vk1[num_bands_1] - vk1[1]) >> 1;
            vk1[1]           = (int16_t)(vk1[1] + change);
            vk1[num_bands_1] = (int16_t)(vk1[num_bands_1] - change);
         }
         qsort(vk1 + 1, (size_t)num_bands_1, sizeof(int16_t),
               raac_sbr_cmp_s16);
         vk1[0] = (int16_t)s->k1;
         for (k = 1; k <= num_bands_1; k++)
         {
            if (vk1[k] <= 0)
               return -1;
            vk1[k] = (int16_t)(vk1[k] + vk1[k - 1]);
         }
         s->n_master = num_bands_0 + num_bands_1;
         if (s->bs_xover_band >= s->n_master)
            return -1;
         for (k = 0; k <= num_bands_0; k++)
            s->f_master[k] = (uint16_t)vk0[k];
         for (k = 1; k <= num_bands_1; k++)
            s->f_master[num_bands_0 + k] = (uint16_t)vk1[k];
      }
      else
      {
         s->n_master = num_bands_0;
         if (s->bs_xover_band >= s->n_master)
            return -1;
         for (k = 0; k <= num_bands_0; k++)
            s->f_master[k] = (uint16_t)vk0[k];
      }
   }
   return 0;
}

/* patch construction (14496-3 4.6.18.6.3) */
static int raac_sbr_calc_patches(raac_sbr *s)
{
   int i, k, sb = 0;
   int msb     = s->k0;
   int usb     = s->kx[1];
   int goal_sb = (int)(((1000u << 11) + (s->sample_rate >> 1)) /
         s->sample_rate);

   s->num_patches = 0;
   if (goal_sb < s->kx[1] + s->m[1])
   {
      for (k = 0; s->f_master[k] < goal_sb; k++) ;
   }
   else
      k = s->n_master;

   do
   {
      int odd = 0;
      for (i = k; i == k || sb > (s->k0 - 1 + msb - odd); i--)
      {
         sb  = s->f_master[i];
         odd = (sb + s->k0) & 1;
      }
      if (s->num_patches > 5)
         return -1;
      s->patch_num_subbands[s->num_patches] =
            (uint8_t)((sb - usb) > 0 ? (sb - usb) : 0);
      s->patch_start_subband[s->num_patches] =
            (uint8_t)(s->k0 - odd - s->patch_num_subbands[s->num_patches]);
      if (s->patch_num_subbands[s->num_patches] > 0)
      {
         usb = sb;
         msb = sb;
         s->num_patches++;
      }
      else
         msb = s->kx[1];
      if (s->f_master[k] - sb < 3)
         k = s->n_master;
   } while (sb != s->kx[1] + s->m[1]);

   if (s->num_patches > 1 &&
         s->patch_num_subbands[s->num_patches - 1] < 3)
      s->num_patches--;
   return 0;
}

/* derived tables: high/low resolution, noise floor and limiter bands
 * (14496-3 4.6.18.3.2.2) */
static int raac_sbr_make_f_derived(raac_sbr *s)
{
   int k, temp;
   s->n[1] = s->n_master - s->bs_xover_band;
   s->n[0] = (s->n[1] + 1) >> 1;
   memcpy(s->f_tablehigh, &s->f_master[s->bs_xover_band],
         (size_t)(s->n[1] + 1) * sizeof(uint16_t));
   s->m[1]  = s->f_tablehigh[s->n[1]] - s->f_tablehigh[0];
   s->kx[1] = s->f_tablehigh[0];
   if (s->kx[1] + s->m[1] > 64 || s->kx[1] > 32)
      return -1;
   s->f_tablelow[0] = s->f_tablehigh[0];
   temp = s->n[1] & 1;
   for (k = 1; k <= s->n[0]; k++)
      s->f_tablelow[k] = s->f_tablehigh[2 * k - temp];
   temp = (int)floor(s->bs_noise_bands *
         (log((double)s->k2 / s->kx[1]) / log(2.0)) + 0.5);
   s->n_q = temp < 1 ? 1 : temp;
   if (s->n_q > 5)
      return -1;
   s->f_tablenoise[0] = s->f_tablelow[0];
   temp = 0;
   for (k = 1; k <= s->n_q; k++)
   {
      temp += (s->n[0] - temp) / (s->n_q + 1 - k);
      s->f_tablenoise[k] = s->f_tablelow[temp];
   }
   if (raac_sbr_calc_patches(s) < 0)
      return -1;
   raac_sbr_make_f_tablelim(s);
   return 0;
}


/* Half of the 128-point IMDCT for the QMF transforms: the middle
 * half of the full transform, computed with the same 32-point-FFT
 * factorization as the main filterbank. out[j] = scale * sum_k
 * in[k] * cos(pi/256 * (2j+129) * (2k+1)). */
static void raac_imdct_half128(raac_t *a, const float in[64],
      float out[64], float scale)
{
   float *fre = a->fft_re, *fim = a->fft_im;
   float  v[64], full[128];
   int    k;
   for (k = 0; k < 32; k++)
   {
      float xr = in[2 * k], xi = in[63 - 2 * k];
      float pr = a->tw32_re[k], pi_ = a->tw32_im[k];
      fre[k] = xr * pr - xi * pi_;
      fim[k] = xr * pi_ + xi * pr;
   }
   raac_fft(a, fre, fim, a->fft2_re, a->fft2_im, 32);
   fre = a->fft2_re;
   fim = a->fft2_im;
   for (k = 0; k < 32; k++)
   {
      float qr = a->tw32_re[32 + k], qi = a->tw32_im[32 + k];
      float yr = fre[k] * qr - fim[k] * qi;
      float yi = fre[k] * qi + fim[k] * qr;
      v[2 * k]      =  yr;
      v[63 - 2 * k] = -yi;
   }
   for (k = 0; k < 32; k++)
   {
      full[k]      =  v[32 + k];
      full[32 + k] = -v[63 - k];
      full[64 + k] = -v[31 - k];
      full[96 + k] = -v[k];
   }
   for (k = 0; k < 64; k++)
      out[k] = full[32 + k] * scale;
}

/* one slot of the 32-band analysis QMF over the core signal
 * (14496-3 4.6.18.4): 32 fresh time samples in, one complex
 * subband vector out */
static void raac_qmf_analysis_slot(raac_t *a, raac_qmf_ch *c,
      const float *in32, float Wr[32], float Wi[32])
{
   float *z   = a->qmf_z;
   float *z64 = a->qmf_z64;
   float *m   = a->qmf_m;
   int    i, k;
   float *x;
   if (c->xring == RAAC_QMF_RING)
   {
      memmove(c->x, c->x + 32 * RAAC_QMF_RING, sizeof(float) * 288);
      c->xring = 0;
   }
   x = c->x + 32 * c->xring++;
   memcpy(x + 288, in32, sizeof(float) * 32);
   /* z[i] = window[2i] * x[319 - i], then the five 64-sample folds */
   for (i = 0; i < 320; i += 4)
   {
      raac_v w = RAAC_EVEN(RAAC_LD(raac_qmf_window + 2 * i),
            RAAC_LD(raac_qmf_window + 2 * i + 4));
      RAAC_ST(z + i, RAAC_MUL(w, RAAC_REV(RAAC_LD(x + 316 - i))));
   }
   for (k = 0; k < 64; k += 4)
      RAAC_ST(z + k, RAAC_ADD(RAAC_ADD(RAAC_ADD(RAAC_LD(z + k),
                  RAAC_LD(z + k + 64)), RAAC_ADD(RAAC_LD(z + k + 128),
                  RAAC_LD(z + k + 192))), RAAC_LD(z + k + 256)));
   z64[0] = z[0];
   for (k = 1; k < 32; k++)
   {
      z64[2 * k - 1] =  z[k];
      z64[2 * k]     = -z[64 - k];
   }
   z64[63] = z[32];
   raac_imdct_half128(a, z64, m, -2.0f);
   for (k = 0; k < 32; k++)
   {
      Wr[k] = -m[63 - k];
      Wi[k] =  m[k];
   }
}

/* one slot of the 64-band synthesis QMF (14496-3 4.6.18.4): one
 * complex subband vector in, 64 output samples at the doubled rate */
static void raac_qmf_synthesis_slot(raac_t *a, raac_qmf_ch *c,
      const float Xr[64], const float Xi[64], float out64[64])
{
   static const uint16_t vo[10] =
      { 0, 192, 256, 448, 512, 704, 768, 960, 1024, 1216 };
   float *xim = a->qmf_xim;
   float *m0  = a->qmf_m0;
   float *m1  = a->qmf_m1;
   int    n, j;
   float *v;
   if (c->vring == RAAC_QMF_RING)
   {
      memmove(c->v + 128 * RAAC_QMF_RING, c->v, sizeof(float) * (1280 - 128));
      c->vring = 0;
   }
   v = c->v + 128 * (RAAC_QMF_RING - 1 - c->vring++);
   for (n = 0; n < 64; n++)
      xim[n] = (n & 1) ? -Xi[n] : Xi[n];
   raac_imdct_half128(a, Xr, m0, 1.0f / 64.0f);
   raac_imdct_half128(a, xim, m1, 1.0f / 64.0f);
   for (n = 0; n < 64; n++)
   {
      v[n]       = -m0[63 - n] + m1[n];
      v[127 - n] =  m0[63 - n] + m1[n];
   }
   /* out[n] = sum over j of v[n + vo[j]] * window[n + 64j], four n at
    * a time */
   for (n = 0; n < 64; n += 4)
   {
      raac_v acc = RAAC_MUL(RAAC_LD(v + n), RAAC_LD(raac_qmf_window + n));
      for (j = 1; j < 10; j++)
         acc = RAAC_ADD(acc, RAAC_MUL(RAAC_LD(v + n + vo[j]),
                  RAAC_LD(raac_qmf_window + n + 64 * j)));
      RAAC_ST(out64 + n, acc);
   }
}


/* assemble the low-band QMF matrix for one channel: eight slots of
 * history from the previous frame's analysis, then the current
 * frame (14496-3 4.6.18.5) */
static void raac_sbr_lf_gen(raac_qmf_ch *q, float X_low[32][40][2],
      int kx_prev, int kx_cur)
{
   int i, k;
   memset(X_low, 0, sizeof(float) * 32 * 40 * 2);
   for (k = 0; k < kx_cur; k++)
      for (i = 8; i < 40; i++)
      {
         X_low[k][i][0] = q->W[1][i - 8][k][0];
         X_low[k][i][1] = q->W[1][i - 8][k][1];
      }
   for (k = 0; k < kx_prev; k++)
      for (i = 0; i < 8; i++)
      {
         X_low[k][i][0] = q->W[0][i + 24][k][0];
         X_low[k][i][1] = q->W[0][i + 24][k][1];
      }
}

/* three-lag complex autocovariance over one band's slots, packed the
 * way the covariance solution below consumes it */
static void raac_sbr_autocorr(float x[40][2], float phi[3][2][2],
      int lag)
{
   int   i;
   float real_sum = 0.0f;
   float imag_sum = 0.0f;
   if (lag)
   {
      for (i = 1; i < 38; i++)
      {
         real_sum += x[i][0] * x[i + lag][0] + x[i][1] * x[i + lag][1];
         imag_sum += x[i][0] * x[i + lag][1] - x[i][1] * x[i + lag][0];
      }
      phi[2 - lag][1][0] = real_sum + x[0][0] * x[lag][0]
                                    + x[0][1] * x[lag][1];
      phi[2 - lag][1][1] = imag_sum + x[0][0] * x[lag][1]
                                    - x[0][1] * x[lag][0];
      if (lag == 1)
      {
         phi[0][0][0] = real_sum + x[38][0] * x[39][0]
                                 + x[38][1] * x[39][1];
         phi[0][0][1] = imag_sum + x[38][0] * x[39][1]
                                 - x[38][1] * x[39][0];
      }
   }
   else
   {
      for (i = 1; i < 38; i++)
         real_sum += x[i][0] * x[i][0] + x[i][1] * x[i][1];
      phi[2][1][0] = real_sum + x[0][0] * x[0][0] + x[0][1] * x[0][1];
      phi[1][0][0] = real_sum + x[38][0] * x[38][0]
                              + x[38][1] * x[38][1];
   }
}

/* per-band second-order LPC by the covariance method (14496-3
 * 4.6.18.6.2); pathological bands collapse to zero coefficients and
 * a joint magnitude clamp guards the known numeric fragility */
static void raac_sbr_invfilter(float alpha0[32][2], float alpha1[32][2],
      float X_low[32][40][2], int k0)
{
   int k;
   for (k = 0; k < k0; k++)
   {
      float phi[3][2][2], dk;
      raac_sbr_autocorr(X_low[k], phi, 0);
      raac_sbr_autocorr(X_low[k], phi, 1);
      raac_sbr_autocorr(X_low[k], phi, 2);
      dk = phi[2][1][0] * phi[1][0][0] -
           (phi[1][1][0] * phi[1][1][0] +
            phi[1][1][1] * phi[1][1][1]) / 1.000001f;
      if (dk == 0.0f)
      {
         alpha1[k][0] = 0.0f;
         alpha1[k][1] = 0.0f;
      }
      else
      {
         float tr = phi[0][0][0] * phi[1][1][0] -
                    phi[0][0][1] * phi[1][1][1] -
                    phi[0][1][0] * phi[1][0][0];
         float ti = phi[0][0][0] * phi[1][1][1] +
                    phi[0][0][1] * phi[1][1][0] -
                    phi[0][1][1] * phi[1][0][0];
         alpha1[k][0] = tr / dk;
         alpha1[k][1] = ti / dk;
      }
      if (phi[1][0][0] == 0.0f)
      {
         alpha0[k][0] = 0.0f;
         alpha0[k][1] = 0.0f;
      }
      else
      {
         float tr = phi[0][0][0] + alpha1[k][0] * phi[1][1][0]
                                 + alpha1[k][1] * phi[1][1][1];
         float ti = phi[0][0][1] + alpha1[k][1] * phi[1][1][0]
                                 - alpha1[k][0] * phi[1][1][1];
         alpha0[k][0] = -tr / phi[1][0][0];
         alpha0[k][1] = -ti / phi[1][0][0];
      }
      if (alpha1[k][0] * alpha1[k][0] + alpha1[k][1] * alpha1[k][1]
            >= 16.0f ||
          alpha0[k][0] * alpha0[k][0] + alpha0[k][1] * alpha0[k][1]
            >= 16.0f)
      {
         alpha0[k][0] = 0.0f;
         alpha0[k][1] = 0.0f;
         alpha1[k][0] = 0.0f;
         alpha1[k][1] = 0.0f;
      }
   }
}

/* chirp factors from the inverse-filtering modes, smoothed against
 * the previous frame (14496-3 4.6.18.6.1) */
static void raac_sbr_chirp(const raac_sbr *s, raac_sbr_ch *c)
{
   static const float bw_tab[4] = { 0.0f, 0.75f, 0.9f, 0.98f };
   int   i;
   for (i = 0; i < s->n_q; i++)
   {
      float nb;
      if (c->bs_invf_mode[0][i] + c->bs_invf_mode[1][i] == 1)
         nb = 0.6f;
      else
         nb = bw_tab[c->bs_invf_mode[0][i]];
      if (nb < c->bw[i])
         nb = 0.75f    * nb + 0.25f    * c->bw[i];
      else
         nb = 0.90625f * nb + 0.09375f * c->bw[i];
      c->bw[i] = nb < 0.015625f ? 0.0f : nb;
   }
}

/* the high-frequency generator (14496-3 4.6.18.6.3): copy patch
 * source bands above the crossover with the chirped second-order
 * inverse filter applied, over the envelope-covered slot range */
static int raac_sbr_hf_gen(const raac_sbr *s, const raac_sbr_ch *c,
      float X_high[64][40][2], float X_low[32][40][2],
      float alpha0[32][2], float alpha1[32][2])
{
   int i, j, x;
   int g = 0;
   int k = s->kx[1];
   memset(X_high, 0, sizeof(float) * 64 * 40 * 2);
   for (j = 0; j < s->num_patches; j++)
      for (x = 0; x < s->patch_num_subbands[j]; x++, k++)
      {
         float     al[4];
         const int p = s->patch_start_subband[j] + x;
         while (g <= s->n_q && k >= s->f_tablenoise[g])
            g++;
         g--;
         if (g < 0 || g >= 5 || p < 0 || p >= 32 || k >= 64)
            return -1;
         al[0] = alpha1[p][0] * c->bw[g] * c->bw[g];
         al[1] = alpha1[p][1] * c->bw[g] * c->bw[g];
         al[2] = alpha0[p][0] * c->bw[g];
         al[3] = alpha0[p][1] * c->bw[g];
         for (i = 2 * c->t_env[0]; i < 2 * c->t_env[c->bs_num_env]; i++)
         {
            const int idx = i + 2;
            X_high[k][idx][0] =
               X_low[p][idx - 2][0] * al[0] -
               X_low[p][idx - 2][1] * al[1] +
               X_low[p][idx - 1][0] * al[2] -
               X_low[p][idx - 1][1] * al[3] +
               X_low[p][idx][0];
            X_high[k][idx][1] =
               X_low[p][idx - 2][1] * al[0] +
               X_low[p][idx - 2][0] * al[1] +
               X_low[p][idx - 1][1] * al[2] +
               X_low[p][idx - 1][0] * al[3] +
               X_low[p][idx][1];
         }
      }
   return 0;
}


/* dequantise envelope and noise scalefactors for both channels of
 * the element, resolving stereo coupling (14496-3 4.6.18.7.1;
 * NOISE_FLOOR_OFFSET is 6) */
static void raac_sbr_dequant(raac_sbr *s, raac_sbr_scratch *sx)
{
   int k, e, ch;
   if (s->is_cpe && s->bs_coupling)
   {
      float alpha      = s->d[0].bs_amp_res ?  1.0f :  0.5f;
      float pan_offset = s->d[0].bs_amp_res ? 12.0f : 24.0f;
      for (e = 1; e <= (int)s->d[0].bs_num_env; e++)
         for (k = 0; k < s->n[s->d[0].bs_freq_res[e]]; k++)
         {
            float t1 = (float)pow(2.0,
                  s->d[0].env_facs_q[e][k] * alpha + 7.0);
            float t2 = (float)pow(2.0,
                  (pan_offset - s->d[1].env_facs_q[e][k]) * alpha);
            float fac = t1 / (1.0f + t2);
            sx->envf[0][e][k] = fac;
            sx->envf[1][e][k] = fac * t2;
         }
      for (e = 1; e <= (int)s->d[0].bs_num_noise; e++)
         for (k = 0; k < s->n_q; k++)
         {
            float t1 = (float)pow(2.0,
                  6.0 - s->d[0].noise_facs_q[e][k] + 1.0);
            float t2 = (float)pow(2.0,
                  12.0 - s->d[1].noise_facs_q[e][k]);
            float fac = t1 / (1.0f + t2);
            sx->noisef[0][e][k] = fac;
            sx->noisef[1][e][k] = fac * t2;
         }
   }
   else
   {
      int nch = s->is_cpe ? 2 : 1;
      for (ch = 0; ch < nch; ch++)
      {
         float alpha = s->d[ch].bs_amp_res ? 1.0f : 0.5f;
         for (e = 1; e <= (int)s->d[ch].bs_num_env; e++)
            for (k = 0; k < s->n[s->d[ch].bs_freq_res[e]]; k++)
               sx->envf[ch][e][k] = (float)pow(2.0,
                     alpha * s->d[ch].env_facs_q[e][k] + 6.0);
         for (e = 1; e <= (int)s->d[ch].bs_num_noise; e++)
            for (k = 0; k < s->n_q; k++)
               sx->noisef[ch][e][k] = (float)pow(2.0,
                     6.0 - s->d[ch].noise_facs_q[e][k]);
      }
   }
}

/* map dequantised factors and sinusoid flags onto the QMF bands per
 * envelope (14496-3 4.6.18.7.2) */
static void raac_sbr_mapping(raac_sbr *s, raac_sbr_ch *c,
      float envf[6][48], float noisef[3][5], raac_sbr_scratch *sx)
{
   int e, i, m;
   memset(c->s_indexmapped[1], 0, 7 * sizeof(c->s_indexmapped[1]));
   for (e = 0; e < (int)c->bs_num_env; e++)
   {
      const int       ilim  = s->n[c->bs_freq_res[e + 1]];
      const uint16_t *table = c->bs_freq_res[e + 1]
            ? s->f_tablehigh : s->f_tablelow;
      int k;
      for (i = 0; i < ilim; i++)
         for (m = table[i]; m < table[i + 1]; m++)
         {
            int mi = m - s->kx[1];
            if (mi >= 0 && mi < 48)
               sx->e_origmapped[e][mi] = envf[e + 1][i];
         }
      k = (c->bs_num_noise > 1) && (c->t_env[e] >= c->t_q[1]);
      for (i = 0; i < s->n_q; i++)
         for (m = s->f_tablenoise[i]; m < s->f_tablenoise[i + 1]; m++)
         {
            int mi = m - s->kx[1];
            if (mi >= 0 && mi < 48)
               sx->q_mapped[e][mi] = noisef[k + 1][i];
         }
      for (i = 0; i < s->n[1]; i++)
         if (c->bs_add_harmonic_flag)
         {
            const int mid =
                  ((s->f_tablehigh[i] + s->f_tablehigh[i + 1]) >> 1)
                  - s->kx[1];
            if (mid >= 0 && mid < 48)
               c->s_indexmapped[e + 1][mid] =
                     c->bs_add_harmonic[i] *
                     (e >= c->e_a[1] ||
                      (c->s_indexmapped[0][mid] == 1));
         }
      for (i = 0; i < ilim; i++)
      {
         int present = 0;
         for (m = table[i]; m < table[i + 1]; m++)
         {
            int mi = m - s->kx[1];
            if (mi >= 0 && mi < 48 && c->s_indexmapped[e + 1][mi])
            {
               present = 1;
               break;
            }
         }
         for (m = table[i]; m < table[i + 1]; m++)
         {
            int mi = m - s->kx[1];
            if (mi >= 0 && mi < 48)
               sx->s_mapped[e][mi] = (uint8_t)present;
         }
      }
   }
   memcpy(c->s_indexmapped[0], c->s_indexmapped[c->bs_num_env],
         sizeof(c->s_indexmapped[0]));
}

/* measured energy of the generated high band per envelope band
 * (14496-3 4.6.18.7.3) */
static void raac_sbr_env_estimate(raac_sbr *s, raac_sbr_ch *c,
      float e_curr[7][48], float X_high[64][40][2])
{
   int e, i, m;
   if (s->bs_interpol_freq)
   {
      for (e = 0; e < (int)c->bs_num_env; e++)
      {
         const float r = 0.5f / (c->t_env[e + 1] - c->t_env[e]);
         const int ilb = c->t_env[e]     * 2 + 2;
         const int iub = c->t_env[e + 1] * 2 + 2;
         for (m = 0; m < s->m[1]; m++)
         {
            float sum = 0.0f;
            for (i = ilb; i < iub; i++)
               sum += X_high[m + s->kx[1]][i][0]
                    * X_high[m + s->kx[1]][i][0]
                    + X_high[m + s->kx[1]][i][1]
                    * X_high[m + s->kx[1]][i][1];
            e_curr[e][m] = sum * r;
         }
      }
   }
   else
   {
      int k, p;
      for (e = 0; e < (int)c->bs_num_env; e++)
      {
         const int env_size = 2 * (c->t_env[e + 1] - c->t_env[e]);
         const int ilb = c->t_env[e]     * 2 + 2;
         const int iub = c->t_env[e + 1] * 2 + 2;
         const uint16_t *table = c->bs_freq_res[e + 1]
               ? s->f_tablehigh : s->f_tablelow;
         for (p = 0; p < s->n[c->bs_freq_res[e + 1]]; p++)
         {
            float sum     = 0.0f;
            const int den = env_size * (table[p + 1] - table[p]);
            for (k = table[p]; k < table[p + 1]; k++)
               for (i = ilb; i < iub; i++)
                  sum += X_high[k][i][0] * X_high[k][i][0]
                       + X_high[k][i][1] * X_high[k][i][1];
            sum /= den;
            for (k = table[p]; k < table[p + 1]; k++)
               e_curr[e][k - s->kx[1]] = sum;
         }
      }
   }
}

/* per-band gains, noise and sinusoid levels with the limiter over
 * the limiter bands (14496-3 4.6.18.7.4-5) */
static void raac_sbr_gain_calc(raac_sbr *s, raac_sbr_ch *c,
      raac_sbr_scratch *sx)
{
   static const float limgain[4] =
      { 0.70795f, 1.0f, 1.41254f, 1e10f };
   const float eps = 1.19209290e-7f;
   int e, k, m;
   for (e = 0; e < (int)c->bs_num_env; e++)
   {
      int delta = !((e == c->e_a[1]) || (e == c->e_a[0]));
      for (k = 0; k < s->n_lim; k++)
      {
         float gain_boost, gain_max;
         float sum0 = 0.0f, sum1 = 0.0f;
         for (m = s->f_tablelim[k] - s->kx[1];
              m < s->f_tablelim[k + 1] - s->kx[1]; m++)
         {
            const float temp = sx->e_origmapped[e][m]
                  / (1.0f + sx->q_mapped[e][m]);
            s->q_m[e][m] = (float)sqrt(temp * sx->q_mapped[e][m]);
            s->s_m[e][m] = (float)sqrt(temp
                  * c->s_indexmapped[e + 1][m]);
            if (!sx->s_mapped[e][m])
               s->gain[e][m] = (float)sqrt(sx->e_origmapped[e][m] /
                     ((1.0f + sx->e_curr[e][m]) *
                      (1.0f + sx->q_mapped[e][m] * delta)));
            else
               s->gain[e][m] = (float)sqrt(sx->e_origmapped[e][m]
                     * sx->q_mapped[e][m] /
                     ((1.0f + sx->e_curr[e][m]) *
                      (1.0f + sx->q_mapped[e][m])));
         }
         for (m = s->f_tablelim[k] - s->kx[1];
              m < s->f_tablelim[k + 1] - s->kx[1]; m++)
         {
            sum0 += sx->e_origmapped[e][m];
            sum1 += sx->e_curr[e][m];
         }
         gain_max = limgain[s->bs_limiter_gains]
               * (float)sqrt((eps + sum0) / (eps + sum1));
         if (gain_max > 100000.0f)
            gain_max = 100000.0f;
         for (m = s->f_tablelim[k] - s->kx[1];
              m < s->f_tablelim[k + 1] - s->kx[1]; m++)
         {
            float q_m_max = s->q_m[e][m] * gain_max / s->gain[e][m];
            if (s->q_m[e][m] > q_m_max)
               s->q_m[e][m] = q_m_max;
            if (s->gain[e][m] > gain_max)
               s->gain[e][m] = gain_max;
         }
         sum0 = sum1 = 0.0f;
         for (m = s->f_tablelim[k] - s->kx[1];
              m < s->f_tablelim[k + 1] - s->kx[1]; m++)
         {
            sum0 += sx->e_origmapped[e][m];
            sum1 += sx->e_curr[e][m] * s->gain[e][m] * s->gain[e][m]
                  + s->s_m[e][m] * s->s_m[e][m]
                  + (delta && !s->s_m[e][m])
                  * s->q_m[e][m] * s->q_m[e][m];
         }
         gain_boost = (float)sqrt((eps + sum0) / (eps + sum1));
         if (gain_boost > 1.584893192f)
            gain_boost = 1.584893192f;
         for (m = s->f_tablelim[k] - s->kx[1];
              m < s->f_tablelim[k + 1] - s->kx[1]; m++)
         {
            s->gain[e][m] *= gain_boost;
            s->q_m[e][m]  *= gain_boost;
            s->s_m[e][m]  *= gain_boost;
         }
      }
   }
}

/* apply gains, inject noise and sinusoids, with the four-slot gain
 * smoothing filter (14496-3 4.6.18.7.5) */
static void raac_sbr_hf_assemble(raac_qmf_ch *q, raac_sbr *s,
      raac_sbr_ch *c, raac_sbr_scratch *sx)
{
   static const float h_smooth[5] =
   {
      0.33333333333333f, 0.30150283239582f, 0.21816949906249f,
      0.11516383427084f, 0.03183050093751f
   };
   static const int phi_r[4] = { -1, 0, 1, 0 };  /* negated: see  */
   static const int phi_i[4] = { 0, -1, 0, 1 };  /* the noise note */
   const int h_SL  = 4 * !s->bs_smoothing_mode;
   const int kx    = s->kx[1];
   const int m_max = s->m[1];
   int e, i, j, m;
   int indexnoise = c->f_indexnoise;
   int indexsine  = c->f_indexsine;
   memcpy(q->Y[0], q->Y[1], sizeof(q->Y[0]));

   if (s->reset)
   {
      for (i = 0; i < h_SL; i++)
      {
         memcpy(q->g_temp[i + 2 * c->t_env[0]], s->gain[0],
               (size_t)m_max * sizeof(float));
         memcpy(q->q_temp[i + 2 * c->t_env[0]], s->q_m[0],
               (size_t)m_max * sizeof(float));
      }
   }
   else if (h_SL)
   {
      memcpy(q->g_temp[2 * c->t_env[0]],
            q->g_temp[2 * c->t_env_num_env_old],
            4 * sizeof(q->g_temp[0]));
      memcpy(q->q_temp[2 * c->t_env[0]],
            q->q_temp[2 * c->t_env_num_env_old],
            4 * sizeof(q->q_temp[0]));
   }

   for (e = 0; e < (int)c->bs_num_env; e++)
      for (i = 2 * c->t_env[e]; i < 2 * c->t_env[e + 1]; i++)
      {
         memcpy(q->g_temp[h_SL + i], s->gain[e],
               (size_t)m_max * sizeof(float));
         memcpy(q->q_temp[h_SL + i], s->q_m[e],
               (size_t)m_max * sizeof(float));
      }

   for (e = 0; e < (int)c->bs_num_env; e++)
      for (i = 2 * c->t_env[e]; i < 2 * c->t_env[e + 1]; i++)
      {
         int phi_sign = 1 - 2 * (kx & 1);
         if (h_SL && e != c->e_a[0] && e != c->e_a[1])
            for (m = 0; m < m_max; m++)
            {
               const int idx1 = i + h_SL;
               float g_filt   = 0.0f;
               for (j = 0; j <= h_SL; j++)
                  g_filt += q->g_temp[idx1 - j][m] * h_smooth[j];
               q->Y[1][i][m + kx][0] =
                     sx->X_high[m + kx][i + 2][0] * g_filt;
               q->Y[1][i][m + kx][1] =
                       sx->X_high[m + kx][i + 2][1] * g_filt;
            }
         else
            for (m = 0; m < m_max; m++)
            {
               const float g_filt = q->g_temp[i + h_SL][m];
               q->Y[1][i][m + kx][0] =
                     sx->X_high[m + kx][i + 2][0] * g_filt;
               q->Y[1][i][m + kx][1] =
                     sx->X_high[m + kx][i + 2][1] * g_filt;
            }
         if (e != c->e_a[0] && e != c->e_a[1])
            for (m = 0; m < m_max; m++)
            {
               indexnoise = (indexnoise + 1) & 0x1ff;
               if (s->s_m[e][m] != 0.0f)
               {
                  q->Y[1][i][m + kx][0] +=
                        s->s_m[e][m] * phi_r[indexsine];
                  q->Y[1][i][m + kx][1] +=
                        s->s_m[e][m] * (phi_i[indexsine] * phi_sign);
               }
               else
               {
                  float q_filt;
                  if (h_SL)
                  {
                     const int idx1 = i + h_SL;
                     q_filt = 0.0f;
                     for (j = 0; j <= h_SL; j++)
                        q_filt += q->q_temp[idx1 - j][m] * h_smooth[j];
                  }
                  else
                     q_filt = q->q_temp[i][m];
/* the additive components enter negated: this decoder's QMF
                   * convention carries a global sign relative to the
                   * reference's internal planes, invisible to energies
                   * and linear filtering but not to injected terms */
                  q->Y[1][i][m + kx][0] -=
                        q_filt * raac_sbr_noise[indexnoise][0];
                  q->Y[1][i][m + kx][1] -=
                        q_filt * raac_sbr_noise[indexnoise][1];
               }
               phi_sign = -phi_sign;
            }
         else
         {
            indexnoise = (indexnoise + m_max) & 0x1ff;
            for (m = 0; m < m_max; m++)
            {
               q->Y[1][i][m + kx][0] +=
                     s->s_m[e][m] * phi_r[indexsine];
               q->Y[1][i][m + kx][1] +=
                     s->s_m[e][m] * (phi_i[indexsine] * phi_sign);
               phi_sign = -phi_sign;
            }
         }
         indexsine = (indexsine + 1) & 3;
      }
   c->f_indexnoise = indexnoise;
   c->f_indexsine  = indexsine;
}

/* assemble the synthesis matrix from the low band and the adjusted
 * high band, splicing at the previous frame's final envelope border
 * (14496-3 4.6.18.7.6) */
static void raac_sbr_x_gen(raac_sbr *s, raac_sbr_ch *c,
      raac_qmf_ch *q, raac_sbr_scratch *sx)
{
   int k, i;
   const int i_f = 32;
   int i_temp = 2 * c->t_env_num_env_old - i_f;
   /* loop ceilings clamped to the physical array extents: a rejected
    * mid-stream reconfiguration can leave the pushed previous-frame
    * crossover values inconsistent for one frame */
   int lo0 = s->kx[0] < 32 ? s->kx[0] : 32;
   int hi0 = s->kx[0] + s->m[0] < 64 ? s->kx[0] + s->m[0] : 64;
   int lo1 = s->kx[1] < 32 ? s->kx[1] : 32;
   int hi1 = s->kx[1] + s->m[1] < 64 ? s->kx[1] + s->m[1] : 64;
   if (i_temp < 0)
      i_temp = 0;
   if (i_temp > 38)
      i_temp = 38;
   memset(sx->X, 0, sizeof(sx->X));
   for (k = 0; k < lo0; k++)
      for (i = 0; i < i_temp; i++)
      {
         sx->X[0][i][k] = sx->X_low[k][i + 2][0];
         sx->X[1][i][k] = sx->X_low[k][i + 2][1];
      }
   for (k = lo0; k < hi0; k++)
      for (i = 0; i < i_temp; i++)
      {
         sx->X[0][i][k] = q->Y[0][i + i_f][k][0];
         sx->X[1][i][k] = q->Y[0][i + i_f][k][1];
      }
   for (k = 0; k < lo1; k++)
      for (i = i_temp; i < 38; i++)
      {
         sx->X[0][i][k] = sx->X_low[k][i + 2][0];
         sx->X[1][i][k] = sx->X_low[k][i + 2][1];
      }
   for (k = lo1 > s->kx[1] ? lo1 : s->kx[1]; k < hi1; k++)
      for (i = i_temp; i < i_f; i++)
      {
         sx->X[0][i][k] = q->Y[1][i][k][0];
         sx->X[1][i][k] = q->Y[1][i][k][1];
      }
}

/* ceil(log2(x + 1)) for the small relative-border counts */
static const uint8_t raac_sbr_ceil_log2[6] = { 0, 1, 2, 2, 3, 3 };

/* sbr_header (14496-3 table 4.56). Sets reset when the spectrum
 * parameters changed and rebuilds the limiter table when only the
 * limiter density did. */
static void raac_sbr_read_header(raac_sbr *s, raac_bits *b)
{
   uint8_t extra_1, extra_2;
   uint8_t o_start = s->bs_start_freq, o_stop = s->bs_stop_freq;
   uint8_t o_xover = s->bs_xover_band, o_scale = s->bs_freq_scale;
   uint8_t o_alter = s->bs_alter_scale, o_noise = s->bs_noise_bands;
   uint8_t o_lim   = s->bs_limiter_bands;

   s->start = 1;
   s->bs_amp_res_hdr = (uint8_t)raac_getbits(b, 1);
   s->bs_start_freq  = (uint8_t)raac_getbits(b, 4);
   s->bs_stop_freq   = (uint8_t)raac_getbits(b, 4);
   s->bs_xover_band  = (uint8_t)raac_getbits(b, 3);
   raac_getbits(b, 2);          /* bs_reserved                       */
   extra_1 = (uint8_t)raac_getbits(b, 1);
   extra_2 = (uint8_t)raac_getbits(b, 1);
   if (extra_1)
   {
      s->bs_freq_scale  = (uint8_t)raac_getbits(b, 2);
      s->bs_alter_scale = (uint8_t)raac_getbits(b, 1);
      s->bs_noise_bands = (uint8_t)raac_getbits(b, 2);
   }
   else
   {
      s->bs_freq_scale  = 2;
      s->bs_alter_scale = 1;
      s->bs_noise_bands = 2;
   }
   if (o_start != s->bs_start_freq || o_stop != s->bs_stop_freq ||
         o_xover != s->bs_xover_band || o_scale != s->bs_freq_scale ||
         o_alter != s->bs_alter_scale || o_noise != s->bs_noise_bands)
      s->reset = 1;
   if (extra_2)
   {
      s->bs_limiter_bands  = (uint8_t)raac_getbits(b, 2);
      s->bs_limiter_gains  = (uint8_t)raac_getbits(b, 2);
      s->bs_interpol_freq  = (uint8_t)raac_getbits(b, 1);
      s->bs_smoothing_mode = (uint8_t)raac_getbits(b, 1);
   }
   else
   {
      s->bs_limiter_bands  = 2;
      s->bs_limiter_gains  = 2;
      s->bs_interpol_freq  = 1;
      s->bs_smoothing_mode = 1;
   }
   if (s->bs_limiter_bands != o_lim && !s->reset)
      raac_sbr_make_f_tablelim(s);
}

/* sbr_grid (14496-3 table 4.62): envelope and noise floor time
 * borders in the four frame classes, and the pointer-derived
 * transient envelope index */
static int raac_sbr_read_grid(raac_sbr *s, raac_bits *b, raac_sbr_ch *c)
{
   int      i;
   int      bs_pointer     = 0;
   int      abs_bord_trail = 16;      /* numTimeSlots at 1024 frames */
   int      num_rel_lead, num_rel_trail;
   unsigned bs_num_env_old = c->bs_num_env;
   int      frame_class, num_env;

   c->bs_freq_res[0]    = c->bs_freq_res[c->bs_num_env];
   c->bs_amp_res        = s->bs_amp_res_hdr;
   c->t_env_num_env_old = c->t_env[c->bs_num_env];

   frame_class = (int)raac_getbits(b, 2);
   switch (frame_class)
   {
      case 0:  /* FIXFIX */
         num_env = 1 << raac_getbits(b, 2);
         if (num_env > 4)
            return -1;
         c->bs_num_env = (unsigned)num_env;
         num_rel_lead  = num_env - 1;
         if (num_env == 1)
            c->bs_amp_res = 0;
         c->t_env[0]       = 0;
         c->t_env[num_env] = abs_bord_trail;
         abs_bord_trail    = (abs_bord_trail + (num_env >> 1)) / num_env;
         for (i = 0; i < num_rel_lead; i++)
            c->t_env[i + 1] = c->t_env[i] + abs_bord_trail;
         c->bs_freq_res[1] = (uint8_t)raac_getbits(b, 1);
         for (i = 1; i < num_env; i++)
            c->bs_freq_res[i + 1] = c->bs_freq_res[1];
         break;
      case 1:  /* FIXVAR */
         abs_bord_trail   += (int)raac_getbits(b, 2);
         num_rel_trail     = (int)raac_getbits(b, 2);
         num_env           = num_rel_trail + 1;
         if (num_env > 5)
            return -1;      /* 1..4 by construction; keeps the array
                             * writes below provably in bounds */
         c->bs_num_env     = (unsigned)num_env;
         c->t_env[0]       = 0;
         c->t_env[num_env] = abs_bord_trail;
         for (i = 0; i < num_rel_trail; i++)
            c->t_env[num_env - 1 - i] =
                  c->t_env[num_env - i] -
                  2 * (int)raac_getbits(b, 2) - 2;
         bs_pointer = (int)raac_getbits(b,
               raac_sbr_ceil_log2[num_env]);
         for (i = 0; i < num_env; i++)
            c->bs_freq_res[num_env - i] =
                  (uint8_t)raac_getbits(b, 1);
         break;
      case 2:  /* VARFIX */
         c->t_env[0]   = (int)raac_getbits(b, 2);
         num_rel_lead  = (int)raac_getbits(b, 2);
         num_env       = num_rel_lead + 1;
         if (num_env > 5)
            return -1;      /* 1..4 by construction; provable bound */
         c->bs_num_env = (unsigned)num_env;
         c->t_env[num_env] = abs_bord_trail;
         for (i = 0; i < num_rel_lead; i++)
            c->t_env[i + 1] = c->t_env[i] +
                  2 * (int)raac_getbits(b, 2) + 2;
         bs_pointer = (int)raac_getbits(b,
               raac_sbr_ceil_log2[num_env]);
         for (i = 0; i < num_env; i++)
            c->bs_freq_res[i + 1] = (uint8_t)raac_getbits(b, 1);
         break;
      default: /* VARVAR */
         c->t_env[0]     = (int)raac_getbits(b, 2);
         abs_bord_trail += (int)raac_getbits(b, 2);
         num_rel_lead    = (int)raac_getbits(b, 2);
         num_rel_trail   = (int)raac_getbits(b, 2);
         num_env         = num_rel_lead + num_rel_trail + 1;
         if (num_env > 5)
            return -1;
         c->bs_num_env = (unsigned)num_env;
         c->t_env[num_env] = abs_bord_trail;
         for (i = 0; i < num_rel_lead; i++)
            c->t_env[i + 1] = c->t_env[i] +
                  2 * (int)raac_getbits(b, 2) + 2;
         for (i = 0; i < num_rel_trail; i++)
            c->t_env[num_env - 1 - i] = c->t_env[num_env - i] -
                  2 * (int)raac_getbits(b, 2) - 2;
         bs_pointer = (int)raac_getbits(b,
               raac_sbr_ceil_log2[num_env]);
         for (i = 0; i < num_env; i++)
            c->bs_freq_res[i + 1] = (uint8_t)raac_getbits(b, 1);
         break;
   }
   c->bs_frame_class = (uint8_t)frame_class;

   if (bs_pointer > (int)c->bs_num_env + 1)
      return -1;
   for (i = 1; i <= (int)c->bs_num_env; i++)
      if (c->t_env[i - 1] >= c->t_env[i])
         return -1;

   c->bs_num_noise = (c->bs_num_env > 1) + 1;
   c->t_q[0] = c->t_env[0];
   c->t_q[c->bs_num_noise] = c->t_env[c->bs_num_env];
   if (c->bs_num_noise > 1)
   {
      int idx;
      if (frame_class == 0)
         idx = (int)(c->bs_num_env >> 1);
      else if (frame_class & 1)          /* FIXVAR or VARVAR         */
      {
         idx = bs_pointer - 1;
         if (idx < 1)
            idx = 1;
         idx = (int)c->bs_num_env - idx;
      }
      else                               /* VARFIX                   */
      {
         if (!bs_pointer)
            idx = 1;
         else if (bs_pointer == 1)
            idx = (int)c->bs_num_env - 1;
         else
            idx = bs_pointer - 1;
      }
      c->t_q[1] = c->t_env[idx];
   }

   c->e_a[0] = -(c->e_a[1] != (int)bs_num_env_old);
   c->e_a[1] = -1;
   if ((frame_class & 1) && bs_pointer)
      c->e_a[1] = (int)c->bs_num_env + 1 - bs_pointer;
   else if (frame_class == 2 && bs_pointer > 1)
      c->e_a[1] = bs_pointer - 1;
   return 0;
}

static void raac_sbr_copy_grid(raac_sbr_ch *dst, const raac_sbr_ch *src)
{
   uint8_t freq_res_prev = dst->bs_freq_res[dst->bs_num_env];
   int     t_env_old     = dst->t_env[dst->bs_num_env];
   int     e_a1          = dst->e_a[1];
   unsigned num_env_old  = dst->bs_num_env;
   memcpy(dst->bs_freq_res, src->bs_freq_res, sizeof(dst->bs_freq_res));
   memcpy(dst->t_env, src->t_env, sizeof(dst->t_env));
   memcpy(dst->t_q, src->t_q, sizeof(dst->t_q));
   dst->bs_num_env      = src->bs_num_env;
   dst->bs_num_noise    = src->bs_num_noise;
   dst->bs_frame_class  = src->bs_frame_class;
   dst->bs_amp_res      = src->bs_amp_res;
   dst->bs_freq_res[0]  = freq_res_prev;
   dst->t_env_num_env_old = t_env_old;
   dst->e_a[0] = -(e_a1 != (int)num_env_old);
   dst->e_a[1] = src->e_a[1];
}

static int raac_sbr_read_dtdf(raac_sbr *s, raac_bits *b, raac_sbr_ch *c)
{
   unsigned i;
   unsigned n_env   = c->bs_num_env;
   unsigned n_noise = c->bs_num_noise;
   (void)s;
   /* the grid parse establishes 1..5 envelopes and 1..2 noise floors;
    * restate the bounds here so the array writes are provably in range
    * even if that invariant ever regresses upstream of this call */
   if (n_env > 5 || n_noise > 2)
      return -1;
   for (i = 0; i < n_env; i++)
      c->bs_df_env[i] = (uint8_t)raac_getbits(b, 1);
   for (i = 0; i < n_noise; i++)
      c->bs_df_noise[i] = (uint8_t)raac_getbits(b, 1);
   return 0;
}

static void raac_sbr_read_invf(raac_sbr *s, raac_bits *b, raac_sbr_ch *c)
{
   int i;
   memcpy(c->bs_invf_mode[1], c->bs_invf_mode[0], 5);
   for (i = 0; i < s->n_q; i++)
      c->bs_invf_mode[0][i] = (uint8_t)raac_getbits(b, 2);
}

/* sbr_envelope (14496-3 4.6.18.3.4): time- or frequency-delta coded
 * envelope scalefactors, huffman book chosen by amplitude resolution
 * and channel coupling, with resolution-crossing index mapping when
 * consecutive envelopes differ in frequency resolution */
static int raac_sbr_read_envelope(raac_t *a, raac_sbr *s, raac_bits *b,
      raac_sbr_ch *c, int ch)
{
   int bits, t_lav, f_lav;
   const raac_sbr_book *t_huff, *f_huff;
   int i, j, k;
   int delta = ((ch == 1 && s->bs_coupling == 1) ? 1 : 0) + 1;
   int odd   = s->n[1] & 1;
   int bi;

   (void)a;
   if (s->bs_coupling && ch)
      bi = c->bs_amp_res ? 6 : 2;
   else
      bi = c->bs_amp_res ? 4 : 0;
   bits   = (s->bs_coupling && ch) ? (c->bs_amp_res ? 5 : 6)
                                   : (c->bs_amp_res ? 6 : 7);
   t_huff = &raac_sbr_books[bi];
   f_huff = &raac_sbr_books[bi + 1];
   t_lav  = raac_sbr_lav[bi];
   f_lav  = raac_sbr_lav[bi + 1];

   for (i = 0; i < (int)c->bs_num_env; i++)
   {
      int nb = s->n[c->bs_freq_res[i + 1]];
      if (c->bs_df_env[i])
      {
         for (j = 0; j < nb; j++)
         {
            int t = raac_sbr_huff(b, t_huff);
            int ref;
            if (t < 0)
               return -1;
            if (c->bs_freq_res[i + 1] == c->bs_freq_res[i])
               ref = c->env_facs_q[i][j];
            else if (c->bs_freq_res[i + 1])
            {
               k   = (j + odd) >> 1;
               ref = c->env_facs_q[i][k];
            }
            else
            {
               k   = j ? 2 * j - odd : 0;
               ref = c->env_facs_q[i][k];
            }
            c->env_facs_q[i + 1][j] = ref + delta * (t - t_lav);
            if ((unsigned)c->env_facs_q[i + 1][j] > 127u)
               return -1;
         }
      }
      else
      {
         c->env_facs_q[i + 1][0] = delta * (int)raac_getbits(b, bits);
         for (j = 1; j < nb; j++)
         {
            int t = raac_sbr_huff(b, f_huff);
            if (t < 0)
               return -1;
            c->env_facs_q[i + 1][j] = c->env_facs_q[i + 1][j - 1] +
                  delta * (t - f_lav);
            if ((unsigned)c->env_facs_q[i + 1][j] > 127u)
               return -1;
         }
      }
   }
   memcpy(c->env_facs_q[0], c->env_facs_q[c->bs_num_env],
         sizeof(c->env_facs_q[0]));
   return 0;
}

static int raac_sbr_read_noise(raac_t *a, raac_sbr *s, raac_bits *b,
      raac_sbr_ch *c, int ch)
{
   const raac_sbr_book *t_huff, *f_huff;
   int t_lav, f_lav;
   int i, j;
   int delta = ((ch == 1 && s->bs_coupling == 1) ? 1 : 0) + 1;

   (void)a;
   if (s->bs_coupling && ch)
   {
      t_huff = &raac_sbr_books[9]; t_lav = raac_sbr_lav[9];
      f_huff = &raac_sbr_books[7]; f_lav = raac_sbr_lav[7];
   }
   else
   {
      t_huff = &raac_sbr_books[8]; t_lav = raac_sbr_lav[8];
      f_huff = &raac_sbr_books[5]; f_lav = raac_sbr_lav[5];
   }
   for (i = 0; i < (int)c->bs_num_noise; i++)
   {
      if (c->bs_df_noise[i])
      {
         for (j = 0; j < s->n_q; j++)
         {
            int t = raac_sbr_huff(b, t_huff);
            if (t < 0)
               return -1;
            c->noise_facs_q[i + 1][j] = c->noise_facs_q[i][j] +
                  delta * (t - t_lav);
            if ((unsigned)c->noise_facs_q[i + 1][j] > 30u)
               return -1;
         }
      }
      else
      {
         c->noise_facs_q[i + 1][0] = delta * (int)raac_getbits(b, 5);
         for (j = 1; j < s->n_q; j++)
         {
            int t = raac_sbr_huff(b, f_huff);
            if (t < 0)
               return -1;
            c->noise_facs_q[i + 1][j] = c->noise_facs_q[i + 1][j - 1] +
                  delta * (t - f_lav);
            if ((unsigned)c->noise_facs_q[i + 1][j] > 30u)
               return -1;
         }
      }
   }
   memcpy(c->noise_facs_q[0], c->noise_facs_q[c->bs_num_noise],
         sizeof(c->noise_facs_q[0]));
   return 0;
}

static int raac_sbr_read_sce(raac_t *a, raac_sbr *s, raac_bits *b)
{
   if (raac_getbits(b, 1))     /* bs_data_extra                     */
      raac_getbits(b, 4);
   if (raac_sbr_read_grid(s, b, &s->d[0]) < 0)
      return -1;
   if (raac_sbr_read_dtdf(s, b, &s->d[0]) < 0)
      return -1;
   raac_sbr_read_invf(s, b, &s->d[0]);
   if (raac_sbr_read_envelope(a, s, b, &s->d[0], 0) < 0)
      return -1;
   if (raac_sbr_read_noise(a, s, b, &s->d[0], 0) < 0)
      return -1;
   if ((s->d[0].bs_add_harmonic_flag = (uint8_t)raac_getbits(b, 1)))
   {
      int i;
      for (i = 0; i < s->n[1]; i++)
         s->d[0].bs_add_harmonic[i] = (uint8_t)raac_getbits(b, 1);
   }
   return 0;
}

static int raac_sbr_read_cpe(raac_t *a, raac_sbr *s, raac_bits *b)
{
   int i;
   if (raac_getbits(b, 1))     /* bs_data_extra                     */
      raac_getbits(b, 8);
   if ((s->bs_coupling = (uint8_t)raac_getbits(b, 1)))
   {
      if (raac_sbr_read_grid(s, b, &s->d[0]) < 0)
         return -1;
      raac_sbr_copy_grid(&s->d[1], &s->d[0]);
      if (raac_sbr_read_dtdf(s, b, &s->d[0]) < 0)
      return -1;
      if (raac_sbr_read_dtdf(s, b, &s->d[1]) < 0)
      return -1;
      raac_sbr_read_invf(s, b, &s->d[0]);
      memcpy(s->d[1].bs_invf_mode[1], s->d[1].bs_invf_mode[0], 5);
      memcpy(s->d[1].bs_invf_mode[0], s->d[0].bs_invf_mode[0], 5);
      if (raac_sbr_read_envelope(a, s, b, &s->d[0], 0) < 0 ||
            raac_sbr_read_noise(a, s, b, &s->d[0], 0) < 0 ||
            raac_sbr_read_envelope(a, s, b, &s->d[1], 1) < 0 ||
            raac_sbr_read_noise(a, s, b, &s->d[1], 1) < 0)
         return -1;
   }
   else
   {
      if (raac_sbr_read_grid(s, b, &s->d[0]) < 0 ||
            raac_sbr_read_grid(s, b, &s->d[1]) < 0)
         return -1;
      if (raac_sbr_read_dtdf(s, b, &s->d[0]) < 0)
      return -1;
      if (raac_sbr_read_dtdf(s, b, &s->d[1]) < 0)
      return -1;
      raac_sbr_read_invf(s, b, &s->d[0]);
      raac_sbr_read_invf(s, b, &s->d[1]);
      if (raac_sbr_read_envelope(a, s, b, &s->d[0], 0) < 0 ||
            raac_sbr_read_envelope(a, s, b, &s->d[1], 1) < 0 ||
            raac_sbr_read_noise(a, s, b, &s->d[0], 0) < 0 ||
            raac_sbr_read_noise(a, s, b, &s->d[1], 1) < 0)
         return -1;
   }
   for (i = 0; i < 2; i++)
      if ((s->d[i].bs_add_harmonic_flag = (uint8_t)raac_getbits(b, 1)))
      {
         int j;
         for (j = 0; j < s->n[1]; j++)
            s->d[i].bs_add_harmonic[j] = (uint8_t)raac_getbits(b, 1);
      }
   return 0;
}

/* sbr_extension_data carried in a FIL element (14496-3 table 4.55).
 * Parses into the element's persistent SBR state; a malformed payload
 * turns SBR off for the element rather than failing the frame, as
 * reference decoders do. Synthesis is not yet wired up: this stage
 * carries the complete bitstream layer and band tables. */
static void raac_sbr_extension(raac_t *a, raac_sbr *s, raac_bits *b,
      int crc, size_t cnt)
{
   size_t limit = b->pos + cnt * 8 - 4;    /* ext type already read  */

   s->reset = 0;
   if (!s->sample_rate)
      s->sample_rate = 2 * a->sample_rate;
   if (crc)
      raac_getbits(b, 10);

   s->kx[0] = s->kx[1];
   s->m[0]  = s->m[1];

   if (raac_getbits(b, 1))     /* bs_header_flag                    */
      raac_sbr_read_header(s, b);
   if (s->reset)
   {
      if (raac_sbr_make_f_master(s) < 0 ||
            raac_sbr_make_f_derived(s) < 0)
      {
         raac_sbr_turnoff(s);
         return;
      }
   }
   if (s->start)
   {
      int ok;
      if (s->is_cpe)
         ok = raac_sbr_read_cpe(a, s, b);
      else
         ok = raac_sbr_read_sce(a, s, b);
      if (ok < 0 || b->err || b->pos > limit)
      {
#ifdef RAAC_SBR_TRACE
         fprintf(stderr, "sbr desync: ok=%d err=%d pos=%u limit=%u\n",
               ok, b->err, (unsigned)b->pos, (unsigned)limit);
#endif
         raac_sbr_turnoff(s);
         return;
      }
#ifdef RAAC_SBR_TRACE
      fprintf(stderr,
            "sbr ok: cpe=%d k0=%d k2=%d kx=%d M=%d nm=%d nq=%d nl=%d "
            "pat=%d env=%u/%u amp=%d cpl=%d slack=%d\n",
            s->is_cpe, s->k0, s->k2, s->kx[1], s->m[1], s->n_master,
            s->n_q, s->n_lim, s->num_patches, s->d[0].bs_num_env,
            s->is_cpe ? s->d[1].bs_num_env : 0, s->bs_amp_res_hdr,
            s->bs_coupling, (int)(limit - b->pos));
#endif
      if (raac_getbits(b, 1))  /* bs_extended_data                  */
      {
         unsigned left = raac_getbits(b, 4);
         if (left == 15)
            left += raac_getbits(b, 8);
         b->pos += (size_t)left * 8;   /* PS and fill: skipped       */
      }
      if (b->err || b->pos > limit)
         raac_sbr_turnoff(s);
   }
}

/* Apply every active CCE at one coupling point to one element, in
 * element instance tag order. */
static void raac_couple_all(raac_t *a, int point, int is_cpe, int tag,
      unsigned ch0, float *pcm0, float *pcm1)
{
   int t, s;
   for (t = 0; t < 16; t++)
      for (s = 0; s < RAAC_MAX_CCE; s++)
         if (a->cce[s].in_use && a->cce[s].tag == t)
            raac_cce_couple(a, &a->cce[s], point, is_cpe, tag,
                  ch0, pcm0, pcm1);
}

/* program_config_element (14496-3 4.4.1.1). Parses one PCE, leaving
 * the reader just past it. Writes the number of channel elements the
 * layout lists (SCE, CPE and LFE each count one) and the channel
 * total (CPEs count two; coupling channels add none) through the
 * optional out pointers. Returns 0, or -1 when the element is
 * truncated. byte_alignment() inside a PCE is
 * relative to the start of the enclosing structure - the ASC or the
 * raw_data_block - and the bit reader's buffer begins there in both
 * callers, so aligning the raw position is correct. */
static int raac_pce(raac_bits *b, int *elems, int *channels)
{
   int nfront, nside, nback, nlfe, nassoc, ncc;
   int i, n_elems, n_ch;
   raac_getbits(b, 4);           /* element_instance_tag       */
   raac_getbits(b, 2);           /* object_type                */
   raac_getbits(b, 4);           /* sampling_frequency_index   */
   nfront = (int)raac_getbits(b, 4);
   nside  = (int)raac_getbits(b, 4);
   nback  = (int)raac_getbits(b, 4);
   nlfe   = (int)raac_getbits(b, 2);
   nassoc = (int)raac_getbits(b, 3);
   ncc    = (int)raac_getbits(b, 4);
   if (raac_getbits(b, 1))       /* mono_mixdown_present       */
      raac_getbits(b, 4);
   if (raac_getbits(b, 1))       /* stereo_mixdown_present     */
      raac_getbits(b, 4);
   if (raac_getbits(b, 1))       /* matrix_mixdown_idx_present */
      raac_getbits(b, 3);        /* idx + pseudo_surround      */
   n_elems = nfront + nside + nback + nlfe;
   n_ch    = nlfe;
   for (i = 0; i < nfront + nside + nback; i++)
   {
      /* element_is_cpe + element_tag_select */
      n_ch += raac_getbits(b, 1) ? 2 : 1;
      raac_getbits(b, 4);
   }
   for (i = 0; i < nlfe; i++)
      raac_getbits(b, 4);        /* lfe_element_tag_select     */
   for (i = 0; i < nassoc; i++)
      raac_getbits(b, 4);        /* assoc_data_element_tag     */
   for (i = 0; i < ncc; i++)
      raac_getbits(b, 5);        /* cc_element_is_ind_sw + tag */
   b->pos = (b->pos + 7) & ~(size_t)7;   /* byte_alignment()   */
   {
      /* comment_field_bytes; read it into a local first: += would
       * leave the order of the old-pos read against the call's pos
       * advance indeterminately sequenced */
      size_t cf = (size_t)raac_getbits(b, 8);
      b->pos += cf * 8;
   }
   if (b->pos > b->size * 8 || b->err)
      return -1;
   if (elems)
      *elems    = n_elems;
   if (channels)
      *channels = n_ch;
   return 0;
}

/* channels carried by channelConfiguration 1..7 (14496-3 Table 1.19):
 * 1 mono, 2 stereo, 3 SCE+CPE, 4 SCE+CPE+SCE, 5 SCE+2CPE,
 * 6 adds an LFE (5.1), 7 is SCE+3CPE+LFE (7.1, eight channels) */
static const uint8_t raac_chcfg_channels[8] = { 0, 1, 2, 3, 4, 5, 6, 8 };

/* Map an explicitly coded sampling frequency onto the sampling
 * frequency index whose scalefactor band tables apply, per the
 * ranges of ISO/IEC 14496-3 Table 4.55. */
static int raac_freq_to_sfi(unsigned freq)
{
   static const unsigned lo[11] = {
      92017, 75132, 55426, 46009, 37566, 27713,
      23004, 18783, 13856, 11502, 9391
   };
   int i;
   for (i = 0; i < 11; i++)
      if (freq >= lo[i])
         return i;
   return 11;
}

/* ===== public API ===== */

raac_t *raac_open(const uint8_t *asc, size_t asc_size)
{
   raac_t   *a;
   raac_bits b;
   unsigned  aot, sfi, chcfg, freq, channels, frame_960;
   unsigned  sbr_explicit, ext_freq;
   int       i;

   if (!asc || asc_size < 2)
      return NULL;
   raac_bits_init(&b, asc, asc_size);
   aot = raac_getbits(&b, 5);
   if (aot == 31)
      aot = 32 + raac_getbits(&b, 6);
   sfi  = raac_getbits(&b, 4);
   freq = 0;
   if (sfi == 15)
      freq = raac_getbits(&b, 24); /* explicit samplingFrequency */
   chcfg = raac_getbits(&b, 4);
   sbr_explicit = 0;
   ext_freq     = 0;
   if (aot == 5 || aot == 29)
   {
      /* hierarchical HE-AAC signaling: the first rate is the core
       * codec's, the extension rate is the SBR output. An AOT-29
       * (HE-AAC v2) stream opens as v1: the parametric-stereo
       * payload is skipped and the SBR mono output is decoded. */
      unsigned ext_sfi = raac_getbits(&b, 4);
      if (ext_sfi == 15)
         ext_freq = raac_getbits(&b, 24);
      else if (ext_sfi > 12)
         return NULL;
      else
         ext_freq = raac_sample_rates[ext_sfi];
      sbr_explicit = 1;
      aot = raac_getbits(&b, 5);
      if (aot == 31)
         aot = 32 + raac_getbits(&b, 6);
   }
   if (aot != 2) /* AAC-LC core only */
      return NULL;
   if (sfi == 15)
   {
      if (!freq)
         return NULL;
      sfi = (unsigned)raac_freq_to_sfi(freq);
   }
   else if (sfi > 12)
      return NULL;
   else
      freq = raac_sample_rates[sfi];
   if (chcfg > 7)
      return NULL;
   frame_960 = raac_getbits(&b, 1); /* frameLengthFlag: 960 frames */
   if (raac_getbits(&b, 1)) /* dependsOnCoreCoder */
      return NULL;
   if (raac_getbits(&b, 1)) /* extensionFlag */
      return NULL;
   if (chcfg == 0)
   {
      /* the channel layout is deferred to a PCE embedded in the
       * GASpecificConfig; the frame parser assigns elements to output
       * channels sequentially, so any coupling-free layout that fits
       * is decodable */
      int elems = 0, nch = 0;
      if (raac_pce(&b, &elems, &nch) < 0)
         return NULL;
      if (nch < 1 || nch > RAAC_MAX_CH)
         return NULL;
      channels = (unsigned)nch;
   }
   else
      channels = raac_chcfg_channels[chcfg];
   if (b.err)
      return NULL;

   /* backward-compatible signaling: a syncExtension after the
    * GASpecificConfig carries sbrPresentFlag for players that parse
    * it; older decoders stop reading at the config end */
   if (!sbr_explicit && b.size * 8 - b.pos >= 16)
   {
      if (raac_getbits(&b, 11) == 0x2b7)
      {
         unsigned ext_aot = raac_getbits(&b, 5);
         if (ext_aot == 31)
            ext_aot = 32 + raac_getbits(&b, 6);
         if (ext_aot == 5 && raac_getbits(&b, 1))
         {
            unsigned ext_sfi = raac_getbits(&b, 4);
            if (ext_sfi == 15)
               ext_freq = raac_getbits(&b, 24);
            else if (ext_sfi <= 12)
               ext_freq = raac_sample_rates[ext_sfi];
            if (!b.err && ext_freq)
               sbr_explicit = 1;
         }
      }
      /* trailing garbage after the config is not an open failure */
      b.err = 0;
   }

   if (!(a = (raac_t*)calloc(1, sizeof(*a))))
      return NULL;
   a->sfi         = (int)sfi;
   a->sample_rate = freq;
   a->channels    = channels;
   a->frame_len   = frame_960 ? 960 : 1024;
   a->noise_state = 0x1f2e3d4cu;

   /* output-rate contract, fixed for the life of the instance:
    * - explicit SBR signaling at twice the core rate turns the
    *   doubled-rate path on (960-frame SBR is not a thing; an
    *   explicitly signaled 960 stream is malformed)
    * - explicit signaling at the core rate is downsampled SBR,
    *   which is not synthesised: the stream decodes as plain LC
    * - without signaling, cores at 16-24 kHz are upsampled from
    *   the first frame, so streams revealing SBR data mid-stream
    *   never switch rates; genuinely plain LC in that window costs
    *   one QMF pair per frame and reports the doubled rate */
   a->sbr_mode = 0;
   a->out_rate = freq;
   if (sbr_explicit)
   {
      if (frame_960 || (ext_freq != freq && ext_freq != freq * 2))
      {
         free(a);
         return NULL;
      }
      if (ext_freq == freq * 2)
         a->sbr_mode = 1;
   }
   else if (!frame_960 && freq >= 16000 && freq <= 24000)
      a->sbr_mode = 1;
   if (a->sbr_mode)
   {
      a->out_rate = freq * 2;
      a->qmf = (raac_qmf_ch*)calloc(channels, sizeof(raac_qmf_ch));
      a->sbx = (raac_sbr_scratch*)calloc(1, sizeof(raac_sbr_scratch));
      if (!a->qmf || !a->sbx)
      {
         free(a->qmf);
         free(a->sbx);
         free(a);
         return NULL;
      }
   }

   for (i = 0; i < 11; i++)
      raac_huff_build(&a->spec[i], a->huff_lut + i * RAAC_HUFF_SPEC_CAP,
            RAAC_HUFF_SPEC_CAP, raac_hcb_code[i], NULL, raac_hcb_bits[i],
            raac_hcb_size[i], i + 1);
   raac_huff_build(&a->sfh, a->huff_lut + 11 * RAAC_HUFF_SPEC_CAP,
         RAAC_HUFF_SF_CAP, NULL, raac_sf_code, raac_sf_bits, 121, 0);
   raac_fft_tables(a);
   for (i = 0; i < 8192; i++)
      a->iq43[i] = (float)pow((double)i, 4.0 / 3.0);
   for (i = 0; i <= 16; i++)
   {
      a->iq43s[16 + i] =  a->iq43[i];
      a->iq43s[16 - i] = -a->iq43[i];
   }
   for (i = 0; i < 256; i++)
      a->sf_gain[i] = (float)pow(2.0, 0.25 * (i - 100));

   {
      int nl = (int)a->frame_len * 2;   /* 2048 or 1920 */
      int ns = nl / 8;                  /*  256 or  240 */
      raac_kbd_window(a->kbd_long, nl, 4.0);
      raac_kbd_window(a->kbd_short, ns, 6.0);
      raac_sine_window(a->sine_long, nl);
      raac_sine_window(a->sine_short, ns);
      raac_make_twiddles(a->tw512_re, a->tw512_im, nl / 4, nl);
      raac_make_twiddles(a->tw64_re, a->tw64_im, ns / 4, ns);
      raac_make_twiddles(a->tw32_re, a->tw32_im, 32, 128);
      if (a->frame_len == 960)
      {
         int k;
         for (k = 0; k < 480; k++)
         {
            double ang    = -2.0 * M_PI * k / 480.0;
            a->w480_re[k] = (float)cos(ang);
            a->w480_im[k] = (float)sin(ang);
         }
         for (k = 0; k < 60; k++)
         {
            double ang   = -2.0 * M_PI * k / 60.0;
            a->w60_re[k] = (float)cos(ang);
            a->w60_im[k] = (float)sin(ang);
         }
      }
   }
   return a;
}

unsigned raac_channels(const raac_t *a)    { return a ? a->channels : 0; }
unsigned raac_frame_len(const raac_t *a)
{
   return a ? (a->frame_len << (a->sbr_mode ? 1 : 0)) : 0;
}
unsigned raac_sample_rate(const raac_t *a) { return a ? a->out_rate : 0; }
size_t raac_last_size(const raac_t *a) { return a ? a->used : 0; }

/* Shared worker: parse and synthesise one access unit into per-channel
 * float PCM in full-scale (+-32768) units. Both public entry points
 * convert from here at the edge, so the pipeline stays float
 * throughout with no intermediate quantisation. */
static int raac_decode_frame(raac_t *a, const uint8_t *pkt, size_t size,
      float pcm[RAAC_MAX_CH][RAAC_FRAME])
{
   raac_bits b;
   int       got[RAAC_MAX_CH];
   raac_elem etab[RAAC_MAX_CH];
   int       n_elems = 0;
   int       done = 0;
   unsigned  ch;
   unsigned  next = 0;
   int       s, t, e;

   if (!a)
      return -1;
   a->used = 0;
   if (!pkt || !size)
      return -1;
   raac_bits_init(&b, pkt, size);
   memset(got, 0, sizeof(got));
   memset(a->sbr_map, -1, sizeof(a->sbr_map));
   for (s = 0; s < RAAC_MAX_CCE; s++)
      a->cce[s].present = 0;

   while (!done)
   {
      unsigned id = raac_getbits(&b, 3);
      if (b.err)
         return -1;
      switch (id)
      {
         case 0:  /* SCE */
         case 3:  /* LFE (same individual_channel_stream)             */
         {
            /* channel elements claim output channels in transmission
             * order, which for the standard configurations is the ISO
             * order (e.g. 5.1: SCE C, CPE L/R, CPE Ls/Rs, LFE) */
            unsigned tag = raac_getbits(&b, 4);
            if (next + 1 > a->channels)
               return -1;
            if (raac_decode_sce(a, &b, &a->ch[next]) < 0)
               return -1;
            etab[n_elems].kind = (uint8_t)id;
            etab[n_elems].tag  = (uint8_t)tag;
            etab[n_elems].ch   = (uint8_t)next;
            n_elems++;
            got[next] = 1;
            next++;
            break;
         }
         case 1:  /* CPE */
         {
            unsigned tag = raac_getbits(&b, 4);
            if (next + 2 > a->channels)
               return -1;
            if (raac_decode_cpe(a, &b, &a->ch[next], &a->ch[next + 1]) < 0)
               return -1;
            etab[n_elems].kind = 1;
            etab[n_elems].tag  = (uint8_t)tag;
            etab[n_elems].ch   = (uint8_t)next;
            n_elems++;
            got[next] = got[next + 1] = 1;
            next += 2;
            break;
         }
         case 2:  /* CCE */
         {
            unsigned  tag = raac_getbits(&b, 4);
            raac_cce *cc  = NULL;
            for (s = 0; s < RAAC_MAX_CCE; s++)
               if (a->cce[s].in_use && a->cce[s].tag == (int)tag)
               {
                  cc = &a->cce[s];
                  break;
               }
            if (!cc)
               for (s = 0; s < RAAC_MAX_CCE; s++)
                  if (!a->cce[s].in_use)
                  {
                     cc = &a->cce[s];
                     memset(cc, 0, sizeof(*cc));
                     cc->in_use = 1;
                     cc->tag    = (int)tag;
                     break;
                  }
            if (!cc || cc->present) /* out of slots, or a duplicate  */
               return -1;
            if (raac_decode_cce(a, &b, cc) < 0)
               return -1;
            cc->present = 1;
            break;
         }
         case 4:  /* DSE */
         {
            unsigned cnt;
            unsigned align;
            raac_getbits(&b, 4);          /* element_instance_tag  */
            align = raac_getbits(&b, 1);  /* data_byte_align_flag  */
            cnt   = raac_getbits(&b, 8);
            if (cnt == 255)
               cnt += raac_getbits(&b, 8);
            /* byte_alignment() comes after the count fields and only
             * when the flag asks for it (14496-3 data_stream_element);
             * it is relative to the start of the raw_data_block, which
             * is where this bit reader's buffer begins */
            if (align)
               b.pos = (b.pos + 7) & ~(size_t)7;
            b.pos += (size_t)cnt * 8;
            break;
         }
         case 5:  /* PCE: parse to keep bit position, contents unused */
            if (raac_pce(&b, NULL, NULL) < 0)
               return -1;
            break;
         case 6:  /* FIL: SBR extension payloads route to the SBR
                   * state of the element they follow; anything else
                   * (fill bytes, DRC) is skipped                     */
         {
            unsigned cnt = raac_getbits(&b, 4);
            size_t   payload;
            if (cnt == 15)
               cnt += raac_getbits(&b, 8) - 1;
            payload = b.pos;
            if (cnt > 0 && a->frame_len == 1024 && n_elems > 0 &&
                  etab[n_elems - 1].kind <= 1)
            {
               unsigned ext = raac_getbits(&b, 4);
               if (ext == 13 || ext == 14)   /* EXT_SBR_DATA[_CRC]   */
               {
                  raac_sbr *sb = NULL;
                  int       is_cpe = (etab[n_elems - 1].kind == 1);
                  int       tag    = (int)etab[n_elems - 1].tag;
                  for (s = 0; s < RAAC_MAX_SBR; s++)
                     if (a->sbr[s].in_use &&
                           a->sbr[s].is_cpe == is_cpe &&
                           a->sbr[s].tag == tag)
                     {
                        sb = &a->sbr[s];
                        break;
                     }
                  if (!sb)
                     for (s = 0; s < RAAC_MAX_SBR; s++)
                        if (!a->sbr[s].in_use)
                        {
                           sb = &a->sbr[s];
                           memset(sb, 0, sizeof(*sb));
                           sb->in_use = 1;
                           sb->is_cpe = is_cpe;
                           sb->tag    = tag;
                           break;
                        }
                  if (sb)
                  {
                     unsigned c0 = etab[n_elems - 1].ch;
                     raac_sbr_extension(a, sb, &b, ext == 14, cnt);
                     if (sb->start && c0 < RAAC_MAX_CH)
                     {
                        a->sbr_map[c0]    = (int8_t)(sb - a->sbr);
                        a->sbr_map_ch[c0] = 0;
                        if (is_cpe && c0 + 1 < RAAC_MAX_CH)
                        {
                           a->sbr_map[c0 + 1]    = (int8_t)(sb - a->sbr);
                           a->sbr_map_ch[c0 + 1] = 1;
                        }
                     }
                  }
               }
            }
            b.pos = payload + (size_t)cnt * 8;
            break;
         }
         case 7:  /* END */
            done = 1;
            break;
         default:
            return -1;
      }
      if (b.pos > b.size * 8)
         return -1;
   }
   a->used = (b.pos + 7) >> 3;

   /* the coupling channels first: their own TNS, and the synthesis
    * feeding time-domain coupling, so targets can consume them */
   for (t = 0; t < 16; t++)
      for (s = 0; s < RAAC_MAX_CCE; s++)
      {
         raac_cce *cc = &a->cce[s];
         if (cc->in_use && cc->tag == t && cc->present)
         {
            raac_tns_apply(a, &cc->ch);
            if (cc->point == 3)
               raac_filterbank(a, &cc->ch, cc->time);
         }
      }

   /* per element: spectral coupling on either side of the target's
    * TNS, filterbank, then time-domain coupling on the output. LFE
    * elements are not coupling targets. */
   for (e = 0; e < n_elems; e++)
   {
      unsigned c0     = etab[e].ch;
      int      is_cpe = (etab[e].kind == 1);
      int      targetable = (etab[e].kind <= 1);
      if (targetable)
         raac_couple_all(a, 0, is_cpe, etab[e].tag, c0, NULL, NULL);
      raac_tns_apply(a, &a->ch[c0]);
      if (is_cpe)
         raac_tns_apply(a, &a->ch[c0 + 1]);
      if (targetable)
         raac_couple_all(a, 1, is_cpe, etab[e].tag, c0, NULL, NULL);
      raac_filterbank(a, &a->ch[c0], pcm[c0]);
      if (is_cpe)
         raac_filterbank(a, &a->ch[c0 + 1], pcm[c0 + 1]);
      if (targetable)
         raac_couple_all(a, 3, is_cpe, etab[e].tag, c0,
               pcm[c0], is_cpe ? pcm[c0 + 1] : NULL);
   }

   /* channels no element claimed keep decaying through the overlap */
   for (ch = 0; ch < a->channels; ch++)
      if (!got[ch])
      {
         memset(a->ch[ch].coef, 0, sizeof(a->ch[ch].coef));
         raac_filterbank(a, &a->ch[ch], pcm[ch]);
      }
   return (int)a->frame_len;
}

/* the doubled-rate output path. Each channel's core frame runs
 * through the analysis bank into the buffered QMF matrix; when SBR
 * data covers the channel the high-frequency generator runs over it
 * (its output is not yet mixed into the synthesis: the envelope
 * adjustment chain lands next, and until then the upper bands stay
 * empty). Output timing already follows the full SBR chain: each
 * output slot i synthesises X_low slot i+2 against the eight-slot
 * history, a six-slot latency, so frames decoded before and after
 * SBR data first appears in a stream splice without a seam. */
static int raac_upsample_frame(raac_t *a)
{
   unsigned ch;
   int      s, k, full;
   for (ch = 0; ch < a->channels; ch++)
   {
      raac_qmf_ch *q = &a->qmf[ch];
      float (*X_low)[40][2] = a->sbx->X_low;
      memcpy(q->W[0], q->W[1], sizeof(q->W[0]));
      for (s = 0; s < 32; s++)
      {
         float Wr[32], Wi[32];
         raac_qmf_analysis_slot(a, q, a->pcm[ch] + s * 32, Wr, Wi);
         for (k = 0; k < 32; k++)
         {
            q->W[1][s][k][0] = Wr[k];
            q->W[1][s][k][1] = Wi[k];
         }
      }
      raac_sbr_lf_gen(q, X_low, 32, 32);
      full = 0;
      if (a->sbr_map[ch] >= 0)
      {
         raac_sbr    *sb = &a->sbr[a->sbr_map[ch]];
         raac_sbr_ch *c  = &sb->d[a->sbr_map_ch[ch]];
         if (sb->start && c->bs_num_env)
         {
            if (a->sbr_map_ch[ch] == 0)
               raac_sbr_dequant(sb, a->sbx);
            raac_sbr_invfilter(a->sbx->alpha0, a->sbx->alpha1,
                  X_low, sb->k0);
            raac_sbr_chirp(sb, c);
            if (raac_sbr_hf_gen(sb, c, a->sbx->X_high, X_low,
                  a->sbx->alpha0, a->sbx->alpha1) == 0)
            {
               raac_sbr_mapping(sb, c,
                     a->sbx->envf[a->sbr_map_ch[ch]],
                     a->sbx->noisef[a->sbr_map_ch[ch]], a->sbx);
               raac_sbr_env_estimate(sb, c, a->sbx->e_curr,
                     a->sbx->X_high);
               raac_sbr_gain_calc(sb, c, a->sbx);
               raac_sbr_hf_assemble(q, sb, c, a->sbx);
               raac_sbr_x_gen(sb, c, q, a->sbx);
               full = 1;
            }
         }
      }
      for (s = 0; s < 32; s++)
      {
         float Xr[64], Xi[64];
         if (full)
            for (k = 0; k < 64; k++)
            {
               Xr[k] = a->sbx->X[0][s][k];
               Xi[k] = a->sbx->X[1][s][k];
            }
         else
            for (k = 0; k < 32; k++)
            {
               Xr[k]      = X_low[k][s + 2][0];
               Xi[k]      = X_low[k][s + 2][1];
               Xr[32 + k] = 0.0f;
               Xi[32 + k] = 0.0f;
            }
         raac_qmf_synthesis_slot(a, q, Xr, Xi, q->out + s * 64);
      }
   }
   return (int)(a->frame_len * 2);
}

int raac_decode_s16(raac_t *a, const uint8_t *pkt, size_t size,
      int16_t *out)
{
   int      ret;
   int      i;
   unsigned ch;
   if (!a)
      return -1;
   if ((ret = raac_decode_frame(a, pkt, size, a->pcm)) < 0)
      return ret;
   if (a->sbr_mode)
      ret = raac_upsample_frame(a);
#ifdef RAAC_HAVE_S16
   if (a->channels == 2 && !(ret & 7))
   {
      const float *l = a->sbr_mode ? a->qmf[0].out : a->pcm[0];
      const float *r = a->sbr_mode ? a->qmf[1].out : a->pcm[1];
      for (i = 0; i < ret; i += 4)
      {
         raac_v x = RAAC_LD(l + i), y = RAAC_LD(r + i);
         raac_v_st_s16x8(out + 2 * i, RAAC_ZIPLO(x, y), RAAC_ZIPHI(x, y));
      }
      return ret;
   }
   if (a->channels == 1 && !(ret & 7))
   {
      const float *p = a->sbr_mode ? a->qmf[0].out : a->pcm[0];
      for (i = 0; i < ret; i += 8)
         raac_v_st_s16x8(out + i, RAAC_LD(p + i), RAAC_LD(p + i + 4));
      return ret;
   }
#endif
   for (ch = 0; ch < a->channels; ch++)
   {
      const float *p = a->sbr_mode ? a->qmf[ch].out : a->pcm[ch];
      int16_t     *o = out + ch;
      for (i = 0; i < ret; i++, o += a->channels)
      {
         /* one rounding, clamped in the float domain: casting an
          * out-of-range or non-finite float to int is undefined, and
          * hostile TNS filters can push the synthesis arbitrarily
          * high, so saturate before the cast (NaN pins to zero). */
         float v = p[i];
         if (!(v > -1e9f && v < 1e9f))
            v = 0.0f;
         v += (v >= 0.0f) ? 0.5f : -0.5f;
         if (v >  32767.0f) v =  32767.0f;
         if (v < -32768.0f) v = -32768.0f;
         *o = (int16_t)(int)v;
      }
   }
   return ret;
}

int raac_decode_f32(raac_t *a, const uint8_t *pkt, size_t size,
      float *out)
{
   int      ret;
   int      i;
   unsigned ch;
   if (!a)
      return -1;
   if ((ret = raac_decode_frame(a, pkt, size, a->pcm)) < 0)
      return ret;
   if (a->sbr_mode)
      ret = raac_upsample_frame(a);
   if (a->channels == 2 && !(ret & 3))
   {
      const float *l = a->sbr_mode ? a->qmf[0].out : a->pcm[0];
      const float *r = a->sbr_mode ? a->qmf[1].out : a->pcm[1];
      raac_v       k = RAAC_SET1(1.0f / 32768.0f);
      for (i = 0; i < ret; i += 4)
      {
         raac_v x = RAAC_MUL(RAAC_LD(l + i), k);
         raac_v y = RAAC_MUL(RAAC_LD(r + i), k);
         RAAC_ST(out + 2 * i,     RAAC_ZIPLO(x, y));
         RAAC_ST(out + 2 * i + 4, RAAC_ZIPHI(x, y));
      }
      return ret;
   }
   for (ch = 0; ch < a->channels; ch++)
   {
      const float *p = a->sbr_mode ? a->qmf[ch].out : a->pcm[ch];
      for (i = 0; i < ret; i++)
         out[i * a->channels + ch] = p[i] * (1.0f / 32768.0f);
   }
   return ret;
}

void raac_reset(raac_t *a)
{
   unsigned ch;
   if (!a)
      return;
   for (ch = 0; ch < RAAC_MAX_CH; ch++)
   {
      memset(a->ch[ch].overlap, 0, sizeof(a->ch[ch].overlap));
      memset(a->ch[ch].coef,    0, sizeof(a->ch[ch].coef));
      a->ch[ch].prev_window_shape = 0;
   }
   memset(a->cce, 0, sizeof(a->cce));
   memset(a->sbr, 0, sizeof(a->sbr));
   if (a->qmf)
      memset(a->qmf, 0, sizeof(raac_qmf_ch) * a->channels);
   /* reseed the PNS generator so a rewound stream decodes exactly as a
    * fresh one */
   a->noise_state = 0x1f2e3d4cu;
}

void raac_close(raac_t *a)
{
   free(a->qmf);
   free(a->sbx);
   free(a);
}

#ifdef RAAC_QMF_TEST
/* test-build-only exports binding the unit tests to the shipped QMF */
void raac_test_qmf_ana(raac_t *a, void *c, const float *in32,
      float Wr[32], float Wi[32])
{
   raac_qmf_analysis_slot(a, (raac_qmf_ch*)c, in32, Wr, Wi);
}
void raac_test_qmf_syn(raac_t *a, void *c, const float Xr[64],
      const float Xi[64], float out64[64])
{
   raac_qmf_synthesis_slot(a, (raac_qmf_ch*)c, Xr, Xi, out64);
}
void raac_test_half(raac_t *a, const float in[64], float out[64], float s)
{
   raac_imdct_half128(a, in, out, s);
}
unsigned raac_test_qmf_ch_size(void) { return sizeof(raac_qmf_ch); }
int raac_test_sbr_huff(const uint8_t *buf, unsigned nbits, int book)
{
   raac_bits b;
   raac_bits_init(&b, buf, (nbits + 7) / 8);
   {
      int v = raac_sbr_huff(&b, &raac_sbr_books[book]);
      if (b.err || b.pos != nbits)
         return -1000;
      return v;
   }
}
void raac_test_invfilter(float a0[32][2], float a1[32][2],
      float X_low[32][40][2], int k0)
{
   raac_sbr_invfilter(a0, a1, X_low, k0);
}
#endif
