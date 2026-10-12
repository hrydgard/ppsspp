/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (ratrac3p.c).
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

/* The signal path, block by block:
 *
 *  - a frame holds channel blocks, mono or stereo; in a stereo block
 *    the second channel ("slave") codes most parameters relative to the
 *    first ("master");
 *  - a block codes up to 32 quantisation units (QUs) over the 2048
 *    spectral coefficients: a word length (precision), a scale factor
 *    and a code table each, then the quantised values, Huffman-coded in
 *    16-coefficient chunks; QU groups may add a deterministic noise fill
 *    ("power compensation") on top;
 *  - the spectrum is 16 bands of 128 coefficients, odd bands
 *    frequency-inverted; each band goes through a 128-coefficient IMDCT
 *    with one of two window shapes a half, overlap-adds with the last
 *    frame, and gain points scale it as in ATRAC3;
 *  - sine waves ("tones") come as frequency, amplitude and phase a band
 *    and are added straight to the band signals;
 *  - a 16-band polyphase filter bank of 384 taps rebuilds the output.
 *
 * Every table and rule here was established by feeding a reference
 * decoder crafted or rewritten streams through its public interface and
 * reading off its return codes and output; none comes from its source. */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <retro_inline.h>
#include <formats/ratrac3p.h>

#include "ratrac3p_tables.h"

/* Four-float vectors: SSE2 or NEON where the target has them, plain C
 * otherwise or with RATRAC3P_NO_SIMD. */
#if defined(RATRAC3P_NO_SIMD)
#define R3P_SCALAR 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
typedef __m128 r3pv;
#define V_LD(p)          _mm_loadu_ps(p)
#define V_ST(p, v)       _mm_storeu_ps((p), (v))
#define V_ADD(a, b)      _mm_add_ps((a), (b))
#define V_SUB(a, b)      _mm_sub_ps((a), (b))
#define V_MUL(a, b)      _mm_mul_ps((a), (b))
#define V_SET1(x)        _mm_set1_ps(x)
#define V_ZERO()         _mm_setzero_ps()
#define V_ZIPLO(a, b)    _mm_unpacklo_ps((a), (b))
#define V_ZIPHI(a, b)    _mm_unpackhi_ps((a), (b))
#define V_REV(v)         _mm_shuffle_ps((v), (v), _MM_SHUFFLE(0, 1, 2, 3))
#define V_SET4(a,b,c,e)  _mm_setr_ps((a), (b), (c), (e))
#define V_TRANSPOSE(a, b, c, e) _MM_TRANSPOSE4_PS(a, b, c, e)
static INLINE void r3p_st_s16x8(int16_t *dst, r3pv a, r3pv b)
{
   const __m128 lo = _mm_set1_ps(-32768.0f), hi = _mm_set1_ps(32767.0f);
   a = _mm_min_ps(_mm_max_ps(a, lo), hi);
   b = _mm_min_ps(_mm_max_ps(b, lo), hi);
   _mm_storeu_si128((__m128i*)dst,
         _mm_packs_epi32(_mm_cvtps_epi32(a), _mm_cvtps_epi32(b)));
}
#define R3P_HAVE_S16 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
typedef float32x4_t r3pv;
#define V_LD(p)          vld1q_f32(p)
#define V_ST(p, v)       vst1q_f32((p), (v))
#define V_ADD(a, b)      vaddq_f32((a), (b))
#define V_SUB(a, b)      vsubq_f32((a), (b))
#define V_MUL(a, b)      vmulq_f32((a), (b))
#define V_SET1(x)        vdupq_n_f32(x)
#define V_ZERO()         vdupq_n_f32(0.0f)
static INLINE r3pv r3p_ziplo(r3pv a, r3pv b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[0];
}
static INLINE r3pv r3p_ziphi(r3pv a, r3pv b)
{
   float32x4x2_t z = vzipq_f32(a, b);
   return z.val[1];
}
#define V_ZIPLO(a, b)    r3p_ziplo((a), (b))
#define V_ZIPHI(a, b)    r3p_ziphi((a), (b))
static INLINE r3pv r3p_rev(r3pv v)
{
   v = vrev64q_f32(v);
   return vcombine_f32(vget_high_f32(v), vget_low_f32(v));
}
#define V_REV(v)         r3p_rev(v)
static INLINE r3pv r3p_set4(float a, float b, float c, float e)
{
   float t[4];
   t[0] = a; t[1] = b; t[2] = c; t[3] = e;
   return vld1q_f32(t);
}
#define V_SET4(a,b,c,e)  r3p_set4((a), (b), (c), (e))
#define V_TRANSPOSE(a, b, c, e) do { \
   float32x4x2_t t01_ = vtrnq_f32((a), (b)); \
   float32x4x2_t t23_ = vtrnq_f32((c), (e)); \
   (a) = vcombine_f32(vget_low_f32(t01_.val[0]), vget_low_f32(t23_.val[0])); \
   (b) = vcombine_f32(vget_low_f32(t01_.val[1]), vget_low_f32(t23_.val[1])); \
   (c) = vcombine_f32(vget_high_f32(t01_.val[0]), vget_high_f32(t23_.val[0])); \
   (e) = vcombine_f32(vget_high_f32(t01_.val[1]), vget_high_f32(t23_.val[1])); \
} while (0)
static INLINE int32x4_t r3p_round_s32(r3pv a)
{
#if defined(__aarch64__) || defined(_M_ARM64)
   return vcvtnq_s32_f32(a);
#else
   uint32x4_t neg  = vcltq_f32(a, vdupq_n_f32(0.0f));
   r3pv       half = vbslq_f32(neg, vdupq_n_f32(-0.5f), vdupq_n_f32(0.5f));
   return vcvtq_s32_f32(vaddq_f32(a, half));
#endif
}
static INLINE void r3p_st_s16x8(int16_t *dst, r3pv a, r3pv b)
{
   vst1q_s16(dst, vcombine_s16(vqmovn_s32(r3p_round_s32(a)),
            vqmovn_s32(r3p_round_s32(b))));
}
#define R3P_HAVE_S16 1
#else
#define R3P_SCALAR 1
#endif

#ifdef R3P_SCALAR
typedef struct { float f[4]; } r3pv;
static INLINE r3pv r3p_ld(const float *p)
{ r3pv v; v.f[0] = p[0]; v.f[1] = p[1]; v.f[2] = p[2]; v.f[3] = p[3]; return v; }
static INLINE void r3p_st(float *p, r3pv v)
{ p[0] = v.f[0]; p[1] = v.f[1]; p[2] = v.f[2]; p[3] = v.f[3]; }
static INLINE r3pv r3p_add(r3pv a, r3pv b)
{ a.f[0] += b.f[0]; a.f[1] += b.f[1]; a.f[2] += b.f[2]; a.f[3] += b.f[3]; return a; }
static INLINE r3pv r3p_sub(r3pv a, r3pv b)
{ a.f[0] -= b.f[0]; a.f[1] -= b.f[1]; a.f[2] -= b.f[2]; a.f[3] -= b.f[3]; return a; }
static INLINE r3pv r3p_mul(r3pv a, r3pv b)
{ a.f[0] *= b.f[0]; a.f[1] *= b.f[1]; a.f[2] *= b.f[2]; a.f[3] *= b.f[3]; return a; }
static INLINE r3pv r3p_set1(float x)
{ r3pv v; v.f[0] = v.f[1] = v.f[2] = v.f[3] = x; return v; }
static INLINE r3pv r3p_zip(r3pv a, r3pv b, unsigned o)
{ r3pv v; v.f[0] = a.f[o]; v.f[1] = b.f[o]; v.f[2] = a.f[o + 1]; v.f[3] = b.f[o + 1]; return v; }
#define V_LD(p)          r3p_ld(p)
#define V_ST(p, v)       r3p_st((p), (v))
#define V_ADD(a, b)      r3p_add((a), (b))
#define V_SUB(a, b)      r3p_sub((a), (b))
#define V_MUL(a, b)      r3p_mul((a), (b))
#define V_SET1(x)        r3p_set1(x)
#define V_ZERO()         r3p_set1(0.0f)
#define V_ZIPLO(a, b)    r3p_zip((a), (b), 0)
#define V_ZIPHI(a, b)    r3p_zip((a), (b), 2)
static INLINE r3pv r3p_rev(r3pv a)
{ r3pv v; v.f[0] = a.f[3]; v.f[1] = a.f[2]; v.f[2] = a.f[1]; v.f[3] = a.f[0]; return v; }
#define V_REV(v)         r3p_rev(v)
static INLINE r3pv r3p_set4(float a, float b, float c, float e)
{ r3pv v; v.f[0] = a; v.f[1] = b; v.f[2] = c; v.f[3] = e; return v; }
#define V_SET4(a,b,c,e)  r3p_set4((a), (b), (c), (e))
#define V_TRANSPOSE(a, b, c, e) do { \
   r3pv t0_ = r3p_set4((a).f[0], (b).f[0], (c).f[0], (e).f[0]); \
   r3pv t1_ = r3p_set4((a).f[1], (b).f[1], (c).f[1], (e).f[1]); \
   r3pv t2_ = r3p_set4((a).f[2], (b).f[2], (c).f[2], (e).f[2]); \
   r3pv t3_ = r3p_set4((a).f[3], (b).f[3], (c).f[3], (e).f[3]); \
   (a) = t0_; (b) = t1_; (c) = t2_; (e) = t3_; \
} while (0)
#endif

#define R3P_BANDS     16
#define R3P_BAND_LEN  128
#define R3P_SPEC_LEN  2048
#define R3P_MAX_TONES 48
#define R3P_GAIN_PTS  8
#define R3P_QMF_HIST  24

/* QU edges in coefficients */
static const uint16_t r3p_qu_start[33] = {
      0,   16,   32,   48,   64,   80,   96,  112,  128,  160,  192,
    224,  256,  288,  320,  352,  384,  448,  512,  576,  640,  704,
    768,  896, 1024, 1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048
};

/* Power compensation: the first QU of each group, and the level scale */
static const uint8_t r3p_comp_start[5] = { 2, 8, 16, 22, 27 };
static const uint8_t r3p_comp_lv[15] = {
   127, 101, 80, 64, 51, 40, 32, 25, 20, 16, 13, 10, 8, 6, 5
};

static const uint16_t *const r3p_vlc_wl[4] = {
   r3p_vlc_wl0, r3p_vlc_wl1, r3p_vlc_wl2, r3p_vlc_wl3
};
static const uint8_t r3p_vlc_wl_bits[4] = {
   R3P_VLC_WL0_BITS, R3P_VLC_WL1_BITS, R3P_VLC_WL2_BITS, R3P_VLC_WL3_BITS
};
static const uint16_t *const r3p_vlc_sfd[4] = {
   r3p_vlc_sfd0, r3p_vlc_sfd1, r3p_vlc_sfd2, r3p_vlc_sfd3
};
static const uint8_t r3p_vlc_sfd_bits[4] = {
   R3P_VLC_SFD0_BITS, R3P_VLC_SFD1_BITS, R3P_VLC_SFD2_BITS, R3P_VLC_SFD3_BITS
};
static const uint16_t *const r3p_vlc_sfr[4] = {
   r3p_vlc_sfr0, r3p_vlc_sfr1, r3p_vlc_sfr2, r3p_vlc_sfr3
};
static const uint8_t r3p_vlc_sfr_bits[4] = {
   R3P_VLC_SFR0_BITS, R3P_VLC_SFR1_BITS, R3P_VLC_SFR2_BITS, R3P_VLC_SFR3_BITS
};

/* ------------------------------------------------------------------ */
/* Parsed frame data */

