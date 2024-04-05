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

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

class PointerWrap;

struct GPUgstate {
	// Getting rid of this ugly union in favor of the accessor functions
	// might be a good idea....
	union {
		u32 cmdmem[256];
		struct {
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
				depthClampEnable,
				cullfaceEnable,
				textureMapEnable,  // 0x1E GE_CMD_TEXTUREMAPENABLE
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

				worldmtxnum,  // 0x3A
				worldmtxdata, // 0x3B
				viewmtxnum,   // 0x3C
				viewmtxdata,  // 0x3D
				projmtxnum,   // 0x3E
				projmtxdata,  // 0x3F
				texmtxnum,    // 0x40
				texmtxdata,   // 0x41

				viewportxscale,           // 0x42
				viewportyscale,           // 0x43
				viewportzscale,           // 0x44
				viewportxcenter,           // 0x45
				viewportycenter,           // 0x46
				viewportzcenter,           // 0x47
				texscaleu,            // 0x48
				texscalev,            // 0x49
				texoffsetu,           // 0x4A
				texoffsetv,           // 0x4B
				offsetx,              // 0x4C
				offsety,              // 0x4D
				pad111[2],
				shademodel,           // 0x50
				reversenormals,       // 0x51
				pad222,
				materialupdate,       // 0x53
				materialemissive,     // 0x54
				materialambient,      // 0x55
				materialdiffuse,      // 0x56
				materialspecular,     // 0x57
				materialalpha,        // 0x58
				pad333[2],
				materialspecularcoef, // 0x5B
				ambientcolor,         // 0x5C
				ambientalpha,         // 0x5D
				lmode,                // 0x5E      GE_CMD_LIGHTMODE
				ltype[4],             // 0x5F-0x62 GE_CMD_LIGHTTYPEx
				lpos[12],             // 0x63-0x6E
				ldir[12],             // 0x6F-0x7A
				latt[12],             // 0x7B-0x86
				lconv[4],             // 0x87-0x8A
				lcutoff[4],           // 0x8B-0x8E
				lcolor[12],           // 0x8F-0x9A
				cullmode,             // 0x9B
				fbptr,                // 0x9C
				fbwidth,              // 0x9D
				zbptr,                // 0x9E
				zbwidth,              // 0x9F
				texaddr[8],           // 0xA0-0xA7
				texbufwidth[8],       // 0xA8-0xAF
				clutaddr,             // 0xB0
				clutaddrupper,        // 0xB1
				transfersrc,          // 0xB2
				transfersrcw,         // 0xB3
				transferdst,          // 0xB4
				transferdstw,         // 0xB5
				padxxx[2],
				texsize[8],           // 0xB8-BF
				texmapmode,           // 0xC0
				texshade,             // 0xC1
				texmode,              // 0xC2 GE_CMD_TEXMODE
				texformat,            // 0xC3
				loadclut,             // 0xC4
				clutformat,           // 0xC5
				texfilter,            // 0xC6
				texwrap,              // 0xC7
				texlevel,             // 0xC8
				texfunc,              // 0xC9
				texenvcolor,          // 0xCA
				texflush,             // 0xCB
				texsync,              // 0xCC
				fog1,                 // 0xCD
				fog2,                 // 0xCE
				fogcolor,             // 0xCF
				texlodslope,          // 0xD0
				padxxxxxx,            // 0xD1
				framebufpixformat,    // 0xD2
				clearmode,            // 0xD3 GE_CMD_CLEARMODE
				scissor1,
				scissor2,
				minz,
				maxz,
				colortest,
				colorref,
				colortestmask,
				alphatest,
				stenciltest,
				stencilop,
				ztestfunc,
				blend,
				blendfixa,
				blendfixb,
				dithmtx[4],
				lop,                  // 0xE6
				zmsk,
				pmskc,
				pmska,
				transferstart,
				transfersrcpos,
				transferdstpos,
				pad99,
				transfersize,  // 0xEE
				pad100,         // 0xEF
				imm_vscx,        // 0xF0
				imm_vscy,
				imm_vscz,
				imm_vtcs,
				imm_vtct,
				imm_vtcq,
				imm_cv,
				imm_ap,
				imm_fc,
				imm_scv;   // 0xF9
				// In the unlikely case we ever add anything else here, don't forget to update the padding on the next line!
			u32 pad05[0xFF- 0xF9];
		};
	};

