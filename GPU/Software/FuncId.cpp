// Copyright (c) 2021- PPSSPP Project.

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

#include "Common/Data/Convert/ColorConv.h"
#include "Common/StringUtils.h"
#include "GPU/Software/FuncId.h"
#include "GPU/GPUState.h"

static_assert(sizeof(SamplerID) == sizeof(SamplerID::fullKey), "Bad sampler ID size");
#ifdef SOFTPIXEL_USE_CACHE
static_assert(sizeof(PixelFuncID) == sizeof(PixelFuncID::fullKey) + sizeof(PixelFuncID::cached), "Bad pixel func ID size");
#else
static_assert(sizeof(PixelFuncID) == sizeof(PixelFuncID::fullKey), "Bad pixel func ID size");
#endif

static inline GEComparison OptimizeRefByteCompare(GEComparison func, u8 ref) {
	// Not equal tests are easier.
	if (ref == 0 && func == GE_COMP_GREATER)
		return GE_COMP_NOTEQUAL;
	if (ref == 0xFF && func == GE_COMP_LESS)
		return GE_COMP_NOTEQUAL;

	// Sometimes games pointlessly use tests like these.
	if (ref == 0 && func == GE_COMP_GEQUAL)
		return GE_COMP_ALWAYS;
	if (ref == 0xFF && func == GE_COMP_LEQUAL)
		return GE_COMP_ALWAYS;
	return func;
}

