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

#include <fstream>
#include "HLE.h"

#include "../Host.h"
#include "../MIPS/MIPS.h"
#include "../MIPS/MIPSAnalyst.h"
#include "../ELF/ElfReader.h"
#include "../Debugger/SymbolMap.h"
#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../Util/BlockAllocator.h"
#include "../PSPLoaders.h"
#include "../System.h"
#include "../MemMap.h"
#include "../Debugger/SymbolMap.h"

#include "sceKernel.h"
#include "sceKernelModule.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"

enum {
	PSP_THREAD_ATTR_USER = 0x80000000
};

struct Module : public KernelObject
{
	const char *GetName() {return name;}
	const char *GetTypeName() {return "Module";}
	void GetQuickInfo(char *ptr, int size)
	{
		// ignore size
		sprintf(ptr, "name=%s gp=%08x entry=%08x",
			name,
			gp_value,
			entry_addr);
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MODULE; }
	int GetIDType() const { return 0; }

	SceSize size;
	char nsegment;
	char reserved[3];
	int segmentaddr[4];
	int segmentsize[4];
	unsigned int entry_addr;
	unsigned int gp_value;
	unsigned int text_addr;
	unsigned int text_size;
	unsigned int data_size;
	unsigned int bss_size;
	// The following is only available in the v1.5 firmware and above,
	// but as sceKernelQueryModuleInfo is broken in v1.0 it doesn't matter.
	unsigned short attribute;
	unsigned char version[2];
	char name[28];
};

//////////////////////////////////////////////////////////////////////////
// MODULES
//////////////////////////////////////////////////////////////////////////
struct StartModuleInfo
{
	u32 size;
	u32 mpidtext;
	u32 mpiddata;
	u32 threadpriority;
	u32 threadattributes;
};

struct SceKernelLMOption {
	SceSize size;
	SceUID mpidtext;
	SceUID mpiddata;
	unsigned int flags;
	char position;
	char access;
	char creserved[2];
};

struct SceKernelSMOption {
	SceSize size;
	SceUID mpidstack;
	SceSize stacksize;
	int priority;
	unsigned int attribute;
};

//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static int numLoadedModules;
static SceUID mainModuleID;	// hack
// STATE END
//////////////////////////////////////////////////////////////////////////

