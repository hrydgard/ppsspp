// Copyright (c) 2012- PPSSPP Project.

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

#include <cstddef>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "GPU/Common/TextureScalerCommon.h"

#include "Core/Config.h"
#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/CommonFuncs.h"
#include "Common/Thread/ParallelLoop.h"
#include "Core/ThreadPools.h"
#include "Common/CPUDetect.h"
#include "ext/xbrz/xbrz.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#include <smmintrin.h>
#endif

// Report the time and throughput for each larger scaling operation in the log
//#define SCALING_MEASURE_TIME

//#define DEBUG_SCALER_OUTPUT

#ifdef SCALING_MEASURE_TIME
#include "Common/TimeUtil.h"
#endif

/////////////////////////////////////// Helper Functions (mostly math for parallelization)

namespace {
//////////////////////////////////////////////////////////////////// Various image processing

#define R(_col) ((_col>> 0)&0xFF)
#define G(_col) ((_col>> 8)&0xFF)
#define B(_col) ((_col>>16)&0xFF)
#define A(_col) ((_col>>24)&0xFF)

#define DISTANCE(_p1,_p2) ( abs(static_cast<int>(static_cast<int>(R(_p1))-R(_p2))) + abs(static_cast<int>(static_cast<int>(G(_p1))-G(_p2))) \
							  + abs(static_cast<int>(static_cast<int>(B(_p1))-B(_p2))) + abs(static_cast<int>(static_cast<int>(A(_p1))-A(_p2))) )

// this is sadly much faster than an inline function with a loop, at least in VC10
#define MIX_PIXELS(_p0, _p1, _factors) \
		( (R(_p0)*(_factors)[0] + R(_p1)*(_factors)[1])/255 <<  0 ) | \
		( (G(_p0)*(_factors)[0] + G(_p1)*(_factors)[1])/255 <<  8 ) | \
		( (B(_p0)*(_factors)[0] + B(_p1)*(_factors)[1])/255 << 16 ) | \
		( (A(_p0)*(_factors)[0] + A(_p1)*(_factors)[1])/255 << 24 )

#define BLOCK_SIZE 32

// 3x3 convolution with Neumann boundary conditions, parallelizable
// quite slow, could be sped up a lot
// especially handling of separable kernels
void convolve3x3(const u32 *data, u32 *out, const int kernel[3][3], int width, int height, int l, int u) {
	for (int yb = 0; yb < (u - l) / BLOCK_SIZE + 1; ++yb) {
		for (int xb = 0; xb < width / BLOCK_SIZE + 1; ++xb) {
			for (int y = l + yb*BLOCK_SIZE; y < l + (yb + 1)*BLOCK_SIZE && y < u; ++y) {
				for (int x = xb*BLOCK_SIZE; x < (xb + 1)*BLOCK_SIZE && x < width; ++x) {
					int val = 0;
					for (int yoff = -1; yoff <= 1; ++yoff) {
						int yy = std::max(std::min(y + yoff, height - 1), 0);
						for (int xoff = -1; xoff <= 1; ++xoff) {
							int xx = std::max(std::min(x + xoff, width - 1), 0);
							val += data[yy*width + xx] * kernel[yoff + 1][xoff + 1];
						}
					}
					out[y*width + x] = abs(val);
				}
			}
		}
	}
}

