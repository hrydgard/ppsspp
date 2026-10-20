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

#include <atomic>
#include <climits>
#include <thread>
#include <algorithm>
#include "Common/File/FileUtil.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/IR/IRNativeCommon.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitProfiler.h"

using namespace MIPSComp;

namespace MIPSComp {

// Compile time flag to enable debug stats for not compiled ops.
static constexpr bool enableDebugStats = false;
// Compile time flag for enabling the simple IR jit profiler.
static constexpr bool enableDebugProfiler = false;

// Used only for debugging when enableDebug is true above.
static std::map<uint8_t, int> debugSeenNotCompiledIR;
static std::map<const char *, int> debugSeenNotCompiled;
static std::map<std::pair<uint32_t, IRProfilerStatus>, int> debugSeenPCUsage;
static double lastDebugStatsLog = 0.0;
static constexpr double debugStatsFrequency = 5.0;

static std::thread debugProfilerThread;
std::atomic<bool> debugProfilerThreadStatus = false;

template <int N>
class IRProfilerTopValues {
public:
	void Add(const std::pair<uint32_t, IRProfilerStatus> &v, int c) {
		for (int i = 0; i < N; ++i) {
			if (c > counts[i]) {
				counts[i] = c;
				values[i] = v;
				return;
			}
		}
	}

