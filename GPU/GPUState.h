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

#pragma once

#include "../Globals.h"
#include "../native/gfx/gl_common.h"
#include "ge_constants.h"
#include <cstring>

// TODO: this doesn't belong here
struct Color4
{
	float r,g,b,a;
	Color4() : r(0), g(0), b(0), a(0) { }
	Color4(float _r, float _g, float _b, float _a=1.0f)
	{
		r=_r; g=_g; b=_b; a=_a;
	}
	Color4(const float in[4]) {r=in[0];g=in[1];b=in[2];a=in[3];}

	float &operator [](int i) {return *(&r + i);}
	const float &operator [](int i) const {return *(&r + i);}

	Color4 operator *(float f) const
	{
		return Color4(f*r,f*g,f*b,f*a);
	}
	Color4 operator *(const Color4 &c) const
	{
		return Color4(r*c.r,g*c.g,b*c.b,a*c.a);
	}
	void operator *=(const Color4 &c)
	{
		r*=c.r,g*=c.g,b*=c.b,a*=c.a;
	}
	Color4 operator +(const Color4 &c) const
	{
		return Color4(r+c.r,g+c.g,b+c.b,a+c.a);
	}
	void operator +=(const Color4 &c)
	{
		r+=c.r;
		g+=c.g;
		b+=c.b;
		a+=c.a;
	}
	void GetFromRGB(u32 col)
	{
		r = ((col>>16)&0xff)/255.0f;
		g = ((col>>8)&0xff)/255.0f;
		b = ((col>>0)&0xff)/255.0f;
	}
	void GetFromA(u32 col)
	{
		a = (col&0xff)/255.0f;
	}
};


struct GPUgstate
{
	union
	{
		u32 cmdmem[256];
		struct
		{
			int nop;
			u32 vaddr,
				iaddr,
				pad00,
				prim,
				bezier,
				spline,
				boundBox,
				jump,
				bjump,
				call,
				ret,
				end,
				pad01,
				signal,
				finish,
				base,
				pad02,
				vertType,
				offsetAddr,
				origin,
				region1,
				region2,
				lightingEnable,
				lightEnable[4],
				clipEnable,
				cullfaceEnable,
				textureMapEnable,
				fogEnable,
				ditherEnable,
				alphaBlendEnable,
				alphaTestEnable,
				zTestEnable,
				stencilTestEnable,
				antiAliasEnable,
				patchCullEnable,
				colorTestEnable,
				logicOpEnable,
				pad03,
				boneMatrixNumber,
				boneMatrixData,
				morphwgt[8], //dont use
				pad04[0x39-0x33],

				worldmtxnum,//0x3A
				worldmtxdata, //0x3B
				viewmtxnum,	 //0x3C
				viewmtxdata,
				projmtxnum,
				projmtxdata,
				texmtxnum,
				texmtxdata,

				viewportx1,
				viewporty1,
				viewportz1,
				viewportx2,
				viewporty2,
				viewportz2,
				texscaleu,
				texscalev,
				texoffsetu,
				texoffsetv,
				offsetx,
				offsety,
				pad111[2],
				lmode,
				reversenormals,
				pad222,
				materialupdate,
				materialemissive,
				materialambient,
				materialdiffuse,
				materialspecular,
				materialalpha,
				pad333[2],
				materialspecularcoef,
				ambientcolor,
				ambientalpha,
				colormodel,
				ltype[4],
				lpos[12],
				ldir[12],
				latt[12],
				lconv[4],
				lcutoff[4],
				lcolor[12],
				cullmode,
				fbptr,
				fbwidth,
				zbptr,
				zbwidth,
				texaddr[8],
				texbufwidth[8],
				clutaddr,
				clutaddrupper,
				transfersrc,
				transfersrcw,
				transferdst,
				transferdstw,
				padxxx[2],
				texsize[8],
				texmapmode,
				texshade,
				texmode,
				texformat,
				loadclut,
				clutformat,
				texfilter,
				texwrap,
				padxxxxx,
				texfunc,
				texenvcolor,
				texflush,
				texsync,
				fog1,
				fog2,
				fogcolor,
				texlodslope,
				padxxxxxx,
				framebufpixformat,
				clearmode,
				scissor1,
				scissor2,
				minz,
				maxz,
				colortest,
				colorref,
				colormask,
				alphatest,
				stenciltest,
				stencilop,
				ztestfunc,
				blend,
				blendfixa,
				blendfixb,
				dith1,
				dith2,
				dith3,
				dith4,
				lop,
				zmsk,
				pmsk1,
				pmsk2,
				transferstart,
				transfersrcpos,
				transferdstpos,
				transfersize				
				;

