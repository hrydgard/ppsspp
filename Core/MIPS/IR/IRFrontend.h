#pragma once

#include "Common/CommonTypes.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRInst.h"

namespace Memory {
struct AddressSpace;
}

namespace MIPSComp {

class IRFrontend : public MIPSFrontendInterface {
public:
	IRFrontend(bool startDefaultPrefix);
	// Copies the state compiling depends on, to compile a block on the side.
	IRFrontend(const IRFrontend &other) : js(other.js), opts(other.opts), space_(other.space_) {}
	void Comp_Generic(MIPSOpcode op) override;

	// What the CPU compiled for sees of memory, which is the main CPU's unless set.
	void SetAddressSpace(const Memory::AddressSpace *space) { space_ = space; }

	void Comp_ReplacementFunc(MIPSOpcode op) override;

	// Ops
	void Comp_ITypeMem(MIPSOpcode op) override;
	void Comp_StoreSync(MIPSOpcode op) override;
	void Comp_Cache(MIPSOpcode op) override;

	void Comp_RelBranch(MIPSOpcode op) override;
	void Comp_RelBranchRI(MIPSOpcode op) override;
	void Comp_FPUBranch(MIPSOpcode op) override;
	void Comp_FPULS(MIPSOpcode op) override;
	void Comp_FPUComp(MIPSOpcode op) override;
	void Comp_Jump(MIPSOpcode op) override;
	void Comp_JumpReg(MIPSOpcode op) override;
	void Comp_Syscall(MIPSOpcode op) override;
	void Comp_Break(MIPSOpcode op) override;

	void Comp_IType(MIPSOpcode op) override;
	void Comp_RType2(MIPSOpcode op) override;
	void Comp_RType3(MIPSOpcode op) override;
	void Comp_ShiftType(MIPSOpcode op) override;
	void Comp_Allegrex(MIPSOpcode op) override;
	void Comp_Allegrex2(MIPSOpcode op) override;
	void Comp_VBranch(MIPSOpcode op) override;
	void Comp_MulDivType(MIPSOpcode op) override;
	void Comp_Special3(MIPSOpcode op) override;

	void Comp_FPU3op(MIPSOpcode op) override;
	void Comp_FPU2op(MIPSOpcode op) override;
	void Comp_mxc1(MIPSOpcode op) override;

	void Comp_DoNothing(MIPSOpcode op) override;

	void Comp_SV(MIPSOpcode op) override;
	void Comp_SVQ(MIPSOpcode op) override;
	void Comp_VPFX(MIPSOpcode op) override;
	void Comp_VVectorInit(MIPSOpcode op) override;
	void Comp_VMatrixInit(MIPSOpcode op) override;
	void Comp_VDot(MIPSOpcode op) override;
	void Comp_VecDo3(MIPSOpcode op) override;
	void Comp_VV2Op(MIPSOpcode op) override;
	void Comp_Mftv(MIPSOpcode op) override;
	void Comp_Vmfvc(MIPSOpcode op) override;
	void Comp_Vmtvc(MIPSOpcode op) override;
	void Comp_Vmmov(MIPSOpcode op) override;
	void Comp_VScl(MIPSOpcode op) override;
	void Comp_Vmmul(MIPSOpcode op) override;
	void Comp_Vmscl(MIPSOpcode op) override;
	void Comp_Vtfm(MIPSOpcode op) override;
	void Comp_VHdp(MIPSOpcode op) override;
	void Comp_VCrs(MIPSOpcode op) override;
	void Comp_VDet(MIPSOpcode op) override;
	void Comp_Vi2x(MIPSOpcode op) override;
	void Comp_Vx2i(MIPSOpcode op) override;
	void Comp_Vf2i(MIPSOpcode op) override;
	void Comp_Vi2f(MIPSOpcode op) override;
	void Comp_Vh2f(MIPSOpcode op) override;
	void Comp_Vcst(MIPSOpcode op) override;
	void Comp_Vhoriz(MIPSOpcode op) override;
	void Comp_VRot(MIPSOpcode op) override;
	void Comp_VIdt(MIPSOpcode op) override;
	void Comp_Vcmp(MIPSOpcode op) override;
	void Comp_Vcmov(MIPSOpcode op) override;
	void Comp_Viim(MIPSOpcode op) override;
	void Comp_Vfim(MIPSOpcode op) override;
	void Comp_VCrossQuat(MIPSOpcode op) override;
	void Comp_Vsgn(MIPSOpcode op) override;
	void Comp_Vocp(MIPSOpcode op) override;
	void Comp_ColorConv(MIPSOpcode op) override;
	void Comp_Vbfy(MIPSOpcode op) override;

