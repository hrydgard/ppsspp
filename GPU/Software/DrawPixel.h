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
#include <unordered_set>
#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerRegCache.h"

class BinManager;

namespace Rasterizer {

// Our std::unordered_map argument will ignore the alignment attribute, but that doesn't matter.
// We'll still have and want it for the actual function call, to keep the args in vector registers.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

typedef void (SOFTRAST_CALL *SingleFunc)(int x, int y, int z, int fog, Vec4IntArg color_in, const PixelFuncID &pixelID);
SingleFunc GetSingleFunc(const PixelFuncID &id, BinManager *binner);

void Init();
void FlushJit();
void Shutdown();

bool CheckDepthTestPassed(GEComparison func, int x, int y, int stride, u16 z);

bool DescribeCodePtr(const u8 *ptr, std::string &name);

struct PixelBlendState {
	bool usesFactors = false;
	bool usesDstAlpha = false;
	bool dstFactorIsInverse = false;
	bool srcColorAsFactor = false;
	bool dstColorAsFactor = false;
	bool readsDstPixel = true;
};
void ComputePixelBlendState(PixelBlendState &state, const PixelFuncID &id);

class PixelJitCache : public Rasterizer::CodeBlock {
public:
	PixelJitCache();

	// Returns a pointer to the code to run.
	SingleFunc GetSingle(const PixelFuncID &id, BinManager *binner);
	static SingleFunc GenericSingle(const PixelFuncID &id);
	void Clear() override;
	void Flush();

	std::string DescribeCodePtr(const u8 *ptr) override;

private:
	void Compile(const PixelFuncID &id);
	SingleFunc CompileSingle(const PixelFuncID &id);

	RegCache::Reg GetPixelID();
	void UnlockPixelID(RegCache::Reg &r);
	// Note: these may require a temporary reg.
	RegCache::Reg GetColorOff(const PixelFuncID &id);
	RegCache::Reg GetDepthOff(const PixelFuncID &id);
	RegCache::Reg GetDestStencil(const PixelFuncID &id);

	void WriteConstantPool(const PixelFuncID &id);

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
	bool Jit_BlendFactor(const PixelFuncID &id, RegCache::Reg factorReg, RegCache::Reg dstReg, PixelBlendFactor factor);
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

	struct LastCache {
		size_t key;
		SingleFunc func;
		int gen = -1;

		bool Match(size_t k, int g) const {
			return key == k && gen == g;
		}

		void Set(size_t k, SingleFunc f, int g) {
			key = k;
			func = f;
			gen = g;
		}
	};

	DenseHashMap<size_t, SingleFunc> cache_;
	std::unordered_map<PixelFuncID, const u8 *> addresses_;
	std::unordered_set<PixelFuncID> compileQueue_;
	static int clearGen_;
	static thread_local LastCache lastSingle_;

	const u8 *constBlendHalf_11_4s_ = nullptr;
	const u8 *constBlendInvert_11_4s_ = nullptr;

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

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

};