// deposterization: smoothes posterized gradients from low-color-depth (e.g. 444, 565, compressed) sources
void deposterizeH(const u32 *data, u32 *out, int w, int l, int u) {
	static const int T = 8;
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < w; ++x) {
			int inpos = y*w + x;
			u32 center = data[inpos];
			if (x == 0 || x == w - 1) {
				out[y*w + x] = center;
				continue;
			}
			u32 left = data[inpos - 1];
			u32 right = data[inpos + 1];
			out[y*w + x] = 0;
			for (int c = 0; c < 4; ++c) {
				u8 lc = ((left >> c * 8) & 0xFF);
				u8 cc = ((center >> c * 8) & 0xFF);
				u8 rc = ((right >> c * 8) & 0xFF);
				if ((lc != rc) && ((lc == cc && abs((int)((int)rc) - cc) <= T) || (rc == cc && abs((int)((int)lc) - cc) <= T))) {
					// blend this component
					out[y*w + x] |= ((rc + lc) / 2) << (c * 8);
				} else {
					// no change for this component
					out[y*w + x] |= cc << (c * 8);
				}
			}
		}
	}
}
void deposterizeV(const u32 *data, u32 *out, int w, int h, int l, int u) {
	static const int T = 8;
	for (int xb = 0; xb < w / BLOCK_SIZE + 1; ++xb) {
		for (int y = l; y < u; ++y) {
			for (int x = xb*BLOCK_SIZE; x < (xb + 1)*BLOCK_SIZE && x < w; ++x) {
				u32 center = data[y    * w + x];
				if (y == 0 || y == h - 1) {
					out[y*w + x] = center;
					continue;
				}
				u32 upper = data[(y - 1) * w + x];
				u32 lower = data[(y + 1) * w + x];
				out[y*w + x] = 0;
				for (int c = 0; c < 4; ++c) {
					u8 uc = ((upper >> c * 8) & 0xFF);
					u8 cc = ((center >> c * 8) & 0xFF);
					u8 lc = ((lower >> c * 8) & 0xFF);
					if ((uc != lc) && ((uc == cc && abs((int)((int)lc) - cc) <= T) || (lc == cc && abs((int)((int)uc) - cc) <= T))) {
						// blend this component
						out[y*w + x] |= ((lc + uc) / 2) << (c * 8);
					} else {
						// no change for this component
						out[y*w + x] |= cc << (c * 8);
					}
				}
			}
		}
	}
}

// generates a distance mask value for each pixel in data
// higher values -> larger distance to the surrounding pixels
void generateDistanceMask(const u32 *data, u32 *out, int width, int height, int l, int u) {
	for (int yb = 0; yb < (u - l) / BLOCK_SIZE + 1; ++yb) {
		for (int xb = 0; xb < width / BLOCK_SIZE + 1; ++xb) {
			for (int y = l + yb*BLOCK_SIZE; y < l + (yb + 1)*BLOCK_SIZE && y < u; ++y) {
				for (int x = xb*BLOCK_SIZE; x < (xb + 1)*BLOCK_SIZE && x < width; ++x) {
					const u32 center = data[y*width + x];
					u32 dist = 0;
					for (int yoff = -1; yoff <= 1; ++yoff) {
						int yy = y + yoff;
						if (yy == height || yy == -1) {
							dist += 1200; // assume distance at borders, usually makes for better result
							continue;
						}
						for (int xoff = -1; xoff <= 1; ++xoff) {
							if (yoff == 0 && xoff == 0) continue;
							int xx = x + xoff;
							if (xx == width || xx == -1) {
								dist += 400; // assume distance at borders, usually makes for better result
								continue;
							}
							dist += DISTANCE(data[yy*width + xx], center);
						}
					}
					out[y*width + x] = dist;
				}
			}
		}
	}
}

// mix two images based on a mask
void mix(u32 *data, const u32 *source, const u32 *mask, u32 maskmax, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			int pos = y*width + x;
			u8 mixFactors[2] = { 0, static_cast<u8>((std::min(mask[pos], maskmax) * 255) / maskmax) };
			mixFactors[0] = 255 - mixFactors[1];
			data[pos] = MIX_PIXELS(data[pos], source[pos], mixFactors);
			if (A(source[pos]) == 0) data[pos] = data[pos] & 0x00FFFFFF; // xBRZ always does a better job with hard alpha
		}
	}
}

//////////////////////////////////////////////////////////////////// Bicubic scaling

// Code for the cubic upscaler is pasted below as-is.
// WARNING: different codestyle.

// NOTE: in several places memcpy is used instead of type punning,
// to avoid strict aliasing problems. This may produce suboptimal
// code, especially on MSVC.

// Loads a sample (4 bytes) from image into 'output'.
static void load_sample(ptrdiff_t w, ptrdiff_t h, ptrdiff_t s, const u8 *pixels, int wrap_mode, ptrdiff_t x, ptrdiff_t y, u8 *output) {
	// Check if the sample is inside. NOTE: for b>=0
	// the expression (UNSIGNED)a<(UNSIGNED)b is
	// equivalent to a>=0&&a<b.
	static_assert(sizeof(ptrdiff_t) == sizeof(size_t), "Assumes ptrdiff_t same width as size_t");

	if((size_t)x >= (size_t)w || (size_t)y >= (size_t)h) {
		switch(wrap_mode) {
			case 0: // Wrap
				if(!((w & (w-1)) | (h & (h-1)))) {
					// Both w and h are powers of 2.
					x &= w-1;
					y &= h-1;
				} else {
					// For e.g. 1x1 images we might need to wrap several
					// times, hence 'while', instead of 'if'. Probably
					// still faster, than modulo.
					while(x <  0) x += w;
					while(y <  0) y += h;
					while(x >= w) x -= w;
					while(y >= h) y -= h;
				}
				break;
			case 1: // Clamp
				if(x <  0) x = 0;
				if(y <  0) y = 0;
				if(x >= w) x = w-1;
				if(y >= h) y = h-1;
				break;
			case 2: // Zero
				memset(output, 0, 4);
				return;
		}
	}
	memcpy(output, pixels + s*y + 4*x, 4);
}

