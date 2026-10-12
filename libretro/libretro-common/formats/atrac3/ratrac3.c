/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (ratrac3.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* The signal path, unit by unit:
 *
 *  - a sound unit codes up to four bands of 256 MDCT coefficients, each
 *    band one quarter of the spectrum as the QMF tree splits it, the odd
 *    bands frequency-inverted;
 *  - the coefficients come as quantised values in 32 subbands of uneven
 *    width, plus "tonal components": short runs added at any position;
 *  - each band goes through a 512-point IMDCT with a window built so the
 *    halves overlap-add back to the signal; gain points scale it, the
 *    current frame's first level on its own first half and the previous
 *    frame's gain curve on what leaves the overlap;
 *  - the four bands go back together through three two-band QMF stages.
 *
 * All of it was established by feeding a reference decoder crafted
 * streams and reading off its output; the comments note the places where
 * that output differs from what the format notes would suggest. That
 * decoder's output is the encoder's input with its polarity inverted;
 * this one keeps the input's polarity, as PPSSPP's decoder does. */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <retro_inline.h>
#include <formats/ratrac3.h>

/* Four-float vectors: SSE2 or NEON where the target has them (chosen at
 * compile time; every configuration that defines these macros may use
 * the instructions anywhere), plain C otherwise or with RATRAC3_NO_SIMD. The synthesis works
 * four at a time throughout: the IMDCT does a channel's four bands at
 * once, one band a lane, and the QMF does four output pairs at once. */
#if defined(RATRAC3_NO_SIMD)
#define RATRAC3_SCALAR 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
typedef __m128 r3v;
#define R3_LD(p)          _mm_loadu_ps(p)
#define R3_ST(p, v)       _mm_storeu_ps((p), (v))
#define R3_ADD(a, b)      _mm_add_ps((a), (b))
#define R3_SUB(a, b)      _mm_sub_ps((a), (b))
#define R3_MUL(a, b)      _mm_mul_ps((a), (b))
#define R3_SET1(x)        _mm_set1_ps(x)
#define R3_SET4(a,b,c,e)  _mm_setr_ps((a), (b), (c), (e))
#define R3_REV(v)         _mm_shuffle_ps((v), (v), _MM_SHUFFLE(0, 1, 2, 3))
#define R3_ZIPLO(a, b)    _mm_unpacklo_ps((a), (b))
#define R3_ZIPHI(a, b)    _mm_unpackhi_ps((a), (b))
#define R3_TRANSPOSE(a, b, c, e) _MM_TRANSPOSE4_PS(a, b, c, e)
/* Eight samples to s16, rounded to nearest and saturated */
static INLINE void r3_st_s16x8(int16_t *dst, r3v a, r3v b)
{
   const __m128 lo = _mm_set1_ps(-32768.0f), hi = _mm_set1_ps(32767.0f);
   a = _mm_min_ps(_mm_max_ps(a, lo), hi);
   b = _mm_min_ps(_mm_max_ps(b, lo), hi);
   _mm_storeu_si128((__m128i*)dst,
         _mm_packs_epi32(_mm_cvtps_epi32(a), _mm_cvtps_epi32(b)));
}
#define R3_HAVE_S16 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
typedef float32x4_t r3v;
#define R3_LD(p)          vld1q_f32(p)
#define R3_ST(p, v)       vst1q_f32((p), (v))
#define R3_ADD(a, b)      vaddq_f32((a), (b))
#define R3_SUB(a, b)      vsubq_f32((a), (b))
#define R3_MUL(a, b)      vmulq_f32((a), (b))
#define R3_SET1(x)        vdupq_n_f32(x)
static INLINE r3v r3_set4(float a, float b, float c, float e)
{
   float t[4];
   t[0] = a; t[1] = b; t[2] = c; t[3] = e;
   return vld1q_f32(t);
}
#define R3_SET4(a,b,c,e)  r3_set4((a), (b), (c), (e))
static INLINE r3v r3_rev(r3v v)
{
   v = vrev64q_f32(v);
   return vcombine_f32(vget_high_f32(v), vget_low_f32(v));
}
#define R3_REV(v)         r3_rev(v)
static INLINE r3v r3_ziplo(r3v a, r3v b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[0];
}
static INLINE r3v r3_ziphi(r3v a, r3v b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[1];
}
#define R3_ZIPLO(a, b)    r3_ziplo((a), (b))
#define R3_ZIPHI(a, b)    r3_ziphi((a), (b))
#define R3_TRANSPOSE(a, b, c, e) do { \
   float32x4x2_t t01_ = vtrnq_f32((a), (b)); \
   float32x4x2_t t23_ = vtrnq_f32((c), (e)); \
   (a) = vcombine_f32(vget_low_f32(t01_.val[0]), vget_low_f32(t23_.val[0])); \
   (b) = vcombine_f32(vget_low_f32(t01_.val[1]), vget_low_f32(t23_.val[1])); \
   (c) = vcombine_f32(vget_high_f32(t01_.val[0]), vget_high_f32(t23_.val[0])); \
   (e) = vcombine_f32(vget_high_f32(t01_.val[1]), vget_high_f32(t23_.val[1])); \
} while (0)
/* Eight samples to s16, rounded to nearest and saturated */
static INLINE int32x4_t r3_round_s32(r3v a)
{
#if defined(__aarch64__) || defined(_M_ARM64)
   return vcvtnq_s32_f32(a);
#else
   /* round half away from zero: add +-0.5 by sign, then truncate */
   uint32x4_t neg  = vcltq_f32(a, vdupq_n_f32(0.0f));
   r3v        half = vbslq_f32(neg, vdupq_n_f32(-0.5f), vdupq_n_f32(0.5f));
   return vcvtq_s32_f32(vaddq_f32(a, half));
#endif
}
static INLINE void r3_st_s16x8(int16_t *dst, r3v a, r3v b)
{
   vst1q_s16(dst, vcombine_s16(vqmovn_s32(r3_round_s32(a)),
            vqmovn_s32(r3_round_s32(b))));
}
#define R3_HAVE_S16 1
#else
#define RATRAC3_SCALAR 1
#endif

#ifdef RATRAC3_SCALAR
typedef struct { float f[4]; } r3v;
static INLINE r3v r3_ld(const float *p)
{ r3v v; v.f[0] = p[0]; v.f[1] = p[1]; v.f[2] = p[2]; v.f[3] = p[3]; return v; }
static INLINE void r3_st(float *p, r3v v)
{ p[0] = v.f[0]; p[1] = v.f[1]; p[2] = v.f[2]; p[3] = v.f[3]; }
static INLINE r3v r3_add(r3v a, r3v b)
{ a.f[0] += b.f[0]; a.f[1] += b.f[1]; a.f[2] += b.f[2]; a.f[3] += b.f[3]; return a; }
static INLINE r3v r3_sub(r3v a, r3v b)
{ a.f[0] -= b.f[0]; a.f[1] -= b.f[1]; a.f[2] -= b.f[2]; a.f[3] -= b.f[3]; return a; }
static INLINE r3v r3_mul(r3v a, r3v b)
{ a.f[0] *= b.f[0]; a.f[1] *= b.f[1]; a.f[2] *= b.f[2]; a.f[3] *= b.f[3]; return a; }
static INLINE r3v r3_set4(float a, float b, float c, float e)
{ r3v v; v.f[0] = a; v.f[1] = b; v.f[2] = c; v.f[3] = e; return v; }
static INLINE r3v r3_rev(r3v a)
{ return r3_set4(a.f[3], a.f[2], a.f[1], a.f[0]); }
static INLINE r3v r3_ziplo(r3v a, r3v b)
{ return r3_set4(a.f[0], b.f[0], a.f[1], b.f[1]); }
static INLINE r3v r3_ziphi(r3v a, r3v b)
{ return r3_set4(a.f[2], b.f[2], a.f[3], b.f[3]); }
#define R3_LD(p)          r3_ld(p)
#define R3_ST(p, v)       r3_st((p), (v))
#define R3_ADD(a, b)      r3_add((a), (b))
#define R3_SUB(a, b)      r3_sub((a), (b))
#define R3_MUL(a, b)      r3_mul((a), (b))
#define R3_SET1(x)        r3_set4((x), (x), (x), (x))
#define R3_SET4(a,b,c,e)  r3_set4((a), (b), (c), (e))
#define R3_REV(v)         r3_rev(v)
#define R3_ZIPLO(a, b)    r3_ziplo((a), (b))
#define R3_ZIPHI(a, b)    r3_ziphi((a), (b))
#define R3_TRANSPOSE(a, b, c, e) do { \
   r3v t0_ = r3_set4((a).f[0], (b).f[0], (c).f[0], (e).f[0]); \
   r3v t1_ = r3_set4((a).f[1], (b).f[1], (c).f[1], (e).f[1]); \
   r3v t2_ = r3_set4((a).f[2], (b).f[2], (c).f[2], (e).f[2]); \
   r3v t3_ = r3_set4((a).f[3], (b).f[3], (c).f[3], (e).f[3]); \
   (a) = t0_; (b) = t1_; (c) = t2_; (e) = t3_; \
} while (0)
#endif

#define RATRAC3_BANDS    4
#define RATRAC3_BAND_LEN 256
#define RATRAC3_SPEC_LEN 1024
#define RATRAC3_GAIN_MAX 8
#define RATRAC3_JS_RAMP  8

/* Subband edges in coefficients */
static const uint16_t ratrac3_sb[33] = {
     0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144,
   160, 176, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480, 512, 576,
   640, 704, 768, 896, 1024
};

/* Bits a value for the fixed-length coding, by selector; selector 1
 * codes two values in its four bits */
static const uint8_t ratrac3_clc_bits[8] = { 0, 4, 3, 3, 4, 4, 5, 6 };

/* The quantiser's largest step, by selector; a value v dequantises to
 * v * 2^((sf - 15) / 3) / step */
static const float ratrac3_inv_step[8] = {
   0.0f, 1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 3.5f, 1.0f / 4.5f,
   1.0f / 7.5f, 1.0f / 15.5f, 1.0f / 31.5f
};