Module *__KernelLoadELFFromPtr(const u8 *ptr, u32 loadAddress, SceUID &id, std::string *error_string)
{
	Module *m = new Module;
	kernelObjects.Create(m);

	u32 magic = *((u32*)ptr);

	if (magic == 0x5053507e) { // "~PSP"
		ERROR_LOG(HLE, "File encrypted - not yet supported");
		*error_string = "Executable encrypted - not yet supported";
		return 0;
	}
	
	if (magic == 0x00000000) {
		// BOOT.BIN is empty, we have to look for EBOOT.BIN which is encrypted.
		ERROR_LOG(HLE, "File encrypted - not yet supported (2)");
		*error_string = "Executable encrypted - not yet supported";
		return 0;
	}

	if (*(u32*)ptr != 0x464c457f)
	{
		char c[4];
		memcpy(c, ptr, 4);
		ERROR_LOG(HLE, "Wrong magic number %c%c%c%c %08x", c[0],c[1],c[2],c[3],*(u32*)ptr);
		*error_string = "File corrupt or encrypted?";
		return 0;
	}
	// Open ELF reader
	ElfReader reader((void*)ptr);

	if (!reader.LoadInto(loadAddress))
	{
		ERROR_LOG(HLE, "LoadInto failed");
		return 0;
	}

	struct libent
	{
		u32 exportName; //default 0
		u16 bcdVersion;
		u16 moduleAttributes;
		u8 exportEntrySize;
		u8 numVariables;
		u16 numFunctions;
		u32 __entrytableAddr;
	};

	SectionID entSection = reader.GetSectionByName(".lib.ent");
	SectionID textSection = reader.GetSectionByName(".text");

	u32 sceResidentAddr = 0;
	u32 moduleInfoAddr = 0;
	u32 textStart = reader.GetSectionAddr(textSection);
	u32 textSize = reader.GetSectionSize(textSection);

	SectionID sceResidentSection = reader.GetSectionByName(".rodata.sceResident");
	SectionID sceModuleInfoSection = reader.GetSectionByName(".rodata.sceModuleInfo");

	bool hasSymbols = false;
	bool dontadd = false;

	if (!host->AttemptLoadSymbolMap())
	{
		hasSymbols = reader.LoadSymbols();
		if (!hasSymbols)
		{
			symbolMap.ResetSymbolMap();
			MIPSAnalyst::ScanForFunctions(textStart, textStart+textSize);
		}
	}
	else
	{
		dontadd = true;
	}

	if (entSection != -1)
	{
		//libent *lib = (libent *)(Memory::GetPointer(reader.GetSectionAddr(entSection)));
		//what's this for?
		//lib->l1+=0;
	}

	sceResidentAddr = reader.GetSectionAddr(sceResidentSection);
	moduleInfoAddr = reader.GetSectionAddr(sceModuleInfoSection);

	struct PspResidentData 
	{
		u32 l1; // unknown 0xd632acdb
		u32 l2; // unknown 0xf01d73a7
		u32 startAddress; // address of _start
		u32 moduleInfoAddr; // address of sceModuleInfo struct
	};

	DEBUG_LOG(LOADER,"Resident data addr: %08x",	sceResidentAddr);

	//PspResidentData *resdata = (PspResidentData *)Memory::GetPointer(sceResidentAddr);

	struct PspModuleInfo
	{
		// 0, 0, 1, 1 ?
		u16 moduleAttrs; //0x0000 User Mode, 0x1000 Kernel Mode
		u16 moduleVersion;
		// 28 bytes of module name, packed with 0's.
		char name[28];
		u32 gp;					 // ptr to MIPS GOT data	(global offset table)
		u32 libent;			 // ptr to .lib.ent section 
		u32 libentend;		// ptr to end of .lib.ent section 
		u32 libstub;			// ptr to .lib.stub section 
		u32 libstubend;	 // ptr to end of .lib.stub section 
	};

	PspModuleInfo *modinfo = (PspModuleInfo *)Memory::GetPointer(moduleInfoAddr);
	m->gp_value = modinfo->gp;
	strncpy(m->name, modinfo->name, 28);	// TODO

	DEBUG_LOG(LOADER,"Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent,modinfo->libstub);

	struct PspLibStubEntry
	{
		// pointer to module name (will be in .rodata.sceResident section)
		u32 moduleNameSymbol;
		// mod version??
		unsigned short version;
		unsigned short val1;
		unsigned char val2; // 0x5
		unsigned char val3;
		// number of function symbols
		unsigned short numFuncs;
		// each symbol has an associated nid; nidData is a pointer
		// (in .rodata.sceNid section) to an array of longs, one
		// for each function, which identifies the function whose
		// address is to be inserted.
		//
		// The hash is the first 4 bytes of a SHA-1 hash of the function
		// name.	(Represented as a little-endian long, so the order
		// of the bytes is reversed.)
		u32 nidData;
		// the address of the function stubs where the function address jumps
		// should be filled in
		u32 firstSymAddr;
	};
	//sceDisplay at 5968-


	int numModules = (modinfo->libstubend - modinfo->libstub)/sizeof(PspLibStubEntry);

	DEBUG_LOG(LOADER,"Num Modules: %i",numModules);
	DEBUG_LOG(LOADER,"===================================================");

	PspLibStubEntry *entry = (PspLibStubEntry *)Memory::GetPointer(modinfo->libstub);

	int numSyms=0;
	for (int m=0; m<numModules; m++)
	{
		const char *modulename = (const char*)Memory::GetPointer(entry[m].moduleNameSymbol);
		u32 *nidDataPtr = (u32*)Memory::GetPointer(entry[m].nidData);
		//u32 *stubs = (u32*)Memory::GetPointer(entry[m].firstSymAddr);

		DEBUG_LOG(LOADER,"Importing Module %s, stubs at %08x",modulename,entry[m].firstSymAddr);

		for (int i=0; i<entry[m].numFuncs; i++)
		{
			u32 addrToWriteSyscall = entry[m].firstSymAddr+i*8;
			DEBUG_LOG(LOADER,"%s : %08x",GetFuncName(modulename, nidDataPtr[i]), addrToWriteSyscall);
			//write a syscall here
			WriteSyscall(modulename, nidDataPtr[i], addrToWriteSyscall);
			if (!dontadd)
			{
				char temp[256];
				sprintf(temp,"zz_%s", GetFuncName(modulename, nidDataPtr[i]));
				symbolMap.AddSymbol(temp, addrToWriteSyscall, 8, ST_FUNCTION);
			}
			numSyms++;
		}
		DEBUG_LOG(LOADER,"-------------------------------------------------------------");
	}

	m->entry_addr = reader.GetEntryPoint();

	return m;
}

bool __KernelLoadPBP(const char *filename, std::string *error_string)
{
	//static const char *FileNames[] =
	//{
	//	"PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "UNKNOWN.PNG",
	//	"PIC1.PNG", "SND0.AT3", "UNKNOWN.PSP", "UNKNOWN.PSAR"
	//};

	std::ifstream in(filename, std::ios::binary);

	char temp[4];
	in.read(temp,4);

	if (memcmp(temp,"\0PBP",4) != 0)
	{
		//This is not a valid file!
		ERROR_LOG(LOADER,"%s is not a valid homebrew PSP1.0 PBP",filename);
		*error_string = "Not a valid homebrew PBP";
		return false;
	}

	u32 version, offset0, offsets[16];
	int numfiles;

	in.read((char*)&version,4);

	in.read((char*)&offset0,4);
	numfiles = (offset0 - 8) / 4;
	offsets[0] = offset0;
	for (int i = 1; i < numfiles; i++)
		in.read((char*)&offsets[i], 4);

	// The 6th is always the executable?
	in.seekg(offsets[5]);
	//in.read((char*)&id,4);
	{
		u8 *temp = new u8[1024*1024*8];
		in.read((char*)temp, 1024*1024*8);
		SceUID id;
		Module *m = __KernelLoadELFFromPtr(temp, PSP_GetDefaultLoadAddress(), id, error_string);
		if (!m)
			return false;
		mipsr4k.pc = m->entry_addr;
		delete [] temp;
	}
	in.close();
	return true;
}


Module *__KernelLoadModule(u8 *fileptr, SceUID &id, SceKernelLMOption *options, std::string *error_string)
{
	Module *m = 0;
	// Check for PBP
	if (memcmp(fileptr, "\0PBP", 4) == 0)
	{
		// PBP!
		u32 version;
		memcpy(&version, fileptr + 4, 4);
		u32 offset0, offsets[16];
		int numfiles;

		memcpy(&offset0, fileptr + 8, 4);
		numfiles = (offset0 - 8)/4;
		offsets[0] = offset0;
		for (int i = 1; i < numfiles; i++)
			memcpy(&offsets[i], fileptr + 12 + 4*i, 4);
		m = __KernelLoadELFFromPtr(fileptr + offsets[5], PSP_GetDefaultLoadAddress(), id, error_string);
	}
	else
	{
		m = __KernelLoadELFFromPtr(fileptr, PSP_GetDefaultLoadAddress(), id, error_string);
	}

	return m;
}

void __KernelStartModule(Module *m, int args, const char *argp, SceKernelSMOption *options)
{
	__KernelSetupRootThread(m->GetUID(), args, argp, options->priority, options->stacksize, options->attribute);
	mainModuleID = m->GetUID();
	//TODO: if current thread, put it in wait state, waiting for the new thread
}


u32 __KernelGetModuleGP(SceUID module)
{
	u32 error;
	Module *m = kernelObjects.Get<Module>(module,error);
	if (m)
	{
		return m->gp_value;
	}
	else
	{
		return 0;
	}
}

bool __KernelLoadExec(const char *filename, SceKernelLoadExecParam *param, std::string *error_string)
{
	// Wipe kernel here, loadexec should reset the entire system
	__KernelInit();

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	s64 size = (s64)info.size;
	if (!size)
	{
		ERROR_LOG(LOADER, "File is size 0: %s", filename);
		return false;
	}
	u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);

	u8 *temp = new u8[(int)size];

	pspFileSystem.ReadFile(handle, temp, (size_t)size);

	SceUID moduleID;
	Module *m = __KernelLoadModule(temp, moduleID, 0, error_string);

	if (!m) {
		ERROR_LOG(LOADER, "Failed to load module %s", filename);
		return false;
	}

	mipsr4k.pc = m->entry_addr;

	INFO_LOG(LOADER, "Module entry: %08x", mipsr4k.pc);

	delete [] temp;

	pspFileSystem.CloseFile(handle);

	SceKernelSMOption option;
	option.size = sizeof(SceKernelSMOption);
	option.attribute = PSP_THREAD_ATTR_USER;
	option.mpidstack = 2;
	option.priority = 0x20;
	option.stacksize = 0x40000;	// crazy? but seems to be the truth

	__KernelStartModule(m, (u32)strlen(filename), filename, &option);

	__KernelStartIdleThreads();
	return true;
}

