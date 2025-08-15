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
					int lane = (inst.src2 >> (i * 2)) & 3;
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
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 4, inst.src2, 1) || Overlap(inst.src1, 4, inst.src2, 1))
				DISABLE;

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

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Dot:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4))
				DISABLE;

			VFMUL_S(regs_.V(inst.dest), regs_.V(inst.src1), regs_.V(inst.src2));
			VOR_V(EncodeRegToV(SCRATCHF1), regs_.V(inst.dest), regs_.V(inst.dest));
			VSHUF4I_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.dest), VFPU_SWIZZLE(1, 0, 3, 2));
			VFADD_S(regs_.V(inst.dest), regs_.V(inst.dest), EncodeRegToV(SCRATCHF1));
			VEXTRINS_D(EncodeRegToV(SCRATCHF1), regs_.V(inst.dest), 1);
			// Do we need care about upper 96 bits?
			VFADD_S(regs_.V(inst.dest), regs_.V(inst.dest), EncodeRegToV(SCRATCHF1));
		} else {
			if (Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4)) {
				// This means inst.dest overlaps one of src1 or src2.  We have to do that one first.
				// Technically this may impact -0.0 and such, but dots accurately need to be aligned anyway.
				for (int i = 0; i < 4; ++i) {
					if (inst.dest == inst.src1 + i || inst.dest == inst.src2 + i)
						FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
				}
				for (int i = 0; i < 4; ++i) {
					if (inst.dest != inst.src1 + i && inst.dest != inst.src2 + i)
						FMADD_S(regs_.F(inst.dest), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i), regs_.F(inst.dest));
				}
			} else {
				FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
				for (int i = 1; i < 4; ++i)
					FMADD_S(regs_.F(inst.dest), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i), regs_.F(inst.dest));
			}
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
	case IROp::Vec2Pack31To16:
		CompIR_Generic(inst);
		break;

	case IROp::Vec4Pack32To8:
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4))
				DISABLE;

			regs_.Map(inst);
			VSRLI_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), 24);
			VPICKEV_B(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VPICKEV_B(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
		} else {
			CompIR_Generic(inst);
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
		// TODO: This works for now, but may need to handle aliasing for vectors.
		if (cpu_info.LOONGARCH_LSX) {
			CompIR_Generic(inst);
			break;
		}
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
		// TODO: This works for now, but may need to handle aliasing for vectors.
		if (cpu_info.LOONGARCH_LSX) {
			if (Overlap(inst.dest, 1, inst.src1, 4))
				DISABLE;

			regs_.Map(inst);
			VSRLI_W(EncodeRegToV(SCRATCHF1), regs_.V(inst.src1), 23);
			VPICKEV_B(EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
			VPICKEV_B(regs_.V(inst.dest), EncodeRegToV(SCRATCHF1), EncodeRegToV(SCRATCHF1));
		} else {
			regs_.Map(inst);
			for (int i = 0; i < 4; ++i) {
				MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1 + i));
				SRLI_D(SCRATCH1, SCRATCH1, 23);
				if (i == 0) {
					ANDI(SCRATCH2, SCRATCH1, 0xFF);
				} else {
					ANDI(SCRATCH1, SCRATCH1, 0xFF);
					SLLI_D(SCRATCH1, SCRATCH1, 8 * i);
					OR(SCRATCH2, SCRATCH2, SCRATCH1);
				}
			}
			MOVGR2FR_W(regs_.F(inst.dest), SCRATCH2);
		}
		break;

	case IROp::Vec2Pack32To16:
		// TODO: This works for now, but may need to handle aliasing for vectors.
		if (cpu_info.LOONGARCH_LSX) {
			CompIR_Generic(inst);
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

void LoongArch64JitBackend::CompIR_VecClamp(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4ClampToZero:
		regs_.Map(inst);
		if (cpu_info.LOONGARCH_LSX) {
			VREPLGR2VR_D(EncodeRegToV(SCRATCHF1), R_ZERO);
			VMAX_W(regs_.V(inst.dest), regs_.V(inst.src1), EncodeRegToV(SCRATCHF1));
		} else {
			for (int i = 0; i < 4; i++) {
				MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1 + i));
				SRAI_W(SCRATCH2, SCRATCH1, 31);
				ORN(SCRATCH2, R_ZERO, SCRATCH2);
				AND(SCRATCH1, SCRATCH1, SCRATCH2);
				MOVGR2FR_W(regs_.F(inst.dest + i), SCRATCH1);
			}
		}
		break;

	case IROp::Vec2ClampToZero:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