#define BLOCK 8

static void init_block(
	ptrdiff_t w, ptrdiff_t h,
	ptrdiff_t src_stride, const u8 *src_pixels,
	int wrap_mode, ptrdiff_t factor, float B, float C,
	ptrdiff_t x0, ptrdiff_t y0,
	float (*cx)[4], float (*cy)[4],
	ptrdiff_t *lx, ptrdiff_t *ly, ptrdiff_t *lx0, ptrdiff_t *ly0, ptrdiff_t *sx, ptrdiff_t *sy,
	u8 (*src)[(BLOCK+4)*4]) {
	// Precomputed coefficients for pixel weights
	// in the Mitchell-Netravali filter:
	//   output = SUM(wij*pixel[i]*t^j)
	// where t is distance from pixel[1] to the
	// sampling position.
	float   w00 = B/6.0f     ,  w01 = -C-0.5f*B,  w02 = 2.0f*C+0.5f*B      , w03 = -C-B/6.0f     ;
	float   w10 = 1.0f-B/3.0f,/*w11 = 0.0f     ,*/w12 = C+2.0f*B-3.0f      , w13 = -C-1.5f*B+2.0f;
	float   w20 = B/6.0f     ,  w21 =  C+0.5f*B,  w22 = -2.0f*C-2.5f*B+3.0f, w23 =  C+1.5f*B-2.0f;
	float /*w30 = 0.0f       ,  w31 = 0.0f     ,*/w32 = -C                 , w33 =  C+B/6.0f     ;
	// Express the sampling position as a rational
	// number num/den-1 (off by one, so that num is
	// always positive, since the C language does
	// not do Euclidean division). Sampling points
	// for both src and dst are assumed at pixel centers.
	ptrdiff_t den = 2*factor;
	float inv_den = 1.0f/(float)den;
	for(int dir = 0; dir < 2; ++dir) {
		ptrdiff_t num = (dir ? 2*y0+1+factor : 2*x0+1+factor);
		ptrdiff_t *l = (dir ? ly : lx), *l0 = (dir ? ly0 : lx0), *s = (dir ? sy : sx);
		float (*c)[4] = (dir ? cy : cx);
		(*l0) = num/den-2;
		num = num%den;
		for(ptrdiff_t i = 0, j = 0; i < BLOCK; ++i) {
			l[i] = j; // i-th dst pixel accesses src pixels (l0+l[i])..(l0+l[i]+3) in {x|y} direction.
			float t = (float)num*inv_den; // Fractional part of the sampling position.
			// Write out pixel weights.
			c[i][0] = ((w03*t+w02)*t  +w01  )*t  +w00  ;
			c[i][1] = ((w13*t+w12)*t/*+w11*/)*t  +w10  ;
			c[i][2] = ((w23*t+w22)*t  +w21  )*t  +w20  ;
			c[i][3] = ((w33*t+w32)*t/*+w31*/)*t/*+w30*/;
			// Increment the sampling position.
			if((num += 2) >= den) {num -= den; j += 1;}
		}
		(*s) = l[BLOCK-1]+4; // Total sampled src pixels in {x|y} direction.
	}
	// Get a local copy of the source pixels.
	if((*lx0) >=0 && (*ly0) >= 0 && *lx0 + (*sx) <= w && *ly0 + (*sy) <= h) {
		for(ptrdiff_t iy = 0; iy < (*sy); ++iy)
			memcpy(src[iy], src_pixels+src_stride*((*ly0) + iy) + 4*(*lx0), (size_t)(4*(*sx)));
	}
	else {
		for(ptrdiff_t iy = 0; iy < (*sy); ++iy) for(ptrdiff_t ix = 0; ix < (*sx); ++ix)
			load_sample(w, h, src_stride, src_pixels, wrap_mode, (*lx0) + ix, (*ly0) + iy, src[iy] + 4*ix);
	}
}

