// Copyright (C) 2020 PPSSPP Project

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

#include <cstdint>
#include <unordered_set>
#include <mutex>
#include <sstream>

#include "Common/StringUtils.h"
#include "Common/MachineContext.h"

#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
#include "Common/x64Analyzer.h"

#elif PPSSPP_ARCH(ARM64)
#include "Core/Util/DisArm64.h"
#elif PPSSPP_ARCH(ARM)
#include "ext/disarm.h"
#endif

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/MemFault.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/Debugger/SymbolMap.h"

// Stack walking stuff
#include "Core/MIPS/MIPSStackWalk.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/HLE/sceKernelThread.h"

namespace Memory {

static int64_t g_numReportedBadAccesses = 0;
const uint8_t *g_lastCrashAddress;
MemoryExceptionType g_lastMemoryExceptionType;
static bool inCrashHandler = false;

std::unordered_set<const uint8_t *> g_ignoredAddresses;

void MemFault_Init() {
	g_numReportedBadAccesses = 0;
	g_lastCrashAddress = nullptr;
	g_lastMemoryExceptionType = MemoryExceptionType::NONE;
	g_ignoredAddresses.clear();
}

bool MemFault_MayBeResumable() {
	return g_lastCrashAddress != nullptr;
}

void MemFault_IgnoreLastCrash() {
	g_ignoredAddresses.insert(g_lastCrashAddress);
}

#ifdef MACHINE_CONTEXT_SUPPORTED

static bool DisassembleNativeAt(const uint8_t *codePtr, int instructionSize, std::string *dest) {
#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
	auto lines = DisassembleX86(codePtr, instructionSize);
	if (!lines.empty()) {
		*dest = lines[0];
		return true;
	}
#elif PPSSPP_ARCH(ARM64)
	auto lines = DisassembleArm64(codePtr, instructionSize);
	if (!lines.empty()) {
		*dest = lines[0];
		return true;
	}
#elif PPSSPP_ARCH(ARM)
	auto lines = DisassembleArm2(codePtr, instructionSize);
	if (!lines.empty()) {
		*dest = lines[0];
		return true;
	}
#elif PPSSPP_ARCH(RISCV64)
	auto lines = DisassembleRV64(codePtr, instructionSize);
	if (!lines.empty()) {
		*dest = lines[0];
		return true;
	}
#endif
	return false;
}

bool HandleFault(uintptr_t hostAddress, void *ctx) {
	if (inCrashHandler)
		return false;
	inCrashHandler = true;

	SContext *context = (SContext *)ctx;
	const uint8_t *codePtr = (uint8_t *)(context->CTX_PC);

	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);

	// We set this later if we think it can be resumed from.
	g_lastCrashAddress = nullptr;

	// TODO: Check that codePtr is within the current JIT space.
	bool inJitSpace = MIPSComp::jit && MIPSComp::jit->CodeInRange(codePtr);
	if (!inJitSpace) {
		// This is a crash in non-jitted code. Not something we want to handle here, ignore.
		// Actually, we could handle crashes from the IR interpreter here, although recovering the call stack
		// might be tricky...
		inCrashHandler = false;
		return false;
	}

	uintptr_t baseAddress = (uintptr_t)base;
#ifdef MASKED_PSP_MEMORY
	const uintptr_t addressSpaceSize = 0x40000000ULL;
#else
	const uintptr_t addressSpaceSize = 0x100000000ULL;
#endif

	// Check whether hostAddress is within the PSP memory space, which (likely) means it was a guest executable that did the bad access.
	bool invalidHostAddress = hostAddress == (uintptr_t)0xFFFFFFFFFFFFFFFFULL;
	if (hostAddress < baseAddress || hostAddress >= baseAddress + addressSpaceSize) {
		// Host address outside - this was a different kind of crash.
		if (!invalidHostAddress) {
			inCrashHandler = false;
			return false;
		}
	}

	// OK, a guest executable did a bad access. Let's handle it.

	uint32_t guestAddress = invalidHostAddress ? 0xFFFFFFFFUL : (uint32_t)(hostAddress - baseAddress);

