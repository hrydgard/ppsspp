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
#include <vector>

#if defined(_M_SSE)
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON) && PPSSPP_ARCH(ARM64)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

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

namespace Rasterizer {

// While not part of the reg cache proper, this is the type it is built for.
#if PPSSPP_ARCH(ARM)
typedef ArmGen::ARMXCodeBlock CodeBlock;
#elif PPSSPP_ARCH(ARM64)
typedef Arm64Gen::ARM64CodeBlock CodeBlock;
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
typedef Gen::XCodeBlock CodeBlock;
#elif PPSSPP_ARCH(MIPS)
typedef MIPSGen::MIPSCodeBlock CodeBlock;
#else
typedef FakeGen::FakeXCodeBlock CodeBlock;
#endif

// We also have the types of things that end up in regs.
#if PPSSPP_ARCH(ARM64)
typedef int32x4_t Vec4IntArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return vld1q_s32(a.AsArray()); }
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
typedef __m128i Vec4IntArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a.ivec; }
#else
typedef const Math3D::Vec4<int> &Vec4IntArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a; }
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
		GEN_ARG_ID = 0x0184,
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

#if PPSSPP_ARCH(ARM)
	typedef ArmGen::ARMReg Reg;
	static constexpr Reg REG_INVALID_VALUE = ArmGen::INVALID_REG;
#elif PPSSPP_ARCH(ARM64)
	typedef Arm64Gen::ARM64Reg Reg;
	static constexpr Reg REG_INVALID_VALUE = Arm64Gen::INVALID_REG;
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	typedef Gen::X64Reg Reg;
	static constexpr Reg REG_INVALID_VALUE = Gen::INVALID_REG;
#elif PPSSPP_ARCH(MIPS)
	typedef MIPSGen::MIPSReg Reg;
	static constexpr Reg REG_INVALID_VALUE = MIPSGen::INVALID_REG;
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

private:
	RegStatus *FindReg(Reg r, Purpose p);

	std::vector<RegStatus> regs;
};

};
