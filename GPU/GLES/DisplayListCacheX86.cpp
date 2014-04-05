// Copyright (c) 2014- PPSSPP Project.

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

#include "Core/Config.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/DisplayListCache.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GLES/ShaderManager.h"

#define DISABLE { return Jit_Generic(op); }
#define CONDITIONAL_DISABLE
//#define CONDITIONAL_DISABLE { return Jit_Generic(op); }

using namespace Gen;

void DisplayListCache::Initialize() {
	AllocCodeSpace(1024 * 1024 * 4);
#if defined(_WIN32)
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#endif

	// TODO: Flush when bSoftwareSkinning changed?  Need some mechanics.

	// TODO: Move to common?
	for (int i = 0; i < 256; ++i) {
		cmds_[i] = &DisplayListCache::Jit_Generic;
	}

	cmds_[GE_CMD_NOP] = &DisplayListCache::Jit_Nop;
	cmds_[GE_CMD_VADDR] = &DisplayListCache::Jit_Vaddr;
	cmds_[GE_CMD_IADDR] = &DisplayListCache::Jit_Iaddr;
	cmds_[GE_CMD_PRIM] = &DisplayListCache::Jit_Prim;
	cmds_[GE_CMD_VERTEXTYPE] = &DisplayListCache::Jit_VertexType;
}

void DisplayListCache::DoExecuteOp(GLES_GPU *g, u32 op, u32 diff) {
	g->ExecuteOpInternal(op, diff);
}

void DisplayListCache::DoFlush(TransformDrawEngine *t) {
	t->DoFlush();
}

#ifdef _M_X64
#define PTRBITS 64
#else
#define PTRBITS 32
#endif

#ifdef _M_X64
static const X64Reg pcReg = R12;
static const X64Reg opReg = R13;
static const X64Reg diffReg = R14;
static const X64Reg gstateReg = R15;
static const X64Reg pcAddrReg = RBP;

#ifdef _WIN32
static const X64Reg pcParamReg = RCX;
#else
static const X64Reg pcParamReg = RDI;
#endif

static inline OpArg MCmdState(u32 cmd) {
	return MDisp(gstateReg, cmd * 4);
}
#else
static const OpArg pcArg = MatR(EBP);
static const X64Reg opReg = ESI;
static const X64Reg diffReg = EDI;
static const X64Reg pcAddrReg = EBP;

static inline OpArg MCmdState(u32 cmd) {
	return M(&gstate.cmdmem[cmd]);
}
#endif

#ifdef LOG_JIT_MISS
static std::map<u32, int> commonCmds;
#endif

JittedDisplayListEntry DisplayListCache::Compile(u32 &pc, int &downcount) {
	if (GetSpaceLeft() < 0x10000) {
		ClearCodeSpace();
		jitted_.clear();
	}

	const u8 *start = this->GetCodePtr();

	ABI_PushAllCalleeSavedRegsAndAdjustStack();

#ifdef _M_X64
	MOV(32, R(pcReg), MatR(pcParamReg));
	MOV(PTRBITS, R(RAX), ImmPtr(Memory::base));
	ADD(PTRBITS, R(pcReg), R(RAX));
	MOV(PTRBITS, R(pcAddrReg), R(pcParamReg));
	MOV(PTRBITS, R(gstateReg), ImmPtr(gstate.cmdmem));
#else
	MOV(32, R(pcAddrReg), MDisp(ESP, 16 + 4 + 0));
#endif

	std::vector<FixupBranch> fixups;

	while (downcount > 0) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + pc);
		const u32 cmd = op >> 24;

#ifdef _M_X64
		MOV(32, R(opReg), MatR(pcReg));
#else
		MOV(32, R(EAX), pcArg);
		MOV(32, R(opReg), MDisp(EAX, (u32)Memory::base));
#endif
		MOV(32, R(diffReg), MCmdState(cmd));
		XOR(32, R(diffReg), R(opReg));

		CMP(32, R(diffReg), Imm32(0x01000000));
		fixups.push_back(J_CC(CC_AE, true));

		(this->*cmds_[cmd])(op);

#ifdef _M_X64
		ADD(PTRBITS, R(pcReg), Imm32(4));
