#pragma once

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"

namespace MIPSComp {

using namespace ArmGen;

inline ARMReg MatchSize(ARMReg x, ARMReg target) {
	if (IsQ(target) && IsQ(x))
		return x;
	if (IsD(target) && IsD(x))
		return x;
	if (IsD(target) && IsQ(x))
		return D_0(x);
	// if (IsQ(target) && IsD(x))
	return (ARMReg)(D0 + (x - Q0) * 2);
}

}