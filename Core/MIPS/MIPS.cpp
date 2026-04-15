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

#include <cmath>
#include <limits>
#include <mutex>
#include <utility>
#include <cstring>


#include "Common/CommonTypes.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/Reporting.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/MemMap.h"
#if PPSSPP_ARCH(ARM64)
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Core/MIPS/x86/X64IRJit.h"
#endif

MIPSState mipsr4k;
MIPSState mipsMe;
MIPSState *currentMIPS = &mipsr4k;
MIPSDebugInterface debugr4k(&mipsr4k);
MIPSDebugInterface debugMe(&mipsMe);
MIPSDebugInterface *currentDebugMIPS = &debugr4k;

u8 voffset[128];
u8 fromvoffset[128];

#ifndef M_LOG2E
#define M_E        2.71828182845904523536f
#define M_LOG2E    1.44269504088896340736f
#define M_LOG10E   0.434294481903251827651f
#define M_LN2      0.693147180559945309417f
#define M_LN10     2.30258509299404568402f
#undef M_PI
#define M_PI       3.14159265358979323846f

#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923f
#endif
#define M_PI_4     0.785398163397448309616f
#define M_1_PI     0.318309886183790671538f
#define M_2_PI     0.636619772367581343076f
#define M_2_SQRTPI 1.12837916709551257390f
#define M_SQRT2    1.41421356237309504880f
#define M_SQRT1_2  0.707106781186547524401f
#endif

const float cst_constants[32] = {
	0,
	std::numeric_limits<float>::max(),  // all these are verified on real PSP
	sqrtf(2.0f),
	sqrtf(0.5f),
	2.0f/sqrtf((float)M_PI),
	2.0f/(float)M_PI,
	1.0f/(float)M_PI,
	(float)M_PI/4,
	(float)M_PI/2,
	(float)M_PI,
	(float)M_E,
	(float)M_LOG2E,
	(float)M_LOG10E,
	(float)M_LN2,
	(float)M_LN10,
	2*(float)M_PI,
	(float)M_PI/6,
	log10f(2.0f),
	logf(10.0f)/logf(2.0f),
	sqrtf(3.0f)/2.0f,
};

MIPSState::MIPSState() {
	MIPSComp::jit = nullptr;
	MIPSComp::mainCpuJit = nullptr;

	// Initialize vorder

	// This reordering of the VFPU registers in RAM means that instead of being like this:

	// 0x00 0x20 0x40 0x60 -> "columns", the most common direction
	// 0x01 0x21 0x41 0x61
	// 0x02 0x22 0x42 0x62
	// 0x03 0x23 0x43 0x63

	// 0x04 0x24 0x44 0x64
	// 0x06 0x26 0x45 0x65
	// ....

	// the VPU registers are effectively organized like this:
	// 0x00 0x01 0x02 0x03
	// 0x04 0x05 0x06 0x07
	// 0x08 0x09 0x0a 0x0b
	// ....

	// This is because the original indices look like this:
	// 0XXMMMYY where M is the matrix number.

	// We will now map 0YYMMMXX to 0MMMXXYY.

	// Advantages:
	// * Columns can be flushed and reloaded faster "at once"
	// * 4x4 Matrices are contiguous in RAM, making them, too, fast-loadable in NEON

	// Disadvantages:
	// * Extra indirection, can be confusing and slower (interpreter only, however we can often skip the table by rerranging formulas)
	// * Flushing and reloading row registers is now slower

	int i = 0;
	for (int m = 0; m < 8; m++) {
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				voffset[m * 4 + x * 32 + y] = i++;
			}
		}
	}

	// And the inverse.
	for (int i = 0; i < 128; i++) {
		fromvoffset[voffset[i]] = i;
	}

	// Sanity check that things that should be ordered are ordered.
	static const u8 firstThirtyTwo[] = {
		0x0, 0x20, 0x40, 0x60,
		0x1, 0x21, 0x41, 0x61,
		0x2, 0x22, 0x42, 0x62,
		0x3, 0x23, 0x43, 0x63,

		0x4, 0x24, 0x44, 0x64,
		0x5, 0x25, 0x45, 0x65,
		0x6, 0x26, 0x46, 0x66,
		0x7, 0x27, 0x47, 0x67,
	};

	for (int i = 0; i < (int)ARRAY_SIZE(firstThirtyTwo); i++) {
		if (voffset[firstThirtyTwo[i]] != i) {
			ERROR_LOG(Log::CPU, "Wrong voffset order! %i: %i should have been %i", firstThirtyTwo[i], voffset[firstThirtyTwo[i]], i);
		}
	}
}

static void ME_ShutdownIR();
static void ME_ShutdownNative();
static void ME_ResetState();

MIPSState::~MIPSState() {
}

void MIPSState::Shutdown() {
	// Only the main CPU owns the JIT. Media Engine has its own IR resources.
	if (this != &mipsr4k) {
		ME_ShutdownIR();
		ME_ShutdownNative();
		ME_ResetState();
		return;
	}
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	MIPSComp::JitInterface *oldjit = MIPSComp::jit;
	if (oldjit) {
		MIPSComp::jit = nullptr;
		MIPSComp::mainCpuJit = nullptr;
		delete oldjit;
	}
}

void MIPSState::Reset() {
	Shutdown();
	Init();
}

