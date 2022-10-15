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

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>

#include "GPU/ge_constants.h"

// 0-10 match GEBlendSrcFactor/GEBlendDstFactor.
enum class PixelBlendFactor {
	OTHERCOLOR,
	INVOTHERCOLOR,
	SRCALPHA,
	INVSRCALPHA,
	DSTALPHA,
	INVDSTALPHA,
	DOUBLESRCALPHA,
	DOUBLEINVSRCALPHA,
	DOUBLEDSTALPHA,
	DOUBLEINVDSTALPHA,
	FIX,
	// These are invented, but common FIX values.
	ZERO,
	ONE,
};

#pragma pack(push, 1)

struct PixelFuncID {
	PixelFuncID() {
	}

	struct {
		// Warning: these are not hashed or compared for equal.  Just cached values.
		uint32_t colorWriteMask{};
		int8_t ditherMatrix[16]{};
		uint32_t fogColor;
		int minz;
		int maxz;
		uint16_t framebufStride;
		uint16_t depthbufStride;
		GELogicOp logicOp;
		uint8_t stencilRef;
		uint8_t stencilTestMask;
		uint8_t alphaTestMask;
		GEComparison colorTestFunc;
		uint32_t colorTestMask;
		uint32_t colorTestRef;
		uint32_t alphaBlendSrc;
		uint32_t alphaBlendDst;
	} cached;

	union {
		uint64_t fullKey{};
		struct {
			bool clearMode : 1;
			// Reused as ColorClear.
			bool colorTest : 1;
			// Reused as StencilClear.
			bool stencilTest : 1;
			// Reused as DepthClear.
			bool depthWrite : 1;
			bool applyDepthRange : 1;
			// If alpha testing is disabled, set to GE_COMP_ALWAYS.
			uint8_t alphaTestFunc : 3;
			// If depth testing is disabled, set to GE_COMP_ALWAYS.
			uint8_t depthTestFunc : 3;
			uint8_t stencilTestFunc : 3;
			uint8_t fbFormat : 2;
			// 16 bits before alphaTestRef.
			uint8_t alphaTestRef : 8;
			uint8_t stencilTestRef : 8;
			// 32 bits before alphaBlend.
			bool alphaBlend : 1;
			uint8_t alphaBlendEq : 3;
			uint8_t alphaBlendSrc : 4;
			uint8_t alphaBlendDst : 4;
			// Meaning: alphaTestMask != 0xFF
			bool hasAlphaTestMask : 1;
			// Meaning: stencilTestMask != 0xFF
			bool hasStencilTestMask : 1;
			bool dithering : 1;
			bool applyLogicOp : 1;
			// 48 bits before applyFog.
			bool applyFog : 1;
			// Meaning: fb_stride == 512 && z_stride == 512
			bool useStandardStride : 1;
			// Meaning: maskRGB != 0 || maskA != 0
			bool applyColorWriteMask : 1;
			uint8_t sFail : 3;
			uint8_t zFail : 3;
			uint8_t zPass : 3;
			bool earlyZChecks : 1;
			// 61 bits, 3 free.
		};
	};

	bool ColorClear() const {
		return colorTest;
	}
	bool StencilClear() const {
		return stencilTest;
	}
	bool DepthClear() const {
		return depthWrite;
	}

	GEComparison AlphaTestFunc() const {
		return GEComparison(alphaTestFunc);
	}
	GEComparison DepthTestFunc() const {
		return GEComparison(depthTestFunc);
	}
	GEComparison StencilTestFunc() const {
		return GEComparison(stencilTestFunc);
	}

	GEBufferFormat FBFormat() const {
		return GEBufferFormat(fbFormat);
	}

	GEBlendMode AlphaBlendEq() const {
		return GEBlendMode(alphaBlendEq);
	}
	PixelBlendFactor AlphaBlendSrc() const {
		return PixelBlendFactor(alphaBlendSrc);
	}
	PixelBlendFactor AlphaBlendDst() const {
		return PixelBlendFactor(alphaBlendDst);
	}

	GEStencilOp SFail() const {
		return GEStencilOp(sFail);
	}
	GEStencilOp ZFail() const {
		return GEStencilOp(zFail);
	}
	GEStencilOp ZPass() const {
		return GEStencilOp(zPass);
	}

	bool operator == (const PixelFuncID &other) const {
		return fullKey == other.fullKey;
	}
};

#pragma pack(pop)

struct SamplerID {
	SamplerID() : fullKey(0) {
	}

	struct {
		struct {
			uint16_t w;
			uint16_t h;
		} sizes[8];
		uint32_t texBlendColor;
		uint32_t clutFormat;
		union {
			const uint8_t *clut;
			const uint16_t *clut16;
			const uint32_t *clut32;
		};
	} cached;

	uint32_t pad;

	union {
		uint32_t fullKey;
		struct {
			uint8_t texfmt : 4;
			uint8_t clutfmt : 2;
			bool clampS : 1;
			bool clampT : 1;
			bool swizzle : 1;
			bool useSharedClut : 1;
			bool hasClutMask : 1;
			bool hasClutShift : 1;
			bool hasClutOffset : 1;
			bool hasInvalidPtr : 1;
			bool overReadSafe : 1;
			bool useStandardBufw : 1;
			uint8_t width0Shift : 4;
			uint8_t height0Shift : 4;
			uint8_t texFunc : 3;
			bool useTextureAlpha : 1;
			bool useColorDoubling : 1;
			bool hasAnyMips : 1;
			bool linear : 1;
			bool fetch : 1;
		};
	};

	GETextureFormat TexFmt() const {
		return GETextureFormat(texfmt);
	}

	GEPaletteFormat ClutFmt() const {
		return GEPaletteFormat(clutfmt);
	}

	GETexFunc TexFunc() const {
		return GETexFunc(texFunc);
	}

	bool operator == (const SamplerID &other) const {
		return fullKey == other.fullKey;
	}
};

namespace std {

template <>
struct hash<PixelFuncID> {
	std::size_t operator()(const PixelFuncID &k) const {
		return hash<uint64_t>()(k.fullKey);
	}
};

template <>
struct hash<SamplerID> {
	std::size_t operator()(const SamplerID &k) const {
		return hash<uint32_t>()(k.fullKey);
	}
};

};

void ComputePixelFuncID(PixelFuncID *id);
std::string DescribePixelFuncID(const PixelFuncID &id);

void ComputeSamplerID(SamplerID *id);
std::string DescribeSamplerID(const SamplerID &id);