	// These are not directly mapped, instead these are loaded one-by-one through special commands.
	// However, these are actual state, and can be read back.
	float worldMatrix[12];  // 4x3
	float viewMatrix[12];   // 4x3
	float projMatrix[16];   // 4x4
	float tgenMatrix[12];   // 4x3
	float boneMatrix[12 * 8];  // Eight 4x3 bone matrices.

	// We ignore the high bits of the framebuffer in fbwidth - even 0x08000000 renders to vRAM.
	// The top bits of mirroring are also not respected, so we mask them away.
	u32 getFrameBufRawAddress() const { return fbptr & 0x1FFFF0; }
	// 0x44000000 is uncached VRAM.
	u32 getFrameBufAddress() const { return 0x44000000 | getFrameBufRawAddress(); }
	GEBufferFormat FrameBufFormat() const { return static_cast<GEBufferFormat>(framebufpixformat & 3); }
	int FrameBufStride() const { return fbwidth&0x7FC; }
	u32 getDepthBufRawAddress() const { return zbptr & 0x1FFFF0; }
	u32 getDepthBufAddress() const { return 0x44600000 | getDepthBufRawAddress(); }
	int DepthBufStride() const { return zbwidth&0x7FC; }

	// Pixel Pipeline
	bool isModeClear()   const { return clearmode & 1; }
	bool isFogEnabled() const { return fogEnable & 1; }
	float getFogCoef1() const { return getFloat24(fog1); }
	float getFogCoef2() const { return getFloat24(fog2); }

	// Cull
	bool isCullEnabled() const { return cullfaceEnable & 1; }
	int getCullMode()   const { return cullmode & 1; }

	// Color Mask
	bool isClearModeColorMask() const { return (clearmode&0x100) != 0; }
	bool isClearModeAlphaMask() const { return (clearmode&0x200) != 0; }
	bool isClearModeDepthMask() const { return (clearmode&0x400) != 0; }
	u32 getClearModeColorMask() const { return ((clearmode&0x100) ? 0 : 0xFFFFFF) | ((clearmode&0x200) ? 0 : 0xFF000000); }

	// Blend
	GEBlendSrcFactor getBlendFuncA() const { return (GEBlendSrcFactor)(blend & 0xF); }
	GEBlendDstFactor getBlendFuncB() const { return (GEBlendDstFactor)((blend >> 4) & 0xF); }
	u32 getFixA() const { return blendfixa & 0xFFFFFF; }
	u32 getFixB() const { return blendfixb & 0xFFFFFF; }
	GEBlendMode getBlendEq() const { return static_cast<GEBlendMode>((blend >> 8) & 0x7); }
	bool isAlphaBlendEnabled() const { return alphaBlendEnable & 1; }

	// AntiAlias
	bool isAntiAliasEnabled() const { return antiAliasEnable & 1; }

	// Dither
	bool isDitherEnabled() const { return ditherEnable & 1; }
	int getDitherValue(int x, int y) const {
		u8 raw = (dithmtx[y & 3] >> ((x & 3) * 4)) & 0xF;
		// Apply sign extension to make 8-F negative, 0-7 positive.
		return ((s8)(raw << 4)) >> 4;
	}

	// Color Mask
	u32 getColorMask() const { return (pmskc & 0xFFFFFF) | ((pmska & 0xFF) << 24); }
	u8 getStencilWriteMask() const { return pmska & 0xFF; }
	bool isLogicOpEnabled() const { return logicOpEnable & 1; }
	GELogicOp getLogicOp() const { return static_cast<GELogicOp>(lop & 0xF); }

	// Depth Test
	bool isDepthTestEnabled() const { return zTestEnable & 1; }
	bool isDepthWriteEnabled() const { return !(zmsk & 1); }
	GEComparison getDepthTestFunction() const { return static_cast<GEComparison>(ztestfunc & 0x7); }
	u16 getDepthRangeMin() const { return minz & 0xFFFF; }
	u16 getDepthRangeMax() const { return maxz & 0xFFFF; }

