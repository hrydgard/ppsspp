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

namespace p = std::placeholders;

// Report the time and throughput for each larger scaling operation in the log
#define SCALING_MEASURE_TIME

#ifdef SCALING_MEASURE_TIME
#include "native/base/timeutil.h"
#endif

namespace {
	void convert4444(u16* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = ((u16*)data)[y*width + x];
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
				u32 val = ((u16*)data)[y*width + x];
				u32 r = ((val>>11) & 0x1F) * 8;
				u32 g = ((val>> 5) & 0x3F) * 4;
				u32 b = ((val    ) & 0x1F) * 8;
				out[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
			}
		}
	}

	void convert5551(u16* data, u32* out, int width, int l, int u) {
		for(int y = l; y < u; ++y) {
			for(int x = 0; x < width; ++x) {
				u32 val = ((u16*)data)[y*width + x];
				u32 r = ((val>>11) & 0x1F) * 8;
				u32 g = ((val>> 6) & 0x1F) * 8;
				u32 b = ((val>> 1) & 0x1F) * 8;
				u32 a = (val & 0x1) * 255;
				out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
			}
		}
	}
	
	// this is sadly much faster than an inline function with a loop
	#define MIX_PIXELS(p0, p1, p2, factors) \
		((((p0>> 0)&0xFF)*factors[0] + ((p1>> 0)&0xFF)*factors[1] + ((p2>> 0)&0xFF)*factors[2])/255 <<  0 ) | \
		((((p0>> 8)&0xFF)*factors[0] + ((p1>> 8)&0xFF)*factors[1] + ((p2>> 8)&0xFF)*factors[2])/255 <<  8 ) | \
		((((p0>>16)&0xFF)*factors[0] + ((p1>>16)&0xFF)*factors[1] + ((p2>>16)&0xFF)*factors[2])/255 << 16 ) | \
		((((p0>>24)&0xFF)*factors[0] + ((p1>>24)&0xFF)*factors[1] + ((p2>>24)&0xFF)*factors[2])/255 << 24 )

	const static u8 BILINEAR_FACTORS[4][5][3] = {
		{ {127,128,  0}, {  0,128,127}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0} }, // x2
		{ {170, 85,  0}, {  0,255,  0}, {  0, 85,170}, {  0,  0,  0}, {  0,  0,  0} }, // x3
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

void TextureScaler::Scale(u32* &data, GLenum &dstFmt, int &width, int &height) {
	if(g_Config.iTexScalingLevel > 1) {
		#ifdef SCALING_MEASURE_TIME
		double t_start = real_time_now();
		#endif

		int factor = g_Config.iTexScalingLevel;

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
