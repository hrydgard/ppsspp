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

#include <stdlib.h>

#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/NativeJit.h"
#include "Common/StringUtils.h"

#include "ext/disarm.h"
#include "ext/udis86/udis86.h"
#include "Core/Util/DisArm64.h"

#if (defined(_M_IX86) || defined(_M_X64)) && defined(_WIN32)
#define DISASM_ALL 1
#endif

namespace MIPSComp {
#if defined(ARM)
	ArmJit *jit;
#elif defined(ARM64)
	Arm64Jit *jit;
#elif defined(_M_IX86) || defined(_M_X64) || defined(MIPS)
	Jit *jit;
#else
	FakeJit *jit;
#endif
	void JitAt() {
		jit->Compile(currentMIPS->pc);
	}
}

#if defined(ARM) || defined(DISASM_ALL)
// We compile this for x86 as well because it may be useful when developing the ARM JIT on a PC.
std::vector<std::string> DisassembleArm2(const u8 *data, int size) {
	std::vector<std::string> lines;

	char temp[256];
	int bkpt_count = 0;
	for (int i = 0; i < size; i += 4) {
		const u32 *codePtr = (const u32 *)(data + i);
		u32 inst = codePtr[0];
		u32 next = (i < size - 4) ? codePtr[1] : 0;
		// MAGIC SPECIAL CASE for MOVW/MOVT readability!
		if ((inst & 0x0FF00000) == 0x03000000 && (next & 0x0FF00000) == 0x03400000) {
			u32 low = ((inst & 0x000F0000) >> 4) | (inst & 0x0FFF);
			u32 hi = ((next & 0x000F0000) >> 4) | (next & 0x0FFF);
			int reg0 = (inst & 0x0000F000) >> 12;
			int reg1 = (next & 0x0000F000) >> 12;
			if (reg0 == reg1) {
				snprintf(temp, sizeof(temp), "MOV32 %s, %04x%04x", ArmRegName(reg0), hi, low);
				lines.push_back(temp);
				i += 4;
				continue;
			}
		}
		ArmDis((u32)(intptr_t)codePtr, inst, temp, sizeof(temp), false);
		std::string buf = temp;
		if (buf == "BKPT 1") {
			bkpt_count++;
		} else {
			if (bkpt_count) {
				lines.push_back(StringFromFormat("BKPT 1 (x%i)", bkpt_count));
				bkpt_count = 0;
			}
			lines.push_back(buf);
		}
	}
	if (bkpt_count) {
		lines.push_back(StringFromFormat("BKPT 1 (x%i)", bkpt_count));
	}
	return lines;
}
#endif

std::string AddAddress(const std::string &buf, uint64_t addr) {
	char buf2[16];
	snprintf(buf2, sizeof(buf2), "%04x%08x", (uint32_t)(addr >> 32), (uint32_t)(addr & 0xFFFFFFFF));
	return std::string(buf2) + " " + buf;
}

#if defined(ARM64) || defined(DISASM_ALL)
std::vector<std::string> DisassembleArm64(const u8 *data, int size) {
	std::vector<std::string> lines;

	char temp[256];
	int bkpt_count = 0;
	for (int i = 0; i < size; i += 4) {
		const u32 *codePtr = (const u32 *)(data + i);
		uint64_t addr = (intptr_t)codePtr;
		u32 inst = codePtr[0];
		u32 next = (i < size - 4) ? codePtr[1] : 0;
		// MAGIC SPECIAL CASE for MOVZ+MOVK readability!
		if (((inst >> 21) & 0x3FF) == 0x294 && ((next >> 21) & 0x3FF) == 0x395) {
			u32 low = (inst >> 5) & 0xFFFF;
			u32 hi = (next >> 5) & 0xFFFF;
			int reg0 = inst & 0x1F;
			int reg1 = next & 0x1F;
			char r = (inst >> 31) ? 'x' : 'w';
			if (reg0 == reg1) {
				snprintf(temp, sizeof(temp), "movi32 %c%d, %04x%04x", r, reg0, hi, low);
				lines.push_back(AddAddress(temp, addr));
				i += 4;
				continue;
			}
		}
		Arm64Dis((intptr_t)codePtr, inst, temp, sizeof(temp), false);
		std::string buf = temp;
		if (buf == "BKPT 1") {
			bkpt_count++;
		} else {
			if (bkpt_count) {
				lines.push_back(StringFromFormat("BKPT 1 (x%i)", bkpt_count));
				bkpt_count = 0;
			}
			if (true) {
				buf = AddAddress(buf, addr);
			}
			lines.push_back(buf);
		}
	}
	if (bkpt_count) {
		lines.push_back(StringFromFormat("BKPT 1 (x%i)", bkpt_count));
	}
	return lines;
}
#endif

#if defined(_M_IX86) || defined(_M_X64)

const char *ppsspp_resolver(struct ud*,
	uint64_t addr,
	int64_t *offset) {
	// For some reason these two don't seem to trigger..
	if (addr >= (uint64_t)(&currentMIPS->r[0]) && addr < (uint64_t)&currentMIPS->r[32]) {
		*offset = addr - (uint64_t)(&currentMIPS->r[0]);
		return "mips.r";
	}
	if (addr >= (uint64_t)(&currentMIPS->v[0]) && addr < (uint64_t)&currentMIPS->v[128]) {
		*offset = addr - (uint64_t)(&currentMIPS->v[0]);
		return "mips.v";
	}
	// But these do.
	if (MIPSComp::jit->IsInSpace((u8 *)(intptr_t)addr)) {
		*offset = addr - (uint64_t)MIPSComp::jit->GetBasePtr();
		return "jitcode";
	}
	if (MIPSComp::jit->Asm().IsInSpace((u8 *)(intptr_t)addr)) {
		*offset = addr - (uint64_t)MIPSComp::jit->Asm().GetBasePtr();
		return "dispatcher";
	}

	return NULL;
}

std::vector<std::string> DisassembleX86(const u8 *data, int size) {
	std::vector<std::string> lines;
	ud_t ud_obj;
	ud_init(&ud_obj);
#ifdef _M_X64
	ud_set_mode(&ud_obj, 64);
#else
	ud_set_mode(&ud_obj, 32);
#endif
	ud_set_pc(&ud_obj, (intptr_t)data);
	ud_set_vendor(&ud_obj, UD_VENDOR_ANY);
	ud_set_syntax(&ud_obj, UD_SYN_INTEL);
	ud_set_sym_resolver(&ud_obj, &ppsspp_resolver);

	ud_set_input_buffer(&ud_obj, data, size);

	int int3_count = 0;
	while (ud_disassemble(&ud_obj) != 0) {
		std::string str = ud_insn_asm(&ud_obj);
		if (str == "int3") {
			int3_count++;
		} else {
			if (int3_count) {
				lines.push_back(StringFromFormat("int3 (x%i)", int3_count));
				int3_count = 0;
			}
			lines.push_back(str);
		}
	}
	if (int3_count) {
		lines.push_back(StringFromFormat("int3 (x%i)", int3_count));
	}
	return lines;
}

#endif