#else
		MOV(32, R(EAX), pcArg);
		ADD(32, R(EAX), Imm32(4));
		MOV(32, pcArg, R(EAX));
#endif
		pc += 4;
		downcount--;

		if (GetSpaceLeft() < 0x200) {
			break;
		}
	}
	JitStorePC();
	MOV(32, M(&downcount), Imm32(downcount));

	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	MOV(32, R(EAX), Imm32(0));
	RET();

	for (auto it = fixups.begin(), end = fixups.end(); it != end; ++it) {
		SetJumpTarget(*it);
	}
	JitStorePC();
	// TODO: Temporary for testing.
	MOV(32, R(EAX), R(diffReg));
	MOV(32, M(&downcount), Imm32(0));
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();

#ifdef LOG_JIT_MISS
	int highest = 0;
	u32 highestCmd = 0;
	int highest2 = 0;
	u32 highest2Cmd = 0;
	for (auto it = commonCmds.begin(), end = commonCmds.end(); it != end; ++it) {
		if (it->second > highest) {
			highest2 = highest;
			highest2Cmd = highestCmd;
			highest = it->second;
			highestCmd = it->first;
		} else if (it->second > highest2) {
			highest2 = it->second;
			highest2Cmd = it->first;
		}
	}
	commonCmds.clear();
	NOTICE_LOG(G3D, "Top 2 GE commands: %02x (%d), %02x (%d)", highestCmd, highest, highest2Cmd, highest2);
#endif

	return (JittedDisplayListEntry)start;
}

void DisplayListCache::JitLoadPC() {
	// On x86, the pc is always stored.
#ifdef _M_X64
	MOV(32, R(pcReg), MatR(pcAddrReg));
	MOV(PTRBITS, R(RAX), ImmPtr(Memory::base));
	ADD(PTRBITS, R(pcReg), R(RAX));
#endif
}

void DisplayListCache::JitStorePC() {
	// On x86, the pc is always stored.
#ifdef _M_X64
	MOV(PTRBITS, R(RAX), ImmPtr(Memory::base));
	SUB(PTRBITS, R(pcReg), R(RAX));
	MOV(32, MatR(pcAddrReg), R(pcReg));
	ADD(PTRBITS, R(pcReg), R(RAX));
#endif
}

inline void DisplayListCache::JitFlush(u32 diff, bool onChange, bool exec) {
	if (exec) {
		gpu_->transformDraw_.Flush();
	}

	FixupBranch skip1;
	if (onChange) {
		CMP(32, R(diffReg), Imm32(0));
		skip1 = J_CC(CC_E);
	}

	MOV(PTRBITS, R(RCX), ImmPtr(&gpu_->transformDraw_));
	CMP(32, MDisp(RCX, offsetof(TransformDrawEngine, numDrawCalls)), Imm32(0));
	FixupBranch skip2 = J_CC(CC_E);
	ABI_CallFunctionR((const void *)&DoFlush, RCX);
	SetJumpTarget(skip2);

	if (onChange) {
		SetJumpTarget(skip1);
	}
}

void DisplayListCache::JitDirtyUniform(u32 what, bool exec) {
	if (exec) {
		gpu_->shaderManager_->DirtyUniform(what);
	}

	MOV(PTRBITS, R(RAX), ImmPtr(&gpu_->shaderManager_->globalDirty_));
	OR(32, MatR(RAX), Imm32(what));
}

void DisplayListCache::Jit_Generic(u32 op) {
	const u32 cmd = op >> 24;
	const u32 diff = op ^ gstate.cmdmem[cmd];
	const u8 cmdFlags = gpu_->commandFlags_[cmd];

#ifdef LOG_JIT_MISS
	if (cmdFlags != 0) {
		commonCmds[cmd]++;
	}
#endif

	if (cmdFlags & FLAG_FLUSHBEFORE) {
		JitFlush();
	} else if (cmdFlags & FLAG_FLUSHBEFOREONCHANGE) {
		JitFlush(diff, true, diff != 0);
	}

	gstate.cmdmem[cmd] = op;
	MOV(32, MCmdState(cmd), R(opReg));

	if (cmdFlags & FLAG_ANY_EXECUTE) {
		FixupBranch changedSkip;
		if (!(cmdFlags & FLAG_EXECUTE)) {
			CMP(32, R(diffReg), Imm32(0));
			changedSkip = J_CC(CC_Z);
		}
		if (cmdFlags & FLAG_READS_PC) {
			JitStorePC();
		}
		gpu_->ExecuteOp(op, diff);
		ABI_CallFunctionPAA((const void *)&DoExecuteOp, gpu_, R(opReg), R(diffReg));
		if (cmdFlags & FLAG_WRITES_PC) {
			JitLoadPC();
		}
		if (!(cmdFlags & FLAG_EXECUTE)) {
			SetJumpTarget(changedSkip);
		}
	}
}