static void upscale_block_c(
	ptrdiff_t w, ptrdiff_t h,
	ptrdiff_t src_stride, const u8 *src_pixels,
	int wrap_mode, ptrdiff_t factor, float B, float C,
	ptrdiff_t x0, ptrdiff_t y0,
	u8 *dst_pixels) {
	float cx[BLOCK][4], cy[BLOCK][4];
	ptrdiff_t lx[BLOCK], ly[BLOCK], lx0, ly0, sx, sy;
	u8 src[BLOCK+4][(BLOCK+4)*4];
	float buf[2][BLOCK+4][BLOCK+4][4];
	init_block(
		w, h, src_stride, src_pixels, wrap_mode, factor, B, C, x0, y0,
		cx, cy, lx, ly, &lx0, &ly0, &sx, &sy, src);
	// Unpack source pixels.
	for(ptrdiff_t iy = 0; iy < sy; ++iy)
		for(ptrdiff_t ix = 0; ix < sx; ++ix)
			for(ptrdiff_t k = 0; k < 4; ++k)
				buf[0][iy][ix][k] = (float)(int)src[iy][4*ix + k];
	// Horizontal pass.
	for(ptrdiff_t ix = 0; ix < BLOCK; ++ix) {
		#define S(i) (buf[0][iy][lx[ix] + i][k])
		float C0 = cx[ix][0], C1 = cx[ix][1], C2 = cx[ix][2], C3 = cx[ix][3];
		for(ptrdiff_t iy = 0; iy < sy; ++iy)
			for(ptrdiff_t k = 0; k < 4; ++k)
				buf[1][iy][ix][k] = S(0)*C0 + S(1)*C1 + S(2)*C2 + S(3)*C3;
		#undef S
	}
	// Vertical pass.
	for(ptrdiff_t iy = 0; iy < BLOCK; ++iy) {
		#define S(i) (buf[1][ly[iy]+i][ix][k])
		float C0 = cy[iy][0], C1 = cy[iy][1], C2 = cy[iy][2], C3 = cy[iy][3];
		for(ptrdiff_t ix = 0; ix < BLOCK; ++ix)
			for(ptrdiff_t k = 0; k < 4; ++k)
				buf[0][iy][ix][k] = S(0)*C0 + S(1)*C1 + S(2)*C2 + S(3)*C3;
		#undef S
	}
	// Pack destination pixels.
	for(ptrdiff_t iy = 0; iy < BLOCK; ++iy)
		for(ptrdiff_t ix = 0; ix < BLOCK; ++ix) {
			u8 pixel[4];
			for(ptrdiff_t k = 0; k < 4; ++k) {
				float C = buf[0][iy][ix][k];
				if(!(C>0.0f)) C = 0.0f;
				if(C>255.0f)  C = 255.0f;
				pixel[k] = (u8)(int)(C + 0.5f);
			}
			memcpy(dst_pixels + 4*(BLOCK*iy + ix), pixel, 4);
		}
}

#if defined(_M_SSE)

#if defined(__GNUC__)
#define ALIGNED(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define ALIGNED(n) __declspec(align(n))
#else
// For our use case, ALIGNED is a hint, not a requirement,
// so it's fine to ignore it.
#define ALIGNED(n)
#endif

