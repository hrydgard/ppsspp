// Copyright (c) 2017- PPSSPP Project.

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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(AMD64)

#include <emmintrin.h>
#include "Common/x64Emitter.h"
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/ge_constants.h"

using namespace Gen;

namespace Rasterizer {

#if PPSSPP_PLATFORM(WINDOWS)
static const X64Reg argXReg = RCX;
static const X64Reg argYReg = RDX;
static const X64Reg argZReg = R8;
static const X64Reg argFogReg = R9;
static const X64Reg argColorReg = XMM4;

// Must save: RBX, RSP, RBP, RDI, RSI, R12-R15, XMM6-15
#else
static const X64Reg argXReg = RDI;
static const X64Reg argYReg = RSI;
static const X64Reg argZReg = RDX;
static const X64Reg argFogReg = RCX;
static const X64Reg argColorReg = XMM0;

// Must save: RBX, RSP, RBP, R12-R15
#endif

SingleFunc PixelJitCache::CompileSingle(const PixelFuncID &id) {
	// Setup the reg cache.
	regCache_.Reset();
	regCache_.Release(RAX, PixelRegCache::T_GEN);
	regCache_.Release(R10, PixelRegCache::T_GEN);
	regCache_.Release(R11, PixelRegCache::T_GEN);
	regCache_.Release(XMM1, PixelRegCache::T_VEC);
	regCache_.Release(XMM2, PixelRegCache::T_VEC);
	regCache_.Release(XMM3, PixelRegCache::T_VEC);
	regCache_.Release(XMM5, PixelRegCache::T_VEC);

#if !PPSSPP_PLATFORM(WINDOWS)
	regCache_.Release(R8, PixelRegCache::T_GEN);
	regCache_.Release(R9, PixelRegCache::T_GEN);
	regCache_.Release(XMM4, PixelRegCache::T_VEC);
#else
	regCache_.Release(XMM0, PixelRegCache::T_VEC);
#endif

	return nullptr;
}

};

#endif
