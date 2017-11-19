#include "stdafx.h"
#include "CMipsInstruction.h"
#include "Core/Common.h"
#include "Mips.h"
#include "MipsOpcodes.h"
#include "Core/FileManager.h"
#include "MipsParser.h"

CMipsInstruction::CMipsInstruction(MipsOpcodeData& opcode, MipsImmediateData& immediate, MipsRegisterData& registers)
{
	this->opcodeData = opcode;
	this->immediateData = immediate;
	this->registerData = registers;

	addNop = false;
	IgnoreLoadDelay = Mips.GetIgnoreDelay();
}

CMipsInstruction::~CMipsInstruction()
{

}

int getImmediateBits(MipsImmediateType type)
{
	switch (type)
	{
	case MipsImmediateType::Immediate5:
		return 5;
	case MipsImmediateType::Immediate7:
		return 7;
	case MipsImmediateType::Immediate10:
		return 10;
	case MipsImmediateType::Immediate16:
	case MipsImmediateType::ImmediateHalfFloat:
		return 16;
	case MipsImmediateType::Immediate20:
	case MipsImmediateType::Immediate20_0:
		return 20;
	case MipsImmediateType::Immediate26:
		return 26;
	default:
		return 0;
	}
}

// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/Allegrex/VfpuState.java?spec=svn3676&r=3383#1196
int CMipsInstruction::floatToHalfFloat(int i)
{
	int s = ((i >> 16) & 0x00008000); // sign
	int e = ((i >> 23) & 0x000000ff) - (127 - 15); // exponent
	int f = ((i >> 0) & 0x007fffff); // fraction

	// need to handle NaNs and Inf?
	if (e <= 0) {
		if (e < -10) {
			if (s != 0) {
				// handle -0.0
				return 0x8000;
			}
			return 0;
		}
		f = (f | 0x00800000) >> (1 - e);
		return s | (f >> 13);
	} else if (e == 0xff - (127 - 15)) {
		if (f == 0) {
			// Inf
			return s | 0x7c00;
		}
		// NAN
		return s | 0x7fff;
	}

	if (e > 30) {
		// Overflow
		return s | 0x7c00;
	}

	return s | (e << 10) | (f >> 13);
}