typedef struct
{
   uint8_t nsb;                     /* subbands with gain data */
   uint8_t np[R3P_BANDS];           /* points a subband, 0..7 */
   uint8_t lev[R3P_BANDS][R3P_GAIN_PTS];
   uint8_t loc[R3P_BANDS][R3P_GAIN_PTS];
} r3p_gain_t;

typedef struct
{
   uint16_t freq;                   /* 0..1023, 1/2048 cycle a sample */
   uint8_t  amp;                    /* 0..63 */
   uint8_t  phase;                  /* 0..31, pi/16 steps */
} r3p_tone_t;

typedef struct
{
   uint8_t has_start, start, has_stop, stop;
   uint8_t first, count;            /* into the channel's tone list */
} r3p_tband_t;

typedef struct
{
   uint8_t     wl[32], sf[32], ct[32];
   uint8_t     copy[32];            /* slave QU takes the master's values */
   uint8_t     comp[5];
   uint8_t     shape[R3P_BANDS];    /* window shape, falling half */
   uint8_t     r1;
   uint8_t     tone_bands;          /* bands with tone data */
   uint8_t     ntones;
   r3p_gain_t  gain;
   r3p_tband_t tband[R3P_BANDS];
   r3p_tone_t  tone[R3P_MAX_TONES];
   int8_t      q[R3P_SPEC_LEN];     /* quantised values */
} r3p_unit_t;

typedef struct
{
   unsigned chans;                  /* 1 or 2 */
   unsigned ch0;                    /* first output channel */
   unsigned nqu, nu, nb;            /* coded, used QUs; bands processed */
   unsigned nbf;                    /* bands with flags: the coded QUs' */
   unsigned full;                   /* code tables: 8 or 4 entries */
   uint8_t  swap[R3P_BANDS], neg[R3P_BANDS];
} r3p_block_t;

/* A band's tone envelope over a region */
typedef struct
{
   uint8_t on;                      /* the tones that coded it sound */
   uint8_t has_start, start, has_stop, stop;
} r3p_tenv_t;

/* Signal history a channel */
typedef struct
{
   float   tail[R3P_BANDS][R3P_BAND_LEN];   /* IMDCT second halves */
   float   gcurve[R3P_BANDS][R3P_BAND_LEN]; /* last frame's gain curve */
   r3p_tband_t ptband[R3P_BANDS];           /* last frame's tones */
   r3p_tone_t  ptone[R3P_MAX_TONES];
   float   qmf[R3P_QMF_HIST - 1][32];       /* filter bank history */
   r3p_tenv_t tenv[R3P_BANDS];              /* last frame's tone envelopes */
   uint8_t gflat[R3P_BANDS];                /* gcurve is all ones */
   uint8_t gmin[R3P_BANDS];                 /* lowest level, last frame */
   uint8_t shape[R3P_BANDS];                /* last frame's window shapes */
} r3p_chan_t;

struct ratrac3p
{
   unsigned    channels;
   unsigned    block_align;
   unsigned    nblocks;
   r3p_block_t block[RATRAC3P_MAX_CHANNELS];
   r3p_unit_t  ttmp[2];                     /* stereo tones being parsed */
   r3p_unit_t *unit;                        /* a channel each */
   r3p_chan_t *chan;
   float      *pcm;                         /* RATRAC3P_SAMPLES a channel */

   /* work space */
   float       spec[2][R3P_SPEC_LEN];
   float       sub[R3P_BANDS][R3P_BAND_LEN];
   float       z[4][128];
   float       zr[64 * 4], zi[64 * 4];
   float       curve[R3P_BAND_LEN];

   /* tables */
   float       sf_scale[64];
   float       tone_amp[64];
   float       pow2q[128];                  /* 2^((i - 64) / 4) */
   float       sine[2048];                  /* sin(2 pi i / 2048) */
   float       tone_win[256];               /* sin^2(pi n / 256) */
   float       rise[2][R3P_BAND_LEN], fall[2][R3P_BAND_LEN];
   float       tw4[64 * 8];                 /* IMDCT twiddles, four of each */
   float       ftw4[32 * 8];                /* FFT twiddles, four of each */
   uint8_t     bitrev[64];
   float       dct4[R3P_BANDS][16];         /* filter bank cosines */
   float       qmf_hh[384];                 /* the prototype, signs folded */
   float       qv[R3P_QMF_HIST - 1 + R3P_BAND_LEN][32];

   uint8_t     *buf;
   size_t       buf_len;
};

/* ------------------------------------------------------------------ */
/* Bit reading, MSB first. The frame is copied to a buffer with four
 * zero bytes after it; reads past that see zeroes, and the frame is
 * rejected when its blocks end beyond its length. */

typedef struct
{
   const uint8_t *p;
   size_t         len;    /* bytes, the padding included */
   size_t         pos;    /* bits */
} r3p_br_t;

static INLINE uint32_t r3p_peek32(const r3p_br_t *b)
{
   size_t   i = b->pos >> 3;
   uint32_t v;
   if (i + 4 <= b->len)
   {
      const uint8_t *q = b->p + i;
      v = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16)
        | ((uint32_t)q[2] << 8)  |  (uint32_t)q[3];
   }
   else
   {
      unsigned k;
      v = 0;
      for (k = 0; k < 4; k++)
      {
         v <<= 8;
         if (i + k < b->len)
            v |= b->p[i + k];
      }
   }
   return v << (b->pos & 7);
}

/* n in 1..25 */
static INLINE unsigned r3p_peek(const r3p_br_t *b, unsigned n)
{
   return (unsigned)(r3p_peek32(b) >> (32 - n));
}

/* n in 0..25 */
static INLINE unsigned r3p_get(r3p_br_t *b, unsigned n)
{
   unsigned v;
   if (!n)
      return 0;
   v       = r3p_peek(b, n);
   b->pos += n;
   return v;
}

#define r3p_bit(b) r3p_get((b), 1)

/* A small VLC: a table of 2^bits entries, (length << 8) | value. Values
 * that are deltas come modulo 256; every use reduces them further. */
static INLINE unsigned r3p_vlc(r3p_br_t *b, const uint16_t *t,
      unsigned bits)
{
   unsigned e = t[r3p_peek(b, bits)];
   b->pos += e >> 8;
   return e & 0xFF;
}