	// Stencil Test
	bool isStencilTestEnabled() const { return stencilTestEnable & 1; }
	GEComparison getStencilTestFunction() const { return static_cast<GEComparison>(stenciltest & 0x7); }
	int getStencilTestRef() const { return (stenciltest>>8) & 0xFF; }
	int getStencilTestMask() const { return (stenciltest>>16) & 0xFF; }
	GEStencilOp getStencilOpSFail() const { return static_cast<GEStencilOp>(stencilop & 0x7); }
	GEStencilOp getStencilOpZFail() const { return static_cast<GEStencilOp>((stencilop>>8) & 0x7); }
	GEStencilOp getStencilOpZPass() const { return static_cast<GEStencilOp>((stencilop>>16) & 0x7); }

	// Alpha Test
	bool isAlphaTestEnabled() const { return alphaTestEnable & 1; }
	GEComparison getAlphaTestFunction() const { return static_cast<GEComparison>(alphatest & 0x7); }
	int getAlphaTestRef() const { return (alphatest >> 8) & 0xFF; }
	int getAlphaTestMask() const { return (alphatest >> 16) & 0xFF; }

	// Color Test
	bool isColorTestEnabled() const { return colorTestEnable & 1; }
	GEComparison getColorTestFunction() const { return static_cast<GEComparison>(colortest & 0x3); }
	u32 getColorTestRef() const { return colorref & 0xFFFFFF; }
	u32 getColorTestMask() const { return colortestmask & 0xFFFFFF; }

	// Texturing
	// TODO: Verify getTextureAddress() alignment?
	u32 getTextureAddress(int level) const { return (texaddr[level] & 0xFFFFF0) | ((texbufwidth[level] << 8) & 0x0F000000); }
	int getTextureWidth(int level) const { return 1 << (texsize[level] & 0xf);}
	int getTextureHeight(int level) const { return 1 << ((texsize[level] >> 8) & 0xf);}
	u16 getTextureDimension(int level) const { return  texsize[level] & 0xf0f;}
	GETexLevelMode getTexLevelMode() const { return static_cast<GETexLevelMode>(texlevel & 0x3); }
	int getTexLevelOffset16() const { return (int)(s8)((texlevel >> 16) & 0xFF); }
	bool isTextureMapEnabled() const { return textureMapEnable & 1; }
	GETexFunc getTextureFunction() const { return static_cast<GETexFunc>(texfunc & 0x7); }
	bool isColorDoublingEnabled() const { return (texfunc & 0x10000) != 0; }
	bool isTextureAlphaUsed() const { return (texfunc & 0x100) != 0; }
	GETextureFormat getTextureFormat() const { return static_cast<GETextureFormat>(texformat & 0xF); }
	bool isTextureFormatIndexed() const { return (texformat & 4) != 0; } // GE_TFMT_CLUT4 - GE_TFMT_CLUT32 are 0b1xx.
	int getTextureEnvColRGB() const { return texenvcolor & 0x00FFFFFF; }
	u32 getClutAddress() const { return (clutaddr & 0x00FFFFF0) | ((clutaddrupper << 8) & 0x0F000000); }
	int getClutLoadBytes() const { return getClutLoadBlocks() * 32; }
	int getClutLoadBlocks() const {
		// The PSP only supports 0x3F, but Misshitsu no Sacrifice has extra color data (see #15727.)
		// 0x40 would be 0, which would be a no-op, so we allow it.
		if ((loadclut & 0x7F) == 0x40)
			return 0x40;
		return loadclut & 0x3F;
	}
	GEPaletteFormat getClutPaletteFormat() const { return static_cast<GEPaletteFormat>(clutformat & 3); }
	int getClutIndexShift() const { return (clutformat >> 2) & 0x1F; }
	int getClutIndexMask() const { return (clutformat >> 8) & 0xFF; }
	int getClutIndexStartPos() const { return ((clutformat >> 16) & 0x1F) << 4; }
	u32 transformClutIndex(u32 index) const {
		// We need to wrap any entries beyond the first 1024 bytes.
		u32 mask = getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 0xFF : 0x1FF;
		return ((index >> getClutIndexShift()) & getClutIndexMask()) | (getClutIndexStartPos() & mask);
	}
	bool isClutIndexSimple() const { return (clutformat & ~3) == 0xC500FF00; } // Meaning, no special mask, shift, or start pos.
	bool isTextureSwizzled() const { return texmode & 1; }
	bool isClutSharedForMipmaps() const { return (texmode & 0x100) == 0; }
	bool isMipmapEnabled() const { return (texfilter & 4) != 0; }
	bool isMipmapFilteringEnabled() const { return (texfilter & 2) != 0; }
	bool isMinifyFilteringEnabled() const { return (texfilter & 1) != 0; }
	bool isMagnifyFilteringEnabled() const { return (texfilter >> 8) & 1; }
	int getTextureMaxLevel() const { return (texmode >> 16) & 0x7; }
	float getTextureLodSlope() const { return getFloat24(texlodslope); }

