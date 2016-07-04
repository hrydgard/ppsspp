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

#include "math/math_util.h"

#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Core/Reporting.h"


// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
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

	static bool IsConsecutive4(const u8 regs[4]) {
		return regs[1] == regs[0] + 1 &&
			     regs[2] == regs[1] + 1 &&
			     regs[3] == regs[2] + 1;
	}

	// Vector regs can overlap in all sorts of swizzled ways.
	// This does allow a single overlap in sregs[i].
	static bool IsOverlapSafeAllowS(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL) {
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

	static bool IsOverlapSafeAllowS(int dn, u8 dregs[], int sn, u8 sregs[], int tn = 0, u8 tregs[] = nullptr) {
		for (int i = 0; i < dn; ++i) {
			if (!IsOverlapSafeAllowS(dregs[i], i, sn, sregs, tn, tregs)) {
				return false;
			}
		}
		return true;
	}

	static bool IsOverlapSafe(int dreg, int sn, u8 sregs[], int tn = 0, u8 tregs[] = nullptr) {
		return IsOverlapSafeAllowS(dreg, -1, sn, sregs, tn, tregs);
	}

	static bool IsOverlapSafe(int dn, u8 dregs[], int sn, u8 sregs[], int tn = 0, u8 tregs[] = nullptr) {
		for (int i = 0; i < dn; ++i) {
			if (!IsOverlapSafe(dregs[i], sn, sregs, tn, tregs)) {
				return false;
			}
		}
		return true;
	}

	void IRFrontend::Comp_VPFX(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
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
			js.prefixD = data;
			js.prefixDFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		default:
			ERROR_LOG(CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
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
		u8 origV[4];
		static const float constantArray[8] = { 0.f, 1.f, 2.f, 0.5f, 3.f, 1.f / 3.f, 0.25f, 1.f / 6.f };

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		// Some common vector prefixes
		if (sz == V_Quad && IsConsecutive4(vregs)) {
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
				// Prefix may say "z, z, z, z" but if this is a pair, we force to x.
				// TODO: But some ops seem to use const 0 instead?
				if (regnum >= n) {
					WARN_LOG(CPU, "JIT: Invalid VFPU swizzle: %08x : %d / %d at PC = %08x (%s)", prefix, regnum, n, GetCompilerPC(), MIPSDisasmAt(GetCompilerPC()));
					regnum = 0;
				}

				if (abs) {
					ir.Write(IROp::FAbs, vregs[i], origV[regnum]);
					if (negate)
						ir.Write(IROp::FNeg, vregs[i], vregs[i]);
				} else {
					if (negate)
						ir.Write(IROp::FNeg, vregs[i], origV[regnum]);
					else
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
	void IRFrontend::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);
		if (!js.prefixD)
			return;

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

	void IRFrontend::Comp_SV(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
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
		CONDITIONAL_DISABLE;
		int imm = (signed short)(op & 0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op & 1) << 5);
		MIPSGPReg rs = _RS;

		u8 vregs[4];
		GetVectorRegs(vregs, V_Quad, vt);

		CheckMemoryBreakpoint(rs, imm);

		switch (op >> 26) {
		case 54: //lv.q
			if (IsConsecutive4(vregs)) {
				ir.Write(IROp::LoadVec4, vregs[0], rs, ir.AddConstant(imm));
			} else {
				// Let's not even bother with "vertical" loads for now.
				ir.Write(IROp::LoadFloat, vregs[0], rs, ir.AddConstant(imm));
				ir.Write(IROp::LoadFloat, vregs[1], rs, ir.AddConstant(imm + 4));
				ir.Write(IROp::LoadFloat, vregs[2], rs, ir.AddConstant(imm + 8));
				ir.Write(IROp::LoadFloat, vregs[3], rs, ir.AddConstant(imm + 12));
			}
			break;

		case 62: //sv.q
			if (IsConsecutive4(vregs)) {
				ir.Write(IROp::StoreVec4, vregs[0], rs, ir.AddConstant(imm));
			} else {
				// Let's not even bother with "vertical" stores for now.
				ir.Write(IROp::StoreFloat, vregs[0], rs, ir.AddConstant(imm));
				ir.Write(IROp::StoreFloat, vregs[1], rs, ir.AddConstant(imm + 4));
				ir.Write(IROp::StoreFloat, vregs[2], rs, ir.AddConstant(imm + 8));
				ir.Write(IROp::StoreFloat, vregs[3], rs, ir.AddConstant(imm + 12));
			}
			break;

		case 53: // lvl/lvr.q - highly unusual
		case 61: // svl/svr.q - highly unusual
			logBlocks = 1;
			DISABLE;
			break;

		default:
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_VVectorInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector init
		// d[N] = CONST[N]

		VectorSize sz = GetVecSize(op);
		int type = (op >> 16) & 0xF;
		int vd = _VD;
		int n = GetNumVectorElements(sz);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, vd);

		if (sz == V_Quad && IsConsecutive4(dregs)) {
			ir.Write(IROp::Vec4Init, dregs[0], (int)(type == 6 ? Vec4Init::AllZERO : Vec4Init::AllONE));
		} else {
			for (int i = 0; i < n; i++) {
				ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(type == 6 ? 0.0f : 1.0f));
			}
		}
		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_VIdt(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector identity row
		// d[N] = IDENTITY[N,m]

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, vd);

		if (sz == 4 && IsConsecutive4(dregs)) {
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

		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_VMatrixInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		MatrixSize sz = GetMtxSize(op);
		if (sz != M_4x4) {
			DISABLE;
		}

		// Matrix init (no prefixes)
		// d[N,M] = CONST[N,M]

		// Not really about trying here, it will work if enabled.
		VectorSize vsz = GetVectorSize(sz);
		u8 vecs[4];
		int vd = _VD;
		if (IsMatrixTransposed(vd)) {
			// All outputs are transpositionally symmetric, so should be fine.
			vd = TransposeMatrixReg(vd);
		}
		GetMatrixColumns(vd, M_4x4, vecs);
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
				return;
			}
			ir.Write(IROp::Vec4Init, vec[0], (int)init);
		}
	}

	void IRFrontend::Comp_VHdp(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector homogenous dot product
		// d[0] = s[0 .. n-2] dot t[0 .. n-2] + t[n-1]
		// Note: s[n-1] is ignored / treated as 1.

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		ir.Write(IROp::FMul, IRVTEMP_0, sregs[0], tregs[0]);

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			if (i == n - 1) {
				ir.Write(IROp::FAdd, IRVTEMP_0, IRVTEMP_0, tregs[i]);
			} else {
				ir.Write(IROp::FMul, IRVTEMP_0 + 1, sregs[i], tregs[i]);
				ir.Write(IROp::FAdd, IRVTEMP_0, IRVTEMP_0, IRVTEMP_0 + 1);
			}
		}

		ir.Write(IROp::FMov, dregs[0], IRVTEMP_0);
		ApplyPrefixD(dregs, V_Single);
	}

	static const float MEMORY_ALIGNED16(vavg_table[4]) = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void IRFrontend::Comp_Vhoriz(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector horizontal add
		// d[0] = s[0] + ... s[n-1]
		// Vector horizontal average
		// d[0] = (s[0] + ... s[n-1]) / n

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

		ApplyPrefixD(dregs, V_Single);
	}

	void IRFrontend::Comp_VDot(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
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

		if (sz == V_Quad && IsConsecutive4(sregs) && IsConsecutive4(tregs) && IsOverlapSafe(dregs[0], n, sregs, n, tregs)) {
			ir.Write(IROp::Vec4Dot, dregs[0], sregs[0], tregs[0]);
			ApplyPrefixD(dregs, V_Single);
			return;
		}

		int temp0 = IRVTEMP_0;
		int temp1 = IRVTEMP_0 + 1;
		ir.Write(IROp::FMul, temp0, sregs[0], tregs[0]);
		for (int i = 1; i < n; i++) {
			ir.Write(IROp::FMul, temp1, sregs[i], tregs[i]);
			ir.Write(IROp::FAdd, i == (n - 1) ? dregs[0] : temp0, temp0, temp1);
		}
		ApplyPrefixD(dregs, V_Single);
	}

	void IRFrontend::Comp_VecDo3(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector arithmetic
		// d[N] = OP(s[N], t[N]) (see below)

		// Check that we can support the ops, and prepare temporary values for ops that need it.
		bool allowSIMD = true;
		switch (op >> 26) {
		case 24: //VFPU0
			switch ((op >> 23) & 7) {
			case 0: // d[i] = s[i] + t[i]; break; //vadd
			case 1: // d[i] = s[i] - t[i]; break; //vsub
			case 7: // d[i] = s[i] / t[i]; break; //vdiv
				break;
			default:
				INVALIDOP;
			}
			break;
		case 25: //VFPU1
			switch ((op >> 23) & 7) {
			case 0: // d[i] = s[i] * t[i]; break; //vmul
				break;
			default:
				INVALIDOP;
			}
			break;
		case 27: //VFPU3
			switch ((op >> 23) & 7) {
			case 2:  // vmin
			case 3:  // vmax
				allowSIMD = false;
				break;
			case 6:  // vsge
			case 7:  // vslt
				allowSIMD = false;
				break;
			default:
				INVALIDOP;
			}
			break;
		default:
			INVALIDOP;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		bool usingTemps = false;
		for (int i = 0; i < n; i++) {
			if (!IsOverlapSafe(dregs[i], n, sregs, n, tregs)) {
				tempregs[i] = IRVTEMP_0 + i;
				usingTemps = true;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		if (allowSIMD && sz == V_Quad && !usingTemps && IsConsecutive4(dregs) && IsConsecutive4(sregs) && IsConsecutive4(tregs)) {
			IROp opFunc = IROp::Nop;
			switch (op >> 26) {
			case 24: //VFPU0
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] + t[i]; break; //vadd
					opFunc = IROp::Vec4Add;
					break;
				case 1: // d[i] = s[i] - t[i]; break; //vsub
					opFunc = IROp::Vec4Sub;
					break;
				case 7: // d[i] = s[i] / t[i]; break; //vdiv
					opFunc = IROp::Vec4Div;
					break;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23) & 7)
				{
				case 0: // d[i] = s[i] * t[i]; break; //vmul
					opFunc = IROp::Vec4Mul;
					break;
				}
				break;
			case 27: //VFPU3
				switch ((op >> 23) & 7)
				{
				case 2:  // vmin
				case 3:  // vmax
				case 6:  // vsge
				case 7:  // vslt
					DISABLE;
					break;
				}
				break;
			}

			if (opFunc != IROp::Nop) {
				ir.Write(opFunc, dregs[0], sregs[0], tregs[0]);
			} else {
				DISABLE;
			}
			ApplyPrefixD(dregs, sz);
			return;
		}

		for (int i = 0; i < n; ++i) {
			switch (op >> 26) {
			case 24: //VFPU0
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] + t[i]; break; //vadd
					ir.Write(IROp::FAdd, tempregs[i], sregs[i], tregs[i]);
					break;
				case 1: // d[i] = s[i] - t[i]; break; //vsub
					ir.Write(IROp::FSub, tempregs[i], sregs[i], tregs[i]);
					break;
				case 7: // d[i] = s[i] / t[i]; break; //vdiv
					ir.Write(IROp::FDiv, tempregs[i], sregs[i], tregs[i]);
					break;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] * t[i]; break; //vmul
					ir.Write(IROp::FMul, tempregs[i], sregs[i], tregs[i]);
					break;
				}
				break;
			case 27: //VFPU3
				switch ((op >> 23) & 7) {
				case 2:  // vmin
					ir.Write(IROp::FMin, tempregs[i], sregs[i], tregs[i]);
					break;
				case 3:  // vmax
					ir.Write(IROp::FMax, tempregs[i], sregs[i], tregs[i]);
					break;
				case 6:  // vsge
				case 7:  // vslt
					DISABLE;
					break;
				}
				break;
			}
		}

		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_VV2Op(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector unary operation
		// d[N] = OP(s[N]) (see below)

		int vs = _VS;
		int vd = _VD;

		// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
		if (((op >> 16) & 0x1f) == 0 && vs == vd && js.HasNoPrefix()) {
			return;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixD(dregs, sz, vd);

		bool usingTemps = false;
		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs)) {
				usingTemps = true;
				tempregs[i] = IRVTEMP_0 + i;
			} else {
				tempregs[i] = dregs[i];
			}
		}

		bool canSIMD = false;
		// Some can be SIMD'd.
		switch ((op >> 16) & 0x1f) {
		case 0:  // vmov
		case 1:  // vabs
		case 2:  // vneg
			canSIMD = true;
			break;
		}

		if (canSIMD && !usingTemps && IsConsecutive4(sregs) && IsConsecutive4(dregs)) {
			switch ((op >> 16) & 0x1f) {
			case 0:  // vmov
				ir.Write(IROp::Vec4Mov, dregs[0], sregs[0]);
				break;
			case 1:  // vabs
				ir.Write(IROp::Vec4Abs, dregs[0], sregs[0]);
				break;
			case 2:  // vneg
				ir.Write(IROp::Vec4Neg, dregs[0], sregs[0]);
				break;
			}
			ApplyPrefixD(dregs, sz);
			return;
		}

		for (int i = 0; i < n; ++i) {
			switch ((op >> 16) & 0x1f) {
			case 0: // d[i] = s[i]; break; //vmov
				// Probably for swizzle.
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
			default:
				INVALIDOP;
			}
		}
		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_Vi2f(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector integer to float
		// d[N] = float(S[N]) * mult

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		int imm = (op >> 16) & 0x1f;
		const float mult = 1.0f / (float)(1UL << imm);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs)) {
				tempregs[i] = IRVTEMP_PFX_T + i;  // Need IRVTEMP_0 for the scaling factor
			} else {
				tempregs[i] = dregs[i];
			}
		}
		if (mult != 1.0f)
			ir.Write(IROp::SetConstF, IRVTEMP_0, ir.AddConstantFloat(mult));
		// TODO: Use the SCVTF with builtin scaling where possible.
		for (int i = 0; i < n; i++) {
			ir.Write(IROp::FCvtSW, tempregs[i], sregs[i]);
		}
		if (mult != 1.0f) {
			for (int i = 0; i < n; i++) {
				ir.Write(IROp::FMul, tempregs[i], tempregs[i], IRVTEMP_0);
			}
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}
		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_Vh2f(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Vector expand half to float
		// d[N*2] = float(lowerhalf(s[N])), d[N*2+1] = float(upperhalf(s[N]))

		DISABLE;
	}

	void IRFrontend::Comp_Vf2i(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Vector float to integer
		// d[N] = int(S[N] * mult)
		// Note: saturates on overflow.

		DISABLE;
	}

	void IRFrontend::Comp_Mftv(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

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
				ir.Write(IROp::SetCtrlVFPU, imm - 128, rt);

				if (imm - 128 == VFPU_CTRL_SPREFIX) {
					js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
					js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
					js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				}
			} else {
				INVALIDOP;
			}
			break;

		default:
			INVALIDOP;
		}
		// This op is marked not to auto-eat prefix so we must do it manually.
		EatPrefix();
	}

	void IRFrontend::Comp_Vmfvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Vector Move from vector control reg (no prefixes)
		// S[0] = VFPU_CTRL[i]

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			ir.Write(IROp::VfpuCtrlToReg, IRTEMP_0, imm - 128);
			ir.Write(IROp::FMovFromGPR, vfpuBase + voffset[vs], IRTEMP_0);
		} else {
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vmtvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Vector Move to vector control reg (no prefixes)
		// VFPU_CTRL[i] = S[0]

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			ir.Write(IROp::SetCtrlVFPUFReg, imm - 128, vfpuBase + voffset[vs]);
			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = JitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = JitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = JitState::PREFIX_UNKNOWN;
			}
		} else {
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Vmmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Matrix move (no prefixes)
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

		// Rough overlap check.
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
				ir.Write(IROp::FMov, dregs[a * 4 + b], sregs[a * 4 + b]);
			}
		}
	}

	void IRFrontend::Comp_Vmscl(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// Matrix scale, matrix by scalar (no prefixes)
		// d[N,M] = s[N,M] * t[0]

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
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
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
		// TODO: Prefixes seem strange...
		GetVectorRegsPrefixT(&treg, V_Single, vt);
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

		if (n == 4 && IsConsecutive4(sregs) && IsConsecutive4(dregs)) {
			if (!overlap || (vs == vd && IsOverlapSafe(treg, n, dregs))) {
				ir.Write(IROp::Vec4Scale, dregs[0], sregs[0], treg);
				ApplyPrefixD(dregs, sz);
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

		ApplyPrefixD(dregs, sz);
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
		CONDITIONAL_DISABLE;

		// Matrix multiply (no prefixes)
		// D[0 .. N,0 .. M] = S[0 .. N, 0 .. M] * T[0 .. N,0 .. M]

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
		if (sz == M_4x4 && IsConsecutive4(dregs)) {
			// TODO: The interpreter would like proper matrix ops better. Can generate those, and
			// expand them like this as needed on "real" architectures.
			int s0 = IRVTEMP_0;
			int s1 = IRVTEMP_PFX_T;
			if (!IsConsecutive4(sregs)) {
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
			} else if (IsConsecutive4(tregs)) {
				// METHOD 2: Handles ABC only. Not efficient on CPUs that don't do fast dots.
				// Dots only work if tregs are consecutive.
				// TODO: Skip this and resort to method one and transpose the output?
				for (int j = 0; j < 4; j++) {
					for (int i = 0; i < 4; i++) {
						ir.Write(IROp::Vec4Dot, s0 + i, sregs[i], tregs[j * 4]);
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
		CONDITIONAL_DISABLE;

		// Vertex transform, vector by matrix (no prefixes)
		// d[N] = s[N*m .. N*m + n-1] dot t[0 .. n-1]
		// Homogenous means t[n-1] is treated as 1.

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
		if (msz == M_4x4 && !IsConsecutive4(sregs)) {
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
			if (IsConsecutive4(dregs)) {
				ir.Write(IROp::Vec4Mov, dregs[0], s0);
			} else {
				for (int i = 0; i < 4; i++) {
					ir.Write(IROp::FMov, dregs[i], s0 + i);
				}
			}
			return;
		} else if (msz == M_4x4 && IsConsecutive4(sregs)) {
			// Consecutive, which is harder.
			DISABLE;
			int s0 = IRVTEMP_0;
			int s1 = IRVTEMP_PFX_S;
			// Doesn't make complete sense to me why this works.... (because it doesn't.)
			ir.Write(IROp::Vec4Scale, s0, sregs[0], tregs[0]);
			for (int i = 1; i < 4; i++) {
				if (!homogenous || (i != n - 1)) {
					ir.Write(IROp::Vec4Scale, s1, sregs[i], tregs[i]);
					ir.Write(IROp::Vec4Add, s0, s0, s1);
				} else {
					ir.Write(IROp::Vec4Add, s0, s0, sregs[i]);
				}
			}
			if (IsConsecutive4(dregs)) {
				ir.Write(IROp::Vec4Mov, dregs[0], s0);
			} else {
				for (int i = 0; i < 4; i++) {
					ir.Write(IROp::FMov, dregs[i], s0 + i);
				}
			}
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
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector cross (half a cross product, n = 3)
		// d[0] = s[y]*t[z], d[1] = s[z]*t[x], d[2] = s[x]*t[y]
		// To do a full cross product: vcrs tmp1, s, t; vcrs tmp2 t, s; vsub d, tmp1, tmp2;
		// (or just use vcrsp.)

		DISABLE;
	}

	void IRFrontend::Comp_VDet(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Vector determinant
		// d[0] = s[0]*t[1] - s[1]*t[0]
		// Note: this operates on two vectors, not a 2x2 matrix.

		DISABLE;
	}

	void IRFrontend::Comp_Vi2x(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix())
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

		int n = GetNumVectorElements(sz);
		int nOut = GetNumVectorElements(outsize);

		// If src registers aren't contiguous, make them.
		if (sz == V_Quad && !IsConsecutive4(sregs)) {
			// T prefix is unused.
			for (int i = 0; i < 4; i++) {
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

		ApplyPrefixD(dregs, outsize);
	}

	void IRFrontend::Comp_Vx2i(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix())
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
			bool consecutive = IsConsecutive4(dregs);
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
		ApplyPrefixD(dregs, outsize);
	}

	void IRFrontend::Comp_VCrossQuat(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		// TODO: Does this instruction even look at prefixes at all?
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector cross product (n = 3)
		// d[0 .. 2] = s[0 .. 2] X t[0 .. 2]
		// Vector quaternion product (n = 4)
		// d[0 .. 2] = t[0 .. 2] X s[0 .. 2] + s[3] * t[0 .. 2] + t[3] * s[0 .. 2]
		// d[3] = s[3]*t[3] - s[0 .. 2] dot t[0 .. 3]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegs(sregs, sz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs, n, tregs)) {
				tempregs[i] = IRVTEMP_PFX_T + i;   // using IRTEMP0 for other things
			} else {
				tempregs[i] = dregs[i];
			}
		}

		if (sz == V_Triple) {
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
		} else if (sz == V_Quad) {
			DISABLE;
		}

		for (int i = 0; i < n; i++) {
			if (tempregs[i] != dregs[i])
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
		}
		// No D prefix supported
	}

	void IRFrontend::Comp_Vcmp(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector compare
		// VFPU_CC[N] = COMPARE(s[N], t[N])

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);

		VCondition cond = (VCondition)(op & 0xF);
		int mask = 0;
		for (int i = 0; i < n; i++) {
			ir.Write(IROp::FCmpVfpuBit, cond | (i << 4), sregs[i], tregs[i]);
			mask |= (1 << i);
		}
		ir.Write(IROp::FCmpVfpuAggregate, mask);
	}

	void IRFrontend::Comp_Vcmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
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
		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_Viim(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector integer immediate
		// d[0] = float(imm)

		s32 imm = (s32)(s16)(u16)(op & 0xFFFF);
		u8 dreg;
		GetVectorRegsPrefixD(&dreg, V_Single, _VT);
		ir.Write(IROp::SetConstF, dreg, ir.AddConstantFloat((float)imm));
		ApplyPrefixD(&dreg, V_Single);
	}

	void IRFrontend::Comp_Vfim(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
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
		ApplyPrefixD(&dreg, V_Single);
	}

	void IRFrontend::Comp_Vcst(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector constant
		// d[N] = CONST

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		for (int i = 0; i < n; i++) {
			ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(cst_constants[conNum]));
		}
		ApplyPrefixD(dregs, sz);
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	void IRFrontend::Comp_VRot(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
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
		bool negSin = (imm & 0x10) ? true : false;

		char d[4] = { '0', '0', '0', '0' };
		if (((imm >> 2) & 3) == (imm & 3)) {
			for (int i = 0; i < 4; i++)
				d[i] = 's';
		}
		d[(imm >> 2) & 3] = 's';
		d[imm & 3] = 'c';

		u8 dregs[4];
		GetVectorRegs(dregs, sz, vd);
		u8 sreg[1];
		GetVectorRegs(sreg, V_Single, vs);
		for (int i = 0; i < n; i++) {
			switch (d[i]) {
			case '0':
				ir.Write(IROp::SetConstF, dregs[i], ir.AddConstantFloat(0.0f));
				break;
			case 's':
				ir.Write(IROp::FSin, dregs[i], sreg[0]);
				if (negSin) {
					ir.Write(IROp::FNeg, dregs[i], dregs[i]);
				}
				break;
			case 'c':
				ir.Write(IROp::FCos, dregs[i], sreg[0]);
				break;
			}
		}
	}

	void IRFrontend::Comp_Vsgn(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
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

		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_Vocp(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		// Actually, not sure that this instruction accepts an S prefix. We don't apply it in the
		// interpreter. But whatever.
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		u8 tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], n, sregs)) {
				tempregs[i] = IRVTEMP_PFX_T + i;   // using IRTEMP0 for other things
			} else {
				tempregs[i] = dregs[i];
			}
		}

		ir.Write(IROp::SetConstF, IRVTEMP_0, ir.AddConstantFloat(1.0f));
		for (int i = 0; i < n; ++i) {
			ir.Write(IROp::FSub, tempregs[i], IRVTEMP_0, sregs[i]);
		}
		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
			}
		}

		ApplyPrefixD(dregs, sz);
	}

	void IRFrontend::Comp_ColorConv(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector color conversion
		// d[N] = ConvertTo16(s[N*2]) | (ConvertTo16(s[N*2+1]) << 16)

		DISABLE;
	}

	void IRFrontend::Comp_Vbfy(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector butterfly operation
		// vbfy2: d[0] = s[0] + s[2], d[1] = s[1] + s[3], d[2] = s[0] - s[2], d[3] = s[1] - s[3]
		// vbfy1: d[N*2] = s[N*2] + s[N*2+1], d[N*2+1] = s[N*2] - s[N*2+1]

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		if (n != 2 && n != 4) {
			// Bad instructions
			DISABLE;
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
		if (subop == 3) {
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
			DISABLE;
		}

		for (int i = 0; i < n; ++i) {
			if (tempregs[i] != dregs[i])
				ir.Write(IROp::FMov, dregs[i], tempregs[i]);
		}

		ApplyPrefixD(dregs, sz);
	}
}