#define R3P_VLC(b, name, NAME) \
   r3p_vlc((b), r3p_vlc_##name, R3P_VLC_##NAME##_BITS)

/* Bits needed for x */
static INLINE unsigned r3p_bitlen(unsigned x)
{
   unsigned n = 0;
   while (x)
   {
      n++;
      x >>= 1;
   }
   return n;
}

/* ------------------------------------------------------------------ */
/* Word lengths */

typedef struct
{
   unsigned mode, nc, spd;
} r3p_trim_t;

static bool r3p_trim(r3p_br_t *b, unsigned nqu, r3p_trim_t *t)
{
   t->mode = r3p_get(b, 2);
   t->nc   = nqu;
   t->spd  = 0;
   if (t->mode)
   {
      t->nc = r3p_get(b, 5);
      if (t->nc > nqu)
         return false;
   }
   if (t->mode == 3)
      t->spd = r3p_get(b, 2);
   return true;
}

static void r3p_trim_fill(r3p_br_t *b, const r3p_trim_t *t,
      unsigned nqu, bool slave, uint8_t *wl)
{
   unsigned i;
   int      split;
   switch (t->mode)
   {
      case 1:
         for (i = t->nc; i < nqu; i++)
            wl[i] = 0;
         break;
      case 2:
         for (i = t->nc; i < nqu; i++)
            wl[i] = slave ? (uint8_t)r3p_bit(b) : 1;
         break;
      case 3:
         split = slave ? (int)(t->nc + t->spd + 3)
                       : (int)nqu - (int)t->spd - 1;
         for (i = t->nc; i < nqu; i++)
            wl[i] = (int)i < split ? 1 : 0;
         break;
      default:
         break;
   }
}

/* Shape tables run in QU groups of three, the last one five long */
#define R3P_GRP(i) ((i) < 27 ? (i) / 3 : 9)

static bool r3p_wl_weights(r3p_unit_t *u, unsigned nqu, unsigned ch,
      unsigned widx)
{
   unsigned i;
   if (widx)
   {
      const uint8_t *w = r3p_wl_weight + (ch * 3 + widx - 1) * 32;
      for (i = 0; i < nqu; i++)
         u->wl[i] = (uint8_t)(u->wl[i] + w[i]);
   }
   for (i = 0; i < nqu; i++)
      if (u->wl[i] > 7)
         return false;
   return true;
}

/* Each value a VLC delta to the one before; mode 3 of either channel,
 * each with its own weights and trim rules */
static bool r3p_wl_delta(r3p_br_t *b, r3p_unit_t *u, unsigned nqu,
      unsigned ch)
{
   unsigned   i, sel, widx = r3p_get(b, 2);
   r3p_trim_t t;
   uint8_t   *wl = u->wl;
   if (!r3p_trim(b, nqu, &t))
      return false;
   if (t.nc)
   {
      sel   = r3p_get(b, 2);
      wl[0] = (uint8_t)r3p_get(b, 3);
      for (i = 1; i < t.nc; i++)
         wl[i] = (uint8_t)((wl[i - 1]
                  + r3p_vlc(b, r3p_vlc_wl[sel], r3p_vlc_wl_bits[sel])) & 7);
   }
   r3p_trim_fill(b, &t, nqu, ch != 0, wl);
   return r3p_wl_weights(u, nqu, ch, widx);
}

static bool r3p_wl_master(r3p_br_t *b, r3p_unit_t *u, unsigned nqu)
{
   unsigned   i, widx = 0, sel;
   r3p_trim_t t;
   uint8_t   *wl = u->wl;

   switch (r3p_get(b, 2))
   {
      case 0:
         for (i = 0; i < nqu; i++)
            wl[i] = (uint8_t)r3p_get(b, 3);
         break;
      case 1:
         {
            unsigned nd, db, mn;
            widx = r3p_get(b, 2);
            if (!r3p_trim(b, nqu, &t))
               return false;
            nd = r3p_get(b, 5);
            db = r3p_get(b, 2);
            mn = r3p_get(b, 3);
            if (nd > t.nc)
               return false;
            for (i = 0; i < nd; i++)
               wl[i] = (uint8_t)r3p_get(b, 3);
            for (; i < t.nc; i++)
               wl[i] = (uint8_t)(mn + r3p_get(b, db));
            r3p_trim_fill(b, &t, nqu, false, wl);
         }
         break;
      case 2:
         {
            unsigned       grp, st, ix;
            const uint8_t *shape;
            const uint16_t *tab;
            unsigned       bits;
            if (!r3p_trim(b, nqu, &t))
               return false;
            grp   = r3p_bit(b);
            sel   = r3p_bit(b);
            st    = r3p_get(b, 3);
            ix    = r3p_get(b, 4);
            shape = r3p_wl_shape + (st * 16 + ix) * 10;
            tab   = r3p_vlc_wl[sel];
            bits  = r3p_vlc_wl_bits[sel];
            i     = 0;
            while (i < t.nc)
            {
               if (grp && i + 1 < t.nc)
               {
                  /* a set flag: both residuals zero */
                  unsigned r0 = 0, r1 = 0;
                  if (!r3p_bit(b))
                  {
                     r0 = r3p_vlc(b, tab, bits);
                     r1 = r3p_vlc(b, tab, bits);
                  }
                  wl[i]     = (uint8_t)((shape[R3P_GRP(i)] + r0) & 7);
                  wl[i + 1] = (uint8_t)((shape[R3P_GRP(i + 1)] + r1) & 7);
                  i        += 2;
               }
               else
               {
                  wl[i] = (uint8_t)((shape[R3P_GRP(i)]
                           + r3p_vlc(b, tab, bits)) & 7);
                  i++;
               }
            }
            r3p_trim_fill(b, &t, nqu, false, wl);
         }
         break;
      default:
         return r3p_wl_delta(b, u, nqu, 0);
   }
   return r3p_wl_weights(u, nqu, 0, widx);
}

static bool r3p_wl_slave(r3p_br_t *b, r3p_unit_t *u, const uint8_t *m,
      unsigned nqu)
{
   unsigned   i, mode = r3p_get(b, 2), sel, acc = 0;
   r3p_trim_t t;
   uint8_t   *wl = u->wl;

   if (mode == 0)
   {
      for (i = 0; i < nqu; i++)
         wl[i] = (uint8_t)r3p_get(b, 3);
      return true;
   }
   if (mode == 3)
      return r3p_wl_delta(b, u, nqu, 1);
   if (!r3p_trim(b, nqu, &t))
      return false;
   /* the table selector comes only with coded values */
   sel = t.nc ? r3p_get(b, 2) : 0;
   for (i = 0; i < t.nc; i++)
   {
      unsigned d = r3p_vlc(b, r3p_vlc_wl[sel], r3p_vlc_wl_bits[sel]);
      if (mode == 1)
         wl[i] = (uint8_t)((m[i] + d) & 7);
      else
      {
         acc   = (acc + d) & 7;
         wl[i] = (uint8_t)((m[i] + acc) & 7);
      }
   }
   r3p_trim_fill(b, &t, nqu, true, wl);
   return true;
}

/* ------------------------------------------------------------------ */
/* Scale factors, nu of them in every channel of the block */

static bool r3p_sf_weight(uint8_t *sf, unsigned nu, unsigned widx)
{
   unsigned i;
   for (i = 0; i < nu; i++)
   {
      int v = sf[i] - (int)(widx == 1 ? i / 2 : i / 3);
      if (v < 0)
         return false;
      sf[i] = (uint8_t)v;
   }
   return true;
}

/* Mode 3, either channel: each value a VLC delta to the one before */
static bool r3p_sf_delta(r3p_br_t *b, r3p_unit_t *u, unsigned nu)
{
   unsigned i, widx, sel;
   uint8_t *sf = u->sf;

   widx = r3p_get(b, 2);
   sel  = r3p_get(b, 2);
   if (widx == 3)
   {
      /* a shape plus residuals, the first offset by 8, the rest
       * VLC deltas to the one before */
      unsigned       base  = r3p_get(b, 6), res;
      const uint8_t *shape = r3p_sf_shape + r3p_get(b, 6) * 10;
      res = r3p_get(b, 4);
      for (i = 0; i < nu; i++)
      {
         if (i)
            res += r3p_vlc(b, r3p_vlc_sfr[sel], r3p_vlc_sfr_bits[sel]);
         sf[i] = (uint8_t)((base + shape[R3P_GRP(i)] + res - 8) & 63);
      }
      return true;
   }
   if (nu)
   {
      sf[0] = (uint8_t)r3p_get(b, 6);
      for (i = 1; i < nu; i++)
         sf[i] = (uint8_t)((sf[i - 1]
                  + r3p_vlc(b, r3p_vlc_sfd[sel], r3p_vlc_sfd_bits[sel]))
               & 63);
   }
   if (widx && !r3p_sf_weight(sf, nu, widx))
      return false;
   return true;
}

static bool r3p_sf_master(r3p_br_t *b, r3p_unit_t *u, unsigned nu)
{
   unsigned i, widx, sel;
   uint8_t *sf = u->sf;

   switch (r3p_get(b, 2))
   {
      case 0:
         for (i = 0; i < nu; i++)
            sf[i] = (uint8_t)r3p_get(b, 6);
         break;
      case 1:
         {
            unsigned nd, db, mn, v;
            widx = r3p_get(b, 2);
            if (widx == 3)
            {
               /* a shape like mode 2's, plus residuals offset by 7:
                * nd of them 4 bits each, the rest from a minimum */
               unsigned       base  = r3p_get(b, 6);
               const uint8_t *shape = r3p_sf_shape + r3p_get(b, 6) * 10;
               nd = r3p_get(b, 5);
               db = r3p_get(b, 2);
               mn = r3p_get(b, 4);
               for (i = 0; i < nu; i++)
               {
                  v     = i < nd ? r3p_get(b, 4) : mn + r3p_get(b, db);
                  sf[i] = (uint8_t)((base + shape[R3P_GRP(i)] + v - 7) & 63);
               }
               for (; i < nd; i++)
                  r3p_get(b, 4);
               break;
            }
            nd = r3p_get(b, 5);
            db = r3p_get(b, 3);
            mn = r3p_get(b, 6);
            if (nd > nu)
               return false;
            for (i = 0; i < nd; i++)
               sf[i] = (uint8_t)r3p_get(b, 6);
            for (; i < nu; i++)
            {
               v = mn + r3p_get(b, db);
               if (v > 63)
                  return false;
               sf[i] = (uint8_t)v;
            }
            if (widx && !r3p_sf_weight(sf, nu, widx))
               return false;
         }
         break;
      case 2:
         {
            unsigned       base;
            const uint8_t *shape;
            sel   = r3p_get(b, 2);
            base  = r3p_get(b, 6);
            shape = r3p_sf_shape + r3p_get(b, 6) * 10;
            for (i = 0; i < nu; i++)
               sf[i] = (uint8_t)((base + shape[R3P_GRP(i)]
                        + r3p_vlc(b, r3p_vlc_sfr[sel], r3p_vlc_sfr_bits[sel]))
                     & 63);
         }
         break;
      default:
         return r3p_sf_delta(b, u, nu);
   }
   return true;
}

static bool r3p_sf_slave(r3p_br_t *b, r3p_unit_t *u, const uint8_t *m,
      unsigned nu)
{
   unsigned i, mode = r3p_get(b, 2), sel, acc = 0;
   uint8_t *sf = u->sf;

   if (mode == 0)
   {
      for (i = 0; i < nu; i++)
         sf[i] = (uint8_t)r3p_get(b, 6);
      return true;
   }
   if (mode == 3)
   {
      /* the master's values, no bits */
      memcpy(sf, m, nu);
      return true;
   }
   sel = r3p_get(b, 2);
   for (i = 0; i < nu; i++)
   {
      unsigned d = r3p_vlc(b, r3p_vlc_sfd[sel], r3p_vlc_sfd_bits[sel]);
      if (mode == 1)
         sf[i] = (uint8_t)((m[i] + d) & 63);
      else
      {
         acc   = (acc + d) & 63;
         sf[i] = (uint8_t)((m[i] + acc) & 63);
      }
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* Code tables */

static INLINE unsigned r3p_ct_value(r3p_br_t *b, unsigned full)
{
   return full ? R3P_VLC(b, ctv1, CTV1) : R3P_VLC(b, ctv0, CTV0);
}

static INLINE unsigned r3p_ct_delta(r3p_br_t *b, unsigned full)
{
   return full ? R3P_VLC(b, ctd1, CTD1) : R3P_VLC(b, ctd0, CTD0);
}

static bool r3p_ct(r3p_br_t *b, r3p_block_t *blk, r3p_unit_t *u,
      const r3p_unit_t *m)
{
   unsigned i, mode, num, n, mask, prev = 0;

   if (!m)
      blk->full = r3p_bit(b);
   u->r1 = (uint8_t)r3p_bit(b);
   mode  = r3p_get(b, 2);
   num   = blk->nu;
   if (r3p_bit(b))
   {
      num = r3p_get(b, 5);
      if (num > blk->nqu)
         return false;
   }
   if (mode == 3 && !m)
      return false;
   mask = blk->full ? 7 : 3;
   n    = num < blk->nu ? num : blk->nu;
   memset(u->ct, 0, sizeof(u->ct));
   if (m)
      for (i = 0; i < blk->nu; i++)
         u->copy[i] = (uint8_t)(!u->wl[i] && m->wl[i]);
   for (i = 0; i < n; i++)
   {
      unsigned v;
      if (!u->wl[i])
      {
         /* slave: a QU only the master codes has a flag, set to stay
          * silent instead of copying the master's values */
         if (m && m->wl[i] && r3p_bit(b))
            u->copy[i] = 0;
         continue;
      }
      switch (mode)
      {
         case 0:
            v = r3p_get(b, blk->full ? 3 : 2);
            break;
         case 1:
            v = r3p_ct_value(b, blk->full);
            break;
         case 2:
            /* QU 0 takes a value, every later QU a delta to the last
             * coded entry (0 when there is none yet) */
            v = i ? (prev + r3p_ct_delta(b, blk->full)) & mask
                  : r3p_ct_value(b, blk->full);
            break;
         default:
            v = (m->ct[i] + (blk->full ? R3P_VLC(b, cts1, CTS1)
                     : R3P_VLC(b, cts0, CTS0))) & mask;
            break;
      }
      u->ct[i]  = (uint8_t)v;
      prev      = v;
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* Spectra */

static INLINE void r3p_group(r3p_br_t *b, const struct r3p_cb_desc *cb,
      int8_t *q)
{
   unsigned k;
   uint32_t e = r3p_cb_lut[cb->prim + r3p_peek(b, cb->pbits)];
   if (!(e >> 25))
   {
      unsigned sb = (e >> 16) & 0xFF;
      b->pos     += cb->pbits;
      e           = r3p_cb_lut[cb->sec + (e & 0xFFFF) + r3p_peek(b, sb)];
      b->pos     += (e >> 25) - cb->pbits;
   }
   else
      b->pos += e >> 25;
   for (k = 0; k < cb->g; k++)
   {
      int v = (int)((e >> (6 * k)) & 63);
      v     = (v ^ 32) - 32;
      q[k]  = (int8_t)v;
   }
   if (e & (1u << 24))
      for (k = 0; k < cb->g; k++)
         if (q[k] && r3p_bit(b))
            q[k] = (int8_t)-q[k];
}

static void r3p_qu_spectrum(r3p_br_t *b, const struct r3p_cb_desc *cb,
      int8_t *q, unsigned n)
{
   unsigned c, f, j;
   for (c = 0; c < n; c += 16, q += 16)
   {
      if (cb->nflags)
      {
         unsigned span = 16 / cb->nflags;
         for (f = 0; f < cb->nflags; f++)
         {
            int8_t *p = q + f * span;
            if (!r3p_bit(b))
            {
               memset(p, 0, span);
               continue;
            }
            for (j = 0; j < span; j += cb->g)
               r3p_group(b, cb, p + j);
         }
      }
      else
         for (j = 0; j < 16; j += cb->g)
            r3p_group(b, cb, q + j);
   }
}

static bool r3p_spectra(r3p_br_t *b, const r3p_block_t *blk, r3p_unit_t *u)
{
   unsigned i;
   for (i = 0; i < blk->nu; i++)
   {
      unsigned lo = r3p_qu_start[i], n = r3p_qu_start[i + 1] - lo;
      unsigned w  = u->wl[i], idx;
      if (!w)
         continue;
      idx = r3p_cb_map[((((w - 1) * 2 + blk->full) * 2 + u->r1) * 8)
         + u->ct[i]];
      if (idx >= R3P_NUM_CB)
         return false;
      r3p_qu_spectrum(b, &r3p_cb[idx], u->q + lo, n);
   }
   for (i = 0; i < 5; i++)
      u->comp[i] = (uint8_t)(blk->nu > r3p_comp_start[i]
            ? r3p_get(b, 4) : 15);
   return true;
}

/* Per-band flags: [any][explicit][a bit a band] */
static void r3p_band_flags(r3p_br_t *b, unsigned nb, uint8_t *out)
{
   unsigned i;
   memset(out, 0, R3P_BANDS);
   if (!r3p_bit(b))
      return;
   if (!r3p_bit(b))
   {
      memset(out, 1, nb);
      return;
   }
   for (i = 0; i < nb; i++)
      out[i] = (uint8_t)r3p_bit(b);
}

/* ------------------------------------------------------------------ */
/* Gain points */

static bool r3p_method_c(r3p_br_t *b, unsigned minbits, unsigned max,
      uint8_t *out, unsigned n)
{
   unsigned i, db = r3p_get(b, 2), mn = r3p_get(b, minbits);
   for (i = 0; i < n; i++)
   {
      unsigned v = mn + r3p_get(b, db);
      if (v > max)
         return false;
      out[i] = (uint8_t)v;
   }
   return true;
}

/* Ascending locations: each from the room left above the last one */
static unsigned r3p_loc_d(r3p_br_t *b, unsigned prev)
{
   unsigned nb = r3p_bitlen(prev < 30 ? 30 - prev : 0);
   if (nb == 5)
      return r3p_get(b, 5);
   return prev + r3p_get(b, nb) + 1;
}

static INLINE unsigned r3p_loc_delta(r3p_br_t *b, const uint8_t *lev,
      unsigned k)
{
   return lev[k] > lev[k - 1] ? R3P_VLC(b, loc1, LOC1)
                              : R3P_VLC(b, loc1b, LOC1B);
}

static void r3p_gain_dup(r3p_gain_t *g, unsigned jv)
{
   /* the rows copied to all lie past the last coded one (nsb >= 1) */
   unsigned       s;
   const uint8_t *lev, *loc;
   if (jv < g->nsb)
      return;
   lev = g->lev[g->nsb - 1];
   loc = g->loc[g->nsb - 1];
   for (s = g->nsb; s <= jv; s++)
   {
      g->np[s] = g->np[g->nsb - 1];
      memcpy(g->lev[s], lev, R3P_GAIN_PTS);
      memcpy(g->loc[s], loc, R3P_GAIN_PTS);
   }
   g->nsb = (uint8_t)(jv + 1);
}

static bool r3p_gain_master(r3p_br_t *b, r3p_gain_t *g)
{
   unsigned s, k, nsb, mode, jv = 0;
   bool     j0;

   memset(g, 0, sizeof(*g));
   if (!r3p_bit(b))
      return true;
   nsb = r3p_get(b, 4) + 1;
   j0  = r3p_bit(b) != 0;
   if (j0)
      jv = r3p_get(b, 4);
   g->nsb = (uint8_t)nsb;

   switch (r3p_get(b, 2))
   {
      case 0:
         for (s = 0; s < nsb; s++)
            g->np[s] = (uint8_t)r3p_get(b, 3);
         break;
      case 1:
         for (s = 0; s < nsb; s++)
            g->np[s] = (uint8_t)R3P_VLC(b, npv, NPV);
         break;
      case 2:
         for (s = 0; s < nsb; s++)
            g->np[s] = (uint8_t)(s ? (g->np[s - 1] + R3P_VLC(b, npd, NPD)) & 7
                  : R3P_VLC(b, npv, NPV));
         break;
      default:
         if (!r3p_method_c(b, 3, 7, g->np, nsb))
            return false;
         break;
   }

   mode = r3p_get(b, 2);
   if (mode == 3)
   {
      uint8_t  all[R3P_BANDS * R3P_GAIN_PTS];
      unsigned n = 0, j = 0;
      for (s = 0; s < nsb; s++)
         n += g->np[s];
      if (!r3p_method_c(b, 4, 15, all, n))
         return false;
      for (s = 0; s < nsb; s++)
         for (k = 0; k < g->np[s]; k++)
            g->lev[s][k] = all[j++];
   }
   else
      for (s = 0; s < nsb; s++)
         for (k = 0; k < g->np[s]; k++)
         {
            unsigned v;
            if (mode == 0)
               v = r3p_get(b, 4);
            else if (mode == 2 && s)
               v = ((k < g->np[s - 1] ? g->lev[s - 1][k] : 7)
                     + R3P_VLC(b, lv3, LV3)) & 15;
            else if (k)
               v = (g->lev[s][k - 1] + R3P_VLC(b, lv2, LV2)) & 15;
            else
               v = R3P_VLC(b, lv1, LV1);
            g->lev[s][k] = (uint8_t)v;
         }

   mode = r3p_get(b, 2);
   if (mode == 3)
   {
      /* from a common minimum, each point one further on than the
       * last: min + delta + index; the sum may run past 31 */
      unsigned db = r3p_get(b, 2) + 1, mn = r3p_get(b, 5);
      for (s = 0; s < nsb; s++)
         for (k = 0; k < g->np[s]; k++)
            g->loc[s][k] = (uint8_t)(mn + r3p_get(b, db) + k);
   }
   else
      for (s = 0; s < nsb; s++)
         for (k = 0; k < g->np[s]; k++)
         {
            unsigned v;
            if (mode == 2 && s && (k < g->np[s - 1] || !k))
            {
               /* a delta to the previous subband's point, from its own
                * table where the level rises from the point before */
               unsigned d = k && g->lev[s][k] > g->lev[s][k - 1]
                  ? R3P_VLC(b, loc2u, LOC2U) : R3P_VLC(b, loc2, LOC2);
               v = ((k < g->np[s - 1] ? g->loc[s - 1][k] : 0) + d) & 31;
            }
            else if ((mode == 1 || (mode == 2 && s)) && k)
               v = g->loc[s][k - 1] + r3p_loc_delta(b, g->lev[s], k);
            else if (!k)
               v = r3p_get(b, 5);
            else
               v = r3p_loc_d(b, g->loc[s][k - 1]);
            g->loc[s][k] = (uint8_t)v;
         }

   if (j0)
      r3p_gain_dup(g, jv);
   return true;
}

static bool r3p_gain_slave(r3p_br_t *b, r3p_gain_t *g, const r3p_gain_t *m)
{
   unsigned s, k, nsb, mode, jv = 0, flag = 0;
   bool     j0;

   memset(g, 0, sizeof(*g));
   if (!r3p_bit(b))
      return true;
   nsb = r3p_get(b, 4) + 1;
   j0  = r3p_bit(b) != 0;
   if (j0)
      jv = r3p_get(b, 4);
   g->nsb = (uint8_t)nsb;

#define M_NP(s)     ((s) < m->nsb ? m->np[s] : 0)
#define M_HAS(s, k) ((s) < m->nsb && (k) < m->np[s])
#define M_LEV(s, k) (M_HAS(s, k) ? m->lev[s][k] : 7)
#define M_LOC(s, k) (M_HAS(s, k) ? m->loc[s][k] : 0)

   mode = r3p_get(b, 2);
   for (s = 0; s < nsb; s++)
   {
      unsigned v;
      switch (mode)
      {
         case 0:  v = r3p_get(b, 3); break;
         case 1:  v = R3P_VLC(b, npv, NPV); break;
         case 2:  v = (M_NP(s) + R3P_VLC(b, npd, NPD)) & 7; break;
         default: v = M_NP(s); break;
      }
      g->np[s] = (uint8_t)v;
   }

   mode = r3p_get(b, 2);
   for (s = 0; s < nsb; s++)
      for (k = 0; k < g->np[s]; k++)
      {
         unsigned v;
         switch (mode)
         {
            case 0:
               v = r3p_get(b, 4);
               break;
            case 1:
               v = (M_LEV(s, k) + R3P_VLC(b, slv1, SLV1)) & 15;
               break;
            case 2:
               if (!k)
                  flag = r3p_bit(b);
               if (!flag)
                  v = M_LEV(s, k);
               else if (!k)
                  v = R3P_VLC(b, lv1, LV1);
               else
                  v = (g->lev[s][k - 1] + R3P_VLC(b, lv2, LV2)) & 15;
               break;
            default:
               v = M_LEV(s, k);
               break;
         }
         g->lev[s][k] = (uint8_t)v;
      }

   /* locations: points the master lacks are coded on their own, after
    * the first as deltas to the previous point (modes 1, 2) or ascending
    * (modes 0, 3); in mode 2 a subband with more points than the
    * master's has no copy flag */
   mode = r3p_get(b, 2);
   for (s = 0; s < nsb; s++)
      for (k = 0; k < g->np[s]; k++)
      {
         unsigned v;
         if (mode == 2 && !k)
            flag = g->np[s] > M_NP(s) ? 1 : r3p_bit(b);
         if (mode == 1 && M_HAS(s, k) && k
               && g->lev[s][k] > g->lev[s][k - 1])
            /* where the level rises: the master's, or ascending */
            v = r3p_bit(b) ? r3p_loc_d(b, g->loc[s][k - 1]) : m->loc[s][k];
         else if (mode == 1 && (M_HAS(s, k) || !k))
            v = (M_LOC(s, k) + R3P_VLC(b, sloc1, SLOC1)) & 31;
         else if ((mode == 3 || (mode == 2 && !flag)) && M_HAS(s, k))
            v = m->loc[s][k];
         else if (!k)
            v = r3p_get(b, 5);
         else if (mode == 1 || (mode == 2 && flag))
            v = g->loc[s][k - 1] + r3p_loc_delta(b, g->lev[s], k);
         else
            v = r3p_loc_d(b, g->loc[s][k - 1]);
         g->loc[s][k] = (uint8_t)v;
      }

#undef M_NP
#undef M_HAS
#undef M_LEV
#undef M_LOC

   if (j0)
      r3p_gain_dup(g, jv);
   return true;
}

/* ------------------------------------------------------------------ */
/* Tones */

/* Tone data comes for a list of bands: the first channel's for all of
 * them, the second channel's for those it does not share. */

static void r3p_tone_envelopes(r3p_br_t *b, r3p_unit_t *u,
      const uint8_t *list, unsigned n)
{
   unsigned i;
   for (i = 0; i < n; i++)
   {
      r3p_tband_t *t = &u->tband[list[i]];
      t->has_start = (uint8_t)r3p_bit(b);
      t->start     = (uint8_t)(t->has_start ? r3p_get(b, 5) : 0);
      t->has_stop  = (uint8_t)r3p_bit(b);
      t->stop      = (uint8_t)(t->has_stop ? r3p_get(b, 5) : 0);
   }
}

static bool r3p_tone_counts(r3p_br_t *b, r3p_unit_t *u,
      const uint8_t *list, unsigned n)
{
   unsigned i, total = u->ntones, unary = r3p_bit(b);
   for (i = 0; i < n; i++)
   {
      unsigned k;
      if (!unary)
         k = r3p_get(b, 4);
      else
         for (k = 0; k <= R3P_MAX_TONES && r3p_bit(b); k++);
      u->tband[list[i]].first = (uint8_t)total;
      u->tband[list[i]].count = (uint8_t)k;
      total                  += k;
      if (total > R3P_MAX_TONES)
         return false;
   }
   u->ntones = (uint8_t)total;
   return true;
}

/* A band's frequencies: the first 10 bits, the rest each in the room
 * left above (ascending) or below (descending) the one before */
static void r3p_tone_freqs(r3p_br_t *b, r3p_tone_t *t, unsigned n)
{
   unsigned k, desc = n >= 2 ? r3p_bit(b) : 0, prev = r3p_get(b, 10);
   t[0].freq = (uint16_t)prev;
   for (k = 1; k < n; k++)
   {
      unsigned nb;
      if (!desc)
      {
         nb   = r3p_bitlen(1023 - prev);
         prev = 1024 - (1u << nb) + r3p_get(b, nb);
      }
      else
      {
         nb   = r3p_bitlen(prev);
         prev = r3p_get(b, nb);
      }
      t[k].freq = (uint16_t)prev;
   }
   if (desc)
      for (k = 0; k < n / 2; k++)
      {
         uint16_t x        = t[k].freq;
         t[k].freq         = t[n - 1 - k].freq;
         t[n - 1 - k].freq = x;
      }
}

/* A count's delta to the first channel's:
 * 0, 10, 110, 1110, 1111xx = 0, 1, -1, 2, then 3, -4, -3, -2 */
static int r3p_count_delta(r3p_br_t *b)
{
   static const int8_t tail[4] = { 3, -4, -3, -2 };
   if (!r3p_bit(b))
      return 0;
   if (!r3p_bit(b))
      return 1;
   if (!r3p_bit(b))
      return -1;
   if (!r3p_bit(b))
      return 2;
   return tail[r3p_get(b, 2)];
}

/* Amplitudes, then phases, of tones t0..t1 */
static void r3p_tone_amps(r3p_br_t *b, r3p_unit_t *u, unsigned t0,
      unsigned t1)
{
   unsigned k, vlc = r3p_bit(b);
   for (k = t0; k < t1; k++)
      u->tone[k].amp = (uint8_t)(vlc ? R3P_VLC(b, ghaa, GHAA)
            : r3p_get(b, 6));
   for (k = t0; k < t1; k++)
      u->tone[k].phase = (uint8_t)r3p_get(b, 5);
}

static bool r3p_tones_mono(r3p_br_t *b, r3p_unit_t *u, const uint8_t *list,
      unsigned n)
{
   unsigned i;
   r3p_tone_envelopes(b, u, list, n);
   if (!r3p_tone_counts(b, u, list, n))
      return false;
   for (i = 0; i < n; i++)
      if (u->tband[list[i]].count)
         r3p_tone_freqs(b, u->tone + u->tband[list[i]].first,
               u->tband[list[i]].count);
   r3p_tone_amps(b, u, 0, u->ntones);
   return true;
}

/* The second channel, for the bands in the list: its envelopes or the
 * first channel's, its counts, its frequencies on their own or as
 * deltas to the first channel's, its amplitudes on their own or as
 * deltas, its phases */
static bool r3p_tones_second(r3p_br_t *b, const r3p_unit_t *m,
      r3p_unit_t *u, const uint8_t *list, unsigned n)
{
   unsigned i, k, lead, rel, same;

   if (r3p_bit(b))
      for (i = 0; i < n; i++)
      {
         u->tband[list[i]].has_start = m->tband[list[i]].has_start;
         u->tband[list[i]].start     = m->tband[list[i]].start;
         u->tband[list[i]].has_stop  = m->tband[list[i]].has_stop;
         u->tband[list[i]].stop      = m->tband[list[i]].stop;
      }
   else
      r3p_tone_envelopes(b, u, list, n);
   if (!r3p_bit(b))
   {
      if (!r3p_tone_counts(b, u, list, n))
         return false;
   }
   else
   {
      /* the first channel's counts, all of them or each with a delta */
      unsigned same = r3p_bit(b), total = u->ntones;
      for (i = 0; i < n; i++)
      {
         unsigned c = m->tband[list[i]].count;
         if (!same)
            c = (c + (unsigned)r3p_count_delta(b)) & 15;
         u->tband[list[i]].first = (uint8_t)total;
         u->tband[list[i]].count = (uint8_t)c;
         total                  += c;
         if (total > R3P_MAX_TONES)
            return false;
      }
      u->ntones = (uint8_t)total;
   }
   lead = r3p_bit(b);
   for (i = 0; i < n; i++)
   {
      const r3p_tband_t *mb = &m->tband[list[i]];
      const r3p_tband_t *tb = &u->tband[list[i]];
      r3p_tone_t        *t  = u->tone + tb->first;
      if (!tb->count)
         continue;
      if (!lead)
      {
         r3p_tone_freqs(b, t, tb->count);
         continue;
      }
      /* tones past the first channel's take its band's last one, or 0 */
      for (k = 0; k < tb->count; k++)
      {
         unsigned f = !mb->count ? 0
            : m->tone[mb->first + (k < mb->count ? k : mb->count - 1u)].freq;
         if (r3p_bit(b))
            f += (unsigned)(int8_t)R3P_VLC(b, ghafd, GHAFD);
         t[k].freq = (uint16_t)(f & 1023);
      }
   }
   rel = r3p_bit(b);
   if (!rel)
   {
      r3p_tone_amps(b, u, 0, u->ntones);
      return true;
   }
   /* the first channel's amplitudes, each with a delta or as they are:
    * of its tone nearest in frequency, within 7, else of the one in the
    * same place; tones past its own start from 34 or 32 */
   same = r3p_bit(b);
   for (i = 0; i < n; i++)
   {
      const r3p_tband_t *mb = &m->tband[list[i]];
      const r3p_tband_t *tb = &u->tband[list[i]];
      for (k = 0; k < tb->count; k++)
      {
         const r3p_tone_t *mt = m->tone + mb->first;
         unsigned f = u->tone[tb->first + k].freq, a, j, best = 8;
         int      at = k < mb->count ? (int)k : -1;
         for (j = 0; j < mb->count; j++)
         {
            unsigned dist = f > mt[j].freq ? f - mt[j].freq : mt[j].freq - f;
            if (dist < best)
            {
               best = dist;
               at   = (int)j;
            }
         }
         a = at >= 0 ? mt[at].amp : same ? 32u : 34u;
         if (!same)
            a += (unsigned)R3P_VLC(b, ghaad, GHAAD);
         u->tone[tb->first + k].amp = (uint8_t)(a & 63);
      }
   }
   for (k = 0; k < u->ntones; k++)
      u->tone[k].phase = (uint8_t)r3p_get(b, 5);
   return true;
}

/* Moves band b's tones of src to the end of dst's list; false when the
 * list would overflow (swaps can pile both channels' tones on one) */
static bool r3p_tone_take(r3p_unit_t *dst, const r3p_unit_t *src,
      unsigned b, uint8_t *count)
{
   const r3p_tband_t *s = &src->tband[b];
   r3p_tband_t       *t = &dst->tband[b];
   unsigned           k;
   if (*count + s->count > R3P_MAX_TONES)
      return false;
   t->has_start = s->has_start;
   t->start     = s->start;
   t->has_stop  = s->has_stop;
   t->stop      = s->stop;
   t->first     = *count;
   t->count     = s->count;
   for (k = 0; k < s->count; k++)
      dst->tone[(*count)++] = src->tone[s->first + k];
   return true;
}

static bool r3p_tones(r3p_br_t *b, const r3p_block_t *blk, r3p_unit_t *u,
      r3p_unit_t *tmp)
{
   unsigned nbands, c, i, n1 = 0, s0, s1;
   uint8_t  all[R3P_BANDS], list[R3P_BANDS], share[R3P_BANDS];
   uint8_t  swap[R3P_BANDS], neg[R3P_BANDS];

   for (c = 0; c < blk->chans; c++)
   {
      u[c].tone_bands = 0;
      u[c].ntones     = 0;
   }
   if (!r3p_bit(b))
      return true;
   if (!r3p_bit(b))
      return false;            /* the other amplitude mode: unknown */
   nbands = R3P_VLC(b, ghan, GHAN);
   if (nbands > R3P_BANDS)
      return false;
   for (i = 0; i < nbands; i++)
      all[i] = (uint8_t)i;

   if (blk->chans == 1)
   {
      u[0].tone_bands = (uint8_t)nbands;
      return r3p_tones_mono(b, &u[0], all, nbands);
   }

   /* bands the second channel takes from the first: all, some, none */
   s0 = r3p_bit(b);
   s1 = s0 ? r3p_bit(b) : 0;
   for (i = 0; i < nbands; i++)
      share[i] = (uint8_t)(s0 && (!s1 || r3p_bit(b)));
   r3p_band_flags(b, nbands, swap);
   r3p_band_flags(b, nbands, neg);

   tmp[0].ntones = tmp[1].ntones = 0;
   if (!r3p_tones_mono(b, &tmp[0], all, nbands))
      return false;
   for (i = 0; i < nbands; i++)
      if (!share[i])
         list[n1++] = (uint8_t)i;
   memset(tmp[1].tband, 0, sizeof(tmp[1].tband));
   if (!(s0 && !s1) && !r3p_tones_second(b, &tmp[0], &tmp[1], list, n1))
      return false;

   /* swapped bands trade places, then the second channel's negated
    * bands turn half a cycle */
   for (c = 0; c < 2; c++)
   {
      uint8_t count = 0;
      for (i = 0; i < nbands; i++)
      {
         const r3p_unit_t *src = (c ^ swap[i]) ? &tmp[1] : &tmp[0];
         if (share[i] && src == &tmp[1])
            src = &tmp[0];
         if (!r3p_tone_take(&u[c], src, i, &count))
            return false;
         if (c && neg[i])
         {
            unsigned k;
            for (k = 0; k < u[c].tband[i].count; k++)
               u[c].tone[u[c].tband[i].first + k].phase =
                  (uint8_t)((u[c].tone[u[c].tband[i].first + k].phase + 16) & 31);
         }
      }
      u[c].ntones     = count;
      u[c].tone_bands = (uint8_t)nbands;
   }
   return true;
}

/* ------------------------------------------------------------------ */
/* Block parsing */

static bool r3p_parse_block(r3p_br_t *b, r3p_block_t *blk, r3p_unit_t *u,
      r3p_unit_t *tmp)
{
   unsigned i, c, used[2] = { 0, 0 }, nqu;

   nqu = r3p_get(b, 5) + 1;
   if (nqu > 28 && nqu < 32)
      return false;
   r3p_bit(b);                 /* meaning unknown; no effect found */
   blk->nqu = nqu;

   for (c = 0; c < blk->chans; c++)
   {
      memset(u[c].wl, 0, sizeof(u[c].wl));
      memset(u[c].sf, 0, sizeof(u[c].sf));
      memset(u[c].copy, 0, sizeof(u[c].copy));
      if (!(c ? r3p_wl_slave(b, &u[1], u[0].wl, nqu)
              : r3p_wl_master(b, &u[0], nqu)))
         return false;
      for (i = 0; i < nqu; i++)
         if (u[c].wl[i])
            used[c] = i + 1;
   }
   blk->nu = used[0] > used[1] ? used[0] : used[1];
   blk->nb  = r3p_qu_start[nqu - 1] / R3P_BAND_LEN + 1;
   blk->nbf = r3p_qu_start[nqu - 1] / R3P_BAND_LEN + 1;

   /* with nothing coded, no scale factors, tables or spectra either */
   for (c = 0; blk->nu && c < blk->chans; c++)
      if (!(c ? r3p_sf_slave(b, &u[1], u[0].sf, blk->nu)
              : r3p_sf_master(b, &u[0], blk->nu)))
         return false;
   for (c = 0; blk->nu && c < blk->chans; c++)
      if (!r3p_ct(b, blk, &u[c], c ? &u[0] : NULL))
         return false;
   for (c = 0; c < blk->chans; c++)
   {
      if (!blk->nu)
      {
         memset(u[c].q, 0, sizeof(u[c].q));
         memset(u[c].comp, 15, sizeof(u[c].comp));
      }
      else if (!r3p_spectra(b, blk, &u[c]))
         return false;
   }

   memset(blk->swap, 0, sizeof(blk->swap));
   memset(blk->neg, 0, sizeof(blk->neg));
   if (blk->chans == 2)
   {
      r3p_band_flags(b, blk->nbf, blk->swap);
      r3p_band_flags(b, blk->nbf, blk->neg);
   }
   for (c = 0; c < blk->chans; c++)
      r3p_band_flags(b, blk->nbf, u[c].shape);
   if (!r3p_gain_master(b, &u[0].gain))
      return false;
   if (blk->chans == 2 && !r3p_gain_slave(b, &u[1].gain, &u[0].gain))
      return false;
   if (!r3p_tones(b, blk, u, tmp))
      return false;
   /* noise: a flag, then a level and a table selector; no effect on
    * the output found */
   if (r3p_bit(b))
      r3p_get(b, 8);
   return true;
}

static bool r3p_parse_frame(ratrac3p_t *d, const uint8_t *frame,
      size_t len)
{
   r3p_br_t b;
   unsigned ch = 0;

   if (len < d->block_align)
      return false;
   memcpy(d->buf, frame, d->block_align);
   memset(d->buf + d->block_align, 0, d->buf_len - d->block_align);
   b.p   = d->buf;
   b.len = d->buf_len;
   b.pos = 0;

   if (r3p_bit(&b))
      return false;
   d->nblocks = 0;
   while (ch < d->channels)
   {
      r3p_block_t *blk;
      unsigned     type = r3p_get(&b, 2);
      if (type == 3)
         break;
      if (type == 2)
         return false;         /* extension block: unknown length */
      blk        = &d->block[d->nblocks];
      blk->chans = type + 1;
      blk->ch0   = ch;
      if (ch + blk->chans > d->channels)
         return false;
      if (!r3p_parse_block(&b, blk, &d->unit[ch], d->ttmp))
         return false;
      if (b.pos > (size_t)d->block_align * 8)
         return false;
      ch += blk->chans;
      d->nblocks++;
   }
   return ch == d->channels;
}

/* ------------------------------------------------------------------ */
/* Dequantisation */

/* p[k] = q[k] * s for a run of 16 values */
static INLINE void r3p_scale16(const int8_t *q, float *p, float s)
{
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#ifndef RATRAC3P_NO_SIMD
   __m128i v  = _mm_loadu_si128((const __m128i*)q);
   __m128i lo = _mm_srai_epi16(_mm_unpacklo_epi8(v, v), 8);
   __m128i hi = _mm_srai_epi16(_mm_unpackhi_epi8(v, v), 8);
   __m128  vs = _mm_set1_ps(s);
   _mm_storeu_ps(p,      _mm_mul_ps(_mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(lo, lo), 16)), vs));
   _mm_storeu_ps(p + 4,  _mm_mul_ps(_mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpackhi_epi16(lo, lo), 16)), vs));
   _mm_storeu_ps(p + 8,  _mm_mul_ps(_mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(hi, hi), 16)), vs));
   _mm_storeu_ps(p + 12, _mm_mul_ps(_mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpackhi_epi16(hi, hi), 16)), vs));
   return;
