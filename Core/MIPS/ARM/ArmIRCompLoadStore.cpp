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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

// This file contains compilation for memory loads and stores.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

void ArmJitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_LoadShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_StoreShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

void ArmJitBackend::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	default:
		CompIR_Generic(inst);
		break;
	}
}

} // namespace MIPSComp

#endif