	int Replace_fabsf() override;
	// Returns true if a loaded state means blocks compiled before it can't run any more.
	bool DoState(PointerWrap &p);
	bool UpdateFCR31(u32 fcr31);  // returns true if the block cache needs clearing
	bool CheckRounding(u32 blockAddress);  // returns true if we need a do-over

	void DoJit(u32 em_address, std::vector<IRInst> &instructions, u32 &mipsBytes);

	void EatPrefix() override {
		js.EatPrefix();
	}

	void SetOptions(const IROptions &o) {
		opts = o;
	}

	// One lane of a Vec4VfpuDot operand: a reg, maybe negated, or a constant (reg < 0).
	struct DotLane {
		int reg;
		float constant;
		bool negate;
	};

private:
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode();

	void FlushAll();
	void FlushPrefixV();

	u32 GetCompilerPC();
	void CompileDelaySlot();
	void EatInstruction(MIPSOpcode op);
	MIPSOpcode GetOffsetInstruction(int offset);

	void CheckBreakpoint(u32 addr);
	void CheckMemoryBreakpoint(int rs, int offset);

	// Utility compilation functions
	void BranchFPFlag(MIPSOpcode op, IRComparison cc, bool likely);
	void BranchVFPUFlag(MIPSOpcode op, IRComparison cc, bool likely);
	void BranchRSZeroComp(MIPSOpcode op, IRComparison cc, bool andLink, bool likely);
	void BranchRSRTComp(MIPSOpcode op, IRComparison cc, bool likely);
	bool CanContinuePastBranch(const BranchInfo &branchInfo, bool constantOperands);

	// Utilities to reduce duplicated code
	void CompShiftImm(MIPSOpcode op, IROp shiftType, int sa);
	void CompShiftVar(MIPSOpcode op, IROp shiftType);

	void ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz, int tempReg);
	void ApplyPrefixD(u8 *vregs, VectorSize sz, int vectorReg);
	void ApplyPrefixDMask(u8 *vregs, VectorSize sz, int vectorReg);
	void GetVectorRegsPrefixS(u8 *regs, VectorSize sz, int vectorReg);
	void GetVectorRegsPrefixT(u8 *regs, VectorSize sz, int vectorReg);
	void GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg);
	void GetVectorRegs(u8 regs[4], VectorSize N, int vectorReg);
	void GetMatrixRegs(u8 regs[16], MatrixSize N, int matrixReg);

	static DotLane DotReg(int reg, bool negate = false) { return DotLane{ reg, 0.0f, negate }; }
	static DotLane DotConst(float f) { return DotLane{ -1, f, false }; }
	// Puts both operands of a Vec4VfpuDot in Vec4 regs, building them in free temps where they
	// aren't one already. avoid is a Vec4 temp that must not be used (or -1). Returns false if
	// there aren't enough free temps, and then nothing was written.
	bool BuildDotOperands(const DotLane s[4], const DotLane t[4], int avoid, u8 *sVec, u8 *tVec);
	// One dot for TryMultiDot(): its operands' lanes, and where the result goes.
	struct MultiDot {
		DotLane a[4];
		DotLane b[4];
		int dest;
	};
	// Writes up to four dots as one Vec4VfpuMultiDot, if they fit it: one operand shared, the others
	// lanes of one 4x4 block, terms past the count zero, and no dot reading what an earlier one wrote.
	// With destScratch, a Vec4 that has dot k's result at lane k may be written whole. avoid is a Vec4
	// temp not to use (or -1). Returns false, having written nothing, if they don't fit.
	bool TryMultiDot(const MultiDot *dots, int count, int terms, int avoid, bool destScratch);

	// State
	JitState js;
	IRWriter ir;
	IROptions opts{};

	int dontLogBlocks = 0;
	int logBlocks = 0;

private:
	bool IsMediaEngine() const;
	MIPSOpcode ReadOp(u32 addr) const;

	const Memory::AddressSpace *space_ = nullptr;
};

}  // namespace