#endif
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)) && !defined(RATRAC3P_NO_SIMD)
   int8x16_t   v  = vld1q_s8(q);
   int16x8_t   lo = vmovl_s8(vget_low_s8(v)), hi = vmovl_s8(vget_high_s8(v));
   float32x4_t vs = vdupq_n_f32(s);
   vst1q_f32(p,      vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(lo))), vs));
   vst1q_f32(p + 4,  vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(lo))), vs));
   vst1q_f32(p + 8,  vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(hi))), vs));
   vst1q_f32(p + 12, vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(hi))), vs));
   return;
#endif
   {
      unsigned k;
      for (k = 0; k < 16; k++)
         p[k] = (float)q[k] * s;
   }
}

/* Largest gain of a band's points, at least 1 */
static unsigned r3p_gain_lmin(const r3p_gain_t *g, unsigned band)
{
   unsigned k, lmin = 6;
   if (band < g->nsb)
      for (k = 0; k < g->np[band]; k++)
         if (g->lev[band][k] < lmin)
            lmin = g->lev[band][k];
   return lmin;
}

/* The noise fill never rises above the signal the gain points let
 * through: it is scaled down by the largest gain it will meet, this
 * frame's own or last frame's times this frame's first point */
static float r3p_noise_att(const ratrac3p_t *d, const r3p_gain_t *g,
      const r3p_chan_t *c, unsigned band)
{
   int e  = 6 - (int)r3p_gain_lmin(g, band);          /* log2 M_t */
   int e0 = (band < g->nsb && g->np[band])
      ? 6 - (int)g->lev[band][0] : 0;
   int ep = e0 + (6 - (int)c->gmin[band]);            /* G_t[0] M_{t-1} */
   if (ep > e)
      e = ep;
   return e > 0 ? d->pow2q[64 - 4 * e] : 1.0f;
}