static void upscale_block_sse2(
	ptrdiff_t w, ptrdiff_t h,
	ptrdiff_t src_stride, const u8 *src_pixels,
	int wrap_mode, ptrdiff_t factor, float B, float C,
	ptrdiff_t x0, ptrdiff_t y0,
	u8 *dst_pixels) {
	float cx[BLOCK][4], cy[BLOCK][4];
	ptrdiff_t lx[BLOCK], ly[BLOCK], lx0, ly0, sx, sy;
	ALIGNED(16) u8 src[BLOCK+4][(BLOCK+4)*4];
	ALIGNED(16) float buf[2][BLOCK+4][BLOCK+4][4];
	init_block(
		w, h, src_stride, src_pixels, wrap_mode, factor, B, C, x0, y0,
		cx, cy, lx, ly, &lx0, &ly0, &sx, &sy, src);
	// Unpack source pixels.
	for(ptrdiff_t iy = 0; iy < sy; ++iy)
		for(ptrdiff_t ix = 0; ix < sx; ++ix) {
			int pixel;
			memcpy(&pixel, src[iy] + 4*ix, 4);
			__m128i C = _mm_cvtsi32_si128(pixel);
			C = _mm_unpacklo_epi8(C, _mm_set1_epi32(0));
			C = _mm_unpacklo_epi8(C, _mm_set1_epi32(0));
			_mm_storeu_ps(buf[0][iy][ix], _mm_cvtepi32_ps(C));
		}
	// Horizontal pass.
	for(ptrdiff_t ix = 0; ix < BLOCK; ++ix) {
		#define S(i) (buf[0][iy][lx[ix] + i])
		__m128 C0 = _mm_set1_ps(cx[ix][0]),
			C1 = _mm_set1_ps(cx[ix][1]),
			C2 = _mm_set1_ps(cx[ix][2]),
			C3 = _mm_set1_ps(cx[ix][3]);
		for(ptrdiff_t iy = 0; iy < sy; ++iy)
			_mm_storeu_ps(buf[1][iy][ix],
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(0)), C0),
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(1)), C1),
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(2)), C2),
					   _mm_mul_ps(_mm_loadu_ps(S(3)), C3)))));
		#undef S
	}
	// Vertical pass.
	for(ptrdiff_t iy = 0; iy < BLOCK; ++iy) {
		#define S(i) (buf[1][ly[iy] + i][ix])
		__m128 C0 = _mm_set1_ps(cy[iy][0]),
			C1 = _mm_set1_ps(cy[iy][1]),
			C2 = _mm_set1_ps(cy[iy][2]),
			C3 = _mm_set1_ps(cy[iy][3]);
		for(ptrdiff_t ix = 0; ix < BLOCK; ++ix)
			_mm_storeu_ps(buf[0][iy][ix],
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(0)), C0),
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(1)), C1),
				_mm_add_ps(_mm_mul_ps(_mm_loadu_ps(S(2)), C2),
					   _mm_mul_ps(_mm_loadu_ps(S(3)), C3)))));
		#undef S
	}
	// Pack destination pixels.
	for(ptrdiff_t iy = 0; iy < BLOCK; ++iy)
		for(ptrdiff_t ix = 0; ix < BLOCK; ++ix) {
			__m128 C = _mm_loadu_ps(buf[0][iy][ix]);
			C = _mm_min_ps(_mm_max_ps(C, _mm_set1_ps(0.0f)), _mm_set1_ps(255.0f));
			C = _mm_add_ps(C, _mm_set1_ps(0.5f));
			__m128i R = _mm_cvttps_epi32(C);
			R = _mm_packus_epi16(R, R);
			R = _mm_packus_epi16(R, R);
			int pixel = _mm_cvtsi128_si32(R);
			memcpy(dst_pixels + 4*(BLOCK*iy+ix), &pixel, 4);
		}
}
#endif // defined(_M_SSE)

static void upscale_cubic(
	ptrdiff_t width, ptrdiff_t height,	ptrdiff_t src_stride_in_bytes, const void *src_pixels,
									  	ptrdiff_t dst_stride_in_bytes, void       *dst_pixels,
	ptrdiff_t scale, float B, float C, int wrap_mode,
	ptrdiff_t x0, ptrdiff_t y0, ptrdiff_t x1, ptrdiff_t y1) {
	u8 pixels[BLOCK*BLOCK*4];
	for(ptrdiff_t y = y0; y < y1; y+= BLOCK)
		for(ptrdiff_t x = x0; x < x1; x+= BLOCK) {
#if defined(_M_SSE)
			upscale_block_sse2(width, height, src_stride_in_bytes, (const u8*)src_pixels, wrap_mode, scale, B, C, x, y, pixels);
#else
			upscale_block_c   (width, height, src_stride_in_bytes, (const u8*)src_pixels, wrap_mode, scale, B, C, x, y, pixels);
#endif
			for(ptrdiff_t iy = 0, ny = (y1-y < BLOCK ? y1-y : BLOCK), nx = (x1-x < BLOCK ? x1-x : BLOCK); iy < ny; ++iy)
				memcpy((u8*)dst_pixels + dst_stride_in_bytes*(y+iy) + 4*x, pixels + BLOCK*4*iy, (size_t)(4*nx));
		}
}

// End of pasted cubic upscaler.

void scaleBicubicBSpline(int factor, const u32 *data, u32 *out, int w, int h, int l, int u) {
	const float B = 1.0f, C = 0.0f;
	const int wrap_mode = 1; // Clamp
	upscale_cubic(
		w, h, w*4, data,
		factor*w*4, out,
		factor, B, C, wrap_mode,
		0, factor*l, factor*w, factor*u);
}

void scaleBicubicMitchell(int factor, const u32 *data, u32 *out, int w, int h, int l, int u) {
	const float B = 0.0f, C = 0.5f; // Actually, Catmull-Rom
	const int wrap_mode = 1; // Clamp
	upscale_cubic(
		w, h, w*4, data,
		factor*w*4, out,
		factor, B, C, wrap_mode,
		0, factor*l, factor*w, factor*u);
}