	// Lighting
	bool isLightingEnabled() const { return lightingEnable & 1; }
	bool isLightChanEnabled(int chan) const { return lightEnable[chan] & 1; }
	GELightComputation getLightComputation(int chan) const { return static_cast<GELightComputation>(ltype[chan] & 0x3); }
	bool isUsingPoweredDiffuseLight(int chan) const { return getLightComputation(chan) == GE_LIGHTCOMP_ONLYPOWDIFFUSE; }
	bool isUsingSpecularLight(int chan) const { return getLightComputation(chan) == GE_LIGHTCOMP_BOTH; }
	bool isUsingSecondaryColor() const { return lmode & 1; }
	GELightType getLightType(int chan) const { return static_cast<GELightType>((ltype[chan] >> 8) & 3); }
	bool isDirectionalLight(int chan) const { return getLightType(chan) == GE_LIGHTTYPE_DIRECTIONAL; }
	bool isPointLight(int chan) const { return getLightType(chan) == GE_LIGHTTYPE_POINT; }
	bool isSpotLight(int chan) const { return getLightType(chan) >= GE_LIGHTTYPE_SPOT; }
	GEShadeMode getShadeMode() const { return static_cast<GEShadeMode>(shademodel & 1); }
	unsigned int getAmbientR() const { return ambientcolor&0xFF; }
	unsigned int getAmbientG() const { return (ambientcolor>>8)&0xFF; }
	unsigned int getAmbientB() const { return (ambientcolor>>16)&0xFF; }
	unsigned int getAmbientA() const { return ambientalpha&0xFF; }
	unsigned int getAmbientRGBA() const { return (ambientcolor&0xFFFFFF) | ((ambientalpha&0xFF)<<24); }
	unsigned int getMaterialUpdate() const { return materialupdate & 7; }
	unsigned int getMaterialAmbientR() const { return materialambient&0xFF; }
	unsigned int getMaterialAmbientG() const { return (materialambient>>8)&0xFF; }
	unsigned int getMaterialAmbientB() const { return (materialambient>>16)&0xFF; }
	unsigned int getMaterialAmbientA() const { return materialalpha&0xFF; }
	unsigned int getMaterialAmbientRGBA() const { return (materialambient & 0x00FFFFFF) | (materialalpha << 24); }
	unsigned int getMaterialDiffuseR() const { return materialdiffuse&0xFF; }
	unsigned int getMaterialDiffuseG() const { return (materialdiffuse>>8)&0xFF; }
	unsigned int getMaterialDiffuseB() const { return (materialdiffuse>>16)&0xFF; }
	unsigned int getMaterialDiffuse() const { return materialdiffuse & 0xffffff; }
	unsigned int getMaterialEmissiveR() const { return materialemissive&0xFF; }
	unsigned int getMaterialEmissiveG() const { return (materialemissive>>8)&0xFF; }
	unsigned int getMaterialEmissiveB() const { return (materialemissive>>16)&0xFF; }
	unsigned int getMaterialEmissive() const { return materialemissive & 0xffffff; }
	unsigned int getMaterialSpecularR() const { return materialspecular&0xFF; }
	unsigned int getMaterialSpecularG() const { return (materialspecular>>8)&0xFF; }
	unsigned int getMaterialSpecularB() const { return (materialspecular>>16)&0xFF; }
	unsigned int getMaterialSpecular() const { return materialspecular & 0xffffff; }
	float getMaterialSpecularCoef() const { return getFloat24(materialspecularcoef); }
	unsigned int getLightAmbientColorR(int chan) const { return lcolor[chan*3]&0xFF; }
	unsigned int getLightAmbientColorG(int chan) const { return (lcolor[chan*3]>>8)&0xFF; }
	unsigned int getLightAmbientColorB(int chan) const { return (lcolor[chan*3]>>16)&0xFF; }
	unsigned int getLightAmbientColor(int chan) const { return lcolor[chan*3]&0xFFFFFF; }
	unsigned int getDiffuseColorR(int chan) const { return lcolor[1+chan*3]&0xFF; }
	unsigned int getDiffuseColorG(int chan) const { return (lcolor[1+chan*3]>>8)&0xFF; }
	unsigned int getDiffuseColorB(int chan) const { return (lcolor[1+chan*3]>>16)&0xFF; }
	unsigned int getDiffuseColor(int chan) const { return lcolor[1+chan*3]&0xFFFFFF; }
	unsigned int getSpecularColorR(int chan) const { return lcolor[2+chan*3]&0xFF; }
	unsigned int getSpecularColorG(int chan) const { return (lcolor[2+chan*3]>>8)&0xFF; }
	unsigned int getSpecularColorB(int chan) const { return (lcolor[2+chan*3]>>16)&0xFF; }
	unsigned int getSpecularColor(int chan) const { return lcolor[2+chan*3]&0xFFFFFF; }

