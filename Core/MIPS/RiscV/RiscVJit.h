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

#include <string>
#include <vector>
#include "Common/RiscVEmitter.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

namespace MIPSComp {

class RiscVJit : public RiscVGen::RiscVCodeBlock, public IRJit {
public:
	RiscVJit(MIPSState *mipsState);
	~RiscVJit();

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

	void CompileIRInst(IRInst inst);

private:
	void GenerateFixedCode(const JitOptions &jo);

	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void MovFromPC(RiscVGen::RiscVReg r);
	void MovToPC(RiscVGen::RiscVReg r);

	void SaveStaticRegisters();
	void LoadStaticRegisters();

	void FlushAll();

	void CompIR_Generic(IRInst inst);
	void CompIR_Basic(IRInst inst);
	void CompIR_Arith(IRInst inst);
	void CompIR_Logic(IRInst inst);
	void CompIR_Assign(IRInst inst);
	void CompIR_Exit(IRInst inst);

	RiscVRegCache gpr;

	const u8 *enterDispatcher_ = nullptr;

	const u8 *outerLoop_ = nullptr;
	const u8 *outerLoopPCInSCRATCH1_ = nullptr;
	const u8 *dispatcherCheckCoreState_ = nullptr;
	const u8 *dispatcherPCInSCRATCH1_ = nullptr;
	const u8 *dispatcher_ = nullptr;
	const u8 *dispatcherNoCheck_ = nullptr;
	const u8 *dispatcherFetch_ = nullptr;

	const u8 *saveStaticRegisters_ = nullptr;
	const u8 *loadStaticRegisters_ = nullptr;

	const u8 *crashHandler_ = nullptr;

	int jitStartOffset_ = 0;
	const u8 **blockStartAddrs_ = nullptr;
};

} // namespace MIPSComp
