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
#elif PPSSPP_ARCH(ARM64_NEON)
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
			reg.everLocked = true;
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
		best->everLocked = true;
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
			reg.everLocked = true;
			return;
		}

		// Okay, we need to swap.  Find that reg.
		needsSwap = true;
		RegStatus *swap = FindReg(swapReg, swapPurpose);
		if (swap) {
			swap->purpose = reg.purpose;
			swap->forceRetained = reg.forceRetained;
			swap->locked = reg.locked;
			swap->everLocked = true;
		} else {
			_assert_msg_(!Has(swapPurpose), "softjit GrabReg() wrong purpose (%04X)", swapPurpose);
			RegStatus newStatus = reg;
			newStatus.reg = swapReg;
			newStatus.everLocked = true;
			regs.push_back(newStatus);
		}

		reg.purpose = p;
		reg.locked = 1;
		reg.everLocked = true;
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
		// Since we're setting it's purpose, we must've used it.
		reg.everLocked = true;
		return true;
	}

	_assert_msg_(false, "softjit ChangeReg() reg that isn't there");
	return false;
}

bool RegCache::UsedReg(Reg r, Purpose flag) {
	for (auto &reg : regs) {
		if (reg.reg != r)
			continue;
		if ((reg.purpose & FLAG_GEN) != (flag & FLAG_GEN))
			continue;
		return reg.everLocked;
	}

	_assert_msg_(false, "softjit UsedReg() reg that isn't there");
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

CodeBlock::CodeBlock(int size)
#if PPSSPP_ARCH(ARM64_NEON)
	: fp(this)
#endif
{
	AllocCodeSpace(size);
	ClearCodeSpace(0);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32) && (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !PPSSPP_PLATFORM(UWP)
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#elif PPSSPP_ARCH(ARM)
	BKPT(0);
	BKPT(0);
#endif
}

int CodeBlock::WriteProlog(int extraStack, const std::vector<RegCache::Reg> &vec, const std::vector<RegCache::Reg> &gen) {
	savedStack_ = 0;
	firstVecStack_ = extraStack;
	prologVec_ = vec;
	prologGen_ = gen;

	int totalStack = 0;

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	using namespace Gen;

	BeginWrite();
	AlignCode16();
	lastPrologStart_ = GetWritableCodePtr();

	for (X64Reg r : gen) {
		PUSH(r);
		regCache_.Add(r, RegCache::GEN_INVALID);
		totalStack += 8;
	}

	savedStack_ = 16 * (int)vec.size() + extraStack;
	// We want to align if possible.  It starts out unaligned.
	if ((totalStack & 8) == 0)
		savedStack_ += 8;
	totalStack += savedStack_;
	if (savedStack_ != 0)
		SUB(64, R(RSP), Imm32(savedStack_));

	int nextOffset = extraStack;
	for (X64Reg r : vec) {
		MOVUPS(MDisp(RSP, nextOffset), r);
		regCache_.Add(r, RegCache::VEC_INVALID);
		nextOffset += 16;
	}

	lastPrologEnd_ = GetWritableCodePtr();
#else
	_assert_msg_(false, "Not yet implemented");
#endif

	return totalStack;
}

const u8 *CodeBlock::WriteFinalizedEpilog() {
	u8 *prologPtr = lastPrologStart_;
	ptrdiff_t prologMaxSize = lastPrologEnd_ - lastPrologStart_;
	lastPrologStart_ = nullptr;
	lastPrologEnd_ = nullptr;

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	using namespace Gen;

	bool prologChange = false;
	int nextOffset = firstVecStack_;
	for (X64Reg r : prologVec_) {
		if (regCache_.UsedReg(r, RegCache::VEC_INVALID)) {
			MOVUPS(r, MDisp(RSP, nextOffset));
			nextOffset += 16;
		} else {
			prologChange = true;
		}
	}

	// We use the stack offset in generated code, so maintain any difference.
	int unusedGenSpace = 0;
	for (X64Reg r : prologGen_) {
		if (!regCache_.UsedReg(r, RegCache::GEN_INVALID))
			unusedGenSpace += 8;
	}
	if (unusedGenSpace != 0)
		prologChange = true;

	if (savedStack_ + unusedGenSpace != 0)
		ADD(64, R(RSP), Imm32(savedStack_ + unusedGenSpace));
	for (int i = (int)prologGen_.size(); i > 0; --i) {
		X64Reg r = prologGen_[i - 1];
		if (regCache_.UsedReg(r, RegCache::GEN_INVALID))
			POP(r);
	}

	RET();
	EndWrite();

	if (prologChange) {
		// Okay, now let's rewrite the prolog since we didn't need all those regs.
		XEmitter prolog(prologPtr);
		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(prologPtr, 128, MEM_PROT_READ | MEM_PROT_WRITE);
		}

		// First, write the new prolog at the original position.
		for (X64Reg r : prologGen_) {
			if (regCache_.UsedReg(r, RegCache::GEN_INVALID))
				prolog.PUSH(r);
		}

		// Even if less of the stack is actually used, we want the number to match to references.
		if (savedStack_ + unusedGenSpace != 0)
			prolog.SUB(64, R(RSP), Imm32(savedStack_ + unusedGenSpace));

		nextOffset = firstVecStack_;
		for (X64Reg r : prologVec_) {
			if (regCache_.UsedReg(r, RegCache::VEC_INVALID)) {
				prolog.MOVUPS(MDisp(RSP, nextOffset), r);
				nextOffset += 16;
			}
		}

		ptrdiff_t prologLen = prolog.GetWritableCodePtr() - prologPtr;
		if (prologLen < prologMaxSize) {
			// We wrote it at the start, but we actually want it at the end.
			u8 *oldPrologPtr = prologPtr;
			prologPtr += prologMaxSize - prologLen;
			memmove(prologPtr, oldPrologPtr, prologLen);
			// Set INT3s before the new start to be safe.
			memset(oldPrologPtr, 0xCC, prologMaxSize - prologLen);
		}

		if (PlatformIsWXExclusive()) {
			ProtectMemoryPages(prologPtr, 128, MEM_PROT_READ | MEM_PROT_EXEC);
		}
	}
#else
	_assert_msg_(false, "Not yet implemented");
#endif

	return prologPtr;
}