void MIPSState::Init() {
	memset(r, 0, sizeof(r));
	memset(f, 0, sizeof(f));
	memset(v, 0, sizeof(v));
	memset(vfpuCtrl, 0, sizeof(vfpuCtrl));

	vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
	vfpuCtrl[VFPU_CTRL_CC] = 0x3f;
	vfpuCtrl[VFPU_CTRL_INF4] = 0;
	vfpuCtrl[VFPU_CTRL_REV] = 0x7772ceab;
	vfpuCtrl[VFPU_CTRL_RCX0] = 0x3f800001;
	vfpuCtrl[VFPU_CTRL_RCX1] = 0x3f800002;
	vfpuCtrl[VFPU_CTRL_RCX2] = 0x3f800004;
	vfpuCtrl[VFPU_CTRL_RCX3] = 0x3f800008;
	vfpuCtrl[VFPU_CTRL_RCX4] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX5] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX6] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX7] = 0x3f800000;

	pc = 0;
	hi = 0;
	lo = 0;
	fpcond = 0;
	fcr31 = 0;
	debugCount = 0;
	currentMIPS = this;
	inDelaySlot = false;
	llBit = 0;
	nextPC = 0;
	downcount = 0;

	memset(vcmpResult, 0, sizeof(vcmpResult));

	// Only create JIT for the main CPU, not the Media Engine.
	if (this != &mipsr4k) {
		return;
	}
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (PSP_CoreParameter().cpuCore == CPUCore::JIT || PSP_CoreParameter().cpuCore == CPUCore::JIT_IR) {
		MIPSComp::jit = MIPSComp::CreateNativeJit(this, PSP_CoreParameter().cpuCore == CPUCore::JIT_IR);
	} else if (PSP_CoreParameter().cpuCore == CPUCore::IR_INTERPRETER) {
		MIPSComp::jit = new MIPSComp::IRJit(this, false);
	} else {
		MIPSComp::jit = nullptr;
	}
	MIPSComp::mainCpuJit = MIPSComp::jit;
}

bool MIPSState::HasDefaultPrefix() const {
	return vfpuCtrl[VFPU_CTRL_SPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_TPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_DPREFIX] == 0;
}

void MIPSState::UpdateCore(CPUCore desired) {
	if (PSP_CoreParameter().cpuCore == desired) {
		return;
	}

	IncrementDebugCounter(DebugCounter::CPUCORE_SWITCHES);

	// Get rid of the old JIT first, before switching.
	{
		std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
		if (MIPSComp::jit) {
			delete MIPSComp::jit;
			MIPSComp::jit = nullptr;
			MIPSComp::mainCpuJit = nullptr;
		}
	}

	PSP_CoreParameter().cpuCore = desired;

	MIPSComp::JitInterface *newjit = nullptr;
	switch (PSP_CoreParameter().cpuCore) {
	case CPUCore::JIT:
	case CPUCore::JIT_IR:
		INFO_LOG(Log::CPU, "Switching to JIT%s", PSP_CoreParameter().cpuCore == CPUCore::JIT_IR ? " IR" : "");
		newjit = MIPSComp::CreateNativeJit(this, PSP_CoreParameter().cpuCore == CPUCore::JIT_IR);
		break;

	case CPUCore::IR_INTERPRETER:
		INFO_LOG(Log::CPU, "Switching to IR interpreter");
		newjit = new MIPSComp::IRJit(this, false);
		break;

	case CPUCore::INTERPRETER:
		INFO_LOG(Log::CPU, "Switching to interpreter");
		// Leaving newjit as null.
		break;

	default:
		WARN_LOG(Log::CPU, "Invalid value for cpuCore, falling back to interpreter");
		break;
	}

	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	MIPSComp::jit = newjit;
	MIPSComp::mainCpuJit = newjit;
}

void MIPSState::DoState(PointerWrap &p) {
	auto s = p.Section("MIPSState", 1, 4);
	if (!s)
		return;

	// Reset the jit if we're loading.
	if (p.mode == p.MODE_READ)
		Reset();
	// Assume we're not saving state during a CPU core reset, so no lock.
	if (MIPSComp::jit)
		MIPSComp::jit->DoState(p);
	else
		MIPSComp::DoDummyJitState(p);

	DoArray(p, r, sizeof(r) / sizeof(r[0]));
	DoArray(p, f, sizeof(f) / sizeof(f[0]));
	if (s <= 2) {
		float vtemp[128];
		DoArray(p, vtemp, sizeof(v) / sizeof(v[0]));
		for (int i = 0; i < 128; i++) {
			v[voffset[i]] = vtemp[i];
		}
	} else {
		DoArray(p, v, sizeof(v) / sizeof(v[0]));
	}
	DoArray(p, vfpuCtrl, sizeof(vfpuCtrl) / sizeof(vfpuCtrl[0]));
	Do(p, pc);
	Do(p, nextPC);
	Do(p, downcount);
	// Reversed, but we can just leave it that way.
	Do(p, hi);
	Do(p, lo);
	Do(p, fpcond);
	if (s <= 1) {
		u32 fcr0_unused = 0;
		Do(p, fcr0_unused);
	}
	Do(p, fcr31);
	if (s <= 3) {
		uint32_t dummy;
		Do(p, dummy); // rng.m_w
		Do(p, dummy); // rng.m_z
	}

	Do(p, inDelaySlot);
	Do(p, llBit);
	Do(p, debugCount);

	if (p.mode == p.MODE_READ && MIPSComp::jit) {
		// Now that we've loaded fcr31, update any jit state associated.
		MIPSComp::jit->UpdateFCR31();
	}
}

