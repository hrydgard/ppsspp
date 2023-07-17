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

#pragma once

#include "Common/RiscVEmitter.h"
#include "Core/MIPS/IR/IRJit.h"

namespace MIPSComp {

class RiscVJit : public RiscVGen::RiscVCodeBlock, public IRJit {
public:
	RiscVJit(MIPSState *mipsState);

	void RunLoopUntil(u64 globalticks) override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;
	bool CodeInRange(const u8 *ptr) const override;
	bool IsAtDispatchFetch(const u8 *ptr) const override;
	const u8 *GetDispatcher() const override;
	const u8 *GetCrashHandler() const override;

	void ClearCache() override;
	void UpdateFCR31() override;

	// TODO: GetBlockCacheDebugInterface, block linking?

protected:
	bool CompileBlock(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes, bool preload) override;

private:
	void GenerateFixedCode(const JitOptions &jo);

	const u8 *enterDispatcher_ = nullptr;

	int jitStartOffset_ = 0;
};

} // namespace MIPSComp