void ComputePixelFuncID(PixelFuncID *id) {
	id->fullKey = 0;

	// TODO: Could this be minz > 0x0000 || maxz < 0xFFFF?  Maybe unsafe, depending on verts...
	id->applyDepthRange = !gstate.isModeThrough();
	// Dither happens even in clear mode.
	id->dithering = gstate.isDitherEnabled();
	id->fbFormat = gstate.FrameBufFormat();
	id->useStandardStride = gstate.FrameBufStride() == 512 && gstate.DepthBufStride() == 512;
	id->applyColorWriteMask = gstate.getColorMask() != 0;

	id->clearMode = gstate.isModeClear();
	if (id->clearMode) {
		id->colorTest = gstate.isClearModeColorMask();
		id->stencilTest = gstate.isClearModeAlphaMask() && gstate.FrameBufFormat() != GE_FORMAT_565;
		id->depthWrite = gstate.isClearModeDepthMask();
		id->depthTestFunc = GE_COMP_ALWAYS;
		id->alphaTestFunc = GE_COMP_ALWAYS;
	} else {
		id->colorTest = gstate.isColorTestEnabled() && gstate.getColorTestFunction() != GE_COMP_ALWAYS;
		if (gstate.isStencilTestEnabled() && gstate.getStencilTestFunction() == GE_COMP_ALWAYS) {
			// If stencil always passes, force off when we won't write any stencil bits.
			bool stencilWrite = (gstate.pmska & 0xFF) != 0xFF && gstate.FrameBufFormat() != GE_FORMAT_565;
			if (gstate.isDepthTestEnabled() && gstate.getDepthTestFunction() != GE_COMP_ALWAYS)
				id->stencilTest = stencilWrite && (gstate.getStencilOpZPass() != GE_STENCILOP_KEEP || gstate.getStencilOpZFail() != GE_STENCILOP_KEEP);
			else
				id->stencilTest = stencilWrite && gstate.getStencilOpZPass() != GE_STENCILOP_KEEP;
		} else {
			id->stencilTest = gstate.isStencilTestEnabled();
		}
		id->depthWrite = gstate.isDepthTestEnabled() && gstate.isDepthWriteEnabled();

		if (id->stencilTest) {
			id->stencilTestRef = gstate.getStencilTestRef() & gstate.getStencilTestMask();
			id->stencilTestFunc = OptimizeRefByteCompare(gstate.getStencilTestFunction(), id->stencilTestRef);
			id->hasStencilTestMask = gstate.getStencilTestMask() != 0xFF && gstate.FrameBufFormat() != GE_FORMAT_565;

			// Stencil can't be written on 565, and any invalid op acts like KEEP, which is 0.
			if (gstate.FrameBufFormat() != GE_FORMAT_565 && gstate.getStencilOpSFail() <= GE_STENCILOP_DECR)
				id->sFail = gstate.getStencilOpSFail();
			if (gstate.FrameBufFormat() != GE_FORMAT_565 && gstate.getStencilOpZFail() <= GE_STENCILOP_DECR)
				id->zFail = gstate.isDepthTestEnabled() ? gstate.getStencilOpZFail() : GE_STENCILOP_KEEP;
			if (gstate.FrameBufFormat() != GE_FORMAT_565 && gstate.getStencilOpZPass() <= GE_STENCILOP_DECR)
				id->zPass = gstate.getStencilOpZPass();

			// Always treat zPass/zFail the same if there's no depth test.
			if (!gstate.isDepthTestEnabled() || gstate.getDepthTestFunction() == GE_COMP_ALWAYS)
				id->zFail = id->zPass;
			// And same for sFail if there's no stencil test.
			if (id->StencilTestFunc() == GE_COMP_ALWAYS)
				id->sFail = id->zPass;

			// Normalize REPLACE 00 to ZERO, especially if using a mask.
			if (gstate.getStencilTestRef() == 0) {
				if (id->SFail() == GE_STENCILOP_REPLACE)
					id->sFail = GE_STENCILOP_ZERO;
				if (id->ZFail() == GE_STENCILOP_REPLACE)
					id->zFail = GE_STENCILOP_ZERO;
				if (id->ZPass() == GE_STENCILOP_REPLACE)
					id->zPass = GE_STENCILOP_ZERO;
			}

			// For 5551, DECR is also the same as ZERO.
			if (id->FBFormat() == GE_FORMAT_5551) {
				if (id->SFail() == GE_STENCILOP_DECR)
					id->sFail = GE_STENCILOP_ZERO;
				if (id->ZFail() == GE_STENCILOP_DECR)
					id->zFail = GE_STENCILOP_ZERO;
				if (id->ZPass() == GE_STENCILOP_DECR)
					id->zPass = GE_STENCILOP_ZERO;
			}

			// Turn off stencil testing if it's doing nothing.
			if (id->SFail() == GE_STENCILOP_KEEP && id->ZFail() == GE_STENCILOP_KEEP && id->ZPass() == GE_STENCILOP_KEEP) {
				if (id->StencilTestFunc() == GE_COMP_ALWAYS)
					id->stencilTest = false;
			}
		}

		id->depthTestFunc = gstate.isDepthTestEnabled() ? gstate.getDepthTestFunction() : GE_COMP_ALWAYS;
		id->alphaTestFunc = gstate.isAlphaTestEnabled() ? gstate.getAlphaTestFunction() : GE_COMP_ALWAYS;
		if (id->AlphaTestFunc() != GE_COMP_ALWAYS) {
			id->alphaTestRef = gstate.getAlphaTestRef() & gstate.getAlphaTestMask();
			id->hasAlphaTestMask = gstate.getAlphaTestMask() != 0xFF;
			// Try to pick a more optimal variant.
			id->alphaTestFunc = OptimizeRefByteCompare(id->AlphaTestFunc(), id->alphaTestRef);
			if (id->alphaTestFunc == GE_COMP_ALWAYS) {
				id->alphaTestRef = 0;
				id->hasAlphaTestMask = false;
			}
		}

		// If invalid (6 or 7), doesn't do any blending, so force off.
		id->alphaBlend = gstate.isAlphaBlendEnabled() && gstate.getBlendEq() <= 5;
		// Force it off if the factors are constant and don't blend.  Some games use this...
		if (id->alphaBlend && gstate.getBlendEq() == GE_BLENDMODE_MUL_AND_ADD) {
			bool srcFixedOne = gstate.getBlendFuncA() == GE_SRCBLEND_FIXA && gstate.getFixA() == 0x00FFFFFF;
			bool dstFixedZero = gstate.getBlendFuncB() == GE_DSTBLEND_FIXB && gstate.getFixB() == 0x00000000;
			if (srcFixedOne && dstFixedZero)
				id->alphaBlend = false;
		}
		if (id->alphaBlend) {
			id->alphaBlendEq = gstate.getBlendEq();
			id->alphaBlendSrc = gstate.getBlendFuncA();
			id->alphaBlendDst = gstate.getBlendFuncB();
		}

		id->applyLogicOp = gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY;
		id->applyFog = gstate.isFogEnabled() && !gstate.isModeThrough();
	}

#ifdef SOFTPIXEL_USE_CACHE
	// Cache some values for later convenience.
	if (id->dithering) {
		for (int y = 0; y < 4; ++y) {
			for (int x = 0; x < 4; ++x)
				id->cached.ditherMatrix[y * 4 + x] = gstate.getDitherValue(x, y);
		}
	}
	if (id->applyColorWriteMask) {
		uint32_t mask = gstate.getColorMask();
		// This flag means stencil clear or stencil test, basically whether writing to stencil.
		if (!id->stencilTest)
			mask |= 0xFF000000;

		switch (id->fbFormat) {
		case GE_FORMAT_565:
			id->cached.colorWriteMask = RGBA8888ToRGB565(mask);
			break;

		case GE_FORMAT_5551:
			id->cached.colorWriteMask = RGBA8888ToRGBA5551(mask);
			break;

		case GE_FORMAT_4444:
			id->cached.colorWriteMask = RGBA8888ToRGBA4444(mask);
			break;

		case GE_FORMAT_8888:
			id->cached.colorWriteMask = mask;
			break;
		}
	}
#endif
}

