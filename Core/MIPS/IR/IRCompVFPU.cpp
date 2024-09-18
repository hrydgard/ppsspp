// Copyright (c) 2012- PPSSPP Project.

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

#include <cmath>

#include "Common/CPUDetect.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"
#include "Core/Compatibility.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/Reporting.h"
#include "Core/System.h"


// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (opts.disableFlags & (uint32_t)JitDisable::flag) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }
#define INVALIDOP { Comp_Generic(op); return; }

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

const int vfpuBase = 32;  // skip the FP registers

namespace MIPSComp {
	static void ApplyVoffset(u8 regs[4], int count) {
		for (int i = 0; i < count; i++) {
			regs[i] = vfpuBase + voffset[regs[i]];
		}
	}

	static bool IsConsecutive2(const u8 regs[2]) {
		return regs[1] == regs[0] + 1;
	}

	static bool IsConsecutive3(const u8 regs[3]) {
		return IsConsecutive2(regs) && regs[2] == regs[1] + 1;
	}

	static bool IsConsecutive4(const u8 regs[4]) {
		return IsConsecutive3(regs) && regs[3] == regs[2] + 1;
	}

	static bool IsVec2(VectorSize sz, const u8 regs[2]) {
		return sz == V_Pair && IsConsecutive2(regs) && (regs[0] & 1) == 0;
	}

	static bool IsVec4(VectorSize sz, const u8 regs[4]) {
		return sz == V_Quad && IsConsecutive4(regs) && (regs[0] & 3) == 0;
	}

	static bool IsVec3of4(VectorSize sz, const u8 regs[4]) {
		return sz == V_Triple && IsConsecutive3(regs) && (regs[0] & 3) == 0;
	}

	static bool IsMatrixVec4(MatrixSize sz, const u8 regs[16]) {
		if (sz != M_4x4)
			return false;
		if (!IsConsecutive4(&regs[0]) || (regs[0] & 3) != 0)
			return false;
		if (!IsConsecutive4(&regs[4]) || (regs[4] & 3) != 0)
			return false;
		if (!IsConsecutive4(&regs[8]) || (regs[8] & 3) != 0)
			return false;
		if (!IsConsecutive4(&regs[12]) || (regs[12] & 3) != 0)
			return false;
		return true;
	}

	// Vector regs can overlap in all sorts of swizzled ways.
	// This does allow a single overlap in sregs[i].
	static bool IsOverlapSafeAllowS(int dreg, int di, int sn, const u8 sregs[], int tn = 0, const u8 tregs[] = NULL) {
		for (int i = 0; i < sn; ++i) {
			if (sregs[i] == dreg && i != di)
				return false;
		}
		for (int i = 0; i < tn; ++i) {
			if (tregs[i] == dreg)
				return false;
		}

		// Hurray, no overlap, we can write directly.
		return true;
	}

	static bool IsOverlapSafeAllowS(int dn, const u8 dregs[], int sn, const u8 sregs[], int tn = 0, const u8 tregs[] = nullptr) {
		for (int i = 0; i < dn; ++i) {
			if (!IsOverlapSafeAllowS(dregs[i], i, sn, sregs, tn, tregs)) {
				return false;
			}
		}
		return true;
	}

	static bool IsOverlapSafe(int dreg, int sn, const u8 sregs[], int tn = 0, const u8 tregs[] = nullptr) {
		return IsOverlapSafeAllowS(dreg, -1, sn, sregs, tn, tregs);
	}

	static bool IsOverlapSafe(int dn, const u8 dregs[], int sn, const u8 sregs[], int tn = 0, const u8 tregs[] = nullptr) {
		for (int i = 0; i < dn; ++i) {
			if (!IsOverlapSafe(dregs[i], sn, sregs, tn, tregs)) {
				return false;
			}
		}
		return true;
	}

	static bool IsPrefixWithinSize(u32 prefix, VectorSize sz) {
		int n = GetNumVectorElements(sz);
		for (int i = n; i < 4; i++) {
			int regnum = (prefix >> (i * 2)) & 3;
			int abs = (prefix >> (8 + i)) & 1;
			int negate = (prefix >> (16 + i)) & 1;
			int constants = (prefix >> (12 + i)) & 1;
			if (regnum >= n && !constants) {
				if (abs || negate || regnum != i)
					return false;
			}
		}

		return true;
	}

	static bool IsPrefixWithinSize(u32 prefix, MIPSOpcode op) {
		return IsPrefixWithinSize(prefix, GetVecSize(op));
	}

	void IRFrontend::Comp_VPFX(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		// This is how prefixes are typically set.
		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		switch (regnum) {
		case 0:  // S
			js.prefixS = data;
			js.prefixSFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		case 1:  // T
			js.prefixT = data;
			js.prefixTFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		case 2:  // D
			js.prefixD = data & 0x00000FFF;
			js.prefixDFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		default:
			ERROR_LOG(Log::CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
			break;
		}
	}

	static void InitRegs(u8 *vregs, int reg) {
		vregs[0] = reg;
		vregs[1] = reg + 1;
		vregs[2] = reg + 2;
		vregs[3] = reg + 3;
	}

	void IRFrontend::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz, int tempReg) {
		if (prefix == 0xE4)
			return;

		int n = GetNumVectorElements(sz);
		u8 origV[4]{};
		static const float constantArray[8] = { 0.f, 1.f, 2.f, 0.5f, 3.f, 1.f / 3.f, 0.25f, 1.f / 6.f };

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		// Some common vector prefixes
		if (IsVec4(sz, vregs)) {
			if (prefix == 0xF00E4) {
				InitRegs(vregs, tempReg);
				ir.Write(IROp::Vec4Neg, vregs[0], origV[0]);
				return;
			}
			if (prefix == 0x00FE4) {
				InitRegs(vregs, tempReg);
				ir.Write(IROp::Vec4Abs, vregs[0], origV[0]);
				return;
			}
			// Pure shuffle
			if (prefix == (prefix & 0xFF)) {
				InitRegs(vregs, tempReg);
				ir.Write(IROp::Vec4Shuffle, vregs[0], origV[0], prefix);
				return;
			}

			if ((prefix & 0x000FF000) == 0x0000F000) {
				// Handle some easy and common cases.
				Vec4Init init = Vec4Init::AllZERO;
				bool useInit;
				switch (prefix & 0xFFF) {
				case 0x00: useInit = true; init = Vec4Init::AllZERO; break;
				case 0x01: useInit = true; init = Vec4Init::Set_1000; break;
				case 0x04: useInit = true; init = Vec4Init::Set_0100; break;
				case 0x10: useInit = true; init = Vec4Init::Set_0010; break;
				case 0x40: useInit = true; init = Vec4Init::Set_0001; break;
				case 0x55: useInit = true; init = Vec4Init::AllONE; break;
				default: useInit = false; break;
				}

				if (useInit) {
					InitRegs(vregs, tempReg);
					ir.Write(IROp::Vec4Init, vregs[0], (int)init);
					return;
				}
			}

			// Check if we're just zeroing certain lanes - this is common.
			u32 zeroedLanes = 0;
			for (int i = 0; i < 4; ++i) {
				int regnum = (prefix >> (i * 2)) & 3;
				int abs = (prefix >> (8 + i)) & 1;
				int negate = (prefix >> (16 + i)) & 1;
				int constants = (prefix >> (12 + i)) & 1;

				if (!constants && regnum == i && !abs && !negate)
					continue;
				if (constants && regnum == 0 && abs == 0 && !negate) {
					zeroedLanes |= 1 << i;
					continue;
				}

				// Nope, it has something else going on.
				zeroedLanes = -1;
				break;
			}

			if (zeroedLanes != -1) {
				InitRegs(vregs, tempReg);
				ir.Write(IROp::Vec4Init, vregs[0], (int)Vec4Init::AllZERO);
				ir.Write(IROp::Vec4Blend, vregs[0], origV[0], vregs[0], zeroedLanes);
				return;
			}
		}

		// Alright, fall back to the generic approach.
		for (int i = 0; i < n; i++) {
			int regnum = (prefix >> (i * 2)) & 3;
			int abs = (prefix >> (8 + i)) & 1;
			int negate = (prefix >> (16 + i)) & 1;
			int constants = (prefix >> (12 + i)) & 1;

			// Unchanged, hurray.
			if (!constants && regnum == i && !abs && !negate)
				continue;

			// This puts the value into a temp reg, so we won't write the modified value back.
			vregs[i] = tempReg + i;
			if (!constants) {
				if (regnum >= n) {
					// Depends on the op, but often zero.
					ir.Write(IROp::SetConstF, vregs[i], ir.AddConstantFloat(0.0f));
				} else if (abs) {
					ir.Write(IROp::FAbs, vregs[i], origV[regnum]);
					if (negate)
						ir.Write(IROp::FNeg, vregs[i], vregs[i]);
				} else {
					if (negate)
						ir.Write(IROp::FNeg, vregs[i], origV[regnum]);
					else if (vregs[i] != origV[regnum])
						ir.Write(IROp::FMov, vregs[i], origV[regnum]);
				}
			} else {
				if (negate) {
					ir.Write(IROp::SetConstF, vregs[i], ir.AddConstantFloat(-constantArray[regnum + (abs << 2)]));
				} else {
					ir.Write(IROp::SetConstF, vregs[i], ir.AddConstantFloat(constantArray[regnum + (abs << 2)]));
				}
			}
		}
	}

	void IRFrontend::GetVectorRegs(u8 regs[4], VectorSize N, int vectorReg) {
		::GetVectorRegs(regs, N, vectorReg);
		ApplyVoffset(regs, N);
	}

	void IRFrontend::GetMatrixRegs(u8 regs[16], MatrixSize N, int matrixReg) {
		::GetMatrixRegs(regs, N, matrixReg);
		for (int i = 0; i < GetMatrixSide(N); i++) {
			ApplyVoffset(regs + 4 * i, GetVectorSize(N));
		}
	}