static void r3p_dequant(const ratrac3p_t *d, const r3p_block_t *blk,
      const r3p_unit_t *u, const r3p_unit_t *m, const r3p_chan_t *c,
      unsigned seed, float *x)
{
   unsigned i, k, g = 0;
   memset(x, 0, R3P_SPEC_LEN * sizeof(float));
   for (i = 0; i < blk->nu; i++)
   {
      unsigned      lo = r3p_qu_start[i], n = r3p_qu_start[i + 1] - lo;
      unsigned      w  = u->wl[i];
      const int8_t *q  = u->q + lo;
      float         sc, *p = x + lo;
      while (g < 5 && i >= r3p_comp_start[g])
         g++;
      if (!w)
      {
         if (!u->copy[i])
            continue;
         w = m->wl[i];
         q = m->q + lo;
      }
      sc = d->sf_scale[u->sf[i]];
      for (k = 0; k < n; k += 16)
         r3p_scale16(q + k, p + k, sc * r3p_unit[w]);
      {
         unsigned cl = g ? u->comp[g - 1] : 15;
      if (cl != 15)
      {
         /* noise from a fixed table, read from the sum of the block's
          * scale factors on; QUs of one band read the same stretch */
         unsigned base = (seed & ~3u) + (lo & ~127u);
         unsigned band = lo >> 7;
         float    s    = sc * r3p_unit[w] / (float)(1u << (w - 1))
            * (float)r3p_comp_lv[cl] * (1.0f / 127.0f);
         s *= r3p_noise_att(d, &u->gain, c, band);
         base &= 1023;
         if (base + n <= 1024)
         {
            r3pv vs = V_SET1(s);
            for (k = 0; k < n; k += 4)
               V_ST(p + k, V_ADD(V_LD(p + k),
                        V_MUL(V_LD(r3p_noise + base + k), vs)));
         }
         else
            for (k = 0; k < n; k++)
               p[k] += r3p_noise[(base + k) & 1023] * s;
      }
      }
   }
}