	int getPatchDivisionU() const { return patchdivision & 0x7F; }
	int getPatchDivisionV() const { return (patchdivision >> 8) & 0x7F; }

	// UV gen
	GETexMapMode getUVGenMode() const { return static_cast<GETexMapMode>(texmapmode & 3);}   // 2 bits
	GETexProjMapMode getUVProjMode() const { return static_cast<GETexProjMapMode>((texmapmode >> 8) & 3);}   // 2 bits
	int getUVLS0() const { return texshade & 0x3; }  // 2 bits
	int getUVLS1() const { return (texshade >> 8) & 0x3; }  // 2 bits

	bool isTexCoordClampedS() const { return texwrap & 1; }
	bool isTexCoordClampedT() const { return (texwrap >> 8) & 1; }

	int getScissorX1() const { return scissor1 & 0x3FF; }
	int getScissorY1() const { return (scissor1 >> 10) & 0x3FF; }
	int getScissorX2() const { return scissor2 & 0x3FF; }
	int getScissorY2() const { return (scissor2 >> 10) & 0x3FF; }
	int getRegionRateX() const { return 0x100 + (region1 & 0x3FF); }
	int getRegionRateY() const { return 0x100 + ((region1 >> 10) & 0x3FF); }
	int getRegionX2() const { return (region2 & 0x3FF); }
	int getRegionY2() const { return (region2 >> 10) & 0x3FF; }

	bool isDepthClampEnabled() const { return depthClampEnable & 1; }

	// Note that the X1/Y1/Z1 here does not mean the upper-left corner, but half the dimensions. X2/Y2/Z2 are the center.
	float getViewportXScale() const { return getFloat24(viewportxscale); }
	float getViewportYScale() const { return getFloat24(viewportyscale); }
	float getViewportZScale() const { return getFloat24(viewportzscale); }
	float getViewportXCenter() const { return getFloat24(viewportxcenter); }
	float getViewportYCenter() const { return getFloat24(viewportycenter); }
	float getViewportZCenter() const { return getFloat24(viewportzcenter); }

	// Fixed 12.4 point.
	int getOffsetX16() const { return offsetx & 0xFFFF; }
	int getOffsetY16() const { return offsety & 0xFFFF; }
	float getOffsetX() const { return (float)getOffsetX16() / 16.0f; }
	float getOffsetY() const { return (float)getOffsetY16() / 16.0f; }

