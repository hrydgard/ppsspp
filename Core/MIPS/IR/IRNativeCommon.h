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

#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"

class IRNativeBlockCacheDebugInterface : public JitBlockCacheDebugInterface {
public:
	IRNativeBlockCacheDebugInterface(MIPSComp::IRBlockCache &irBlocks, CodeBlockCommon &codeBlock);
	int GetNumBlocks() const;
	int GetBlockNumberFromStartAddress(u32 em_address, bool realBlocksOnly = true) const;
	JitBlockDebugInfo GetBlockDebugInfo(int blockNum) const;
	void ComputeStats(BlockCacheStats &bcStats) const;

private:
	void GetBlockCodeRange(int blockNum, int *startOffset, int *size) const;

	MIPSComp::IRBlockCache &irBlocks_;
	CodeBlockCommon &codeBlock_;
};

namespace MIPSComp {

class IRNativeBackend {
public:
	virtual ~IRNativeBackend() {}

	void CompileIRInst(IRInst inst);

protected:
	virtual void CompIR_Arith(IRInst inst) = 0;
	virtual void CompIR_Assign(IRInst inst) = 0;
	virtual void CompIR_Basic(IRInst inst) = 0;
	virtual void CompIR_Bits(IRInst inst) = 0;
	virtual void CompIR_Breakpoint(IRInst inst) = 0;
	virtual void CompIR_Compare(IRInst inst) = 0;
	virtual void CompIR_CondAssign(IRInst inst) = 0;
	virtual void CompIR_CondStore(IRInst inst) = 0;
	virtual void CompIR_Div(IRInst inst) = 0;
	virtual void CompIR_Exit(IRInst inst) = 0;
	virtual void CompIR_ExitIf(IRInst inst) = 0;
	virtual void CompIR_FArith(IRInst inst) = 0;
	virtual void CompIR_FAssign(IRInst inst) = 0;
	virtual void CompIR_FCompare(IRInst inst) = 0;
	virtual void CompIR_FCondAssign(IRInst inst) = 0;
	virtual void CompIR_FCvt(IRInst inst) = 0;
	virtual void CompIR_FLoad(IRInst inst) = 0;
	virtual void CompIR_FRound(IRInst inst) = 0;
	virtual void CompIR_FSat(IRInst inst) = 0;
	virtual void CompIR_FSpecial(IRInst inst) = 0;
	virtual void CompIR_FStore(IRInst inst) = 0;
	virtual void CompIR_Generic(IRInst inst) = 0;
	virtual void CompIR_HiLo(IRInst inst) = 0;
	virtual void CompIR_Interpret(IRInst inst) = 0;
	virtual void CompIR_Load(IRInst inst) = 0;
	virtual void CompIR_LoadShift(IRInst inst) = 0;
	virtual void CompIR_Logic(IRInst inst) = 0;
	virtual void CompIR_Mult(IRInst inst) = 0;
	virtual void CompIR_RoundingMode(IRInst inst) = 0;
	virtual void CompIR_Shift(IRInst inst) = 0;
	virtual void CompIR_Store(IRInst inst) = 0;
	virtual void CompIR_StoreShift(IRInst inst) = 0;
	virtual void CompIR_System(IRInst inst) = 0;
	virtual void CompIR_Transfer(IRInst inst) = 0;
	virtual void CompIR_VecArith(IRInst inst) = 0;
	virtual void CompIR_VecAssign(IRInst inst) = 0;
	virtual void CompIR_VecClamp(IRInst inst) = 0;
	virtual void CompIR_VecHoriz(IRInst inst) = 0;
	virtual void CompIR_VecLoad(IRInst inst) = 0;
	virtual void CompIR_VecPack(IRInst inst) = 0;
	virtual void CompIR_VecStore(IRInst inst) = 0;
	virtual void CompIR_ValidateAddress(IRInst inst) = 0;
};

class IRNativeJit : public IRJit {
public:
	IRNativeJit(MIPSState *mipsState) : IRJit(mipsState) {}
};

} // namespace MIPSComp
