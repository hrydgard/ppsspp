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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Common/Common.h"
#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM64_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#if PPSSPP_ARCH(ARM)
#include "Common/ArmEmitter.h"
#elif PPSSPP_ARCH(ARM64_NEON)
#include "Common/Arm64Emitter.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Common/x64Emitter.h"
#elif PPSSPP_ARCH(MIPS)
#include "Common/MipsEmitter.h"
#elif PPSSPP_ARCH(RISCV64)
#include "Common/RiscVEmitter.h"
#else
#include "Common/FakeEmitter.h"
#endif
#include "GPU/Math3D.h"

namespace Rasterizer {

// While not part of the reg cache proper, this is the type it is built for.
#if PPSSPP_ARCH(ARM)
typedef ArmGen::ARMXCodeBlock BaseCodeBlock;
#elif PPSSPP_ARCH(ARM64_NEON)
typedef Arm64Gen::ARM64CodeBlock BaseCodeBlock;
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
typedef Gen::XCodeBlock BaseCodeBlock;
#elif PPSSPP_ARCH(MIPS)
typedef MIPSGen::MIPSCodeBlock BaseCodeBlock;
#elif PPSSPP_ARCH(RISCV64)
typedef RiscVGen::RiscVCodeBlock BaseCodeBlock;
#else
typedef FakeGen::FakeXCodeBlock BaseCodeBlock;
#endif

// We also have the types of things that end up in regs.
#if PPSSPP_ARCH(ARM64_NEON)
typedef int32x4_t Vec4IntArg;
typedef int32x4_t Vec4IntResult;
typedef float32x4_t Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return vld1q_s32(a.AsArray()); }
static inline Vec4IntArg ToVec4IntArg(const Vec4IntResult &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return vld1q_s32(a.AsArray()); }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return vld1q_f32(a.AsArray()); }
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
typedef __m128i Vec4IntArg;
typedef __m128i Vec4IntResult;
typedef __m128 Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a.ivec; }
static inline Vec4IntArg ToVec4IntArg(const Vec4IntResult &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return a.ivec; }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return a.vec; }
#else
typedef const Math3D::Vec4<int> &Vec4IntArg;
typedef Math3D::Vec4<int> Vec4IntResult;
typedef const Math3D::Vec4<float> &Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return a; }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return a; }
#endif

#if PPSSPP_ARCH(AMD64) && PPSSPP_PLATFORM(WINDOWS) && (defined(_MSC_VER) || defined(__clang__) || defined(__INTEL_COMPILER))
#define SOFTRAST_CALL __vectorcall
#else
#define SOFTRAST_CALL
#endif

struct RegCache {
	enum Purpose {
		FLAG_GEN = 0x0100,
		FLAG_TEMP = 0x1000,

		VEC_ZERO = 0x0000,
		VEC_RESULT = 0x0001,
		VEC_RESULT1 = 0x0002,
		VEC_U1 = 0x0003,
		VEC_V1 = 0x0004,
		VEC_INDEX = 0x0005,
		VEC_INDEX1 = 0x0006,

		GEN_SRC_ALPHA = 0x0100,
		GEN_ID = 0x0101,
		GEN_STENCIL = 0x0103,
		GEN_COLOR_OFF = 0x0104,
		GEN_DEPTH_OFF = 0x0105,
		GEN_RESULT = 0x0106,
		GEN_SHIFTVAL = 0x0107,

		GEN_ARG_X = 0x0180,
		GEN_ARG_Y = 0x0181,
		GEN_ARG_Z = 0x0182,
		GEN_ARG_FOG = 0x0183,
		GEN_ARG_ID = 0x0184,
		GEN_ARG_U = 0x0185,
		GEN_ARG_V = 0x0186,
		GEN_ARG_TEXPTR = 0x0187,
		GEN_ARG_BUFW = 0x0188,
		GEN_ARG_LEVEL = 0x0189,
		GEN_ARG_TEXPTR_PTR = 0x018A,
		GEN_ARG_BUFW_PTR = 0x018B,
		GEN_ARG_LEVELFRAC = 0x018C,
		VEC_ARG_COLOR = 0x0080,
		VEC_ARG_MASK = 0x0081,
		VEC_ARG_U = 0x0082,
		VEC_ARG_V = 0x0083,
		VEC_ARG_S = 0x0084,
		VEC_ARG_T = 0x0085,
		VEC_FRAC = 0x0086,

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

#if PPSSPP_ARCH(ARM)
	typedef ArmGen::ARMReg Reg;
	static constexpr Reg REG_INVALID_VALUE = ArmGen::INVALID_REG;
#elif PPSSPP_ARCH(ARM64_NEON)
	typedef Arm64Gen::ARM64Reg Reg;
	static constexpr Reg REG_INVALID_VALUE = Arm64Gen::INVALID_REG;
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	typedef Gen::X64Reg Reg;
	static constexpr Reg REG_INVALID_VALUE = Gen::INVALID_REG;
#elif PPSSPP_ARCH(MIPS)
	typedef MIPSGen::MIPSReg Reg;
	static constexpr Reg REG_INVALID_VALUE = MIPSGen::INVALID_REG;
#elif PPSSPP_ARCH(RISCV64)
	typedef RiscVGen::RiscVReg Reg;
	static constexpr Reg REG_INVALID_VALUE = RiscVGen::INVALID_REG;
#else
	typedef int Reg;
	static constexpr Reg REG_INVALID_VALUE = -1;
#endif