	int counts[N]{};
	std::pair<uint32_t, IRProfilerStatus> values[N]{};
};

const char *IRProfilerStatusToString(IRProfilerStatus s) {
	switch (s) {
	case IRProfilerStatus::NOT_RUNNING: return "NOT_RUNNING";
	case IRProfilerStatus::IN_JIT: return "IN_JIT";
	case IRProfilerStatus::TIMER_ADVANCE: return "TIMER_ADVANCE";
	case IRProfilerStatus::COMPILING: return "COMPILING";
	case IRProfilerStatus::MATH_HELPER: return "MATH_HELPER";
	case IRProfilerStatus::REPLACEMENT: return "REPLACEMENT";
	case IRProfilerStatus::SYSCALL: return "SYSCALL";
	case IRProfilerStatus::INTERPRET: return "INTERPRET";
	case IRProfilerStatus::IR_INTERPRET: return "IR_INTERPRET";
	}
	return "INVALID";
}

static void LogDebugStats() {
	if (!enableDebugStats && !enableDebugProfiler)
		return;

	double now = time_now_d();
	if (now < lastDebugStatsLog + debugStatsFrequency)
		return;
	lastDebugStatsLog = now;

	int worstIROp = -1;
	int worstIRVal = 0;
	for (auto it : debugSeenNotCompiledIR) {
		if (it.second > worstIRVal) {
			worstIRVal = it.second;
			worstIROp = it.first;
		}
	}
	debugSeenNotCompiledIR.clear();

	const char *worstName = nullptr;
	int worstVal = 0;
	for (auto it : debugSeenNotCompiled) {
		if (it.second > worstVal) {
			worstVal = it.second;
			worstName = it.first;
		}
	}
	debugSeenNotCompiled.clear();

	IRProfilerTopValues<4> slowestPCs;
	int64_t totalCount = 0;
	for (auto it : debugSeenPCUsage) {
		slowestPCs.Add(it.first, it.second);
		totalCount += it.second;
	}
	debugSeenPCUsage.clear();

	if (worstIROp != -1)
		WARN_LOG(Log::JIT, "Most not compiled IR op: %s (%d)", GetIRMeta((IROp)worstIROp)->name, worstIRVal);
	if (worstName != nullptr)
		WARN_LOG(Log::JIT, "Most not compiled op: %s (%d)", worstName, worstVal);
	if (slowestPCs.counts[0] != 0) {
		for (int i = 0; i < 4; ++i) {
			uint32_t pc = slowestPCs.values[i].first;
			const char *status = IRProfilerStatusToString(slowestPCs.values[i].second);
			const std::string label = g_symbolMap ? g_symbolMap->GetDescription(pc) : "";
			WARN_LOG(Log::JIT, "Slowest sampled PC #%d: %08x (%s)/%s (%f%%)", i, pc, label.c_str(), status, 100.0 * (double)slowestPCs.counts[i] / (double)totalCount);
		}
	}
}

bool IRNativeBackend::DebugStatsEnabled() const {
	return enableDebugStats;
}

bool IRNativeBackend::DebugProfilerEnabled() const {
	return enableDebugProfiler;
}

void IRNativeBackend::NotifyMIPSInterpret(const char *name) {
	_assert_(enableDebugStats);
	debugSeenNotCompiled[name]++;
}

void IRNativeBackend::DoMIPSInst(uint32_t value, MIPSState *mips) {
	MIPSOpcode op;
	memcpy(&op, &value, sizeof(op));

	if constexpr (enableDebugStats)
		debugSeenNotCompiled[MIPSGetName(op)]++;

	MIPSInterpret(mips, op);
}

// This is called from IR->JIT implementation to fall back to the IR interpreter for missing ops.
// Not fast.
uint32_t IRNativeBackend::DoIRInst(uint64_t value, MIPSState *mips) {
	IRInst inst[2]{};
	memcpy(&inst[0], &value, sizeof(value));
	if constexpr (enableDebugStats)
		debugSeenNotCompiledIR[(uint8_t)inst[0].op]++;
	// Doesn't really matter what value it returns as PC.
	inst[1].op = IROp::ExitToPC;
	return IRInterpret(mips, &inst[0]);
}

int IRNativeBackend::ReportBadAddress(uint32_t addr, uint32_t alignment, uint32_t isWrite, MIPSState *mips) {
	const auto toss = [&](MemoryExceptionType t) {
		Core_MemoryException(addr, alignment, mips->pc, t);
		return coreState != CORE_RUNNING_CPU ? 1 : 0;
	};

	if (!Memory::IsValidRange(addr, alignment)) {
		MemoryExceptionType t = isWrite == 1 ? MemoryExceptionType::WRITE_WORD : MemoryExceptionType::READ_WORD;
		if (alignment > 4)
			t = isWrite ? MemoryExceptionType::WRITE_BLOCK : MemoryExceptionType::READ_BLOCK;
		return toss(t);
	} else if (alignment > 1 && (addr & (alignment - 1)) != 0) {
		return toss(MemoryExceptionType::ALIGNMENT);
	}
	return 0;
}


IRNativeBackend::~IRNativeBackend() {
	if (debugProfilerThreadStatus) {
		debugProfilerThreadStatus = false;
		debugProfilerThread.join();
	}
}

void IRNativeBackend::CompileIRInst(IRInst inst) {
	switch (inst.op) {
	case IROp::Nop:
		break;

	case IROp::SetConst:
	case IROp::SetConstF:
	case IROp::Downcount:
	case IROp::SetPC:
	case IROp::SetPCConst:
		CompIR_Basic(inst);
		break;

	case IROp::Add:
	case IROp::Sub:
	case IROp::AddConst:
	case IROp::SubConst:
	case IROp::Neg:
		CompIR_Arith(inst);
		break;

	case IROp::And:
	case IROp::Or:
	case IROp::Xor:
	case IROp::AndConst:
	case IROp::OrConst:
	case IROp::XorConst:
	case IROp::Not:
		CompIR_Logic(inst);
		break;

	case IROp::Mov:
	case IROp::Ext8to32:
	case IROp::Ext16to32:
		CompIR_Assign(inst);
		break;

	case IROp::ReverseBits:
	case IROp::BSwap16:
	case IROp::BSwap32:
	case IROp::Clz:
		CompIR_Bits(inst);
		break;

	case IROp::Shl:
	case IROp::Shr:
	case IROp::Sar:
	case IROp::Ror:
	case IROp::ShlImm:
	case IROp::ShrImm:
	case IROp::SarImm:
	case IROp::RorImm:
		CompIR_Shift(inst);
		break;

	case IROp::Slt:
	case IROp::SltConst:
	case IROp::SltU:
	case IROp::SltUConst:
		CompIR_Compare(inst);
		break;

	case IROp::MovZ:
	case IROp::MovNZ:
	case IROp::Max:
	case IROp::Min:
		CompIR_CondAssign(inst);
		break;

	case IROp::MtLo:
	case IROp::MtHi:
	case IROp::MfLo:
	case IROp::MfHi:
		CompIR_HiLo(inst);
		break;

	case IROp::Mult:
	case IROp::MultU:
	case IROp::Madd:
	case IROp::MaddU:
	case IROp::Msub:
	case IROp::MsubU:
		CompIR_Mult(inst);
		break;

	case IROp::Div:
	case IROp::DivU:
		CompIR_Div(inst);
		break;

	case IROp::Load16:
	case IROp::Load16Ext:
		// 16-bit accesses can go through the depth layout (Memory::DepthMirrored16).
		if (Memory::DepthMirrorsActive())
			CompIR_Generic(inst);
		else
			CompIR_Load(inst);
		break;

	case IROp::Load8:
	case IROp::Load8Ext:
	case IROp::Load32:
	case IROp::Load32Linked:
		CompIR_Load(inst);
		break;

	case IROp::Load32Left:
	case IROp::Load32Right:
		CompIR_LoadShift(inst);
		break;

	case IROp::LoadFloat:
		CompIR_FLoad(inst);
		break;

	case IROp::LoadVec4:
		CompIR_VecLoad(inst);
		break;

	case IROp::Store16:
		if (Memory::DepthMirrorsActive())
			CompIR_Generic(inst);
		else
			CompIR_Store(inst);
		break;

	case IROp::Store8:
	case IROp::Store32:
		CompIR_Store(inst);
		break;

	case IROp::Store32Conditional:
		CompIR_CondStore(inst);
		break;

	case IROp::Store32Left:
	case IROp::Store32Right:
		CompIR_StoreShift(inst);
		break;

	case IROp::StoreFloat:
		CompIR_FStore(inst);
		break;

	case IROp::StoreVec4:
		CompIR_VecStore(inst);
		break;

	case IROp::FAdd:
	case IROp::FSub:
	case IROp::FMul:
	case IROp::FDiv:
	case IROp::FSqrt:
	case IROp::FNeg:
		CompIR_FArith(inst);
		break;

	case IROp::FMin:
	case IROp::FMax:
		CompIR_FCondAssign(inst);
		break;

	case IROp::FMov:
	case IROp::FAbs:
	case IROp::FSign:
		CompIR_FAssign(inst);
		break;

	case IROp::FRound:
	case IROp::FTrunc:
	case IROp::FCeil:
	case IROp::FFloor:
		CompIR_FRound(inst);
		break;

	case IROp::FCvtWS:
	case IROp::FCvtSW:
	case IROp::FCvtScaledWS:
	case IROp::FCvtScaledSW:
		CompIR_FCvt(inst);
		break;

	case IROp::FSat0_1:
	case IROp::FSatMinus1_1:
		CompIR_FSat(inst);
		break;

	case IROp::FCmp:
	case IROp::FCmovVfpuCC:
	case IROp::FCmpVfpuBit:
	case IROp::FCmpVfpuAggregate:
		CompIR_FCompare(inst);
		break;

	case IROp::RestoreRoundingMode:
	case IROp::ApplyRoundingMode:
	case IROp::UpdateRoundingMode:
		CompIR_RoundingMode(inst);
		break;

	case IROp::SetCtrlVFPU:
	case IROp::SetCtrlVFPUReg:
	case IROp::SetCtrlVFPUFReg:
	case IROp::FpCondFromReg:
	case IROp::FpCondToReg:
	case IROp::FpCtrlFromReg:
	case IROp::FpCtrlToReg:
	case IROp::VfpuCtrlToReg:
	case IROp::FMovFromGPR:
	case IROp::FMovToGPR:
		CompIR_Transfer(inst);
		break;

	case IROp::Vec4Init:
	case IROp::Vec4Shuffle:
	case IROp::Vec4Blend:
	case IROp::Vec4Mov:
		CompIR_VecAssign(inst);
		break;

	case IROp::Vec4Add:
	case IROp::Vec4Sub:
	case IROp::Vec4Mul:
	case IROp::Vec4Div:
	case IROp::Vec4Scale:
	case IROp::Vec4Neg:
	case IROp::Vec4Abs:
	case IROp::Vec4Min:
	case IROp::Vec4Max:
	case IROp::Vec4Slt:
	case IROp::Vec4Sge:
	case IROp::Vec4CvtScaledSW:
		CompIR_VecArith(inst);
		break;

	case IROp::Vec4VfpuDot:
	case IROp::Vec4VfpuMultiDot:
		CompIR_VecHoriz(inst);
		break;


	case IROp::Vec2Unpack16To31:
	case IROp::Vec2Unpack16To32:
	case IROp::Vec4Unpack8To32:
	case IROp::Vec4DuplicateUpperBitsAndShift1:
	case IROp::Vec4Pack31To8:
	case IROp::Vec4Pack32To8:
	case IROp::Vec2Pack31To16:
	case IROp::Vec2Pack32To16:
		CompIR_VecPack(inst);
		break;

	case IROp::FSin:
	case IROp::FCos:
	case IROp::FRSqrt:
	case IROp::FRecip:
	case IROp::FAsin:
	case IROp::FVSqrt:
	case IROp::FExp2:
	case IROp::FLog2:
	case IROp::FHalfToFloat:
	case IROp::FSinCos:
		CompIR_FSpecial(inst);
		break;

	case IROp::Interpret:
		CompIR_Interpret(inst);
		break;

	case IROp::LoadKernel8:
	case IROp::LoadKernel8Ext:
	case IROp::LoadKernel16:
	case IROp::LoadKernel16Ext:
	case IROp::LoadKernel32:
	case IROp::StoreKernel8:
	case IROp::StoreKernel16:
	case IROp::StoreKernel32:
		// Kernel code only, and mostly hardware registers once ResolveKernelMemory has run.
		CompIR_Generic(inst);
		break;

	case IROp::Syscall:
	case IROp::SyscallUnresolved:
	case IROp::CallReplacement:
	case IROp::Break:
		CompIR_System(inst);
		break;

	case IROp::Breakpoint:
	case IROp::MemoryCheck:
		CompIR_Breakpoint(inst);
		break;

	case IROp::ValidateAddress8:
	case IROp::ValidateAddress16:
	case IROp::ValidateAddress32:
	case IROp::ValidateAddress128:
		CompIR_ValidateAddress(inst);
		break;

	case IROp::ExitToConst:
	case IROp::ExitToReg:
	case IROp::ExitToPC:
		CompIR_Exit(inst);
		break;

	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
	case IROp::ExitToConstIfGtZ:
	case IROp::ExitToConstIfGeZ:
	case IROp::ExitToConstIfLtZ:
	case IROp::ExitToConstIfLeZ:
	case IROp::ExitToConstIfFpTrue:
	case IROp::ExitToConstIfFpFalse:
		CompIR_ExitIf(inst);
		break;

	default:
		_assert_msg_(false, "Unexpected IR op %d", (int)inst.op);
		CompIR_Generic(inst);
		break;
	}
}

IRNativeJit::IRNativeJit(MIPSState *mipsState)
	: IRJit(mipsState, true) {}

void IRNativeJit::Init(IRNativeBackend &backend) {
	backend_ = &backend;
	backend_->SetProfiling(profiling_);
	backend_->SetMIPSState(mips_);
	backend_->GenerateFixedCode(mips_);

	// Wanted this to be a reference, but vtbls get in the way.  Shouldn't change.
	hooks_ = backend.GetNativeHooks();

	if (enableDebugProfiler && hooks_.profilerPC) {
		debugProfilerThreadStatus = true;
		debugProfilerThread = std::thread([&] {
			// Spin, spin spin... maybe could at least hook into sleeps.
			while (debugProfilerThreadStatus) {
				IRProfilerStatus stat = *hooks_.profilerStatus;
				uint32_t pc = *hooks_.profilerPC;
				if (stat != IRProfilerStatus::NOT_RUNNING && stat != IRProfilerStatus::SYSCALL) {
					debugSeenPCUsage[std::make_pair(pc, stat)]++;
				}
			}
		});
	}
}

IRJit::CompileResult IRNativeJit::CompileTargetBlock(int block_num, const std::vector<IRInst> &instructions) {
	JitBlock *block = blocks_.GetBlock(block_num);
	if (!backend_->CompileBlock(block_num, block->GetOriginalStart(), instructions.data(), (int)instructions.size()))
		return CompileResult::CODE_FULL;
	block->SetEntry(backend_->GetBlockEntry(block_num));
	return CompileResult::OK;
}

void IRNativeJit::GetBlockInstructions(const JitBlock &block, std::vector<IRInst> &instructions) const {
	instructions.clear();
	const u32 start = block.GetOriginalStart();
	if (!block.IsValid() || !Memory::IsValid4AlignedAddress(start))
		return;
	// On a copy, since compiling changes the frontend's state. This gives what ran unless the code or
	// the frontend's state has changed since.
	IRFrontend frontend = frontend_;
	u32 mipsBytes;
	frontend.DoJit(start, instructions, mipsBytes);
}

void IRNativeJit::FinalizeTargetBlock(int block_num) {
	backend_->FinalizeBlock(block_num, jo);
}

void IRNativeJit::InvalidateTargetBlock(int block_num) {
	// Already invalidated, or never compiled (the code space filled first).
	if (blocks_.GetBlock(block_num)->GetOriginalStart() == 0 || !backend_->GetNativeBlock(block_num))
		return;
	backend_->InvalidateBlock(block_num);
}

void IRNativeJit::RunLoopUntil(u64 globalticks) {
	if constexpr (enableDebugStats || enableDebugProfiler) {
		LogDebugStats();
	}

	if (profiling_) {
		if (!JitProfiler::IsRunning() && !JitProfiler::Start(50)) {
			WARN_LOG(Log::JIT, "JIT profiling isn't supported on this platform");
			profiling_ = false;
		}
		CollectProfileSamples();
	}

	PROFILE_THIS_SCOPE("jit");
	hooks_.enterDispatcher();
}

void IRNativeJit::FlushProfile() {
	if (!profiling_)
		return;
	JitProfiler::Stop();
	WriteProfileReport();
}

// Where the fixed code ends: the first block, or the code so far.
int IRNativeJit::FindBlockOffsetEnd() const {
	if (backend_->GetNumNativeBlocks() > 0)
		return backend_->GetBlockCodeStart(0);
	return (int)backend_->CodeBlock().GetOffset(backend_->CodeBlock().GetCodePtr());
}

int IRNativeJit::FindBlockByCodeOffset(int offset) const {
	int low = 0;
	int high = backend_->GetNumNativeBlocks() - 1;
	int found = -1;
	while (low <= high) {
		int mid = low + (high - low) / 2;
		if (backend_->GetBlockCodeStart(mid) <= offset) {
			found = mid;
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	return found;
}

void IRNativeJit::CollectProfileSamples() {
	std::vector<JitProfiler::Sample> samples;
	JitProfiler::Drain(samples);
	for (const JitProfiler::Sample &s : samples) {
		totalSamples_++;
		const int offset = backend_->OffsetFromCodePtr((const u8 *)s.pc);
		if (offset < 0) {
			hostProfile_[s.pc]++;
			continue;
		}
		const int blockNum = FindBlockByCodeOffset(offset);
		if (blockNum >= 0) {
			BlockProfile &profile = blockProfiles_[blockNum];
			profile.samples++;
			profile.byOffset[offset]++;
			continue;
		}
		// In the fixed code. If it's a routine called from a block, charge the block.
		if (fixedLabels_.empty()) {
			backend_->GetAllFixedCodeLabels(fixedLabels_);
			fixedLabels_.insert(fixedLabels_.begin(), { backend_->CodeBlock().GetBasePtr(), "(start of code)", false });
		}
		auto label = std::upper_bound(fixedLabels_.begin(), fixedLabels_.end(), (const u8 *)s.pc, [](const u8 *p, const IRNativeFixedCodeLabel &l) { return p < l.ptr; });
		const IRNativeFixedCodeLabel &where = *(label - 1);
		fixedCodeByOffset_[offset]++;
		const int callerOffset = backend_->OffsetFromCodePtr((const u8 *)s.caller);
		const int callerBlock = callerOffset < 0 || !where.calledFromBlocks ? -1 : FindBlockByCodeOffset(callerOffset);
		if (callerBlock >= 0) {
			BlockProfile &profile = blockProfiles_[callerBlock];
			profile.routineSamples++;
			profile.routineByReturn[callerOffset]++;
			routinesByLabel_[where.name]++;
		} else {
			fixedCodeSamples_++;
			fixedCodeByLabel_[where.name]++;
		}
	}
}

static std::vector<std::string> DisassembleHost(const u8 *p, int size, std::vector<int> &offsets) {
	std::vector<std::string> lines;
#if PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(ARM)
	for (int i = 0; i + 4 <= size; i += 4) {
#if PPSSPP_ARCH(ARM64)
		std::vector<std::string> one = DisassembleArm64(p + i, 4);
#else
		std::vector<std::string> one = DisassembleArm2(p + i, 4);
#endif
		lines.push_back(one.empty() ? "?" : one[0]);
		offsets.push_back(i);
	}
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	lines = DisassembleX86(p, size, &offsets);
#elif PPSSPP_ARCH(RISCV64)
	for (int i = 0; i + 2 <= size; ) {
		// Compressed instructions are 2 bytes.
		const int len = (p[i] & 3) == 3 ? 4 : 2;
		if (i + len > size)
			break;
		std::vector<std::string> one = DisassembleRV64(p + i, len);
		lines.push_back(one.empty() ? "?" : one[0]);
		offsets.push_back(i);
		i += len;
	}
#elif PPSSPP_ARCH(LOONGARCH64)
	for (int i = 0; i + 4 <= size; i += 4) {
		std::vector<std::string> one = DisassembleLA64(p + i, 4);
		lines.push_back(one.empty() ? "?" : one[0]);
		offsets.push_back(i);
	}
#else
	lines.push_back("(no per-instruction disassembly for this host)");
	offsets.push_back(0);
#endif
	return lines;
}

void IRNativeJit::WriteProfileReport() {
	CollectProfileSamples();
	// Skip the near-empty profiles of a cache cleared while booting or loading a state.
	if (totalSamples_ < 1000) {
		blockProfiles_.clear();
		hostProfile_.clear();
		fixedCodeSamples_ = 0;
		totalSamples_ = 0;
		return;
	}

	FILE *f = File::OpenCFile(Path(g_Config.sJitProfileFile), "a");
	if (!f)
		return;

	const double total = (double)totalSamples_;
	int64_t blockSamples = 0, routineSamples = 0, hostSamples = 0;
	std::vector<int> hot;
	for (const auto &it : blockProfiles_) {
		blockSamples += it.second.samples;
		routineSamples += it.second.routineSamples;
		hot.push_back(it.first);
	}
	for (const auto &it : hostProfile_)
		hostSamples += it.second;
	auto cost = [&](int blockNum) {
		const BlockProfile &p = blockProfiles_[blockNum];
		return p.samples + p.routineSamples;
	};
	std::sort(hot.begin(), hot.end(), [&](int a, int b) { return cost(a) > cost(b); });

	int validBlocks = 0;
	for (int i = 0; i < blocks_.GetNumBlocks(); ++i)
		validBlocks += blocks_.IsValidBlock(i) ? 1 : 0;
	fprintf(f, "JIT profile: %lld samples of the emulator thread, every %d us (%llu dropped)\n", (long long)totalSamples_, JitProfiler::PeriodMicros(), (unsigned long long)JitProfiler::Dropped());
	fprintf(f, "  %d blocks compiled since the cache was cleared, %d still valid\n", blocks_.GetNumBlocks(), validBlocks);
	fprintf(f, "  %5.1f%% in %d blocks, %.1f%% in routines they call, %.1f%% in other JIT code (dispatcher), %.1f%% outside JIT code\n\n",
		100.0 * blockSamples / total, (int)hot.size(), 100.0 * routineSamples / total, 100.0 * fixedCodeSamples_ / total, 100.0 * hostSamples / total);

	// Host functions, by symbol.
	std::map<std::string, int64_t> byFunction;
	for (const auto &it : hostProfile_) {
		uintptr_t symbolStart = 0;
		std::string name = JitProfiler::DescribeHostAddress(it.first, &symbolStart);
		if (name.empty())
			name = StringFromFormat("unknown %p", (const void *)symbolStart);
		byFunction[name] += it.second;
	}
	std::vector<std::pair<int64_t, std::string>> functions;
	for (const auto &it : byFunction)
		functions.emplace_back(it.second, it.first);
	std::sort(functions.begin(), functions.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	fprintf(f, "Routines called from blocks:\n");
	for (const auto &it : routinesByLabel_)
		fprintf(f, "  %5.1f%% %s\n", 100.0 * it.second / total, it.first.c_str());
	fprintf(f, "Other JIT code:\n");
	for (const auto &it : fixedCodeByLabel_)
		fprintf(f, "  %5.1f%% %s\n", 100.0 * it.second / total, it.first.c_str());
	fprintf(f, "Outside JIT code:\n");
	for (int i = 0; i < (int)functions.size() && i < 30; ++i)
		fprintf(f, "  %5.1f%% %s\n", 100.0 * functions[i].first / total, functions[i].second.c_str());
	fprintf(f, "\nBlocks:\n");
	for (int rank = 0; rank < (int)hot.size() && rank < 30; ++rank) {
		const int blockNum = hot[rank];
		const JitBlock *b = blocks_.GetBlock(blockNum);
		const std::string label = g_symbolMap ? g_symbolMap->GetDescription(b->GetOriginalStart()) : "";
		fprintf(f, "  %5.1f%% block %d at %08x %s\n", 100.0 * cost(blockNum) / total, blockNum, b->GetOriginalStart(), label.c_str());
	}
	fprintf(f, "\n");

	const u8 *base = backend_->CodeBlock().GetBasePtr();
	for (int rank = 0; rank < (int)hot.size() && rank < 30; ++rank) {
		const int blockNum = hot[rank];
		const BlockProfile &profile = blockProfiles_[blockNum];
		const JitBlock *b = blocks_.GetBlock(blockNum);
		const IRNativeBlock *nativeBlock = backend_->GetNativeBlock(blockNum);
		const int start = backend_->GetBlockCodeStart(blockNum);
		const int end = blockNum + 1 < backend_->GetNumNativeBlocks() ? backend_->GetBlockCodeStart(blockNum + 1) : (int)backend_->CodeBlock().GetOffset(backend_->CodeBlock().GetCodePtr());
		u32 mipsStart, mipsSize;
		b->GetRange(&mipsStart, &mipsSize);
		const std::string label = g_symbolMap ? g_symbolMap->GetDescription(mipsStart) : "";

		fprintf(f, "======== #%d: %.2f%% (%.2f%% in routines), block %d at %08x %s, %d MIPS instructions, %d IR, %d host bytes\n",
			rank + 1, 100.0 * cost(blockNum) / total, 100.0 * profile.routineSamples / total, blockNum, mipsStart, label.c_str(),
			(int)(mipsSize / 4), b->GetNumIRInstructions(), end - start);
		JitBlockDebugInfo info = debugInterface_.GetBlockDebugInfo(blockNum);
		for (const std::string &line : info.origDisasm)
			fprintf(f, "    %s\n", line.c_str());
		fprintf(f, "\n");

		std::map<int, int> irAt;
		if (nativeBlock) {
			for (int i = 0; i < (int)nativeBlock->irOffsets.size(); ++i)
				irAt.emplace(nativeBlock->irOffsets[i], i);
		}
		std::vector<int> offsets;
		std::vector<std::string> lines = DisassembleHost(base + start, end - start, offsets);
		for (size_t i = 0; i < lines.size(); ++i) {
			const int from = start + offsets[i];
			const int to = i + 1 < lines.size() ? start + offsets[i + 1] : end;
			for (auto it = irAt.lower_bound(from); it != irAt.end() && it->first < to && it->second < (int)info.irDisasm.size(); ++it) {
				fprintf(f, "              IR #%d: %s\n", it->second, info.irDisasm[it->second].c_str());
			}
			int64_t count = 0, routine = 0;
			for (auto it = profile.byOffset.lower_bound(from); it != profile.byOffset.end() && it->first < to; ++it)
				count += it->second;
			// A return address is the instruction after the call.
			for (auto it = profile.routineByReturn.lower_bound(from + 1); it != profile.routineByReturn.end() && it->first <= to; ++it)
				routine += it->second;
			if (count)
				fprintf(f, "%6.2f%% ", 100.0 * count / total);
			else
				fprintf(f, "        ");
			if (routine)
				fprintf(f, "%s  (routine %.2f%%)\n", lines[i].c_str(), 100.0 * routine / total);
			else
				fprintf(f, "%s\n", lines[i].c_str());
		}
		fprintf(f, "\n");
	}
	// The hot fixed code, a routine at a time.
	for (size_t i = 0; i < fixedLabels_.size(); ++i) {
		const int from = (int)backend_->CodeBlock().GetOffset(fixedLabels_[i].ptr);
		const int to = i + 1 < fixedLabels_.size() ? (int)backend_->CodeBlock().GetOffset(fixedLabels_[i + 1].ptr) : FindBlockOffsetEnd();
		int64_t count = 0;
		for (auto it = fixedCodeByOffset_.lower_bound(from); it != fixedCodeByOffset_.end() && it->first < to; ++it)
			count += it->second;
		if (count < totalSamples_ / 200 || to <= from)
			continue;
		fprintf(f, "======== %s: %.2f%%\n", fixedLabels_[i].name, 100.0 * count / total);
		std::vector<int> offsets;
		std::vector<std::string> lines = DisassembleHost(base + from, to - from, offsets);
		for (size_t j = 0; j < lines.size(); ++j) {
			const int lineFrom = from + offsets[j];
			const int lineTo = j + 1 < lines.size() ? from + offsets[j + 1] : to;
			int64_t lineCount = 0;
			for (auto it = fixedCodeByOffset_.lower_bound(lineFrom); it != fixedCodeByOffset_.end() && it->first < lineTo; ++it)
				lineCount += it->second;
			if (lineCount)
				fprintf(f, "%6.2f%% %s\n", 100.0 * lineCount / total, lines[j].c_str());
			else
				fprintf(f, "        %s\n", lines[j].c_str());
		}
		fprintf(f, "\n");
	}
	fclose(f);

	blockProfiles_.clear();
	hostProfile_.clear();
	fixedCodeByLabel_.clear();
	routinesByLabel_.clear();
	fixedCodeByOffset_.clear();
	fixedCodeSamples_ = 0;
	totalSamples_ = 0;
}

void IRNativeJit::ClearCache() {
	IRJit::ClearCache();
	backend_->ClearAllBlocks();
}

bool IRNativeJit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (ptr != nullptr && backend_->DescribeCodePtr(ptr, name))
		return true;

	int offset = backend_->OffsetFromCodePtr(ptr);
	if (offset == -1)
		return false;

	int block_num = -1;
	int block_offset = INT_MAX;
	for (int i = 0; i < backend_->GetNumNativeBlocks(); ++i) {
		const IRNativeBlock *nativeBlock = backend_->GetNativeBlock(i);
		int b_start = nativeBlock->entryOffset;
		if (b_start > offset)
			continue;

		int b_end = nativeBlock->checkedOffset;
		int b_offset = offset - b_start;
		if (b_end > b_start && b_end >= offset) {
			// For sure within the block.
			block_num = i;
			block_offset = b_offset;
			break;
		}

		if (b_offset < block_offset) {
			// Possibly within the block, unless in some other block...
			block_num = i;
			block_offset = b_offset;
		}
	}

	// Used by profiling tools that don't like spaces.
	if (block_num == -1) {
		name = "unknownOrDeletedBlock";
		return true;
	}

	const JitBlock *block = blocks_.GetBlock(block_num);
	if (block) {
		u32 start = 0, size = 0;
		block->GetRange(&start, &size);

		// It helps to know which func this block is inside.
		const std::string label = g_symbolMap ? g_symbolMap->GetDescription(start) : "";
		if (!label.empty())
			name = StringFromFormat("block%d_%08x_%s_0x%x", block_num, start, label.c_str(), block_offset);
		else
			name = StringFromFormat("block%d_%08x_0x%x", block_num, start, block_offset);
		return true;
	}
	return false;
}

bool IRNativeJit::CodeInRange(const u8 *ptr) const {
	return backend_->CodeInRange(ptr);
}

const u8 *IRNativeJit::GetCodeBase() const {
	return backend_->CodeBlock().GetBasePtr();
}

void IRNativeJit::GetStats(JitStats *stats) const {
	IRJit::GetStats(stats);
	const CodeBlockCommon &code = backend_->CodeBlock();
	stats->codeUsed = code.GetOffset(code.GetCodePtr());
	stats->codeSize = code.GetRegionSize();
}

u32 IRNativeJit::GetBlockShadowHookValue() const {
	return (u32)backend_->OffsetFromCodePtr(backend_->GetNativeHooks().blockShadowHook);
}

bool IRNativeJit::IsAtDispatchFetch(const u8 *ptr) const {
	return ptr == backend_->GetNativeHooks().dispatchFetch;
}

const u8 *IRNativeJit::GetDispatcher() const {
	return backend_->GetNativeHooks().dispatcher;
}

const u8 *IRNativeJit::GetCrashHandler() const {
	return backend_->GetNativeHooks().crashHandler;
}

void IRNativeJit::UpdateFCR31() {
	IRJit::UpdateFCR31();
	backend_->UpdateFCR31(mips_);
}

bool IRNativeBackend::CodeInRange(const u8 *ptr) const {
	return CodeBlock().IsInSpace(ptr);
}

bool IRNativeBackend::DescribeCodePtr(const u8 *ptr, std::string &name) const {
	if (!CodeBlock().IsInSpace(ptr))
		return false;

	// Used in disassembly viewer.
	if (ptr == (const uint8_t *)hooks_.enterDispatcher) {
		name = "enterDispatcher";
	} else if (ptr == hooks_.dispatcher) {
		name = "dispatcher";
	} else if (ptr == hooks_.dispatchFetch) {
		name = "dispatchFetch";
	} else if (ptr == hooks_.crashHandler) {
		name = "crashHandler";
	} else {
		return false;
	}
	return true;
}

void IRNativeBackend::GetAllFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const {
	labels.push_back({ (const u8 *)hooks_.enterDispatcher, "enterDispatcher", false });
	labels.push_back({ hooks_.dispatcher, "dispatcher", false });
	labels.push_back({ hooks_.dispatchFetch, "dispatchFetch", false });
	labels.push_back({ hooks_.crashHandler, "crashHandler", false });
	GetFixedCodeLabels(labels);
	labels.erase(std::remove_if(labels.begin(), labels.end(), [](const IRNativeFixedCodeLabel &l) { return l.ptr == nullptr; }), labels.end());
	std::sort(labels.begin(), labels.end(), [](const IRNativeFixedCodeLabel &a, const IRNativeFixedCodeLabel &b) { return a.ptr < b.ptr; });
}

std::string IRNativeBackend::DescribeFixedCode(const u8 *ptr) const {
	std::vector<IRNativeFixedCodeLabel> labels;
	GetAllFixedCodeLabels(labels);
	auto next = std::upper_bound(labels.begin(), labels.end(), ptr, [](const u8 *p, const IRNativeFixedCodeLabel &l) { return p < l.ptr; });
	// Far past a routine's start is likely something else (constants, say).
	if (next == labels.begin() || ptr - (next - 1)->ptr >= 0x1000)
		return "fixedCode";
	const IRNativeFixedCodeLabel &label = *(next - 1);
	if (label.ptr == ptr)
		return label.name;
	return StringFromFormat("%s+0x%x", label.name, (int)(ptr - label.ptr));
}

int IRNativeBackend::OffsetFromCodePtr(const u8 *ptr) {
	auto &codeBlock = CodeBlock();
	if (!codeBlock.IsInSpace(ptr))
		return -1;
	return (int)codeBlock.GetOffset(ptr);
}

void IRNativeBackend::FinalizeBlock(int block_num, const JitOptions &jo) {
	if (jo.enableBlocklink) {
		uint32_t pc = nativeBlocks_[block_num].startPC;

		// First, link other blocks to this one now that it's finalized.
		auto incoming = linksTo_.equal_range(pc);
		for (auto it = incoming.first; it != incoming.second; ++it) {
			auto &exits = nativeBlocks_[it->second].exits;
			for (auto &blockExit : exits) {
				if (blockExit.dest == pc)
					OverwriteExit(blockExit.offset, blockExit.len, block_num);
			}
		}

		// And also any blocks from this one, in case we're finalizing it later.
		auto &outgoing = nativeBlocks_[block_num].exits;
		for (auto &blockExit : outgoing) {
			int dstBlockNum = FindBlockByStart(blockExit.dest);
			const IRNativeBlock *nativeBlock = GetNativeBlock(dstBlockNum);
			if (nativeBlock)
				OverwriteExit(blockExit.offset, blockExit.len, dstBlockNum);
		}
	}
}

int IRNativeBackend::GetBlockCodeStart(int block_num) const {
	const IRNativeBlock &nativeBlock = nativeBlocks_[block_num];
	return std::min(nativeBlock.entryOffset, nativeBlock.checkedOffset);
}

const IRNativeBlock *IRNativeBackend::GetNativeBlock(int block_num) const {
	if (block_num < 0 || block_num >= (int)nativeBlocks_.size())
		return nullptr;
	return &nativeBlocks_[block_num];
}

void IRNativeBackend::SetBlockCheckedOffset(int block_num, int offset) {
	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);

	nativeBlocks_[block_num].checkedOffset = offset;
}

void IRNativeBackend::SetBlockEntry(int block_num, u32 startPC, int offset) {
	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);
	nativeBlocks_[block_num].startPC = startPC;
	nativeBlocks_[block_num].entryOffset = offset;
	blockByStart_[startPC] = block_num;
}

int IRNativeBackend::FindBlockByStart(u32 pc) const {
	auto it = blockByStart_.find(pc);
	return it != blockByStart_.end() ? it->second : -1;
}

void IRNativeBackend::AddLinkableExit(int block_num, uint32_t pc, int exitStartOffset, int exitLen) {
	linksTo_.emplace(pc, block_num);

	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);
	IRNativeBlockExit blockExit;
	blockExit.offset = exitStartOffset;
	blockExit.len = exitLen;
	blockExit.dest = pc;
	nativeBlocks_[block_num].exits.push_back(blockExit);
}

