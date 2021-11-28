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

#include "ppsspp_config.h"

#include <string>
#include <vector>
#include <unordered_map>
#if PPSSPP_ARCH(ARM)
#include "Common/ArmEmitter.h"
#elif PPSSPP_ARCH(ARM64)
#include "Common/Arm64Emitter.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Common/x64Emitter.h"
#elif PPSSPP_ARCH(MIPS)
#include "Common/MipsEmitter.h"
#else
#include "Common/FakeEmitter.h"
#endif
#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerRegCache.h"

namespace Rasterizer {

#if PPSSPP_ARCH(AMD64) && PPSSPP_PLATFORM(WINDOWS) && (defined(_MSC_VER) || defined(__clang__))
#define SOFTPIXEL_CALL __vectorcall
#define SOFTPIXEL_VEC4I __m128i
#define SOFTPIXEL_TO_VEC4I(x) (x).ivec
#elif PPSSPP_ARCH(AMD64)
#define SOFTPIXEL_CALL
#define SOFTPIXEL_VEC4I __m128i
#define SOFTPIXEL_TO_VEC4I(x) (x).ivec
#else
#define SOFTPIXEL_CALL
#define SOFTPIXEL_VEC4I const Math3D::Vec4<int> &
#define SOFTPIXEL_TO_VEC4I(x) (x)
#endif

typedef void (SOFTPIXEL_CALL *SingleFunc)(int x, int y, int z, int fog, SOFTPIXEL_VEC4I color_in, const PixelFuncID &pixelID);
SingleFunc GetSingleFunc(const PixelFuncID &id);

void Init();
void Shutdown();

bool DescribeCodePtr(const u8 *ptr, std::string &name);

struct PixelBlendState {
	bool usesFactors = false;
	bool usesDstAlpha = false;
	bool dstFactorIsInverse = false;
};
void ComputePixelBlendState(PixelBlendState &state, const PixelFuncID &id);

#if PPSSPP_ARCH(ARM)
class PixelJitCache : public ArmGen::ARMXCodeBlock {
#elif PPSSPP_ARCH(ARM64)
class PixelJitCache : public Arm64Gen::ARM64CodeBlock {
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
class PixelJitCache : public Gen::XCodeBlock {
#elif PPSSPP_ARCH(MIPS)
class PixelJitCache : public MIPSGen::MIPSCodeBlock {
#else
class PixelJitCache : public FakeGen::FakeXCodeBlock {
#endif
public:
	PixelJitCache();

	// Returns a pointer to the code to run.
	SingleFunc GetSingle(const PixelFuncID &id);
	SingleFunc GenericSingle(const PixelFuncID &id);
	void Clear();

	std::string DescribeCodePtr(const u8 *ptr);

private:
	SingleFunc CompileSingle(const PixelFuncID &id);

#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif

	RegCache::Reg GetGState();
	RegCache::Reg GetConstBase();
	RegCache::Reg GetZeroVec();
	// Note: these may require a temporary reg.
	RegCache::Reg GetColorOff(const PixelFuncID &id);
	RegCache::Reg GetDepthOff(const PixelFuncID &id);
	RegCache::Reg GetDestStencil(const PixelFuncID &id);

	bool Jit_ApplyDepthRange(const PixelFuncID &id);
	bool Jit_AlphaTest(const PixelFuncID &id);
	bool Jit_ApplyFog(const PixelFuncID &id);
	bool Jit_ColorTest(const PixelFuncID &id);
	bool Jit_StencilAndDepthTest(const PixelFuncID &id);
	bool Jit_StencilTest(const PixelFuncID &id, RegCache::Reg stencilReg, RegCache::Reg maskedReg);
	bool Jit_DepthTestForStencil(const PixelFuncID &id, RegCache::Reg stencilReg);
	bool Jit_ApplyStencilOp(const PixelFuncID &id, GEStencilOp op, RegCache::Reg stencilReg);
	bool Jit_WriteStencilOnly(const PixelFuncID &id, RegCache::Reg stencilReg);
	bool Jit_DepthTest(const PixelFuncID &id);
	bool Jit_WriteDepth(const PixelFuncID &id);
	bool Jit_AlphaBlend(const PixelFuncID &id);
	bool Jit_BlendFactor(const PixelFuncID &id, RegCache::Reg factorReg, RegCache::Reg dstReg, GEBlendSrcFactor factor);
	bool Jit_DstBlendFactor(const PixelFuncID &id, RegCache::Reg srcFactorReg, RegCache::Reg dstFactorReg, RegCache::Reg dstReg);
	bool Jit_Dither(const PixelFuncID &id);
	bool Jit_WriteColor(const PixelFuncID &id);
	bool Jit_ApplyLogicOp(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg maskReg);
	bool Jit_ConvertTo565(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg);
	bool Jit_ConvertTo5551(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertTo4444(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertFrom565(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg);
	bool Jit_ConvertFrom5551(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertFrom4444(const PixelFuncID &id, RegCache::Reg colorReg, RegCache::Reg temp1Reg, RegCache::Reg temp2Reg, bool keepAlpha);

	std::unordered_map<PixelFuncID, SingleFunc> cache_;
	std::unordered_map<PixelFuncID, const u8 *> addresses_;
	RegCache regCache_;

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	void Discard();
	void Discard(Gen::CCFlags cc);

	// Used for any test failure.
	std::vector<Gen::FixupBranch> discards_;
	// Used in Jit_ApplyLogicOp() to skip the standard MOV/OR write.
	std::vector<Gen::FixupBranch> skipStandardWrites_;
	int stackIDOffset_ = 0;
	bool colorIs16Bit_ = false;
#endif
};

};