void MIPSState::SingleStep() {
	int cycles = MIPS_SingleStep();
	currentMIPS->downcount -= cycles;
	CoreTiming::Advance();
}

// returns 1 if reached ticks limit
int MIPSState::RunLoopUntil(u64 globalTicks) {
	switch (PSP_CoreParameter().cpuCore) {
	case CPUCore::JIT:
	case CPUCore::JIT_IR:
	case CPUCore::IR_INTERPRETER:
		while (inDelaySlot) {
			// We must get out of the delay slot before going into jit.
			// This normally should never take more than one step...
			SingleStep();
		}
		insideJit = true;
		if (hasPendingClears)
			ProcessPendingClears();
		MIPSComp::jit->RunLoopUntil(globalTicks);
		insideJit = false;
		break;

	case CPUCore::INTERPRETER:
		return MIPSInterpret_RunUntil(globalTicks);
	}
	return 1;
}

// Kept outside MIPSState to avoid header pollution (MIPS.h doesn't even have vector, and is used widely.)
static std::vector<std::pair<u32, int>> pendingClears;

void MIPSState::ProcessPendingClears() {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	for (auto &p : pendingClears) {
		if (p.first == 0 && p.second == 0)
			MIPSComp::jit->ClearCache();
		else
			MIPSComp::jit->InvalidateCacheAt(p.first, p.second);
	}
	pendingClears.clear();
	hasPendingClears = false;
}

void MIPSState::InvalidateICache(u32 address, int length) {
	// Only really applies to jit.
	// Note that the backend is responsible for ensuring native code can still be returned to.
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit && length != 0) {
		MIPSComp::jit->InvalidateCacheAt(address, length);
	}
}

void MIPSState::ClearJitCache() {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit) {
		if (coreState == CORE_RUNNING_CPU || insideJit) {
			pendingClears.emplace_back(0, 0);
			hasPendingClears = true;
			CoreTiming::ForceCheck();
		} else {
			MIPSComp::jit->ClearCache();
		}
	}
}

// Media Engine state and scheduling.

static bool meEnabled = false;
static int meCoreSliceEvent = -1;

// Flag set by Int_Halt when a HALT instruction is executed inside a JIT block.
bool g_meHaltDetected = false;

// Used for debug drift accounting only.
static s64 meCyclesExecuted_ = 0;
static u32 meCountAccum_ = 0;  // fractional accumulator for CP0 Count

// Debug-only drift tracking between SC and ME.
#define ME_DEBUG_DRIFT 0
#if ME_DEBUG_DRIFT
static s64 driftMax_ = 0;
static s64 driftSum_ = 0;
static int driftSamples_ = 0;
#endif

static void ME_ResetState() {
	meEnabled = false;
	meCoreSliceEvent = -1;
	g_meHaltDetected = false;
	meCyclesExecuted_ = 0;
	meCountAccum_ = 0;
#if ME_DEBUG_DRIFT
	driftMax_ = 0;
	driftSum_ = 0;
	driftSamples_ = 0;
#endif
}

// Scheduling quantum in us while idle or spinwaiting.
static constexpr int ME_SLICE_US = 100;
static constexpr int ME_FREQ = 333000000;

// Minimal ME syscall trampoline for the ME kernel image range.
static void ME_HLE_DispatchSyscall(u32 pc) {
	mipsMe.pc = mipsMe.r[31]; // jr $ra
}

// Detect a spinwait pattern: the ME is sitting on a tight loop reading a
// shared-memory word that hasn't changed.  Pattern:
//   PC+0: LW  rt, offset(base)     [opcode 0x23 = LW]
//   PC+4: branch back to PC+0 (BEQ rt,$zero,-8  or  BNE/$zero variants)
// If the loaded register equals the comparison value (i.e. the branch is
// taken), this is a spinwait and we should yield.
static bool ME_DetectSpinwait(u32 pc, u32 opWord);