bool CMipsInstruction::Validate()
{
	bool Result = false;

	bool previousNop = addNop;
	addNop = false;

	RamPos = g_fileManager->getVirtualAddress();
	if (RamPos % 4)
	{
		Logger::queueError(Logger::Error,L"opcode not aligned to word boundary");
		return false;
	}

	// check immediates
	if (immediateData.primary.type != MipsImmediateType::None)
	{
		if (immediateData.primary.expression.isLoaded())
		{
			if (immediateData.primary.expression.evaluateInteger(immediateData.primary.value) == false)
			{
				Logger::queueError(Logger::Error, L"Invalid immediate expression");
				return false;
			}

			immediateData.primary.originalValue = immediateData.primary.value;
		}

		if (immediateData.primary.type == MipsImmediateType::ImmediateHalfFloat)
			immediateData.primary.value = floatToHalfFloat(immediateData.primary.originalValue);

		if (opcodeData.opcode.flags & MO_IMMALIGNED)	// immediate must be aligned
		{
			if (immediateData.primary.value % 4)
			{
				Logger::queueError(Logger::Error,L"Immediate must be word aligned");
				return false;
			}
		}

		if (opcodeData.opcode.flags & MO_NEGIMM) 		// negated immediate
		{
			immediateData.primary.value = -immediateData.primary.value;
		} else if (opcodeData.opcode.flags & MO_IPCA)	// absolute value >> 2
		{
			immediateData.primary.value = (immediateData.primary.value >> 2) & 0x3FFFFFF;
		} else if (opcodeData.opcode.flags & MO_IPCR)	// relative 16 bit value
		{
			int num = (int) (immediateData.primary.value-RamPos-4);
			
			if (num > 0x20000 || num < (-0x20000))
			{
				Logger::queueError(Logger::Error,L"Branch target %08X out of range",immediateData.primary.value);
				return false;
			}
			immediateData.primary.value = num >> 2;
		} else if (opcodeData.opcode.flags & (MO_RSP_HWOFFSET | MO_RSP_WOFFSET | MO_RSP_DWOFFSET | MO_RSP_QWOFFSET))
		{
			int shift = 0;

			if (opcodeData.opcode.flags & MO_RSP_HWOFFSET) shift = 1;
			else if (opcodeData.opcode.flags & MO_RSP_WOFFSET) shift = 2;
			else if (opcodeData.opcode.flags & MO_RSP_DWOFFSET) shift = 3;
			else if (opcodeData.opcode.flags & MO_RSP_QWOFFSET) shift = 4;

			if (immediateData.primary.value & ((1 << shift) - 1))
			{
				Logger::queueError(Logger::Error,L"Offset must be %d-byte aligned",1<<shift);
				return false;
			}
			immediateData.primary.value = immediateData.primary.value >> shift;
		}
		
		int immediateBits = getImmediateBits(immediateData.primary.type);
		unsigned int mask = (0xFFFFFFFF << (32-immediateBits)) >> (32-immediateBits);
		int digits = (immediateBits+3) / 4;

		if ((unsigned int)std::abs(immediateData.primary.value) > mask)
		{
			Logger::queueError(Logger::Error,L"Immediate value 0x%0*X out of range",digits,immediateData.primary.value);
			return false;
		}

		immediateData.primary.value &= mask;
	}

	if (immediateData.secondary.type != MipsImmediateType::None)
	{
		if (immediateData.secondary.expression.isLoaded())
		{
			if (immediateData.secondary.expression.evaluateInteger(immediateData.secondary.value) == false)
			{
				Logger::queueError(Logger::Error, L"Invalid immediate expression");
				return false;
			}

			immediateData.secondary.originalValue = immediateData.secondary.value;
		}

		switch (immediateData.secondary.type)
		{
		case MipsImmediateType::CacheOp:
			if ((unsigned int)immediateData.secondary.value > 0x1f)
			{
				Logger::queueError(Logger::Error,L"Immediate value %02X out of range",immediateData.secondary.value);
				return false;
			}
			break;
		case MipsImmediateType::Ext:
		case MipsImmediateType::Ins:
			if (immediateData.secondary.value > 32 || immediateData.secondary.value == 0)
			{
				Logger::queueError(Logger::Error,L"Immediate value %02X out of range",immediateData.secondary.value);
				return false;
			}
		
			immediateData.secondary.value--;
			if (immediateData.secondary.type == MipsImmediateType::Ins)
				immediateData.secondary.value += immediateData.primary.value;
			break;
		}
	}

	// check load delay
	if (Mips.hasLoadDelay() && Mips.GetLoadDelay() && IgnoreLoadDelay == false)
	{
		bool fix = false;

		if (registerData.grd.num != -1 && registerData.grd.num == Mips.GetLoadDelayRegister())
		{
			Logger::queueError(Logger::Warning,L"register %S may not be available due to load delay",registerData.grd.name);
			fix = true;
		} else if (registerData.grs.num != -1 && registerData.grs.num == Mips.GetLoadDelayRegister())
		{
			Logger::queueError(Logger::Warning,L"register %S may not be available due to load delay",registerData.grs.name);
			fix = true;
		} else if (registerData.grt.num != -1 && registerData.grt.num == Mips.GetLoadDelayRegister()
			&& !(opcodeData.opcode.flags & MO_IGNORERTD))
		{
			Logger::queueError(Logger::Warning,L"register %S may not be available due to load delay",registerData.grt.name);
			fix = true;
		}

		if (Mips.GetFixLoadDelay() == true && fix == true)
		{
			addNop = true;
			Logger::queueError(Logger::Notice,L"added nop to ensure correct behavior");
		}
	}

	if ((opcodeData.opcode.flags & MO_NODELAYSLOT) && Mips.GetDelaySlot() == true && IgnoreLoadDelay == false)
	{
		Logger::queueError(Logger::Error,L"This instruction can't be in a delay slot");
	}

	Mips.SetDelaySlot(opcodeData.opcode.flags & MO_DELAY ? true : false);

	// now check if this opcode causes a load delay
	if (Mips.hasLoadDelay())
		Mips.SetLoadDelay(opcodeData.opcode.flags & MO_DELAYRT ? true : false,registerData.grt.num);
	
	if (previousNop != addNop)
		Result = true;

	g_fileManager->advanceMemory(addNop ? 8 : 4);
	return Result;
}