std::string DescribePixelFuncID(const PixelFuncID &id) {
	std::string desc;
	if (id.clearMode) {
		desc = "Clear";
		if (id.ColorClear())
			desc += "C";
		if (id.StencilClear())
			desc += "S";
		if (id.DepthClear())
			desc += "D";
		desc += ":";
	}
	if (id.applyDepthRange)
		desc += "DepthR:";
	if (id.useStandardStride)
		desc += "Str512:";
	if (id.dithering)
		desc += "Dith:";
	if (id.applyColorWriteMask)
		desc += "Msk:";

	switch (id.fbFormat) {
	case GE_FORMAT_565: desc += "5650:"; break;
	case GE_FORMAT_5551: desc += "5551:"; break;
	case GE_FORMAT_4444: desc += "4444:"; break;
	case GE_FORMAT_8888: desc += "8888:"; break;
	}

	if (id.AlphaTestFunc() != GE_COMP_ALWAYS) {
		switch (id.AlphaTestFunc()) {
		case GE_COMP_NEVER: desc += "ANever"; break;
		case GE_COMP_ALWAYS: break;
		case GE_COMP_EQUAL: desc += "AEQ"; break;
		case GE_COMP_NOTEQUAL: desc += "ANE"; break;
		case GE_COMP_LESS: desc += "ALT"; break;
		case GE_COMP_LEQUAL: desc += "ALE"; break;
		case GE_COMP_GREATER: desc += "AGT"; break;
		case GE_COMP_GEQUAL: desc += "AGE"; break;
		}
		if (id.hasAlphaTestMask)
			desc += "Msk";
		desc += StringFromFormat("%02X:", id.alphaTestRef);
	}

	if (id.DepthTestFunc() != GE_COMP_ALWAYS) {
		switch (id.DepthTestFunc()) {
		case GE_COMP_NEVER: desc += "ZNever:"; break;
		case GE_COMP_ALWAYS: break;
		case GE_COMP_EQUAL: desc += "ZEQ:"; break;
		case GE_COMP_NOTEQUAL: desc += "ZNE:"; break;
		case GE_COMP_LESS: desc += "ZLT:"; break;
		case GE_COMP_LEQUAL: desc += "ZLE:"; break;
		case GE_COMP_GREATER: desc += "ZGT:"; break;
		case GE_COMP_GEQUAL: desc += "ZGE:"; break;
		}
	}
	if (id.depthWrite && !id.clearMode)
		desc += "ZWr:";

	if (id.colorTest && !id.clearMode)
		desc += "CTest:";

	if (id.stencilTest && !id.clearMode) {
		switch (id.StencilTestFunc()) {
		case GE_COMP_NEVER: desc += "SNever"; break;
		case GE_COMP_ALWAYS: desc += "SAlways";  break;
		case GE_COMP_EQUAL: desc += "SEQ"; break;
		case GE_COMP_NOTEQUAL: desc += "SNE"; break;
		case GE_COMP_LESS: desc += "SLT"; break;
		case GE_COMP_LEQUAL: desc += "SLE"; break;
		case GE_COMP_GREATER: desc += "SGT"; break;
		case GE_COMP_GEQUAL: desc += "SGE"; break;
		}
		if (id.hasStencilTestMask)
			desc += "Msk";
		desc += StringFromFormat("%02X:", id.stencilTestRef);
	}

	switch (id.SFail()) {
	case GE_STENCILOP_KEEP: break;
	case GE_STENCILOP_ZERO: desc += "STstF0:"; break;
	case GE_STENCILOP_REPLACE: desc += "STstFRpl:"; break;
	case GE_STENCILOP_INVERT: desc += "STstFXor:"; break;
	case GE_STENCILOP_INCR: desc += "STstFInc:"; break;
	case GE_STENCILOP_DECR: desc += "STstFDec:"; break;
	}
	switch (id.ZFail()) {
	case GE_STENCILOP_KEEP: break;
	case GE_STENCILOP_ZERO: desc += "ZTstF0:"; break;
	case GE_STENCILOP_REPLACE: desc += "ZTstFRpl:"; break;
	case GE_STENCILOP_INVERT: desc += "ZTstFXor:"; break;
	case GE_STENCILOP_INCR: desc += "ZTstFInc:"; break;
	case GE_STENCILOP_DECR: desc += "ZTstFDec:"; break;
	}
	switch (id.ZPass()) {
	case GE_STENCILOP_KEEP: break;
	case GE_STENCILOP_ZERO: desc += "ZTstT0:"; break;
	case GE_STENCILOP_REPLACE: desc += "ZTstTRpl:"; break;
	case GE_STENCILOP_INVERT: desc += "ZTstTXor:"; break;
	case GE_STENCILOP_INCR: desc += "ZTstTInc:"; break;
	case GE_STENCILOP_DECR: desc += "ZTstTDec:"; break;
	}

	if (id.alphaBlend) {
		switch (id.AlphaBlendEq()) {
		case GE_BLENDMODE_MUL_AND_ADD: desc += "BlendAdd<"; break;
		case GE_BLENDMODE_MUL_AND_SUBTRACT: desc += "BlendSub<"; break;
		case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE: desc += "BlendRSub<"; break;
		case GE_BLENDMODE_MIN: desc += "BlendMin<"; break;
		case GE_BLENDMODE_MAX: desc += "BlendMax<"; break;
		case GE_BLENDMODE_ABSDIFF: desc += "BlendDiff<"; break;
		}
		switch (id.AlphaBlendSrc()) {
		case GE_SRCBLEND_DSTCOLOR: desc += "DstRGB,"; break;
		case GE_SRCBLEND_INVDSTCOLOR: desc += "1-DstRGB,"; break;
		case GE_SRCBLEND_SRCALPHA: desc += "SrcA,"; break;
		case GE_SRCBLEND_INVSRCALPHA: desc += "1-SrcA,"; break;
		case GE_SRCBLEND_DSTALPHA: desc += "DstA,"; break;
		case GE_SRCBLEND_INVDSTALPHA: desc += "1-DstA,"; break;
		case GE_SRCBLEND_DOUBLESRCALPHA: desc += "2*SrcA,"; break;
		case GE_SRCBLEND_DOUBLEINVSRCALPHA: desc += "1-2*SrcA,"; break;
		case GE_SRCBLEND_DOUBLEDSTALPHA: desc += "2*DstA,"; break;
		case GE_SRCBLEND_DOUBLEINVDSTALPHA: desc += "1-2*DstA,"; break;
		case GE_SRCBLEND_FIXA: desc += "Fix,"; break;
		}
		switch (id.AlphaBlendDst()) {
		case GE_DSTBLEND_SRCCOLOR: desc += "SrcRGB>:"; break;
		case GE_DSTBLEND_INVSRCCOLOR: desc += "1-SrcRGB>:"; break;
		case GE_DSTBLEND_SRCALPHA: desc += "SrcA>:"; break;
		case GE_DSTBLEND_INVSRCALPHA: desc += "1-SrcA>:"; break;
		case GE_DSTBLEND_DSTALPHA: desc += "DstA>:"; break;
		case GE_DSTBLEND_INVDSTALPHA: desc += "1-DstA>:"; break;
		case GE_DSTBLEND_DOUBLESRCALPHA: desc += "2*SrcA>:"; break;
		case GE_DSTBLEND_DOUBLEINVSRCALPHA: desc += "1-2*SrcA>:"; break;
		case GE_DSTBLEND_DOUBLEDSTALPHA: desc += "2*DstA>:"; break;
		case GE_DSTBLEND_DOUBLEINVDSTALPHA: desc += "1-2*DstA>:"; break;
		case GE_DSTBLEND_FIXB: desc += "Fix>:"; break;
		}
	}

	if (id.applyLogicOp)
		desc += "Logic:";
	if (id.applyFog)
		desc += "Fog:";

	if (desc.empty())
		return desc;
	desc.resize(desc.size() - 1);
	return desc;
}

