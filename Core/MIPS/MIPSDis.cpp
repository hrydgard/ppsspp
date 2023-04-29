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

#include <cstring>
#include "Common/StringUtils.h"
#include "Core/HLE/HLE.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSDebugInterface.h"

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

#define RN(i) (currentDebugMIPS->GetRegName(0, i).c_str())
#define FN(i) (currentDebugMIPS->GetRegName(1, i).c_str())
//#define VN(i) (currentDebugMIPS->GetRegName(2, i).c_str())

namespace MIPSDis
{
	std::string SignedHex(int i) {
		char temp[32];
		int offset = 0;
		if (i < 0)
		{
			temp[0] = '-';
			offset = 1;
			i = -i;
		}

		snprintf(&temp[offset], sizeof(temp) - offset, "0x%X", i);
		return temp;
	}

	void Dis_Generic(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		snprintf(out, outSize, "%s\t --- unknown ---", MIPSGetName(op));
	}

	void Dis_Cache(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int imm = SignExtend16ToS32(op & 0xFFFF);
		int rs = _RS;
		int func = (op >> 16) & 0x1F;
		snprintf(out, outSize, "%s\tfunc=%i, %s(%s)", MIPSGetName(op), func, RN(rs), SignedHex(imm).c_str());
	}

	void Dis_mxc1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int fs = _FS;
		int rt = _RT;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, RN(rt), FN(fs));
	}

	void Dis_FPU3op(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int ft = _FT;
		int fs = _FS;
		int fd = _FD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s, %s", name, FN(fd), FN(fs), FN(ft));
	}

	void Dis_FPU2op(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int fs = _FS;
		int fd = _FD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, FN(fd), FN(fs));
	}

	void Dis_FPULS(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int offset = SignExtend16ToS32(op & 0xFFFF);
		int ft = _FT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s(%s)", name, FN(ft), SignedHex(offset).c_str(), RN(rs));
	}

	void Dis_FPUComp(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int fs = _FS;
		int ft = _FT;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, FN(fs), FN(ft));
	}

	void Dis_FPUBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 off = pc;
		int imm = SignExtend16ToS32(op & 0xFFFF) << 2;
		off += imm + 4;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t->$%08x", name, off);
	}

	void Dis_RelBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 off = pc;
		int imm = SignExtend16ToS32(op & 0xFFFF) << 2;
		int rs = _RS;
		off += imm + 4;

		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, ->$%08x", name, RN(rs), off);
	}

	void Dis_Syscall(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 callno = (op>>6) & 0xFFFFF; //20 bits
		int funcnum = callno & 0xFFF;
		int modulenum = (callno & 0xFF000) >> 12;
		snprintf(out, outSize, "syscall\t	%s", GetFuncName(modulenum, funcnum));
	}

	void Dis_ToHiloTransfer(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rs = _RS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s", name, RN(rs));
	}
	void Dis_FromHiloTransfer(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rd = _RD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s", name, RN(rd));
	}

	void Dis_RelBranch2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 off = pc;
		int imm = SignExtend16ToS32(op & 0xFFFF) << 2;
		int rt = _RT;
		int rs = _RS;
		off += imm + 4;

		const char *name = MIPSGetName(op);
		int o = op>>26;
		if (o==4 && rs == rt)//beq
			snprintf(out, outSize, "b\t->$%08x", off);
		else if (o==20 && rs == rt)//beql
			snprintf(out, outSize, "bl\t->$%08x", off);
		else
			snprintf(out, outSize, "%s\t%s, %s, ->$%08x", name, RN(rs), RN(rt), off);
	}

	void Dis_IType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 uimm = op & 0xFFFF;
		u32 suimm = SignExtend16ToU32(op);
		s32 simm = SignExtend16ToS32(op);

		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		switch (op >> 26)
		{
		case 8: //addi
		case 9: //addiu
		case 10: //slti
			snprintf(out, outSize, "%s\t%s, %s, %s", name, RN(rt), RN(rs), SignedHex(simm).c_str());
			break;
		case 11: //sltiu
			snprintf(out, outSize, "%s\t%s, %s, 0x%X", name, RN(rt), RN(rs), suimm);
			break;
		default:
			snprintf(out, outSize, "%s\t%s, %s, 0x%X", name, RN(rt), RN(rs), uimm);
			break;
		}
	}
	void Dis_ori(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 uimm = op & 0xFFFF;
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		if (rs == 0)
			snprintf(out, outSize, "li\t%s, 0x%X", RN(rt), uimm);
		else
			snprintf(out, outSize, "%s\t%s, %s, 0x%X", name, RN(rt), RN(rs), uimm);
	}

	void Dis_IType1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 uimm = op & 0xFFFF;
		int rt = _RT;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, 0x%X", name, RN(rt), uimm);
	}

	void Dis_addi(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int imm = SignExtend16ToS32(op & 0xFFFF);
		int rt = _RT;
		int rs = _RS;
		if (rs == 0)
			snprintf(out, outSize, "li\t%s, %s", RN(rt), SignedHex(imm).c_str());
		else
			Dis_IType(op, pc, out, outSize);
	}

	void Dis_ITypeMem(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int imm = SignExtend16ToS32(op & 0xFFFF);
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s(%s)", name, RN(rt), SignedHex(imm).c_str(), RN(rs));
	}

	void Dis_RType2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, RN(rd), RN(rs));
	}

	void Dis_RType3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s, %s", name, RN(rd), RN(rs), RN(rt));
	}

	void Dis_addu(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		if (rs==0 && rt==0)
			snprintf(out, outSize, "li\t%s, 0", RN(rd));
		else if (rs == 0)
			snprintf(out, outSize, "move\t%s, %s", RN(rd), RN(rt));
		else if (rt == 0)
			snprintf(out, outSize, "move\t%s, %s", RN(rd), RN(rs));
		else
			snprintf(out, outSize, "%s\t%s, %s, %s", name, RN(rd), RN(rs), RN(rt));
	}

	void Dis_ShiftType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		int sa = (op>>6) & 0x1F;
		const char *name = MIPSGetName(op);
		if (((op & 0x3f) == 2) && rs == 1)
			name = "rotr";
		if (((op & 0x3f) == 6) && sa == 1)
			name = "rotrv";
		snprintf(out, outSize, "%s\t%s, %s, 0x%X", name, RN(rd), RN(rt), sa);
	}

	void Dis_VarShiftType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		int sa = (op>>6) & 0x1F;
		const char *name = MIPSGetName(op);
		if (((op & 0x3f) == 6) && sa == 1)
			name = "rotrv";
		snprintf(out, outSize, "%s\t%s, %s, %s", name, RN(rd), RN(rt), RN(rs));
	}

	void Dis_MulDivType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s, %s", name, RN(rs), RN(rt));
	}

	void Dis_Special3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rs = _RS;
		int Rt = _RT;
		int pos = _POS;
		const char *name = MIPSGetName(op);

		switch (op & 0x3f)
		{
		case 0x0: //ext
			{
				int size = _SIZE + 1;
				snprintf(out, outSize, "%s\t%s, %s, 0x%X, 0x%X", name, RN(Rt), RN(rs), pos, size);
			}
			break;
		case 0x4: // ins
			{
				int size = (_SIZE + 1) - pos;
				snprintf(out, outSize, "%s\t%s, %s, 0x%X, 0x%X", name, RN(Rt), RN(rs), pos, size);
			}
			break;
		}
	}

	void Dis_JumpType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		u32 off = ((op & 0x03FFFFFF) << 2);
		u32 addr = (pc & 0xF0000000) | off;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t->$%08x", name, addr);
	}

	void Dis_JumpRegType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		if ((op & 0x3f) == 9 && rd != MIPS_REG_RA)
			snprintf(out, outSize, "%s\t%s,->%s", name, RN(rd), RN(rs));
		else
			snprintf(out, outSize, "%s\t->%s", name, RN(rs));
	}

	void Dis_Allegrex(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize, "%s\t%s,%s", name, RN(rd), RN(rt));
	}

	void Dis_Allegrex2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		int rt = _RT;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		snprintf(out, outSize,"%s\t%s,%s", name, RN(rd), RN(rt));
	}

	void Dis_Emuhack(MIPSOpcode op, uint32_t pc, char *out, size_t outSize) {
		auto resolved = Memory::Read_Instruction(pc, true);
		char disasm[256];
		if (MIPS_IS_EMUHACK(resolved)) {
			truncate_cpy(disasm, sizeof(disasm), "(invalid emuhack)");
		} else {
			MIPSDisAsm(resolved, pc, disasm, sizeof(disasm), true);
		}

		switch (op.encoding >> 24) {
		case 0x68:
			snprintf(out, outSize, "* jitblock: %s", disasm);
			break;
		case 0x6a:
			snprintf(out, outSize, "* replacement: %s", disasm);
			break;
		default:
			snprintf(out, outSize, "* (invalid): %s", disasm);
			break;
		}
	}


}