/* ------------------------------------------------------------------ */
/* IMDCT, four bands at once (one a lane): 128 coefficients to 256
 * samples, x[n] = sum X[k] cos(pi/128 (n + 64.5)(k + 0.5)). A DCT-IV z
 * of the coefficients through a 64-point complex FFT gives them all:
 * x = z[64..127], -z[127..0], -z[0..63]. Odd bands come
 * frequency-inverted, which swaps the real and imaginary inputs. Leaves
 * each band's z in d->z. */

static void r3p_imdct4(ratrac3p_t *d, const float *x)
{
   unsigned k, len;
   float   *zr = d->zr, *zi = d->zi;
   const float *x0 = x, *x1 = x + 128, *x2 = x + 256, *x3 = x + 384;

   for (k = 0; k < 64; k++)
   {
      unsigned e  = 2 * k, o = 127 - 2 * k, j = 4 * d->bitrev[k];
      r3pv     re = V_SET4(x0[e], x1[o], x2[e], x3[o]);
      r3pv     im = V_SET4(x0[o], x1[e], x2[o], x3[e]);
      r3pv     cr = V_LD(d->tw4 + 8 * k), ci = V_LD(d->tw4 + 8 * k + 4);
      V_ST(zr + j, V_SUB(V_MUL(re, cr), V_MUL(im, ci)));
      V_ST(zi + j, V_ADD(V_MUL(re, ci), V_MUL(im, cr)));
   }

   /* the first two passes together: twiddles 1 and -i */
   for (k = 0; k < 64; k += 4)
   {
      float *pr = zr + 4 * k, *pi = zi + 4 * k;
      r3pv a0r = V_LD(pr),      a0i = V_LD(pi);
      r3pv a1r = V_LD(pr + 4),  a1i = V_LD(pi + 4);
      r3pv a2r = V_LD(pr + 8),  a2i = V_LD(pi + 8);
      r3pv a3r = V_LD(pr + 12), a3i = V_LD(pi + 12);
      r3pv b0r = V_ADD(a0r, a1r), b0i = V_ADD(a0i, a1i);
      r3pv b1r = V_SUB(a0r, a1r), b1i = V_SUB(a0i, a1i);
      r3pv b2r = V_ADD(a2r, a3r), b2i = V_ADD(a2i, a3i);
      r3pv b3r = V_SUB(a2r, a3r), b3i = V_SUB(a2i, a3i);
      V_ST(pr,      V_ADD(b0r, b2r)); V_ST(pi,      V_ADD(b0i, b2i));
      V_ST(pr + 8,  V_SUB(b0r, b2r)); V_ST(pi + 8,  V_SUB(b0i, b2i));
      V_ST(pr + 4,  V_ADD(b1r, b3i)); V_ST(pi + 4,  V_SUB(b1i, b3r));
      V_ST(pr + 12, V_SUB(b1r, b3i)); V_ST(pi + 12, V_ADD(b1i, b3r));
   }

   for (len = 8; len <= 64; len <<= 1)
   {
      unsigned half = len >> 1, step = 64 / len, s;
      for (s = 0; s < 64; s += len)
         for (k = 0; k < half; k++)
         {
            float *ar = zr + 4 * (s + k),        *ai = zi + 4 * (s + k);
            float *br = zr + 4 * (s + k + half), *bi = zi + 4 * (s + k + half);
            r3pv wr  = V_LD(d->ftw4 + 8 * k * step);
            r3pv wi  = V_LD(d->ftw4 + 8 * k * step + 4);
            r3pv vbr = V_LD(br), vbi = V_LD(bi);
            r3pv var = V_LD(ar), vai = V_LD(ai);
            r3pv tr  = V_SUB(V_MUL(vbr, wr), V_MUL(vbi, wi));
            r3pv ti  = V_ADD(V_MUL(vbr, wi), V_MUL(vbi, wr));
            V_ST(br, V_SUB(var, tr));
            V_ST(bi, V_SUB(vai, ti));
            V_ST(ar, V_ADD(var, tr));
            V_ST(ai, V_ADD(vai, ti));
         }
   }

   /* post-twiddle: z[2k] = Re, z[127 - 2k] = -Im, kept lane by lane */
   for (k = 0; k < 64; k++)
   {
      r3pv cr = V_LD(d->tw4 + 8 * k), ci = V_LD(d->tw4 + 8 * k + 4);
      r3pv vr = V_LD(zr + 4 * k), vi = V_LD(zi + 4 * k);
      V_ST(zr + 4 * k, V_SUB(V_MUL(vr, cr), V_MUL(vi, ci)));
      V_ST(zi + 4 * k, V_SUB(V_ZERO(), V_ADD(V_MUL(vr, ci), V_MUL(vi, cr))));
   }
   /* four k at a time: transposed, the evens z[2k] come from zr[k], the
    * odds z[2k + 1] = z[127 - 2(63 - k)] from zi[63 - k] */
   for (k = 0; k < 64; k += 4)
   {
      r3pv e0 = V_LD(zr + 4 * k),       e1 = V_LD(zr + 4 * k + 4);
      r3pv e2 = V_LD(zr + 4 * k + 8),   e3 = V_LD(zr + 4 * k + 12);
      r3pv o0 = V_LD(zi + 4 * (63 - k)), o1 = V_LD(zi + 4 * (62 - k));
      r3pv o2 = V_LD(zi + 4 * (61 - k)), o3 = V_LD(zi + 4 * (60 - k));
      V_TRANSPOSE(e0, e1, e2, e3);
      V_TRANSPOSE(o0, o1, o2, o3);
      V_ST(d->z[0] + 2 * k, V_ZIPLO(e0, o0)); V_ST(d->z[0] + 2 * k + 4, V_ZIPHI(e0, o0));
      V_ST(d->z[1] + 2 * k, V_ZIPLO(e1, o1)); V_ST(d->z[1] + 2 * k + 4, V_ZIPHI(e1, o1));
      V_ST(d->z[2] + 2 * k, V_ZIPLO(e2, o2)); V_ST(d->z[2] + 2 * k + 4, V_ZIPHI(e2, o2));
      V_ST(d->z[3] + 2 * k, V_ZIPLO(e3, o3)); V_ST(d->z[3] + 2 * k + 4, V_ZIPHI(e3, o3));
   }
}

