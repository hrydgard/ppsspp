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

struct PixelRegCache {
	enum Purpose {
		FLAG_GEN = 0x0100,
		FLAG_TEMP = 0x1000,

		VEC_ZERO = 0x0000,

		GEN_SRC_ALPHA = 0x0100,
		GEN_GSTATE = 0x0101,
		GEN_CONST_BASE = 0x0102,
		GEN_STENCIL = 0x0103,
		GEN_COLOR_OFF = 0x0104,
		GEN_DEPTH_OFF = 0x0105,

		GEN_ARG_X = 0x0180,
		GEN_ARG_Y = 0x0181,
		GEN_ARG_Z = 0x0182,
		GEN_ARG_FOG = 0x0183,
		VEC_ARG_COLOR = 0x0080,
		VEC_ARG_MASK = 0x0081,

		VEC_TEMP0 = 0x1000,
		VEC_TEMP1 = 0x1001,
		VEC_TEMP2 = 0x1002,
		VEC_TEMP3 = 0x1003,
		VEC_TEMP4 = 0x1004,
		VEC_TEMP5 = 0x1005,

		GEN_TEMP0 = 0x1100,
		GEN_TEMP1 = 0x1101,
		GEN_TEMP2 = 0x1102,
		GEN_TEMP3 = 0x1103,
		GEN_TEMP4 = 0x1104,
		GEN_TEMP5 = 0x1105,
		GEN_TEMP_HELPER = 0x1106,

		VEC_INVALID = 0xFEFF,
		GEN_INVALID = 0xFFFF,
	};

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	typedef Gen::X64Reg Reg;
	static constexpr Reg REG_INVALID_VALUE = Gen::INVALID_REG;
#else
	typedef int Reg;
	static constexpr Reg REG_INVALID_VALUE = -1;
#endif

	struct RegStatus {
		Reg reg;
		Purpose purpose;
		uint8_t locked = 0;
		bool forceRetained = false;
	};

	void Reset(bool validate);
	void Add(Reg r, Purpose p);
	void Change(Purpose history, Purpose destiny);
	void Release(Reg r, Purpose p);
	void Unlock(Reg r, Purpose p);
	bool Has(Purpose p);
	Reg Find(Purpose p);
	Reg Alloc(Purpose p);
	void ForceRetain(Purpose p);
	void ForceRelease(Purpose p);

	// For getting a specific reg.  WARNING: May return a locked reg, so you have to check.
	void GrabReg(Reg r, Purpose p, bool &needsSwap, Reg swapReg, Purpose swapPurpose);

private:
	RegStatus *FindReg(Reg r, Purpose p);
	RegStatus *FindReg(Reg r, bool isGen);

	std::vector<RegStatus> regs;
};

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

	PixelRegCache::Reg GetGState();
	PixelRegCache::Reg GetConstBase();
	PixelRegCache::Reg GetZeroVec();
	// Note: these may require a temporary reg.
	PixelRegCache::Reg GetColorOff(const PixelFuncID &id);
	PixelRegCache::Reg GetDepthOff(const PixelFuncID &id);
	PixelRegCache::Reg GetDestStencil(const PixelFuncID &id);

	bool Jit_ApplyDepthRange(const PixelFuncID &id);
	bool Jit_AlphaTest(const PixelFuncID &id);
	bool Jit_ApplyFog(const PixelFuncID &id);
	bool Jit_ColorTest(const PixelFuncID &id);
	bool Jit_StencilAndDepthTest(const PixelFuncID &id);
	bool Jit_StencilTest(const PixelFuncID &id, PixelRegCache::Reg stencilReg, PixelRegCache::Reg maskedReg);
	bool Jit_DepthTestForStencil(const PixelFuncID &id, PixelRegCache::Reg stencilReg);
	bool Jit_ApplyStencilOp(const PixelFuncID &id, GEStencilOp op, PixelRegCache::Reg stencilReg);
	bool Jit_WriteStencilOnly(const PixelFuncID &id, PixelRegCache::Reg stencilReg);
	bool Jit_DepthTest(const PixelFuncID &id);
	bool Jit_WriteDepth(const PixelFuncID &id);
	bool Jit_AlphaBlend(const PixelFuncID &id);
	bool Jit_BlendFactor(const PixelFuncID &id, PixelRegCache::Reg factorReg, PixelRegCache::Reg dstReg, GEBlendSrcFactor factor);
	bool Jit_DstBlendFactor(const PixelFuncID &id, PixelRegCache::Reg srcFactorReg, PixelRegCache::Reg dstFactorReg, PixelRegCache::Reg dstReg);
	bool Jit_Dither(const PixelFuncID &id);
	bool Jit_WriteColor(const PixelFuncID &id);
	bool Jit_ApplyLogicOp(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg maskReg);
	bool Jit_ConvertTo565(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg);
	bool Jit_ConvertTo5551(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertTo4444(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertFrom565(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg);
	bool Jit_ConvertFrom5551(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertFrom4444(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);

	std::unordered_map<PixelFuncID, SingleFunc> cache_;
	std::unordered_map<PixelFuncID, const u8 *> addresses_;
	PixelRegCache regCache_;

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