	struct RegStatus {
		Reg reg;
		Purpose purpose;
		uint8_t locked = 0;
		bool forceRetained = false;
		bool everLocked = false;
	};

	// Note: Assumes __vectorcall on Windows.
	// Keep in mind, some args won't fit in regs, this ignores stack and tracks what's in regs.
	void SetupABI(const std::vector<Purpose> &args, bool forceRetain = true);
	// Reset after compile complete, pass false for validate if compile failed.
	void Reset(bool validate);
	// Add register to cache for tracking with initial purpose (won't be locked or force retained.)
	void Add(Reg r, Purpose p);
	// Find registers with one purpose and change to the other.
	void Change(Purpose history, Purpose destiny);
	// Release a previously found or allocated register, setting purpose to invalid.
	void Release(Reg &r, Purpose p);
	// Unlock a previously found or allocated register, but try to retain it.
	void Unlock(Reg &r, Purpose p);
	// Check if the purpose is currently in a register.
	bool Has(Purpose p);
	// Return the register for a given purpose (check with Has() first if not certainly there.)
	Reg Find(Purpose p);
	// Allocate a new register for the given purpose.
	Reg Alloc(Purpose p);
	// Force a register to be retained, even if we run short on regs.
	void ForceRetain(Purpose p);
	// Reverse ForceRetain, and release the register back to invalid.
	void ForceRelease(Purpose p);

	// For getting a specific reg.  WARNING: May return a locked reg, so you have to check.
	void GrabReg(Reg r, Purpose p, bool &needsSwap, Reg swapReg, Purpose swapPurpose);
	// For setting the purpose of a specific reg.  Returns false if it is locked.
	bool ChangeReg(Reg r, Purpose p);
	// Retrieves whether reg was ever used.
	bool UsedReg(Reg r, Purpose flag);

private:
	RegStatus *FindReg(Reg r, Purpose p);

	std::vector<RegStatus> regs;
};

class CodeBlock : public BaseCodeBlock {
public:
	virtual std::string DescribeCodePtr(const u8 *ptr);
	virtual void Clear();

protected:
	CodeBlock(int size);

	RegCache::Reg GetZeroVec();

	void Describe(const std::string &message);
	// Returns amount of stack space used.
	int WriteProlog(int extraStack, const std::vector<RegCache::Reg> &vec, const std::vector<RegCache::Reg> &gen);
	// Returns updated function start position, modifies prolog and finishes writing.
	const u8 *WriteFinalizedEpilog();

	void WriteSimpleConst16x8(const u8 *&ptr, uint8_t value);
	void WriteSimpleConst8x16(const u8 *&ptr, uint16_t value);
	void WriteSimpleConst4x32(const u8 *&ptr, uint32_t value);
	void WriteDynamicConst16x8(const u8 *&ptr, uint8_t value);
	void WriteDynamicConst8x16(const u8 *&ptr, uint16_t value);
	void WriteDynamicConst4x32(const u8 *&ptr, uint32_t value);

#if PPSSPP_ARCH(ARM64_NEON)
	Arm64Gen::ARM64FloatEmitter fp;
#endif

	std::unordered_map<const u8 *, std::string> descriptions_;
	Rasterizer::RegCache regCache_;

private:
	u8 *lastPrologStart_ = nullptr;
	u8 *lastPrologEnd_ = nullptr;
	int savedStack_;
	int firstVecStack_;
	std::vector<RegCache::Reg> prologVec_;
	std::vector<RegCache::Reg> prologGen_;
};

};