/* Selector 1's VLC symbols, as value pairs */
static const int8_t ratrac3_pair[9][2] = {
   { 0, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 },
   { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
};

/* Half of the 48-tap QMF prototype; the filter is symmetric */
static const float ratrac3_qmf_half[24] = {
   -0.00001461907f, -0.00009205479f, -0.000056157569f, 0.00030117269f,
    0.0002422519f,  -0.00085293897f, -0.0005205574f,   0.0020340169f,
    0.00078333891f, -0.0042153862f,  -0.00075614988f,  0.0078402944f,
   -0.000061169922f,-0.01344162f,     0.0024626821f,   0.021736089f,
   -0.007801671f,   -0.034090221f,    0.01880949f,     0.054326009f,
   -0.043596379f,   -0.099384367f,    0.13207909f,     0.46424159f
};

/* Joint stereo matrices, per band selector: L = a*M + b*S, R = c*M + d*S.
 * The steady matrix is what a frame uses after its first eight samples;
 * the ramp matrix is what the first eight samples interpolate between
 * when the selector changes. The two differ only for selector 2. */
static const float ratrac3_js_steady[4][4] = {
   { 0.0f, 2.0f, 2.0f, -2.0f },
   { 2.0f, 2.0f, 0.0f, -2.0f },
   { 1.0f, 1.0f, 1.0f, -1.0f },
   { 1.0f, 1.0f, 1.0f, -1.0f }
};
static const float ratrac3_js_ramp[4][4] = {
   { 0.0f, 2.0f, 2.0f, -2.0f },
   { 2.0f, 2.0f, 0.0f, -2.0f },
   { 0.0f, 0.0f, 2.0f,  0.0f },
   { 1.0f, 1.0f, 1.0f, -1.0f }
};

/* Spectral VLC lookup tables, one per selector 1..7, indexed by the
 * next 8 bits of the stream. Each entry decodes one symbol, or two
 * when both fit in the 8 bits:
 *    bits  0..3   length of all it decodes
 *    bits  4..5   symbols decoded, 1 or 2
 *    bits  6..9   length of the first alone
 *    bits 10..17  first symbol, bits 18..25 second (int8)
 * A symbol is the coefficient for selectors 2..7, the pair index for 1. */
static const uint32_t ratrac3_vlc[7][256] = {
   {
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0080064, 0x0080064, 0x0080064, 0x0080064,
      0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064,
      0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064,
      0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065,
      0x00C0065, 0x00C0065, 0x0100065, 0x0100065, 0x0100065, 0x0100065,
      0x0100065, 0x0100065, 0x0100065, 0x0100065, 0x0140066, 0x0140066,
      0x0140066, 0x0140066, 0x0180066, 0x0180066, 0x0180066, 0x0180066,
      0x01C0066, 0x01C0066, 0x01C0066, 0x01C0066, 0x0200066, 0x0200066,
      0x0200066, 0x0200066, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00404E6, 0x00404E6, 0x00404E6, 0x00404E6, 0x00804E6, 0x00804E6,
      0x00804E6, 0x00804E6, 0x00C04E7, 0x00C04E7, 0x01004E7, 0x01004E7,
      0x01404E8, 0x01804E8, 0x01C04E8, 0x02004E8, 0x00008E4, 0x00008E4,
      0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4,
      0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4,
      0x00008E4, 0x00008E4, 0x00408E6, 0x00408E6, 0x00408E6, 0x00408E6,
      0x00808E6, 0x00808E6, 0x00808E6, 0x00808E6, 0x00C08E7, 0x00C08E7,
      0x01008E7, 0x01008E7, 0x01408E8, 0x01808E8, 0x01C08E8, 0x02008E8,
      0x0000D25, 0x0000D25, 0x0000D25, 0x0000D25, 0x0000D25, 0x0000D25,
      0x0000D25, 0x0000D25, 0x0040D27, 0x0040D27, 0x0080D27, 0x0080D27,
      0x00C0D28, 0x0100D28, 0x0000D14, 0x0000D14, 0x0001125, 0x0001125,
      0x0001125, 0x0001125, 0x0001125, 0x0001125, 0x0001125, 0x0001125,
      0x0041127, 0x0041127, 0x0081127, 0x0081127, 0x00C1128, 0x0101128,
      0x0001114, 0x0001114, 0x0001566, 0x0001566, 0x0001566, 0x0001566,
      0x0041568, 0x0081568, 0x0001555, 0x0001555, 0x0001966, 0x0001966,
      0x0001966, 0x0001966, 0x0041968, 0x0081968, 0x0001955, 0x0001955,
      0x0001D66, 0x0001D66, 0x0001D66, 0x0001D66, 0x0041D68, 0x0081D68,
      0x0001D55, 0x0001D55, 0x0002166, 0x0002166, 0x0002166, 0x0002166,
      0x0042168, 0x0082168, 0x0002155, 0x0002155
   },
   {
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064,
      0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x0080064,
      0x0080064, 0x0080064, 0x0080064, 0x0080064, 0x3F80064, 0x3F80064,
      0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064,
      0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064, 0x3F80064,
      0x3F80064, 0x3F80064, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00404E6, 0x00404E6, 0x00404E6, 0x00404E6, 0x3FC04E6, 0x3FC04E6,
      0x3FC04E6, 0x3FC04E6, 0x00804E6, 0x00804E6, 0x00804E6, 0x00804E6,
      0x3F804E6, 0x3F804E6, 0x3F804E6, 0x3F804E6, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x007FCE6, 0x007FCE6, 0x007FCE6, 0x007FCE6,
      0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x00BFCE6, 0x00BFCE6,
      0x00BFCE6, 0x00BFCE6, 0x3FBFCE6, 0x3FBFCE6, 0x3FBFCE6, 0x3FBFCE6,
      0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4,
      0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4,
      0x00008E4, 0x00008E4, 0x00008E4, 0x00008E4, 0x00408E6, 0x00408E6,
      0x00408E6, 0x00408E6, 0x3FC08E6, 0x3FC08E6, 0x3FC08E6, 0x3FC08E6,
      0x00808E6, 0x00808E6, 0x00808E6, 0x00808E6, 0x3F808E6, 0x3F808E6,
      0x3F808E6, 0x3F808E6, 0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4,
      0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4,
      0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4, 0x003F8E4,
      0x007F8E6, 0x007F8E6, 0x007F8E6, 0x007F8E6, 0x3FFF8E6, 0x3FFF8E6,
      0x3FFF8E6, 0x3FFF8E6, 0x00BF8E6, 0x00BF8E6, 0x00BF8E6, 0x00BF8E6,
      0x3FBF8E6, 0x3FBF8E6, 0x3FBF8E6, 0x3FBF8E6
   },
   {
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x0080065, 0x0080065, 0x0080065, 0x0080065, 0x0080065, 0x0080065,
      0x0080065, 0x0080065, 0x3F80065, 0x3F80065, 0x3F80065, 0x3F80065,
      0x3F80065, 0x3F80065, 0x3F80065, 0x3F80065, 0x00C0065, 0x00C0065,
      0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065, 0x00C0065,
      0x3F40065, 0x3F40065, 0x3F40065, 0x3F40065, 0x3F40065, 0x3F40065,
      0x3F40065, 0x3F40065, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00404E6, 0x00404E6, 0x00404E6, 0x00404E6, 0x3FC04E6, 0x3FC04E6,
      0x3FC04E6, 0x3FC04E6, 0x00804E7, 0x00804E7, 0x3F804E7, 0x3F804E7,
      0x00C04E7, 0x00C04E7, 0x3F404E7, 0x3F404E7, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x007FCE6, 0x007FCE6, 0x007FCE6, 0x007FCE6,
      0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x00BFCE7, 0x00BFCE7,
      0x3FBFCE7, 0x3FBFCE7, 0x00FFCE7, 0x00FFCE7, 0x3F7FCE7, 0x3F7FCE7,
      0x0000925, 0x0000925, 0x0000925, 0x0000925, 0x0000925, 0x0000925,
      0x0000925, 0x0000925, 0x0040927, 0x0040927, 0x3FC0927, 0x3FC0927,
      0x0080928, 0x3F80928, 0x00C0928, 0x3F40928, 0x003F925, 0x003F925,
      0x003F925, 0x003F925, 0x003F925, 0x003F925, 0x003F925, 0x003F925,
      0x007F927, 0x007F927, 0x3FFF927, 0x3FFF927, 0x00BF928, 0x3FBF928,
      0x00FF928, 0x3F7F928, 0x0000D25, 0x0000D25, 0x0000D25, 0x0000D25,
      0x0000D25, 0x0000D25, 0x0000D25, 0x0000D25, 0x0040D27, 0x0040D27,
      0x3FC0D27, 0x3FC0D27, 0x0080D28, 0x3F80D28, 0x00C0D28, 0x3F40D28,
      0x003F525, 0x003F525, 0x003F525, 0x003F525, 0x003F525, 0x003F525,
      0x003F525, 0x003F525, 0x007F527, 0x007F527, 0x3FFF527, 0x3FFF527,
      0x00BF528, 0x3FBF528, 0x00FF528, 0x3F7F528
   },
   {
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0000062,
      0x0000062, 0x0000062, 0x0000062, 0x0000062, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064, 0x0040064,
      0x0040064, 0x0040064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064, 0x3FC0064,
      0x0080065, 0x0080065, 0x0080065, 0x0080065, 0x0080065, 0x0080065,
      0x0080065, 0x0080065, 0x3F80065, 0x3F80065, 0x3F80065, 0x3F80065,
      0x3F80065, 0x3F80065, 0x3F80065, 0x3F80065, 0x00C0066, 0x00C0066,
      0x00C0066, 0x00C0066, 0x3F40066, 0x3F40066, 0x3F40066, 0x3F40066,
      0x0100066, 0x0100066, 0x0100066, 0x0100066, 0x3F00066, 0x3F00066,
      0x3F00066, 0x3F00066, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4, 0x00004E4,
      0x00404E6, 0x00404E6, 0x00404E6, 0x00404E6, 0x3FC04E6, 0x3FC04E6,
      0x3FC04E6, 0x3FC04E6, 0x00804E7, 0x00804E7, 0x3F804E7, 0x3F804E7,
      0x00C04E8, 0x3F404E8, 0x01004E8, 0x3F004E8, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4, 0x003FCE4,
      0x003FCE4, 0x003FCE4, 0x007FCE6, 0x007FCE6, 0x007FCE6, 0x007FCE6,
      0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x00BFCE7, 0x00BFCE7,
      0x3FBFCE7, 0x3FBFCE7, 0x00FFCE8, 0x3F7FCE8, 0x013FCE8, 0x3F3FCE8,
      0x0000925, 0x0000925, 0x0000925, 0x0000925, 0x0000925, 0x0000925,
      0x0000925, 0x0000925, 0x0040927, 0x0040927, 0x3FC0927, 0x3FC0927,
      0x0080928, 0x3F80928, 0x0000914, 0x0000914, 0x003F925, 0x003F925,
      0x003F925, 0x003F925, 0x003F925, 0x003F925, 0x003F925, 0x003F925,
      0x007F927, 0x007F927, 0x3FFF927, 0x3FFF927, 0x00BF928, 0x3FBF928,
      0x003F914, 0x003F914, 0x0000D66, 0x0000D66, 0x0000D66, 0x0000D66,
      0x0040D68, 0x3FC0D68, 0x0000D55, 0x0000D55, 0x003F566, 0x003F566,
      0x003F566, 0x003F566, 0x007F568, 0x3FFF568, 0x003F555, 0x003F555,
      0x0001166, 0x0001166, 0x0001166, 0x0001166, 0x0041168, 0x3FC1168,
      0x0001155, 0x0001155, 0x003F166, 0x003F166, 0x003F166, 0x003F166,
      0x007F168, 0x3FFF168, 0x003F155, 0x003F155
   },
   {
      0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4,
      0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4,
      0x00000A4, 0x00000A4, 0x00000A4, 0x00000A4, 0x00400A5, 0x00400A5,
      0x00400A5, 0x00400A5, 0x00400A5, 0x00400A5, 0x00400A5, 0x00400A5,
      0x3FC00A5, 0x3FC00A5, 0x3FC00A5, 0x3FC00A5, 0x3FC00A5, 0x3FC00A5,
      0x3FC00A5, 0x3FC00A5, 0x00800A6, 0x00800A6, 0x00800A6, 0x00800A6,
      0x3F800A6, 0x3F800A6, 0x3F800A6, 0x3F800A6, 0x00C00A6, 0x00C00A6,
      0x00C00A6, 0x00C00A6, 0x3F400A6, 0x3F400A6, 0x3F400A6, 0x3F400A6,
      0x01C00A6, 0x01C00A6, 0x01C00A6, 0x01C00A6, 0x3E400A6, 0x3E400A6,
      0x3E400A6, 0x3E400A6, 0x01000A7, 0x01000A7, 0x3F000A7, 0x3F000A7,
      0x01400A8, 0x3EC00A8, 0x01800A8, 0x3E800A8, 0x00004E5, 0x00004E5,
      0x00004E5, 0x00004E5, 0x00004E5, 0x00004E5, 0x00004E5, 0x00004E5,
      0x00404E6, 0x00404E6, 0x00404E6, 0x00404E6, 0x3FC04E6, 0x3FC04E6,
      0x3FC04E6, 0x3FC04E6, 0x00804E7, 0x00804E7, 0x3F804E7, 0x3F804E7,
      0x00C04E7, 0x00C04E7, 0x3F404E7, 0x3F404E7, 0x01C04E7, 0x01C04E7,
      0x3E404E7, 0x3E404E7, 0x01004E8, 0x3F004E8, 0x00004D3, 0x00004D3,
      0x003FCE5, 0x003FCE5, 0x003FCE5, 0x003FCE5, 0x003FCE5, 0x003FCE5,
      0x003FCE5, 0x003FCE5, 0x007FCE6, 0x007FCE6, 0x007FCE6, 0x007FCE6,
      0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x3FFFCE6, 0x00BFCE7, 0x00BFCE7,
      0x3FBFCE7, 0x3FBFCE7, 0x00FFCE7, 0x00FFCE7, 0x3F7FCE7, 0x3F7FCE7,
      0x01FFCE7, 0x01FFCE7, 0x3E7FCE7, 0x3E7FCE7, 0x013FCE8, 0x3F3FCE8,
      0x003FCD3, 0x003FCD3, 0x0000926, 0x0000926, 0x0000926, 0x0000926,
      0x0040927, 0x0040927, 0x3FC0927, 0x3FC0927, 0x0080928, 0x3F80928,
      0x00C0928, 0x3F40928, 0x01C0928, 0x3E40928, 0x0000914, 0x0000914,
      0x003F926, 0x003F926, 0x003F926, 0x003F926, 0x007F927, 0x007F927,
      0x3FFF927, 0x3FFF927, 0x00BF928, 0x3FBF928, 0x00FF928, 0x3F7F928,
      0x01FF928, 0x3E7F928, 0x003F914, 0x003F914, 0x0000D26, 0x0000D26,
      0x0000D26, 0x0000D26, 0x0040D27, 0x0040D27, 0x3FC0D27, 0x3FC0D27,
      0x0080D28, 0x3F80D28, 0x00C0D28, 0x3F40D28, 0x01C0D28, 0x3E40D28,
      0x0000D14, 0x0000D14, 0x003F526, 0x003F526, 0x003F526, 0x003F526,
      0x007F527, 0x007F527, 0x3FFF527, 0x3FFF527, 0x00BF528, 0x3FBF528,
      0x00FF528, 0x3F7F528, 0x01FF528, 0x3E7F528, 0x003F514, 0x003F514,
      0x0001D26, 0x0001D26, 0x0001D26, 0x0001D26, 0x0041D27, 0x0041D27,
      0x3FC1D27, 0x3FC1D27, 0x0081D28, 0x3F81D28, 0x00C1D28, 0x3F41D28,
      0x01C1D28, 0x3E41D28, 0x0001D14, 0x0001D14, 0x003E526, 0x003E526,
      0x003E526, 0x003E526, 0x007E527, 0x007E527, 0x3FFE527, 0x3FFE527,
      0x00BE528, 0x3FBE528, 0x00FE528, 0x3F7E528, 0x01FE528, 0x3E7E528,
      0x003E514, 0x003E514, 0x0001167, 0x0001167, 0x0041168, 0x3FC1168,
      0x0001155, 0x0001155, 0x0001155, 0x0001155, 0x003F167, 0x003F167,
      0x007F168, 0x3FFF168, 0x003F155, 0x003F155, 0x003F155, 0x003F155,
      0x00015A8, 0x0001596, 0x0001596, 0x0001596, 0x003EDA8, 0x003ED96,
      0x003ED96, 0x003ED96, 0x00019A8, 0x0001996, 0x0001996, 0x0001996,
      0x003E9A8, 0x003E996, 0x003E996, 0x003E996
   },
   {
      0x00000E6, 0x00000E6, 0x00000E6, 0x00000E6, 0x00400E7, 0x00400E7,
      0x3FC00E7, 0x3FC00E7, 0x00800E7, 0x00800E7, 0x3F800E7, 0x3F800E7,
      0x00C00E7, 0x00C00E7, 0x3F400E7, 0x3F400E7, 0x03C00E7, 0x03C00E7,
      0x3C400E7, 0x3C400E7, 0x01000E8, 0x3F000E8, 0x01400E8, 0x3EC00E8,
      0x01800E8, 0x3E800E8, 0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3,
      0x00000D3, 0x00000D3, 0x0000527, 0x0000527, 0x0040528, 0x3FC0528,
      0x0080528, 0x3F80528, 0x00C0528, 0x3F40528, 0x03C0528, 0x3C40528,
      0x0000514, 0x0000514, 0x0000514, 0x0000514, 0x0000514, 0x0000514,
      0x003FD27, 0x003FD27, 0x007FD28, 0x3FFFD28, 0x00BFD28, 0x3FBFD28,
      0x00FFD28, 0x3F7FD28, 0x03FFD28, 0x3C7FD28, 0x003FD14, 0x003FD14,
      0x003FD14, 0x003FD14, 0x003FD14, 0x003FD14, 0x0000927, 0x0000927,
      0x0040928, 0x3FC0928, 0x0080928, 0x3F80928, 0x00C0928, 0x3F40928,
      0x03C0928, 0x3C40928, 0x0000914, 0x0000914, 0x0000914, 0x0000914,
      0x0000914, 0x0000914, 0x003F927, 0x003F927, 0x007F928, 0x3FFF928,
      0x00BF928, 0x3FBF928, 0x00FF928, 0x3F7F928, 0x03FF928, 0x3C7F928,
      0x003F914, 0x003F914, 0x003F914, 0x003F914, 0x003F914, 0x003F914,
      0x0000D27, 0x0000D27, 0x0040D28, 0x3FC0D28, 0x0080D28, 0x3F80D28,
      0x00C0D28, 0x3F40D28, 0x03C0D28, 0x3C40D28, 0x0000D14, 0x0000D14,
      0x0000D14, 0x0000D14, 0x0000D14, 0x0000D14, 0x003F527, 0x003F527,
      0x007F528, 0x3FFF528, 0x00BF528, 0x3FBF528, 0x00FF528, 0x3F7F528,
      0x03FF528, 0x3C7F528, 0x003F514, 0x003F514, 0x003F514, 0x003F514,
      0x003F514, 0x003F514, 0x0003D27, 0x0003D27, 0x0043D28, 0x3FC3D28,
      0x0083D28, 0x3F83D28, 0x00C3D28, 0x3F43D28, 0x03C3D28, 0x3C43D28,
      0x0003D14, 0x0003D14, 0x0003D14, 0x0003D14, 0x0003D14, 0x0003D14,
      0x003C527, 0x003C527, 0x007C528, 0x3FFC528, 0x00BC528, 0x3FBC528,
      0x00FC528, 0x3F7C528, 0x03FC528, 0x3C7C528, 0x003C514, 0x003C514,
      0x003C514, 0x003C514, 0x003C514, 0x003C514, 0x0001168, 0x0001155,
      0x0001155, 0x0001155, 0x0001155, 0x0001155, 0x0001155, 0x0001155,
      0x003F168, 0x003F155, 0x003F155, 0x003F155, 0x003F155, 0x003F155,
      0x003F155, 0x003F155, 0x0001568, 0x0001555, 0x0001555, 0x0001555,
      0x0001555, 0x0001555, 0x0001555, 0x0001555, 0x003ED68, 0x003ED55,
      0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55,
      0x0001968, 0x0001955, 0x0001955, 0x0001955, 0x0001955, 0x0001955,
      0x0001955, 0x0001955, 0x003E968, 0x003E955, 0x003E955, 0x003E955,
      0x003E955, 0x003E955, 0x003E955, 0x003E955, 0x0001D96, 0x0001D96,
      0x0001D96, 0x0001D96, 0x003E596, 0x003E596, 0x003E596, 0x003E596,
      0x0002196, 0x0002196, 0x0002196, 0x0002196, 0x003E196, 0x003E196,
      0x003E196, 0x003E196, 0x0002596, 0x0002596, 0x0002596, 0x0002596,
      0x003DD96, 0x003DD96, 0x003DD96, 0x003DD96, 0x0002996, 0x0002996,
      0x0002996, 0x0002996, 0x003D996, 0x003D996, 0x003D996, 0x003D996,
      0x0002DD7, 0x0002DD7, 0x003D5D7, 0x003D5D7, 0x00031D7, 0x00031D7,
      0x003D1D7, 0x003D1D7, 0x00035D7, 0x00035D7, 0x003CDD7, 0x003CDD7,
      0x00039D7, 0x00039D7, 0x003C9D7, 0x003C9D7
   },
   {
      0x00000E6, 0x00000E6, 0x00000E6, 0x00000E6, 0x07C00E7, 0x07C00E7,
      0x38400E7, 0x38400E7, 0x00400E8, 0x3FC00E8, 0x00800E8, 0x3F800E8,
      0x00C00E8, 0x3F400E8, 0x01000E8, 0x3F000E8, 0x01400E8, 0x3EC00E8,
      0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3,
      0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3, 0x00000D3,
      0x00000D3, 0x00000D3, 0x0007D27, 0x0007D27, 0x07C7D28, 0x3847D28,
      0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14,
      0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14, 0x0007D14,
      0x0038527, 0x0038527, 0x07F8528, 0x3878528, 0x0038514, 0x0038514,
      0x0038514, 0x0038514, 0x0038514, 0x0038514, 0x0038514, 0x0038514,
      0x0038514, 0x0038514, 0x0038514, 0x0038514, 0x0000568, 0x0000555,
      0x0000555, 0x0000555, 0x0000555, 0x0000555, 0x0000555, 0x0000555,
      0x003FD68, 0x003FD55, 0x003FD55, 0x003FD55, 0x003FD55, 0x003FD55,
      0x003FD55, 0x003FD55, 0x0000968, 0x0000955, 0x0000955, 0x0000955,
      0x0000955, 0x0000955, 0x0000955, 0x0000955, 0x003F968, 0x003F955,
      0x003F955, 0x003F955, 0x003F955, 0x003F955, 0x003F955, 0x003F955,
      0x0000D68, 0x0000D55, 0x0000D55, 0x0000D55, 0x0000D55, 0x0000D55,
      0x0000D55, 0x0000D55, 0x003F568, 0x003F555, 0x003F555, 0x003F555,
      0x003F555, 0x003F555, 0x003F555, 0x003F555, 0x0001168, 0x0001155,
      0x0001155, 0x0001155, 0x0001155, 0x0001155, 0x0001155, 0x0001155,
      0x003F168, 0x003F155, 0x003F155, 0x003F155, 0x003F155, 0x003F155,
      0x003F155, 0x003F155, 0x0001568, 0x0001555, 0x0001555, 0x0001555,
      0x0001555, 0x0001555, 0x0001555, 0x0001555, 0x003ED68, 0x003ED55,
      0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55, 0x003ED55,
      0x0001996, 0x0001996, 0x0001996, 0x0001996, 0x003E996, 0x003E996,
      0x003E996, 0x003E996, 0x0001D96, 0x0001D96, 0x0001D96, 0x0001D96,
      0x003E596, 0x003E596, 0x003E596, 0x003E596, 0x0002196, 0x0002196,
      0x0002196, 0x0002196, 0x003E196, 0x003E196, 0x003E196, 0x003E196,
      0x0002596, 0x0002596, 0x0002596, 0x0002596, 0x003DD96, 0x003DD96,
      0x003DD96, 0x003DD96, 0x0002996, 0x0002996, 0x0002996, 0x0002996,
      0x003D996, 0x003D996, 0x003D996, 0x003D996, 0x0002D96, 0x0002D96,
      0x0002D96, 0x0002D96, 0x003D596, 0x003D596, 0x003D596, 0x003D596,
      0x0003196, 0x0003196, 0x0003196, 0x0003196, 0x003D196, 0x003D196,
      0x003D196, 0x003D196, 0x0003596, 0x0003596, 0x0003596, 0x0003596,
      0x003CD96, 0x003CD96, 0x003CD96, 0x003CD96, 0x00039D7, 0x00039D7,
      0x003C9D7, 0x003C9D7, 0x0003DD7, 0x0003DD7, 0x003C5D7, 0x003C5D7,
      0x00041D7, 0x00041D7, 0x003C1D7, 0x003C1D7, 0x00045D7, 0x00045D7,
      0x003BDD7, 0x003BDD7, 0x00049D7, 0x00049D7, 0x003B9D7, 0x003B9D7,
      0x0004DD7, 0x0004DD7, 0x003B5D7, 0x003B5D7, 0x00051D7, 0x00051D7,
      0x003B1D7, 0x003B1D7, 0x0005618, 0x003AE18, 0x0005A18, 0x003AA18,
      0x0005E18, 0x003A618, 0x0006218, 0x003A218, 0x0006618, 0x0039E18,
      0x0006A18, 0x0039A18, 0x0006E18, 0x0039618, 0x0007218, 0x0039218,
      0x0007618, 0x0038E18, 0x0007A18, 0x0038A18
   }
};

/* A gain curve: up to 8 points of (level, location), level 4 neutral */
typedef struct
{
   uint8_t n;
   uint8_t level[RATRAC3_GAIN_MAX];
   uint8_t loc[RATRAC3_GAIN_MAX];
} ratrac3_gain_t;

/* One sound unit, parsed and not yet applied */
typedef struct
{
   ratrac3_gain_t gain[RATRAC3_BANDS];
   unsigned       bands;            /* bands carrying any coefficient */
   float          spec[RATRAC3_SPEC_LEN];
} ratrac3_unit_t;

/* What a coded channel carries from frame to frame */
typedef struct
{
   float          tail[RATRAC3_BANDS][RATRAC3_BAND_LEN];
   ratrac3_gain_t gain[RATRAC3_BANDS]; /* the previous frame's */
   bool           tail_zero[RATRAC3_BANDS];
} ratrac3_chan_t;

/* What an output channel carries: the three QMF stages' history, as
 * the sums and differences of the two bands, 23 of each */
#define RATRAC3_QMF_H 23
typedef struct
{
   float s0[RATRAC3_QMF_H + RATRAC3_BAND_LEN], d0[RATRAC3_QMF_H + RATRAC3_BAND_LEN];
   float s1[RATRAC3_QMF_H + RATRAC3_BAND_LEN], d1[RATRAC3_QMF_H + RATRAC3_BAND_LEN];
   float s2[RATRAC3_QMF_H + 2 * RATRAC3_BAND_LEN], d2[RATRAC3_QMF_H + 2 * RATRAC3_BAND_LEN];
} ratrac3_qmf_t;

struct ratrac3
{
   unsigned       channels;
   unsigned       block_align;
   bool           joint;

   ratrac3_chan_t chan[2];
   ratrac3_qmf_t  qmf[2];

   /* Joint stereo parameters of the last two frames, [1] the newer:
    * frame n applies those of frame n - 1, ramping from frame n - 2's */
   uint8_t        js_weight[2];
   uint8_t        js_sel[2][RATRAC3_BANDS];

   /* Scratch */
   ratrac3_unit_t unit[2];
   float          band[2][RATRAC3_BANDS][RATRAC3_BAND_LEN];
   float          pcm[2][RATRAC3_SAMPLES];
   float          tonal[RATRAC3_SPEC_LEN + 8];
   float          zr[128 * 4], zi[128 * 4];  /* FFT, four bands a row */
   float          u[RATRAC3_BAND_LEN * 4];
   float          head[RATRAC3_BANDS][RATRAC3_BAND_LEN];
   float          ntail[RATRAC3_BANDS][RATRAC3_BAND_LEN];
   float          lo[2 * RATRAC3_BAND_LEN], hi[2 * RATRAC3_BAND_LEN];

   /* Tables */
   float          sf[64];
   float          win[RATRAC3_BAND_LEN];   /* first half; the rest mirrors it */
   float          win_a[128], win_b[128], win_c[128], win_d[128];
   float          tw[128 * 8];             /* pre/post twiddle: re x4, im x4 */
   float          fft_tw[64 * 8];
   uint8_t        bitrev[128];
   float          qmf_even[24 * 4];        /* taps 0, 2, ..., 46, four of each */
   float          gain_level[16];
   float          gain_step[31][RATRAC3_GAIN_MAX + 1];
   float          js_w[16][2];
};

/* ------------------------------------------------------------------ */
/* Bit reading, MSB first, forwards or (for the joint stereo side unit)
 * backwards through the bytes. Past the end reads zeroes; the caller
 * checks the position against the length when the unit is done. */

typedef struct
{
   const uint8_t *p;     /* the first byte read */
   size_t         len;   /* bytes */
   size_t         pos;   /* bits */
   bool           back;  /* bytes run downwards from p */
} ratrac3_br_t;

/* The 64 bits from bit pos on, MSB first; at least 57 of them are the
 * stream's (the rest zero), as pos & 7 shifts out up to 7. Backwards,
 * the bytes at decreasing addresses read big-endian are the same bytes
 * read little-endian from the lowest address. */
static INLINE uint64_t ratrac3_br_peek64(const ratrac3_br_t *b, size_t pos)
{
   size_t   i = pos >> 3;
   uint64_t v;
   if (i + 8 <= b->len)
   {
      const uint8_t *q;
      if (!b->back)
      {
         q = b->p + i;
         v = ((uint64_t)q[0] << 56) | ((uint64_t)q[1] << 48)
           | ((uint64_t)q[2] << 40) | ((uint64_t)q[3] << 32)
           | ((uint64_t)q[4] << 24) | ((uint64_t)q[5] << 16)
           | ((uint64_t)q[6] << 8)  |  (uint64_t)q[7];
      }
      else
      {
         q = b->p - i - 7;
         v = ((uint64_t)q[7] << 56) | ((uint64_t)q[6] << 48)
           | ((uint64_t)q[5] << 40) | ((uint64_t)q[4] << 32)
           | ((uint64_t)q[3] << 24) | ((uint64_t)q[2] << 16)
           | ((uint64_t)q[1] << 8)  |  (uint64_t)q[0];
      }
   }
   else
   {
      unsigned k;
      v = 0;
      for (k = 0; k < 8; k++)
      {
         v <<= 8;
         if (i + k < b->len)
            v |= b->back ? *(b->p - (i + k)) : b->p[i + k];
      }
   }
   return v << (pos & 7);
}

/* n in 1..32 */
static INLINE unsigned ratrac3_br_get(ratrac3_br_t *b, unsigned n)
{
   uint64_t v = ratrac3_br_peek64(b, b->pos) >> (64 - n);
   b->pos    += n;
   return (unsigned)v;
}

/* Sign-extends the n-bit two's complement value v */
#define RATRAC3_SEXT(v, n) \
   ((int)((unsigned)(v) ^ (1u << ((n) - 1))) - (int)(1u << ((n) - 1)))

/* Reads n quantised values with selector sel (1..7), fixed-length
 * (mode 1) or VLC (mode 0) */
static void ratrac3_ints(ratrac3_br_t *b, unsigned sel, unsigned mode,
      unsigned n, int *out)
{
   size_t   pos   = b->pos;
   unsigned i     = 0;
   uint64_t c     = ratrac3_br_peek64(b, pos);
   unsigned avail = 57;

   if (mode)
   {
      unsigned bits = ratrac3_clc_bits[sel];
      if (sel == 1)
      {
         /* two 2-bit values in each 4 bits */
         while (i < n)
         {
            unsigned v;
            if (avail < 4)
            {
               c     = ratrac3_br_peek64(b, pos);
               avail = 57;
            }
            v      = (unsigned)(c >> 60);
            c    <<= 4;
            avail -= 4;
            pos   += 4;
            out[i++] = RATRAC3_SEXT(v >> 2, 2);
            out[i++] = RATRAC3_SEXT(v & 3, 2);
         }
      }
      else
      {
         while (i < n)
         {
            unsigned v;
            if (avail < bits)
            {
               c     = ratrac3_br_peek64(b, pos);
               avail = 57;
            }
            v        = (unsigned)(c >> (64 - bits));
            c      <<= bits;
            avail   -= bits;
            pos     += bits;
            out[i++] = RATRAC3_SEXT(v, bits);
         }
      }
   }
   else
   {
      /* Two symbols a lookup where they fit; out has room for the
       * second symbol's values past n, written and then left */
      const uint32_t *lut = ratrac3_vlc[sel - 1];
      if (sel == 1)
      {
         /* pairs: two symbols are four values */
         while (i + 2 < n)
         {
            uint32_t e;
            unsigned l;
            if (avail < 8)
            {
               c     = ratrac3_br_peek64(b, pos);
               avail = 57;
            }
            e          = lut[c >> 56];
            l          = (unsigned)(e & 0xF);
            c        <<= l;
            avail     -= l;
            pos       += l;
            out[i]     = ratrac3_pair[(e >> 10) & 0xFF][0];
            out[i + 1] = ratrac3_pair[(e >> 10) & 0xFF][1];
            out[i + 2] = ratrac3_pair[(e >> 18) & 0xFF][0];
            out[i + 3] = ratrac3_pair[(e >> 18) & 0xFF][1];
            i         += 2 * (unsigned)((e >> 4) & 3);
         }
         if (i < n)
         {
            uint32_t e = lut[(ratrac3_br_peek64(b, pos)) >> 56];
            pos       += (e >> 6) & 0xF;
            out[i]     = ratrac3_pair[(e >> 10) & 0xFF][0];
            out[i + 1] = ratrac3_pair[(e >> 10) & 0xFF][1];
         }
      }
      else
      {
         while (i + 1 < n)
         {
            uint32_t e;
            unsigned l;
            if (avail < 8)
            {
               c     = ratrac3_br_peek64(b, pos);
               avail = 57;
            }
            e          = lut[c >> 56];
            l          = (unsigned)(e & 0xF);
            c        <<= l;
            avail     -= l;
            pos       += l;
            out[i]     = (int8_t)((e >> 10) & 0xFF);
            out[i + 1] = (int8_t)((e >> 18) & 0xFF);
            i         += (unsigned)((e >> 4) & 3);
         }
         if (i < n)
         {
            uint32_t e = lut[(ratrac3_br_peek64(b, pos)) >> 56];
            pos       += (e >> 6) & 0xF;
            out[i]     = (int8_t)((e >> 10) & 0xFF);
         }
      }
   }
   b->pos = pos;
}

/* ------------------------------------------------------------------ */
/* Tables */

static void ratrac3_tables(ratrac3_t *d)
{
   unsigned i, j;
   const double pi = 3.14159265358979323846;

   for (i = 0; i < 64; i++)
      d->sf[i] = (float)pow(2.0, ((double)i - 15.0) / 3.0);

   /* The window: a raised sine, divided through so that the
    * overlapping halves of two blocks add back to one */
   for (i = 0; i < RATRAC3_BAND_LEN; i++)
   {
      double a = (sin((((double)i + 0.5) / 256.0 - 0.5) * pi) + 1.0) * 0.5;
      double b = (sin((((double)(255 - i) + 0.5) / 256.0 - 0.5) * pi)
            + 1.0) * 0.5;
      d->win[i] = (float)(a / (a * a + b * b));
   }
   /* The four quarters of the window as the IMDCT's output order meets
    * them, the IMDCT's signs folded in */
   for (i = 0; i < 128; i++)
   {
      d->win_a[i] =  d->win[i];
      d->win_b[i] = -d->win[128 + i];
      d->win_c[i] = -d->win[255 - i];
      d->win_d[i] = -d->win[127 - i];
   }

   /* IMDCT of 256 coefficients through a 128-point complex FFT */
   /* Twiddles four of each, to load as vectors */
   for (i = 0; i < 128; i++)
   {
      double t = -2.0 * pi * ((double)i + 0.125) / 512.0;
      for (j = 0; j < 4; j++)
      {
         d->tw[8 * i + j]     = (float)cos(t);
         d->tw[8 * i + 4 + j] = (float)sin(t);
      }
   }
   for (i = 0; i < 64; i++)
   {
      double t = -2.0 * pi * (double)i / 128.0;
      for (j = 0; j < 4; j++)
      {
         d->fft_tw[8 * i + j]     = (float)cos(t);
         d->fft_tw[8 * i + 4 + j] = (float)sin(t);
      }
   }
   for (i = 0; i < 128; i++)
   {
      unsigned r = 0;
      for (j = 0; j < 7; j++)
         if (i & (1u << j))
            r |= 1u << (6 - j);
      d->bitrev[i] = (uint8_t)r;
   }

   /* The filter is half[0..23] then half[23..0], doubled; its odd taps
    * are its even ones backwards */
   for (i = 0; i < 24; i++)
   {
      unsigned e = 2 * i;
      for (j = 0; j < 4; j++)
         d->qmf_even[4 * i + j] = 2.0f * ratrac3_qmf_half[e < 24 ? e : 47 - e];
   }

   for (i = 0; i < 16; i++)
      d->gain_level[i] = (float)pow(2.0, 4.0 - (double)i);
   /* A transition between levels a and b moves by 2^((a - b) / 8) a
    * sample: index the difference + 15, the sample 0..8 */
   for (i = 0; i < 31; i++)
      for (j = 0; j <= RATRAC3_GAIN_MAX; j++)
         d->gain_step[i][j] = (float)pow(2.0,
               ((double)i - 15.0) * (double)j / 8.0);

   /* Joint stereo weights: the top bit picks the side that gets k/7,
    * the other side gets what keeps the power, sqrt(2 - (k/7)^2);
    * k = 7 is unity on both */
   for (i = 0; i < 16; i++)
   {
      unsigned k = i & 7;
      float    a, o;
      if (k == 7)
         a = o = 1.0f;
      else
      {
         a = (float)k / 7.0f;
         o = (float)sqrt(2.0 - (double)a * (double)a);
      }
      d->js_w[i][0] = (i & 8) ? o : a;
      d->js_w[i][1] = (i & 8) ? a : o;
   }
}

/* ------------------------------------------------------------------ */
/* Parsing */

/* Parses the unit after its header; nb is the coded band count */
static bool ratrac3_parse_unit(ratrac3_t *d, ratrac3_br_t *b,
      unsigned nb, ratrac3_unit_t *u)
{
   unsigned i, j, ntc, field, n, mode, cut, top;
   unsigned sel[32], sfi[32];
   int      tonal_max = -1;
   int      tonal_min = RATRAC3_SPEC_LEN;

   /* Gain points; locations must rise */
   for (i = 0; i < RATRAC3_BANDS; i++)
   {
      ratrac3_gain_t *g = &u->gain[i];
      g->n = 0;
      if (i >= nb)
         continue;
      g->n = (uint8_t)ratrac3_br_get(b, 3);
      for (j = 0; j < g->n; j++)
      {
         g->level[j] = (uint8_t)ratrac3_br_get(b, 4);
         g->loc[j]   = (uint8_t)ratrac3_br_get(b, 5);
         if (j && g->loc[j] <= g->loc[j - 1])
            return false;
      }
   }

   /* Tonal components, summed into d->tonal */
   ntc = ratrac3_br_get(b, 5);
   if (ntc)
   {
      unsigned tsel = ratrac3_br_get(b, 2);
      if (tsel == 2)
         return false;
      memset(d->tonal, 0, sizeof(d->tonal));
      for (i = 0; i < ntc; i++)
      {
         unsigned flags = 0, cv, qs, tmode, band;
         for (j = 0; j < nb; j++)
            flags |= ratrac3_br_get(b, 1) << j;
         cv = ratrac3_br_get(b, 3) + 1;
         qs = ratrac3_br_get(b, 3);
         if (qs <= 1)
            return false;
         tmode = (tsel == 3) ? ratrac3_br_get(b, 1) : (tsel & 1);
         for (band = 0; band < nb; band++)
         {
            unsigned c;
            if (!(flags & (1u << band)))
               continue;
            for (c = 0; c < 4; c++)
            {
               unsigned cnt = ratrac3_br_get(b, 3), k;
               for (k = 0; k < cnt; k++)
               {
                  unsigned s   = ratrac3_br_get(b, 6);
                  unsigned pos = band * 256 + c * 64 + ratrac3_br_get(b, 6);
                  int   v[8 + 4];
                  float scale = d->sf[s] * ratrac3_inv_step[qs];
                  unsigned m, room = RATRAC3_SPEC_LEN - pos;
                  ratrac3_ints(b, qs, tmode, cv, v);
                  /* Values that would land past the spectrum are read
                   * and dropped */
                  for (m = 0; m < cv && m < room; m++)
                     d->tonal[pos + m] += (float)v[m] * scale;
                  if ((int)(pos + cv - 1) > tonal_max)
                     tonal_max = (int)(pos + cv - 1);
                  if ((int)pos < tonal_min)
                     tonal_min = (int)pos;
               }
            }
         }
         if (b->pos > b->len * 8)
            return false;
      }
   }

   /* Spectral subbands */
   field = ratrac3_br_get(b, 5);
   n     = field + 1;
   mode  = ratrac3_br_get(b, 1);
   for (i = 0; i < n; i++)
      sel[i] = ratrac3_br_get(b, 3);
   for (i = 0; i < n; i++)
      sfi[i] = sel[i] ? ratrac3_br_get(b, 6) : 0;
   for (i = 0; i < n; i++)
   {
      float   *dst = u->spec + ratrac3_sb[i];
      unsigned w   = ratrac3_sb[i + 1] - ratrac3_sb[i];
      if (sel[i])
      {
         int      v[128 + 4];
         float    scale = d->sf[sfi[i]] * ratrac3_inv_step[sel[i]];
         ratrac3_ints(b, sel[i], mode, w, v);
         for (j = 0; j < w; j++)
            dst[j] = (float)v[j] * scale;
      }
      else
         memset(dst, 0, w * sizeof(float));
   }
   if (b->pos > b->len * 8)
      return false;

   /* The reference synthesises the spectral coefficients of only as
    * many bands as reach the start of the last coded subband, or, with
    * tonal components, of two bands more than the last one those
    * reach; tonal values themselves are always kept. */
   cut = (ratrac3_sb[field] + 255) >> 8;
   if (tonal_max >= 0)
   {
      unsigned t = (unsigned)((tonal_max + 1) >> 8) + 2;
      if (t > cut)
         cut = t;
   }
   if (cut > RATRAC3_BANDS)
      cut = RATRAC3_BANDS;
   top = ratrac3_sb[n];
   if (top > cut * 256)
      top = cut * 256;
   if (top < RATRAC3_SPEC_LEN)
      memset(u->spec + top, 0, (RATRAC3_SPEC_LEN - top) * sizeof(float));

   if (tonal_max >= 0)
   {
      int end = tonal_max < RATRAC3_SPEC_LEN - 1
         ? tonal_max : RATRAC3_SPEC_LEN - 1;
      int k;
      for (k = tonal_min; k <= end; k++)
         u->spec[k] += d->tonal[k];
      if ((unsigned)end + 1 > top)
         top = (unsigned)end + 1;
   }
   u->bands = (top + 255) >> 8;
   return true;
}

/* ------------------------------------------------------------------ */
/* Synthesis */

/* The IMDCT of a channel's four bands at once, one band a lane: a
 * 512-point IMDCT through a 128-point complex FFT. The pre-twiddle
 * writes straight into bit-reversed order; odd bands come
 * frequency-inverted, which swaps the real and imaginary inputs. The
 * windowed halves land in d->head (to overlap the previous tail) and
 * d->ntail (the next frame's). */
static void ratrac3_imdct4(ratrac3_t *d, const float *spec)
{
   unsigned     k, len;
   float       *zr = d->zr, *zi = d->zi, *u = d->u;
   const float *x0 = spec,       *x1 = spec + 256;
   const float *x2 = spec + 512, *x3 = spec + 768;

   for (k = 0; k < 128; k++)
   {
      unsigned e  = 2 * k, o = 255 - 2 * k, j = 4 * d->bitrev[k];
      r3v      re = R3_SET4(x0[e], x1[o], x2[e], x3[o]);
      r3v      im = R3_SET4(x0[o], x1[e], x2[o], x3[e]);
      r3v      cr = R3_LD(d->tw + 8 * k);
      r3v      ci = R3_LD(d->tw + 8 * k + 4);
      R3_ST(zr + j, R3_SUB(R3_MUL(re, cr), R3_MUL(im, ci)));
      R3_ST(zi + j, R3_ADD(R3_MUL(re, ci), R3_MUL(im, cr)));
   }

   /* The first two passes together: twiddles 1 and -i */
   for (k = 0; k < 128; k += 4)
   {
      float *pr = zr + 4 * k, *pi = zi + 4 * k;
      r3v a0r = R3_LD(pr),      a0i = R3_LD(pi);
      r3v a1r = R3_LD(pr + 4),  a1i = R3_LD(pi + 4);
      r3v a2r = R3_LD(pr + 8),  a2i = R3_LD(pi + 8);
      r3v a3r = R3_LD(pr + 12), a3i = R3_LD(pi + 12);
      r3v b0r = R3_ADD(a0r, a1r), b0i = R3_ADD(a0i, a1i);
      r3v b1r = R3_SUB(a0r, a1r), b1i = R3_SUB(a0i, a1i);
      r3v b2r = R3_ADD(a2r, a3r), b2i = R3_ADD(a2i, a3i);
      r3v b3r = R3_SUB(a2r, a3r), b3i = R3_SUB(a2i, a3i);
      R3_ST(pr,      R3_ADD(b0r, b2r)); R3_ST(pi,      R3_ADD(b0i, b2i));
      R3_ST(pr + 8,  R3_SUB(b0r, b2r)); R3_ST(pi + 8,  R3_SUB(b0i, b2i));
      R3_ST(pr + 4,  R3_ADD(b1r, b3i)); R3_ST(pi + 4,  R3_SUB(b1i, b3r));
      R3_ST(pr + 12, R3_SUB(b1r, b3i)); R3_ST(pi + 12, R3_ADD(b1i, b3r));
   }

   for (len = 8; len <= 128; len <<= 1)
   {
      unsigned half = len >> 1, step = 128 / len, s;
      for (s = 0; s < 128; s += len)
      {
         for (k = 0; k < half; k++)
         {
            float *ar = zr + 4 * (s + k),        *ai = zi + 4 * (s + k);
            float *cr = zr + 4 * (s + k + half), *ci = zi + 4 * (s + k + half);
            r3v wr  = R3_LD(d->fft_tw + 8 * k * step);
            r3v wi  = R3_LD(d->fft_tw + 8 * k * step + 4);
            r3v vcr = R3_LD(cr), vci = R3_LD(ci);
            r3v var = R3_LD(ar), vai = R3_LD(ai);
            r3v xr  = R3_SUB(R3_MUL(vcr, wr), R3_MUL(vci, wi));
            r3v xi  = R3_ADD(R3_MUL(vcr, wi), R3_MUL(vci, wr));
            R3_ST(cr, R3_SUB(var, xr));
            R3_ST(ci, R3_SUB(vai, xi));
            R3_ST(ar, R3_ADD(var, xr));
            R3_ST(ai, R3_ADD(vai, xi));
         }
      }
   }

   /* Post-twiddle into the half-length sequence u: u[2k] is the real
    * part, u[255 - 2k] minus the imaginary one */
   for (k = 0; k < 128; k++)
   {
      r3v cr = R3_LD(d->tw + 8 * k);
      r3v ci = R3_LD(d->tw + 8 * k + 4);
      r3v vr = R3_LD(zr + 4 * k), vi = R3_LD(zi + 4 * k);
      R3_ST(u + 4 * (2 * k), R3_SUB(R3_MUL(vr, cr), R3_MUL(vi, ci)));
      R3_ST(u + 4 * (255 - 2 * k),
            R3_MUL(R3_ADD(R3_MUL(vr, ci), R3_MUL(vi, cr)), R3_SET1(-1.0f)));
   }

   /* The 512 outputs are u[128..255], -u[255..0], -u[0..127]; window
    * them and turn the band-a-lane rows into band-a-row runs */
   for (k = 0; k < 128; k += 4)
   {
      r3v r0, r1, r2, r3, w;

      r0 = R3_LD(u + 4 * (128 + k)); r1 = R3_LD(u + 4 * (129 + k));
      r2 = R3_LD(u + 4 * (130 + k)); r3 = R3_LD(u + 4 * (131 + k));
      R3_TRANSPOSE(r0, r1, r2, r3);
      w  = R3_LD(d->win_a + k);
      R3_ST(d->head[0] + k, R3_MUL(r0, w)); R3_ST(d->head[1] + k, R3_MUL(r1, w));
      R3_ST(d->head[2] + k, R3_MUL(r2, w)); R3_ST(d->head[3] + k, R3_MUL(r3, w));

      r0 = R3_LD(u + 4 * (252 - k)); r1 = R3_LD(u + 4 * (253 - k));
      r2 = R3_LD(u + 4 * (254 - k)); r3 = R3_LD(u + 4 * (255 - k));
      R3_TRANSPOSE(r0, r1, r2, r3);
      w  = R3_LD(d->win_b + k);
      R3_ST(d->head[0] + 128 + k, R3_MUL(R3_REV(r0), w));
      R3_ST(d->head[1] + 128 + k, R3_MUL(R3_REV(r1), w));
      R3_ST(d->head[2] + 128 + k, R3_MUL(R3_REV(r2), w));
      R3_ST(d->head[3] + 128 + k, R3_MUL(R3_REV(r3), w));

      r0 = R3_LD(u + 4 * (124 - k)); r1 = R3_LD(u + 4 * (125 - k));
      r2 = R3_LD(u + 4 * (126 - k)); r3 = R3_LD(u + 4 * (127 - k));
      R3_TRANSPOSE(r0, r1, r2, r3);
      w  = R3_LD(d->win_c + k);
      R3_ST(d->ntail[0] + k, R3_MUL(R3_REV(r0), w));
      R3_ST(d->ntail[1] + k, R3_MUL(R3_REV(r1), w));
      R3_ST(d->ntail[2] + k, R3_MUL(R3_REV(r2), w));
      R3_ST(d->ntail[3] + k, R3_MUL(R3_REV(r3), w));

      r0 = R3_LD(u + 4 * k);       r1 = R3_LD(u + 4 * (k + 1));
      r2 = R3_LD(u + 4 * (k + 2)); r3 = R3_LD(u + 4 * (k + 3));
      R3_TRANSPOSE(r0, r1, r2, r3);
      w  = R3_LD(d->win_d + k);
      R3_ST(d->ntail[0] + 128 + k, R3_MUL(r0, w));
      R3_ST(d->ntail[1] + 128 + k, R3_MUL(r1, w));
      R3_ST(d->ntail[2] + 128 + k, R3_MUL(r2, w));
      R3_ST(d->ntail[3] + 128 + k, R3_MUL(r3, w));
   }
}

/* Scales x by the gain curve g: each point's level holds up to its
 * location * 8, then moves to the next level (or to unity after the
 * last) over eight samples */
static void ratrac3_gain_apply(const ratrac3_t *d, const ratrac3_gain_t *g,
      float *x)
{
   unsigned k, i, pos = 0;
   for (k = 0; k < g->n; k++)
   {
      unsigned at   = (unsigned)g->loc[k] * 8;
      unsigned lv   = g->level[k];
      unsigned next = (k + 1 < g->n) ? g->level[k + 1] : 4;
      float    a    = d->gain_level[lv];
      const float *step = d->gain_step[lv - next + 15];
      for (i = pos; i <= at; i++)
         x[i] *= a;
      for (i = 1; i <= RATRAC3_GAIN_MAX && at + i < RATRAC3_BAND_LEN; i++)
         x[at + i] *= a * step[i];
      pos = at + RATRAC3_GAIN_MAX + 1;
   }
   /* Unity from pos on */
}

static void ratrac3_synth_unit(ratrac3_t *d, ratrac3_chan_t *c,
      const ratrac3_unit_t *u, float out[RATRAC3_BANDS][RATRAC3_BAND_LEN])
{
   unsigned b, i;
   if (u->bands)
      ratrac3_imdct4(d, u->spec);
   for (b = 0; b < RATRAC3_BANDS; b++)
   {
      float *o    = out[b];
      float *tail = c->tail[b];
      if (b < u->bands)
      {
         const float *h  = d->head[b];
         r3v          g0 = R3_SET1(u->gain[b].n
               ? d->gain_level[u->gain[b].level[0]] : 1.0f);
         for (i = 0; i < RATRAC3_BAND_LEN; i += 4)
            R3_ST(o + i, R3_ADD(R3_LD(tail + i), R3_MUL(R3_LD(h + i), g0)));
         memcpy(tail, d->ntail[b], sizeof(c->tail[b]));
         c->tail_zero[b] = false;
      }
      else if (c->tail_zero[b])
         memset(o, 0, sizeof(out[b]));
      else
      {
         memcpy(o, tail, sizeof(out[b]));
         memset(tail, 0, sizeof(c->tail[b]));
         c->tail_zero[b] = true;
      }
      if (c->gain[b].n)
         ratrac3_gain_apply(d, &c->gain[b], o);
      c->gain[b] = u->gain[b];
   }
}

/* Joint stereo: band[0] is mid, band[1] side; they become L and R */
static void ratrac3_joint(ratrac3_t *d, uint8_t weight,
      const uint8_t *sel)
{
   unsigned b, i;
   const float *pw = d->js_w[d->js_weight[0] >> 4];
   const float *cw = d->js_w[d->js_weight[1] >> 4];

   for (b = 0; b < RATRAC3_BANDS; b++)
   {
      float   *l  = d->band[0][b];
      float   *r  = d->band[1][b];
      unsigned ps = d->js_sel[0][b], cs = d->js_sel[1][b];
      const float *m = ratrac3_js_steady[cs];
      float        wl1 = b ? cw[0] : 1.0f, wr1 = b ? cw[1] : 1.0f;
      r3v          a0  = R3_SET1(m[0] * wl1), a1 = R3_SET1(m[1] * wl1);
      r3v          a2  = R3_SET1(m[2] * wr1), a3 = R3_SET1(m[3] * wr1);
      for (i = 0; i < RATRAC3_JS_RAMP; i++)
      {
         float ma = l[i], sa = r[i];
         float t  = (float)i * (1.0f / RATRAC3_JS_RAMP);
         float c0 = m[0], c1 = m[1], c2 = m[2], c3 = m[3];
         float wl = 1.0f, wr = 1.0f;
         if (ps != cs)
         {
            const float *p = ratrac3_js_ramp[ps];
            const float *q = ratrac3_js_ramp[cs];
            c0 = p[0] + (q[0] - p[0]) * t;
            c1 = p[1] + (q[1] - p[1]) * t;
            c2 = p[2] + (q[2] - p[2]) * t;
            c3 = p[3] + (q[3] - p[3]) * t;
         }
         /* The reference ramps left from the older weights' left to
          * their right, and right from the newer's left to their
          * right - not each side from old to new. Kept as is. */
         if (b)
         {
            wl = pw[0] + (pw[1] - pw[0]) * t;
            wr = cw[0] + (cw[1] - cw[0]) * t;
         }
         l[i] = (c0 * ma + c1 * sa) * wl;
         r[i] = (c2 * ma + c3 * sa) * wr;
      }
      for (; i < RATRAC3_BAND_LEN; i += 4)
      {
         r3v ma = R3_LD(l + i), sa = R3_LD(r + i);
         R3_ST(l + i, R3_ADD(R3_MUL(a0, ma), R3_MUL(a1, sa)));
         R3_ST(r + i, R3_ADD(R3_MUL(a2, ma), R3_MUL(a3, sa)));
      }
   }

   /* Shift the parameters along */
   d->js_weight[0] = d->js_weight[1];
   d->js_weight[1] = weight;
   memcpy(d->js_sel[0], d->js_sel[1], RATRAC3_BANDS);
   memcpy(d->js_sel[1], sel, RATRAC3_BANDS);
}

/* One two-band QMF synthesis stage: n low and n high samples to 2n.
 * The filter's even taps run over the bands' sums, its odd taps over
 * their differences; s and dd hold 23 samples of history ahead of the
 * n new ones. n must be a multiple of 16. */
static void ratrac3_qmf(const ratrac3_t *d, float *s, float *dd,
      const float *lo, const float *hi, unsigned n, float *out)
{
   unsigned i, j, m;
   float   *st = s + RATRAC3_QMF_H, *dt = dd + RATRAC3_QMF_H;
   for (i = 0; i < n; i += 4)
   {
      r3v a = R3_LD(lo + i), b = R3_LD(hi + i);
      R3_ST(st + i, R3_ADD(a, b));
      R3_ST(dt + i, R3_SUB(a, b));
   }
#ifdef RATRAC3_SCALAR
   /* Plain C: four output pairs a pass in eight scalar accumulators,
    * each coefficient loaded once for the sums and the differences */
   for (j = 0; j < n; j += 4)
   {
      const float *sp = s + j, *dp = dd + j + 23;
      float q  = d->qmf_even[0];
      float o0 = q * sp[0], o1 = q * sp[1], o2 = q * sp[2], o3 = q * sp[3];
      float e0 = q * dp[0], e1 = q * dp[1], e2 = q * dp[2], e3 = q * dp[3];
      for (m = 1; m < 24; m++)
      {
         const float *a = sp + m, *b = dp - m;
         q   = d->qmf_even[4 * m];
         o0 += q * a[0]; o1 += q * a[1]; o2 += q * a[2]; o3 += q * a[3];
         e0 += q * b[0]; e1 += q * b[1]; e2 += q * b[2]; e3 += q * b[3];
      }
      out[2 * j]     = e0; out[2 * j + 1] = o0;
      out[2 * j + 2] = e1; out[2 * j + 3] = o1;
      out[2 * j + 4] = e2; out[2 * j + 5] = o2;
      out[2 * j + 6] = e3; out[2 * j + 7] = o3;
   }
#else
   /* The odd taps are the even ones backwards, so each coefficient is
    * loaded once for both; sixteen output pairs a pass */
   for (j = 0; j < n; j += 16)
   {
      const float *sp = s + j, *dp = dd + j + 23;
      r3v q  = R3_LD(d->qmf_even);
      r3v o0 = R3_MUL(q, R3_LD(sp)),      o1 = R3_MUL(q, R3_LD(sp + 4));
      r3v o2 = R3_MUL(q, R3_LD(sp + 8)),  o3 = R3_MUL(q, R3_LD(sp + 12));
      r3v e0 = R3_MUL(q, R3_LD(dp)),      e1 = R3_MUL(q, R3_LD(dp + 4));
      r3v e2 = R3_MUL(q, R3_LD(dp + 8)),  e3 = R3_MUL(q, R3_LD(dp + 12));
#if defined(__GNUC__) && (__GNUC__ >= 8) && !defined(__clang__)
#pragma GCC unroll 23
#elif defined(__clang__)
#pragma unroll
#endif
      for (m = 1; m < 24; m++)
      {
         q  = R3_LD(d->qmf_even + 4 * m);
         o0 = R3_ADD(o0, R3_MUL(q, R3_LD(sp + m)));
         o1 = R3_ADD(o1, R3_MUL(q, R3_LD(sp + m + 4)));
         o2 = R3_ADD(o2, R3_MUL(q, R3_LD(sp + m + 8)));
         o3 = R3_ADD(o3, R3_MUL(q, R3_LD(sp + m + 12)));
         e0 = R3_ADD(e0, R3_MUL(q, R3_LD(dp - m)));
         e1 = R3_ADD(e1, R3_MUL(q, R3_LD(dp - m + 4)));
         e2 = R3_ADD(e2, R3_MUL(q, R3_LD(dp - m + 8)));
         e3 = R3_ADD(e3, R3_MUL(q, R3_LD(dp - m + 12)));
      }
      R3_ST(out + 2 * j,      R3_ZIPLO(e0, o0));
      R3_ST(out + 2 * j + 4,  R3_ZIPHI(e0, o0));
      R3_ST(out + 2 * j + 8,  R3_ZIPLO(e1, o1));
      R3_ST(out + 2 * j + 12, R3_ZIPHI(e1, o1));
      R3_ST(out + 2 * j + 16, R3_ZIPLO(e2, o2));
      R3_ST(out + 2 * j + 20, R3_ZIPHI(e2, o2));
      R3_ST(out + 2 * j + 24, R3_ZIPLO(e3, o3));
      R3_ST(out + 2 * j + 28, R3_ZIPHI(e3, o3));
   }
#endif
   memmove(s,  s + n,  RATRAC3_QMF_H * sizeof(float));
   memmove(dd, dd + n, RATRAC3_QMF_H * sizeof(float));
}

static void ratrac3_qmf_tree(ratrac3_t *d, ratrac3_qmf_t *q,
      float band[RATRAC3_BANDS][RATRAC3_BAND_LEN], float *pcm)
{
   ratrac3_qmf(d, q->s0, q->d0, band[0], band[1], RATRAC3_BAND_LEN, d->lo);
   ratrac3_qmf(d, q->s1, q->d1, band[3], band[2], RATRAC3_BAND_LEN, d->hi);
   ratrac3_qmf(d, q->s2, q->d2, d->lo, d->hi, 2 * RATRAC3_BAND_LEN, pcm);
}

/* ------------------------------------------------------------------ */
/* Frames */

static bool ratrac3_frame(ratrac3_t *d, const uint8_t *frame, size_t len)
{
   unsigned c;
   if (!frame || len < d->block_align)
      return false;

   if (d->joint)
   {
      ratrac3_br_t b;
      size_t       end = d->block_align;
      unsigned     nb, weight, s23;
      uint8_t      sel[RATRAC3_BANDS];

      /* Mid: forwards from the start */
      b.p = frame; b.back = false; b.len = d->block_align; b.pos = 0;
      if (ratrac3_br_get(&b, 6) != 0x28)
         return false;
      nb = ratrac3_br_get(&b, 2) + 1;
      if (!ratrac3_parse_unit(d, &b, nb, &d->unit[0]))
         return false;

      /* Side: backwards from the end, past any F8h padding; the
       * stereo parameters come first */
      while (end && frame[end - 1] == 0xF8)
         end--;
      if (!end)
         return false;
      b.p = frame + end - 1; b.back = true; b.len = end; b.pos = 0;
      weight = ratrac3_br_get(&b, 8);
      s23    = ratrac3_br_get(&b, 4);
      sel[0] = (uint8_t)((weight >> 2) & 3);
      sel[1] = (uint8_t)(weight & 3);
      sel[2] = (uint8_t)(s23 >> 2);
      sel[3] = (uint8_t)(s23 & 3);
      if (ratrac3_br_get(&b, 2) != 3)
         return false;
      nb = ratrac3_br_get(&b, 2) + 1;
      if (!ratrac3_parse_unit(d, &b, nb, &d->unit[1]))
         return false;

      ratrac3_synth_unit(d, &d->chan[0], &d->unit[0], d->band[0]);
      ratrac3_synth_unit(d, &d->chan[1], &d->unit[1], d->band[1]);
      ratrac3_joint(d, (uint8_t)weight, sel);
   }
   else
   {
      size_t per = d->block_align / d->channels;
      for (c = 0; c < d->channels; c++)
      {
         ratrac3_br_t b;
         unsigned     nb;
         b.p = frame + c * per; b.back = false; b.len = per; b.pos = 0;
         if (ratrac3_br_get(&b, 6) != 0x28)
            return false;
         nb = ratrac3_br_get(&b, 2) + 1;
         if (!ratrac3_parse_unit(d, &b, nb, &d->unit[c]))
            return false;
      }
      for (c = 0; c < d->channels; c++)
         ratrac3_synth_unit(d, &d->chan[c], &d->unit[c], d->band[c]);
   }

   for (c = 0; c < d->channels; c++)
      ratrac3_qmf_tree(d, &d->qmf[c], d->band[c], d->pcm[c]);
   return true;
}

int ratrac3_decode_float(ratrac3_t *d, const uint8_t *frame, size_t len,
      float *out)
{
   unsigned i, c, nc;
   if (!d || !out)
      return -1;
   nc = d->channels;
   if (!ratrac3_frame(d, frame, len))
   {
      memset(out, 0, RATRAC3_SAMPLES * nc * sizeof(float));
      return -1;
   }
   if (nc == 2)
   {
      const float *l = d->pcm[0], *r = d->pcm[1];
      r3v          k = R3_SET1(1.0f / 32768.0f);
      for (i = 0; i < RATRAC3_SAMPLES; i += 4)
      {
         r3v a = R3_MUL(R3_LD(l + i), k), b = R3_MUL(R3_LD(r + i), k);
         R3_ST(out + 2 * i,     R3_ZIPLO(a, b));
         R3_ST(out + 2 * i + 4, R3_ZIPHI(a, b));
      }
   }
   else
      for (c = 0; c < nc; c++)
      {
         const float *p = d->pcm[c];
         for (i = 0; i < RATRAC3_SAMPLES; i++)
            out[i * nc + c] = p[i] * (1.0f / 32768.0f);
      }
   return RATRAC3_SAMPLES;
}

int ratrac3_decode_s16(ratrac3_t *d, const uint8_t *frame, size_t len,
      int16_t *out)
{
   unsigned i, c, nc;
   if (!d || !out)
      return -1;
   nc = d->channels;
   if (!ratrac3_frame(d, frame, len))
   {
      memset(out, 0, RATRAC3_SAMPLES * nc * sizeof(int16_t));
      return -1;
   }
#ifdef R3_HAVE_S16
   if (nc == 2)
   {
      const float *l = d->pcm[0], *r = d->pcm[1];
      for (i = 0; i < RATRAC3_SAMPLES; i += 4)
      {
         r3v a = R3_LD(l + i), b = R3_LD(r + i);
         r3_st_s16x8(out + 2 * i, R3_ZIPLO(a, b), R3_ZIPHI(a, b));
      }
      return RATRAC3_SAMPLES;
   }
   if (nc == 1)
   {
      const float *p = d->pcm[0];
      for (i = 0; i < RATRAC3_SAMPLES; i += 8)
         r3_st_s16x8(out + i, R3_LD(p + i), R3_LD(p + i + 4));
      return RATRAC3_SAMPLES;
   }
#endif
   for (c = 0; c < nc; c++)
   {
      const float *p = d->pcm[c];
      for (i = 0; i < RATRAC3_SAMPLES; i++)
      {
         /* clamped, then rounded by truncating the value moved above
          * zero: selects and no branch on the sign */
         float v = p[i];
         v = v > 32767.0f ? 32767.0f : v;
         v = v < -32768.0f ? -32768.0f : v;
         out[i * nc + c] = (int16_t)((int)(v + 32768.5f) - 32768);
      }
   }
   return RATRAC3_SAMPLES;
}

/* ------------------------------------------------------------------ */

void ratrac3_reset(ratrac3_t *d)
{
   unsigned c, b;
   if (!d)
      return;
   memset(d->chan, 0, sizeof(d->chan));
   memset(d->qmf, 0, sizeof(d->qmf));
   for (c = 0; c < 2; c++)
      for (b = 0; b < RATRAC3_BANDS; b++)
      {
         d->chan[c].tail_zero[b] = true;
         d->js_sel[c][b]         = 3;
      }
   d->js_weight[0] = d->js_weight[1] = 0xFF;
}

ratrac3_t *ratrac3_new(unsigned channels, unsigned block_align,
      bool joint_stereo)
{
   ratrac3_t *d;
   if (channels < 1 || channels > 2 || !block_align
         || block_align % channels
         || (joint_stereo && channels != 2))
      return NULL;
   d = (ratrac3_t*)calloc(1, sizeof(*d));
   if (!d)
      return NULL;
   d->channels    = channels;
   d->block_align = block_align;
   d->joint       = joint_stereo;
   ratrac3_tables(d);
   ratrac3_reset(d);
   return d;
}

void ratrac3_free(ratrac3_t *d)
{
   free(d);
}

static unsigned ratrac3_le16(const uint8_t *p)
{
   return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t ratrac3_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ratrac3_parse_wave_fmt(const uint8_t *fmt, size_t len,
      ratrac3_info_t *info)
{
   unsigned extra;
   if (!fmt || !info || len < 16)
      return false;
   if (ratrac3_le16(fmt) != RATRAC3_FORMAT_TAG)
      return false;
   info->channels     = ratrac3_le16(fmt + 2);
   info->sample_rate  = ratrac3_le32(fmt + 4);
   info->bitrate      = ratrac3_le32(fmt + 8) * 8;
   info->block_align  = ratrac3_le16(fmt + 12);
   info->joint_stereo = false;
   extra              = len >= 18 ? ratrac3_le16(fmt + 16) : 0;
   /* The 14 bytes of extra data: a version, the samples a channel per
    * frame, then the coding mode twice; 1 is joint stereo */
   if (extra >= 8 && len >= 18 + 8)
      info->joint_stereo = ratrac3_le16(fmt + 18 + 6) == 1;
   if (info->channels < 1 || info->channels > 2 || !info->block_align
         || info->block_align % info->channels
         || (info->joint_stereo && info->channels != 2))
      return false;
   return true;
}
