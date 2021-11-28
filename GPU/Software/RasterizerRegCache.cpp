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

namespace Rasterizer {

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

RegCache::RegStatus *RegCache::FindReg(Reg r, Purpose p) {
	for (auto &reg : regs) {
		if (reg.reg == r && reg.purpose == p) {
			return &reg;
		}
	}

	return nullptr;
}

};
