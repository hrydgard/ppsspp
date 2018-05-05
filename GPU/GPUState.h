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

#include <cmath>

#include "Common/Common.h"
#include "Common/Swap.h"
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"

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
				clipEnable,
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
				dith1,
				dith2,
				dith3,
				dith4,
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
	u32 getFrameBufRawAddress() const { return (fbptr & 0xFFFFFF); }
	// 0x44000000 is uncached VRAM.
	u32 getFrameBufAddress() const { return 0x44000000 | getFrameBufRawAddress(); }
	GEBufferFormat FrameBufFormat() const { return static_cast<GEBufferFormat>(framebufpixformat & 3); }
	int FrameBufStride() const { return fbwidth&0x7FC; }
	u32 getDepthBufRawAddress() const { return (zbptr & 0xFFFFFF); }
	u32 getDepthBufAddress() const { return 0x44000000 | getDepthBufRawAddress(); }
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

	// Color Mask
	u32 getColorMask() const { return (pmskc & 0xFFFFFF) | ((pmska & 0xFF) << 24); }
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
	int getTextureEnvColR() const { return texenvcolor&0xFF; }
	int getTextureEnvColG() const { return (texenvcolor>>8)&0xFF; }
	int getTextureEnvColB() const { return (texenvcolor>>16)&0xFF; }
	u32 getClutAddress() const { return (clutaddr & 0x00FFFFF0) | ((clutaddrupper << 8) & 0x0F000000); }
	int getClutLoadBytes() const { return (loadclut & 0x3F) * 32; }
	int getClutLoadBlocks() const { return (loadclut & 0x3F); }
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
	bool isUsingPoweredDiffuseLight(int chan) const { return getLightComputation(chan) == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE; }
	bool isUsingSpecularLight(int chan) const { return getLightComputation(chan) != GE_LIGHTCOMP_ONLYDIFFUSE; }
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
	unsigned int getMaterialUpdate() const { return materialupdate&0xFFFFFF; }
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
	int getRegionX1() const { return region1 & 0x3FF; }
	int getRegionY1() const { return (region1 >> 10) & 0x3FF; }
	int getRegionX2() const { return (region2 & 0x3FF); }
	int getRegionY2() const { return (region2 >> 10) & 0x3FF; }

	// Note that the X1/Y1/Z1 here does not mean the upper-left corner, but half the dimensions. X2/Y2/Z2 are the center.
	bool isClippingEnabled() const { return clipEnable & 1; }
	float getViewportXScale() const { return getFloat24(viewportxscale); }
	float getViewportYScale() const { return getFloat24(viewportyscale); }
	float getViewportZScale() const { return getFloat24(viewportzscale); }
	float getViewportXCenter() const { return getFloat24(viewportxcenter); }
	float getViewportYCenter() const { return getFloat24(viewportycenter); }
	float getViewportZCenter() const { return getFloat24(viewportzcenter); }

	// Fixed 16 point.
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

	void Reset();
	void Save(u32_le *ptr);
	void Restore(u32_le *ptr);
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

