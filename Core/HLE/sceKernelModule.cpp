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

#include <fstream>
#include <algorithm>

#include "HLE.h"
#include "Common/FileUtil.h"
#include "../Host.h"
#include "../MIPS/MIPS.h"
#include "../MIPS/MIPSAnalyst.h"
#include "../ELF/ElfReader.h"
#include "../ELF/PrxDecrypter.h"
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
#include "sceIo.h"

enum {
	PSP_THREAD_ATTR_USER = 0x80000000
};

enum {
	// Function exports.
	NID_MODULE_START = 0xD632ACDB,
	NID_MODULE_STOP = 0xCEE8593C,
	NID_MODULE_REBOOT_BEFORE = 0x2F064FA6,
	NID_MODULE_REBOOT_PHASE = 0xADF12745,
	NID_MODULE_BOOTSTART = 0xD3744BE0,

	// Variable exports.
	NID_MODULE_INFO = 0xF01D73A7,
	NID_MODULE_START_THREAD_PARAMETER = 0x0F7C276C,
	NID_MODULE_STOP_THREAD_PARAMETER = 0xCF0CC697,
	NID_MODULE_REBOOT_BEFORE_THREAD_PARAMETER = 0xF4F4299D,
	NID_MODULE_SDK_VERSION = 0x11B97506,
};

static const char *blacklistedModules[] = {
	"sceATRAC3plus_Library",
	"sceFont_Library",
	"SceFont_Library",
	"sceNetAdhocctl_Library",
	"sceNetAdhocDownload_Library",
	"sceNetAdhocMatching_Library",
	"sceNetAdhoc_Library",
	"sceNetApctl_Library",
	"sceNetInet_Library",
	"sceNet_Library",
	"sceMpeg_library",
};

struct NativeModule {
	u32 next;
	u16 attribute;
	u8 version[2];
	char name[28];
	u32 status;
	u32 unk1;
	u32 usermod_thid;
	u32 memid;
	u32 mpidtext;
	u32 mpiddata;
	u32 ent_top;
	u32 ent_size;
	u32 stub_top;
	u32 stub_size;
	u32 module_start_func;
	u32 module_stop_func;
	u32 module_bootstart_func;
	u32 module_reboot_before_func;
	u32 module_reboot_phase_func;
	u32 entry_addr;
	u32 gp_value;
	u32 text_addr;
	u32 text_size;
	u32 data_size;
	u32 bss_size;
	u32 nsegment;
	u32 segmentaddr[4];
	u32 segmentsize[4];
	u32 module_start_thread_priority;
	u32 module_start_thread_stacksize;
	u32 module_start_thread_attr;
	u32 module_stop_thread_priority;
	u32 module_stop_thread_stacksize;
	u32 module_stop_thread_attr;
	u32 module_reboot_before_thread_priority;
	u32 module_reboot_before_thread_stacksize;
	u32 module_reboot_before_thread_attr;
};

// by QueryModuleInfo
struct ModuleInfo {
	u32 nsegment;
	u32 segmentaddr[4];
	u32 segmentsize[4];
	u32 entry_addr;
	u32 gp_value;
	u32 text_addr;
	u32 text_size;
	u32 data_size;
	u32 bss_size;
	u16 attribute;
	u8 version[2];
	char name[28];
};


class Module : public KernelObject
{
public:
	Module() : memoryBlockAddr(0), isFake(false) {}
	~Module() {
		if (memoryBlockAddr) {
			userMemory.Free(memoryBlockAddr);
		}
	}
	const char *GetName() {return nm.name;}
	const char *GetTypeName() {return "Module";}
	void GetQuickInfo(char *ptr, int size)
	{
		// ignore size
		sprintf(ptr, "%sname=%s gp=%08x entry=%08x",
			isFake ? "faked " : "",
			nm.name,
			nm.gp_value,
			nm.entry_addr);
	}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MODULE; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_Module; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nm);
		p.Do(memoryBlockAddr);
		p.DoMarker("Module");
	}

	NativeModule nm;

	u32 memoryBlockAddr;
	bool isFake;
};