	// Vertex type
	bool isModeThrough() const { return (vertType & GE_VTYPE_THROUGH) != 0; }
	bool areNormalsReversed() const { return reversenormals & 1; }
	bool isSkinningEnabled() const { return ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE); }
	int getNumMorphWeights() const { return ((vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT) + 1; }

	GEPatchPrimType getPatchPrimitiveType() const { return static_cast<GEPatchPrimType>(patchprimitive & 3); }
	bool isPatchNormalsReversed() const { return patchfacing & 1; }

	// Transfers
	u32 getTransferSrcAddress() const { return (transfersrc & 0xFFFFF0) | ((transfersrcw & 0xFF0000) << 8); }
	// Bits 0xf800 are ignored, > 0x400 is treated as 0.
	u32 getTransferSrcStride() const { int stride = transfersrcw & 0x7F8; return stride > 0x400 ? 0 : stride; }
	int getTransferSrcX() const { return (transfersrcpos >> 0) & 0x3FF; }
	int getTransferSrcY() const { return (transfersrcpos >> 10) & 0x3FF; }
	u32 getTransferDstAddress() const { return (transferdst & 0xFFFFF0) | ((transferdstw & 0xFF0000) << 8); }
	// Bits 0xf800 are ignored, > 0x400 is treated as 0.
	u32 getTransferDstStride() const { int stride = transferdstw & 0x7F8; return stride > 0x400 ? 0 : stride; }
	int getTransferDstX() const { return (transferdstpos >> 0) & 0x3FF; }
	int getTransferDstY() const { return (transferdstpos >> 10) & 0x3FF; }
	int getTransferWidth() const { return ((transfersize >> 0) & 0x3FF) + 1; }
	int getTransferHeight() const { return ((transfersize >> 10) & 0x3FF) + 1; }
	int getTransferBpp() const { return (transferstart & 1) ? 4 : 2; }


	void FastLoadBoneMatrix(u32 addr);

	// Real data in the context ends here

	static void Reset();
	void Save(u32_le *ptr);
	void Restore(const u32_le *ptr);
};

bool vertTypeIsSkinningEnabled(u32 vertType);