void CMipsInstruction::encodeNormal() const
{
	int encoding = opcodeData.opcode.destencoding;

	if (registerData.grs.num != -1) encoding |= MIPS_RS(registerData.grs.num);	// source reg
	if (registerData.grt.num != -1) encoding |= MIPS_RT(registerData.grt.num);	// target reg
	if (registerData.grd.num != -1) encoding |= MIPS_RD(registerData.grd.num);	// dest reg
	
	if (registerData.frt.num != -1) encoding |= MIPS_FT(registerData.frt.num);	// float target reg
	if (registerData.frs.num != -1) encoding |= MIPS_FS(registerData.frs.num);	// float source reg
	if (registerData.frd.num != -1) encoding |= MIPS_FD(registerData.frd.num);	// float dest reg

	if (registerData.ps2vrt.num != -1) encoding |= (registerData.ps2vrt.num << 16);	// ps2 vector target reg
	if (registerData.ps2vrs.num != -1) encoding |= (registerData.ps2vrs.num << 21);	// ps2 vector source reg
	if (registerData.ps2vrd.num != -1) encoding |= (registerData.ps2vrd.num << 6);	// ps2 vector dest reg

	if (registerData.rspvrt.num != -1) encoding |= MIPS_FT(registerData.rspvrt.num);	// rsp vector target reg
	if (registerData.rspvrs.num != -1) encoding |= MIPS_FS(registerData.rspvrs.num);	// rsp vector source reg
	if (registerData.rspvrd.num != -1) encoding |= MIPS_FD(registerData.rspvrd.num);	// rsp vector dest reg

	if (registerData.rspve.num != -1) encoding |= MIPS_RSP_VE(registerData.rspve.num);			// rsp element
	if (registerData.rspvde.num != -1) encoding |= MIPS_RSP_VDE(registerData.rspvde.num);		// rsp destination element
	if (registerData.rspvealt.num != -1) encoding |= MIPS_RSP_VEALT(registerData.rspvealt.num);	// rsp element (alt. placement)

	if (!(opcodeData.opcode.flags & MO_VFPU_MIXED) && registerData.vrt.num != -1)			// vfpu rt
		encoding |= registerData.vrt.num << 16;

	switch (immediateData.primary.type)
	{
	case MipsImmediateType::Immediate5:
	case MipsImmediateType::Immediate10:
	case MipsImmediateType::Immediate20:
		encoding |= immediateData.primary.value << 6;
		break;
	case MipsImmediateType::Immediate16:
	case MipsImmediateType::Immediate26:
	case MipsImmediateType::Immediate20_0:
	case MipsImmediateType::Immediate7:
	case MipsImmediateType::ImmediateHalfFloat:
		encoding |= immediateData.primary.value;
		break;
	}

	switch (immediateData.secondary.type)
	{
	case MipsImmediateType::CacheOp:
		encoding |= immediateData.secondary.value << 16;
		break;
	case MipsImmediateType::Ext:
	case MipsImmediateType::Ins:
		encoding |= immediateData.secondary.value << 11;
		break;
	case MipsImmediateType::Cop2BranchType:
		encoding |= immediateData.secondary.value << 18;
		break;
	}

	if (opcodeData.opcode.flags & MO_VFPU_MIXED)
	{
		// always vrt
		encoding |= registerData.vrt.num >> 5;
		encoding |= (registerData.vrt.num & 0x1F) << 16;
	}

	g_fileManager->writeU32((uint32_t)encoding);
}

void CMipsInstruction::encodeVfpu() const
{
	int encoding = opcodeData.opcode.destencoding;
	
	if (opcodeData.vectorCondition != -1) encoding |= (opcodeData.vectorCondition << 0);
	if (registerData.vrd.num != -1) encoding |= (registerData.vrd.num << 0);
	if (registerData.vrs.num != -1) encoding |= (registerData.vrs.num << 8);
	if (registerData.vrt.num != -1) encoding |= (registerData.vrt.num << 16);
	if (opcodeData.vfpuSize != -1 && (opcodeData.opcode.flags & (MO_VFPU_PAIR|MO_VFPU_SINGLE|MO_VFPU_TRIPLE|MO_VFPU_QUAD)) == 0)
	{
		if (opcodeData.vfpuSize & 1) encoding |= (1 << 7);
		if (opcodeData.vfpuSize & 2) encoding |= (1 << 15);
	}

	if (registerData.grt.num != -1) encoding |= (registerData.grt.num << 16);
	
	switch (immediateData.primary.type)
	{
	case MipsImmediateType::Immediate5:
		encoding |= immediateData.primary.value << 16;
		break;
	case MipsImmediateType::Immediate7:
		encoding |= immediateData.primary.value << 0;
		break;
	}

	g_fileManager->writeU32((uint32_t)encoding);
}

void CMipsInstruction::Encode() const
{
	if (addNop)
		g_fileManager->writeU32(0);

	if (opcodeData.opcode.flags & MO_VFPU)
		encodeVfpu();
	else
		encodeNormal();
}

void CMipsInstruction::writeTempData(TempData& tempData) const
{
	MipsOpcodeFormatter formatter;
	tempData.writeLine(RamPos,formatter.formatOpcode(opcodeData,registerData,immediateData));
}
