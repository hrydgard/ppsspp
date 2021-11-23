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
		INVALID,
		GSTATE,
		CONST_BASE,
		ALPHA,
		STENCIL,
		COLOR_OFF,
		DEPTH_OFF,

		// Above this can only be temps.
		TEMP0,
		TEMP1,
		TEMP2,
		TEMP3,
		TEMP4,
		TEMP5,
		TEMP_HELPER,
	};
	enum Type {
		T_GEN,
		T_VEC,
	};

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	typedef Gen::X64Reg Reg;
#else
	typedef int Reg;
#endif

	struct RegStatus {
		Reg reg;
		Purpose purpose;
		Type type;
		uint8_t locked = 0;
		bool forceLocked = false;
	};

	void Reset();
	void Release(Reg r, Type t, Purpose p = INVALID);
	void Unlock(Reg r, Type t);
	bool Has(Purpose p, Type t);
	Reg Find(Purpose p, Type t);
	Reg Alloc(Purpose p, Type t);
	void ForceLock(Purpose p, Type t, bool state = true);

	// For getting a specific reg.  WARNING: May return a locked reg, so you have to check.
	void GrabReg(Reg r, Purpose p, Type t, bool &needsSwap, Reg swapReg);

private:
	RegStatus *FindReg(Reg r, Type t);

	std::vector<RegStatus> regs;
};

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
	bool Jit_Dither(const PixelFuncID &id);
	bool Jit_WriteColor(const PixelFuncID &id);
	bool Jit_ConvertTo565(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg);
	bool Jit_ConvertTo5551(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);
	bool Jit_ConvertTo4444(const PixelFuncID &id, PixelRegCache::Reg colorReg, PixelRegCache::Reg temp1Reg, PixelRegCache::Reg temp2Reg, bool keepAlpha);

	std::unordered_map<PixelFuncID, SingleFunc> cache_;
	std::unordered_map<PixelFuncID, const u8 *> addresses_;
	PixelRegCache regCache_;

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	void Discard();
	void Discard(Gen::CCFlags cc);

	std::vector<Gen::FixupBranch> discards_;
#endif
};

};