// Scan forward from a given PC, skipping cache/sync/nop instructions, looking
// for a LW+branch spinwait pattern within `maxInsns` instructions.
// Unlike ME_DetectSpinwait (which requires the branch to target the LW itself),
// this allows the branch to target any address <= startPC (backward branch),
// covering patterns like: sync; cache-loop; sync; lw; beqz -> outer_loop_start.
static bool ME_DetectSpinwaitScan(u32 startPC, int maxInsns = 16) {
	for (int i = 0; i < maxInsns; i++) {
		u32 scanPC = startPC + i * 4;
		if (!Memory::IsValidAddress(scanPC))
			return false;
		u32 op = Memory::Read_Instruction(scanPC, true).encoding;
		u32 opcode = op >> 26;
		// Skip NOP
		if (op == 0)
			continue;
		// Skip CACHE (opcode 0x2F)
		if (opcode == 0x2F)
			continue;
		// Skip SYNC
		if (op == 0x0000000F || (op & 0xFFFFF83F) == 0x0000000F)
			continue;
		// Skip MOVE (addu/or rd,$zero) and ADDIU (commonly part of cache loops)
		if (opcode == 0x00) {
			u32 func = op & 0x3F;
			// ADDU, OR with $zero source (= MOVE)
			if ((func == 0x21 || func == 0x25) && ((op >> 21) & 0x1F) == 0)
				continue;
		}
		if (opcode == 0x09) // ADDIU
			continue;
		// Skip BNE (inner cache loop branch)
		if (opcode == 0x05) {
			// Also skip its delay slot
			i++;
			continue;
		}
		// Found a LW - check for a relaxed spinwait pattern.
		if (opcode == 0x23) {
			u32 rt = (op >> 16) & 0x1F;
			if (rt == 0)
				return false;
			u32 base = (op >> 21) & 0x1F;
			s16 imm = (s16)(op & 0xFFFF);
			u32 addr = mipsMe.r[base] + imm;
			u32 memVal = Memory::Read_U32(addr);

			u32 nextAddr = scanPC + 4;
			if (!Memory::IsValidAddress(nextAddr))
				return false;
			u32 nextOp = Memory::Read_Instruction(nextAddr, true).encoding;
			u32 nextOpcode = nextOp >> 26;

			// BEQ rt,$zero,offset, waiting for nonzero.
			if (nextOpcode == 0x04) {
				u32 brt = (nextOp >> 21) & 0x1F;
				u32 brs = (nextOp >> 16) & 0x1F;
				s16 offset = (s16)(nextOp & 0xFFFF);
				u32 target = nextAddr + 4 + (offset << 2);
				if (target <= startPC && target >= startPC - (u32)(maxInsns * 4) && ((brt == rt && brs == 0) || (brt == 0 && brs == rt))) {
					if (memVal == 0)
						return true;
				}
			}
			// BNE rt,$zero,offset, waiting for zero.
			if (nextOpcode == 0x05) {
				u32 brt = (nextOp >> 21) & 0x1F;
				u32 brs = (nextOp >> 16) & 0x1F;
				s16 offset = (s16)(nextOp & 0xFFFF);
				u32 target = nextAddr + 4 + (offset << 2);
				if (target <= startPC && target >= startPC - (u32)(maxInsns * 4) && ((brt == rt && brs == 0) || (brt == 0 && brs == rt))) {
					if (memVal != 0)
						return true;
				}
			}
			return false;
		}
		// Any other instruction stops the scan.
		return false;
	}
	return false;
}

static bool ME_DetectSpinwait(u32 pc, u32 opWord) {
	// Is current instruction a LW?  opcode field = bits[31:26]
	u32 opcode = opWord >> 26;
	if (opcode != 0x23)  // LW
		return false;

	u32 rt = (opWord >> 16) & 0x1F;
	if (rt == 0)
		return false;

	// Compute the effective address of the LW to read fresh memory value.
	u32 base = (opWord >> 21) & 0x1F;
	s16 imm  = (s16)(opWord & 0xFFFF);
	u32 addr = mipsMe.r[base] + imm;
	u32 memVal = Memory::Read_U32(addr);

	// Read next instruction (the branch).
	u32 nextOp = Memory::Read_Instruction(pc + 4, true).encoding;
	u32 nextOpcode = nextOp >> 26;

	// BEQ rt, $zero, offset, branch back to pc.
	if (nextOpcode == 0x04) { // BEQ
		u32 brt = (nextOp >> 21) & 0x1F;
		u32 brs = (nextOp >> 16) & 0x1F;
		s16 offset = (s16)(nextOp & 0xFFFF);
		u32 target = (pc + 4) + (offset << 2) + 4; // MIPS branch delay: target = PC+4 + offset*4
		// Check: BEQ rt,$zero back to the LW  (or BEQ $zero,rt)
		if (target == pc) {
			if ((brt == rt && brs == 0) || (brt == 0 && brs == rt)) {
				// If the branch is still taken, this is a spinwait.
				if (memVal == 0)
					return true;
			}
		}
	}

	// BNEZ rt, offset, loop waiting for the value to become zero.
	if (nextOpcode == 0x05) { // BNE
		u32 brt = (nextOp >> 21) & 0x1F;
		u32 brs = (nextOp >> 16) & 0x1F;
		s16 offset = (s16)(nextOp & 0xFFFF);
		u32 target = (pc + 4) + (offset << 2) + 4;
		if (target == pc) {
			if ((brt == rt && brs == 0) || (brt == 0 && brs == rt)) {
				if (memVal != 0)
					return true;
			}
		}
	}

	return false;
}

static int meSliceCount_ = 0;
static int meInsnCount_ = 0;
static int meNativeInterpreterCountdown_ = 0;
static constexpr int ME_NATIVE_WARMUP_INSNS = 20000;

static constexpr int ME_NATIVE_EXCEPTION_INSNS = 2048;

static bool ME_ShouldInterpretAtPC(u32 pc) {
	// Previously forced all ME code to the interpreter.  Now that IR's
	// Comp_Generic terminates blocks on ERET/HALT, the IR backend can
	// compile ME code.  MFC0/MTC0/cache fall through to IROp::Interpret
	// which is safe for non-control-flow instructions.
	(void)pc;
	return false;
}

