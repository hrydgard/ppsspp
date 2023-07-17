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

#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Common/Profiler/Profiler.h"

namespace MIPSComp {

RiscVJit::RiscVJit(MIPSState *mipsState) : IRJit(mipsState) {
	AllocCodeSpace(1024 * 1024 * 16);

	// TODO: gpr, fpr, GenerateFixedCode(jo);
}

void RiscVJit::RunLoopUntil(u64 globalticks) {
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterDispatcher_)();
}

bool RiscVJit::CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) {
	if (!IRJit::CompileBlock(em_address, instructions, mipsBytes, preload))
		return false;

	// TODO: Compile native.
	return true;
}

bool RiscVJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// TODO: Describe for debugging / profiling.
	return false;
}

bool RiscVJit::CodeInRange(const u8 *ptr) const {
	return IsInSpace(ptr);
}

bool RiscVJit::IsAtDispatchFetch(const u8 *ptr) const {
	// TODO
	return false;
}

const u8 *RiscVJit::GetDispatcher() const {
	// TODO
	return nullptr;
}

const u8 *RiscVJit::GetCrashHandler() const {
	// TODO: Implement a crash handler
	return nullptr;
}

void RiscVJit::ClearCache() {
	IRJit::ClearCache();

	ClearCodeSpace(jitStartOffset_);
	FlushIcacheSection(region + jitStartOffset_, region + region_size - jitStartOffset_);
}

void RiscVJit::UpdateFCR31() {
	IRJit::UpdateFCR31();

	// TODO: Handle rounding modes.
}

} // namespace MIPSComp