//TODO: second param
void sceKernelLoadExec()
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	SceKernelLoadExecParam *param = 0;
	if (PARAM(1))
	{
		param = (SceKernelLoadExecParam*)Memory::GetPointer(PARAM(1));
	}
	DEBUG_LOG(HLE,"sceKernelLoadExec(name=%s,...)", name);
	std::string error_string;
	if (!__KernelLoadExec(name, param, &error_string))
		ERROR_LOG(HLE, "sceKernelLoadExec failed: %s", error_string.c_str());
}

void sceKernelLoadModule()
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	u32 flags = PARAM(1);
	if (PARAM(2))
	{
		SceKernelLMOption *lmoption= (SceKernelLMOption *)Memory::GetPointer(PARAM(2));

		//TODO: Check if module name is in "blacklist" (HLE:d list)
		//If it is, don't load it.
		//Else, actually do load it and resolve pointers!

		DEBUG_LOG(HLE,"%i=sceKernelLoadModule(name=%s,flag=%08x,%08x,%08x,%08x,%08x(...))",
			numLoadedModules+1,name,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);

			
	}
	else
	{
		ERROR_LOG(HLE,"%i=sceKernelLoadModule(name=%s,flag=%08x,(...))", numLoadedModules+1, name, flags);
	}
	RETURN(++numLoadedModules);
}