void ComputeSamplerID(SamplerID *id_out) {
	SamplerID id{};

	id.useStandardBufw = true;
	id.hasStandardMips = true;
	int maxLevel = gstate.isMipmapEnabled() ? gstate.getTextureMaxLevel() : 0;
	int lastWidth = -1;
	for (int i = 0; i <= maxLevel; ++i) {
		if (gstate.getTextureAddress(i) == 0)
			id.hasInvalidPtr = true;
		int w = gstate.getTextureWidth(i);
		if (w != (gstate.texbufwidth[i] & 0x00001FFF))
			id.useStandardBufw = false;
		if (lastWidth != -1 && lastWidth != w * 2)
			id.hasStandardMips = false;
		lastWidth = w;
	}
	id.hasAnyMips = maxLevel != 0;

	id.texfmt = gstate.getTextureFormat();
	id.swizzle = gstate.isTextureSwizzled();
	// Only CLUT4 can use separate CLUTs per mimap.
	id.useSharedClut = gstate.getTextureFormat() != GE_TFMT_CLUT4 || maxLevel == 0 || !gstate.isMipmapEnabled() || gstate.isClutSharedForMipmaps();
	if (gstate.isTextureFormatIndexed()) {
		id.clutfmt = gstate.getClutPaletteFormat();
		id.hasClutMask = gstate.getClutIndexMask() != 0xFF;
		id.hasClutShift = gstate.getClutIndexShift() != 0;
		id.hasClutOffset = gstate.getClutIndexStartPos() != 0;
	}

	id.clampS = gstate.isTexCoordClampedS();
	id.clampT = gstate.isTexCoordClampedT();
	id.width0Shift = gstate.texsize[0] & 0xF;
	id.height0Shift = (gstate.texsize[0] >> 8) & 0xF;

	id.useTextureAlpha = gstate.isTextureAlphaUsed();
	id.useColorDoubling = gstate.isColorDoublingEnabled();
	id.texFunc = gstate.getTextureFunction();
	if (id.texFunc > GE_TEXFUNC_ADD)
		id.texFunc = GE_TEXFUNC_ADD;

	*id_out = id;
}