inline int vertTypeGetNumBoneWeights(u32 vertType) { return 1 + ((vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT); }
inline int vertTypeGetWeightMask(u32 vertType) { return vertType & GE_VTYPE_WEIGHT_MASK; }

// The rest is cached simplified/converted data for fast access.
// Does not need to be saved when saving/restoring context.
//
// Lots of this, however, is actual emulator state which must be saved when savestating.
// vertexAddr, indexAddr, offsetAddr for example.

struct UVScale {
	float uScale, vScale;
	float uOff, vOff;
};

#define FLAG_BIT(x) (1 << x)

// These flags are mainly to make sure that we make decisions on code path in a single
// location. Sometimes we need to take things into account in multiple places, it helps
// to centralize into flags like this. They're also fast to check since the cache line
// will be hot.
// NOTE: Do not forget to update the string array at the end of GPUState.cpp!
enum {
	GPU_USE_DUALSOURCE_BLEND = FLAG_BIT(0),
	GPU_USE_LIGHT_UBERSHADER = FLAG_BIT(1),
	GPU_USE_FRAGMENT_TEST_CACHE = FLAG_BIT(2),
	GPU_USE_VS_RANGE_CULLING = FLAG_BIT(3),
	GPU_USE_BLEND_MINMAX = FLAG_BIT(4),
	GPU_USE_LOGIC_OP = FLAG_BIT(5),
	GPU_USE_FRAGMENT_UBERSHADER = FLAG_BIT(6),
	GPU_USE_TEXTURE_NPOT = FLAG_BIT(7),
	GPU_USE_ANISOTROPY = FLAG_BIT(8),
	GPU_USE_CLEAR_RAM_HACK = FLAG_BIT(9),
	GPU_USE_INSTANCE_RENDERING = FLAG_BIT(10),
	GPU_USE_VERTEX_TEXTURE_FETCH = FLAG_BIT(11),
	GPU_USE_TEXTURE_FLOAT = FLAG_BIT(12),
	GPU_USE_16BIT_FORMATS = FLAG_BIT(13),
	GPU_USE_DEPTH_CLAMP = FLAG_BIT(14),
	GPU_USE_TEXTURE_LOD_CONTROL = FLAG_BIT(15),
	GPU_USE_DEPTH_TEXTURE = FLAG_BIT(16),
	GPU_USE_ACCURATE_DEPTH = FLAG_BIT(17),
	GPU_USE_GS_CULLING = FLAG_BIT(18),  // Geometry shader
	GPU_USE_FRAMEBUFFER_ARRAYS = FLAG_BIT(19),
	GPU_USE_FRAMEBUFFER_FETCH = FLAG_BIT(20),
	GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT = FLAG_BIT(21),
	GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT = FLAG_BIT(22),
	GPU_ROUND_DEPTH_TO_16BIT = FLAG_BIT(23),  // Can be disabled either per game or if we use a real 16-bit depth buffer
	GPU_USE_CLIP_DISTANCE = FLAG_BIT(24),
	GPU_USE_CULL_DISTANCE = FLAG_BIT(25),

	// VR flags (reserved or in-use)
	GPU_USE_VIRTUAL_REALITY = FLAG_BIT(29),
	GPU_USE_SINGLE_PASS_STEREO = FLAG_BIT(30),
	GPU_USE_SIMPLE_STEREO_PERSPECTIVE = FLAG_BIT(31),
};

// Note that this take a flag index, not the bit value.
const char *GpuUseFlagToString(int useFlag);

struct KnownVertexBounds {
	u16 minU;
	u16 minV;
	u16 maxU;
	u16 maxV;
};

enum class SubmitType {
	DRAW,
	BEZIER,
	SPLINE,
	HW_BEZIER,
	HW_SPLINE,
};

extern GPUgstate gstate;

struct GPUStateCache {
	bool Use(u32 flags) const { return (useFlags_ & flags) != 0; } // Return true if ANY of flags are true.
	bool UseAll(u32 flags) const { return (useFlags_ & flags) == flags; } // Return true if ALL flags are true.

	u32 UseFlags() const { return useFlags_; }

	uint64_t GetDirtyUniforms() { return dirty & DIRTY_ALL_UNIFORMS; }
	void Dirty(u64 what) {
		dirty |= what;
	}
	void CleanUniforms() {
		dirty &= ~DIRTY_ALL_UNIFORMS;
	}
	void Clean(u64 what) {
		dirty &= ~what;
	}
	bool IsDirty(u64 what) const {
		return (dirty & what) != 0ULL;
	}
	void SetUseShaderDepal(ShaderDepalMode mode) {
		if (mode != shaderDepalMode) {
			shaderDepalMode = mode;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}
	void SetTextureFullAlpha(bool fullAlpha) {
		if (fullAlpha != textureFullAlpha) {
			textureFullAlpha = fullAlpha;
			Dirty(DIRTY_FRAGMENTSHADER_STATE | DIRTY_TEX_ALPHA_MUL);
		}
	}
	void SetNeedShaderTexclamp(bool need) {
		if (need != needShaderTexClamp) {
			needShaderTexClamp = need;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
			if (need)
				Dirty(DIRTY_TEXCLAMP);
		}
	}
	void SetTextureIs3D(bool is3D) {
		if (is3D != curTextureIs3D) {
			curTextureIs3D = is3D;
			Dirty(DIRTY_FRAGMENTSHADER_STATE | (is3D ? DIRTY_MIPBIAS : 0));
		}
	}
	void SetTextureIsArray(bool isArrayTexture) {  // VK only
		if (textureIsArray != isArrayTexture) {
			textureIsArray = isArrayTexture;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}
	void SetTextureIsVideo(bool isVideo) {
		textureIsVideo = isVideo;
	}
	void SetTextureIsBGRA(bool isBGRA) {
		if (bgraTexture != isBGRA) {
			bgraTexture = isBGRA;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}
	void SetTextureIsFramebuffer(bool isFramebuffer) {
		if (textureIsFramebuffer != isFramebuffer) {
			textureIsFramebuffer = isFramebuffer;
			Dirty(DIRTY_UVSCALEOFFSET);
		} else if (isFramebuffer) {
			// Always dirty if it's a framebuffer, since the uniform value depends both
			// on the specified texture size and the bound texture size. Makes things easier.
			// TODO: Look at this again later.
			Dirty(DIRTY_UVSCALEOFFSET);
		}
	}
	void SetUseFlags(u32 newFlags) {
		if (newFlags != useFlags_) {
			if (useFlags_ != 0)
				useFlagsChanged = true;
			useFlags_ = newFlags;
		}
	}

	// When checking for a single flag, use Use()/UseAll().
	u32 GetUseFlags() const {
		return useFlags_;
	}

	void UpdateUVScaleOffset() {
#if defined(_M_SSE)
		__m128i values = _mm_slli_epi32(_mm_load_si128((const __m128i *)&gstate.texscaleu), 8);
		_mm_storeu_si128((__m128i *)&uv, values);
#elif PPSSPP_ARCH(ARM_NEON)
		const uint32x4_t values = vshlq_n_u32(vld1q_u32((const u32 *)&gstate.texscaleu), 8);
		vst1q_u32((u32 *)&uv, values);
#else
		uv.uScale = getFloat24(gstate.texscaleu);
		uv.vScale = getFloat24(gstate.texscalev);
		uv.uOff = getFloat24(gstate.texoffsetu);
		uv.vOff = getFloat24(gstate.texoffsetv);
#endif
	}

private:
	u32 useFlags_;
public:
	u32 vertexAddr;
	u32 indexAddr;
	u32 offsetAddr;

	uint64_t dirty;

	bool usingDepth;  // For deferred depth copies.
	bool clearingDepth;

	bool textureFullAlpha;
	bool vertexFullAlpha;

	int skipDrawReason;

	UVScale uv;

	bool bgraTexture;
	bool needShaderTexClamp;
	bool textureIsArray;
	bool textureIsFramebuffer;
	bool textureIsVideo;
	bool useFlagsChanged;

	float morphWeights[8];
	u32 deferredVertTypeDirty;

	u32 curTextureWidth;
	u32 curTextureHeight;
	u32 actualTextureHeight;
	// Only applied when needShaderTexClamp = true.
	int curTextureXOffset;
	int curTextureYOffset;
	bool curTextureIs3D;

	float vpWidth;
	float vpHeight;

	float vpXOffset;
	float vpYOffset;
	float vpZOffset;
	float vpWidthScale;
	float vpHeightScale;
	float vpDepthScale;

	KnownVertexBounds vertBounds;

	GEBufferFormat framebufFormat;
	// Some games use a very specific masking setup to draw into the alpha channel of a 4444 target using the blue channel of a 565 target.
	// This is done because on PSP you can't write to destination alpha, other than stencil values, which can't be set from a texture.
	// Examples of games that do this: Outrun, Split/Second.
	// We detect this case and go into a special drawing mode.
	bool blueToAlpha;

	// U/V is 1:1 to pixels. Can influence texture sampling.
	bool pixelMapped;

	// TODO: These should be accessed from the current VFB object directly.
	u32 curRTWidth;
	u32 curRTHeight;
	u32 curRTRenderWidth;
	u32 curRTRenderHeight;

	void SetCurRTOffset(int xoff, int yoff) {
		if (xoff != curRTOffsetX || yoff != curRTOffsetY) {
			curRTOffsetX = xoff;
			curRTOffsetY = yoff;
			Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_PROJTHROUGHMATRIX);
		}
	}
	int curRTOffsetX;
	int curRTOffsetY;

	// Set if we are doing hardware bezier/spline.
	SubmitType submitType;
	int spline_num_points_u;

	ShaderDepalMode shaderDepalMode;
	GEBufferFormat depalFramebufferFormat;

	u32 getRelativeAddress(u32 data) const;
	static void Reset();
	void DoState(PointerWrap &p);
};

class GPUInterface;
class GPUDebugInterface;

extern GPUStateCache gstate_c;

inline u32 GPUStateCache::getRelativeAddress(u32 data) const {
	u32 baseExtended = ((gstate.base & 0x000F0000) << 8) | data;
	return (gstate_c.offsetAddr + baseExtended) & 0x0FFFFFFF;
}
