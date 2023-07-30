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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/IR/IRFrontend.h"
#include "Core/MIPS/IR/IRRegCache.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (opts.disableFlags & (uint32_t)JitDisable::flag) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }
#define INVALIDOP { Comp_Generic(op); return; }

namespace MIPSComp {
	void IRFrontend::Comp_ITypeMem(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;
		if (((op >> 29) & 1) == 0 && rt == MIPS_REG_ZERO) {
			// Don't load anything into $zr
			return;
		}

		CheckMemoryBreakpoint(rs, offset);

		switch (o) {
			// Load
		case 35:
			ir.Write(IROp::Load32, rt, rs, ir.AddConstant(offset));
			break;
		case 37:
			ir.Write(IROp::Load16, rt, rs, ir.AddConstant(offset));
			break;
		case 33:
			ir.Write(IROp::Load16Ext, rt, rs, ir.AddConstant(offset));
			break;
		case 36:
			ir.Write(IROp::Load8, rt, rs, ir.AddConstant(offset));
			break;
		case 32:
			ir.Write(IROp::Load8Ext, rt, rs, ir.AddConstant(offset));
			break;
			// Store
		case 43:
			ir.Write(IROp::Store32, rt, rs, ir.AddConstant(offset));
			break;
		case 41:
			ir.Write(IROp::Store16, rt, rs, ir.AddConstant(offset));
			break;
		case 40:
			ir.Write(IROp::Store8, rt, rs, ir.AddConstant(offset));
			break;

		case 34: //lwl
			ir.Write(IROp::Load32Left, rt, rs, ir.AddConstant(offset));
			break;
		case 38: //lwr
			ir.Write(IROp::Load32Right, rt, rs, ir.AddConstant(offset));
			break;
		case 42: //swl
			ir.Write(IROp::Store32Left, rt, rs, ir.AddConstant(offset));
			break;
		case 46: //swr
			ir.Write(IROp::Store32Right, rt, rs, ir.AddConstant(offset));
			break;

		default:
			INVALIDOP;
			return;
		}
	}

	void IRFrontend::Comp_StoreSync(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		int offset = _IMM16;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		// Note: still does something even if loading to zero.

		CheckMemoryBreakpoint(rs, offset);

		switch (op >> 26) {
		case 48: // ll
			ir.Write(IROp::Load32Linked, rt, rs, ir.AddConstant(offset));
			break;

		case 56: // sc
			ir.Write(IROp::Store32Conditional, rt, rs, ir.AddConstant(offset));
			break;

		default:
			INVALIDOP;
		}
	}

	void IRFrontend::Comp_Cache(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU);

		int func = (op >> 16) & 0x1F;

		// See Int_Cache for the definitions.
		switch (func) {
		case 24: break;
		case 25: break;
		case 27: break;
		case 30: break;
		default:
			// Fall back to the interpreter.
			DISABLE;
		}
	}
}
