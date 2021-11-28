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

#define SOFTPIXEL_USE_CACHE 1

#pragma pack(push, 1)

struct PixelFuncID {
	PixelFuncID() {
	}

#ifdef SOFTPIXEL_USE_CACHE
	struct {
		// Warning: these are not hashed or compared for equal.  Just cached values.
		uint32_t colorWriteMask{};
		int16_t ditherMatrix[16]{};
	} cached;
#endif

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
			// 60 bits, 4 free.
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
	GEBlendSrcFactor AlphaBlendSrc() const {
		return GEBlendSrcFactor(alphaBlendSrc);
	}
	GEBlendDstFactor AlphaBlendDst() const {
		return GEBlendDstFactor(alphaBlendDst);
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

	union {
		uint32_t fullKey;
		struct {
			uint8_t texfmt : 4;
			uint8_t clutfmt : 2;
			uint8_t : 2;
			bool swizzle : 1;
			bool useSharedClut : 1;
			bool hasClutMask : 1;
			bool hasClutShift : 1;
			bool hasClutOffset : 1;
			bool hasInvalidPtr : 1;
			bool linear : 1;
		};
	};

	GETextureFormat TexFmt() const {
		return GETextureFormat(texfmt);
	}

	GEPaletteFormat ClutFmt() const {
		return GEPaletteFormat(clutfmt);
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