void DisplayListCache::Jit_Nop(u32 op) {
	CONDITIONAL_DISABLE;
	// Do nothing.
}

void DisplayListCache::Jit_Vaddr(u32 op) {
	CONDITIONAL_DISABLE;

	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);

	// TODO: Update cmdmem also?

	MOV(32, R(EAX), M(&gstate.base));
	AND(32, R(EAX), Imm32(0x000F0000));
	SHL(32, R(EAX), Imm8(8));
	MOV(32, R(EDX), R(opReg));
	AND(32, R(EDX), Imm32(0x00FFFFFF));
	OR(32, R(EAX), R(EDX));
	MOV(32, R(ECX), M(&gstate_c.offsetAddr));
	ADD(32, R(ECX), R(EAX));
	AND(32, R(ECX), Imm32(0x0FFFFFFF));
	MOV(32, M(&gstate_c.vertexAddr), R(ECX));
}

void DisplayListCache::Jit_Iaddr(u32 op) {
	CONDITIONAL_DISABLE;

	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);

	// TODO: Update cmdmem also?

	MOV(32, R(EAX), M(&gstate.base));
	AND(32, R(EAX), Imm32(0x000F0000));
	SHL(32, R(EAX), Imm8(8));
	MOV(32, R(EDX), R(opReg));
	AND(32, R(EDX), Imm32(0x00FFFFFF));
	OR(32, R(EAX), R(EDX));
	MOV(32, R(ECX), M(&gstate_c.offsetAddr));
	ADD(32, R(ECX), R(EAX));
	AND(32, R(ECX), Imm32(0x0FFFFFFF));
	MOV(32, M(&gstate_c.indexAddr), R(ECX));
}

void DisplayListCache::Jit_VertexType(u32 op) {
	CONDITIONAL_DISABLE;

	const u32 cmd = op >> 24;
	const u32 diff = op ^ gstate.cmdmem[cmd];

	if (g_Config.bSoftwareSkinning) {
		TEST(32, R(diffReg), Imm32(~GE_VTYPE_WEIGHTCOUNT_MASK));
		FixupBranch diffPassed = J_CC(CC_NZ);
		TEST(32, R(opReg), Imm32(GE_VTYPE_MORPHCOUNT_MASK));
		FixupBranch morphFailed = J_CC(CC_Z);

		SetJumpTarget(diffPassed);
		JitFlush(0, false, (diff & ~GE_VTYPE_WEIGHTCOUNT_MASK) != 0 || (op & GE_VTYPE_MORPHCOUNT_MASK) != 0);

		SetJumpTarget(morphFailed);
	} else {
		JitFlush(diff, true, diff != 0);
	}

	gstate.cmdmem[cmd] = op;
	MOV(32, MCmdState(cmd), R(opReg));

	TEST(32, R(diffReg), Imm32(GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK));
	FixupBranch skip = J_CC(CC_Z);
	JitDirtyUniform(DIRTY_UVSCALEOFFSET, (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK)) != 0);
	SetJumpTarget(skip);
}

void DisplayListCache::Jit_Prim(u32 op) {
	DISABLE;

	// TODO: Can maybe skip this part?
	const u32 cmd = op >> 24;
	gstate.cmdmem[cmd] = op;
	MOV(32, MCmdState(cmd), R(opReg));

	// TODO: Do better, we can combine vertexAddr updates.
	//gpu_->ExecutePrim(op);
	//ABI_CallFunctionPA((const void *)&DoExecutePrim, gpu_, R(opReg));
}