//////////////////////////////////////////////////////////////////// Bilinear scaling

const static u8 BILINEAR_FACTORS[4][3][2] = {
		{ { 44, 211 }, { 0, 0 }, { 0, 0 } }, // x2
		{ { 64, 191 }, { 0, 255 }, { 0, 0 } }, // x3
		{ { 77, 178 }, { 26, 229 }, { 0, 0 } }, // x4
		{ { 102, 153 }, { 51, 204 }, { 0, 255 } }, // x5
};
// integral bilinear upscaling by factor f, horizontal part
template<int f>
void bilinearHt(const u32 *data, u32 *out, int w, int l, int u) {
	static_assert(f > 1 && f <= 5, "Bilinear scaling only implemented for factors 2 to 5");
	int outw = w*f;
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < w; ++x) {
			int inpos = y*w + x;
			u32 left = data[inpos - (x == 0 ? 0 : 1)];
			u32 center = data[inpos];
			u32 right = data[inpos + (x == w - 1 ? 0 : 1)];
			int i = 0;
			for (; i < f / 2 + f % 2; ++i) { // first half of the new pixels + center, hope the compiler unrolls this
				out[y*outw + x*f + i] = MIX_PIXELS(left, center, BILINEAR_FACTORS[f - 2][i]);
			}
			for (; i < f; ++i) { // second half of the new pixels, hope the compiler unrolls this
				out[y*outw + x*f + i] = MIX_PIXELS(right, center, BILINEAR_FACTORS[f - 2][f - 1 - i]);
			}
		}
	}
}
void bilinearH(int factor, const u32 *data, u32 *out, int w, int l, int u) {
	switch (factor) {
	case 2: bilinearHt<2>(data, out, w, l, u); break;
	case 3: bilinearHt<3>(data, out, w, l, u); break;
	case 4: bilinearHt<4>(data, out, w, l, u); break;
	case 5: bilinearHt<5>(data, out, w, l, u); break;
	default: ERROR_LOG(Log::G3D, "Bilinear upsampling only implemented for factors 2 to 5");
	}
}
// integral bilinear upscaling by factor f, vertical part
// gl/gu == global lower and upper bound
template<int f>
void bilinearVt(const u32 *data, u32 *out, int w, int gl, int gu, int l, int u) {
	static_assert(f>1 && f <= 5, "Bilinear scaling only implemented for 2x, 3x, 4x, and 5x");
	int outw = w*f;
	for (int xb = 0; xb < outw / BLOCK_SIZE + 1; ++xb) {
		for (int y = l; y < u; ++y) {
			u32 uy = y - (y == gl ? 0 : 1);
			u32 ly = y + (y == gu - 1 ? 0 : 1);
			for (int x = xb*BLOCK_SIZE; x < (xb + 1)*BLOCK_SIZE && x < outw; ++x) {
				u32 upper = data[uy * outw + x];
				u32 center = data[y * outw + x];
				u32 lower = data[ly * outw + x];
				int i = 0;
				for (; i < f / 2 + f % 2; ++i) { // first half of the new pixels + center, hope the compiler unrolls this
					out[(y*f + i)*outw + x] = MIX_PIXELS(upper, center, BILINEAR_FACTORS[f - 2][i]);
				}
				for (; i < f; ++i) { // second half of the new pixels, hope the compiler unrolls this
					out[(y*f + i)*outw + x] = MIX_PIXELS(lower, center, BILINEAR_FACTORS[f - 2][f - 1 - i]);
				}
			}
		}
	}
}
void bilinearV(int factor, const u32 *data, u32 *out, int w, int gl, int gu, int l, int u) {
	switch (factor) {
	case 2: bilinearVt<2>(data, out, w, gl, gu, l, u); break;
	case 3: bilinearVt<3>(data, out, w, gl, gu, l, u); break;
	case 4: bilinearVt<4>(data, out, w, gl, gu, l, u); break;
	case 5: bilinearVt<5>(data, out, w, gl, gu, l, u); break;
	default: ERROR_LOG(Log::G3D, "Bilinear upsampling only implemented for factors 2 to 5");
	}
}

#undef BLOCK_SIZE
#undef MIX_PIXELS
#undef DISTANCE
#undef R
#undef G
#undef B
#undef A

#ifdef DEBUG_SCALER_OUTPUT