			u32 pad05[0x63-0x40];

		};
	};

	float worldMatrix[12];
	float viewMatrix[12];
	float projMatrix[16];
	float tgenMatrix[12];
	float boneMatrix[12 * 8];  // Eight bone matrices.

	bool isModeThrough() const { return (vertType & GE_VTYPE_THROUGH) != 0; }
	bool isModeClear()   const { return clearmode & 1; }
	bool isCullEnabled() const { return cullfaceEnable & 1; }
	int  getCullMode()   const { return cullmode & 1; }
	int  getBlendFuncA() const { return blend & 0xF; }
	u32 getFixA() const { return blendfixa & 0xFFFFFF; }
	u32 getFixB() const { return blendfixb & 0xFFFFFF; }
	int  getBlendFuncB() const { return (blend >> 4) & 0xF; }
	int  getBlendEq()    const { return (blend >> 8) & 0x7; }
	bool isDepthTestEnabled() const { return zTestEnable & 1; }
	bool isDepthWriteEnabled() const { return !(zmsk & 1); }
	int  getDepthTestFunc() const { return ztestfunc & 0x7; }
	bool isFogEnabled() const { return fogEnable & 1; }
};
// Real data in the context ends here

// The rest is cached simplified/converted data for fast access.
// Does not need to be saved when saving/restoring context.
struct GPUStateCache
{
	u32 vertexAddr;
	u32 indexAddr;

	bool textureChanged;

	float uScale,vScale,zScale;
	float uOff,vOff,zOff;
	float zMin, zMax;
	float lightpos[4][3];
	float lightdir[4][3];
	float lightatt[4][3];
	Color4 lightColor[3][4]; //Amtient Diffuse Specular
	float morphWeights[8];

	// bezier patch subdivision
	int patch_div_s;
	int patch_div_t;

	u32 curTextureWidth;
	u32 curTextureHeight;

	float vpWidth;
	float vpHeight;
};

// TODO: Implement support for these.
struct GPUStatistics
{
	void reset() {
		memset(this, 0, sizeof(*this));
	}
	void resetFrame() {
		numDrawCalls = 0;
		numVertsTransformed = 0;
		numTextureSwitches = 0;
		numShaderSwitches = 0;
	}

	// Per frame statistics
	int numDrawCalls;
	int numVertsTransformed;
	int numTextureSwitches;
	int numShaderSwitches;

	// Total statistics, updated by the GPU core in UpdateStats
	int numFrames;
	int numTextures;
	int numVertexShaders;
	int numFragmentShaders;
	int numShaders;
};

void InitGfxState();
void ShutdownGfxState();
void ReapplyGfxState();

// PSP uses a curious 24-bit float - it's basically the top 24 bits of a regular IEEE754 32-bit float.
// This is used for light positions, transform matrices, you name it.
inline float getFloat24(unsigned int data)
{
	data <<= 8;
	float f;
	memcpy(&f, &data, 4);
	return f;
}

// in case we ever want to generate PSP display lists...
inline unsigned int toFloat24(float f) {
	unsigned int i;
	memcpy(&i, &f, 4);
	return i >> 8;
}

class GPUInterface;

extern GPUgstate gstate;
extern GPUStateCache gstate_c;
extern GPUInterface *gpu;
extern GPUStatistics gpuStats;