void sceKernelStartModule()
{
	int id = PARAM(0);
	int argsize = PARAM(1);
	u32 argptr = PARAM(2);
	u32 ptrReturn = PARAM(3);
	//if (PARAM(4)) {
		//SceKernelSMOption *smoption = (SceKernelSMOption*)Memory::GetPointer(PARAM(4));
	//}
	ERROR_LOG(HLE,"UNIMPL sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,...)",
		id,argsize,argptr,ptrReturn);
	RETURN(0);
}

void sceKernelStopModule()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelStopModule");
	RETURN(0);

}

void sceKernelUnloadModule()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelUnloadModule");
	RETURN(0);

}

void sceKernelGetModuleIdByAddress()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelGetModuleIdByAddress(%08x)", PARAM(0));
	if (PARAM(0) == 0x08800000)
		RETURN(mainModuleID);
	else
		RETURN(0);
}
void sceKernelGetModuleId()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelGetModuleId");
	RETURN(0);

}

u32 sceKernelFindModuleByName(u32)
{
	ERROR_LOG(HLE,"UNIMPL sceKernelFindModuleByName()");
	return 1;
}

const HLEFunction ModuleMgrForUser[] = 
{
	{0x977DE386,sceKernelLoadModule,"sceKernelLoadModule"},
	{0xb7f46618,0,"sceKernelLoadModuleByID"},
	{0x50F0C1EC,sceKernelStartModule,"sceKernelStartModule"},
	{0xD675EBB8,sceKernelExitGame,"sceKernelSelfStopUnloadModule"}, //HACK
	{0xd1ff982a,sceKernelStopModule,"sceKernelStopModule"},
	{0x2e0911aa,sceKernelUnloadModule,"sceKernelUnloadModule"},
	{0x710F61B5,0,"sceKernelLoadModuleMs"},
	{0xF9275D98,0,"sceKernelLoadModuleBufferUsbWlan"}, ///???
	{0xCC1D3699,0,"sceKernelStopUnloadSelfModule"},
	{0x748CBED9,0,"sceKernelQueryModuleInfo"},
	{0xd8b73127,sceKernelGetModuleIdByAddress, "sceKernelGetModuleIdByAddress"},
	{0xf0a26395,sceKernelGetModuleId, "sceKernelGetModuleId"},
	{0x8f2df740,0,"sceKernel_ModuleMgr_8f2df740"},
};


void Register_ModuleMgrForUser()
{
	RegisterModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}