/* ------------------------------------------------------------------ */
/* Gain curve of one band, 128 samples; returns false when flat */

static bool r3p_gain_curve(const ratrac3p_t *d, const r3p_gain_t *g,
      unsigned band, float *c)
{
   unsigned k, pos = 0, np = band < g->nsb ? g->np[band] : 0;
   if (!np)
      return false;
   for (k = 0; k < np; k++)
   {
      int      l   = g->lev[band][k];
      int      ln  = k + 1 < np ? g->lev[band][k + 1] : 6;
      unsigned end = 4u * g->loc[band][k], j;
      while (pos <= end && pos < R3P_BAND_LEN)
         c[pos++] = d->pow2q[64 + 4 * (6 - l)];
      for (j = 1; j < 4; j++)
         if (end + j < R3P_BAND_LEN)
            c[end + j] = d->pow2q[64 + 4 * (6 - l) + (l - ln) * (int)j];
      if (pos < end + 4)
         pos = end + 4;
   }
   for (; pos < R3P_BAND_LEN; pos++)
      c[pos] = 1.0f;
   return true;
}

/* ------------------------------------------------------------------ */
/* Tone synthesis into the band signal (first half) and the overlap */

/* A band's tone envelope over one region: off before a start point and
 * on after it (a three-sample ramp between), on or off as the region
 * began without one, then on up to a stop point and off after it */
/* No tones last frame: the envelope is on throughout */
static void r3p_tenv_reset(r3p_tenv_t *v)
{
   memset(v, 0, sizeof(*v));
   v->on = 1;
}

static float r3p_tone_env(const r3p_tenv_t *v, unsigned n)
{
   static const float up[3]   = { 0.14644661f, 0.5f, 0.85355339f };
   static const float down[3] = { 0.85355339f, 0.5f, 0.14644661f };
   float    x;
   unsigned s = 4u * v->start, e = 4u * v->stop;
   if (v->has_start && v->start)
      x = n <= s ? 0.0f : (n - s <= 3 ? up[n - s - 1] : 1.0f);
   else
      x = (v->has_start || v->on) ? 1.0f : 0.0f;
   if (v->has_stop && n >= e)
      x = n - e < 3 ? x * down[n - e] : 0.0f;
   return x;
}

/* One tone over the region, under a weight a sample */
static void r3p_tone_add(const ratrac3p_t *d, const r3p_tone_t *t,
      unsigned n0, const float *wt, float *out)
{
   float    a  = d->tone_amp[t->amp];
   unsigned ph = (unsigned)(64 * t->phase + t->freq * n0 - 128 * t->freq)
      & 2047, n;
   for (n = 0; n < R3P_BAND_LEN; n++)
   {
      out[n] += a * wt[n] * d->sine[ph];
      ph      = (ph + t->freq) & 2047;
   }
}

/* A band's tones over one region, made when the frame coding the next
 * one is in: each frame's tones span the region before its own and its
 * own, under a sin^2 window and the region's envelope, which the frame
 * before coded. Last frame's tones fall under the window; held instead
 * up to their stop point unless this frame's carry on into the region.
 * This frame's rise under it, unless they start in their own region. */
static void r3p_tones_band(const ratrac3p_t *d, const r3p_unit_t *u,
      r3p_chan_t *c, unsigned band, bool coded, unsigned count, float *out)
{
   r3p_tenv_t *env = &c->tenv[band];
   unsigned    k, n, pn = c->ptband[band].count;
   float       wt[R3P_BAND_LEN];

   if (pn)
   {
      bool hold = env->has_stop && !(count && !u->tband[band].has_start);
      for (n = 0; n < R3P_BAND_LEN; n++)
         wt[n] = (hold ? 1.0f : d->tone_win[128 + n]) * r3p_tone_env(env, n);
      for (k = 0; k < pn; k++)
         r3p_tone_add(d, &c->ptone[c->ptband[band].first + k], 128, wt, out);
   }
   if (count && !u->tband[band].has_start)
   {
      r3p_tenv_t rise = *env;
      rise.on = 1;
      for (n = 0; n < R3P_BAND_LEN; n++)
         wt[n] = d->tone_win[n] * r3p_tone_env(&rise, n);
      for (k = 0; k < count; k++)
         r3p_tone_add(d, &u->tone[u->tband[band].first + k], 0, wt, out);
   }

   /* this frame's own region gets this frame's envelope, even with
    * no tones in the band */
   if (coded)
   {
      /* tones that last frame stopped stay silent in this frame's own
       * region unless they start again */
      const r3p_tband_t *tb = &u->tband[band];
      env->on        = (uint8_t)(tb->has_start || !env->has_stop);
      env->has_start = tb->has_start;
      env->start     = tb->start;
      env->has_stop  = tb->has_stop;
      env->stop      = tb->stop;
   }
   else
      r3p_tenv_reset(env);
}

/* Keeps this frame's tones for the next region */
static void r3p_tones_keep(const r3p_unit_t *u, const r3p_block_t *blk,
      r3p_chan_t *c)
{
   unsigned b;
   memcpy(c->ptone, u->tone, sizeof(c->ptone));
   for (b = 0; b < R3P_BANDS; b++)
   {
      c->ptband[b] = u->tband[b];
      if (b >= u->tone_bands || b >= blk->nb)
         c->ptband[b].count = 0;
   }
}

/* ------------------------------------------------------------------ */
/* Polyphase synthesis: 16 band signals of 128 to 2048 samples, through
 * filters FB_b[t] = h[t] cos((2b+1) pi (t - 7.5) / 32), t < 384.
 *
 * For a band sample time j let W[n] = sum_b s_b[j] cos((2b+1)(2n+1) pi
 * / 64), n < 16 (a DCT-IV). The cosine of FB_b at t = 16 i + r is then
 * +-W at a mirrored index, the mirror and sign depending on r and on
 * i mod 4 only: with A = W7..W0 W0..W7 and B = W8..W15 W15..W8,
 *    y[16 j + r] = sum_{i < 24} hh[i][r] (i even ? A : B)_{j - i}[r]
 * where hh folds the signs into h. */

static void r3p_qmf(ratrac3p_t *d, r3p_chan_t *c, unsigned nbands,
      float *out)
{
   unsigned j, b, i;
   float   *row = d->qv[0];

   memcpy(row, c->qmf, sizeof(c->qmf));
   for (j = 0; j < R3P_BAND_LEN; j++)
   {
      r3pv  w0 = V_ZERO(), w1 = V_ZERO(), w2 = V_ZERO(), w3 = V_ZERO();
      float *ab = d->qv[R3P_QMF_HIST - 1 + j];
      for (b = 0; b < nbands; b++)
      {
         r3pv         x  = V_SET1(d->sub[b][j]);
         const float *cb = d->dct4[b];
         w0 = V_ADD(w0, V_MUL(x, V_LD(cb)));
         w1 = V_ADD(w1, V_MUL(x, V_LD(cb + 4)));
         w2 = V_ADD(w2, V_MUL(x, V_LD(cb + 8)));
         w3 = V_ADD(w3, V_MUL(x, V_LD(cb + 12)));
      }
      V_ST(ab,      V_REV(w1));
      V_ST(ab + 4,  V_REV(w0));
      V_ST(ab + 8,  w0);
      V_ST(ab + 12, w1);
      V_ST(ab + 16, w2);
      V_ST(ab + 20, w3);
      V_ST(ab + 24, V_REV(w3));
      V_ST(ab + 28, V_REV(w2));
   }

   for (j = 0; j < R3P_BAND_LEN; j++)
   {
      r3pv         a0 = V_ZERO(), a1 = V_ZERO(), a2 = V_ZERO(), a3 = V_ZERO();
      const float *h  = d->qmf_hh;
      const float *v  = d->qv[R3P_QMF_HIST - 1 + j];
      for (i = 0; i < R3P_QMF_HIST; i += 2, h += 32, v -= 64)
      {
         a0 = V_ADD(a0, V_MUL(V_LD(h),      V_LD(v)));
         a1 = V_ADD(a1, V_MUL(V_LD(h + 4),  V_LD(v + 4)));
         a2 = V_ADD(a2, V_MUL(V_LD(h + 8),  V_LD(v + 8)));
         a3 = V_ADD(a3, V_MUL(V_LD(h + 12), V_LD(v + 12)));
         a0 = V_ADD(a0, V_MUL(V_LD(h + 16), V_LD(v - 16)));
         a1 = V_ADD(a1, V_MUL(V_LD(h + 20), V_LD(v - 12)));
         a2 = V_ADD(a2, V_MUL(V_LD(h + 24), V_LD(v - 8)));
         a3 = V_ADD(a3, V_MUL(V_LD(h + 28), V_LD(v - 4)));
      }
      V_ST(out + 16 * j,      a0);
      V_ST(out + 16 * j + 4,  a1);
      V_ST(out + 16 * j + 8,  a2);
      V_ST(out + 16 * j + 12, a3);
   }
   memcpy(c->qmf, d->qv[R3P_BAND_LEN], sizeof(c->qmf));
}

/* ------------------------------------------------------------------ */
/* One channel's synthesis */

static void r3p_synth(ratrac3p_t *d, const r3p_block_t *blk,
      const r3p_unit_t *u, r3p_chan_t *c, const float *x, float *pcm)
{
   unsigned b, n, g;

   for (g = 0; g < R3P_BANDS; g += 4)
   {
      unsigned l;
      if (g < blk->nb)
         r3p_imdct4(d, x + g * R3P_BAND_LEN);
      for (l = 0; l < 4; l++)
      {
         float       *s    = d->sub[g + l];
         float       *tail = c->tail[g + l];
         const float *z    = d->z[l];
         const float *ri   = d->rise[c->shape[g + l]];
         const float *fa   = d->fall[u->shape[g + l]];
         float        g0;
         bool         flat;
         b    = g + l;
         flat = !r3p_gain_curve(d, &u->gain, b, d->curve);
         g0   = flat ? 1.0f : d->curve[0];
         if (b < blk->nb)
         {
            /* head: z[64..127] then -z[127..64]; tail: -z[63..0], then
             * -z[0..63] */
            r3pv vg = V_SET1(g0), ng = V_SET1(-g0), m1 = V_SET1(-1.0f);
            for (n = 0; n < 64; n += 4)
            {
               V_ST(s + n, V_ADD(V_MUL(V_MUL(V_LD(z + 64 + n), V_LD(ri + n)), vg),
                        V_LD(tail + n)));
               V_ST(s + 64 + n, V_ADD(V_MUL(V_MUL(V_REV(V_LD(z + 124 - n)),
                              V_LD(ri + 64 + n)), ng), V_LD(tail + 64 + n)));
            }
            if (!c->gflat[b])
               for (n = 0; n < R3P_BAND_LEN; n += 4)
                  V_ST(s + n, V_MUL(V_LD(s + n), V_LD(c->gcurve[b] + n)));
            for (n = 0; n < 64; n += 4)
            {
               V_ST(tail + n, V_MUL(V_MUL(V_REV(V_LD(z + 60 - n)), V_LD(fa + n)), m1));
               V_ST(tail + 64 + n, V_MUL(V_MUL(V_LD(z + n), V_LD(fa + 64 + n)), m1));
            }
         }
         else
         {
            memset(s, 0, R3P_BAND_LEN * sizeof(float));
            memset(tail, 0, R3P_BAND_LEN * sizeof(float));
         }
         if (!flat)
            memcpy(c->gcurve[b], d->curve, sizeof(d->curve));
         c->gmin[b] = (uint8_t)r3p_gain_lmin(&u->gain, b);
         c->gflat[b] = (uint8_t)flat;
         c->shape[b] = u->shape[b];

         r3p_tones_band(d, u, c, b, b < u->tone_bands && b < blk->nb,
               b < u->tone_bands && b < blk->nb ? u->tband[b].count : 0, s);
      }
   }
   r3p_tones_keep(u, blk, c);
   r3p_qmf(d, c, blk->nb, pcm);
}

