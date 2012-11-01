// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "HLE.h"
#include <map>
#include "../MemMap.h"

#include "HLETables.h"
#include "../System.h"
#include "sceDisplay.h"
#include "sceIo.h"
#include "sceAudio.h"
#include "sceKernelMemory.h"
#include "../MIPS/MIPSCodeUtils.h"

static std::vector<HLEModule> moduleDB;

void HLEInit()
{
	RegisterAllModules();
}

void HLEShutdown()
{
	moduleDB.clear();
}

/*
//unused
struct PSPHeader
{
	char psp[4];
	u32 version; //00080000
	short whatever; //0101
	char sometext[28];
	short whatever2; //0102
	u32 filesizedecrypted;
	u32 filesize;
	u32 unknownoffsets[3];
	short whatever3[2]; //0x40 0x40
	u32 whatever4[2]; //00
	u32 whatever5; //1067008
	u32 whatever6[2]; //00
	u32 whatever7; //1067008
	u32 whatever8; //2553296
	u32 whatever9[8]; //0
	u32 whatever10; //12
	u32 encryptedStuff[12];
	u32 filesizedecrypted2;
	u32 whatever11;//0x80
	u32 whatever12[6];
};*/

void RegisterModule(const char *name, int numFunctions, const HLEFunction *funcTable)
{
	HLEModule module = {name, numFunctions, funcTable};
	moduleDB.push_back(module);
}

int GetModuleIndex(const char *moduleName)
{
	for (size_t i = 0; i < moduleDB.size(); i++)
		if (strcmp(moduleName, moduleDB[i].name) == 0)
			return (int)i;
	return -1;
}

int GetFuncIndex(int moduleIndex, u32 nib)
{
	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++)
	{
		if (module.funcTable[i].ID == nib)
			return i;
	}
	return -1;
}

u32 GetNibByName(const char *moduleName, const char *function)
{
	int moduleIndex = GetModuleIndex(moduleName);
	const HLEModule &module = moduleDB[moduleIndex];
	for (int i = 0; i < module.numFunctions; i++)
	{
		if (!strcmp(module.funcTable[i].name, function))
			return module.funcTable[i].ID;
	}
	return -1;
}

const HLEFunction *GetFunc(const char *moduleName, u32 nib)
{
	int moduleIndex = GetModuleIndex(moduleName);
	if (moduleIndex != -1)
	{
		int idx = GetFuncIndex(moduleIndex, nib);
		if (idx != -1)
			return &(moduleDB[moduleIndex].funcTable[idx]);
	}
	return 0;
}

const char *GetFuncName(const char *moduleName, u32 nib)
{
	const HLEFunction *func = GetFunc(moduleName,nib);
	if (func)
		return func->name;
	else
	{
		static char temp[256];
		sprintf(temp,"[UNK:%08x]",nib);
		return temp;
	}
}

u32 GetSyscallOp(const char *moduleName, u32 nib)
{
	int modindex = GetModuleIndex(moduleName);
	if (modindex != -1)
	{
		int funcindex = GetFuncIndex(modindex, nib);
		if (funcindex != -1)
		{
			return (0x0000000c | (modindex<<18) | (funcindex<<6));
		}
		else
		{
			return (0x0003FFCC | (modindex<<18));  // invalid syscall
		}
	}
	else
	{
		ERROR_LOG(HLE, "Unknown module %s!", moduleName);
		return (0x0003FFCC);	// invalid syscall
	}
}

void WriteSyscall(const char *moduleName, u32 nib, u32 address)
{
	if (nib == 0)
	{
		Memory::Write_U32(MIPS_MAKE_JR_RA(), address); //patched out?
		Memory::Write_U32(MIPS_MAKE_NOP(), address+4); //patched out?
		return;
	}
	Memory::Write_U32(MIPS_MAKE_JR_RA(), address); // jr ra
	Memory::Write_U32(GetSyscallOp(moduleName, nib), address + 4);
}

const char *GetFuncName(int moduleIndex, int func)
{
	if (moduleIndex >= 0 && moduleIndex < (int)moduleDB.size())
	{
		const HLEModule &module = moduleDB[moduleIndex];
		if (func>=0 && func <= module.numFunctions)
		{
			return module.funcTable[func].name;
		}
	}
	return "[unknown]";
}

void CallSyscall(u32 op)
{
	u32 callno = (op >> 6) & 0xFFFFF; //20 bits
	int funcnum = callno & 0xFFF;
	int modulenum = (callno & 0xFF000) >> 12;
	if (funcnum == 0xfff)
	{
		_dbg_assert_msg_(HLE,0,"Unknown syscall");
		ERROR_LOG(HLE,"Unknown syscall: Module: %s", moduleDB[modulenum].name); 
		return;
	}
	HLEFunc func = moduleDB[modulenum].funcTable[funcnum].func;
	if (func)
	{
		func();
	}
	else
	{
		ERROR_LOG(HLE,"Unimplemented HLE function %s", moduleDB[modulenum].funcTable[funcnum].name);
	}
}