// used for debugging texture scaling (writing textures to files)
static int g_imgCount = 0;
void dbgPPM(int w, int h, u8* pixels, const char* prefix = "dbg") { // 3 component RGB
	char fn[32];
	snprintf(fn, 32, "%s%04d.ppm", prefix, g_imgCount++);
	FILE *fp = fopen(fn, "wb");
	fprintf(fp, "P6\n%d %d\n255\n", w, h);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			static unsigned char color[3];
			color[0] = pixels[(j*w + i) * 4 + 0];  /* red */
			color[1] = pixels[(j*w + i) * 4 + 1];  /* green */
			color[2] = pixels[(j*w + i) * 4 + 2];  /* blue */
			fwrite(color, 1, 3, fp);
		}
	}
	fclose(fp);
}
void dbgPGM(int w, int h, u32* pixels, const char* prefix = "dbg") { // 1 component
	char fn[32];
	snprintf(fn, 32, "%s%04d.pgm", prefix, g_imgCount++);
	FILE *fp = fopen(fn, "wb");
	fprintf(fp, "P5\n%d %d\n65536\n", w, h);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			fwrite((pixels + (j*w + i)), 1, 2, fp);
		}
	}
	fclose(fp);
}

#endif

}

/////////////////////////////////////// Texture Scaler

TextureScalerCommon::TextureScalerCommon() {
	// initBicubicWeights() used to be here.
}

TextureScalerCommon::~TextureScalerCommon() {
}

bool TextureScalerCommon::IsEmptyOrFlat(const u32 *data, int pixels) {
	u32 ref = data[0];
	// TODO: SIMD-ify this (although, for most textures we'll get out very early)
	for (int i = 1; i < pixels; ++i) {
		if (data[i] != ref)
			return false;
	}
	return true;
}

void TextureScalerCommon::ScaleAlways(u32 *out, u32 *src, int width, int height, int *scaledWidth, int *scaledHeight, int factor) {
	if (IsEmptyOrFlat(src, width * height)) {
		// This means it was a flat texture.  Vulkan wants the size up front, so we need to make it happen.
		u32 pixel = *src;

		*scaledWidth = width * factor;
		*scaledHeight = height * factor;

		size_t pixelCount = *scaledWidth * *scaledHeight;

		// ABCD.  If A = D, and AB = CD, then they must all be equal (B = C, etc.)
		if ((pixel & 0x000000FF) == (pixel >> 24) && (pixel & 0x0000FFFF) == (pixel >> 16)) {
			memset(out, pixel & 0xFF, pixelCount * sizeof(u32));
		} else {
			// Let's hope this is vectorized.
			for (int i = 0; i < pixelCount; ++i) {
				out[i] = pixel;
			}
		}
	} else {
		ScaleInto(out, src, width, height, scaledWidth, scaledHeight, factor);
	}
}

bool TextureScalerCommon::ScaleInto(u32 *outputBuf, u32 *src, int width, int height, int *scaledWidth, int *scaledHeight, int factor) {
#ifdef SCALING_MEASURE_TIME
	double t_start = time_now_d();
#endif

	u32 *inputBuf = src;

	// deposterize
	if (g_Config.bTexDeposterize) {
		bufDeposter.resize(width * height);
		DePosterize(inputBuf, bufDeposter.data(), width, height);
		inputBuf = bufDeposter.data();
	}

	// scale 
	switch (g_Config.iTexScalingType) {
	case XBRZ:
		ScaleXBRZ(factor, inputBuf, outputBuf, width, height);
		break;
	case HYBRID:
		ScaleHybrid(factor, inputBuf, outputBuf, width, height);
		break;
	case BICUBIC:
		ScaleBicubicMitchell(factor, inputBuf, outputBuf, width, height);
		break;
	case HYBRID_BICUBIC:
		ScaleHybrid(factor, inputBuf, outputBuf, width, height, true);
		break;
	default:
		ERROR_LOG(Log::G3D, "Unknown scaling type: %d", g_Config.iTexScalingType);
	}

	// update values accordingly
	*scaledWidth = width * factor;
	*scaledHeight = height * factor;

#ifdef SCALING_MEASURE_TIME
	if (*scaledWidth* *scaledHeight > 64 * 64 * factor*factor) {
		double t = time_now_d() - t_start;
		NOTICE_LOG(Log::G3D, "TextureScaler: processed %9d pixels in %6.5lf seconds. (%9.2lf Mpixels/second)",
			*scaledWidth * *scaledHeight, t, (*scaledWidth * *scaledHeight) / (t * 1000 * 1000));
	}
#endif

	return true;
}

