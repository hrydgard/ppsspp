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

#pragma once

#include "ppsspp_config.h"

#include <unordered_map>
#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerRegCache.h"

namespace Sampler {

// Our std::unordered_map argument will ignore the alignment attribute, but that doesn't matter.
// We'll still have and want it for the actual function call, to keep the args in vector registers.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

typedef Rasterizer::Vec4IntResult (SOFTRAST_CALL *NearestFunc)(int u, int v, const u8 *tptr, int bufw, int level);
NearestFunc GetNearestFunc();

typedef Rasterizer::Vec4IntResult (SOFTRAST_CALL *LinearFunc)(Rasterizer::Vec4IntArg u, Rasterizer::Vec4IntArg v, int frac_u, int frac_v, const u8 *tptr, int bufw, int level);
LinearFunc GetLinearFunc();

struct Funcs {
	NearestFunc nearest;
	LinearFunc linear;
};
static inline Funcs GetFuncs() {
	Funcs f;
	f.nearest = GetNearestFunc();
	f.linear = GetLinearFunc();
	return f;
}

void Init();
void Shutdown();

bool DescribeCodePtr(const u8 *ptr, std::string &name);

class SamplerJitCache : public Rasterizer::CodeBlock {
public:
	SamplerJitCache();

	void ComputeSamplerID(SamplerID *id_out, bool linear);

	// Returns a pointer to the code to run.
	NearestFunc GetNearest(const SamplerID &id);
	LinearFunc GetLinear(const SamplerID &id);
	void Clear();

	std::string DescribeCodePtr(const u8 *ptr);
	std::string DescribeSamplerID(const SamplerID &id);

private:
	NearestFunc Compile(const SamplerID &id);
	LinearFunc CompileLinear(const SamplerID &id);

	bool Jit_ReadTextureFormat(const SamplerID &id);
	bool Jit_GetTexData(const SamplerID &id, int bitsPerTexel);
	bool Jit_GetTexDataSwizzled(const SamplerID &id, int bitsPerTexel);
	bool Jit_GetTexDataSwizzled4();
	bool Jit_Decode5650();
	bool Jit_Decode5551();
	bool Jit_Decode4444();
	bool Jit_TransformClutIndex(const SamplerID &id, int bitsPerIndex);
	bool Jit_ReadClutColor(const SamplerID &id);
	bool Jit_GetDXT1Color(const SamplerID &id, int blockSize, int alpha);
	bool Jit_ApplyDXTAlpha(const SamplerID &id);

#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif

	std::unordered_map<SamplerID, NearestFunc> cache_;
	std::unordered_map<SamplerID, const u8 *> addresses_;
	Rasterizer::RegCache regCache_;
};

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

};
