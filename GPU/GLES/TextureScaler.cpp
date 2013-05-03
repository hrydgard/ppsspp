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

#include "TextureScaler.h"

#include "Core/Config.h"
#include "Common/Log.h"
#include "Common/MsgHandler.h"
#include "Common/CommonFuncs.h"
#include "Common/ThreadPools.h"
#include "ext/xbrz/xbrz.h"
#include <stdlib.h>

#ifdef __SYMBIAN32__
#define p
#elif defined(IOS)
#include <tr1/functional>
namespace p = std::tr1::placeholders;
#else
namespace p = std::placeholders;
#endif

// Report the time and throughput for each larger scaling operation in the log
//#define SCALING_MEASURE_TIME

#ifdef SCALING_MEASURE_TIME
#include "native/base/timeutil.h"
#endif

/////////////////////////////////////// Helper Functions (mostly math for parallelization)

namespace {
	// convert 4444 image to 8888, parallelizable
	void convert4444(u16* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = data[y*width + x];
				u32 r = ((val>>12) & 0xF) * 17;
				u32 g = ((val>> 8) & 0xF) * 17;
				u32 b = ((val>> 4) & 0xF) * 17;
				u32 a = ((val>> 0) & 0xF) * 17;
				out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
			}
		}
	}

	// convert 565 image to 8888, parallelizable
	void convert565(u16* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = data[y*width + x];
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert6To8((val>> 5) & 0x3F);
				u32 b = Convert5To8((val    ) & 0x1F);
				out[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
			}
		}
	}

	// convert 5551 image to 8888, parallelizable
	void convert5551(u16* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = data[y*width + x];
				u32 r = Convert5To8((val>>11) & 0x1F);
				u32 g = Convert5To8((val>> 6) & 0x1F);
				u32 b = Convert5To8((val>> 1) & 0x1F);
				u32 a = (val & 0x1) * 255;
				out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
			}
		}
	}

	#define R(_col) ((_col>> 0)&0xFF)
	#define G(_col) ((_col>> 8)&0xFF)
	#define B(_col) ((_col>>16)&0xFF)
	#define A(_col) ((_col>>24)&0xFF)

	#define DISTANCE(_p1,_p2) ( abs((int)((int)(R(_p1))-R(_p2))) + abs((int)((int)(G(_p1))-G(_p2))) \
							  + abs((int)((int)(B(_p1)-B(_p2)))) + abs((int)((int)(A(_p1)-A(_p2)))) )
	
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
	void convolve3x3(u32* data, u32* out, const int kernel[3][3], int width, int height, int l, int u) {
		for(int yb = 0; yb < (u-l)/BLOCK_SIZE+1; ++yb) {
			for(int xb = 0; xb < width/BLOCK_SIZE+1; ++xb) {
				for(int y = l+yb*BLOCK_SIZE; y < l+(yb+1)*BLOCK_SIZE && y < u; ++y) {
					for(int x = xb*BLOCK_SIZE; x < (xb+1)*BLOCK_SIZE && x < width; ++x) {
						int val = 0;
						for(int yoff = -1; yoff <= 1; ++yoff) {
							int yy = std::max(std::min(y+yoff, height-1), 0);
							for(int xoff = -1; xoff <= 1; ++xoff) {
								int xx = std::max(std::min(x+xoff, width-1), 0);
								val += data[yy*width + xx] * kernel[yoff+1][xoff+1];
							}
						}
						out[y*width + x] = abs(val);
					}
				}
			}
		}
	}

	void deposterizeH(u32* data, u32* out, int w, int l, int u) {
		static const int T = 8;
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < w; ++x) {
				int inpos = y*w + x;
				u32 center = data[inpos];
				if(x==0 || x==w-1) {
					out[y*w + x] = center;
					continue;
				}
				u32 left   = data[inpos - 1];
				u32 right  = data[inpos + 1];
				out[y*w + x] = 0;
				for(int c=0; c<4; ++c) {
					u8 lc = ((  left>>c*8)&0xFF);
					u8 cc = ((center>>c*8)&0xFF);
					u8 rc = (( right>>c*8)&0xFF);
					if((lc != rc) && ((lc == cc && abs((int)((int)rc)-cc) <= T) || (rc == cc && abs((int)((int)lc)-cc) <= T))) {
						// blend this component
						out[y*w + x] |= ((rc+lc)/2) << (c*8);
					} else {
						// no change for this component
						out[y*w + x] |= cc << (c*8);
					}
				}
			}
		}
	}
	void deposterizeV(u32* data, u32* out, int w, int h, int l, int u) {
		static const int T = 8;
		for(int xb = 0; xb < w/BLOCK_SIZE+1; ++xb) {
			for(int y = l; y < u; ++y) {
				for(int x = xb*BLOCK_SIZE; x < (xb+1)*BLOCK_SIZE && x < w; ++x) {
					u32 center = data[ y    * w + x];
					if(y==0 || y==h-1) {
						out[y*w + x] = center;
						continue;
					}
					u32 upper  = data[(y-1) * w + x];
					u32 lower  = data[(y+1) * w + x];
					out[y*w + x] = 0;
					for(int c=0; c<4; ++c) {
						u8 uc = (( upper>>c*8)&0xFF);
						u8 cc = ((center>>c*8)&0xFF);
						u8 lc = (( lower>>c*8)&0xFF);
						if((uc != lc) && ((uc == cc && abs((int)((int)lc)-cc) <= T) || (lc == cc && abs((int)((int)uc)-cc) <= T))) {
							// blend this component
							out[y*w + x] |= ((lc+uc)/2) << (c*8);
						} else {
							// no change for this component
							out[y*w + x] |= cc << (c*8);
						}
					}
				}
			}
		}
	}

	void generateDistanceMask(u32* data, u32* out, int width, int height, int l, int u) {
		for(int yb = 0; yb < (u-l)/BLOCK_SIZE+1; ++yb) {
			for(int xb = 0; xb < width/BLOCK_SIZE+1; ++xb) {
				for(int y = l+yb*BLOCK_SIZE; y < l+(yb+1)*BLOCK_SIZE && y < u; ++y) {
					for(int x = xb*BLOCK_SIZE; x < (xb+1)*BLOCK_SIZE && x < width; ++x) {
						out[y*width + x] = 0;
						u32 center = data[y*width + x];
						for(int yoff = -1; yoff <= 1; ++yoff) {
							int yy = y+yoff;
							if(yy == height || yy == -1) {
								out[y*width + x] += 1200; // assume distance at borders, usually makes for better result
								continue;
							}
							for(int xoff = -1; xoff <= 1; ++xoff) {
								if(yoff == 0 && xoff == 0) continue;
								int xx = x+xoff;
								if(xx == width || xx == -1) {
									out[y*width + x] += 400; // assume distance at borders, usually makes for better result
									continue;
								}
								out[y*width + x] += DISTANCE(data[yy*width + xx], center);
							}
						}
					}
				}
			}
		}
	}

	void mix(u32* data, u32* source, u32* mask, u32 maskmax, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				int pos = y*width + x;
				u8 mixFactors[2] = { 0, static_cast<u8>((std::min(mask[pos], maskmax)*255)/maskmax) };
				mixFactors[0] = 255-mixFactors[1];
				data[pos] = MIX_PIXELS(data[pos], source[pos], mixFactors);
				if(A(source[pos]) == 0) data[pos] = data[pos] & 0x00FFFFFF; // xBRZ always does a better job with hard alpha
			}
		}
	}
	
	const static u8 BILINEAR_FACTORS[4][3][2] = {
		{ { 44,211}, {  0,  0}, {  0,  0} }, // x2
		{ { 64,191}, {  0,255}, {  0,  0} }, // x3
		{ { 77,178}, { 26,229}, {  0,  0} }, // x4
		{ {102,153}, { 51,204}, {  0,255} }, // x5
	};
	// integral bilinear upscaling by factor f, horizontal part
	template<int f>
	void bilinearHt(u32* data, u32* out, int w, int l, int u) {
		static_assert(f>1 && f<=5, "Bilinear scaling only implemented for factors 2 to 5");
		int outw = w*f;
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < w; ++x) {
				int inpos = y*w + x;
				u32 left   = data[inpos - (x==0  ?0:1)];
				u32 center = data[inpos];
				u32 right  = data[inpos + (x==w-1?0:1)];
				int i=0;
				for(; i<f/2+f%2; ++i) { // first half of the new pixels + center, hope the compiler unrolls this
					out[y*outw + x*f + i] = MIX_PIXELS(left, center, BILINEAR_FACTORS[f-2][i]);
				}
				for(; i<f      ; ++i) { // second half of the new pixels, hope the compiler unrolls this
					out[y*outw + x*f + i] = MIX_PIXELS(right, center, BILINEAR_FACTORS[f-2][f-1-i]);
				}
			}
		}
	}
	void bilinearH(int factor, u32* data, u32* out, int w, int l, int u) {
		switch(factor) {
		case 2: bilinearHt<2>(data, out, w, l, u); break;
		case 3: bilinearHt<3>(data, out, w, l, u); break;
		case 4: bilinearHt<4>(data, out, w, l, u); break;
		case 5: bilinearHt<5>(data, out, w, l, u); break;
		default: ERROR_LOG(G3D, "Bilinear upsampling only implemented for factors 2 to 5");
		}
	}
	// integral bilinear upscaling by factor f, vertical part
	// gl/gu == global lower and upper bound
	template<int f>
	void bilinearVt(u32* data, u32* out, int w, int gl, int gu, int l, int u) {
		static_assert(f>1 && f<=5, "Bilinear scaling only implemented for 2x, 3x, 4x, and 5x");
		int outw = w*f;
		for(int xb = 0; xb < outw/BLOCK_SIZE+1; ++xb) {
			for(int y = l; y < u; ++y) {
				u32 uy = y - (y==gl  ?0:1);
				u32 ly = y + (y==gu-1?0:1);
				for(int x = xb*BLOCK_SIZE; x < (xb+1)*BLOCK_SIZE && x < outw; ++x) {
					u32 upper  = data[uy * outw + x];
					u32 center = data[y * outw + x];
					u32 lower  = data[ly * outw + x];
					int i=0;
					for(; i<f/2+f%2; ++i) { // first half of the new pixels + center, hope the compiler unrolls this
						out[(y*f + i)*outw + x] = MIX_PIXELS(upper, center, BILINEAR_FACTORS[f-2][i]);
					}
					for(; i<f      ; ++i) { // second half of the new pixels, hope the compiler unrolls this
						out[(y*f + i)*outw + x] = MIX_PIXELS(lower, center, BILINEAR_FACTORS[f-2][f-1-i]);
					}
				}
			}
		}
	}
	void bilinearV(int factor, u32* data, u32* out, int w, int gl, int gu, int l, int u) {
		switch(factor) {
		case 2: bilinearVt<2>(data, out, w, gl, gu, l, u); break;
		case 3: bilinearVt<3>(data, out, w, gl, gu, l, u); break;
		case 4: bilinearVt<4>(data, out, w, gl, gu, l, u); break;
		case 5: bilinearVt<5>(data, out, w, gl, gu, l, u); break;
		default: ERROR_LOG(G3D, "Bilinear upsampling only implemented for factors 2 to 5");
		}
	}

	#undef BLOCK_SIZE
	#undef MIX_PIXELS
	#undef DISTANCE
	#undef R
	#undef G
	#undef B
	#undef A

	// used for debugging texture scaling (writing textures to files)
	static int g_imgCount = 0;
	void dbgPPM(int w, int h, u8* pixels, const char* prefix = "dbg") { // 3 component RGB
		char fn[32];
		snprintf(fn, 32, "%s%04d.ppm", prefix, g_imgCount++);
		FILE *fp = fopen(fn, "wb");
		fprintf(fp, "P6\n%d %d\n255\n", w, h);
		for(int j = 0; j < h; ++j) {
			for(int i = 0; i < w; ++i) {
				static unsigned char color[3];
				color[0] = pixels[(j*w+i)*4+0];  /* red */
				color[1] = pixels[(j*w+i)*4+1];  /* green */
				color[2] = pixels[(j*w+i)*4+2];  /* blue */
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
		for(int j = 0; j < h; ++j) {
			for(int i = 0; i < w; ++i) {
				fwrite((pixels+(j*w+i)), 1, 2, fp);
			}
		}
		fclose(fp);
	}
}

/////////////////////////////////////// Texture Scaler

TextureScaler::TextureScaler() {
}

bool TextureScaler::IsEmptyOrFlat(u32* data, int pixels, GLenum fmt) {
	int pixelsPerWord = (fmt == GL_UNSIGNED_BYTE) ? 1 : 2;
	int ref = data[0];
	for(int i=0; i<pixels/pixelsPerWord; ++i) {
		if(data[i]!=ref) return false;
	}
	return true;
}

void TextureScaler::Scale(u32* &data, GLenum &dstFmt, int &width, int &height, int factor) {
	// prevent processing empty or flat textures (this happens a lot in some games)
	// doesn't hurt the standard case, will be very quick for textures with actual texture
	if(IsEmptyOrFlat(data, width*height, dstFmt)) {
		INFO_LOG(G3D, "TextureScaler: early exit -- empty/flat texture");
		return;
	}

	#ifdef SCALING_MEASURE_TIME
	double t_start = real_time_now();
	#endif

	bufInput.resize(width*height); // used to store the input image image if it needs to be reformatted
	bufOutput.resize(width*height*factor*factor); // used to store the upscaled image
	u32 *inputBuf = bufInput.data();
	u32 *outputBuf = bufOutput.data();

	// convert texture to correct format for scaling
	ConvertTo8888(dstFmt, data, inputBuf, width, height);
	
	// deposterize
	if(g_Config.bTexDeposterize) {
		bufDeposter.resize(width*height);
		DePosterize(inputBuf, bufDeposter.data(), width, height);
		inputBuf = bufDeposter.data();
	}
	
	// scale 
	switch(g_Config.iTexScalingType) {
	case XBRZ:
		ScaleXBRZ(factor, inputBuf, outputBuf, width, height);
		break;
	case HYBRID:
		ScaleHybrid(factor, inputBuf, outputBuf, width, height);
		break;
	default:
		ERROR_LOG(G3D, "Unknown scaling type: %d", g_Config.iTexScalingType);
	}

	// update values accordingly
	data = outputBuf;
	dstFmt = GL_UNSIGNED_BYTE;
	width *= factor;
	height *= factor;

	#ifdef SCALING_MEASURE_TIME
	if(width*height > 64*64*factor*factor) {
		double t = real_time_now() - t_start;
		NOTICE_LOG(MASTER_LOG, "TextureScaler: processed %9d pixels in %6.5lf seconds. (%9.0lf Mpixels/second)", 
			width*height, t, (width*height)/(t*1000*1000));
	}
	#endif
}

void TextureScaler::ScaleXBRZ(int factor, u32* source, u32* dest, int width, int height) {
	xbrz::ScalerCfg cfg;
	GlobalThreadPool::Loop(bind(&xbrz::scale, factor, source, dest, width, height, cfg, p::_1, p::_2), 0, height);
}

void TextureScaler::ScaleBilinear(int factor, u32* source, u32* dest, int width, int height) {
	bufTmp1.resize(width*height*factor);
	u32 *tmpBuf = bufTmp1.data();
	GlobalThreadPool::Loop(bind(&bilinearH, factor, source, tmpBuf, width, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(bind(&bilinearV, factor, tmpBuf, dest, width, 0, height, p::_1, p::_2), 0, height);
}

void TextureScaler::ScaleHybrid(int factor, u32* source, u32* dest, int width, int height) {
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
	GlobalThreadPool::Loop(bind(&generateDistanceMask, source, bufTmp1.data(), width, height, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(bind(&convolve3x3, bufTmp1.data(), bufTmp2.data(), KERNEL_SPLAT, width, height, p::_1, p::_2), 0, height);
	ScaleBilinear(factor, bufTmp2.data(), bufTmp3.data(), width, height);
	// mask C is now in bufTmp3

	ScaleXBRZ(factor, source, bufTmp2.data(), width, height);
	// xBRZ upscaled source is in bufTmp2

	ScaleBilinear(factor, source, dest, width, height);
	// Bilinear upscaled source is in dest

	// Now we can mix it all together
	// The factor 8192 was found through practical testing on a variety of textures
	GlobalThreadPool::Loop(bind(&mix, dest, bufTmp2.data(), bufTmp3.data(), 8192, width*factor, p::_1, p::_2), 0, height*factor);
}

void TextureScaler::DePosterize(u32* source, u32* dest, int width, int height) {
	bufTmp3.resize(width*height);
	GlobalThreadPool::Loop(bind(&deposterizeH, source, bufTmp3.data(), width, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(bind(&deposterizeV, bufTmp3.data(), dest, width, height, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(bind(&deposterizeH, dest, bufTmp3.data(), width, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(bind(&deposterizeV, bufTmp3.data(), dest, width, height, p::_1, p::_2), 0, height);
}

void TextureScaler::ConvertTo8888(GLenum format, u32* source, u32* &dest, int width, int height) {
	switch(format) {
	case GL_UNSIGNED_BYTE:
		dest = source; // already fine
		break;

	case GL_UNSIGNED_SHORT_4_4_4_4:
		GlobalThreadPool::Loop(bind(&convert4444, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_6_5:
		GlobalThreadPool::Loop(bind(&convert565, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_5_5_1:
		GlobalThreadPool::Loop(bind(&convert5551, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