KernelObject *__KernelModuleObject()
{
	return new Module;
}

class AfterModuleEntryCall : public Action {
public:
	AfterModuleEntryCall() {}
	SceUID moduleID_;
	u32 retValAddr;
	virtual void run(MipsCall &call);
	virtual void DoState(PointerWrap &p) {
		p.Do(moduleID_);
		p.Do(retValAddr);
		p.DoMarker("AfterModuleEntryCall");
	}
	static Action *Create() {
		return new AfterModuleEntryCall;
	}
};

void AfterModuleEntryCall::run(MipsCall &call) {
	Memory::Write_U32(retValAddr, currentMIPS->r[2]);
}

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
static int actionAfterModule;
static SceUID mainModuleID;	// hack
// STATE END
//////////////////////////////////////////////////////////////////////////

void __KernelModuleInit()
{
	actionAfterModule = __KernelRegisterActionType(AfterModuleEntryCall::Create);
}

void __KernelModuleDoState(PointerWrap &p)
{
	p.Do(mainModuleID);
	p.Do(actionAfterModule);
	__KernelRestoreActionType(actionAfterModule, AfterModuleEntryCall::Create);
	p.DoMarker("sceKernelModule");
}

Module *__KernelLoadELFFromPtr(const u8 *ptr, u32 loadAddress, std::string *error_string)
{
	Module *module = new Module;
	kernelObjects.Create(module);
	memset(&module->nm, 0, sizeof(module->nm));

	u8 *newptr = 0;
	if (*(u32*)ptr == 0x4543537e) { // "~SCE"
		INFO_LOG(HLE, "~SCE module, skipping header");
		ptr += *(u32*)(ptr + 4);
	}

	if (*(u32*)ptr == 0x5053507e) { // "~PSP"
		// Decrypt module! YAY!
		INFO_LOG(HLE, "Decrypting ~PSP file");
		PSP_Header *head = (PSP_Header*)ptr;
		const u8 *in = ptr;
		u32 size = head->elf_size;
		if (head->psp_size > size)
		{
			size = head->psp_size;
		}
		newptr = new u8[head->elf_size + head->psp_size];
		ptr = newptr;
		int ret = pspDecryptPRX(in, (u8*)ptr, head->psp_size);
		if (ret == MISSING_KEY)
		{
			*error_string = "Missing key";
			delete [] newptr;
			module->isFake = true;
			strncpy(module->nm.name, head->modname, 28);
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;
			return module;
		}
		else if (ret <= 0)
		{
			ERROR_LOG(HLE, "Failed decrypting PRX! That's not normal!\n");
		}
	}

	if (*(u32*)ptr != 0x464c457f)
	{
		ERROR_LOG(HLE, "Wrong magic number %08x",*(u32*)ptr);
		*error_string = "File corrupt";
		if (newptr)
		{
			delete [] newptr;
		}
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	// Open ELF reader
	ElfReader reader((void*)ptr);

	if (!reader.LoadInto(loadAddress))
	{
		ERROR_LOG(HLE, "LoadInto failed");
		if (newptr)
		{
			delete [] newptr;
		}
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	module->memoryBlockAddr = reader.GetVaddr();

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

	SectionID sceModuleInfoSection = reader.GetSectionByName(".rodata.sceModuleInfo");
	PspModuleInfo *modinfo;
	if (sceModuleInfoSection != -1)
		modinfo = (PspModuleInfo *)Memory::GetPointer(reader.GetSectionAddr(sceModuleInfoSection));
	else
		modinfo = (PspModuleInfo *)Memory::GetPointer(reader.GetSegmentVaddr(0) + (reader.GetSegmentPaddr(0) & 0x7FFFFFFF) - reader.GetSegmentOffset(0));

	module->nm.gp_value = modinfo->gp;
	strncpy(module->nm.name, modinfo->name, 28);

	// Check for module blacklist - we don't allow games to load these modules from disc
	// as we have HLE implementations and the originals won't run in the emu because they
	// directly access hardware or for other reasons.
	for (u32 i = 0; i < ARRAY_SIZE(blacklistedModules); i++) {
		if (strcmp(modinfo->name, blacklistedModules[i]) == 0) {
			*error_string = "Blacklisted";
			if (newptr)
			{
				delete [] newptr;
			}
			module->isFake = true;
			module->nm.entry_addr = -1;
			return module;
		}
	}

	bool hasSymbols = false;
	bool dontadd = false;

	SectionID textSection = reader.GetSectionByName(".text");

	if (textSection != -1)
	{
		u32 textStart = reader.GetSectionAddr(textSection);
		u32 textSize = reader.GetSectionSize(textSection);

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
	}

	INFO_LOG(LOADER,"Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent,modinfo->libstub);

	struct PspLibStubEntry
	{
		u32 name;
		u16 version;
		u16 flags;
		u16 size;
		u16 numFuncs;
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

	int numModules = (modinfo->libstubend - modinfo->libstub)/sizeof(PspLibStubEntry);

	DEBUG_LOG(LOADER,"Num Modules: %i",numModules);
	DEBUG_LOG(LOADER,"===================================================");

	PspLibStubEntry *entry = (PspLibStubEntry *)Memory::GetPointer(modinfo->libstub);

	int numSyms=0;
	for (int m = 0; m < numModules; m++)
	{
		const char *modulename = (const char*)Memory::GetPointer(entry[m].name);
		u32 *nidDataPtr = (u32*)Memory::GetPointer(entry[m].nidData);
		// u32 *stubs = (u32*)Memory::GetPointer(entry[m].firstSymAddr);

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

	// Look at the exports, too.

	struct PspLibEntEntry
	{
		u32 name; /* ent's name (module name) address */
		u16 version;
		u16 flags;
		u8 size;
		u8 vcount;
		u16 fcount;
		u32 resident;
	};

	int numEnts = (modinfo->libentend - modinfo->libent)/sizeof(PspLibEntEntry);
	PspLibEntEntry *ent = (PspLibEntEntry *)Memory::GetPointer(modinfo->libent);
	for (int m=0; m<numEnts; m++)
	{
		const char *name;
		if (ent->size == 0)
		{
			continue;
		}

		if (ent->name == 0)
		{
			// ?
			name = module->nm.name;
		}
		else
		{
			name = (const char*)Memory::GetPointer(ent->name);
		}

		INFO_LOG(HLE,"Exporting ent %d named %s, %d funcs, %d vars, resident %08x", m, name, ent->fcount, ent->vcount, ent->resident);
		
		u32 *residentPtr = (u32*)Memory::GetPointer(ent->resident);

		for (u32 j = 0; j < ent->fcount; j++)
		{
			u32 nid = residentPtr[j];
			u32 exportAddr = residentPtr[ent->fcount + ent->vcount + j];

			switch (nid)
			{
			case NID_MODULE_START:
				module->nm.module_start_func = exportAddr;
				break;
			case NID_MODULE_STOP:
				module->nm.module_stop_func = exportAddr;
				break;
			case NID_MODULE_REBOOT_BEFORE:
				module->nm.module_reboot_before_func = exportAddr;
				break;
			case NID_MODULE_REBOOT_PHASE:
				module->nm.module_reboot_phase_func = exportAddr;
				break;
			case NID_MODULE_BOOTSTART:
				module->nm.module_bootstart_func = exportAddr;
				break;
			default:
				ResolveSyscall(name, nid, exportAddr);
			}
		}

		for (u32 j = 0; j < ent->vcount; j++)
		{
			u32 nid = residentPtr[ent->fcount + j];
			u32 exportAddr = residentPtr[ent->fcount + ent->vcount + ent->fcount + j];

			switch (nid)
			{
			case NID_MODULE_INFO:
				break;
			case NID_MODULE_START_THREAD_PARAMETER:
				if (Memory::Read_U32(exportAddr) != 3)
					WARN_LOG(LOADER, "Strange value at module_start_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_start_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_start_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_start_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_STOP_THREAD_PARAMETER:
				if (Memory::Read_U32(exportAddr) != 3)
					WARN_LOG(LOADER, "Strange value at module_stop_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_stop_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_stop_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_stop_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_REBOOT_BEFORE_THREAD_PARAMETER:
				if (Memory::Read_U32(exportAddr) != 3)
					WARN_LOG(LOADER, "Strange value at module_reboot_before_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_reboot_before_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_reboot_before_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_reboot_before_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_SDK_VERSION:
				DEBUG_LOG(LOADER, "Module SDK: %08x", Memory::Read_U32(exportAddr));
				break;
			default:
				DEBUG_LOG(LOADER, "Unexpected variable with nid: %08x", nid);
				break;
			}
		}

		if (ent->size > 4)
		{
			ent = (PspLibEntEntry*)((u8*)ent + ent->size * 4);
		}
		else
		{
			ent++;
		}
	}

	module->nm.entry_addr = reader.GetEntryPoint();

	if (newptr)
	{
		delete [] newptr;
	}
	return module;
}

bool __KernelLoadPBP(const char *filename, std::string *error_string)
{
	static const char *FileNames[] =
	{
		"PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "UNKNOWN.PNG",
		"PIC1.PNG", "SND0.AT3", "UNKNOWN.PSP", "UNKNOWN.PSAR"
	};

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
		u8 *elftemp = new u8[1024*1024*8];
		in.read((char*)elftemp, 1024*1024*8);
		Module *module = __KernelLoadELFFromPtr(elftemp, PSP_GetDefaultLoadAddress(), error_string);
		if (!module)
			return false;
		mipsr4k.pc = module->nm.entry_addr;
		delete [] elftemp;
	}
	in.close();
	return true;
}

Module *__KernelLoadModule(u8 *fileptr, SceKernelLMOption *options, std::string *error_string)
{
	Module *module = 0;
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
		module = __KernelLoadELFFromPtr(fileptr + offsets[5], PSP_GetDefaultLoadAddress(), error_string);
	}
	else
	{
		module = __KernelLoadELFFromPtr(fileptr, PSP_GetDefaultLoadAddress(), error_string);
	}

	return module;
}

void __KernelStartModule(Module *m, int args, const char *argp, SceKernelSMOption *options)
{
	if (m->nm.module_start_func != 0 && m->nm.module_start_func != -1)
	{
		if (m->nm.module_start_func != m->nm.entry_addr)
			WARN_LOG(LOADER, "Main module has start func (%08x) different from entry (%08x)?", m->nm.module_start_func, m->nm.entry_addr);
	}

	__KernelSetupRootThread(m->GetUID(), args, argp, options->priority, options->stacksize, options->attribute);
	mainModuleID = m->GetUID();
	//TODO: if current thread, put it in wait state, waiting for the new thread
}


u32 __KernelGetModuleGP(SceUID uid)
{
	u32 error;
	Module *module = kernelObjects.Get<Module>(uid, error);
	if (module)
	{
		return module->nm.gp_value;
	}
	else
	{
		return 0;
	}
}

bool __KernelLoadExec(const char *filename, SceKernelLoadExecParam *param, std::string *error_string)
{
	// Wipe kernel here, loadexec should reset the entire system
	if (__KernelIsRunning())
		__KernelShutdown();

	__KernelModuleInit();
	__KernelInit();
	
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);

	u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);

	u8 *temp = new u8[(int)info.size + 0x1000000];

	pspFileSystem.ReadFile(handle, temp, (size_t)info.size);

	Module *module = __KernelLoadModule(temp, 0, error_string);

	if (!module) {
		ERROR_LOG(LOADER, "Failed to load module %s", filename);
		return false;
	}

	mipsr4k.pc = module->nm.entry_addr;

	INFO_LOG(LOADER, "Module entry: %08x", mipsr4k.pc);

	delete [] temp;

	pspFileSystem.CloseFile(handle);

	SceKernelSMOption option;
	option.size = sizeof(SceKernelSMOption);
	option.attribute = PSP_THREAD_ATTR_USER;
	option.mpidstack = 2;
	option.priority = 0x20;
	option.stacksize = 0x40000;	// crazy? but seems to be the truth

	// Replace start options with module-specified values if they exist.
	if (module->nm.module_start_thread_attr != 0)
		option.attribute = module->nm.module_start_thread_attr;
	if (module->nm.module_start_thread_priority != 0)
		option.priority = module->nm.module_start_thread_priority;
	if (module->nm.module_start_thread_stacksize != 0)
		option.stacksize = module->nm.module_start_thread_stacksize;

	__KernelStartModule(module, (u32)strlen(filename) + 1, filename, &option);

	__KernelStartIdleThreads();
	return true;
}

//TODO: second param
int sceKernelLoadExec(const char *filename, u32 paramPtr)
{
	SceKernelLoadExecParam *param = 0;
	if (paramPtr)
	{
		param = (SceKernelLoadExecParam*)Memory::GetPointer(paramPtr);
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	
	if (!info.exists) {
		ERROR_LOG(LOADER, "sceKernelLoadExec(%s, ...): File does not exist", filename);
		return SCE_KERNEL_ERROR_NOFILE;
	}

	s64 size = (s64)info.size;
	if (!size)
	{
		ERROR_LOG(LOADER, "sceKernelLoadExec(%s, ...): File is size 0", filename);
		return SCE_KERNEL_ERROR_ILLEGAL_OBJECT;
	}

	DEBUG_LOG(HLE,"sceKernelLoadExec(name=%s,...)", filename);
	std::string error_string;
	if (!__KernelLoadExec(filename, param, &error_string)) {
		ERROR_LOG(HLE, "sceKernelLoadExec failed: %s", error_string.c_str());
		return -1;
	}
	return 0;
}

u32 sceKernelLoadModule(const char *name, u32 flags)
{
	if(!name)
		return 0;

	PSPFileInfo info = pspFileSystem.GetFileInfo(name);
	std::string error_string;
	s64 size = (s64)info.size;

	if (!info.exists) {
		ERROR_LOG(LOADER, "sceKernelLoadModule(%s, %08x): File does not exist", name, flags);
		return SCE_KERNEL_ERROR_NOFILE;
	}

	if (!size) {
		ERROR_LOG(LOADER, "sceKernelLoadModule(%s, %08x): Module file is size 0", name, flags);
		return SCE_KERNEL_ERROR_ILLEGAL_OBJECT;
	}

	DEBUG_LOG(LOADER, "sceKernelLoadModule(%s, %08x)", name, flags);

	SceKernelLMOption *lmoption = 0;
	int position = 0;
	// TODO: Use position to decide whether to load high or low
	if (PARAM(2)) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(PARAM(2));
	}

	Module *module = 0;
	u8 *temp = new u8[(int)size];
	u32 handle = pspFileSystem.OpenFile(name, FILEACCESS_READ);
	pspFileSystem.ReadFile(handle, temp, (size_t)size);
	module = __KernelLoadELFFromPtr(temp, 0, &error_string);
	delete [] temp;
	pspFileSystem.CloseFile(handle);

	if (!module) {
		// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run.
		// Let's just act as if it worked.
		NOTICE_LOG(LOADER, "Module %s is blacklisted or undecryptable - we lie about success", name);
		return 1;
	}

	if (lmoption) {
		INFO_LOG(HLE,"%i=sceKernelLoadModule(name=%s,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),name,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(HLE,"%i=sceKernelLoadModule(name=%s,flag=%08x,(...))", module->GetUID(), name, flags);
	}

	return module->GetUID();
}

void sceKernelStartModule(u32 moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	// Dunno what these three defaults should be...
	u32 priority = 0x20;
	u32 stacksize = 0x40000; 
	u32 attr = 0;
	int stackPartition = 0;
	if (optionAddr) {
		SceKernelSMOption smoption;
		Memory::ReadStruct(optionAddr, &smoption);;
		priority = smoption.priority;
		attr = smoption.attribute;
		stacksize = smoption.stacksize;
		stackPartition = smoption.mpidstack;
	}
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module) {
		RETURN(error);
		return;
	} else if (module->isFake) {
		INFO_LOG(HLE,"sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): faked (undecryptable module)",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);
	} else {
		ERROR_LOG(HLE,"UNIMPL sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x)",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);

		u32 entryAddr = module->nm.entry_addr;
		if (entryAddr == -1) {
			entryAddr = module->nm.module_start_func;
			// attr = module->nm
		}
	}

	//SceUID threadId;
	//__KernelCreateThread(threadId, moduleId, module->nm.name, module->nm.entry_addr, priority, stacksize, attr);

	// Apparently, we need to call the entry point directly and insert the return value afterwards. This calls
	// for a MipsCall and an Action. TODO
	RETURN(moduleId); // TODO: Delete
}

void sceKernelStopModule(u32 moduleId, u32 argSize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	ERROR_LOG(HLE,"UNIMPL sceKernelStopModule(%i, %i, %08x, %08x, %08x)",
		moduleId, argSize, argAddr, returnValueAddr, optionAddr);

	// We should call the "stop" entry point and return the value in returnValueAddr. See StartModule.
	RETURN(0);
}

u32 sceKernelUnloadModule(u32 moduleId)
{
	INFO_LOG(HLE,"sceKernelUnloadModule(%i)", moduleId);
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module)
		return error;

	kernelObjects.Destroy<Module>(moduleId);
	return 0;
}

u32 sceKernelGetModuleIdByAddress(u32 moduleAddr)
{
	ERROR_LOG(HLE,"HACKIMPL sceKernelGetModuleIdByAddress(%08x)", PARAM(0));

	if ((moduleAddr & 0xFFFF0000) == 0x08800000)
	{
		return mainModuleID;
	}
	else
	{
		Module* foundMod= kernelObjects.GetByModuleByEntryAddr<Module>(moduleAddr);

		if(foundMod)
		{
			return foundMod->GetUID();
		}
	}
	return 0;
}

u32 sceKernelGetModuleId()
{
	INFO_LOG(HLE,"sceKernelGetModuleId()");
	return __KernelGetCurThreadModuleId();
}

u32 sceKernelFindModuleByName()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelFindModuleByName()");
	return 1;
}

u32 sceKernelLoadModuleByID(u32 id, u32 flags, u32 lmoptionPtr)
{
	u32 error;
	u32 handle = __IoGetFileHandleFromId(id, error);
	if (handle == -1) {
		ERROR_LOG(HLE,"sceKernelLoadModuleByID(%08x, %08x, %08x): could not open file id",id,flags,lmoptionPtr);
		return error;
	}
	SceKernelLMOption *lmoption = 0;
	if (lmoptionPtr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(lmoptionPtr);
	}
	u32 pos = (u32) pspFileSystem.SeekFile(handle, 0, FILEMOVE_CURRENT);
	size_t size = pspFileSystem.SeekFile(handle, 0, FILEMOVE_END);
	std::string error_string;
	pspFileSystem.SeekFile(handle, pos, FILEMOVE_BEGIN);
	Module *module = 0;
	u8 *temp = new u8[size];
	pspFileSystem.ReadFile(handle, temp, size);
	module = __KernelLoadELFFromPtr(temp, 0, &error_string);
	delete [] temp;

	if (!module) {
		// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run.
		// Let's just act as if it worked.
		NOTICE_LOG(LOADER, "Module %d is blacklisted or undecryptable - we lie about success", id);
		return 1;
	}

	if (lmoption) {
		INFO_LOG(HLE,"%i=sceKernelLoadModuleByID(%d,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),id,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(HLE,"%i=sceKernelLoadModuleByID(%d,flag=%08x,(...))", module->GetUID(), id, flags);
	}

	return module->GetUID();
}

u32 sceKernelLoadModuleDNAS(const char *name, u32 flags)
{
	ERROR_LOG(HLE,"UNIMPL 0=sceKernelLoadModuleDNAS()");
	return 0;
}

u32 sceKernelQueryModuleInfo(u32 uid, u32 infoAddr)
{
	INFO_LOG(HLE, "sceKernelQueryModuleInfo(%i, %08x)", uid, infoAddr);
	u32 error;
	Module *module = kernelObjects.Get<Module>(uid, error);
	if (!module)
		return error;
	if (!Memory::IsValidAddress(infoAddr)) {
		ERROR_LOG(HLE, "sceKernelQueryModuleInfo(%i, %08x) - bad infoAddr", uid, infoAddr);
		return -1;
	}
	ModuleInfo info;
	memcpy(info.segmentaddr, module->nm.segmentaddr, sizeof(info.segmentaddr));
	memcpy(info.segmentsize, module->nm.segmentsize, sizeof(info.segmentsize));
	info.nsegment = module->nm.nsegment;
	info.entry_addr = module->nm.entry_addr;
	info.gp_value = module->nm.gp_value;
	info.text_addr = module->nm.text_addr;
	info.text_size = module->nm.text_size;
	info.data_size = module->nm.data_size;
	info.bss_size = module->nm.bss_size;
	info.attribute = module->nm.attribute;
	info.version[0] = module->nm.version[0];
	info.version[1] = module->nm.version[1];
	memcpy(info.name, module->nm.name, 28);
	Memory::WriteStruct(infoAddr, &info);
	return 0;
}

const HLEFunction ModuleMgrForUser[] = 
{
	{0x977DE386,&WrapU_CU<sceKernelLoadModule>,"sceKernelLoadModule"},
	{0xb7f46618,&WrapU_UUU<sceKernelLoadModuleByID>,"sceKernelLoadModuleByID"},
	{0x50F0C1EC,&WrapV_UUUUU<sceKernelStartModule>,"sceKernelStartModule"},
	{0xD675EBB8,&sceKernelExitGame,"sceKernelSelfStopUnloadModule"}, //HACK
	{0xd1ff982a,&WrapV_UUUUU<sceKernelStopModule>,"sceKernelStopModule"},
	{0x2e0911aa,WrapU_U<sceKernelUnloadModule>,"sceKernelUnloadModule"},
	{0x710F61B5,0,"sceKernelLoadModuleMs"},
	{0xF9275D98,0,"sceKernelLoadModuleBufferUsbWlan"}, ///???
	{0xCC1D3699,0,"sceKernelStopUnloadSelfModule"},
	{0x748CBED9,WrapU_UU<sceKernelQueryModuleInfo>,"sceKernelQueryModuleInfo"},
	{0xd8b73127,&WrapU_U<sceKernelGetModuleIdByAddress>, "sceKernelGetModuleIdByAddress"},
	{0xf0a26395,WrapU_V<sceKernelGetModuleId>, "sceKernelGetModuleId"},
	{0x8f2df740,0,"sceKernelStopUnloadSelfModuleWithStatus"},
	{0xfef27dc1,&WrapU_CU<sceKernelLoadModuleDNAS> , "sceKernelLoadModuleDNAS"},
};


void Register_ModuleMgrForUser()
{
	RegisterModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}