RegCache::Reg CodeBlock::GetZeroVec() {
	if (!regCache_.Has(RegCache::VEC_ZERO)) {
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
		using namespace Gen;
		X64Reg r = regCache_.Alloc(RegCache::VEC_ZERO);
		PXOR(r, R(r));
		return r;
#else
		return RegCache::REG_INVALID_VALUE;
#endif
	}
	return regCache_.Find(RegCache::VEC_ZERO);
}

void CodeBlock::Describe(const std::string &message) {
	descriptions_[GetCodePointer()] = message;
}

std::string CodeBlock::DescribeCodePtr(const u8 *ptr) {
	ptrdiff_t dist = 0x7FFFFFFF;
	std::string found;
	for (const auto &it : descriptions_) {
		ptrdiff_t it_dist = ptr - it.first;
		if (it_dist >= 0 && it_dist < dist) {
			found = it.second;
			dist = it_dist;
		}
	}
	return found;
}

void CodeBlock::Clear() {
	ClearCodeSpace(0);
	descriptions_.clear();
}

void CodeBlock::WriteSimpleConst16x8(const u8 *&ptr, uint8_t value) {
	if (ptr == nullptr)
		WriteDynamicConst16x8(ptr, value);
}

void CodeBlock::WriteSimpleConst8x16(const u8 *&ptr, uint16_t value) {
	if (ptr == nullptr)
		WriteDynamicConst8x16(ptr, value);
}

void CodeBlock::WriteSimpleConst4x32(const u8 *&ptr, uint32_t value) {
	if (ptr == nullptr)
		WriteDynamicConst4x32(ptr, value);
}

void CodeBlock::WriteDynamicConst16x8(const u8 *&ptr, uint8_t value) {
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	ptr = AlignCode16();
	for (int i = 0; i < 16; ++i)
		Write8(value);
#else
	_assert_msg_(false, "Not yet implemented");
#endif
}

void CodeBlock::WriteDynamicConst8x16(const u8 *&ptr, uint16_t value) {
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	ptr = AlignCode16();
	for (int i = 0; i < 8; ++i)
		Write16(value);
#else
	_assert_msg_(false, "Not yet implemented");
#endif
}

void CodeBlock::WriteDynamicConst4x32(const u8 *&ptr, uint32_t value) {
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	ptr = AlignCode16();
	for (int i = 0; i < 4; ++i)
		Write32(value);
#else
	_assert_msg_(false, "Not yet implemented");
#endif
}

};
