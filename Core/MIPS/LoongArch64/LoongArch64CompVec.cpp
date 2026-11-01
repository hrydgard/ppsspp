// Copyright (c) 2023- PPSSPP Project.

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

#include <algorithm>
#include "Core/MemMap.h"
#include "Core/MIPS/LoongArch64/LoongArch64Jit.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"
#include "Core/MIPS/LoongArch64/LoongArch64VfpuDot.h"

// This file contains compilation for vector instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace LoongArch64Gen;
using namespace LoongArch64JitConstants;

static bool Overlap(IRReg r1, int l1, IRReg r2, int l2) {
	return r1 < r2 + l2 && r1 + l1 > r2;
}

void LoongArch64JitBackend::CompIR_VecAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Init:
		regs_.Map(inst);

		switch ((Vec4Init)inst.src1) {
		case Vec4Init::AllZERO:
			if (cpu_info.LOONGARCH_LSX)
				VREPLGR2VR_D(regs_.V(inst.dest), R_ZERO);
			else
				for (int i = 0; i < 4; ++i)
					MOVGR2FR_W(regs_.F(inst.dest + i), R_ZERO);
			break;

		case Vec4Init::AllONE:
			LI(SCRATCH1, 1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_W(regs_.V(inst.dest), SCRATCH1);
			} else {
				MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
				for (int i = 1; i < 4; ++i)
					FMOV_S(regs_.F(inst.dest + i), regs_.F(inst.dest));
			}
			break;

		case Vec4Init::AllMinusONE:
			LI(SCRATCH1, -1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_W(regs_.V(inst.dest), SCRATCH1);
			} else {
				MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
				for (int i = 1; i < 4; ++i)
					FMOV_S(regs_.F(inst.dest + i), regs_.F(inst.dest));
			}
			break;

		case Vec4Init::Set_1000:
			LI(SCRATCH1, 1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_D(regs_.V(inst.dest), R_ZERO);
				VINSGR2VR_W(regs_.V(inst.dest), SCRATCH1, 0);
			} else {
				for (int i = 0; i < 4; ++i) {
					if (i == 0) {
						MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
					} else {
						MOVGR2FR_W(regs_.F(inst.dest + i), R_ZERO);
					}
				}
			}
			break;

		case Vec4Init::Set_0100:
			LI(SCRATCH1, 1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_D(regs_.V(inst.dest), R_ZERO);
				VINSGR2VR_W(regs_.V(inst.dest), SCRATCH1, 1);
			} else {
				for (int i = 0; i < 4; ++i) {
					if (i == 1) {
						MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
					} else {
						MOVGR2FR_W(regs_.F(inst.dest + i), R_ZERO);
					}
				}
			}
			break;

		case Vec4Init::Set_0010:
			LI(SCRATCH1, 1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_D(regs_.V(inst.dest), R_ZERO);
				VINSGR2VR_W(regs_.V(inst.dest), SCRATCH1, 2);
			} else {
				for (int i = 0; i < 4; ++i) {
					if (i == 2) {
						MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
					} else {
						MOVGR2FR_W(regs_.F(inst.dest + i), R_ZERO);
					}
				}
			}
			break;

		case Vec4Init::Set_0001:
			LI(SCRATCH1, 1.0f);
			if (cpu_info.LOONGARCH_LSX) {
				VREPLGR2VR_D(regs_.V(inst.dest), R_ZERO);
				VINSGR2VR_W(regs_.V(inst.dest), SCRATCH1, 3);
			} else {
				for (int i = 0; i < 4; ++i) {
					if (i == 3) {
						MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
					} else {
						MOVGR2FR_W(regs_.F(inst.dest + i), R_ZERO);
					}
				}
			}
			break;
		}
		break;

	case IROp::Vec4Shuffle:
		if (cpu_info.LOONGARCH_LSX) {
			regs_.Map(inst);
			if (regs_.GetFPRLaneCount(inst.src1) == 1 && (inst.src1 & 3) == 0 && inst.src2 == 0) {
				// This is a broadcast.  If dest == src1, this won't clear it.
				regs_.SpillLockFPR(inst.src1);
				regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
			} else {
				regs_.Map(inst);
			}

			VSHUF4I_W(regs_.V(inst.dest), regs_.V(inst.src1), inst.src2);
		} else {
			if (inst.dest == inst.src1) {
				regs_.Map(inst);
				// Try to find the least swaps needed to move in place, never worse than 6 FMOVs.
				// Would be better with a vmerge and vector regs.
				int state[4]{ 0, 1, 2, 3 };
				int goal[4]{ (inst.src2 >> 0) & 3, (inst.src2 >> 2) & 3, (inst.src2 >> 4) & 3, (inst.src2 >> 6) & 3 };

				static constexpr int NOT_FOUND = 4;
				auto findIndex = [](int *arr, int val, int start = 0) {
					return (int)(std::find(arr + start, arr + 4, val) - arr);
				};
				auto moveChained = [&](const std::vector<int> &lanes, bool rotate) {
					int firstState = state[lanes.front()];
					if (rotate)
						FMOV_S(SCRATCHF1, regs_.F(inst.dest + lanes.front()));
					for (size_t i = 1; i < lanes.size(); ++i) {
						FMOV_S(regs_.F(inst.dest + lanes[i - 1]), regs_.F(inst.dest + lanes[i]));
						state[lanes[i - 1]] = state[lanes[i]];
					}
					if (rotate) {
						FMOV_S(regs_.F(inst.dest + lanes.back()), SCRATCHF1);
						state[lanes.back()] = firstState;
					}
				};

				for (int i = 0; i < 4; ++i) {
					// Overlap, so if they match, nothing to do.
					if (goal[i] == state[i])
						continue;

					int neededBy = findIndex(goal, state[i], i + 1);
					int foundIn = findIndex(state, goal[i], 0);
					_assert_(foundIn != NOT_FOUND);

					if (neededBy == NOT_FOUND || neededBy == foundIn) {
						moveChained({ i, foundIn }, neededBy == foundIn);
						continue;
					}

					// Maybe we can avoid a swap and move the next thing into place.
					int neededByDepth2 = findIndex(goal, state[neededBy], i + 1);
					if (neededByDepth2 == NOT_FOUND || neededByDepth2 == foundIn) {
						moveChained({ neededBy, i, foundIn }, neededByDepth2 == foundIn);
						continue;
					}

					// Since we only have 4 items, this is as deep as the chain could go.
					int neededByDepth3 = findIndex(goal, state[neededByDepth2], i + 1);
					moveChained({ neededByDepth2, neededBy, i, foundIn }, neededByDepth3 == foundIn);
				}
			} else {
				regs_.Map(inst);
				for (int i = 0; i < 4; ++i) {
					int lane = Vec4ShuffleLane(inst.src2, i);
					FMOV_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + lane));
				}
			}
		}
		break;

	case IROp::Vec4Blend:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			IRReg src = inst.src1;
			uint8_t imm = inst.constant;
			if (inst.dest == inst.src1) {
				src = inst.src2;
			} else if (inst.dest == inst.src2) {
				imm = ~imm;
			} else {
				VOR_V(regs_.V(inst.dest), regs_.V(src), regs_.V(src));
				src = inst.src2;
			}

			for (int i = 0; i < 4; ++i)
				if (imm & (1 << i))
					VEXTRINS_W(regs_.V(inst.dest), regs_.V(src), (i << 4) | i);
		} else {
			for (int i = 0; i < 4; ++i) {
				int which = (inst.constant >> i) & 1;
				IRReg srcReg = which ? inst.src2 : inst.src1;
				if (inst.dest != srcReg)
					FMOV_S(regs_.F(inst.dest + i), regs_.F(srcReg + i));
			}
		}
		break;

	case IROp::Vec4Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			if (cpu_info.LOONGARCH_LSX)
				VOR_V(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src1));
			else
				for (int i = 0; i < 4; ++i)
					FMOV_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Add:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VFADD_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
		else
			for (int i = 0; i < 4; ++i)
				FADD_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
		break;

	case IROp::Vec4Sub:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VFSUB_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
		else
			for (int i = 0; i < 4; ++i)
				FSUB_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
		break;

	case IROp::Vec4Mul:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VFMUL_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
		else
			for (int i = 0; i < 4; ++i)
				FMUL_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
		break;

	case IROp::Vec4Div:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VFDIV_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
		else
			for (int i = 0; i < 4; ++i)
				FDIV_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
		break;

	case IROp::Vec4Scale:
		// Check overlap before mapping: the reg cache can't map a reg as both a vector and a scalar.
		if (cpu_info.LOONGARCH_LSX && (Overlap(inst.dest, 4, inst.src2, 1) || Overlap(inst.src1, 4, inst.src2, 1)))
			DISABLE;
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {

			VSHUF4I_W(regs_.V(inst.src2), regs_.V(inst.src2), 0);
			VFMUL_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
		} else {
			if (Overlap(inst.src2, 1, inst.dest, 3)) {
				// We have to handle overlap, doing dest == src2 last.
				for (int i = 0; i < 4; ++i) {
					if (inst.src2 != inst.dest + i)
						FMUL_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2));
				}
				for (int i = 0; i < 4; ++i) {
					if (inst.src2 == inst.dest + i)
						FMUL_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2));
				}
			} else {
				for (int i = 0; i < 4; ++i)
					FMUL_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2));
			}
		}
		break;

	case IROp::Vec4Neg:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VBITREVI_W(regs_.V(inst.dest), regs_.V(inst.src1), 31);
		else
			for (int i = 0; i < 4; ++i)
				FNEG_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
		break;

	case IROp::Vec4Abs:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX)
			VBITCLRI_W(regs_.V(inst.dest), regs_.V(inst.src1), 31);
		else
			for (int i = 0; i < 4; ++i)
				FABS_S(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
		break;

	case IROp::Vec4Min:
	case IROp::Vec4Max:
		if (cpu_info.LOONGARCH_LSX) {
			// vfpu_minmax_key() of each lane of both, compared as signed, picks the lanes. A tie
			// takes src2.
			const LoongArch64Reg temp = EncodeRegToV(regs_.MapWithFPRTemp(inst));
			const LoongArch64Reg a = regs_.V(inst.src1), b = regs_.V(inst.src2);
			const LoongArch64Reg keyA = EncodeRegToV(SCRATCHF1), keyB = EncodeRegToV(SCRATCHF2);
			auto key = [&](LoongArch64Reg k, LoongArch64Reg src) {
				VSRAI_W(temp, src, 31);
				VSRLI_W(temp, temp, 1);
				VXOR_V(k, src, temp);
				// Zero if the exponent is.
				VSLLI_W(temp, src, 1);
				VSRLI_W(temp, temp, 24);
				VSEQI_W(temp, temp, 0);
				VANDN_V(k, temp, k);
			};
			key(keyA, a);
			key(keyB, b);
			if (inst.op == IROp::Vec4Min)
				VSLT_W(keyA, keyA, keyB);
			else
				VSLT_W(keyA, keyB, keyA);
			VBITSEL_V(regs_.V(inst.dest), b, a, keyA);
		} else {
			// As FMin and FMax, lane by lane. The lanes line up, so overlap doesn't matter.
			for (int i = 0; i < 4; ++i) {
				IRInst lane{ inst.op == IROp::Vec4Min ? IROp::FMin : IROp::FMax };
				lane.dest = inst.dest + i;
				lane.src1 = inst.src1 + i;
				lane.src2 = inst.src2 + i;
				CompIR_FCondAssign(lane);
			}
		}
		break;

	case IROp::Vec4Slt:
	case IROp::Vec4Sge:
		// 1.0 where src1 < src2 (vslt) or src1 >= src2 (vsge), 0.0 otherwise, NaN included.
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			if (inst.op == IROp::Vec4Slt)
				VFCMP_CLT_S(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), regs_.V(inst.src2));
			else
				VFCMP_CLE_S(EncodeRegToV(SCRATCHF1), regs_.V(inst.src2), regs_.V(inst.src1));
			LU12I_W(SCRATCH1, 0x3F800000 >> 12);
			VREPLGR2VR_W(EncodeRegToV(SCRATCHF2), SCRATCH1);
			VAND_V(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF2));
		} else {
			QuickFLI(32, SCRATCHF1, 1.0f, SCRATCH1);
			MOVGR2FR_W(SCRATCHF2, R_ZERO);
			for (int i = 0; i < 4; ++i) {
				if (inst.op == IROp::Vec4Slt)
					FCMP_COND_S(FCC0, regs_.F(inst.src1 + i), regs_.F(inst.src2 + i), LoongArch64Fcond::CLT);
				else
					FCMP_COND_S(FCC0, regs_.F(inst.src2 + i), regs_.F(inst.src1 + i), LoongArch64Fcond::CLE);
				FSEL(regs_.F(inst.dest + i), SCRATCHF2, SCRATCHF1, FCC0);
			}
		}
		break;

	case IROp::Vec4CvtScaledSW:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			VFFINT_S_W(regs_.V(inst.dest), regs_.V(inst.src1));
			if ((inst.src2 & 0x1F) != 0) {
				float scale = 1.0f / (1UL << (inst.src2 & 0x1F));
				u32 bits;
				memcpy(&bits, &scale, 4);
				LI(SCRATCH1, (s32)bits);
				VREPLGR2VR_W(EncodeRegToV(SCRATCHF1), SCRATCH1);
				VFMUL_S(regs_.V(inst.dest), regs_.V(inst.dest), EncodeRegToV(SCRATCHF1));
			}
		} else {
			if ((inst.src2 & 0x1F) != 0)
				QuickFLI(32, SCRATCHF1, 1.0f / (1UL << (inst.src2 & 0x1F)), SCRATCH1);
			for (int i = 0; i < 4; ++i) {
				FFINT_S_W(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
				if ((inst.src2 & 0x1F) != 0)
					FMUL_S(regs_.F(inst.dest + i), regs_.F(inst.dest + i), SCRATCHF1);
			}
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4VfpuDot:
	{
		// The operands go to the shared routine, in v8 and v9 (SCRATCHF1 and SCRATCHF2) with LSX and
		// mips->vfpuDotArgs without, and the result comes back in f8. Whatever guest state is in the
		// registers it clobbers gets flushed after the operands are moved, in case one was mapped into
		// one of them. They come last in the allocation order, so usually that's nothing.
		LoongArch64VfpuDotRegs dotRegs;
		GetLoongArch64VfpuDotRegs(CTXREG, &dotRegs);
		const bool lsx = cpu_info.LOONGARCH_LSX;
		if (lsx) {
			regs_.SpillLockFPR(inst.src1, inst.src2);
			const LoongArch64Reg x = regs_.MapVec4(inst.src1);
			const LoongArch64Reg y = regs_.MapVec4(inst.src2);
			VOR_V(dotRegs.x, x, x);
			VOR_V(dotRegs.y, y, y);
			regs_.ReleaseSpillLockFPR(inst.src1, inst.src2);
			for (LoongArch64Reg r : dotRegs.v)
				regs_.FlushHostRegs({ r });
		} else {
			for (int i = 0; i < 4; ++i) {
				regs_.SpillLockFPR(inst.src1 + i, inst.src2 + i);
				FST_S(regs_.MapFPR(inst.src1 + i), CTXREG, dotRegs.offset + i * 4);
				FST_S(regs_.MapFPR(inst.src2 + i), CTXREG, dotRegs.offset + 16 + i * 4);
			}
			for (int i = 0; i < 4; ++i)
				regs_.ReleaseSpillLockFPR(inst.src1 + i, inst.src2 + i);
		}
		for (int i = 0; i < (lsx ? 5 : 9); ++i)
			regs_.FlushHostRegs({ dotRegs.gpr[i] });
		QuickCallFunction(vfpuDot_, SCRATCH2);

		if (lsx && (Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4))) {
			// The dest lane is in a Vec4 the operands were mapped as.
			regs_.MapVec4(inst.dest & ~3, MIPSMap::DIRTY);
			VEXTRINS_W(regs_.V(inst.dest & ~3), EncodeRegToV(dotRegs.ftemp), (inst.dest & 3) << 4);
		} else {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			FMOV_S(regs_.F(inst.dest), dotRegs.ftemp);
		}
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
		// With LSX, the pair is one vector reg (F() would give the same reg for both lanes).
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 2, inst.src1, 1))
				DISABLE;
			regs_.Map(inst);
			// Each halfword to the top of its lane, then down one.
			VXOR_V(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VILVL_H(regs_.V(inst.dest), regs_.V(inst.src1), EncodeRegToV(SCRATCHF1));
			VSRLI_W(regs_.V(inst.dest), regs_.V(inst.dest), 1);
			break;
		}
		// Like Vec2Unpack16To32, shifted down one more.
		regs_.Map(inst);
		MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1));
		SLLI_W(SCRATCH1, SCRATCH2, 16);
		SRLI_W(SCRATCH1, SCRATCH1, 1);
		MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
		SRLI_W(SCRATCH1, SCRATCH2, 16);
		SLLI_W(SCRATCH1, SCRATCH1, 15);
		MOVGR2FR_W(regs_.F(inst.dest + 1), SCRATCH1);
		break;

	case IROp::Vec2Pack31To16:
	{
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 2))
				DISABLE;
			regs_.Map(inst);
			VREPLGR2VR_D(EncodeRegToV(SCRATCHF1), R_ZERO);
			VMAX_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), EncodeRegToV(SCRATCHF1));
			VSRLI_W(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), 15);
			VPICKEV_H(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			break;
		}
		// Bits 30-15 of each lane, with negative lanes clamped to zero.
		regs_.Map(inst);
		LoongArch64Reg maskReg = regs_.GetAndLockTempGPR();
		MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1));
		MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1 + 1));
		SRAI_W(maskReg, SCRATCH1, 31);
		ANDN(SCRATCH1, SCRATCH1, maskReg);
		SRAI_W(maskReg, SCRATCH2, 31);
		ANDN(SCRATCH2, SCRATCH2, maskReg);
		SRLI_D(SCRATCH1, SCRATCH1, 15);
		SRLI_D(SCRATCH2, SCRATCH2, 15);
		SLLI_D(SCRATCH2, SCRATCH2, 16);
		OR(SCRATCH1, SCRATCH1, SCRATCH2);
		MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
		break;
	}

	case IROp::Vec4Pack32To8:
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4))
				DISABLE;

			regs_.Map(inst);
			VSRLI_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), 24);
			VPICKEV_B(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VPICKEV_B(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
		} else {
			// The top byte of each lane. Every lane is read before dest is written.
			regs_.Map(inst);
			for (int i = 0; i < 4; ++i) {
				MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1 + i));
				SRLI_W(SCRATCH1, SCRATCH1, 24);
				if (i == 0) {
					MOVE(SCRATCH2, SCRATCH1);
				} else {
					SLLI_D(SCRATCH1, SCRATCH1, 8 * i);
					OR(SCRATCH2, SCRATCH2, SCRATCH1);
				}
			}
			MOVGR2FR_W(regs_.F(inst.dest), SCRATCH2);
		}
		break;

	case IROp::Vec4Unpack8To32:
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4))
				DISABLE;

			regs_.Map(inst);
			VSLLWIL_HU_BU(regs_.V(inst.dest), regs_.V(inst.src1), 0);
			VSLLWIL_WU_HU(regs_.V(inst.dest), regs_.V(inst.dest), 0);
			VSLLI_W(regs_.V(inst.dest), regs_.V(inst.dest), 24);
		} else {
			regs_.Map(inst);
			MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1));
			for (int i = 0; i < 4; ++i) {
				// Mask using walls.
				if (i != 0) {
					SRLI_D(SCRATCH1, SCRATCH2, i * 8);
					SLLI_D(SCRATCH1, SCRATCH1, 24);
				} else {
					SLLI_D(SCRATCH1, SCRATCH2, 24);
				}
				MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
			}
		}
		break;

	case IROp::Vec2Unpack16To32:
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 2, inst.src1, 1))
				DISABLE;
			regs_.Map(inst);
			VXOR_V(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VILVL_H(regs_.V(inst.dest), regs_.V(inst.src1), EncodeRegToV(SCRATCHF1));
			break;
		}
		// src1 is read before either lane of dest is written.
		regs_.Map(inst);
		MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1));
		SLLI_D(SCRATCH1, SCRATCH2, 16);
		MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
		SRLI_D(SCRATCH1, SCRATCH2, 16);
		SLLI_D(SCRATCH1, SCRATCH1, 16);
		MOVGR2FR_W(regs_.F(inst.dest + 1), SCRATCH1);
		break;

	case IROp::Vec4DuplicateUpperBitsAndShift1:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			VSRLI_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), 16);
			VOR_V(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), regs_.V(inst.src1));
			VSRLI_W(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), 8);
			VOR_V(regs_.V(inst.dest), regs_.V(inst.dest), EncodeRegToV(SCRATCHF1));
			VSRLI_W(regs_.V(inst.dest), regs_.V(inst.dest), 1);
		} else {
			for (int i = 0; i < 4; i++) {
				MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1 + i));
				SRLI_W(SCRATCH2, SCRATCH1, 8);
				OR(SCRATCH1, SCRATCH1, SCRATCH2);
				SRLI_W(SCRATCH2, SCRATCH1, 16);
				OR(SCRATCH1, SCRATCH1, SCRATCH2);
				SRLI_W(SCRATCH1, SCRATCH1, 1);
				MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
			}
		}
		break;

	case IROp::Vec4Pack31To8:
		// Bits 30-23 of each lane, with negative lanes clamped to zero.
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4))
				DISABLE;

			regs_.Map(inst);
			VREPLGR2VR_D(EncodeRegToV(SCRATCHF1), R_ZERO);
			VMAX_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), EncodeRegToV(SCRATCHF1));
			VSRLI_W(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), 23);
			VPICKEV_B(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VPICKEV_B(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
		} else {
			// Every lane is read before dest is written.
			regs_.Map(inst);
			LoongArch64Reg maskReg = regs_.GetAndLockTempGPR();
			for (int i = 0; i < 4; ++i) {
				MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1 + i));
				SRAI_W(maskReg, SCRATCH1, 31);
				ANDN(SCRATCH1, SCRATCH1, maskReg);
				// At most 0x7FFFFFFF, so this leaves 0-255.
				SRLI_D(SCRATCH1, SCRATCH1, 23);
				if (i == 0) {
					MOVE(SCRATCH2, SCRATCH1);
				} else {
					SLLI_D(SCRATCH1, SCRATCH1, 8 * i);
					OR(SCRATCH2, SCRATCH2, SCRATCH1);
				}
			}
			MOVGR2FR_W(regs_.F(inst.dest), SCRATCH2);
		}
		break;

	case IROp::Vec2Pack32To16:
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 2))
				DISABLE;
			// The top halfword of each lane.
			regs_.Map(inst);
			VPICKOD_H(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src1));
			break;
		}
		regs_.Map(inst);
		MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1));
		MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1 + 1));
		// Keep in mind, this was sign-extended, so we have to zero the upper.
		SLLI_D(SCRATCH1, SCRATCH1, 32);
		// Now we just set (SCRATCH2 & 0xFFFF0000) | SCRATCH1.
		SRLI_D(SCRATCH1, SCRATCH1, 48);
		// Use a wall to mask.  We can ignore the upper 32 here.
		SRLI_D(SCRATCH2, SCRATCH2, 16);
		SLLI_D(SCRATCH2, SCRATCH2, 16);
		OR(SCRATCH1, SCRATCH1, SCRATCH2);
		// Okay, to the floating point register.
		MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