	void IRFrontend::GetVectorRegsPrefixS(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixSFlag & JitState::PREFIX_KNOWN);
		GetVectorRegs(regs, sz, vectorReg);
		ApplyPrefixST(regs, js.prefixS, sz, IRVTEMP_PFX_S);
	}
	void IRFrontend::GetVectorRegsPrefixT(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixTFlag & JitState::PREFIX_KNOWN);
		GetVectorRegs(regs, sz, vectorReg);
		ApplyPrefixST(regs, js.prefixT, sz, IRVTEMP_PFX_T);
	}

	void IRFrontend::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);

		GetVectorRegs(regs, sz, vectorReg);
		int n = GetNumVectorElements(sz);
		if (js.prefixD == 0)
			return;

		if (IsVec4(sz, regs) && js.VfpuWriteMask() != 0 && opts.preferVec4) {
			// Use temps for all, we'll blend in the end (keeping in Vec4.)
			for (int i = 0; i < 4; ++i)
				regs[i] = IRVTEMP_PFX_D + i;
			return;
		}

		for (int i = 0; i < n; i++) {
			// Hopefully this is rare, we'll just write it into a dumping ground reg.
			if (js.VfpuWriteMask(i))
				regs[i] = IRVTEMP_PFX_D + i;
		}
	}

	inline int GetDSat(int prefix, int i) {
		return (prefix >> (i * 2)) & 3;
	}

	// "D" prefix is really a post process. No need to allocate a temporary register (except
	// dummies to simulate writemask, which is done in GetVectorRegsPrefixD
	void IRFrontend::ApplyPrefixD(u8 *vregs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);
		if (!js.prefixD)
			return;

		ApplyPrefixDMask(vregs, sz, vectorReg);

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) {
			if (js.VfpuWriteMask(i))
				continue;
			int sat = GetDSat(js.prefixD, i);
			if (sat == 1) {
				// clamped = x < 0 ? (x > 1 ? 1 : x) : x [0, 1]
				ir.Write(IROp::FSat0_1, vregs[i], vregs[i]);
			} else if (sat == 3) {
				ir.Write(IROp::FSatMinus1_1, vregs[i], vregs[i]);
			}
		}
	}

	void IRFrontend::ApplyPrefixDMask(u8 *vregs, VectorSize sz, int vectorReg) {
		if (IsVec4(sz, vregs) && js.VfpuWriteMask() != 0 && opts.preferVec4) {
			u8 origV[4];
			GetVectorRegs(origV, sz, vectorReg);

			// Just keep the original values where it was masked.
			ir.Write(IROp::Vec4Blend, origV[0], vregs[0], origV[0], js.VfpuWriteMask());

			// So that saturate works, change it back.
			for (int i = 0; i < 4; ++i)
				vregs[i] = origV[i];
		}
	}

	void IRFrontend::Comp_SV(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU_VFPU);
		s32 offset = (signed short)(op & 0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		MIPSGPReg rs = _RS;

		CheckMemoryBreakpoint(rs, offset);

		switch (op >> 26) {
		case 50: //lv.s
			ir.Write(IROp::LoadFloat, vfpuBase + voffset[vt], rs, ir.AddConstant(offset));
			break;

		case 58: //sv.s
			ir.Write(IROp::StoreFloat, vfpuBase + voffset[vt], rs, ir.AddConstant(offset));
			break;

		default:
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_SVQ(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU_VFPU);
		int imm = (signed short)(op & 0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op & 1) << 5);
		MIPSGPReg rs = _RS;

		u8 vregs[4];
		GetVectorRegs(vregs, V_Quad, vt);

		CheckMemoryBreakpoint(rs, imm);

		enum class LSVType {
			INVALID,
			LVQ,
			SVQ,
			LVLQ,
			LVRQ,
			SVLQ,
			SVRQ,
		};

		LSVType optype = LSVType::INVALID;
		switch (op >> 26) {
		case 54: optype = LSVType::LVQ; break; // lv.q
		case 62: optype = LSVType::SVQ; break; // sv.q
		case 53: // lvl/lvr.q - highly unusual
			optype = (op & 2) == 0 ? LSVType::LVLQ : LSVType::LVRQ;
			break;
		case 61: // svl/svr.q - highly unusual
			optype = (op & 2) == 0 ? LSVType::SVLQ : LSVType::SVRQ;
			break;
		}
		if (optype == LSVType::INVALID)
			INVALIDOP;

		if ((optype == LSVType::LVRQ || optype == LSVType::SVRQ) && opts.unalignedLoadStoreVec4) {
			// We don't bother with an op for this, but we do fuse unaligned stores which happen.
			MIPSOpcode nextOp = GetOffsetInstruction(1);
			if ((nextOp.encoding ^ op.encoding) == 0x0000000E) {
				// Okay, it's an svr.q/svl.q pair, same registers.  Treat as lv.q/sv.q.
				EatInstruction(nextOp);
				optype = optype == LSVType::LVRQ ? LSVType::LVQ : LSVType::SVQ;
			}
		}

		switch (optype) {
		case LSVType::LVQ:
			if (IsVec4(V_Quad, vregs)) {
				ir.Write(IROp::LoadVec4, vregs[0], rs, ir.AddConstant(imm));
			} else {
				// Let's not even bother with "vertical" loads for now.
				if (!g_Config.bFastMemory)
					ir.Write(IROp::ValidateAddress128, 0, (u8)rs, 0, (u32)imm);
				ir.Write(IROp::LoadFloat, vregs[0], rs, ir.AddConstant(imm));
				ir.Write(IROp::LoadFloat, vregs[1], rs, ir.AddConstant(imm + 4));
				ir.Write(IROp::LoadFloat, vregs[2], rs, ir.AddConstant(imm + 8));
				ir.Write(IROp::LoadFloat, vregs[3], rs, ir.AddConstant(imm + 12));
			}
			break;

		case LSVType::SVQ:
			if (IsVec4(V_Quad, vregs)) {
				ir.Write(IROp::StoreVec4, vregs[0], rs, ir.AddConstant(imm));
			} else {
				// Let's not even bother with "vertical" stores for now.
				if (!g_Config.bFastMemory)
					ir.Write(IROp::ValidateAddress128, 0, (u8)rs, 1, (u32)imm);
				ir.Write(IROp::StoreFloat, vregs[0], rs, ir.AddConstant(imm));
				ir.Write(IROp::StoreFloat, vregs[1], rs, ir.AddConstant(imm + 4));
				ir.Write(IROp::StoreFloat, vregs[2], rs, ir.AddConstant(imm + 8));
				ir.Write(IROp::StoreFloat, vregs[3], rs, ir.AddConstant(imm + 12));
			}
			break;

		case LSVType::LVLQ:
		case LSVType::LVRQ:
		case LSVType::SVLQ:
		case LSVType::SVRQ:
			// These are pretty uncommon unless paired.
			DISABLE;
			break;

		default:
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_VVectorInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix() || js.HasSPrefix()) {
			DISABLE;
		}

		// Vector init
		// d[N] = CONST[N]
		// Note: probably implemented as vmov with prefix hack.

		VectorSize sz = GetVecSize(op);
		int type = (op >> 16) & 0xF;
		int vd = _VD;
		int n = GetNumVectorElements(sz);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, vd);

		if (IsVec4(sz, dregs)) {
			ir.Write(IROp::Vec4Init, dregs[0], (int)(type == 6 ? Vec4Init::AllZERO : Vec4Init::AllONE));
		} else {
			for (int i = 0; i < n; i++) {
				ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(type == 6 ? 0.0f : 1.0f));
			}
		}
		ApplyPrefixD(dregs, sz, vd);
	}

	void IRFrontend::Comp_VIdt(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix() || js.HasSPrefix()) {
			DISABLE;
		}

		// Vector identity row
		// d[N] = IDENTITY[N,m]
		// Note: probably implemented as vmov with prefix hack.

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, vd);

		if (IsVec4(sz, dregs)) {
			int row = vd & 3;
			Vec4Init init = Vec4Init((int)Vec4Init::Set_1000 + row);
			ir.Write(IROp::Vec4Init, dregs[0], (int)init);
		} else {
			switch (sz) {
			case V_Pair:
				ir.Write(IROp::SetConstF, dregs[0], ir.AddConstantFloat((vd & 1) == 0 ? 1.0f : 0.0f));
				ir.Write(IROp::SetConstF, dregs[1], ir.AddConstantFloat((vd & 1) == 1 ? 1.0f : 0.0f));
				break;
			case V_Quad:
				ir.Write(IROp::SetConstF, dregs[0], ir.AddConstantFloat((vd & 3) == 0 ? 1.0f : 0.0f));
				ir.Write(IROp::SetConstF, dregs[1], ir.AddConstantFloat((vd & 3) == 1 ? 1.0f : 0.0f));
				ir.Write(IROp::SetConstF, dregs[2], ir.AddConstantFloat((vd & 3) == 2 ? 1.0f : 0.0f));
				ir.Write(IROp::SetConstF, dregs[3], ir.AddConstantFloat((vd & 3) == 3 ? 1.0f : 0.0f));
				break;
			default:
				INVALIDOP;
			}
		}

		ApplyPrefixD(dregs, sz, vd);
	}

	void IRFrontend::Comp_VMatrixInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		MatrixSize sz = GetMtxSize(op);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		// Matrix init (weird prefixes)
		// d[N,M] = CONST[N,M]

		int vd = _VD;
		if (IsMatrixTransposed(vd)) {
			// All outputs are transpositionally symmetric, so should be fine.
			vd = TransposeMatrixReg(vd);
		}

		if (sz != M_4x4) {
			// 3x3 is decently common.  It expands a lot, but let's set each.
			u8 dregs[16];
			GetMatrixRegs(dregs, sz, vd);

			// TODO: It might be worth using Vec4Blend for 3x3 to mask w.
			int n = GetMatrixSide(sz);
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) {
					switch ((op >> 16) & 0xF) {
					case 3: // vmidt
						if (x == 0 && y == 0)
							ir.Write(IROp::SetConstF, dregs[y * 4 + x], ir.AddConstantFloat(1.0f));
						else if (x == y)
							ir.Write(IROp::FMov, dregs[y * 4 + x], dregs[0]);
						else
							ir.Write(IROp::SetConstF, dregs[y * 4 + x], ir.AddConstantFloat(0.0f));
						break;
					case 6: // vmzero
						// Likely to be fast.
						ir.Write(IROp::SetConstF, dregs[y * 4 + x], ir.AddConstantFloat(0.0f));
						break;
					case 7: // vmone
						if (x == 0 && y == 0)
							ir.Write(IROp::SetConstF, dregs[y * 4 + x], ir.AddConstantFloat(1.0f));
						else
							ir.Write(IROp::FMov, dregs[y * 4 + x], dregs[0]);
						break;
					default:
						INVALIDOP;
					}
				}
			}
			return;
		}

		// Not really about trying here, it will work if enabled.
		VectorSize vsz = GetVectorSize(sz);
		u8 vecs[4];
		GetMatrixColumns(vd, sz, vecs);
		for (int i = 0; i < 4; i++) {
			u8 vec[4];
			GetVectorRegs(vec, vsz, vecs[i]);
			// As they are columns, they will be nicely consecutive.
			Vec4Init init;
			switch ((op >> 16) & 0xF) {
			case 3:
				init = Vec4Init((int)Vec4Init::Set_1000 + i);
				break;
			case 6:
				init = Vec4Init::AllZERO;
				break;
			case 7:
				init = Vec4Init::AllONE;
				break;
			default:
				INVALIDOP;
				return;
			}
			ir.Write(IROp::Vec4Init, vec[0], (int)init);
		}
	}

	void IRFrontend::Comp_VHdp(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || js.HasSPrefix() || !IsPrefixWithinSize(js.prefixT, op)) {
			DISABLE;
		}

		// Vector homogenous dot product
		// d[0] = s[0 .. n-2] dot t[0 .. n-2] + t[n-1]
		// Note: s[n-1] is ignored / treated as 1 via prefix override.

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		if (js.prefixS & (0x0101 << (8 + n - 1)))
			DISABLE;

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		ir.Write(IROp::FMul, IRVTEMP_0, sregs[0], tregs[0]);

		for (int i = 1; i < n; i++) {
			if (i == n - 1) {
				ir.Write(IROp::FAdd, IRVTEMP_0, IRVTEMP_0, tregs[i]);
			} else {
				ir.Write(IROp::FMul, IRVTEMP_0 + 1, sregs[i], tregs[i]);
				ir.Write(IROp::FAdd, IRVTEMP_0, IRVTEMP_0, IRVTEMP_0 + 1);
			}
		}

		ir.Write(IROp::FMov, dregs[0], IRVTEMP_0);
		ApplyPrefixD(dregs, V_Single, vd);
	}

	alignas(16) static const float vavg_table[4] = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void IRFrontend::Comp_Vhoriz(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector horizontal add
		// d[0] = s[0] + ... s[n-1]
		// Vector horizontal average
		// d[0] = s[0] / n + ... s[n-1] / n
		// Note: Both are implemented as dot products against generated constants.

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, V_Single, _VD);

		// We have to start at +0.000 in case any values are -0.000.
		ir.Write(IROp::SetConstF, IRVTEMP_0, ir.AddConstantFloat(0.0f));
		for (int i = 0; i < n; ++i) {
			ir.Write(IROp::FAdd, IRVTEMP_0, IRVTEMP_0, sregs[i]);
		}

		switch ((op >> 16) & 31) {
		case 6:  // vfad
			ir.Write(IROp::FMov, dregs[0], IRVTEMP_0);
			break;
		case 7:  // vavg
			ir.Write(IROp::SetConstF, IRVTEMP_0 + 1, ir.AddConstantFloat(vavg_table[n - 1]));
			ir.Write(IROp::FMul, dregs[0], IRVTEMP_0, IRVTEMP_0 + 1);
			break;
		}

		ApplyPrefixD(dregs, V_Single, _VD);
	}

	void IRFrontend::Comp_VDot(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || !IsPrefixWithinSize(js.prefixT, op)) {
			DISABLE;
		}

		// Vector dot product
		// d[0] = s[0 .. n-1] dot t[0 .. n-1]

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		if (IsVec4(sz, sregs) && IsVec4(sz, tregs)) {
			if (IsOverlapSafe(dregs[0], n, sregs, n, tregs)) {
				ir.Write(IROp::Vec4Dot, dregs[0], sregs[0], tregs[0]);
			} else {
				ir.Write(IROp::Vec4Dot, IRVTEMP_0, sregs[0], tregs[0]);
				ir.Write(IROp::FMov, dregs[0], IRVTEMP_0);
			}
			ApplyPrefixD(dregs, V_Single, vd);
			return;
		} else if (IsVec3of4(sz, sregs) && IsVec3of4(sz, tregs) && opts.preferVec4Dot) {
			// Note: this is often worse than separate muliplies and adds on x86.
			if (IsOverlapSafe(dregs[0], n, tregs) || sregs[0] == tregs[0]) {
				// Nice example of this in Fat Princess (US) in block 088181A0 (hot.)
				// Create a temporary copy of S with the last element zeroed.
				ir.Write(IROp::Vec4Init, IRVTEMP_0, (int)Vec4Init::AllZERO);
				ir.Write(IROp::Vec4Blend, IRVTEMP_0, IRVTEMP_0, sregs[0], 0x7);
				// Now we can just dot like normal, with the last element effectively masked.
				ir.Write(IROp::Vec4Dot, dregs[0], IRVTEMP_0, sregs[0] == tregs[0] ? IRVTEMP_0 : tregs[0]);
				ApplyPrefixD(dregs, V_Single, vd);
				return;
			}
		}

		int temp0 = IRVTEMP_0;
		int temp1 = IRVTEMP_0 + 1;
		ir.Write(IROp::FMul, temp0, sregs[0], tregs[0]);
		for (int i = 1; i < n; i++) {
			ir.Write(IROp::FMul, temp1, sregs[i], tregs[i]);
			ir.Write(IROp::FAdd, i == (n - 1) ? dregs[0] : temp0, temp0, temp1);
		}
		ApplyPrefixD(dregs, V_Single, vd);
	}

	void IRFrontend::Comp_VecDo3(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || !IsPrefixWithinSize(js.prefixT, op)) {
			DISABLE;
		}

		// Vector arithmetic
		// d[N] = OP(s[N], t[N]) (see below)

		enum class VecDo3Op : uint8_t {
			INVALID,
			VADD,
			VSUB,
			VDIV,
			VMUL,
			VMIN,
			VMAX,
			VSGE,
			VSLT,
		};
		VecDo3Op type = VecDo3Op::INVALID;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		// Check that we can support the ops, and prepare temporary values for ops that need it.
		switch (op >> 26) {
		case 24: //VFPU0
			switch ((op >> 23) & 7) {
			case 0: type = VecDo3Op::VADD; break;
			case 1: type = VecDo3Op::VSUB; break;
			case 7: type = VecDo3Op::VDIV; break;
			default: INVALIDOP;
			}
			break;
		case 25: //VFPU1
			switch ((op >> 23) & 7) {
			case 0: type = VecDo3Op::VMUL; break;
			default: INVALIDOP;
			}
			break;
		case 27: //VFPU3
			switch ((op >> 23) & 7) {
			case 2: type = VecDo3Op::VMIN; break;
			case 3: type = VecDo3Op::VMAX; break;
			case 6: type = VecDo3Op::VSGE; break;
			case 7: type = VecDo3Op::VSLT; break;
			default: INVALIDOP;
			}
			break;
		default: INVALIDOP;
		}
		_assert_(type != VecDo3Op::INVALID);

		bool allowSIMD = true;
		switch (type) {
		case VecDo3Op::VADD:
		case VecDo3Op::VSUB:
		case VecDo3Op::VMUL:
			break;
		case VecDo3Op::VDIV:
			if (js.HasUnknownPrefix() || (sz != V_Single && !js.HasNoPrefix()))
				DISABLE;
			// If it's single, we just need to check the prefixes are within the size.
			if (!IsPrefixWithinSize(js.prefixS, op) || !IsPrefixWithinSize(js.prefixT, op))
				DISABLE;
			break;
		case VecDo3Op::VMIN:
		case VecDo3Op::VMAX:
		case VecDo3Op::VSGE:
		case VecDo3Op::VSLT:
			allowSIMD = false;
			break;
		case VecDo3Op::INVALID:  // Can't happen, but to avoid compiler warnings
			break;
		}

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; i++) {
			if (!IsOverlapSafe(dregs[i], n, sregs, n, tregs)) {
				tempregs[i] = IRVTEMP_0 + i;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// If all three are consecutive 4, we're safe regardless of if we use temps so we should not check that here.
		if (allowSIMD) {
			IROp opFunc = IROp::Nop;
			switch (type) {
			case VecDo3Op::VADD: // d[i] = s[i] + t[i]; break; //vadd
				opFunc = IROp::Vec4Add;
				break;
			case VecDo3Op::VSUB: // d[i] = s[i] - t[i]; break; //vsub
				opFunc = IROp::Vec4Sub;
				break;
			case VecDo3Op::VDIV: // d[i] = s[i] / t[i]; break; //vdiv
				opFunc = IROp::Vec4Div;
				break;
			case VecDo3Op::VMUL: // d[i] = s[i] * t[i]; break; //vmul
				opFunc = IROp::Vec4Mul;
				break;
			default:
				// Leave it Nop, disabled below.
				break;
			}

			if (IsVec4(sz, dregs) && IsVec4(sz, sregs) && IsVec4(sz, tregs)) {
				if (opFunc != IROp::Nop) {
					ir.Write(opFunc, dregs[0], sregs[0], tregs[0]);
				} else {
					DISABLE;
				}
				ApplyPrefixD(dregs, sz, _VD);
				return;
			} else if (IsVec3of4(sz, dregs) && IsVec3of4(sz, sregs) && IsVec3of4(sz, tregs) && opts.preferVec4) {
				// This is actually pretty common.  Use a temp + blend.
				// We could post-process this, but it's easier to do it here.
				if (opFunc == IROp::Nop)
					DISABLE;
				ir.Write(opFunc, IRVTEMP_0, sregs[0], tregs[0]);
				ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
				ApplyPrefixD(dregs, sz, _VD);
				return;
			}
		}

		if (type == VecDo3Op::VSGE || type == VecDo3Op::VSLT) {
			// TODO: Consider a dedicated op?  For now, we abuse FpCond a bit.
			ir.Write(IROp::FpCondToReg, IRTEMP_0);
		}

		for (int i = 0; i < n; ++i) {
			switch (type) {
			case VecDo3Op::VADD: // d[i] = s[i] + t[i]; break; //vadd
				ir.Write(IROp::FAdd, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VSUB: // d[i] = s[i] - t[i]; break; //vsub
				ir.Write(IROp::FSub, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VDIV: // d[i] = s[i] / t[i]; break; //vdiv
				ir.Write(IROp::FDiv, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VMUL: // d[i] = s[i] * t[i]; break; //vmul
				ir.Write(IROp::FMul, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VMIN: // vmin
				ir.Write(IROp::FMin, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VMAX: // vmax
				ir.Write(IROp::FMax, tempregs[i], sregs[i], tregs[i]);
				break;
			case VecDo3Op::VSGE: // vsge
				ir.Write(IROp::FCmp, (int)IRFpCompareMode::LessUnordered, sregs[i], tregs[i]);
				ir.Write(IROp::FpCondToReg, IRTEMP_1);
				ir.Write(IROp::XorConst, IRTEMP_1, IRTEMP_1, ir.AddConstant(1));
				ir.Write(IROp::FMovFromGPR, tempregs[i], IRTEMP_1);
				ir.Write(IROp::FCvtSW, tempregs[i], tempregs[i]);
				break;
			case VecDo3Op::VSLT: // vslt
				ir.Write(IROp::FCmp, (int)IRFpCompareMode::LessOrdered, sregs[i], tregs[i]);
				ir.Write(IROp::FpCondToReg, IRTEMP_1);
				ir.Write(IROp::FMovFromGPR, tempregs[i], IRTEMP_1);
				ir.Write(IROp::FCvtSW, tempregs[i], tempregs[i]);
				break;
			case VecDo3Op::INVALID:  // Can't happen, but to avoid compiler warnings
				break;
			}
		}

		if (type == VecDo3Op::VSGE || type == VecDo3Op::VSLT) {
			ir.Write(IROp::FpCondFromReg, IRTEMP_0);
		}

		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_VV2Op(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);

		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int optype = (op >> 16) & 0x1f;
		if (optype == 0) {
			if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op))
				DISABLE;
		} else if (optype == 1 || optype == 2) {
			// D prefix is fine for these, and used sometimes.
			if (js.HasUnknownPrefix() || js.HasSPrefix())
				DISABLE;
		} else if (optype == 5 && js.HasDPrefix()) {
			DISABLE;
		}

		// Vector unary operation
		// d[N] = OP(s[N]) (see below)

		int vs = _VS;
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		if (optype >= 16 && !js.HasNoPrefix()) {
			// Many of these apply the D prefix strangely or override parts of the S prefix.
			if (js.HasUnknownPrefix() || sz != V_Single)
				DISABLE;
			// If it's single, we just need to check the prefixes are within the size.
			if (!IsPrefixWithinSize(js.prefixS, op))
				DISABLE;
			// The negative ones seem to use negate flags as a prefix hack.
			if (optype >= 24 && (js.prefixS & 0x000F0000) != 0)
				DISABLE;
		}

		// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
		if (optype == 0 && vs == vd && js.HasNoPrefix()) {
			return;
		}

		u8 sregs[4]{}, dregs[4]{};
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixD(dregs, sz, vd);

		bool usingTemps = false;
		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs)) {
				usingTemps = true;
				tempregs[i] = IRVTEMP_0 + i;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		bool canSIMD = false;
		// Some can be SIMD'd.
		switch (optype) {
		case 0:  // vmov
		case 1:  // vabs
		case 2:  // vneg
			canSIMD = true;
			break;
		}

		if (canSIMD && !usingTemps) {
			IROp irop = IROp::Nop;
			switch (optype) {
			case 0:  // vmov
				irop = IROp::Vec4Mov;
				break;
			case 1:  // vabs
				irop = IROp::Vec4Abs;
				break;
			case 2:  // vneg
				irop = IROp::Vec4Neg;
				break;
			}
			if (IsVec4(sz, sregs) && IsVec4(sz, dregs) && irop != IROp::Nop) {
				ir.Write(irop, dregs[0], sregs[0]);
				ApplyPrefixD(dregs, sz, vd);
				return;
			} else if (IsVec3of4(sz, sregs) && IsVec3of4(sz, dregs) && irop != IROp::Nop && opts.preferVec4) {
				// This is a simple case of vmov.t, just blend.
				if (irop == IROp::Vec4Mov) {
					ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], sregs[0], 0x7);
				} else {
					ir.Write(irop, IRVTEMP_0, sregs[0]);
					ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
				}
				ApplyPrefixD(dregs, sz, vd);
				return;
			}
		}

		for (int i = 0; i < n; ++i) {
			switch (optype) {
			case 0: // d[i] = s[i]; break; //vmov
				// Probably for swizzle.
				if (tempregs[i] != sregs[i])
					ir.Write(IROp::FMov, tempregs[i], sregs[i]);
				break;
			case 1: // d[i] = fabsf(s[i]); break; //vabs
				ir.Write(IROp::FAbs, tempregs[i], sregs[i]);
				break;
			case 2: // d[i] = -s[i]; break; //vneg
				ir.Write(IROp::FNeg, tempregs[i], sregs[i]);
				break;
			case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
				ir.Write(IROp::FSat0_1, tempregs[i], sregs[i]);
				break;
			case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
				ir.Write(IROp::FSatMinus1_1, tempregs[i], sregs[i]);
				break;
			case 16: // d[i] = 1.0f / s[i]; break; //vrcp
				ir.Write(IROp::FRecip, tempregs[i], sregs[i]);
				break;
			case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
				ir.Write(IROp::FRSqrt, tempregs[i], sregs[i]);
				break;
			case 18: // d[i] = sinf((float)M_PI_2 * s[i]); break; //vsin
				ir.Write(IROp::FSin, tempregs[i], sregs[i]);
				break;
			case 19: // d[i] = cosf((float)M_PI_2 * s[i]); break; //vcos
				ir.Write(IROp::FCos, tempregs[i], sregs[i]);
				break;
			case 20: // d[i] = powf(2.0f, s[i]); break; //vexp2
				DISABLE;
				break;
			case 21: // d[i] = logf(s[i])/log(2.0f); break; //vlog2
				DISABLE;
				break;
			case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
				ir.Write(IROp::FSqrt, tempregs[i], sregs[i]);
				break;
			case 23: // d[i] = asinf(s[i]) / M_PI_2; break; //vasin
				ir.Write(IROp::FAsin, tempregs[i], sregs[i]);
				break;
			case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
				ir.Write(IROp::FRecip, tempregs[i], sregs[i]);
				ir.Write(IROp::FNeg, tempregs[i], tempregs[i]);
				break;
			case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
				ir.Write(IROp::FSin, tempregs[i], sregs[i]);
				ir.Write(IROp::FNeg, tempregs[i], tempregs[i]);
				break;
			case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
				DISABLE;
				break;
			default:
				INVALIDOP;
			}
		}
		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz, vd);
	}

	void IRFrontend::Comp_Vi2f(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op)) {
			DISABLE;
		}

		// Vector integer to float
		// d[N] = float(S[N]) * mult

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		uint8_t imm = (op >> 16) & 0x1f;

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		for (int i = 0; i < n; i++) {
			if (imm == 0)
				ir.Write(IROp::FCvtSW, dregs[i], sregs[i]);
			else
				ir.Write(IROp::FCvtScaledSW, dregs[i], sregs[i], imm);
		}
		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_Vh2f(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op)) {
			DISABLE;
		}

		// Vector expand half to float
		// d[N*2] = float(lowerhalf(s[N])), d[N*2+1] = float(upperhalf(s[N]))

		DISABLE;
	}

	void IRFrontend::Comp_Vf2i(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || (js.prefixD & 0xFF) != 0) {
			DISABLE;
		}

		// Vector float to integer
		// d[N] = int(S[N] * mult)
		// Note: saturates on overflow.

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		uint8_t imm = (op >> 16) & 0x1f;

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		// Same values as FCR31.
		uint8_t rmode = (op >> 21) & 3;
		if (((op >> 21) & 0x1C) != 0x10)
			INVALIDOP;

		if (imm != 0) {
			for (int i = 0; i < n; i++)
				ir.Write(IROp::FCvtScaledWS, dregs[i], sregs[i], imm | (rmode << 6));
		} else {
			for (int i = 0; i < n; i++) {
				switch (IRRoundMode(rmode)) {
				case IRRoundMode::RINT_0: // vf2in
					ir.Write(IROp::FRound, dregs[i], sregs[i]);
					break;

				case IRRoundMode::CAST_1: // vf2iz
					ir.Write(IROp::FTrunc, dregs[i], sregs[i]);
					break;

				case IRRoundMode::CEIL_2: // vf2iu
					ir.Write(IROp::FCeil, dregs[i], sregs[i]);
					break;

				case IRRoundMode::FLOOR_3: // vf2id
					ir.Write(IROp::FFloor, dregs[i], sregs[i]);
					break;

				default:
					INVALIDOP;
				}
			}
		}

		ApplyPrefixDMask(dregs, sz, _VD);
	}

	void IRFrontend::Comp_Mftv(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);

		// Vector move from VFPU / from VFPU ctrl (no prefixes)
		// gpr = S
		// gpr = VFPU_CTRL[i]

		int imm = op & 0xFF;
		MIPSGPReg rt = _RT;
		switch ((op >> 21) & 0x1f) {
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != MIPS_REG_ZERO) {
				if (imm < 128) {  //R(rt) = VI(imm);
					ir.Write(IROp::FMovToGPR, rt, vfpuBase + voffset[imm]);
				} else {
					switch (imm - 128) {
					case VFPU_CTRL_DPREFIX:
					case VFPU_CTRL_SPREFIX:
					case VFPU_CTRL_TPREFIX:
						FlushPrefixV();
						break;
					}
					if (imm - 128 < VFPU_CTRL_MAX) {
						ir.Write(IROp::VfpuCtrlToReg, rt, imm - 128);
					} else {
						INVALIDOP;
					}
				}
			}
			break;

		case 7: // mtv
			if (imm < 128) {
				ir.Write(IROp::FMovFromGPR, vfpuBase + voffset[imm], rt);
			} else if ((imm - 128) < VFPU_CTRL_MAX) {
				u32 mask;
				if (GetVFPUCtrlMask(imm - 128, &mask)) {
					if (mask != 0xFFFFFFFF) {
						ir.Write(IROp::AndConst, IRTEMP_0, rt, ir.AddConstant(mask));
						ir.Write(IROp::SetCtrlVFPUReg, imm - 128, IRTEMP_0);
					} else {
						ir.Write(IROp::SetCtrlVFPUReg, imm - 128, rt);
					}
				}

				if (imm - 128 == VFPU_CTRL_SPREFIX) {
					js.prefixSFlag = JitState::PREFIX_UNKNOWN;
					js.blockWrotePrefixes = true;
				} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
					js.prefixTFlag = JitState::PREFIX_UNKNOWN;
					js.blockWrotePrefixes = true;
				} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
					js.prefixDFlag = JitState::PREFIX_UNKNOWN;
					js.blockWrotePrefixes = true;
				}
			} else {
				INVALIDOP;
			}
			break;

		default:
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vmfvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);

		// Vector Move from vector control reg (no prefixes)
		// D[0] = VFPU_CTRL[i]

		int vd = _VD;
		int imm = (op >> 8) & 0x7F;
		if (imm < VFPU_CTRL_MAX) {
			ir.Write(IROp::VfpuCtrlToReg, IRTEMP_0, imm);
			ir.Write(IROp::FMovFromGPR, vfpuBase + voffset[vd], IRTEMP_0);
		} else {
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vmtvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);

		// Vector Move to vector control reg (no prefixes)
		// VFPU_CTRL[i] = S[0]

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm < VFPU_CTRL_MAX) {
			u32 mask;
			if (GetVFPUCtrlMask(imm, &mask)) {
				if (mask != 0xFFFFFFFF) {
					ir.Write(IROp::FMovToGPR, IRTEMP_0, vfpuBase + voffset[imm]);
					ir.Write(IROp::AndConst, IRTEMP_0, IRTEMP_0, ir.AddConstant(mask));
					ir.Write(IROp::SetCtrlVFPUReg, imm, IRTEMP_0);
				} else {
					ir.Write(IROp::SetCtrlVFPUFReg, imm, vfpuBase + voffset[vs]);
				}
			}
			if (imm == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			}
		} else {
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vmmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMMOV);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		// Matrix move (weird prefixes)
		// D[N,M] = S[N,M]

		int vs = _VS;
		int vd = _VD;
		// This probably ignores prefixes for all sane intents and purposes.
		if (vs == vd) {
			// A lot of these no-op matrix moves in Wipeout... Just drop the instruction entirely.
			return;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, vs);
		GetMatrixRegs(dregs, sz, vd);

		switch (GetMatrixOverlap(vs, vd, sz)) {
		case OVERLAP_EQUAL:
			// In-place transpose
			DISABLE;
		case OVERLAP_PARTIAL:
			DISABLE;
		case OVERLAP_NONE:
		default:
			break;
		}
		if (IsMatrixTransposed(vd) == IsMatrixTransposed(vs) && sz == M_4x4) {
			// Untranspose both matrices
			if (IsMatrixTransposed(vd)) {
				vd = TransposeMatrixReg(vd);
				vs = TransposeMatrixReg(vs);
			}
			// Get the columns
			u8 scols[4], dcols[4];
			GetMatrixColumns(vs, sz, scols);
			GetMatrixColumns(vd, sz, dcols);
			for (int i = 0; i < 4; i++) {
				u8 svec[4], dvec[4];
				GetVectorRegs(svec, GetVectorSize(sz), scols[i]);
				GetVectorRegs(dvec, GetVectorSize(sz), dcols[i]);
				ir.Write(IROp::Vec4Mov, dvec[0], svec[0]);
			}
			return;
		}
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				if (dregs[a * 4 + b] != sregs[a * 4 + b])
					ir.Write(IROp::FMov, dregs[a * 4 + b], sregs[a * 4 + b]);
			}
		}
	}

	void IRFrontend::Comp_Vmscl(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMSCL);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		// Matrix scale, matrix by scalar (weird prefixes)
		// d[N,M] = s[N,M] * t[0]
		// Note: behaves just slightly differently than a series of vscls.

		int vs = _VS;
		int vd = _VD;
		int vt = _VT;

		MatrixSize sz = GetMtxSize(op);
		if (sz != M_4x4) {
			DISABLE;
		}
		if (GetMtx(vt) == GetMtx(vd)) {
			DISABLE;
		}
		int n = GetMatrixSide(sz);

		// The entire matrix is scaled equally, so transpose doesn't matter.  Let's normalize.
		if (IsMatrixTransposed(vs) && IsMatrixTransposed(vd)) {
			vs = TransposeMatrixReg(vs);
			vd = TransposeMatrixReg(vd);
		}
		if (IsMatrixTransposed(vs) || IsMatrixTransposed(vd)) {
			DISABLE;
		}

		u8 sregs[16], dregs[16], tregs[1];
		GetMatrixRegs(sregs, sz, vs);
		GetMatrixRegs(dregs, sz, vd);
		GetVectorRegs(tregs, V_Single, vt);

		for (int i = 0; i < n; ++i) {
			ir.Write(IROp::Vec4Scale, dregs[i * 4], sregs[i * 4], tregs[0]);
		}
	}

	void IRFrontend::Comp_VScl(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector scale, vector by scalar
		// d[N] = s[N] * t[0]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		int vs = _VS;
		int vd = _VD;
		int vt = _VT;
		u8 sregs[4], dregs[4], treg;
		GetVectorRegsPrefixS(sregs, sz, vs);
		// T prefixes handled by interp.
		GetVectorRegs(&treg, V_Single, vt);
		GetVectorRegsPrefixD(dregs, sz, vd);

		bool overlap = false;
		// For prefixes to work, we just have to ensure that none of the output registers spill
		// and that there's no overlap.
		u8 tempregs[4];
		memcpy(tempregs, dregs, sizeof(tempregs));
		for (int i = 0; i < n; ++i) {
			// Conservative, can be improved
			if (treg == dregs[i] || !IsOverlapSafe(dregs[i], n, sregs)) {
				// Need to use temp regs
				tempregs[i] = IRVTEMP_0 + i;
				overlap = true;
			}
		}

		if (!overlap || (vs == vd && IsOverlapSafe(treg, n, dregs))) {
			if (IsVec4(sz, sregs) && IsVec4(sz, dregs)) {
				ir.Write(IROp::Vec4Scale, dregs[0], sregs[0], treg);
				ApplyPrefixD(dregs, sz, vd);
				return;
			} else if (IsVec3of4(sz, sregs) && IsVec3of4(sz, dregs) && opts.preferVec4) {
				ir.Write(IROp::Vec4Scale, IRVTEMP_0, sregs[0], treg);
				ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
				ApplyPrefixD(dregs, sz, vd);
				return;
			}
		}

		for (int i = 0; i < n; i++) {
			ir.Write(IROp::FMul, tempregs[i], sregs[i], treg);
		}

		for (int i = 0; i < n; i++) {
			// All must be mapped for prefixes to work.
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz, vd);
	}

	/*
	// Capital = straight, lower case = transposed
	// 8 possibilities:
	ABC   2
	ABc   missing
	AbC   1
	Abc   1

	aBC = ACB    2 + swap
	aBc = AcB    1 + swap
	abC = ACb    missing
	abc = Acb    1 + swap

	*/

	// This may or may not be a win when using the IR interpreter...
	// Many more instructions to interpret.
	void IRFrontend::Comp_Vmmul(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMMUL);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		if (PSP_CoreParameter().compat.flags().MoreAccurateVMMUL) {
			// Fall back to interpreter, which has the accurate implementation.
			// Later we might do something more optimized here.
			DISABLE;
		}

		// Matrix multiply (weird prefixes)
		// D[0 .. N, 0 .. M] = S[0 .. N, 0 .. M]' * T[0 .. N, 0 .. M]
		// Note: Behaves as if it's implemented through a series of vdots.
		// Important: this is a matrix multiply with a pre-transposed S.

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		int vs = _VS;
		int vd = _VD;
		int vt = _VT;
		MatrixOverlapType soverlap = GetMatrixOverlap(vs, vd, sz);
		MatrixOverlapType toverlap = GetMatrixOverlap(vt, vd, sz);

		// A very common arrangment. Rearrange to something we can handle.
		if (IsMatrixTransposed(vd)) {
			// Matrix identity says (At * Bt) = (B * A)t
			// D = S * T
			// Dt = (S * T)t = (Tt * St)
			vd = TransposeMatrixReg(vd);
			std::swap(vs, vt);
		}

		u8 sregs[16], tregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, vs);
		GetMatrixRegs(tregs, sz, vt);
		GetMatrixRegs(dregs, sz, vd);

		if (soverlap || toverlap) {
			DISABLE;
		}

		// dregs are always consecutive, thanks to our transpose trick.
		// However, not sure this is always worth it.
		if (IsMatrixVec4(sz, dregs)) {
			// TODO: The interpreter would like proper matrix ops better. Can generate those, and
			// expand them like this as needed on "real" architectures.
			int s0 = IRVTEMP_0;
			int s1 = IRVTEMP_PFX_T;
			if (!IsMatrixVec4(sz, sregs)) {
				// METHOD 1: Handles AbC and Abc
				for (int j = 0; j < 4; j++) {
					ir.Write(IROp::Vec4Scale, s0, sregs[0], tregs[j * 4]);
					for (int i = 1; i < 4; i++) {
						ir.Write(IROp::Vec4Scale, s1, sregs[i], tregs[j * 4 + i]);
						ir.Write(IROp::Vec4Add, s0, s0, s1);
					}
					ir.Write(IROp::Vec4Mov, dregs[j * 4], s0);
				}
				return;
			} else if (IsMatrixVec4(sz, tregs)) {
				// METHOD 2: Handles ABC only. Not efficient on CPUs that don't do fast dots.
				// Dots only work if tregs are consecutive.
				// TODO: Skip this and resort to method one and transpose the output?
				for (int j = 0; j < 4; j++) {
					for (int i = 0; i < 4; i++) {
						ir.Write(IROp::Vec4Dot, s0 + i, sregs[i * 4], tregs[j * 4]);
					}
					ir.Write(IROp::Vec4Mov, dregs[j * 4], s0);
				}
				return;
			} else {
				// ABc - s consecutive, t not.
				// Tekken uses this.
				// logBlocks = 1;
			}
		}

		// Fallback. Expands a LOT
		int temp0 = IRVTEMP_0;
		int temp1 = IRVTEMP_0 + 1;
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				ir.Write(IROp::FMul, temp0, sregs[b * 4], tregs[a * 4]);
				for (int c = 1; c < n; c++) {
					ir.Write(IROp::FMul, temp1, sregs[b * 4 + c], tregs[a * 4 + c]);
					ir.Write(IROp::FAdd, (c == n - 1) ? dregs[a * 4 + b] : temp0, temp0, temp1);
				}
			}
		}
	}

	void IRFrontend::Comp_Vtfm(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VTFM);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		// Vertex transform, vector by matrix (weird prefixes)
		// d[N] = s[N*m .. N*m + n-1] dot t[0 .. n-1]
		// Homogenous means t[n-1] is treated as 1.
		// Note: this might be implemented as a series of vdots with special prefixes.

		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);
		int ins = (op >> 23) & 7;

		bool homogenous = false;
		if (n == ins) {
			n++;
			sz = (VectorSize)((int)(sz)+1);
			msz = (MatrixSize)((int)(msz)+1);
			homogenous = true;
		}
		// Otherwise, n should already be ins + 1.
		else if (n != ins + 1) {
			DISABLE;
		}

		u8 sregs[16], dregs[4], tregs[4];
		GetMatrixRegs(sregs, msz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		// SIMD-optimized implementations - if sregs[0..3] is non-consecutive, it's transposed.
		if (msz == M_4x4 && !IsMatrixVec4(msz, sregs)) {
			int s0 = IRVTEMP_0;
			int s1 = IRVTEMP_PFX_S;
			// For this algorithm, we don't care if tregs are consecutive or not,
			// they are accessed one at a time. This handles homogenous transforms correctly, as well.
			// We take advantage of sregs[0] + 1 being sregs[4] here.
			ir.Write(IROp::Vec4Scale, s0, sregs[0], tregs[0]);
			for (int i = 1; i < 4; i++) {
				if (!homogenous || (i != n - 1)) {
					ir.Write(IROp::Vec4Scale, s1, sregs[i], tregs[i]);
					ir.Write(IROp::Vec4Add, s0, s0, s1);
				} else {
					ir.Write(IROp::Vec4Add, s0, s0, sregs[i]);
				}
			}
			if (IsVec4(sz, dregs)) {
				ir.Write(IROp::Vec4Mov, dregs[0], s0);
			} else {
				for (int i = 0; i < 4; i++) {
					ir.Write(IROp::FMov, dregs[i], s0 + i);
				}
			}
			return;
		} else if (msz == M_4x4 && IsMatrixVec4(msz, sregs) && IsVec4(sz, tregs)) {
			IRReg t = tregs[0];
			if (homogenous) {
				// This is probably even what the hardware basically does, wiring t[3] to 1.0f.
				ir.Write(IROp::Vec4Init, IRVTEMP_PFX_T, (int)Vec4Init::AllONE);
				ir.Write(IROp::Vec4Blend, IRVTEMP_PFX_T, IRVTEMP_PFX_T, t, 0x7);
				t = IRVTEMP_PFX_T;
			}
			for (int i = 0; i < 4; i++)
				ir.Write(IROp::Vec4Dot, IRVTEMP_PFX_D + i, sregs[i * 4], t);
			for (int i = 0; i < 4; i++)
				ir.Write(IROp::FMov, dregs[i], IRVTEMP_PFX_D + i);
			return;
		}

		// TODO: test overlap, optimize.
		u8 tempregs[4];
		int s0 = IRVTEMP_0;
		int temp1 = IRVTEMP_0 + 1;
		for (int i = 0; i < n; i++) {
			ir.Write(IROp::FMul, s0, sregs[i * 4], tregs[0]);
			for (int k = 1; k < n; k++) {
				if (!homogenous || k != n - 1) {
					ir.Write(IROp::FMul, temp1, sregs[i * 4 + k], tregs[k]);
					ir.Write(IROp::FAdd, s0, s0, temp1);
				} else {
					ir.Write(IROp::FAdd, s0, s0, sregs[i * 4 + k]);
				}
			}
			int temp = IRVTEMP_PFX_T + i;
			ir.Write(IROp::FMov, temp, s0);
			tempregs[i] = temp;
		}
		for (int i = 0; i < n; i++) {
			if (tempregs[i] != dregs[i])
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
		}
	}

	void IRFrontend::Comp_VCrs(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || js.HasSPrefix() || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector cross (half a cross product, n = 3)
		// d[0] = s[y]*t[z], d[1] = s[z]*t[x], d[2] = s[x]*t[y]
		// To do a full cross product: vcrs tmp1, s, t; vcrs tmp2 t, s; vsub d, tmp1, tmp2;
		// (or just use vcrsp.)
		// Note: this is possibly just a swizzle prefix hack for vmul.

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		if (sz != V_Triple)
			DISABLE;

		u8 sregs[4], dregs[4], tregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		if (IsVec3of4(sz, dregs) && IsVec3of4(sz, sregs) && IsVec3of4(sz, tregs) && opts.preferVec4) {
			// Use Vec4 where we can.  First, apply shuffles.
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_S, sregs[0], VFPU_SWIZZLE(1, 2, 0, 3));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_T, tregs[0], VFPU_SWIZZLE(2, 0, 1, 3));
			ir.Write(IROp::Vec4Mul, IRVTEMP_0, IRVTEMP_PFX_S, IRVTEMP_PFX_T);
			// Now just retain w and blend in our values.
			ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
		} else {
			u8 tempregs[4]{};
			if (!IsOverlapSafe(n, dregs, n, sregs, n, tregs)) {
				for (int i = 0; i < n; ++i)
					tempregs[i] = IRVTEMP_0 + i;
			} else {
				for (int i = 0; i < n; ++i)
					tempregs[i] = dregs[i];
			}

			ir.Write(IROp::FMul, tempregs[0], sregs[1], tregs[2]);
			ir.Write(IROp::FMul, tempregs[1], sregs[2], tregs[0]);
			ir.Write(IROp::FMul, tempregs[2], sregs[0], tregs[1]);

			for (int i = 0; i < n; i++) {
				if (tempregs[i] != dregs[i])
					ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_VDet(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector determinant
		// d[0] = s[0]*t[1] - s[1]*t[0]
		// Note: this operates on two vectors, not a 2x2 matrix.

		VectorSize sz = GetVecSize(op);
		if (sz != V_Pair)
			DISABLE;

		u8 sregs[4], dregs[4], tregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, V_Single, _VD);

		ir.Write(IROp::FMul, IRVTEMP_0, sregs[1], tregs[0]);
		ir.Write(IROp::FMul, dregs[0], sregs[0], tregs[1]);
		ir.Write(IROp::FSub, dregs[0], dregs[0], IRVTEMP_0);

		ApplyPrefixD(dregs, V_Single, _VD);
	}

	void IRFrontend::Comp_Vi2x(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || js.HasSPrefix())
			DISABLE;

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vi2uc/vi2c (0/1), vi2us/vi2s (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vi2uc (0), vi2us (2)

		// These instructions pack pairs or quads of integers into 32 bits.
		// The unsigned (u) versions skip the sign bit when packing, first doing a signed clamp to 0 (so the sign bit won't ever be 1).

		VectorSize sz = GetVecSize(op);
		VectorSize outsize;
		if (bits == 8) {
			outsize = V_Single;
			if (sz != V_Quad) {
				DISABLE;
			}
		} else {
			switch (sz) {
			case V_Pair:
				outsize = V_Single;
				break;
			case V_Quad:
				outsize = V_Pair;
				break;
			default:
				DISABLE;
			}
		}

		u8 sregs[4], dregs[2], srcregs[4], tempregs[2];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);
		memcpy(srcregs, sregs, sizeof(sregs));
		memcpy(tempregs, dregs, sizeof(dregs));

		int nOut = GetNumVectorElements(outsize);

		// If src registers aren't contiguous, make them.
		if (!IsVec2(sz, sregs) && !IsVec4(sz, sregs)) {
			// T prefix is unused.
			for (int i = 0; i < GetNumVectorElements(sz); i++) {
				srcregs[i] = IRVTEMP_PFX_T + i;
				ir.Write(IROp::FMov, srcregs[i], sregs[i]);
			}
		}

		if (bits == 8) {
			if (unsignedOp) {  //vi2uc
				// Output is only one register.
				ir.Write(IROp::Vec4ClampToZero, IRVTEMP_0, srcregs[0]);
				ir.Write(IROp::Vec4Pack31To8, tempregs[0], IRVTEMP_0);
			} else {  //vi2c
				ir.Write(IROp::Vec4Pack32To8, tempregs[0], srcregs[0]);
			}
		} else {
			// bits == 16
			if (unsignedOp) {  //vi2us
				// Output is only one register.
				ir.Write(IROp::Vec2ClampToZero, IRVTEMP_0, srcregs[0]);
				ir.Write(IROp::Vec2Pack31To16, tempregs[0], IRVTEMP_0);
				if (outsize == V_Pair) {
					ir.Write(IROp::Vec2ClampToZero, IRVTEMP_0 + 2, srcregs[2]);
					ir.Write(IROp::Vec2Pack31To16, tempregs[1], IRVTEMP_0 + 2);
				}
			} else {  //vi2s
				ir.Write(IROp::Vec2Pack32To16, tempregs[0], srcregs[0]);
				if (outsize == V_Pair) {
					ir.Write(IROp::Vec2Pack32To16, tempregs[1], srcregs[2]);
				}
			}
		}

		for (int i = 0; i < nOut; i++) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, outsize, _VD);
	}

	void IRFrontend::Comp_Vx2i(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || js.HasSPrefix())
			DISABLE;

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vuc2i/vc2i (0/1), vus2i/vs2i (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vuc2i (0), vus2i (2)

		// vs2i or vus2i unpack pairs of 16-bit integers into 32-bit integers, with the values
		// at the top.  vus2i shifts it an extra bit right afterward.
		// vc2i and vuc2i unpack quads of 8-bit integers into 32-bit integers, with the values
		// at the top too.  vuc2i is a bit special (see below.)
		// Let's do this similarly as h2f - we do a solution that works for both singles and pairs
		// then use it for both.

		VectorSize sz = GetVecSize(op);
		VectorSize outsize;
		if (bits == 8) {
			outsize = V_Quad;
			sz = V_Single;  // For some reason, sz is set to Quad in this case though the outsize is Single.
		} else {
			switch (sz) {
			case V_Single:
				outsize = V_Pair;
				break;
			case V_Pair:
				outsize = V_Quad;
				break;
			default:
				DISABLE;
			}
		}

		u8 sregs[2], dregs[4], tempregs[4], srcregs[2];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);
		memcpy(tempregs, dregs, sizeof(dregs));
		memcpy(srcregs, sregs, sizeof(sregs));

		// Remap source regs to be consecutive. This is not required
		// but helpful when implementations can join two Vec2Expand.
		if (sz == V_Pair && !IsConsecutive2(srcregs)) {
			for (int i = 0; i < 2; i++) {
				srcregs[i] = IRVTEMP_0 + i;
				ir.Write(IROp::FMov, srcregs[i], sregs[i]);
			}
		}

		int nIn = GetNumVectorElements(sz);

		int nOut = 2;
		if (outsize == V_Quad)
			nOut = 4;
		// Remap dest regs. PFX_T is unused.
		if (outsize == V_Pair) {
			bool consecutive = IsConsecutive2(dregs);
			// We must have them consecutive, so all temps, or none.
			if (!consecutive || !IsOverlapSafe(nOut, dregs, nIn, srcregs)) {
				for (int i = 0; i < nOut; i++) {
					tempregs[i] = IRVTEMP_PFX_T + i;
				}
			}
		} else if (outsize == V_Quad) {
			bool consecutive = IsVec4(outsize, dregs);
			if (!consecutive || !IsOverlapSafe(nOut, dregs, nIn, srcregs)) {
				for (int i = 0; i < nOut; i++) {
					tempregs[i] = IRVTEMP_PFX_T + i;
				}
			}
		}

		if (bits == 16) {
			if (unsignedOp) {
				ir.Write(IROp::Vec2Unpack16To31, tempregs[0], srcregs[0]);
				if (outsize == V_Quad)
					ir.Write(IROp::Vec2Unpack16To31, tempregs[2], srcregs[1]);
			} else {
				ir.Write(IROp::Vec2Unpack16To32, tempregs[0], srcregs[0]);
				if (outsize == V_Quad)
					ir.Write(IROp::Vec2Unpack16To32, tempregs[2], srcregs[1]);
			}
		} else if (bits == 8) {
			if (unsignedOp) {
				// See the interpreter, this one is odd. Hardware bug?
				ir.Write(IROp::Vec4Unpack8To32, tempregs[0], srcregs[0]);
				ir.Write(IROp::Vec4DuplicateUpperBitsAndShift1, tempregs[0], tempregs[0]);
			} else {
				ir.Write(IROp::Vec4Unpack8To32, tempregs[0], srcregs[0]);
			}
		}

		for (int i = 0; i < nOut; i++) {
			if (tempregs[i] != dregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}
		ApplyPrefixD(dregs, outsize, _VD);
	}

	void IRFrontend::Comp_VCrossQuat(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (!js.HasNoPrefix())
			DISABLE;

		// Vector cross product (n = 3, weird prefixes)
		// d[0 .. 2] = s[0 .. 2] X t[0 .. 2]
		// Vector quaternion product (n = 4, weird prefixes)
		// d[0 .. 2] = t[0 .. 2] X s[0 .. 2] + s[3] * t[0 .. 2] + t[3] * s[0 .. 2]
		// d[3] = s[3]*t[3] - s[0 .. 2] dot t[0 .. 3]
		// Note: Behaves as if it's implemented through a series of vdots.

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegs(sregs, sz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		if (sz == V_Triple) {
			u8 tempregs[4]{};
			for (int i = 0; i < n; ++i) {
				if (!IsOverlapSafe(dregs[i], n, sregs, n, tregs)) {
					tempregs[i] = IRVTEMP_PFX_T + i;   // using IRTEMP0 for other things
				} else {
					tempregs[i] = dregs[i];
				}
			}

			int temp0 = IRVTEMP_0;
			int temp1 = IRVTEMP_0 + 1;
			// Compute X
			ir.Write(IROp::FMul, temp0, sregs[1], tregs[2]);
			ir.Write(IROp::FMul, temp1, sregs[2], tregs[1]);
			ir.Write(IROp::FSub, tempregs[0], temp0, temp1);

			// Compute Y
			ir.Write(IROp::FMul, temp0, sregs[2], tregs[0]);
			ir.Write(IROp::FMul, temp1, sregs[0], tregs[2]);
			ir.Write(IROp::FSub, tempregs[1], temp0, temp1);

			// Compute Z
			ir.Write(IROp::FMul, temp0, sregs[0], tregs[1]);
			ir.Write(IROp::FMul, temp1, sregs[1], tregs[0]);
			ir.Write(IROp::FSub, tempregs[2], temp0, temp1);

			for (int i = 0; i < n; i++) {
				if (tempregs[i] != dregs[i])
					ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		} else if (sz == V_Quad) {
			// Rather than using vdots, we organize this as SIMD multiplies and adds.
			// That means flipping the logic column-wise.  Also, luckily no prefix temps used.
			if (!IsConsecutive4(sregs) || !IsConsecutive4(tregs) || !IsConsecutive4(dregs)) {
				DISABLE;
			}

			auto shuffleImm = [](int x, int y, int z, int w) { return x | (y << 2) | (z << 4) | (w << 6); };
			auto blendConst = [](int x, int y, int z, int w) { return x | (y << 1) | (z << 2) | (w << 3); };

			// Prepare some negatives.
			ir.Write(IROp::Vec4Neg, IRVTEMP_0, tregs[0]);

			// tmp = S[x,x,x,x] * T[w,-z,y,-x]
			ir.Write(IROp::Vec4Blend, IRVTEMP_PFX_S, tregs[0], IRVTEMP_0, blendConst(1, 0, 1, 0));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_T, IRVTEMP_PFX_S, shuffleImm(3, 2, 1, 0));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_S, sregs[0], shuffleImm(0, 0, 0, 0));
			ir.Write(IROp::Vec4Mul, IRVTEMP_PFX_D, IRVTEMP_PFX_S, IRVTEMP_PFX_T);

			// tmp += S[y,y,y,y] * T[z,w,-x,-y]
			ir.Write(IROp::Vec4Blend, IRVTEMP_PFX_S, tregs[0], IRVTEMP_0, blendConst(1, 1, 0, 0));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_T, IRVTEMP_PFX_S, shuffleImm(2, 3, 0, 1));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_S, sregs[0], shuffleImm(1, 1, 1, 1));
			ir.Write(IROp::Vec4Mul, IRVTEMP_PFX_S, IRVTEMP_PFX_S, IRVTEMP_PFX_T);
			ir.Write(IROp::Vec4Add, IRVTEMP_PFX_D, IRVTEMP_PFX_D, IRVTEMP_PFX_S);

			// tmp += S[z,z,z,z] * T[-y,x,w,-z]
			ir.Write(IROp::Vec4Blend, IRVTEMP_PFX_S, tregs[0], IRVTEMP_0, blendConst(0, 1, 1, 0));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_T, IRVTEMP_PFX_S, shuffleImm(1, 0, 3, 2));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_S, sregs[0], shuffleImm(2, 2, 2, 2));
			ir.Write(IROp::Vec4Mul, IRVTEMP_PFX_S, IRVTEMP_PFX_S, IRVTEMP_PFX_T);
			ir.Write(IROp::Vec4Add, IRVTEMP_PFX_D, IRVTEMP_PFX_D, IRVTEMP_PFX_S);

			// tmp += S[w,w,w,w] * T[x,y,z,w]
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_PFX_S, sregs[0], shuffleImm(3, 3, 3, 3));
			ir.Write(IROp::Vec4Mul, IRVTEMP_PFX_S, IRVTEMP_PFX_S, tregs[0]);
			ir.Write(IROp::Vec4Add, dregs[0], IRVTEMP_PFX_D, IRVTEMP_PFX_S);
		} else {
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vcmp(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_COMP);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || !IsPrefixWithinSize(js.prefixT, op)) {
			DISABLE;
		}

		// Vector compare
		// VFPU_CC[N] = COMPARE(s[N], t[N])

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);

		int cond = op & 0xF;
		int mask = 0;
		for (int i = 0; i < n; i++) {
			ir.Write(IROp::FCmpVfpuBit, cond | (i << 4), sregs[i], tregs[i]);
			mask |= (1 << i);
		}
		ir.Write(IROp::FCmpVfpuAggregate, mask);
	}

	void IRFrontend::Comp_Vcmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_COMP);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector conditional move
		// imm3 >= 6: d[N] = VFPU_CC[N] == tf ? s[N] : d[N]
		// imm3 < 6:  d[N] = VFPU_CC[imm3] == tf ? s[N] : d[N]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);
		int tf = (op >> 19) & 1;
		int imm3 = (op >> 16) & 7;

		if (IsVec4(sz, sregs) && IsVec4(sz, dregs)) {
			// TODO: Could do a VfpuCC variant of Vec4Blend.
		}

		for (int i = 0; i < n; ++i) {
			// Simplification: Disable if overlap unsafe
			if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs)) {
				DISABLE;
			}
		}
		if (imm3 < 6) {
			// Test one bit of CC. This bit decides whether none or all subregisters are copied.
			for (int i = 0; i < n; i++) {
				ir.Write(IROp::FCmovVfpuCC, dregs[i], sregs[i], (imm3) | ((!tf) << 7));
			}
		} else {
			// Look at the bottom four bits of CC to individually decide if the subregisters should be copied.
			for (int i = 0; i < n; i++) {
				ir.Write(IROp::FCmovVfpuCC, dregs[i], sregs[i], (i) | ((!tf) << 7));
			}
		}
		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_Viim(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector integer immediate
		// d[0] = float(imm)

		s32 imm = SignExtend16ToS32(op);
		u8 dreg;
		GetVectorRegsPrefixD(&dreg, V_Single, _VT);
		ir.Write(IROp::SetConstF, dreg, ir.AddConstantFloat((float)imm));
		ApplyPrefixD(&dreg, V_Single, _VT);
	}

	void IRFrontend::Comp_Vfim(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector half-float immediate
		// d[0] = float(imm)

		FP16 half;
		half.u = op & 0xFFFF;
		FP32 fval = half_to_float_fast5(half);

		u8 dreg;
		GetVectorRegsPrefixD(&dreg, V_Single, _VT);
		ir.Write(IROp::SetConstF, dreg, ir.AddConstantFloat(fval.f));
		ApplyPrefixD(&dreg, V_Single, _VT);
	}

	void IRFrontend::Comp_Vcst(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector constant
		// d[N] = CONST

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, vd);

		if (IsVec4(sz, dregs)) {
			ir.Write(IROp::SetConstF, IRVTEMP_0, ir.AddConstantFloat(cst_constants[conNum]));
			ir.Write(IROp::Vec4Shuffle, dregs[0], IRVTEMP_0, 0);
		} else if (IsVec3of4(sz, dregs) && opts.preferVec4) {
			ir.Write(IROp::SetConstF, IRVTEMP_0, ir.AddConstantFloat(cst_constants[conNum]));
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_0, IRVTEMP_0, 0);
			ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
		} else {
			for (int i = 0; i < n; i++) {
				// Most of the time, materializing a float is slower than copying from another float.
				if (i == 0)
					ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(cst_constants[conNum]));
				else
					ir.Write(IROp::FMov, dregs[i], dregs[0]);
			}
		}
		ApplyPrefixD(dregs, sz, vd);
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	void IRFrontend::Comp_VRot(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (!js.HasNoPrefix()) {
			// Prefixes work strangely for this:
			//  * They never apply to cos (whether d or s prefixes.)
			//  * They mostly apply to sin/0, e.g. 0:1, M, or |x|.
			DISABLE;
		}

		// Vector rotation matrix (weird prefixes)
		// d[N] = SINCOSVAL(s[0], imm[N])
		// The imm selects: cos index, sin index, 0 or sin for others, sin sign flip.

		int vd = _VD;
		int vs = _VS;
		int imm = (op >> 16) & 0x1f;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		int sineLane = (imm >> 2) & 3;
		int cosineLane = imm & 3;
		bool negSin = (imm & 0x10) ? true : false;
		bool broadcastSine = sineLane == cosineLane;

		char d[4] = { '0', '0', '0', '0' };
		if (broadcastSine) {
			for (int i = 0; i < 4; i++)
				d[i] = 's';
		}
		d[sineLane] = 's';
		d[cosineLane] = 'c';

		u8 dregs[4];
		GetVectorRegs(dregs, sz, vd);
		u8 sreg[1];
		GetVectorRegs(sreg, V_Single, vs);

		// If there's overlap, sin is calculated without it, but cosine uses the result.
		// This corresponds with prefix handling, where cosine doesn't get in prefixes.
		if (broadcastSine || !IsOverlapSafe(n, dregs, 1, sreg)) {
			ir.Write(IROp::FSin, IRVTEMP_0, sreg[0]);
			if (negSin)
				ir.Write(IROp::FNeg, IRVTEMP_0, IRVTEMP_0);
		}

		for (int i = 0; i < n; i++) {
			switch (d[i]) {
			case '0':
				ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(0.0f));
				break;
			case 's':
				if (broadcastSine || !IsOverlapSafe(n, dregs, 1, sreg)) {
					ir.Write(IROp::FMov, dregs[i], IRVTEMP_0);
				} else {
					ir.Write(IROp::FSin, dregs[i], sreg[0]);
					if (negSin) {
						ir.Write(IROp::FNeg, dregs[i], dregs[i]);
					}
				}
				break;
			case 'c':
				if (IsOverlapSafe(n, dregs, 1, sreg))
					ir.Write(IROp::FCos, dregs[i], sreg[0]);
				else if (dregs[sineLane] == sreg[0])
					ir.Write(IROp::FCos, dregs[i], IRVTEMP_0);
				else
					ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(1.0f));
				break;
			}
		}
	}

	void IRFrontend::Comp_Vsgn(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector extract sign
		// d[N] = signum(s[N])

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs)) {
				tempregs[i] = IRTEMP_0 + i;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		for (int i = 0; i < n; ++i) {
			ir.Write(IROp::FSign, tempregs[i], sregs[i]);
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_Vocp(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix() || (js.prefixS & VFPU_NEGATE(1, 1, 1, 1)) != 0) {
			DISABLE;
		}

		// Vector one's complement
		// d[N] = 1.0 - s[N]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		// This is a hack that modifies prefixes.  We eat them later, so just overwrite.
		// S prefix forces the negate flags.
		js.prefixS |= 0x000F0000;
		// T prefix forces constants on and regnum to 1.
		// That means negate still works, and abs activates a different constant.
		js.prefixT = (js.prefixT & ~0x000000FF) | 0x00000055 | 0x0000F000;

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		// There's no bits for t, so just reuse s.  It'll be constants only.
		GetVectorRegsPrefixT(tregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		if (IsVec4(sz, dregs) && IsVec4(sz, sregs) && IsVec4(sz, tregs)) {
			ir.Write(IROp::Vec4Add, dregs[0], tregs[0], sregs[0]);
		} else if (IsVec3of4(sz, dregs) && IsVec3of4(sz, sregs) && IsVec3of4(sz, tregs) && opts.preferVec4) {
			ir.Write(IROp::Vec4Add, IRVTEMP_0, tregs[0], sregs[0]);
			ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
		} else {
			u8 tempregs[4];
			for (int i = 0; i < n; ++i) {
				if (!IsOverlapSafe(dregs[i], n, sregs)) {
					tempregs[i] = IRVTEMP_0 + i;
				} else {
					tempregs[i] = dregs[i];
				}
			}

			for (int i = 0; i < n; ++i) {
				ir.Write(IROp::FAdd, tempregs[i], tregs[i], sregs[i]);
			}
			for (int i = 0; i < n; ++i) {
				if (dregs[i] != tempregs[i]) {
					ir.Write(IROp::FMov, dregs[i], tempregs[i]);
				}
			}
		}

		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_ColorConv(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix()) {
			DISABLE;
		}

		// Vector color conversion
		// d[N] = ConvertTo16(s[N*2]) | (ConvertTo16(s[N*2+1]) << 16)

		DISABLE;
	}

	void IRFrontend::Comp_Vbfy(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op) || js.HasTPrefix() || (js.prefixS & VFPU_NEGATE(1, 1, 1, 1)) != 0) {
			DISABLE;
		}

		// Vector butterfly operation
		// vbfy2: d[0] = s[0] + s[2], d[1] = s[1] + s[3], d[2] = s[0] - s[2], d[3] = s[1] - s[3]
		// vbfy1: d[N*2] = s[N*2] + s[N*2+1], d[N*2+1] = s[N*2] - s[N*2+1]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		if (n != 2 && n != 4) {
			// Bad instructions
			INVALIDOP;
		}

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs)) {
				tempregs[i] = IRVTEMP_0 + i;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		int subop = (op >> 16) & 0x1F;
		if (subop == 3 && n == 4) {
			// vbfy2
			ir.Write(IROp::FAdd, tempregs[0], sregs[0], sregs[2]);
			ir.Write(IROp::FAdd, tempregs[1], sregs[1], sregs[3]);
			ir.Write(IROp::FSub, tempregs[2], sregs[0], sregs[2]);
			ir.Write(IROp::FSub, tempregs[3], sregs[1], sregs[3]);
		} else if (subop == 2) {
			// vbfy1
			ir.Write(IROp::FAdd, tempregs[0], sregs[0], sregs[1]);
			ir.Write(IROp::FSub, tempregs[1], sregs[0], sregs[1]);
			if (n == 4) {
				ir.Write(IROp::FAdd, tempregs[2], sregs[2], sregs[3]);
				ir.Write(IROp::FSub, tempregs[3], sregs[2], sregs[3]);
			}
		} else {
			INVALIDOP;
		}

		for (int i = 0; i < n; ++i) {
			if (tempregs[i] != dregs[i])
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
		}

		ApplyPrefixD(dregs, sz, _VD);
	}
}