void IRNativeBackend::RecordIRAddresses(int block_num, const std::vector<const u8 *> &addresses) {
	if (!profiling_)
		return;
	if (block_num >= (int)nativeBlocks_.size())
		nativeBlocks_.resize(block_num + 1);
	std::vector<int> &offsets = nativeBlocks_[block_num].irOffsets;
	offsets.clear();
	for (const u8 *p : addresses)
		offsets.push_back(OffsetFromCodePtr(p));
}

void IRNativeBackend::EraseAllLinks(int block_num) {
	if (block_num == -1) {
		linksTo_.clear();
		blockByStart_.clear();
		nativeBlocks_.clear();
	} else if (block_num < (int)nativeBlocks_.size()) {
		// linksTo_ is keyed by the exits' targets.
		for (const IRNativeBlockExit &blockExit : nativeBlocks_[block_num].exits) {
			auto range = linksTo_.equal_range(blockExit.dest);
			for (auto it = range.first; it != range.second;) {
				if (it->second == block_num)
					it = linksTo_.erase(it);
				else
					++it;
			}
		}
		nativeBlocks_[block_num].exits.clear();
		auto start = blockByStart_.find(nativeBlocks_[block_num].startPC);
		if (start != blockByStart_.end() && start->second == block_num)
			blockByStart_.erase(start);
	}
}