// Check if the ME should take an interrupt and deliver it if so.
// Returns true if an interrupt was delivered (PC has been redirected to the exception vector).
static bool ME_CheckAndDeliverInterrupt() {
	// Update hardware interrupt lines in Cause.  Per MIPS spec, Cause.IP[7:2]
	// are read-only bits that reflect the current state of hardware lines.

	u32 cause = mipsMe.cp0regs[13];

	// IP7: timer interrupt when Count >= Compare.
	if (mipsMe.cp0regs[11] != 0 && mipsMe.cp0regs[9] >= mipsMe.cp0regs[11]) {
		cause |= 0x8000;   // Cause.IP7
	}

	// IP2: External soft interrupt from SC or ME pending flag.
	if (Memory::ME_HasPendingInterrupt()) {
		cause |= 0x0400;   // Cause.IP2
	} else {
		cause &= ~0x0400;  // Clear IP2 when hardware line is deasserted
	}

	mipsMe.cp0regs[13] = cause;

	// Any pending and enabled interrupt?
	u32 status = mipsMe.cp0regs[12];
	u32 pendingAndEnabled = (cause & status) & 0xFF00;  // IP[7:0] & IM[7:0]
	if (!pendingAndEnabled)
		return false;

	// Check: interrupts globally enabled (IE=bit0), not in exception (EXL=bit1).
	if (!(status & 0x01))  // IE not set
		return false;
	if (status & 0x02)     // EXL set, already handling an exception.
		return false;

	// Deliver the interrupt:
	mipsMe.cp0regs[14] = mipsMe.pc;               // EPC = current PC
	mipsMe.cp0regs[12] |= 0x02;                   // Status.EXL = 1
	mipsMe.pc = mipsMe.cp0regs[25];               // Jump to EBase (exception vector)
	mipsMe.inDelaySlot = false;
	// Clear external soft interrupt if that's what triggered us.
	if (cause & 0x0400) {
		Memory::ME_ClearSoftInterrupt();
	}
	if (meNativeInterpreterCountdown_ < ME_NATIVE_EXCEPTION_INSNS)
		meNativeInterpreterCountdown_ = ME_NATIVE_EXCEPTION_INSNS;
	return true;
}

static int MEInterpret_RunSlice(int budget) {
	MIPSState *saved = currentMIPS;
	currentMIPS = &mipsMe;
	meSliceCount_++;
	int startBudget = budget;

	// Check for pending interrupts once at slice entry (not per-instruction).
	ME_CheckAndDeliverInterrupt();

	while (budget > 0 && meEnabled) {
		u32 pc = mipsMe.pc;

		// Bail out if PC is not valid (e.g. ran off the end of ME SRAM).
		if (!Memory::IsValidAddress(pc)) {
			WARN_LOG(Log::CPU, "ME interp: invalid PC %08x, stopping", pc);
			meEnabled = false;
			break;
		}

		meInsnCount_++;

		// Check for ME kernel syscall dispatch (0x88300000-0x883FFFFF)
		u32 phys = pc & 0x1FFFFFFF;
		if (phys >= 0x08300000 && phys < 0x08400000) {
			ME_HLE_DispatchSyscall(pc);
			budget -= 10;
			continue;
		}

		// Resolve replacements so ME never sees EMUHACK opcodes.
		u32 opWord = Memory::Read_Instruction(pc, true).encoding;

		// HALT instruction (0x70000000)
		if (opWord == 0x70000000) {
			meEnabled = false;
			break;
		}

		// Spinwait detection: yield remaining budget if the ME is busy-waiting
		// on a shared-memory flag that the main CPU hasn't set yet.
		if (ME_DetectSpinwait(pc, opWord)) {
			break;
		}

		bool isNop = false;

		// CACHE instruction (opcode field = 0x2F)
		if ((opWord >> 26) == 0x2F) {
			isNop = true;
		}

		// SYNC instruction
		if (opWord == 0x0000000F || (opWord & 0xFFFFF83F) == 0x0000000F) {
			isNop = true;
		}

		// MTC0/MFC0 (COP0): handle CP0 register access
		if ((opWord >> 26) == 0x10) { // COP0
			u32 rs = (opWord >> 21) & 0x1F;
			u32 rt = (opWord >> 16) & 0x1F;
			u32 rd = (opWord >> 11) & 0x1F;
			if (rs == 0x04) { // MTC0: rt -> cp0[rd]
				mipsMe.cp0regs[rd] = mipsMe.r[rt];
				// Per MIPS spec, writing to Compare (reg 11) clears Cause.IP7.
				if (rd == 11) {
					mipsMe.cp0regs[13] &= ~0x8000;
				}
				isNop = true;
			} else if (rs == 0x00) { // MFC0: cp0[rd] -> rt
				if (rt != 0)
					mipsMe.r[rt] = mipsMe.cp0regs[rd];
				isNop = true;
			} else if (rs == 0x10 && (opWord & 0x3F) == 0x18) {
				// ERET: Return from exception.
				// encoding: 0x42000018 (COP0 CO=1, func=0x18)
				mipsMe.pc = mipsMe.cp0regs[14];  // PC = EPC
				mipsMe.cp0regs[12] &= ~0x02;     // Clear Status.EXL
				mipsMe.inDelaySlot = false;
				budget--;
				continue;  // Skip normal PC advance
			}
		}

		bool wasInDelaySlot = mipsMe.inDelaySlot;

		if (isNop) {
			mipsMe.pc += 4;
		} else {
			// Normal MIPS instruction - use standard interpreter
			MIPSOpcode op(opWord);

			MIPSInterpret(op);

			// Fixup: DelayBranchTo/SkipLikely use mipsr4k directly instead of currentMIPS
			if (mipsr4k.inDelaySlot && !mipsMe.inDelaySlot) {
				mipsMe.inDelaySlot = true;
				mipsMe.nextPC = mipsr4k.nextPC;
				mipsr4k.inDelaySlot = false;
			}
		}

		if (mipsMe.inDelaySlot && wasInDelaySlot) {
			mipsMe.pc = mipsMe.nextPC;
			mipsMe.inDelaySlot = false;
		}

		// CP0 Count register (reg 9).
		// On real PSP, Count ticks at CPU_CLOCK/2.  The bench's me_cycles_to_us()
		// formula is: cycles * 2 / 333.  For the ME timing to be consistent with
		// the SC's CoreTiming model (which counts 1 cycle per instruction at
		// the current clock), we scale Count so:
		//   count * 2 / 333 == insns / (currentClockMHz)
		//   count/insn = 333 / (2 * currentClockMHz)
		// At 333 MHz: 0.5 per insn.  At 222 MHz: 0.75 per insn.
		// Use a fractional accumulator for precision.
		{
			meCountAccum_ += 333;
			int clockMHz = CoreTiming::GetClockFrequencyHz() / 1000000;
			if (clockMHz < 100) clockMHz = 333;  // safety
			u32 inc = meCountAccum_ / (2 * clockMHz);
			meCountAccum_ %= (2 * clockMHz);
			mipsMe.cp0regs[9] += inc;
		}

		budget--;
	}

	currentMIPS = saved;
	return startBudget - budget;
}