bool TextureScalerCommon::Scale(u32* &data, int width, int height, int *scaledWidth, int *scaledHeight, int factor) {
	// prevent processing empty or flat textures (this happens a lot in some games)
	// doesn't hurt the standard case, will be very quick for textures with actual texture
	if (IsEmptyOrFlat(data, width*height)) {
		DEBUG_LOG(Log::G3D, "TextureScaler: early exit -- empty/flat texture");
		return false;
	}

	bufOutput.resize(width * height * (factor * factor)); // used to store the upscaled image
	u32 *outputBuf = bufOutput.data();

	if (ScaleInto(outputBuf, data, width, height, scaledWidth, scaledHeight, factor)) {
		data = outputBuf;
		return true;
	}
	return false;
}

const int MIN_LINES_PER_THREAD = 4;

void TextureScalerCommon::ScaleXBRZ(int factor, u32* source, u32* dest, int width, int height) {
	xbrz::ScalerCfg cfg;
	ParallelRangeLoop(&g_threadManager, std::bind(&xbrz::scale, factor, source, dest, width, height, xbrz::ColorFormat::ARGB, cfg, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
}

void TextureScalerCommon::ScaleBilinear(int factor, u32* source, u32* dest, int width, int height) {
	bufTmp1.resize(width * height * factor);
	u32 *tmpBuf = bufTmp1.data();
	ParallelRangeLoop(&g_threadManager, std::bind(&bilinearH, factor, source, tmpBuf, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ParallelRangeLoop(&g_threadManager, std::bind(&bilinearV, factor, tmpBuf, dest, width, 0, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
}

void TextureScalerCommon::ScaleBicubicBSpline(int factor, u32* source, u32* dest, int width, int height) {
	ParallelRangeLoop(&g_threadManager,std::bind(&scaleBicubicBSpline, factor, source, dest, width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
}

void TextureScalerCommon::ScaleBicubicMitchell(int factor, u32* source, u32* dest, int width, int height) {
	ParallelRangeLoop(&g_threadManager,std::bind(&scaleBicubicMitchell, factor, source, dest, width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
}

void TextureScalerCommon::ScaleHybrid(int factor, u32* source, u32* dest, int width, int height, bool bicubic) {
	// Basic algorithm:
	// 1) determine a feature mask C based on a sobel-ish filter + splatting, and upscale that mask bilinearly
	// 2) generate 2 scaled images: A - using Bilinear filtering, B - using xBRZ
	// 3) output = A*C + B*(1-C)

	const static int KERNEL_SPLAT[3][3] = {
			{ 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }
	};

	bufTmp1.resize(width*height);
	bufTmp2.resize(width*height*factor*factor);
	bufTmp3.resize(width*height*factor*factor);

	ParallelRangeLoop(&g_threadManager,std::bind(&generateDistanceMask, source, bufTmp1.data(), width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ParallelRangeLoop(&g_threadManager,std::bind(&convolve3x3, bufTmp1.data(), bufTmp2.data(), KERNEL_SPLAT, width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ScaleBilinear(factor, bufTmp2.data(), bufTmp3.data(), width, height);
	// mask C is now in bufTmp3

	ScaleXBRZ(factor, source, bufTmp2.data(), width, height);
	// xBRZ upscaled source is in bufTmp2

	if (bicubic) ScaleBicubicBSpline(factor, source, dest, width, height);
	else ScaleBilinear(factor, source, dest, width, height);
	// Upscaled source is in dest

	// Now we can mix it all together
	// The factor 8192 was found through practical testing on a variety of textures
	ParallelRangeLoop(&g_threadManager,std::bind(&mix, dest, bufTmp2.data(), bufTmp3.data(), 8192, width*factor, std::placeholders::_1, std::placeholders::_2), 0, height*factor, MIN_LINES_PER_THREAD);
}

void TextureScalerCommon::DePosterize(u32* source, u32* dest, int width, int height) {
	bufTmp3.resize(width*height);
	ParallelRangeLoop(&g_threadManager,std::bind(&deposterizeH, source, bufTmp3.data(), width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ParallelRangeLoop(&g_threadManager,std::bind(&deposterizeV, bufTmp3.data(), dest, width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ParallelRangeLoop(&g_threadManager,std::bind(&deposterizeH, dest, bufTmp3.data(), width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
	ParallelRangeLoop(&g_threadManager,std::bind(&deposterizeV, bufTmp3.data(), dest, width, height, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_LINES_PER_THREAD);
}
