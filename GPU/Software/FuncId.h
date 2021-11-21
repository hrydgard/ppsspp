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

struct PixelFuncID {
	PixelFuncID() {
	}

	union {
		uint64_t fullKey{};
		struct {
			bool clearMode : 1;
			union {
				bool colorTest : 1;
				bool colorClear : 1;
			};
			union {
				bool stencilTest : 1;
				bool stencilClear : 1;
			};
			union {
				bool depthWrite : 1;
				bool depthClear : 1;
			};
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

	bool operator == (const PixelFuncID &other) const {
		return fullKey == other.fullKey;
	}
};

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