// ME IR interpreter backend.
// ME blocks compile to IR without patching RAM.

static MIPSComp::IRFrontend *meFrontend_ = nullptr;
static MIPSComp::IRBlockCache *meBlocks_ = nullptr;

static void ME_InitIR() {
	if (meFrontend_)
		return;
	InitIR();
	meFrontend_ = new MIPSComp::IRFrontend(true);  // defaultPrefix = true
	meBlocks_ = new MIPSComp::IRBlockCache(false, false);  // compileToNative=false, patchMemory=false

	// Set up IR options for interpreter mode.
	IROptions opts{};
	opts.optimizeForInterpreter = true;
	meFrontend_->SetOptions(opts);
}

static void ME_ShutdownIR() {
	delete meBlocks_;
	meBlocks_ = nullptr;
	delete meFrontend_;
	meFrontend_ = nullptr;
}

static int ME_IRRunSlice(int budget) {
	MIPSState *saved = currentMIPS;
	currentMIPS = &mipsMe;
	int startBudget = budget;

	// Use the plain interpreter for kernel and exception paths.
	if (ME_ShouldInterpretAtPC(mipsMe.pc)) {
		int executed = MEInterpret_RunSlice(budget);
		currentMIPS = saved;
		return executed;
	}

	ME_InitIR();

	while (budget > 0 && meEnabled) {
		// Check for pending interrupts at block boundaries.
		// Only check when a soft interrupt might actually be pending.
		if (Memory::ME_HasPendingInterrupt())
			ME_CheckAndDeliverInterrupt();

		u32 pc = mipsMe.pc;

		// Bail out if PC is not valid (e.g. jr $ra with $ra=0).
		if (!Memory::IsValidAddress(pc)) {
			WARN_LOG(Log::CPU, "ME IR: invalid PC %08x, stopping", pc);
			meEnabled = false;
			break;
		}

		// Check for ME kernel syscall dispatch (0x88300000-0x883FFFFF)
		u32 phys = pc & 0x1FFFFFFF;
		if (phys >= 0x08300000 && phys < 0x08400000) {
			ME_HLE_DispatchSyscall(pc);
			budget -= 10;
			continue;
		}

		// Fetch first opcode of block to check for ME-specific instructions.
		u32 opWord = Memory::Read_Instruction(pc, true).encoding;

		// HALT stops ME execution.
		if (opWord == 0x70000000) {
			meEnabled = false;
			break;
		}

		// Check for spinwaits before compiling a block.
		if (ME_DetectSpinwait(pc, opWord) || ME_DetectSpinwaitScan(pc)) {
			break;
		}

		// Handle ERET at block boundaries.
		if (opWord == 0x42000018) {
			mipsMe.pc = mipsMe.cp0regs[14];  // PC = EPC
			mipsMe.cp0regs[12] &= ~0x02;     // Clear Status.EXL
			mipsMe.inDelaySlot = false;
			budget--;
			continue;
		}

		// Look up an existing IR block for this address.
		int blockNum = meBlocks_->FindPreloadBlock(pc);
		if (blockNum < 0) {
			// Compile a new block.
			std::vector<IRInst> instructions;
			u32 mipsBytes = 0;
			meFrontend_->DoJit(pc, instructions, mipsBytes);
			if (instructions.empty()) {
				// Compilation failed, single-step instead.
				MIPSOpcode op(opWord);
				MIPSInterpret(op);
				budget--;
				continue;
			}
			blockNum = meBlocks_->AllocateBlock(pc, mipsBytes, instructions);
			if (blockNum < 0) {
				// Arena full, clear and retry.
				meBlocks_->Clear();
				blockNum = meBlocks_->AllocateBlock(pc, mipsBytes, instructions);
			}
			if (blockNum >= 0) {
				meBlocks_->FinalizeBlock(blockNum);
			} else {
				// Still failed, single-step.
				MIPSOpcode op(opWord);
				MIPSInterpret(op);
				budget--;
				continue;
			}
		}

		// Execute the IR block.
		const MIPSComp::IRBlock *block = meBlocks_->GetBlock(blockNum);
		const IRInst *instPtr = meBlocks_->GetBlockInstructionPtr(*block);

		// The IR block's Downcount instruction decrements mips->downcount
		// internally.  Set it to our budget before execution, then read
		// back the remaining amount to compute the actual block cost.
		mipsMe.downcount = budget;

		u32 newPC = IRInterpret(&mipsMe, instPtr);

		// Fixup: MIPSInterpret (called via IROp::Interpret fallback) may
		// modify mipsr4k instead of mipsMe for delay slot handling.
		if (mipsr4k.inDelaySlot && !mipsMe.inDelaySlot) {
			mipsMe.inDelaySlot = true;
			mipsMe.nextPC = mipsr4k.nextPC;
			mipsr4k.inDelaySlot = false;
		}

		mipsMe.pc = newPC;
		int blockCost = budget - mipsMe.downcount;
		if (blockCost < 1) blockCost = 1;  // safety: always consume at least 1
		budget = mipsMe.downcount;

		// Update CP0 Count with the same scaling used by the interpreter.
		{
			meCountAccum_ += (s64)blockCost * 333;
			int clockMHz = CoreTiming::GetClockFrequencyHz() / 1000000;
			if (clockMHz < 100) clockMHz = 333;
			u32 inc = (u32)(meCountAccum_ / (2 * clockMHz));
			meCountAccum_ %= (2 * clockMHz);
			mipsMe.cp0regs[9] += inc;
		}

		// Check if we landed on HALT after the block.
		if (Memory::IsValidAddress(mipsMe.pc)) {
			u32 nextOp = Memory::Read_Instruction(mipsMe.pc, true).encoding;
			if (nextOp == 0x70000000) {
				meEnabled = false;
				break;
			}
		}
	}

	currentMIPS = saved;
	return startBudget - budget;
}
// Native ME backend. ME code is compiled without patching RAM.

