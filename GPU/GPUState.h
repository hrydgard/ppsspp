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

struct GPUgstate
{
	// Getting rid of this ugly union in favor of the accessor functions
	// might be a good idea....
	union
	{
		u32 cmdmem[256];
		struct
		{
			u32 nop,
				vaddr,
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
				pad04[2],
				patchdivision,
				patchprimitive,
				patchfacing,
				pad04_a,

				worldmtxnum,  //0x3A
				worldmtxdata,  //0x3B
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
				shademodel,
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
				lmode,
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
				texlevel,
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
				pmskc,
				pmska,
				transferstart,
				transfersrcpos,
				transferdstpos,
				pad99,
				transfersize;  // 0xEE

			u32 pad05[0xFF- 0xEE];
		};
	};

	float worldMatrix[12];
	float viewMatrix[12];
	float projMatrix[16];
	float tgenMatrix[12];
	float boneMatrix[12 * 8];  // Eight bone matrices.

	// Pixel Pipeline
	bool isModeClear()   const { return clearmode & 1; }
	bool isCullEnabled() const { return cullfaceEnable & 1; }
	int getCullMode()   const { return cullmode & 1; }
	int getBlendFuncA() const { return blend & 0xF; }
	u32 getFixA() const { return blendfixa & 0xFFFFFF; }
	u32 getFixB() const { return blendfixb & 0xFFFFFF; }
	int getBlendFuncB() const { return (blend >> 4) & 0xF; }
	int getBlendEq()    const { return (blend >> 8) & 0x7; }
	bool isDepthTestEnabled() const { return zTestEnable & 1; }
	bool isDepthWriteEnabled() const { return !(zmsk & 1); }
	int getDepthTestFunc() const { return ztestfunc & 0x7; }
	bool isFogEnabled() const { return fogEnable & 1; }
	bool isStencilTestEnabled() const { return stencilTestEnable & 1; }
	bool isAlphaBlendEnabled() const { return alphaBlendEnable & 1; }
	bool isDitherEnabled() const { return ditherEnable & 1; }
	bool isAlphaTestEnabled() const { return alphaTestEnable & 1; }
	bool isColorTestEnabled() const { return colorTestEnable & 1; }
	bool isLightingEnabled() const { return lightingEnable & 1; }
	bool isTextureMapEnabled() const { return textureMapEnable & 1; }

	// UV gen
	int getUVGenMode() const { return texmapmode & 3;}   // 2 bits
	int getUVProjMode() const { return (texmapmode >> 8) & 3;}   // 2 bits
	int getUVLS0() const { return texshade & 0x3; }  // 2 bits
	int getUVLS1() const { return (texshade >> 8) & 0x3; }  // 2 bits

	int getScissorX1() const { return scissor1 & 0x3FF; }
	int getScissorY1() const { return (scissor1 >> 10) & 0x3FF; }
	int getScissorX2() const { return scissor2 & 0x3FF; }
	int getScissorY2() const { return (scissor2 >> 10) & 0x3FF; }

	// Vertex type
	bool isModeThrough() const { return (vertType & GE_VTYPE_THROUGH) != 0; }
	int getNumBoneWeights() const {
		return 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT);
	}

// Real data in the context ends here
};

enum SkipDrawReasonFlags {
	SKIPDRAW_SKIPFRAME = 1,
	SKIPDRAW_NON_DISPLAYED_FB = 2,   // Skip drawing to FBO:s that have not been displayed.
	SKIPDRAW_BAD_FB_TEXTURE = 4,
};

// The rest is cached simplified/converted data for fast access.
// Does not need to be saved when saving/restoring context.
struct GPUStateCache
{
	u32 vertexAddr;
	u32 indexAddr;

	u32 offsetAddr;

	bool textureChanged;
	bool textureFullAlpha;
	bool framebufChanged;

	int skipDrawReason;

	float uScale,vScale;
	float uOff,vOff;
	bool flipTexture;

	float zMin, zMax;
	float lightpos[4][3];
	float lightdir[4][3];
	float lightatt[4][3];
	float lightColor[3][4][3];  // Ambient Diffuse Specular
	float lightangle[4]; // spotlight cone angle (cosine)
	float lightspotCoef[4]; // spotlight dropoff
	float morphWeights[8];

	u32 curTextureWidth;
	u32 curTextureHeight;
	u32 actualTextureHeight;

	float vpWidth;
	float vpHeight;

	u32 curRTWidth;
	u32 curRTHeight;

	u32 getRelativeAddress(u32 data) const;
};

// TODO: Implement support for these.
struct GPUStatistics
{
	void reset() {
		memset(this, 0, sizeof(*this));
	}
	void resetFrame() {
		numDrawCalls = 0;
		numCachedDrawCalls = 0;
		numVertsSubmitted = 0;
		numCachedVertsDrawn = 0;
		numUncachedVertsDrawn = 0;
		numTrackedVertexArrays = 0;
		numTextureInvalidations = 0;
		numTextureSwitches = 0;
		numShaderSwitches = 0;
		numFlushes = 0;
		numTexturesDecoded = 0;
		msProcessingDisplayLists = 0;
		vertexGPUCycles = 0;
		otherGPUCycles = 0;
	}

	// Per frame statistics
	int numDrawCalls;
	int numCachedDrawCalls;
	int numFlushes;
	int numVertsSubmitted;
	int numCachedVertsDrawn;
	int numUncachedVertsDrawn;
	int numTrackedVertexArrays;
	int numTextureInvalidations;
	int numTextureSwitches;
	int numShaderSwitches;
	int numTexturesDecoded;
	double msProcessingDisplayLists;
	int vertexGPUCycles;
	int otherGPUCycles;

	// Total statistics, updated by the GPU core in UpdateStats
	int numFrames;
	int numTextures;
	int numVertexShaders;
	int numFragmentShaders;
	int numShaders;
	int numFBOs;
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

inline u32 GPUStateCache::getRelativeAddress(u32 data) const {
	u32 baseExtended = ((gstate.base & 0x0F0000) << 8) | data;
	return (gstate_c.offsetAddr + baseExtended) & 0x0FFFFFFF;
}
