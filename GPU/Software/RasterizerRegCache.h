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

namespace Rasterizer {

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

	void Reset(bool validate);
	void Add(Reg r, Purpose p);
	void Change(Purpose history, Purpose destiny);
	void Release(Reg &r, Purpose p);
	void Unlock(Reg &r, Purpose p);
	bool Has(Purpose p);
	Reg Find(Purpose p);
	Reg Alloc(Purpose p);
	void ForceRetain(Purpose p);
	void ForceRelease(Purpose p);

	// For getting a specific reg.  WARNING: May return a locked reg, so you have to check.
	void GrabReg(Reg r, Purpose p, bool &needsSwap, Reg swapReg, Purpose swapPurpose);

private:
	RegStatus *FindReg(Reg r, Purpose p);

	std::vector<RegStatus> regs;
};

};