static MIPSComp::JitInterface *meJit_ = nullptr;

MIPSComp::JitInterface *ME_GetJit() {
	return meJit_;
}

static void ME_InitNative() {
	if (meJit_)
		return;
#if PPSSPP_ARCH(ARM64)
	meJit_ = new MIPSComp::Arm64MEIRJit(&mipsMe);
#else
	// Fallback: other architectures not yet supported for ME native JIT.
	return;
#endif
}

static void ME_ShutdownNative() {
	if (meJit_) {
		delete meJit_;
		meJit_ = nullptr;
	}
}

// Called from generated ME dispatcher code.
const u8 *MECompileAndLookup() {
#if PPSSPP_ARCH(ARM64)
	u32 pc = currentMIPS->pc;
	u32 phys = pc & 0x1FFFFFFF;

	// HLE syscall range: bail to the C++ wrapper.
	if (phys >= 0x08300000 && phys < 0x08400000) {
		currentMIPS->downcount = -1;  // Force dispatcher exit
		return nullptr;
	}

	// HALT instruction.
	if (Memory::IsValidAddress(pc)) {
		u32 opWord = Memory::Read_Instruction(pc, true).encoding;
		if (opWord == 0x70000000) {
			g_meHaltDetected = true;
			currentMIPS->downcount = -1;
			return nullptr;
		}
	}

	auto *meNativeJit = static_cast<MIPSComp::Arm64MEIRJit *>(meJit_);
	const u8 *result = meNativeJit->CompileAndLookup(pc);
	return result;
#else
	return nullptr;
#endif
}

static int ME_NativeRunSlice(int budget) {
#if PPSSPP_ARCH(ARM64)
	ME_InitNative();
	if (!meJit_)
		return ME_IRRunSlice(budget);

	MIPSState *saved = currentMIPS;
	currentMIPS = &mipsMe;

	int startBudget = budget;

	// Pre-dispatch: deliver pending interrupts.
	ME_CheckAndDeliverInterrupt();

	if (!meEnabled) {
		currentMIPS = saved;
		return 0;
	}

	// Pre-dispatch: check for HALT/spinwait at current PC.
	if (Memory::IsValidAddress(mipsMe.pc)) {
		u32 opWord = Memory::Read_Instruction(mipsMe.pc, true).encoding;
		if (opWord == 0x70000000) {
			meEnabled = false;
			currentMIPS = saved;
			return 0;
		}
		if (ME_DetectSpinwait(mipsMe.pc, opWord) || ME_DetectSpinwaitScan(mipsMe.pc)) {
			currentMIPS = saved;
			return 0;
		}
	} else {
		meEnabled = false;
		currentMIPS = saved;
		return 0;
	}

	// Run native blocks until downcount < 0.
	mipsMe.downcount = budget;
	auto *meNativeJit = static_cast<MIPSComp::Arm64MEIRJit *>(meJit_);

	meNativeJit->EnterDispatcher();

	// Compute consumed budget.
	int consumed = startBudget - mipsMe.downcount;
	if (consumed < 1) consumed = 1;

	// Update CP0 Count with the same scaling used elsewhere.
	{
		meCountAccum_ += (s64)consumed * 333;
		int clockMHz = CoreTiming::GetClockFrequencyHz() / 1000000;
		if (clockMHz < 100) clockMHz = 333;
		u32 inc = (u32)(meCountAccum_ / (2 * clockMHz));
		meCountAccum_ %= (2 * clockMHz);
		mipsMe.cp0regs[9] += inc;
	}

	// Check if HALT was detected during block lookup.
	if (g_meHaltDetected) {
		g_meHaltDetected = false;
		meEnabled = false;
	}

	currentMIPS = saved;
	return consumed;
#else
	return ME_IRRunSlice(budget);
#endif
}

