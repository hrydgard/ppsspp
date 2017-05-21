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

struct SamplerID {
	SamplerID() : fullKey(0) {
	}

	union {
		u32 fullKey;
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
struct hash<SamplerID> {
	std::size_t operator()(const SamplerID &k) const {
		return hash<u32>()(k.fullKey);
	}
};

};

namespace Sampler {

typedef u32 (*NearestFunc)(int u, int v, const u8 *tptr, int bufw, int level);
NearestFunc GetNearestFunc();

typedef u32 (*LinearFunc)(int u[4], int v[4], int frac_u, int frac_v, const u8 *tptr, int bufw, int level);
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

#if PPSSPP_ARCH(ARM)
class SamplerJitCache : public ArmGen::ARMXCodeBlock {
#elif PPSSPP_ARCH(ARM64)
class SamplerJitCache : public Arm64Gen::ARM64CodeBlock {
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
class SamplerJitCache : public Gen::XCodeBlock {
#elif PPSSPP_ARCH(MIPS)
class SamplerJitCache : public MIPSGen::MIPSCodeBlock {
#else
class SamplerJitCache : public FakeGen::FakeXCodeBlock {
#endif
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

#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif

	std::unordered_map<SamplerID, NearestFunc> cache_;
	std::unordered_map<SamplerID, const u8 *> addresses_;
};

};
