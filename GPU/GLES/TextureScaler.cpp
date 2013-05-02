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
#include "Common/ThreadPool.h"
#include "ext/xbrz/xbrz.h"

#ifdef __SYMBIAN32__
#define p
#elif defined(IOS)
#include <tr1/functional>
namespace p = std::tr1::placeholders;
#else
namespace p = std::placeholders;
#endif

// Hack for Meego
#ifdef MEEGO_EDITION_HARMATTAN
#include "Common/ThreadPool.cpp"
#endif

// Report the time and throughput for each larger scaling operation in the log
#define SCALING_MEASURE_TIME

#ifdef SCALING_MEASURE_TIME
#include "native/base/timeutil.h"
#endif

namespace {
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

	void convertSum(u32* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = data[y*width + x];
				out[y*width + x] = (val>>24)&0xFF + (val>>16)&0xFF + (val>>8)&0xFF + (val>>0)&0xFF;
			}
		}
	}

	// 3x3 convolution with Neumann boundary conditions
	// quite slow, could be sped up a lot
	// especially handling of separable kernels
	void convolve3x3(u32* data, u32* out, const int kernel[3][3], int width, int height, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				int val = 0;
				for(int yoff = -1; yoff < 1; ++yoff) {
					int yy = std::max(std::min(y+yoff, height-1), 0);
					for(int xoff = -1; xoff < 1; ++xoff) {
						int xx = std::max(std::min(x+xoff, width-1), 0);
						val += data[yy*width + xx] * kernel[yoff+1][xoff+1];
					}
				}
				out[y*width + x] = abs(val);
			}
		}
	}

	// this is sadly much faster than an inline function with a loop
	#define MIX_PIXELS(p0, p1, p2, factors) \
		((((p0>> 0)&0xFF)*factors[0] + ((p1>> 0)&0xFF)*factors[1] + ((p2>> 0)&0xFF)*factors[2])/255 <<  0 ) | \
		((((p0>> 8)&0xFF)*factors[0] + ((p1>> 8)&0xFF)*factors[1] + ((p2>> 8)&0xFF)*factors[2])/255 <<  8 ) | \
		((((p0>>16)&0xFF)*factors[0] + ((p1>>16)&0xFF)*factors[1] + ((p2>>16)&0xFF)*factors[2])/255 << 16 ) | \
		((((p0>>24)&0xFF)*factors[0] + ((p1>>24)&0xFF)*factors[1] + ((p2>>24)&0xFF)*factors[2])/255 << 24 )

	void mix(u32* data, u32* source, u32* mask, u32 maskmax, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				int pos = y*width + x;
				u8 mixFactors[3] = {(std::min(mask[pos], maskmax)*255)/maskmax, 0, 0 };
				mixFactors[1] = 255-mixFactors[0];
				data[pos] = MIX_PIXELS(data[pos], source[pos], 0, mixFactors);
			}
		}
	}
	
	const static u8 BILINEAR_FACTORS[4][5][3] = {
		{ { 76,179,  0}, {  0,179, 76}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0} }, // x2
		{ { 85,170,  0}, {  0,255,  0}, {  0,170, 85}, {  0,  0,  0}, {  0,  0,  0} }, // x3
		{ {102,153,  0}, { 51,204,  0}, {  0,204, 51}, {  0,153,102}, {  0,  0,  0} }, // x4
		{ {102,153,  0}, { 51,204,  0}, {  0,255,  0}, {  0,204, 51}, {  0,153,102} }, // x5
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
				for(int i=0; i<f; ++i) { // hope the compiler unrolls this
					out[y*outw + x*f + i] = MIX_PIXELS(left, center, right, BILINEAR_FACTORS[f-2][i]);
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
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < outw; ++x) {
				u32 upper  = data[(y - (y==gl  ?0:1)) * outw + x];
				u32 center = data[y * outw + x];
				u32 lower  = data[(y + (y==gu-1?0:1)) * outw + x];
				for(int i=0; i<f; ++i) { // hope the compiler unrolls this
					out[(y*f + i)*outw + x] = MIX_PIXELS(upper, center, lower, BILINEAR_FACTORS[f-2][i]);
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

	#undef MIX_PIXELS
}


TextureScaler::TextureScaler() {
}

void TextureScaler::Scale(u32* &data, GLenum &dstFmt, int &width, int &height, int factor) {
	#ifdef SCALING_MEASURE_TIME
	double t_start = real_time_now();
	#endif

	bufInput.resize(width*height); // used to store the input image image if it needs to be reformatted
	bufOutput.resize(width*height*factor*factor); // used to store the upscaled image
	u32 *inputBuf = bufInput.data();
	u32 *outputBuf = bufOutput.data();

	// convert texture to correct format for scaling
	ConvertTo8888(dstFmt, data, inputBuf, width, height);

	// scale 
	switch(g_Config.iTexScalingType) {
	case BILINEAR:
		ScaleBilinear(factor, inputBuf, outputBuf, width, height);
		break;
	case XBRZ:
		ScaleXBRZ(factor, inputBuf, outputBuf, width, height);
		break;
	case HYBRID:
		ScaleHybrid(factor, inputBuf, outputBuf, width, height);
		break;
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
	GlobalThreadPool::Loop(std::bind(&xbrz::scale, factor, source, dest, width, height, cfg, p::_1, p::_2), 0, height);
}

void TextureScaler::ScaleBilinear(int factor, u32* source, u32* dest, int width, int height) {
	bufTmp1.resize(width*height*factor);
	u32 *tmpBuf = bufTmp1.data();
	GlobalThreadPool::Loop(std::bind(&bilinearH, factor, source, tmpBuf, width, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(std::bind(&bilinearV, factor, tmpBuf, dest, width, 0, height, p::_1, p::_2), 0, height);
}

void TextureScaler::ScaleHybrid(int factor, u32* source, u32* dest, int width, int height) {
	// Basic algorithm:
	// 1) determine a feature mask C based on a sobel-ish filter + splatting, and upscale that mask bilinearly
	// 2) generate 2 scaled images: A - using Bilinear filtering, B - using xBRZ
	// 3) output = A*C + B*(1-C)

	const static int KERNEL_EDGE_DETECT[3][3] = {
		{ -1, -1, -1 }, { -1, 8, -1 }, { -1, -1, -1 }
	};
	const static int KERNEL_SPLAT[3][3] = {
		{ 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }
	};

	bufTmp1.resize(width*height);
	bufTmp2.resize(width*height*factor*factor);
	bufTmp3.resize(width*height*factor*factor);
	GlobalThreadPool::Loop(std::bind(&convertSum, source, bufTmp2.data(), width, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(std::bind(&convolve3x3, bufTmp2.data(), bufTmp1.data(), KERNEL_EDGE_DETECT, width, height, p::_1, p::_2), 0, height);
	GlobalThreadPool::Loop(std::bind(&convolve3x3, bufTmp1.data(), bufTmp2.data(), KERNEL_SPLAT, width, height, p::_1, p::_2), 0, height);
	ScaleBilinear(factor, bufTmp2.data(), bufTmp3.data(), width, height);
	// mask C is now in bufTmp3

	ScaleXBRZ(factor, source, bufTmp2.data(), width, height);
	// xBRZ upscaled source is in bufTmp2

	ScaleBilinear(factor, source, dest, width, height);
	// Bilinear upscaled source is in dest

	// Now we can mix it all together
	GlobalThreadPool::Loop(std::bind(&mix, dest, bufTmp2.data(), bufTmp3.data(), 333, width, p::_1, p::_2), 0, height);
}

void TextureScaler::ConvertTo8888(GLenum format, u32* source, u32* &dest, int width, int height) {
	switch(format) {
	case GL_UNSIGNED_BYTE:
		dest = source; // already fine
		break;

	case GL_UNSIGNED_SHORT_4_4_4_4:
		GlobalThreadPool::Loop(std::bind(&convert4444, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_6_5:
		GlobalThreadPool::Loop(std::bind(&convert565, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_5_5_1:
		GlobalThreadPool::Loop(std::bind(&convert5551, (u16*)source, dest, width, p::_1, p::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