static void r3p_synth_block(ratrac3p_t *d, const r3p_block_t *blk)
{
   unsigned     c, i, b, seed = 0;
   r3p_unit_t  *u = &d->unit[blk->ch0];

   for (c = 0; c < blk->chans; c++)
      for (i = 0; i < blk->nu; i++)
         seed += u[c].sf[i];
   for (c = 0; c < blk->chans; c++)
      r3p_dequant(d, blk, &u[c], &u[0], &d->chan[blk->ch0 + c], seed,
            d->spec[c]);
   if (blk->chans == 2)
      for (b = 0; b < blk->nb; b++)
      {
         float *l = d->spec[0] + b * R3P_BAND_LEN;
         float *r = d->spec[1] + b * R3P_BAND_LEN;
         if (blk->swap[b])
            for (i = 0; i < R3P_BAND_LEN; i++)
            {
               float t = l[i];
               l[i]    = r[i];
               r[i]    = t;
            }
         if (blk->neg[b])
            for (i = 0; i < R3P_BAND_LEN; i++)
               r[i] = -r[i];
      }
   for (c = 0; c < blk->chans; c++)
      r3p_synth(d, blk, &u[c], &d->chan[blk->ch0 + c], d->spec[c],
            d->pcm + (blk->ch0 + c) * RATRAC3P_SAMPLES);
}

static bool r3p_frame(ratrac3p_t *d, const uint8_t *frame, size_t len)
{
   unsigned i;
   if (!frame || !r3p_parse_frame(d, frame, len))
      return false;
   for (i = 0; i < d->nblocks; i++)
      r3p_synth_block(d, &d->block[i]);
   return true;
}

/* ------------------------------------------------------------------ */

int ratrac3p_decode_float(ratrac3p_t *d, const uint8_t *frame, size_t len,
      float *out)
{
   unsigned i, c, nc;
   if (!d || !out)
      return -1;
   nc = d->channels;
   if (!r3p_frame(d, frame, len))
   {
      memset(out, 0, RATRAC3P_SAMPLES * nc * sizeof(float));
      return -1;
   }
   if (nc == 2)
   {
      const float *l = d->pcm, *r = d->pcm + RATRAC3P_SAMPLES;
      for (i = 0; i < RATRAC3P_SAMPLES; i += 4)
      {
         r3pv a = V_LD(l + i), b = V_LD(r + i);
         V_ST(out + 2 * i,     V_ZIPLO(a, b));
         V_ST(out + 2 * i + 4, V_ZIPHI(a, b));
      }
   }
   else
      for (c = 0; c < nc; c++)
      {
         const float *p = d->pcm + c * RATRAC3P_SAMPLES;
         for (i = 0; i < RATRAC3P_SAMPLES; i++)
            out[i * nc + c] = p[i];
      }
   return RATRAC3P_SAMPLES;
}

int ratrac3p_decode_s16(ratrac3p_t *d, const uint8_t *frame, size_t len,
      int16_t *out)
{
   unsigned i, c, nc;
   if (!d || !out)
      return -1;
   nc = d->channels;
   if (!r3p_frame(d, frame, len))
   {
      memset(out, 0, RATRAC3P_SAMPLES * nc * sizeof(int16_t));
      return -1;
   }
#ifdef R3P_HAVE_S16
   {
      r3pv k = V_SET1(32768.0f);
      if (nc == 2)
      {
         const float *l = d->pcm, *r = d->pcm + RATRAC3P_SAMPLES;
         for (i = 0; i < RATRAC3P_SAMPLES; i += 4)
         {
            r3pv a = V_MUL(V_LD(l + i), k), b = V_MUL(V_LD(r + i), k);
            r3p_st_s16x8(out + 2 * i, V_ZIPLO(a, b), V_ZIPHI(a, b));
         }
         return RATRAC3P_SAMPLES;
      }
      if (nc == 1)
      {
         const float *p = d->pcm;
         for (i = 0; i < RATRAC3P_SAMPLES; i += 8)
            r3p_st_s16x8(out + i, V_MUL(V_LD(p + i), k),
                  V_MUL(V_LD(p + i + 4), k));
         return RATRAC3P_SAMPLES;
      }
   }
#endif
   for (c = 0; c < nc; c++)
   {
      const float *p = d->pcm + c * RATRAC3P_SAMPLES;
      for (i = 0; i < RATRAC3P_SAMPLES; i++)
      {
         float v = p[i] * 32768.0f;
         v = v > 32767.0f ? 32767.0f : v;
         v = v < -32768.0f ? -32768.0f : v;
         out[i * nc + c] = (int16_t)((int)(v + 32768.5f) - 32768);
      }
   }
   return RATRAC3P_SAMPLES;
}

/* ------------------------------------------------------------------ */

static void r3p_tables(ratrac3p_t *d)
{
   const double pi = 3.14159265358979323846;
   unsigned     i, j, b;

   for (i = 0; i < 64; i++)
   {
      d->sf_scale[i] = (float)pow(2.0, ((double)i - 40.0) / 3.0);
      d->tone_amp[i] = (float)(0.0580692432 * pow(2.0, (double)i / 4.0));
   }
   for (i = 0; i < 128; i++)
      d->pow2q[i] = (float)pow(2.0, ((double)i - 64.0) / 4.0);
   for (i = 0; i < 2048; i++)
      d->sine[i] = (float)sin(2.0 * pi * i / 2048.0);
   for (i = 0; i < 256; i++)
   {
      double s = sin(pi * i / 256.0);
      d->tone_win[i] = (float)(s * s);
   }
   for (i = 0; i < R3P_BAND_LEN; i++)
   {
      d->rise[0][i] = (float)sin(pi * (i + 0.5) / 256.0);
      d->fall[0][i] = (float)sin(pi * (i + 128 + 0.5) / 256.0);
      if (i < 32)
      {
         d->rise[1][i] = 0.0f;
         d->fall[1][i] = 1.0f;
      }
      else if (i < 96)
      {
         d->rise[1][i] = (float)sin(pi * (i - 32 + 0.5) / 128.0);
         d->fall[1][i] = (float)cos(pi * (i - 32 + 0.5) / 128.0);
      }
      else
      {
         d->rise[1][i] = 1.0f;
         d->fall[1][i] = 0.0f;
      }
   }
   for (i = 0; i < 64; i++)
   {
      double a = -pi * (i + 0.125) / 128.0;
      for (j = 0; j < 4; j++)
      {
         d->tw4[8 * i + j]     = (float)cos(a);
         d->tw4[8 * i + 4 + j] = (float)sin(a);
      }
      for (j = 0, b = i; j < 6; j++, b >>= 1)
         d->bitrev[i] = (uint8_t)((d->bitrev[i] << 1) | (b & 1));
   }
   for (i = 0; i < 32; i++)
   {
      double a = -2.0 * pi * i / 64.0;
      for (j = 0; j < 4; j++)
      {
         d->ftw4[8 * i + j]     = (float)cos(a);
         d->ftw4[8 * i + 4 + j] = (float)sin(a);
      }
   }
   for (b = 0; b < R3P_BANDS; b++)
      for (i = 0; i < 16; i++)
         d->dct4[b][i] = (float)cos(pi * (2 * b + 1) * (2 * i + 1) / 64.0);
   /* A rows carry the even taps, B rows the odd ones; B runs the sign of
    * its lower half in quarters 1 and 3, A is negated in quarter 2 */
   for (i = 0; i < 24; i++)
      for (j = 0; j < 16; j++)
      {
         unsigned q = i & 3;
         float    sg = 1.0f;
         if (q == 2 || (q == 1 && j >= 8) || (q == 3 && j < 8))
            sg = -1.0f;
         d->qmf_hh[(i >> 1) * 32 + (i & 1) * 16 + j] = sg * r3p_qmf_h[16 * i + j];
      }
}

void ratrac3p_reset(ratrac3p_t *d)
{
   unsigned c, b;
   if (!d)
      return;
   memset(d->chan, 0, d->channels * sizeof(*d->chan));
   for (c = 0; c < d->channels; c++)
      for (b = 0; b < R3P_BANDS; b++)
      {
         d->chan[c].gflat[b] = 1;
         d->chan[c].gmin[b]  = 6;
         r3p_tenv_reset(&d->chan[c].tenv[b]);
      }
}

ratrac3p_t *ratrac3p_new(unsigned channels, unsigned block_align)
{
   ratrac3p_t *d;
   if (channels < 1 || channels > RATRAC3P_MAX_CHANNELS || block_align < 8)
      return NULL;
   d = (ratrac3p_t*)calloc(1, sizeof(*d));
   if (!d)
      return NULL;
   /* the per-channel state is sized to the stream: a stereo decoder
    * keeps a quarter of what 7.1 needs */
   d->channels = channels;
   d->buf_len  = (size_t)block_align + 4;
   d->buf      = (uint8_t*)malloc(d->buf_len);
   d->unit     = (r3p_unit_t*)calloc(channels, sizeof(*d->unit));
   d->chan     = (r3p_chan_t*)calloc(channels, sizeof(*d->chan));
   d->pcm      = (float*)calloc((size_t)channels * RATRAC3P_SAMPLES,
         sizeof(float));
   if (!d->buf || !d->unit || !d->chan || !d->pcm)
   {
      ratrac3p_free(d);
      return NULL;
   }
   d->block_align = block_align;
   r3p_tables(d);
   ratrac3p_reset(d);
   return d;
}

void ratrac3p_free(ratrac3p_t *d)
{
   if (!d)
      return;
   free(d->buf);
   free(d->unit);
   free(d->chan);
   free(d->pcm);
   free(d);
}

static unsigned r3p_le16(const uint8_t *p)
{
   return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t r3p_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ratrac3p_parse_wave_fmt(const uint8_t *fmt, size_t len,
      ratrac3p_info_t *info)
{
   /* the sub-format GUID E923AABF-CB58-4471-A119-FFFA01E4CE62 */
   static const uint8_t guid[16] = {
      0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44,
      0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62
   };
   if (!fmt || !info || len < 40)
      return false;
   if (r3p_le16(fmt) != 0xFFFE || r3p_le16(fmt + 16) < 22
         || memcmp(fmt + 24, guid, 16))
      return false;
   info->channels    = r3p_le16(fmt + 2);
   info->sample_rate = r3p_le32(fmt + 4);
   info->bitrate     = r3p_le32(fmt + 8) * 8;
   info->block_align = r3p_le16(fmt + 12);
   if (info->channels < 1 || info->channels > RATRAC3P_MAX_CHANNELS
         || info->block_align < 8)
      return false;
   return true;
}