	// TODO: Share the struct between the various analyzers, that will allow us to share most of
	// the implementations here.
	bool success = false;

	MemoryExceptionType type = MemoryExceptionType::NONE;

	int instructionSize = 4;
#if PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(X86)
	// X86, X86-64. Variable instruction size so need to analyze the mov instruction in detail.
	instructionSize = 15;

	// To ignore the access, we need to disassemble the instruction and modify context->CTX_PC
	LSInstructionInfo info{};
	success = X86AnalyzeMOV(codePtr, info);
	if (success)
		instructionSize = info.instructionSize;
#elif PPSSPP_ARCH(ARM64)
	uint32_t word;
	memcpy(&word, codePtr, 4);
	// To ignore the access, we need to disassemble the instruction and modify context->CTX_PC
	Arm64LSInstructionInfo info{};
	success = Arm64AnalyzeLoadStore((uint64_t)codePtr, word, &info);
#elif PPSSPP_ARCH(ARM)
	uint32_t word;
	memcpy(&word, codePtr, 4);
	// To ignore the access, we need to disassemble the instruction and modify context->CTX_PC
	ArmLSInstructionInfo info{};
	success = ArmAnalyzeLoadStore((uint32_t)codePtr, word, &info);
#elif PPSSPP_ARCH(RISCV64)
	// TODO: Put in a disassembler.
	struct RiscVLSInstructionInfo {
		int instructionSize;
		bool isIntegerLoadStore;
		bool isFPLoadStore;
		int size;
		bool isMemoryWrite;
	};

	uint32_t word;
	memcpy(&word, codePtr, 4);

	RiscVLSInstructionInfo info{};
	// Compressed instructions have low bits 00, 01, or 10.
	info.instructionSize = (word & 3) == 3 ? 4 : 2;
	instructionSize = info.instructionSize;

	success = true;
	switch (word & 0x7F) {
	case 3:
		info.isIntegerLoadStore = true;
		info.size = 1 << ((word >> 12) & 3);
		break;
	case 7:
		info.isFPLoadStore = true;
		info.size = 1 << ((word >> 12) & 3);
		break;
	case 35:
		info.isIntegerLoadStore = true;
		info.isMemoryWrite = true;
		info.size = 1 << ((word >> 12) & 3);
		break;
	case 39:
		info.isFPLoadStore = true;
		info.isMemoryWrite = true;
		info.size = 1 << ((word >> 12) & 3);
		break;
	default:
		// Compressed instruction.
		switch (word & 0x6003) {
		case 0x4000:
		case 0x4002:
		case 0x6000:
		case 0x6002:
			info.isIntegerLoadStore = true;
			info.size = (word & 0x2000) != 0 ? 8 : 4;
			info.isMemoryWrite = (word & 0x8000) != 0;
			break;
		case 0x2000:
		case 0x2002:
			info.isFPLoadStore = true;
			info.size = 8;
			info.isMemoryWrite = (word & 0x8000) != 0;
			break;
		default:
			// Not a read or a write.
			success = false;
			break;
		}
		break;
	}
#endif

	if (MIPSComp::jit && MIPSComp::jit->IsAtDispatchFetch(codePtr)) {
		u32 targetAddr = currentMIPS->pc;  // bad approximation
		// TODO: Do the other archs and platforms.
#if PPSSPP_ARCH(AMD64) && PPSSPP_PLATFORM(WINDOWS)
		// We know which register the address is in, look in Asm.cpp.
		targetAddr = (uint32_t)context->Rax;
#endif
		Core_ExecException(targetAddr, currentMIPS->pc, ExecExceptionType::JUMP);
		// Redirect execution to a crash handler that will switch to CoreState::CORE_RUNTIME_ERROR immediately.
		uintptr_t crashHandler = (uintptr_t)MIPSComp::jit->GetCrashHandler();
		if (crashHandler != 0) {
			context->CTX_PC = crashHandler;
			ERROR_LOG(Log::MemMap, "Bad execution access detected, halting: %08x (last known pc %08x, host: %p)", targetAddr, currentMIPS->pc, (void *)hostAddress);
			inCrashHandler = false;
			return true;
		}

		type = MemoryExceptionType::UNKNOWN;
	} else if (success) {
		if (info.isMemoryWrite) {
			type = MemoryExceptionType::WRITE_WORD;
		} else {
			type = MemoryExceptionType::READ_WORD;
		}
	} else {
		type = MemoryExceptionType::UNKNOWN;
	}