void IRNativeJit::GetBlockCodeRange(int blockNum, int *startOffset, int *size) const {
	const CodeBlockCommon *codeBlock = &backend_->CodeBlock();
	int blockOffset = backend_->GetBlockEntry(blockNum);
	int endOffset = backend_->GetNativeBlock(blockNum)->checkedOffset;

	// If endOffset is before, the checked entry is before the block start.
	if (endOffset < blockOffset) {
		// We assume linear allocation.  Maybe a bit dangerous, should always be right.
		if (blockNum + 1 >= backend_->GetNumNativeBlocks()) {
			// Last block, get from current code pointer.
			endOffset = (int)codeBlock->GetOffset(codeBlock->GetCodePtr());
		} else {
			endOffset = backend_->GetBlockEntry(blockNum + 1);
			_assert_msg_(endOffset >= blockOffset, "Next block not sequential, block=%d/%08x, next=%d/%08x", blockNum, blockOffset, blockNum + 1, endOffset);
		}
	}

	*startOffset = blockOffset;
	*size = endOffset - blockOffset;
}

int IRNativeJit::GetBlockCodeSize(int blockNum) const {
	int blockOffset, codeSize;
	GetBlockCodeRange(blockNum, &blockOffset, &codeSize);
	return codeSize;
}

void IRNativeJit::GetBlockTargetDisasm(int blockNum, std::vector<std::string> &lines) const {
	int blockOffset, codeSize;
	GetBlockCodeRange(blockNum, &blockOffset, &codeSize);

	const u8 *blockStart = backend_->CodeBlock().GetBasePtr() + blockOffset;
#if PPSSPP_ARCH(ARM)
	lines = DisassembleArm2(blockStart, codeSize);
#elif PPSSPP_ARCH(ARM64)
	lines = DisassembleArm64(blockStart, codeSize);
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	lines = DisassembleX86(blockStart, codeSize);
#elif PPSSPP_ARCH(RISCV64)
	lines = DisassembleRV64(blockStart, codeSize);
#elif PPSSPP_ARCH(LOONGARCH64)
	lines = DisassembleLA64(blockStart, codeSize);
#endif
}

} // namespace MIPSComp
