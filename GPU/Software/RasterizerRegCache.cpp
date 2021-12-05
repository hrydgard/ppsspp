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

#include "GPU/Software/RasterizerRegCache.h"

#include "Common/Arm64Emitter.h"

namespace Rasterizer {

void RegCache::SetupABI(const std::vector<Purpose> &args, bool forceRetain) {
#if PPSSPP_ARCH(ARM)
	_assert_msg_(false, "Not yet implemented");
#elif PPSSPP_ARCH(ARM64)
	using namespace Arm64Gen;

	// ARM64 has a generous allotment of registers.
	static const Reg genArgs[] = { X0, X1, X2, X3, X4, X5, X6, X7 };
	static const Reg vecArgs[] = { Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7 };
	size_t genIndex = 0;
	size_t vecIndex = 0;

	for (const Purpose &p : args) {
		if ((p & FLAG_GEN) != 0) {
			if (genIndex < ARRAY_SIZE(genArgs)) {
				Add(genArgs[genIndex++], p);
				if (forceRetain)
					ForceRetain(p);
			}
		} else {
			if (vecIndex < ARRAY_SIZE(vecArgs)) {
				Add(vecArgs[vecIndex++], p);
				if (forceRetain)
					ForceRetain(p);
			}
		}
	}

	// Any others are free and purposeless.
	for (size_t i = genIndex; i < ARRAY_SIZE(genArgs); ++i)
		Add(genArgs[i], GEN_INVALID);
	for (size_t i = vecIndex; i < ARRAY_SIZE(vecArgs); ++i)
		Add(vecArgs[i], VEC_INVALID);

	// Add all other caller saved regs without purposes yet.
	static const Reg genTemps[] = { X8, X9, X10, X11, X12, X13, X14, X15 };
	for (Reg r : genTemps)
		Add(r, GEN_INVALID);
	static const Reg vecTemps[] = { Q16, Q17, Q18, Q19, Q20, Q21, Q22, Q23 };
	for (Reg r : vecTemps)
		Add(r, VEC_INVALID);
	// We also have X16-17 and Q24-Q31, but leave those for ordered paired instructions.
#elif PPSSPP_ARCH(X86)
	_assert_msg_(false, "Not yet implemented");
#elif PPSSPP_ARCH(AMD64)
	using namespace Gen;

#if PPSSPP_PLATFORM(WINDOWS)
	// The Windows convention is annoying, as it wastes registers and keeps to "positions."
	Reg genArgs[] = { RCX, RDX, R8, R9 };
	Reg vecArgs[] = { XMM0, XMM1, XMM2, XMM3, XMM4, XMM5 };

	for (size_t i = 0; i < args.size(); ++i) {
		const Purpose &p = args[i];
		if ((p & FLAG_GEN) != 0) {
			if (i < ARRAY_SIZE(genArgs)) {
				Add(genArgs[i], p);
				genArgs[i] = INVALID_REG;
				if (forceRetain)
					ForceRetain(p);
			}
		} else {
			if (i < ARRAY_SIZE(vecArgs)) {
				Add(vecArgs[i], p);
				vecArgs[i] = INVALID_REG;
				if (forceRetain)
					ForceRetain(p);
			}
		}
	}

	// Any unused regs can be used freely as temps.
	for (Reg r : genArgs) {
		if (r != INVALID_REG)
			Add(r, GEN_INVALID);
	}
	for (Reg r : vecArgs) {
		if (r != INVALID_REG)
			Add(r, VEC_INVALID);
	}

	// Additionally, these three are volatile.
	// Must save: RBX, RSP, RBP, RDI, RSI, R12-R15, XMM6-15
	static const Reg genTemps[] = { RAX, R10, R11 };
	for (Reg r : genTemps)
		Add(r, GEN_INVALID);
#else
	// Okay, first, allocate args.  SystemV gives to the first of each usable pool.
	static const Reg genArgs[] = { RDI, RSI, RDX, RCX, R8, R9 };
	static const Reg vecArgs[] = { XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7 };
	size_t genIndex = 0;
	size_t vecIndex = 0;

	for (const Purpose &p : args) {
		if ((p & FLAG_GEN) != 0) {
			if (genIndex < ARRAY_SIZE(genArgs)) {
				Add(genArgs[genIndex++], p);
				if (forceRetain)
					ForceRetain(p);
			}
		} else {
			if (vecIndex < ARRAY_SIZE(vecArgs)) {
				Add(vecArgs[vecIndex++], p);
				if (forceRetain)
					ForceRetain(p);
			}
		}
	}

	// Any others are free and purposeless.
	for (size_t i = genIndex; i < ARRAY_SIZE(genArgs); ++i)
		Add(genArgs[i], GEN_INVALID);
	for (size_t i = vecIndex; i < ARRAY_SIZE(vecArgs); ++i)
		Add(vecArgs[i], VEC_INVALID);

	// Add all other caller saved regs without purposes yet.
	// Must save: RBX, RSP, RBP, R12-R15
	static const Reg genTemps[] = { RAX, R10, R11 };
	for (Reg r : genTemps)
		Add(r, GEN_INVALID);
	static const Reg vecTemps[] = { XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15 };
	for (Reg r : vecTemps)
		Add(r, VEC_INVALID);
#endif
#elif PPSSPP_ARCH(MIPS)
	_assert_msg_(false, "Not yet implemented");
#else
	_assert_msg_(false, "Not yet implemented");
#endif
}

void RegCache::Reset(bool validate) {
	if (validate) {
		for (auto &reg : regs) {
			_assert_msg_(reg.locked == 0, "softjit: Reset() with reg still locked (%04X)", reg.purpose);
			_assert_msg_(!reg.forceRetained, "softjit: Reset() with reg force retained (%04X)", reg.purpose);
		}
	}
	regs.clear();
}

void RegCache::Add(Reg r, Purpose p) {
	for (auto &reg : regs) {
		if (reg.reg == r && (reg.purpose & FLAG_GEN) == (p & FLAG_GEN)) {
			_assert_msg_(false, "softjit Add() reg duplicate (%04X)", p);
		}
	}
	_assert_msg_(r != REG_INVALID_VALUE, "softjit Add() invalid reg (%04X)", p);

	RegStatus newStatus;
	newStatus.reg = r;
	newStatus.purpose = p;
	regs.push_back(newStatus);
}

void RegCache::Change(Purpose history, Purpose destiny) {
	for (auto &reg : regs) {
		if (reg.purpose == history) {
			reg.purpose = destiny;
			return;
		}
	}

	_assert_msg_(false, "softjit Change() reg that isn't there (%04X)", history);
}

void RegCache::Release(Reg &r, Purpose p) {
	RegStatus *status = FindReg(r, p);
	_assert_msg_(status != nullptr, "softjit Release() reg that isn't there (%04X)", p);
	_assert_msg_(status->locked > 0, "softjit Release() reg that isn't locked (%04X)", p);
	_assert_msg_(!status->forceRetained, "softjit Release() reg that is force retained (%04X)", p);

	status->locked--;
	if (status->locked == 0) {
		if ((status->purpose & FLAG_GEN) != 0)
			status->purpose = GEN_INVALID;
		else
			status->purpose = VEC_INVALID;
	}

	r = REG_INVALID_VALUE;
}

void RegCache::Unlock(Reg &r, Purpose p) {
	_assert_msg_((p & FLAG_TEMP) == 0, "softjit Unlock() temp reg (%04X)", p);
	RegStatus *status = FindReg(r, p);
	if (status) {
		_assert_msg_(status->locked > 0, "softjit Unlock() reg that isn't locked (%04X)", p);
		status->locked--;
		r = REG_INVALID_VALUE;
		return;
	}

	_assert_msg_(false, "softjit Unlock() reg that isn't there (%04X)", p);
}

bool RegCache::Has(Purpose p) {
	for (auto &reg : regs) {
		if (reg.purpose == p) {
			return true;
		}
	}
	return false;
}

RegCache::Reg RegCache::Find(Purpose p) {
	for (auto &reg : regs) {
		if (reg.purpose == p) {
			_assert_msg_(reg.locked <= 255, "softjit Find() reg has lots of locks (%04X)", p);
			reg.locked++;
			return reg.reg;
		}
	}
	_assert_msg_(false, "softjit Find() reg that isn't there (%04X)", p);
	return REG_INVALID_VALUE;
}

RegCache::Reg RegCache::Alloc(Purpose p) {
	_assert_msg_(!Has(p), "softjit Alloc() reg duplicate (%04X)", p);
	RegStatus *best = nullptr;
	for (auto &reg : regs) {
		if (reg.locked != 0 || reg.forceRetained)
			continue;
		// Needs to be the same type.
		if ((reg.purpose & FLAG_GEN) != (p & FLAG_GEN))
			continue;

		if (best == nullptr)
			best = &reg;
		// Prefer a free/purposeless reg (includes INVALID.)
		if ((reg.purpose & FLAG_TEMP) != 0) {
			best = &reg;
			break;
		}
		// But also prefer a lower priority reg.
		if (reg.purpose < best->purpose)
			best = &reg;
	}

	if (best) {
		best->locked = 1;
		best->purpose = p;
		return best->reg;
	}

	_assert_msg_(false, "softjit Alloc() reg with none free (%04X)", p);
	return REG_INVALID_VALUE;
}

void RegCache::ForceRetain(Purpose p) {
	for (auto &reg : regs) {
		if (reg.purpose == p) {
			reg.forceRetained = true;
			return;
		}
	}

	_assert_msg_(false, "softjit ForceRetain() reg that isn't there (%04X)", p);
}

void RegCache::ForceRelease(Purpose p) {
	for (auto &reg : regs) {
		if (reg.purpose == p) {
			_assert_msg_(reg.locked == 0, "softjit ForceRelease() while locked (%04X)", p);
			reg.forceRetained = false;
			if ((reg.purpose & FLAG_GEN) != 0)
				reg.purpose = GEN_INVALID;
			else
				reg.purpose = VEC_INVALID;
			return;
		}
	}

	_assert_msg_(false, "softjit ForceRelease() reg that isn't there (%04X)", p);
}

void RegCache::GrabReg(Reg r, Purpose p, bool &needsSwap, Reg swapReg, Purpose swapPurpose) {
	for (auto &reg : regs) {
		if (reg.reg != r)
			continue;
		if ((reg.purpose & FLAG_GEN) != (p & FLAG_GEN))
			continue;

		// Easy version, it's free.
		if (reg.locked == 0 && !reg.forceRetained) {
			needsSwap = false;
			reg.purpose = p;
			reg.locked = 1;
			return;
		}

		// Okay, we need to swap.  Find that reg.
		needsSwap = true;
		RegStatus *swap = FindReg(swapReg, swapPurpose);
		if (swap) {
			swap->purpose = reg.purpose;
			swap->forceRetained = reg.forceRetained;
			swap->locked = reg.locked;
		} else {
			_assert_msg_(!Has(swapPurpose), "softjit GrabReg() wrong purpose (%04X)", swapPurpose);
			RegStatus newStatus = reg;
			newStatus.reg = swapReg;
			regs.push_back(newStatus);
		}

		reg.purpose = p;
		reg.locked = 1;
		reg.forceRetained = false;
		return;
	}

	_assert_msg_(false, "softjit GrabReg() reg that isn't there");
}

bool RegCache::ChangeReg(Reg r, Purpose p) {
	for (auto &reg : regs) {
		if (reg.reg != r)
			continue;
		if ((reg.purpose & FLAG_GEN) != (p & FLAG_GEN))
			continue;

		if (reg.purpose == p)
			return true;
		_assert_msg_(!Has(p), "softjit ChangeReg() duplicate purpose (%04X)", p);

		if (reg.locked != 0 || reg.forceRetained)
			return false;

		reg.purpose = p;
		return true;
	}

	_assert_msg_(false, "softjit ChangeReg() reg that isn't there");
	return false;
}

RegCache::RegStatus *RegCache::FindReg(Reg r, Purpose p) {
	for (auto &reg : regs) {
		if (reg.reg == r && reg.purpose == p) {
			return &reg;
		}
	}

	return nullptr;
}

};