// Some of these are OpenGL-specific even though this file is neutral, unfortunately.
// Might want to move this mechanism into the backend later.
enum {
	GPU_SUPPORTS_DUALSOURCE_BLEND = FLAG_BIT(0),
	GPU_SUPPORTS_GLSL_ES_300 = FLAG_BIT(1),
	GPU_SUPPORTS_GLSL_330 = FLAG_BIT(2),
	GPU_SUPPORTS_UNPACK_SUBIMAGE = FLAG_BIT(3),
	GPU_SUPPORTS_BLEND_MINMAX = FLAG_BIT(4),
	GPU_SUPPORTS_LOGIC_OP = FLAG_BIT(5),
	GPU_USE_DEPTH_RANGE_HACK = FLAG_BIT(6),
	GPU_SUPPORTS_WIDE_LINES = FLAG_BIT(7),
	GPU_SUPPORTS_ANISOTROPY = FLAG_BIT(8),
	GPU_USE_CLEAR_RAM_HACK = FLAG_BIT(9),
	GPU_SUPPORTS_INSTANCE_RENDERING = FLAG_BIT(10),
	GPU_SUPPORTS_VERTEX_TEXTURE_FETCH = FLAG_BIT(11),
	GPU_SUPPORTS_TEXTURE_FLOAT = FLAG_BIT(12),
	GPU_SUPPORTS_16BIT_FORMATS = FLAG_BIT(13),
	GPU_SUPPORTS_DEPTH_CLAMP = FLAG_BIT(14),
	GPU_SUPPORTS_LARGE_VIEWPORTS = FLAG_BIT(16),
	GPU_SUPPORTS_ACCURATE_DEPTH = FLAG_BIT(17),
	GPU_SUPPORTS_VAO = FLAG_BIT(18),
	GPU_SUPPORTS_ANY_COPY_IMAGE = FLAG_BIT(19),
	GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH = FLAG_BIT(20),
	GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT = FLAG_BIT(21),
	GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT = FLAG_BIT(22),
	GPU_ROUND_DEPTH_TO_16BIT = FLAG_BIT(23),  // Can be disabled either per game or if we use a real 16-bit depth buffer
	GPU_SUPPORTS_TEXTURE_LOD_CONTROL = FLAG_BIT(24),
	GPU_SUPPORTS_FBO = FLAG_BIT(25),
	GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT = FLAG_BIT(26),
	GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT = FLAG_BIT(27),
	GPU_SUPPORTS_OES_TEXTURE_NPOT = FLAG_BIT(28),
	GPU_PREFER_CPU_DOWNLOAD = FLAG_BIT(30),
	GPU_PREFER_REVERSE_COLOR_ORDER = FLAG_BIT(31),
};

struct KnownVertexBounds {
	u16 minU;
	u16 minV;
	u16 maxU;
	u16 maxV;
};

struct GPUStateCache {
	bool Supports(u32 flags) { return (featureFlags & flags) != 0; } // Return true if ANY of flags are true.
	bool SupportsAll(u32 flags) { return (featureFlags & flags) == flags; } // Return true if ALL flags are true.
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
	void SetUseShaderDepal(bool depal) {
		if (depal != useShaderDepal) {
			useShaderDepal = depal;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}
	void SetTextureFullAlpha(bool fullAlpha) {
		if (fullAlpha != textureFullAlpha) {
			textureFullAlpha = fullAlpha;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
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
	void SetAllowShaderBlend(bool allow) {
		if (allowShaderBlend != allow) {
			allowShaderBlend = allow;
			Dirty(DIRTY_FRAGMENTSHADER_STATE);
		}
	}

	u32 featureFlags;

	u32 vertexAddr;
	u32 indexAddr;
	u32 offsetAddr;

	uint64_t dirty;

	bool textureFullAlpha;
	bool vertexFullAlpha;

	int skipDrawReason;

	UVScale uv;

	bool bgraTexture;
	bool needShaderTexClamp;
	bool allowShaderBlend;

	float morphWeights[8];
	u32 deferredVertTypeDirty;

	u32 curTextureWidth;
	u32 curTextureHeight;
	u32 actualTextureHeight;
	// Only applied when needShaderTexClamp = true.
	u32 curTextureXOffset;
	u32 curTextureYOffset;

	float vpWidth;
	float vpHeight;

	float vpXOffset;
	float vpYOffset;
	float vpZOffset;
	float vpWidthScale;
	float vpHeightScale;
	float vpDepthScale;

	KnownVertexBounds vertBounds;

	// TODO: These should be accessed from the current VFB object directly.
	u32 curRTWidth;
	u32 curRTHeight;
	u32 curRTRenderWidth;
	u32 curRTRenderHeight;

	void SetCurRTOffsetX(int off) {
		if (off != curRTOffsetX) {
			curRTOffsetX = off;
			Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		}
	}
	u32 curRTOffsetX;

	bool bezier;
	bool spline;
	int spline_count_u;
	int spline_count_v;
	int spline_type_u;
	int spline_type_v;

	bool useShaderDepal;
	GEBufferFormat depalFramebufferFormat;

	u32 getRelativeAddress(u32 data) const;
	void Reset();
	void DoState(PointerWrap &p);
};

class GPUInterface;
class GPUDebugInterface;

extern GPUgstate gstate;
extern GPUStateCache gstate_c;

inline u32 GPUStateCache::getRelativeAddress(u32 data) const {
	u32 baseExtended = ((gstate.base & 0x000F0000) << 8) | data;
	return (gstate_c.offsetAddr + baseExtended) & 0x0FFFFFFF;
}