std::string DescribeSamplerID(const SamplerID &id) {
	std::string name;
	switch (id.TexFmt()) {
	case GE_TFMT_5650: name = "5650"; break;
	case GE_TFMT_5551: name = "5551"; break;
	case GE_TFMT_4444: name = "4444"; break;
	case GE_TFMT_8888: name = "8888"; break;
	case GE_TFMT_CLUT4: name = "CLUT4"; break;
	case GE_TFMT_CLUT8: name = "CLUT8"; break;
	case GE_TFMT_CLUT16: name = "CLUT16"; break;
	case GE_TFMT_CLUT32: name = "CLUT32"; break;
	case GE_TFMT_DXT1: name = "DXT1"; break;
	case GE_TFMT_DXT3: name = "DXT3"; break;
	case GE_TFMT_DXT5: name = "DXT5"; break;
	}
	switch (id.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		switch (id.TexFmt()) {
		case GE_TFMT_CLUT4:
		case GE_TFMT_CLUT8:
		case GE_TFMT_CLUT16:
		case GE_TFMT_CLUT32:
			name += ":C5650";
			break;
		default:
			// Ignore 0 clutfmt when no clut.
			break;
		}
		break;
	case GE_CMODE_16BIT_ABGR5551: name += ":C5551"; break;
	case GE_CMODE_16BIT_ABGR4444: name += ":C4444"; break;
	case GE_CMODE_32BIT_ABGR8888: name += ":C8888"; break;
	}
	if (id.swizzle) {
		name += ":SWZ";
	}
	if (!id.useSharedClut) {
		name += ":CMIP";
	}
	if (id.hasInvalidPtr) {
		name += ":INV";
	}
	if (id.hasClutMask) {
		name += ":CMASK";
	}
	if (id.hasClutShift) {
		name += ":CSHF";
	}
	if (id.hasClutOffset) {
		name += ":COFF";
	}
	if (id.clampS || id.clampT) {
		name += std::string(":CL") + (id.clampS ? "S" : "") + (id.clampT ? "T" : "");
	}
	if (!id.useStandardBufw) {
		name += ":BUFW";
	}
	if (!id.hasStandardMips) {
		name += ":XMIP";
	} else if (id.hasAnyMips) {
		name += ":MIP";
	}
	if (id.linear) {
		name += ":LERP";
	}
	if (id.fetch) {
		name += ":FETCH";
	}
	if (id.useTextureAlpha) {
		name += ":A";
	}
	if (id.useColorDoubling) {
		name += ":DBL";
	}
	switch (id.texFunc) {
	case GE_TEXFUNC_MODULATE:
		name += ":MOD";
		break;
	case GE_TEXFUNC_DECAL:
		name += ":DECAL";
		break;
	case GE_TEXFUNC_BLEND:
		name += ":BLEND";
		break;
	case GE_TEXFUNC_REPLACE:
		break;
	case GE_TEXFUNC_ADD:
		name += ":ADD";
	default:
		break;
	}
	name += StringFromFormat(":W%dH%d", 1 << id.width0Shift, 1 << id.height0Shift);

	return name;
}
