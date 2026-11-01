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

	// The same for the matrix's transpose: regs[i] starts a Vec4 holding regs[i], regs[i + 4], ...
	// Not all matrices that fail IsMatrixVec4 pass this, an offset (like E723) breaks both.
	static bool IsMatrixTransposedVec4(MatrixSize sz, const u8 regs[16]) {
		u8 transposed[16];
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				transposed[i * 4 + j] = regs[j * 4 + i];
			}
		}
		return IsMatrixVec4(sz, transposed);
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

	// The lanes are already a Vec4 reg, used as is.
	static bool DotLanesAreVec4(const IRFrontend::DotLane lanes[4]) {
		for (int i = 0; i < 4; i++) {
			if (lanes[i].reg < 0 || lanes[i].negate || lanes[i].reg != lanes[0].reg + i)
				return false;
		}
		return (lanes[0].reg & 3) == 0;
	}

	static bool DotLanesRead(const IRFrontend::DotLane lanes[4], int vec) {
		for (int i = 0; i < 4; i++) {
			if (lanes[i].reg >= vec && lanes[i].reg < vec + 4)
				return true;
		}
		return false;
	}

	// Building lane i into vec + i only clobbers what a later lane still has to read.
	static bool DotLanesBuildableIn(const IRFrontend::DotLane lanes[4], int vec) {
		for (int i = 0; i < 4; i++) {
			for (int j = i + 1; j < 4; j++) {
				if (lanes[j].reg == vec + i)
					return false;
			}
		}
		return true;
	}

	bool IRFrontend::BuildDotOperands(const DotLane s[4], const DotLane t[4], int avoid, u8 *sVec, u8 *tVec) {
		const bool sReady = DotLanesAreVec4(s), tReady = DotLanesAreVec4(t);
		// The D temp holds the destination when a D prefix masks lanes.
		const int temps[] = { IRVTEMP_0, js.prefixD == 0 ? IRVTEMP_PFX_D : -1, IRVTEMP_PFX_T, IRVTEMP_PFX_S };

		int sTemp = -1, tTemp = -1;
		if (!sReady) {
			for (int temp : temps) {
				if (temp >= 0 && temp != avoid && !DotLanesRead(t, temp) && DotLanesBuildableIn(s, temp)) {
					sTemp = temp;
					break;
				}
			}
			if (sTemp < 0)
				return false;
		}
		if (!tReady) {
			for (int temp : temps) {
				// Built after s, so it mustn't overwrite what s is (built or not).
				const int sVecReg = sReady ? s[0].reg : sTemp;
				const bool hitsS = sVecReg >= temp && sVecReg < temp + 4;
				if (temp >= 0 && temp != avoid && temp != sTemp && !hitsS && DotLanesBuildableIn(t, temp)) {
					tTemp = temp;
					break;
				}
			}
			if (tTemp < 0)
				return false;
		}

		auto build = [&](const DotLane lanes[4], int vec) {
			// The lanes of one quad in place, the rest constants Vec4Init has (vhdp's s): two vector
			// ops, which keeps the quad in one register.
			int quad = -1;
			u8 blend = 0;
			float c[4]{};
			bool vector = true;
			for (int i = 0; i < 4 && vector; i++) {
				if (lanes[i].reg < 0) {
					c[i] = lanes[i].constant;
				} else if (!lanes[i].negate && (lanes[i].reg & 3) == i && (quad < 0 || quad == lanes[i].reg - i)) {
					quad = lanes[i].reg - i;
					blend |= 1 << i;
				} else {
					vector = false;
				}
			}
			int init = -1;
			if (vector && quad >= 0 && quad != vec && blend != 0xF) {
				// In Vec4Init's order.
				static const float inits[][4] = {
					{ 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { -1, -1, -1, -1 },
					{ 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 },
				};
				for (int k = 0; k < (int)(sizeof(inits) / sizeof(inits[0])) && init < 0; k++) {
					bool match = true;
					for (int i = 0; i < 4; i++) {
						if (!(blend & (1 << i)) && memcmp(&inits[k][i], &c[i], sizeof(float)) != 0)
							match = false;
					}
					if (match)
						init = k;
				}
			}
			if (init >= 0) {
				ir.Write(IROp::Vec4Init, vec, init);
				ir.Write(IROp::Vec4Blend, vec, vec, quad, blend);
				return;
			}
			for (int i = 0; i < 4; i++) {
				if (lanes[i].reg < 0) {
					ir.WriteSetConstantFloat(vec + i, lanes[i].constant);
				} else if (lanes[i].negate) {
					ir.Write(IROp::FNeg, vec + i, lanes[i].reg);
				} else if (lanes[i].reg != vec + i) {
					ir.Write(IROp::FMov, vec + i, lanes[i].reg);
				}
			}
		};
		// s's temp holds no lane of t, and t's temp isn't where s is, so neither clobbers the other.
		if (!sReady)
			build(s, sTemp);
		if (!tReady)
			build(t, tTemp);
		*sVec = sReady ? s[0].reg : sTemp;
		*tVec = tReady ? t[0].reg : tTemp;
		return true;
	}

	bool IRFrontend::TryMultiDot(const MultiDot *dots, int count, int terms, int avoid, bool destScratch) {
		if (!opts.vfpuMultiDot || count < 2 || count > 4 || terms < 2)
			return false;

		auto sameLanes = [](const DotLane *x, const DotLane *y) {
			for (int i = 0; i < 4; i++) {
				if (x[i].reg != y[i].reg || x[i].negate != y[i].negate || (x[i].reg < 0 && x[i].constant != y[i].constant))
					return false;
			}
			return true;
		};
		bool sharedA = true, sharedB = true;
		for (int j = 1; j < count; j++) {
			sharedA = sharedA && sameLanes(dots[j].a, dots[0].a);
			sharedB = sharedB && sameLanes(dots[j].b, dots[0].b);
		}
		if (!sharedA && !sharedB)
			return false;
		auto blockLanes = [&](int j) { return sharedB ? dots[j].a : dots[j].b; };
		const DotLane *vec = sharedB ? dots[0].b : dots[0].a;

		auto isZero = [](const DotLane &l) { return l.reg < 0 && l.constant == 0.0f; };
		for (int j = 0; j < count; j++) {
			for (int i = terms; i < 4; i++) {
				if (!isZero(blockLanes(j)[i]) || !isZero(vec[i]))
					return false;
			}
		}

		// Find the block: lane k of dot j is its row (base + 4 * i + k), or its Vec4 (base + 4 * k + i).
		int base = -1;
		bool transposed = false;
		int laneOf[4];
		for (int mode = 0; mode < 2 && base < 0; mode++) {
			bool ok = true;
			int b = -1;
			int used = 0;
			for (int j = 0; j < count && ok; j++) {
				const DotLane *l = blockLanes(j);
				for (int i = 0; i < terms; i++)
					ok = ok && l[i].reg >= 0 && !l[i].negate;
				if (!ok)
					break;
				int lane, jb;
				if (mode == 0) {
					lane = l[0].reg & 3;
					jb = l[0].reg & ~3;
				} else {
					jb = l[0].reg & ~15;
					lane = (l[0].reg - jb) >> 2;
					ok = (l[0].reg & 3) == 0;
				}
				for (int i = 0; i < terms; i++)
					ok = ok && l[i].reg == (mode == 0 ? jb + 4 * i + lane : jb + 4 * lane + i);
				ok = ok && (b < 0 || jb == b) && (used & (1 << lane)) == 0;
				b = jb;
				used |= 1 << lane;
				laneOf[j] = lane;
			}
			// The whole block has to be VFPU regs.
			if (ok && b >= 32 && b + 16 <= 32 + 128) {
				base = b;
				transposed = mode == 1;
			}
		}
		if (base < 0)
			return false;

		// The dots run in order, so none may read an earlier one's result.
		for (int j = 1; j < count; j++) {
			for (int k = 0; k < j; k++) {
				for (int i = 0; i < terms; i++) {
					if (blockLanes(j)[i].reg == dots[k].dest || vec[i].reg == dots[k].dest)
						return false;
				}
			}
		}

		bool vecReady = (vec[0].reg & 3) == 0;
		for (int i = 0; i < terms; i++)
			vecReady = vecReady && vec[i].reg == vec[0].reg + i && !vec[i].negate;

		// All of them at lane k of one Vec4 can go straight there.
		const int destVec = dots[0].dest - laneOf[0];
		bool destDirect = (destVec & 3) == 0 && (destScratch || count == 4);
		for (int j = 0; j < count; j++)
			destDirect = destDirect && dots[j].dest == destVec + laneOf[j];

		const int temps[] = { IRVTEMP_0, js.prefixD == 0 ? IRVTEMP_PFX_D : -1, IRVTEMP_PFX_T, IRVTEMP_PFX_S };
		auto tempFree = [&](int temp, int other) {
			if (temp < 0 || temp == avoid || temp == other || DotLanesRead(vec, temp))
				return false;
			for (int j = 0; j < count; j++) {
				if (DotLanesRead(blockLanes(j), temp) || (dots[j].dest >= temp && dots[j].dest < temp + 4))
					return false;
			}
			return true;
		};
		int vecTemp = -1, destTemp = -1;
		if (!vecReady) {
			for (int temp : temps) {
				if (tempFree(temp, -1)) {
					vecTemp = temp;
					break;
				}
			}
			if (vecTemp < 0)
				return false;
		}
		if (!destDirect) {
			for (int temp : temps) {
				if (tempFree(temp, vecTemp)) {
					destTemp = temp;
					break;
				}
			}
			if (destTemp < 0)
				return false;
		}

		if (!vecReady) {
			for (int i = 0; i < terms; i++) {
				if (vec[i].reg < 0)
					ir.WriteSetConstantFloat(vecTemp + i, vec[i].constant);
				else if (vec[i].negate)
					ir.Write(IROp::FNeg, vecTemp + i, vec[i].reg);
				else
					ir.Write(IROp::FMov, vecTemp + i, vec[i].reg);
			}
		}
		const int out = destDirect ? destVec : destTemp;
		ir.Write(IROp::Vec4VfpuMultiDot, out, base, vecReady ? vec[0].reg : vecTemp, terms | (transposed ? IRMULTIDOT_TRANSPOSED : 0));
		if (!destDirect) {
			for (int j = 0; j < count; j++)
				ir.Write(IROp::FMov, dots[j].dest, destTemp + laneOf[j]);
		}
		return true;
	}

	// True if the prefix only touches lanes the op has. A position past the size may only be the
	// identity, and a position within it may not name a lane past it (which zeroes the result lane
	// on hardware, see cpu/vfpu/prefix_ctrl - the interpreter handles that).
	static bool IsPrefixWithinSize(u32 prefix, VectorSize sz) {
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < 4; i++) {
			int regnum = VFPUPrefixSwizzle(prefix, i);
			bool abs = VFPUPrefixAbs(prefix, i);
			bool negate = VFPUPrefixNegate(prefix, i);
			bool constants = VFPUPrefixIsConstant(prefix, i);
			if (constants) {
				continue;
			}
			if (i >= n) {
				if (abs || negate || regnum != i)
					return false;
			} else if (regnum >= n) {
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
				int regnum = VFPUPrefixSwizzle(prefix, i);
				bool abs = VFPUPrefixAbs(prefix, i);
				bool negate = VFPUPrefixNegate(prefix, i);
				bool constants = VFPUPrefixIsConstant(prefix, i);

				if (!constants && regnum == i && !abs && !negate)
					continue;
				if (constants && regnum == 0 && abs == 0 && !negate) {
					zeroedLanes |= 1 << i;
					continue;
				}

				// Nope, it has something else going on.
				zeroedLanes = (u32)-1;
				break;
			}

			if (zeroedLanes != (u32)-1) {
				InitRegs(vregs, tempReg);
				ir.Write(IROp::Vec4Init, vregs[0], (int)Vec4Init::AllZERO);
				ir.Write(IROp::Vec4Blend, vregs[0], origV[0], vregs[0], zeroedLanes);
				return;
			}
		}

		// Alright, fall back to the generic approach.
		for (int i = 0; i < n; i++) {
			int regnum = VFPUPrefixSwizzle(prefix, i);
			bool abs = VFPUPrefixAbs(prefix, i);
			bool negate = VFPUPrefixNegate(prefix, i);
			bool constants = VFPUPrefixIsConstant(prefix, i);

			// Unchanged, hurray.
			if (!constants && regnum == i && !abs && !negate)
				continue;

			// This puts the value into a temp reg, so we won't write the modified value back.
			vregs[i] = tempReg + i;
			if (!constants) {
				if (regnum >= n) {
					// Depends on the op, but often zero.
					ir.WriteSetConstantFloat(vregs[i], 0.0f);
				} else if (abs) {
					// Could have a FNAbs op, but probably not worth it.
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
				ir.WriteSetConstantFloat(vregs[i], VFPUPrefixConstant(prefix, i));
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
			int sat = VFPUPrefixSat(js.prefixD, i);
			if (sat == 1) {
				// clamped = x < 0 ? (x > 1 ? 1 : x) : x [0, 1]
				ir.Write(IROp::FSat0_1, vregs[i], vregs[i]);
			} else if (sat == 3) {
				ir.Write(IROp::FSatMinus1_1, vregs[i], vregs[i]);
			}
		}
	}

	void IRFrontend::ApplyPrefixDMask(u8 *vregs, VectorSize sz, int vectorReg) {
		u8 origV[4];
		GetVectorRegs(origV, sz, vectorReg);
		// vregs can be all temps (so a Vec4) without the destination being one, if every lane is masked.
		if (IsVec4(sz, vregs) && IsVec4(sz, origV) && js.VfpuWriteMask() != 0 && opts.preferVec4) {
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

		if (js.kernelMode) {
			// Send all memory accesses to the interpreter in kernel mode.
			// TODO: Do something faster - but it hardly matters, currently this is VSH-only.
			DISABLE;
			return;
		}

		switch (op >> 26) {
		case 50: //lv.s
			ir.Write(IROp::LoadFloat, vfpuBase + voffset[vt], rs, 0, offset);
			break;

		case 58: //sv.s
			ir.Write(IROp::StoreFloat, vfpuBase + voffset[vt], rs, 0, offset);
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

		if (js.kernelMode) {
			// Send all memory accesses to the interpreter in kernel mode.
			// TODO: Do something faster - but it hardly matters, currently this is VSH-only.
			DISABLE;
			return;
		}

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
				ir.Write(IROp::LoadVec4, vregs[0], rs, 0, imm);
			} else {
				// Fall back to individual reads for vertical vector stores.
				if (!g_Config.bFastMemory)
					ir.Write(IROp::ValidateAddress128, 0, (u8)rs, 0, (u32)imm);
				ir.Write(IROp::LoadFloat, vregs[0], rs, 0, imm);
				ir.Write(IROp::LoadFloat, vregs[1], rs, 0, imm + 4);
				ir.Write(IROp::LoadFloat, vregs[2], rs, 0, imm + 8);
				ir.Write(IROp::LoadFloat, vregs[3], rs, 0, imm + 12);
			}
			break;

		case LSVType::SVQ:
			if (IsVec4(V_Quad, vregs)) {
				ir.Write(IROp::StoreVec4, vregs[0], rs, 0, imm);
			} else {
				// Fall back to individual writes for vertical vector stores.
				if (!g_Config.bFastMemory)
					ir.Write(IROp::ValidateAddress128, 0, (u8)rs, 1, (u32)imm);
				ir.Write(IROp::StoreFloat, vregs[0], rs, 0, imm);
				ir.Write(IROp::StoreFloat, vregs[1], rs, 0, imm + 4);
				ir.Write(IROp::StoreFloat, vregs[2], rs, 0, imm + 8);
				ir.Write(IROp::StoreFloat, vregs[3], rs, 0, imm + 12);
			}
			break;

		case LSVType::LVLQ:
		case LSVType::LVRQ:
		case LSVType::SVLQ:
		case LSVType::SVRQ:
			// These are pretty uncommon unless paired (which would be caught above).
			// Fall back to interpreter.
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
				ir.WriteSetConstantFloat(dregs[i], type == 6 ? 0.0f : 1.0f);
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
				ir.WriteSetConstantFloat(dregs[0], (vd & 1) == 0 ? 1.0f : 0.0f);
				ir.WriteSetConstantFloat(dregs[1], (vd & 1) == 1 ? 1.0f : 0.0f);
				break;
			case V_Quad:
				ir.WriteSetConstantFloat(dregs[0], (vd & 3) == 0 ? 1.0f : 0.0f);
				ir.WriteSetConstantFloat(dregs[1], (vd & 3) == 1 ? 1.0f : 0.0f);
				ir.WriteSetConstantFloat(dregs[2], (vd & 3) == 2 ? 1.0f : 0.0f);
				ir.WriteSetConstantFloat(dregs[3], (vd & 3) == 3 ? 1.0f : 0.0f);
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
		u8 dregs[16]{};
		GetMatrixRegs(dregs, sz, vd);
		// The constants are symmetric, so a transposed matrix comes out as Vec4s taken in transposed
		// order. (Flipping the transposed bit instead names other registers for a matrix with an
		// offset, which wraps around.)
		if (!IsMatrixVec4(sz, dregs)) {
			u8 dt[16];
			for (int k = 0; k < 16; k++)
				dt[k] = dregs[(k % 4) * 4 + k / 4];
			if (IsMatrixVec4(sz, dt))
				memcpy(dregs, dt, sizeof(dt));
		}
		if (!IsMatrixVec4(sz, dregs)) {
			// 3x3 is decently common.  It expands a lot, but let's set each.
			// TODO: It might be worth using Vec4Blend for 3x3 to mask w.
			int n = GetMatrixSide(sz);
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) {
					switch ((op >> 16) & 0xF) {
					case 3: // vmidt
						if (x == 0 && y == 0)
							ir.WriteSetConstantFloat(dregs[y * 4 + x], 1.0f);
						else if (x == y)
							ir.Write(IROp::FMov, dregs[y * 4 + x], dregs[0]);
						else
							ir.WriteSetConstantFloat(dregs[y * 4 + x], 0.0f);
						break;
					case 6: // vmzero
						// Likely to be fast.
						ir.WriteSetConstantFloat(dregs[y * 4 + x], 0.0f);
						break;
					case 7: // vmone
						if (x == 0 && y == 0)
							ir.WriteSetConstantFloat(dregs[y * 4 + x], 1.0f);
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

		// Each Vec4 of dregs in turn (lane i of the i-th is the diagonal).
		for (int i = 0; i < 4; i++) {
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
			ir.Write(IROp::Vec4Init, dregs[i * 4], (int)init);
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

		// s's last lane is 1, so t's is added in.
		DotLane sl[4], tl[4];
		for (int i = 0; i < 4; i++) {
			sl[i] = i < n - 1 ? DotReg(sregs[i]) : DotConst(i == n - 1 ? 1.0f : 0.0f);
			tl[i] = i < n ? DotReg(tregs[i]) : DotConst(0.0f);
		}
		u8 sv, tv;
		if (!BuildDotOperands(sl, tl, -1, &sv, &tv))
			DISABLE;
		ir.Write(IROp::Vec4VfpuDot, dregs[0], sv, tv);
		ApplyPrefixD(dregs, V_Single, vd);
	}

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

		// Dot products with constants from the T prefix in every lane: ones for vfad, 1/n for
		// vavg (0 for vavg.s), as in the interpreter. An S constant past n is added in too
		// (cpu/vfpu/prefix_lanes), which IsPrefixWithinSize() lets through.
		const bool vavg = ((op >> 16) & 31) == 7;
		static const float vavgConst[4] = { 0.0f, 0.5f, 1.0f / 3.0f, 0.25f };
		const float c = vavg ? vavgConst[n - 1] : 1.0f;
		DotLane sl[4], tl[4];
		for (int i = 0; i < 4; i++) {
			const bool constantLane = i >= n && VFPUPrefixIsConstant(js.prefixS, i);
			sl[i] = i < n ? DotReg(sregs[i]) : DotConst(constantLane ? VFPUPrefixConstant(js.prefixS, i) : 0.0f);
			tl[i] = DotConst(c);
		}
		u8 sv, tv;
		if (!BuildDotOperands(sl, tl, -1, &sv, &tv))
			DISABLE;
		ir.Write(IROp::Vec4VfpuDot, dregs[0], sv, tv);
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

		// On the PSP, lanes past n where both prefixes are constants add their product, and the rest
		// read zero (cpu/vfpu/prefix_lanes). IsPrefixWithinSize() lets those through.
		auto constantLane = [&](int i) {
			return i >= n && VFPUPrefixIsConstant(js.prefixS, i) && VFPUPrefixIsConstant(js.prefixT, i);
		};

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		// A run of vdots against one vector (a matrix times a vector, written out) goes as one
		// Vec4VfpuMultiDot, eating the rest of the run.
		if (opts.vfpuMultiDot && js.HasNoPrefix() && !js.inDelaySlot && n >= 2) {
			MultiDot dots[4];
			int count = 0;
			for (; count < 4; count++) {
				const MIPSOpcode next = count == 0 ? op : GetOffsetInstruction(count);
				if ((next.encoding & 0xFF808080) != (op.encoding & 0xFF808080))
					break;
				u8 s[4], t[4], d[1];
				GetVectorRegs(s, sz, (next.encoding >> 8) & 0x7F);
				GetVectorRegs(t, sz, (next.encoding >> 16) & 0x7F);
				GetVectorRegs(d, V_Single, next.encoding & 0x7F);
				for (int i = 0; i < 4; i++) {
					dots[count].a[i] = i < n ? DotReg(s[i]) : DotConst(0.0f);
					dots[count].b[i] = i < n ? DotReg(t[i]) : DotConst(0.0f);
				}
				dots[count].dest = d[0];
			}
			for (int c = count; c >= 2; c--) {
				if (TryMultiDot(dots, c, n, -1, false)) {
					for (int k = 1; k < c; k++)
						EatInstruction(GetOffsetInstruction(1));
					return;
				}
			}
		}

		DotLane sl[4], tl[4];
		for (int i = 0; i < 4; i++) {
			sl[i] = i < n ? DotReg(sregs[i]) : DotConst(constantLane(i) ? VFPUPrefixConstant(js.prefixS, i) : 0.0f);
			tl[i] = i < n ? DotReg(tregs[i]) : DotConst(constantLane(i) ? VFPUPrefixConstant(js.prefixT, i) : 0.0f);
		}
		u8 sv, tv;
		if (!BuildDotOperands(sl, tl, -1, &sv, &tv))
			DISABLE;
		ir.Write(IROp::Vec4VfpuDot, dregs[0], sv, tv);
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
			allowSIMD = opts.moreVec4Ops;
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
			case VecDo3Op::VMIN:
				opFunc = IROp::Vec4Min;
				break;
			case VecDo3Op::VMAX:
				opFunc = IROp::Vec4Max;
				break;
			case VecDo3Op::VSLT:
				opFunc = IROp::Vec4Slt;
				break;
			case VecDo3Op::VSGE:
				opFunc = IROp::Vec4Sge;
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
				ir.Write(IROp::XorConst, IRTEMP_1, IRTEMP_1, 0, 1);
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
			ir.Write(IROp::FpCondFromReg, 0, IRTEMP_0);
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
				ir.Write(IROp::FExp2, tempregs[i], sregs[i]);
				break;
			case 21: // d[i] = logf(s[i])/log(2.0f); break; //vlog2
				ir.Write(IROp::FLog2, tempregs[i], sregs[i]);
				break;
			case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
				ir.Write(IROp::FVSqrt, tempregs[i], sregs[i]);
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
				// exp2(-x), as vfpu_rexp2 computes it.
				ir.Write(IROp::FNeg, tempregs[i], sregs[i]);
				ir.Write(IROp::FExp2, tempregs[i], tempregs[i]);
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

		u8 sregs[4], dregs[4], outregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);
		if (opts.moreVec4Ops && IsVec4(sz, dregs) && IsVec4(sz, sregs)) {
			ir.Write(IROp::Vec4CvtScaledSW, dregs[0], sregs[0], imm);
			ApplyPrefixD(dregs, sz, _VD);
			return;
		}
		// Through temps if a lane would be written before a later one reads it.
		const bool direct = IsOverlapSafeAllowS(n, dregs, n, sregs);
		for (int i = 0; i < n; i++)
			outregs[i] = direct ? dregs[i] : IRVTEMP_0 + i;

		for (int i = 0; i < n; i++) {
			if (imm == 0)
				ir.Write(IROp::FCvtSW, outregs[i], sregs[i]);
			else
				ir.Write(IROp::FCvtScaledSW, outregs[i], sregs[i], imm);
		}
		for (int i = 0; i < n && !direct; i++)
			ir.Write(IROp::FMov, dregs[i], outregs[i]);
		ApplyPrefixD(dregs, sz, _VD);
	}

	void IRFrontend::Comp_Vh2f(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix() || !IsPrefixWithinSize(js.prefixS, op)) {
			DISABLE;
		}

		// Vector expand half to float
		// d[N*2] = float(lowerhalf(s[N])), d[N*2+1] = float(upperhalf(s[N]))
		// Sizes above pair act like pair.
		VectorSize sz = GetVecSize(op);
		VectorSize outsize = sz == V_Single ? V_Pair : V_Quad;
		int nOut = GetNumVectorElements(outsize);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);

		// Through temps, since d may overlap s.
		for (int i = 0; i < nOut; i++) {
			ir.Write(IROp::FHalfToFloat, IRVTEMP_0 + i, sregs[i / 2], i & 1);
		}
		for (int i = 0; i < nOut; i++) {
			ir.Write(IROp::FMov, dregs[i], IRVTEMP_0 + i);
		}

		ApplyPrefixD(dregs, outsize, _VD);
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

		u8 sregs[4], dregs[4], outregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);
		// Through temps if a lane would be written before a later one reads it.
		const bool direct = IsOverlapSafeAllowS(n, dregs, n, sregs);
		for (int i = 0; i < n; i++)
			outregs[i] = direct ? dregs[i] : IRVTEMP_0 + i;

		// Same values as FCR31.
		uint8_t rmode = (op >> 21) & 3;
		if (((op >> 21) & 0x1C) != 0x10)
			INVALIDOP;

		if (imm != 0) {
			for (int i = 0; i < n; i++)
				ir.Write(IROp::FCvtScaledWS, outregs[i], sregs[i], imm | (rmode << 6));
		} else {
			for (int i = 0; i < n; i++) {
				switch (IRRoundMode(rmode)) {
				case IRRoundMode::RINT_0: // vf2in
					ir.Write(IROp::FRound, outregs[i], sregs[i]);
					break;

				case IRRoundMode::CAST_1: // vf2iz
					ir.Write(IROp::FTrunc, outregs[i], sregs[i]);
					break;

				case IRRoundMode::CEIL_2: // vf2iu
					ir.Write(IROp::FCeil, outregs[i], sregs[i]);
					break;

				case IRRoundMode::FLOOR_3: // vf2id
					ir.Write(IROp::FFloor, outregs[i], sregs[i]);
					break;

				default:
					INVALIDOP;
				}
			}
		}
		for (int i = 0; i < n && !direct; i++)
			ir.Write(IROp::FMov, dregs[i], outregs[i]);

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
					u32 setBits = GetVFPUCtrlSetBits(imm - 128);
					if (mask != 0xFFFFFFFF || setBits != 0) {
						ir.Write(IROp::AndConst, IRTEMP_0, rt, 0, mask);
						if (setBits != 0)
							ir.Write(IROp::OrConst, IRTEMP_0, IRTEMP_0, 0, setBits);
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
			switch (imm) {
			case VFPU_CTRL_DPREFIX:
			case VFPU_CTRL_SPREFIX:
			case VFPU_CTRL_TPREFIX:
				// In case we have a saved prefix.
				FlushPrefixV();
				break;
			}
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
				u32 setBits = GetVFPUCtrlSetBits(imm);
				if (mask != 0xFFFFFFFF || setBits != 0) {
					ir.Write(IROp::FMovToGPR, IRTEMP_0, vfpuBase + voffset[vs]);
					ir.Write(IROp::AndConst, IRTEMP_0, IRTEMP_0, 0, mask);
					if (setBits != 0)
						ir.Write(IROp::OrConst, IRTEMP_0, IRTEMP_0, 0, setBits);
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
		const bool bothVec4 = IsMatrixVec4(sz, sregs) && IsMatrixVec4(sz, dregs);
		const bool bothTransposedVec4 = IsMatrixTransposedVec4(sz, sregs) && IsMatrixTransposedVec4(sz, dregs);
		if (bothVec4 || bothTransposedVec4) {
			// The Vec4s start at every fourth reg, or for the transposed layout at the first four.
			// (Transposing the register names instead goes wrong when the rows wrap around, like E020.)
			for (int i = 0; i < 4; i++) {
				int start = bothVec4 ? i * 4 : i;
				ir.Write(IROp::Vec4Mov, dregs[start], sregs[start]);
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
		int n = GetMatrixSide(sz);

		u8 sregs[16]{}, dregs[16]{}, tregs[1];
		GetMatrixRegs(sregs, sz, vs);
		GetMatrixRegs(dregs, sz, vd);
		GetVectorRegs(tregs, V_Single, vt);

		// Every element is scaled the same, so the elements can go in any order, as long as each
		// source keeps its destination. Transposed matrices come out as Vec4s when both are taken
		// in transposed order. (Flipping the transposed bit of the registers instead would name other
		// registers for a matrix with an offset, which wraps around.)
		if (!IsMatrixVec4(sz, sregs) || !IsMatrixVec4(sz, dregs)) {
			u8 st[16], dt[16];
			for (int k = 0; k < 16; k++) {
				st[k] = sregs[(k % 4) * 4 + k / 4];
				dt[k] = dregs[(k % 4) * 4 + k / 4];
			}
			if (IsMatrixVec4(sz, st) && IsMatrixVec4(sz, dt)) {
				memcpy(sregs, st, sizeof(st));
				memcpy(dregs, dt, sizeof(dt));
			}
		}

		// Either way, a source that partly overlaps the destination would be read after it's written.
		if (vs != vd && GetMatrixOverlap(vs, vd, sz) != OVERLAP_NONE) {
			DISABLE;
		}

		if (GetMtx(vt) != GetMtx(vd) && IsMatrixVec4(sz, sregs) && IsMatrixVec4(sz, dregs)) {
			for (int i = 0; i < n; ++i) {
				ir.Write(IROp::Vec4Scale, dregs[i * 4], sregs[i * 4], tregs[0]);
			}
			return;
		}

		// Element by element otherwise, which works for any size and transposition.
		// The scale can be part of the destination, so keep a copy.
		ir.Write(IROp::FMov, IRVTEMP_0, tregs[0]);
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				ir.Write(IROp::FMul, dregs[a * 4 + b], sregs[a * 4 + b], IRVTEMP_0);
			}
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

	void IRFrontend::Comp_Vmmul(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMMUL);
		if (!js.HasNoPrefix()) {
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

		// d[a][b] = s[b] dot t[a], each a row of n lanes and zeros, as in the interpreter. With
		// no overlap, the results can go straight to d.
		if (soverlap || toverlap)
			DISABLE;
		u8 sregs[16], tregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, vs);
		GetMatrixRegs(tregs, sz, vt);
		GetMatrixRegs(dregs, sz, vd);
		for (int a = 0; a < n; a++) {
			DotLane tl[4];
			for (int c = 0; c < 4; c++)
				tl[c] = c < n ? DotReg(tregs[a * 4 + c]) : DotConst(0.0f);
			MultiDot dots[4];
			for (int b = 0; b < n; b++) {
				for (int c = 0; c < 4; c++) {
					dots[b].a[c] = c < n ? DotReg(sregs[b * 4 + c]) : DotConst(0.0f);
					dots[b].b[c] = tl[c];
				}
				dots[b].dest = dregs[a * 4 + b];
			}
			if (TryMultiDot(dots, n, n, -1, false))
				continue;
			for (int b = 0; b < n; b++) {
				DotLane sl[4];
				for (int c = 0; c < 4; c++)
					sl[c] = c < n ? DotReg(sregs[b * 4 + c]) : DotConst(0.0f);
				u8 sv, tv;
				if (!BuildDotOperands(sl, tl, -1, &sv, &tv))
					DISABLE;
				// Built once per row of t.
				for (int c = 0; c < 4; c++)
					tl[c] = DotReg(tv + c);
				ir.Write(IROp::Vec4VfpuDot, dregs[a * 4 + b], sv, tv);
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

		// A dot per row, against t with the lanes past it zero, and lane ins 1 for the
		// homogenous form. The results go to the D temp first, since d may overlap s or t.
		const int tn = homogenous ? n - 1 : n;
		DotLane tl[4];
		for (int i = 0; i < 4; i++)
			tl[i] = i < tn ? DotReg(tregs[i]) : DotConst(homogenous && i == ins ? 1.0f : 0.0f);
		MultiDot dots[4];
		for (int i = 0; i < n; i++) {
			for (int k = 0; k < 4; k++) {
				dots[i].a[k] = k < n ? DotReg(sregs[i * 4 + k]) : DotConst(0.0f);
				dots[i].b[k] = tl[k];
			}
			dots[i].dest = IRVTEMP_PFX_D + i;
		}
		const bool multi = TryMultiDot(dots, n, n, IRVTEMP_PFX_D, true);
		for (int i = 0; i < n && !multi; i++) {
			DotLane sl[4];
			for (int k = 0; k < 4; k++)
				sl[k] = k < n ? DotReg(sregs[i * 4 + k]) : DotConst(0.0f);
			u8 sv, tv;
			if (!BuildDotOperands(sl, tl, IRVTEMP_PFX_D, &sv, &tv))
				DISABLE;
			// Built once, t is a Vec4 for the rest.
			for (int k = 0; k < 4; k++)
				tl[k] = DotReg(tv + k);
			ir.Write(IROp::Vec4VfpuDot, IRVTEMP_PFX_D + i, sv, tv);
		}
		for (int i = 0; i < n; i++)
			ir.Write(IROp::FMov, dregs[i], IRVTEMP_PFX_D + i);
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

		// The T prefix's swizzle is forced to yx: s0 * t1 + (-s1) * t0.
		const DotLane sl[4] = { DotReg(sregs[0]), DotReg(sregs[1], true), DotConst(0.0f), DotConst(0.0f) };
		const DotLane tl[4] = { DotReg(tregs[1]), DotReg(tregs[0]), DotConst(0.0f), DotConst(0.0f) };
		u8 sv, tv;
		if (!BuildDotOperands(sl, tl, -1, &sv, &tv))
			DISABLE;
		ir.Write(IROp::Vec4VfpuDot, dregs[0], sv, tv);
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

		// The first output may be a source of the second (vi2s.q C002, C000), so pack into temps
		// then. No S prefix here, so its temps are free.
		if (!IsOverlapSafe(nOut, dregs, GetNumVectorElements(sz), sregs)) {
			for (int i = 0; i < nOut; i++) {
				tempregs[i] = IRVTEMP_PFX_S + i;
			}
		}

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
				// Output is only one register. The pack clamps negative lanes to zero.
				ir.Write(IROp::Vec4Pack31To8, tempregs[0], srcregs[0]);
			} else {  //vi2c
				ir.Write(IROp::Vec4Pack32To8, tempregs[0], srcregs[0]);
			}
		} else {
			// bits == 16
			if (unsignedOp) {  //vi2us
				// Output is only one register.
				// The pack clamps negative lanes to zero.
				ir.Write(IROp::Vec2Pack31To16, tempregs[0], srcregs[0]);
				if (outsize == V_Pair) {
					ir.Write(IROp::Vec2Pack31To16, tempregs[1], srcregs[2]);
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
		// vc2i and vuc2i read one lane, the first of the source at the size encoded, which decides
		// which register that is.
		int sourceReg = -1;
		if (bits == 8) {
			outsize = V_Quad;
			u8 full[4];
			GetVectorRegs(full, sz, _VS);
			sourceReg = full[0];
			sz = V_Single;
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
		if (sourceReg >= 0)
			sregs[0] = sourceReg;
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

		if (sz != V_Triple && sz != V_Quad) {
			INVALIDOP;
		}

		// The dots the interpreter does, against permuted and negated copies of t. The results
		// go to the D temp first, since d may overlap s or t.
		const DotLane zero = DotConst(0.0f);
		auto tl = [&](int i, bool neg = false) { return i < n ? DotReg(tregs[i], neg) : zero; };
		DotLane tDots[4][4];
		int dots;
		if (sz == V_Triple) {
			const DotLane t0[4] = { zero, tl(2), tl(1, true), zero };
			const DotLane t1[4] = { tl(2, true), zero, tl(0), zero };
			const DotLane t2[4] = { tl(1), tl(0, true), zero, tl(2) };
			memcpy(tDots[0], t0, sizeof(t0));
			memcpy(tDots[1], t1, sizeof(t1));
			memcpy(tDots[2], t2, sizeof(t2));
			dots = 3;
		} else {
			const DotLane t0[4] = { tl(3), tl(2), tl(1, true), tl(0) };
			const DotLane t1[4] = { tl(2, true), tl(3), tl(0), tl(1) };
			const DotLane t2[4] = { tl(1), tl(0, true), tl(3), tl(2) };
			const DotLane t3[4] = { tl(0, true), tl(1, true), tl(2, true), tl(3) };
			memcpy(tDots[0], t0, sizeof(t0));
			memcpy(tDots[1], t1, sizeof(t1));
			memcpy(tDots[2], t2, sizeof(t2));
			memcpy(tDots[3], t3, sizeof(t3));
			dots = 4;
		}
		DotLane sl[4];
		for (int i = 0; i < 4; i++)
			sl[i] = i < n ? DotReg(sregs[i]) : zero;
		for (int i = 0; i < dots; i++) {
			u8 sv, tv;
			if (!BuildDotOperands(sl, tDots[i], IRVTEMP_PFX_D, &sv, &tv))
				DISABLE;
			// Built once, s is a Vec4 for the rest.
			for (int j = 0; j < 4; j++)
				sl[j] = DotReg(sv + j);
			ir.Write(IROp::Vec4VfpuDot, IRVTEMP_PFX_D + i, sv, tv);
		}
		for (int i = 0; i < dots; i++)
			ir.Write(IROp::FMov, dregs[i], IRVTEMP_PFX_D + i);
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
		// imm3 == 6: d[N] = VFPU_CC[N] == tf ? s[N] : d[N]
		// otherwise: d[N] = VFPU_CC[imm3] == tf ? s[N] : d[N]

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
		// A D prefix that saturates puts the result in temps first. A lane that isn't moved keeps
		// d, so they start as d.
		u8 realregs[4];
		GetVectorRegs(realregs, sz, _VD);
		for (int i = 0; i < n; ++i) {
			if (dregs[i] != realregs[i])
				ir.Write(IROp::FMov, dregs[i], realregs[i]);
		}
		// 7 tests a CC bit that is always clear (cpu/vfpu/undefined).
		if (imm3 != 6) {
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
		ir.WriteSetConstantFloat(dreg, (float)imm);
		ApplyPrefixD(&dreg, V_Single, _VT);
	}

	void IRFrontend::Comp_Vfim(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix())
			DISABLE;

		// Vector half-float immediate
		// d[0] = float(imm)

		// Converted like vh2f: denormal halves are zero (cpu/vfpu/vfim).
		const float value = vfpu_h2f_float((u16)(op & 0xFFFF));

		u8 dreg;
		GetVectorRegsPrefixD(&dreg, V_Single, _VT);
		ir.WriteSetConstantFloat(dreg, value);
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
			ir.WriteSetConstantFloat(IRVTEMP_0, cst_constants[conNum]);
			ir.Write(IROp::Vec4Shuffle, dregs[0], IRVTEMP_0, 0);
		} else if (IsVec3of4(sz, dregs) && opts.preferVec4) {
			ir.WriteSetConstantFloat(IRVTEMP_0, cst_constants[conNum]);
			ir.Write(IROp::Vec4Shuffle, IRVTEMP_0, IRVTEMP_0, 0);
			ir.Write(IROp::Vec4Blend, dregs[0], dregs[0], IRVTEMP_0, 0x7);
		} else {
			for (int i = 0; i < n; i++) {
				// Most of the time, materializing a float is slower than copying from another float.
				if (i == 0)
					ir.WriteSetConstantFloat(dregs[i], cst_constants[conNum]);
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

		// With both a sine and a cosine lane and no overlap, one call computes both.
		bool hasSine = false, hasCosine = false;
		for (int i = 0; i < n; i++) {
			hasSine = hasSine || d[i] == 's';
			hasCosine = hasCosine || d[i] == 'c';
		}
		if (hasSine && hasCosine && IsOverlapSafe(n, dregs, 1, sreg)) {
			if (!broadcastSine && dregs[cosineLane] == dregs[sineLane] + 1 && (dregs[sineLane] & 1) == 0) {
				// The pair is in place (and aligned, as a Vec2), so write it directly.
				ir.Write(IROp::FSinCos, dregs[sineLane], sreg[0]);
				if (negSin)
					ir.Write(IROp::FNeg, dregs[sineLane], dregs[sineLane]);
				for (int i = 0; i < n; i++) {
					if (d[i] == '0')
						ir.WriteFC(IROp::SetConstF, dregs[i], 0, 0, 0.0f);
				}
				return;
			}
			ir.Write(IROp::FSinCos, IRVTEMP_0, sreg[0]);
			if (negSin)
				ir.Write(IROp::FNeg, IRVTEMP_0, IRVTEMP_0);
			for (int i = 0; i < n; i++) {
				if (d[i] == 's')
					ir.Write(IROp::FMov, dregs[i], IRVTEMP_0);
				else if (d[i] == 'c')
					ir.Write(IROp::FMov, dregs[i], IRVTEMP_0 + 1);
				else
					ir.WriteFC(IROp::SetConstF, dregs[i], 0, 0, 0.0f);
			}
			return;
		}

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
				ir.WriteFC(IROp::SetConstF, dregs[i], 0, 0, 0.0f);
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
				if (IsOverlapSafe(n, dregs, 1, sreg)) {
					ir.Write(IROp::FCos, dregs[i], sreg[0]);
				} else {
					// The cosine is taken of what the source lane got: the sine, or zero.
					int srcLane = 0;
					while (dregs[srcLane] != sreg[0]) {
						srcLane++;
					}
					if (broadcastSine || srcLane == sineLane) {
						ir.Write(IROp::FCos, dregs[i], IRVTEMP_0);
					} else {
						ir.WriteFC(IROp::SetConstF, dregs[i], 0, 0, 1.0f);
					}
				}
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
		// Four 8888 colors from a quad, whatever the size. Each channel keeps its top bits.
		struct Channel {
			u8 srcShift;
			u8 bits;
			u8 destShift;
		};
		static const Channel c4444[] = { { 4, 4, 0 }, { 12, 4, 4 }, { 20, 4, 8 }, { 28, 4, 12 } };
		static const Channel c5551[] = { { 3, 5, 0 }, { 11, 5, 5 }, { 19, 5, 10 }, { 31, 1, 15 } };
		static const Channel c5650[] = { { 3, 5, 0 }, { 10, 6, 5 }, { 19, 5, 11 } };
		const Channel *channels;
		int numChannels;
		switch ((op >> 16) & 3) {
		case 1: channels = c4444; numChannels = 4; break;
		case 2: channels = c5551; numChannels = 4; break;
		case 3: channels = c5650; numChannels = 3; break;
		default: INVALIDOP;
		}

		VectorSize outsize = GetVecSize(op) == V_Single ? V_Single : V_Pair;
		int nOut = GetNumVectorElements(outsize);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, V_Quad, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);

		// Through temps, since d may overlap s.
		for (int w = 0; w < nOut; w++) {
			bool first = true;
			for (int k = 0; k < 2; k++) {
				ir.Write(IROp::FMovToGPR, IRTEMP_0, sregs[w * 2 + k]);
				for (int c = 0; c < numChannels; c++) {
					const Channel &ch = channels[c];
					ir.Write(IROp::ShrImm, IRTEMP_1, IRTEMP_0, ch.srcShift);
					if (ch.srcShift + ch.bits < 32)
						ir.Write(IROp::AndConst, IRTEMP_1, IRTEMP_1, 0, (1 << ch.bits) - 1);
					if (ch.destShift + k * 16 != 0)
						ir.Write(IROp::ShlImm, IRTEMP_1, IRTEMP_1, ch.destShift + k * 16);
					if (first) {
						ir.Write(IROp::Mov, IRTEMP_2, IRTEMP_1);
						first = false;
					} else {
						ir.Write(IROp::Or, IRTEMP_2, IRTEMP_2, IRTEMP_1);
					}
				}
			}
			ir.Write(IROp::FMovFromGPR, IRVTEMP_0 + w, IRTEMP_2);
		}
		for (int w = 0; w < nOut; w++) {
			ir.Write(IROp::FMov, dregs[w], IRVTEMP_0 + w);
		}

		ApplyPrefixD(dregs, outsize, _VD);
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