static void MECallback(u64 userdata, int cyclesLate) {
	if (!meEnabled) {
		// ME not booted.  Core_EnableME() will schedule a new event
		// when the main CPU enables the ME.
		return;
	}

	// ME budget per slice, scaled to the current CPU clock frequency.
	int meBudget = (int)(CoreTiming::GetClockFrequencyHz() / (1000000 / ME_SLICE_US));
	if (meBudget < 1000) meBudget = 1000;

	// Cap the interpreter budget: the plain interpreter runs at ~50ns/insn on
	// the host, so a budget of 33300 would block the SC for ~1.6ms per slice.
	// IR/JIT backends are fast enough to handle the full clock-scaled budget.
	static constexpr int ME_MAX_INTERP_BUDGET = 5000;
	if ((CPUCore)g_Config.iMECpuCore == CPUCore::INTERPRETER && meBudget > ME_MAX_INTERP_BUDGET)
		meBudget = ME_MAX_INTERP_BUDGET;

	// Deliver any pending SC->ME soft interrupt before the slice.
	u32 softIntBefore = Memory::ME_PeekSoftInterruptRaw();
	if (softIntBefore != 0)
		Memory::ME_RaiseSoftInterrupt();

	int executed = 0;

	switch ((CPUCore)g_Config.iMECpuCore) {
	case CPUCore::IR_INTERPRETER:
		executed = ME_IRRunSlice(meBudget);
		break;
	case CPUCore::JIT:
	case CPUCore::JIT_IR:
		executed = ME_NativeRunSlice(meBudget);
		break;
	default:
		executed = MEInterpret_RunSlice(meBudget);
		break;
	}

	// Update ME virtual-time position.
	meCyclesExecuted_ += executed;

#if ME_DEBUG_DRIFT
	{
		s64 scCycles = CoreTiming::GetTicks();
		s64 drift = scCycles - meCyclesExecuted_;
		if (drift < 0) drift = -drift;
		if (drift > driftMax_) driftMax_ = drift;
		driftSum_ += drift;
		driftSamples_++;
		if ((driftSamples_ % 10000) == 0) {
			int clockMHz = CoreTiming::GetClockFrequencyHz() / 1000000;
			INFO_LOG(Log::CPU, "ME drift: max=%lld avg=%lld cycles (%d samples, %d MHz)",
				driftMax_, driftSamples_ ? driftSum_ / driftSamples_ : 0,
				driftSamples_, clockMHz);
		}
	}
#endif

	// ME->SC soft interrupt: defer delivery until after the ME slice so the
	// main CPU state is active again.
	u32 softIntAfter = Memory::ME_PeekSoftInterruptRaw();
	if (Memory::ME_ConsumeCpuInterruptRequest() || (softIntBefore == 0 && softIntAfter != 0)) {
		__TriggerInterrupt(PSP_INTR_IMMEDIATE, PSP_MECODEC_INTR, PSP_INTR_SUB_NONE);
		Memory::ME_ClearSoftInterrupt();
	}

	// Schedule next event only if ME is still running.  When the ME is in a
	// spinwait (executed == 0), use a longer interval to reduce host overhead.
	if (meEnabled) {
		static constexpr int ME_BACKOFF_US = 1000;  // 1ms when idle
		int nextUs = (executed == 0) ? ME_BACKOFF_US : ME_SLICE_US;
		CoreTiming::ScheduleEvent(usToCycles(nextUs), meCoreSliceEvent, 0);
	}
}

void Core_EnableME() {
	if (meEnabled) return;

	// ME disabled via config (iMECpuCore == -1)
	if (g_Config.iMECpuCore < 0) return;

	mipsMe.pc = 0xBFC00000;
	mipsMe.r[0] = 0;
	mipsMe.r[28] = mipsr4k.r[28];  // GP: copy from main CPU so ME can access globals
	mipsMe.r[29] = 0x80014000;  // SP: top of scratchpad (kseg0 cached view of 0x00014000)
	mipsMe.inDelaySlot = false;
	mipsMe.nextPC = 0;
	memset(mipsMe.cp0regs, 0, sizeof(mipsMe.cp0regs));
	mipsMe.cp0regs[22] = 2;  // Processor ID: ME = 2 (bit[1]), main CPU = 1 (bit[0]) for HW mutex

	meEnabled = true;
	meSliceCount_ = 0;
	meInsnCount_ = 0;
	meCyclesExecuted_ = CoreTiming::GetTicks();  // Sync ME start to current SC position
	meCountAccum_ = 0;
	meNativeInterpreterCountdown_ = ME_NATIVE_WARMUP_INSNS;

	// Reset interrupt state for a fresh ME boot.
	Memory::ME_ResetInterruptState();

	// Make HW register page read-write so the spinlock (0xBC100048)
	// and subsequent JIT accesses go through without faulting.
	Memory::ME_ProtectHwPage(false);

	INFO_LOG(Log::CPU, "ME: Core_EnableME called, PC=%08x, core=%d", mipsMe.pc, g_Config.iMECpuCore);

	// Schedule the first ME slice event.  MECallback no longer self-schedules
	// when !meEnabled, so we must kick-start it here.
	if (meCoreSliceEvent == -1) {
		meCoreSliceEvent = CoreTiming::RegisterEvent("meCoreSlice", MECallback);
	}
	CoreTiming::ScheduleEvent(usToCycles(ME_SLICE_US), meCoreSliceEvent, 0);
}

void ME_InitPolling() {
	if (g_Config.iMECpuCore < 0)
		return;
	if (meCoreSliceEvent == -1) {
		meCoreSliceEvent = CoreTiming::RegisterEvent("meCoreSlice", MECallback);
	}
	// Core_EnableME() schedules the first event.
	// when the main CPU enables the ME.  This avoids wasting CoreTiming
	// overhead on a disabled ME.
	INFO_LOG(Log::CPU, "ME: Event registered (core=%d, slice=%dus)", g_Config.iMECpuCore, ME_SLICE_US);
}