	g_lastMemoryExceptionType = type;

	bool handled = true;
	if (success && (g_Config.bIgnoreBadMemAccess || g_ignoredAddresses.find(codePtr) != g_ignoredAddresses.end())) {
		if (!info.isMemoryWrite) {
			// It was a read. Fill the destination register with 0.
			// TODO
		}
		// Move on to the next instruction. Note that handling bad accesses like this is pretty slow.
		context->CTX_PC += info.instructionSize;
		g_numReportedBadAccesses++;
		if (g_numReportedBadAccesses < 100) {
			ERROR_LOG(Log::MemMap, "Bad memory access detected and ignored: %08x (%p)", guestAddress, (void *)hostAddress);
		}
	} else {
		std::string infoString = "";
		std::string temp;
		if (MIPSComp::jit && MIPSComp::jit->DescribeCodePtr(codePtr, temp)) {
			infoString += temp + "\n";
		}
		temp.clear();
		if (DisassembleNativeAt(codePtr, instructionSize, &temp)) {
			infoString += temp + "\n";
		}

		// Either bIgnoreBadMemAccess is off, or we failed recovery analysis.
		// We can't ignore this memory access.
		uint32_t approximatePC = currentMIPS->pc;
		// TODO: Determine access size from the disassembled native instruction. We have some partial info already,
		// just need to clean it up.
		Core_MemoryExceptionInfo(guestAddress, 0, approximatePC, type, infoString, true);

		// There's a small chance we can resume from this type of crash.
		g_lastCrashAddress = codePtr;

		// Redirect execution to a crash handler that will switch to CoreState::CORE_RUNTIME_ERROR immediately.
		uintptr_t crashHandler = 0;
		if (MIPSComp::jit)
			crashHandler = (uintptr_t)MIPSComp::jit->GetCrashHandler();
		if (crashHandler != 0)
			context->CTX_PC = crashHandler;
		else
			handled = false;
		ERROR_LOG(Log::MemMap, "Bad memory access detected! %08x (%p) Stopping emulation. Info:\n%s", guestAddress, (void *)hostAddress, infoString.c_str());
	}

	inCrashHandler = false;
	return handled;
}

#else

bool HandleFault(uintptr_t hostAddress, void *ctx) {
	ERROR_LOG(Log::MemMap, "Exception handling not supported");
	return false;
}

#endif

}  // namespace Memory

std::vector<MIPSStackWalk::StackFrame> WalkCurrentStack(int threadID) {
	DebugInterface *cpuDebug = currentDebugMIPS;

	auto threads = GetThreadsInfo();
	uint32_t entry = cpuDebug->GetPC();
	uint32_t stackTop = 0;
	for (const DebugThreadInfo &th : threads) {
		if ((threadID == -1 && th.isCurrent) || th.id == threadID) {
			entry = th.entrypoint;
			stackTop = th.initialStack;
			break;
		}
	}

	uint32_t ra = cpuDebug->GetRegValue(0, MIPS_REG_RA);
	uint32_t sp = cpuDebug->GetRegValue(0, MIPS_REG_SP);
	return MIPSStackWalk::Walk(cpuDebug->GetPC(), ra, sp, entry, stackTop);
}

std::string FormatStackTrace(const std::vector<MIPSStackWalk::StackFrame> &frames) {
	std::stringstream str;
	for (const auto &frame : frames) {
		std::string desc = g_symbolMap->GetDescription(frame.entry);
		str << StringFromFormat("%s (%08x+%03x, pc: %08x sp: %08x)\n", desc.c_str(), frame.entry, frame.pc - frame.entry, frame.pc, frame.sp);
	}
	return str.str();
}
