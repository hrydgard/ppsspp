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

#include "native/base/stringutil.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"
#include "Core/Reporting.h"
#include "Common/FileUtil.h"
#include "../Host.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/ELF/ElfReader.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/PrxDecrypter.h"
#include "../Debugger/SymbolMap.h"
#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../Util/BlockAllocator.h"
#include "Core/CoreTiming.h"
#include "Core/PSPLoaders.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"

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

// This is a workaround for misbehaving homebrew (like TBL's Suicide Barbie (Final)).
static const char *lieAboutSuccessModules[] = {
	"flash0:/kd/audiocodec.prx",
	"flash0:/kd/libatrac3plus.prx",
};

static const char *blacklistedModules[] = {
	"sceATRAC3plus_Library",
	"sceFont_Library",
	"SceFont_Library",
	"SceHttp_Library",
	"sceMpeg_library",
	"sceNetAdhocctl_Library",
	"sceNetAdhocDownload_Library",
	"sceNetAdhocMatching_Library",
	"sceNetApDialogDummy_Library",
	"sceNetAdhoc_Library",
	"sceNetApctl_Library",
	"sceNetInet_Library",
	"sceNetResolver_Library",
	"sceNet_Library",
	"sceSsl_Module",
};

struct VarSymbol {
	char moduleName[32];
	u32 symAddr;
	u32 nid;
	u8 type;
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

struct ModuleWaitingThread
{
	SceUID threadID;
	u32 statusPtr;
};

class Module : public KernelObject
{
public:
	Module() : memoryBlockAddr(0), isFake(false), isStarted(false) {}
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
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_Module; }
	int GetIDType() const { return PPSSPP_KERNEL_TMID_Module; }

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nm);
		p.Do(memoryBlockAddr);
		p.Do(memoryBlockSize);
		p.Do(isFake);
		p.Do(isStarted);
		ModuleWaitingThread mwt = {0};
		p.Do(waitingThreads, mwt);
		p.DoMarker("Module");
	}

	NativeModule nm;
	std::vector<ModuleWaitingThread> waitingThreads;

	u32 memoryBlockAddr;
	u32 memoryBlockSize;
	bool isFake;
	// Probably one of the NativeModule fields, but not sure...
	bool isStarted;
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

static std::vector<VarSymbol> unresolvedVars;
static std::vector<VarSymbol> exportedVars;
// STATE END
//////////////////////////////////////////////////////////////////////////

void __KernelModuleInit()
{
	actionAfterModule = __KernelRegisterActionType(AfterModuleEntryCall::Create);
}

void __KernelModuleDoState(PointerWrap &p)
{
	p.Do(actionAfterModule);
	__KernelRestoreActionType(actionAfterModule, AfterModuleEntryCall::Create);
	VarSymbol vs = {""};
	p.Do(unresolvedVars, vs);
	p.Do(exportedVars, vs);
	p.DoMarker("sceKernelModule");
}

void __KernelModuleShutdown()
{
	unresolvedVars.clear();
	exportedVars.clear();
	MIPSAnalyst::Shutdown();
}

// Sometimes there are multiple LO16's or HI16's per pair, even though the ABI says nothing of this.
// For multiple LO16's, we need the original (unrelocated) instruction data of the HI16.
// For multiple HI16's, we just need to set each one.
struct HI16RelocInfo
{
	u32 addr;
	u32 data;
};
void WriteVarSymbol(u32 exportAddress, u32 relocAddress, u8 type)
{
	// We have to post-process the HI16 part, since it might be +1 or not depending on the LO16 value.
	static u32 lastHI16ExportAddress = 0;
	static std::vector<HI16RelocInfo> lastHI16Relocs;
	static bool lastHI16Processed = true;

	u32 relocData = Memory::Read_Instruction(relocAddress);

	switch (type)
	{
	case R_MIPS_NONE:
		WARN_LOG_REPORT(LOADER, "Var relocation type NONE - %08x => %08x", exportAddress, relocAddress);
		break;

	case R_MIPS_32:
		relocData += exportAddress;
		break;

	// Not really tested, but should work...
	/*
	case R_MIPS_26:
		if (exportAddress % 4 || (exportAddress >> 28) != ((relocAddress + 4) >> 28))
			WARN_LOG_REPORT(LOADER, "Bad var relocation addresses for type 26 - %08x => %08x", exportAddress, relocAddress)
		else
			relocData = (relocData & ~0x03ffffff) | ((relocData + (exportAddress >> 2)) & 0x03ffffff);
		break;
	*/

	case R_MIPS_HI16:
		if (lastHI16ExportAddress != exportAddress)
		{
			if (!lastHI16Processed && lastHI16Relocs.size() >= 1)
				WARN_LOG_REPORT(LOADER, "Unsafe unpaired HI16 variable relocation @ %08x / %08x", lastHI16Relocs[lastHI16Relocs.size() - 1].addr, relocAddress);

			lastHI16ExportAddress = exportAddress;
			lastHI16Relocs.clear();
		}

		// After this will be an R_MIPS_LO16.  If that addition overflows, we need to account for it in HI16.
		// The R_MIPS_LO16 and R_MIPS_HI16 will often be *different* relocAddress values.
		HI16RelocInfo reloc;
		reloc.addr = relocAddress;
		reloc.data = Memory::Read_Instruction(relocAddress);
		lastHI16Relocs.push_back(reloc);
		lastHI16Processed = false;
		break;

	case R_MIPS_LO16:
		{
			// Sign extend the existing low value (e.g. from addiu.)
			const u32 exportOffsetLo = exportAddress + (s16)(u16)(relocData & 0xFFFF);
			u32 full = exportOffsetLo;

			// The ABI requires that these come in pairs, at least.
			if (lastHI16Relocs.empty())
				ERROR_LOG_REPORT(LOADER, "LO16 without any HI16 variable import at %08x for %08x", relocAddress, exportAddress)
			// Try to process at least the low relocation...
			else if (lastHI16ExportAddress != exportAddress)
				ERROR_LOG_REPORT(LOADER, "HI16 and LO16 imports do not match at %08x for %08x (should be %08x)", relocAddress, lastHI16ExportAddress, exportAddress)
			else
			{
				// Process each of the HI16.  Usually there's only one.
				for (auto it = lastHI16Relocs.begin(), end = lastHI16Relocs.end(); it != end; ++it)
				{
					full = (it->data << 16) + exportOffsetLo;
					// The low instruction will be a signed add, which means (full & 0x8000) will subtract.
					// We add 1 in that case so that it ends up the right value.
					u16 high = (full >> 16) + ((full & 0x8000) ? 1 : 0);
					Memory::Write_U32((it->data & ~0xFFFF) | high, it->addr);
				}
				lastHI16Processed = true;
			}

			// With full set above (hopefully), now we just need to correct the low instruction.
			relocData = (relocData & ~0xFFFF) | (full & 0xFFFF);
		}
		break;

	default:
		WARN_LOG_REPORT(LOADER, "Unsupported var relocation type %d - %08x => %08x", type, exportAddress, relocAddress);
	}

	Memory::Write_U32(relocData, relocAddress);
}

void ImportVarSymbol(const char *moduleName, u32 nid, u32 address, u8 type)
{
	_dbg_assert_msg_(HLE, moduleName != NULL, "Invalid module name.");

	if (nid == 0)
	{
		// TODO: What's the right thing for this?
		ERROR_LOG_REPORT(LOADER, "Var import with nid = 0, type = %d", type);
		return;
	}

	if (!Memory::IsValidAddress(address))
	{
		ERROR_LOG_REPORT(LOADER, "Invalid address for var import nid = %08x, type = %d, addr = %08x", nid, type, address);
		return;
	}

	for (auto it = exportedVars.begin(), end = exportedVars.end(); it != end; ++it)
	{
		if (!strncmp(it->moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH) && it->nid == nid)
		{
			WriteVarSymbol(it->symAddr, address, type);
			return;
		}
	}

	// It hasn't been exported yet, but hopefully it will later.
	INFO_LOG(LOADER, "Variable (%s,%08x) unresolved, storing for later resolving", moduleName, nid);
	VarSymbol vs = {"", address, nid, type};
	strncpy(vs.moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
	vs.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
	unresolvedVars.push_back(vs);
}

void ExportVarSymbol(const char *moduleName, u32 nid, u32 address)
{
	_dbg_assert_msg_(HLE, moduleName != NULL, "Invalid module name.");

	VarSymbol ex = {"", address, nid};
	strncpy(ex.moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
	ex.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';
	exportedVars.push_back(ex);

	for (auto it = unresolvedVars.begin(), end = unresolvedVars.end(); it != end; ++it)
	{
		if (strncmp(it->moduleName, moduleName, KERNELOBJECT_MAX_NAME_LENGTH) == 0 && it->nid == nid)
		{
			INFO_LOG(HLE,"Resolving var %s/%08x", moduleName, nid);
			WriteVarSymbol(address, it->symAddr, it->type);
		}
	}
}

Module *__KernelLoadELFFromPtr(const u8 *ptr, u32 loadAddress, std::string *error_string, u32 *magic) {
	Module *module = new Module;
	kernelObjects.Create(module);
	memset(&module->nm, 0, sizeof(module->nm));

	u8 *newptr = 0;
	if (*(u32*)ptr == 0x4543537e) { // "~SCE"
		INFO_LOG(HLE, "~SCE module, skipping header");
		ptr += *(u32*)(ptr + 4);
	}
	*magic = *(u32*)ptr;
	if (*magic == 0x5053507e) { // "~PSP"
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
		if (ret == MISSING_KEY) {
			// This should happen for all "kernel" modules so disabling.
			// Reporting::ReportMessage("Missing PRX decryption key!");
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
			ERROR_LOG(HLE, "Failed decrypting PRX! That's not normal! ret = %i\n", ret);
			Reporting::ReportMessage("Failed decrypting the PRX (ret = %i, size = %i, psp_size = %i)!", ret, head->elf_size, head->psp_size);
			// Fall through to safe exit in the next check.
		}
	}

	// DO NOT change to else if, see above.
	if (*(u32*)ptr != 0x464c457f) {
		ERROR_LOG_REPORT(HLE, "Wrong magic number %08x", *(u32*)ptr);
		*error_string = "File corrupt";
		if (newptr)
			delete [] newptr;
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	// Open ELF reader
	ElfReader reader((void*)ptr);

	if (!reader.LoadInto(loadAddress)) 	{
		ERROR_LOG(HLE, "LoadInto failed");
		if (newptr)
			delete [] newptr;
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	module->memoryBlockAddr = reader.GetVaddr();
	module->memoryBlockSize = reader.GetTotalSize();

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

	if (textSection != -1) {
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
	else if (host->AttemptLoadSymbolMap())
	{
		dontadd = true;
	}

	INFO_LOG(LOADER,"Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent,modinfo->libstub);

	struct PspLibStubEntry
	{
		u32 name;
		u16 version;
		u16 flags;
		u8 size;
		u8 numVars;
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
		// Optional, this is where var relocations are.
		// They use the format: u32 addr, u32 nid, ...
		// WARNING: May have garbage if size < 6.
		u32 varData;
		// Not sure what this is yet, assume garbage for now.
		// TODO: Tales of the World: Radiant Mythology 2 has something here?
		u32 extra;
	};

	DEBUG_LOG(LOADER,"===================================================");

	u32 *entryPos = (u32 *)Memory::GetPointer(modinfo->libstub);
	u32 *entryEnd = (u32 *)Memory::GetPointer(modinfo->libstubend);

	bool needReport = false;
	while (entryPos < entryEnd) {
		PspLibStubEntry *entry = (PspLibStubEntry *)entryPos;
		entryPos += entry->size;

		const char *modulename;
		if (Memory::IsValidAddress(entry->name)) {
			modulename = Memory::GetCharPointer(entry->name);
		} else {
			modulename = "(invalidname)";
			needReport = true;
		}

		DEBUG_LOG(LOADER, "Importing Module %s, stubs at %08x", modulename, entry->firstSymAddr);
		if (entry->size != 5 && entry->size != 6) {
			WARN_LOG_REPORT(LOADER, "Unexpected module entry size %d", entry->size);
			needReport = true;
		}

		// If nidData is 0, only variables are being imported.
		if (entry->nidData != 0) {
			if (!Memory::IsValidAddress(entry->nidData)) {
				ERROR_LOG_REPORT(LOADER, "Crazy nidData address %08x, skipping entire module", entry->nidData);
				needReport = true;
				continue;
			}

			u32 *nidDataPtr = (u32 *)Memory::GetPointer(entry->nidData);
			for (int i = 0; i < entry->numFuncs; ++i) {
				u32 addrToWriteSyscall = entry->firstSymAddr + i * 8;
				DEBUG_LOG(LOADER, "%s : %08x", GetFuncName(modulename, nidDataPtr[i]), addrToWriteSyscall);
				//write a syscall here
				if (Memory::IsValidAddress(addrToWriteSyscall)) {
					WriteSyscall(modulename, nidDataPtr[i], addrToWriteSyscall);
				} else {
					WARN_LOG_REPORT(LOADER, "Invalid address for syscall stub %s %08x", modulename, nidDataPtr[i]);
				}

				if (!dontadd) {
					char temp[256];
					sprintf(temp,"zz_%s", GetFuncName(modulename, nidDataPtr[i]));
					symbolMap.AddSymbol(temp, addrToWriteSyscall, 8, ST_FUNCTION);
				}
			}
		} else if (entry->numFuncs > 0) {
			WARN_LOG_REPORT(LOADER, "Module entry with %d imports but no valid address", entry->numFuncs);
			needReport = true;
		}

		if (entry->varData != 0) {
			if (!Memory::IsValidAddress(entry->varData)) {
				ERROR_LOG_REPORT(LOADER, "Crazy varData address %08x, skipping rest of module", entry->varData);
				needReport = true;
				continue;
			}

			for (int i = 0; i < entry->numVars; ++i) {
				u32 varRefsPtr = Memory::Read_U32(entry->varData + i * 8);
				u32 nid = Memory::Read_U32(entry->varData + i * 8 + 4);
				if (!Memory::IsValidAddress(varRefsPtr)) {
					WARN_LOG_REPORT(LOADER, "Bad relocation list address for nid %08x in %s", nid, modulename);
					continue;
				}

				u32 *varRef = (u32 *)Memory::GetPointer(varRefsPtr);
				for (; *varRef != 0; ++varRef) {
					ImportVarSymbol(modulename, nid, (*varRef & 0x03FFFFFF) << 2, *varRef >> 26);
				}
			}
		} else if (entry->numVars > 0) {
			WARN_LOG_REPORT(LOADER, "Module entry with %d var imports but no valid address", entry->numVars);
			needReport = true;
		}

		DEBUG_LOG(LOADER, "-------------------------------------------------------------");
	}

	if (needReport) {
		std::string debugInfo;
		entryPos = (u32 *)Memory::GetPointer(modinfo->libstub);
		while (entryPos < entryEnd) {
			PspLibStubEntry *entry = (PspLibStubEntry *)entryPos;
			entryPos += entry->size;

			char temp[512];
			const char *modulename;
			if (Memory::IsValidAddress(entry->name)) {
				modulename = Memory::GetCharPointer(entry->name);
			} else {
				modulename = "(invalidname)";
			}

			snprintf(temp, sizeof(temp), "%s ver=%04x, flags=%04x, size=%d, numVars=%d, numFuncs=%d, nidData=%08x, firstSym=%08x, varData=%08x, extra=%08x\n",
				modulename, entry->version, entry->flags, entry->size, entry->numVars, entry->numFuncs, entry->nidData, entry->firstSymAddr, entry->size >= 6 ? entry->varData : 0, entry->size >= 7 ? entry->extra : 0);
			debugInfo += temp;
		}

		Reporting::ReportMessage("Module linking debug info:\n%s", debugInfo.c_str());
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

	u32 *entPos = (u32 *)Memory::GetPointer(modinfo->libent);
	u32 *entEnd = (u32 *)Memory::GetPointer(modinfo->libentend);
	for (int m = 0; entPos < entEnd; ++m) {
		PspLibEntEntry *ent = (PspLibEntEntry *)entPos;
		entPos += ent->size;
		if (ent->size == 0) {
			WARN_LOG_REPORT(LOADER, "Invalid export entry size %d", ent->size);
			entPos += 4;
			continue;
		}

		const char *name;
		if (Memory::IsValidAddress(ent->name)) {
			name = Memory::GetCharPointer(ent->name);
		} else if (ent->name == 0) {
			name = module->nm.name;
		} else {
			name = "invalid?";
		}

		INFO_LOG(HLE, "Exporting ent %d named %s, %d funcs, %d vars, resident %08x", m, name, ent->fcount, ent->vcount, ent->resident);

		if (!Memory::IsValidAddress(ent->resident)) {
			if (ent->fcount + ent->vcount > 0) {
				WARN_LOG_REPORT(LOADER, "Invalid export resident address %08x", ent->resident);
			}
			continue;
		}

		u32 *residentPtr = (u32 *)Memory::GetPointer(ent->resident);
		u32 *exportPtr = residentPtr + ent->fcount + ent->vcount;

		for (u32 j = 0; j < ent->fcount; j++) {
			u32 nid = residentPtr[j];
			u32 exportAddr = exportPtr[j];

			switch (nid) {
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

		for (u32 j = 0; j < ent->vcount; j++) {
			u32 nid = residentPtr[ent->fcount + j];
			u32 exportAddr = exportPtr[ent->fcount + j];

			int size;
			switch (nid) {
			case NID_MODULE_INFO:
				break;
			case NID_MODULE_START_THREAD_PARAMETER:
				size = Memory::Read_U32(exportAddr);
				if (size == 0)
					break;
				else if (size != 3)
					WARN_LOG_REPORT(LOADER, "Strange value at module_start_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_start_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_start_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_start_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_STOP_THREAD_PARAMETER:
				size = Memory::Read_U32(exportAddr);
				if (size == 0)
					break;
				else if (size != 3)
					WARN_LOG_REPORT(LOADER, "Strange value at module_stop_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_stop_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_stop_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_stop_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_REBOOT_BEFORE_THREAD_PARAMETER:
				size = Memory::Read_U32(exportAddr);
				if (size == 0)
					break;
				else if (size != 3)
					WARN_LOG_REPORT(LOADER, "Strange value at module_reboot_before_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_reboot_before_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_reboot_before_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_reboot_before_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_SDK_VERSION:
				DEBUG_LOG(LOADER, "Module SDK: %08x", Memory::Read_U32(exportAddr));
				break;
			default:
				ExportVarSymbol(name, nid, exportAddr);
				break;
			}
		}
	}

	module->nm.entry_addr = reader.GetEntryPoint();
	
	// use module_start_func instead of entry_addr if entry_addr is 0
	if (module->nm.entry_addr == 0)
		module->nm.entry_addr = module->nm.module_start_func;

	if (newptr)
		delete [] newptr;

	return module;
}

bool __KernelLoadPBP(const char *filename, std::string *error_string)
{
	static const char *FileNames[] =
	{
		"PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "UNKNOWN.PNG",
		"PIC1.PNG", "SND0.AT3", "UNKNOWN.PSP", "UNKNOWN.PSAR"
	};

	PBPReader pbp(filename);
	if (!pbp.IsValid()) {
		ERROR_LOG(LOADER,"%s is not a valid homebrew PSP1.0 PBP",filename);
		*error_string = "Not a valid homebrew PBP";
		return false;
	}

	size_t elfSize;
	u8 *elfData = pbp.GetSubFile(PBP_EXECUTABLE_PSP, &elfSize);
	u32 magic;
	Module *module = __KernelLoadELFFromPtr(elfData, PSP_GetDefaultLoadAddress(), error_string, &magic);
	if (!module) {
		delete [] elfData;
		return false;
	}
	mipsr4k.pc = module->nm.entry_addr;
	delete [] elfData;
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
		u32 magic = 0;
		module = __KernelLoadELFFromPtr(fileptr + offsets[5], PSP_GetDefaultLoadAddress(), error_string, &magic);
	}
	else
	{
		u32 magic = 0;
		module = __KernelLoadELFFromPtr(fileptr, PSP_GetDefaultLoadAddress(), error_string, &magic);
	}

	return module;
}

void __KernelStartModule(Module *m, int args, const char *argp, SceKernelSMOption *options)
{
	m->isStarted = true;
	if (m->nm.module_start_func != 0 && m->nm.module_start_func != (u32)-1)
	{
		if (m->nm.module_start_func != m->nm.entry_addr)
			WARN_LOG_REPORT(LOADER, "Main module has start func (%08x) different from entry (%08x)?", m->nm.module_start_func, m->nm.entry_addr);
	}

	SceUID threadID = __KernelSetupRootThread(m->GetUID(), args, argp, options->priority, options->stacksize, options->attribute);
	__KernelSetThreadRA(threadID, NID_MODULERETURN);
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
	{
		__KernelShutdown();
		//HLE needs to be reset here
		HLEShutdown();
		HLEInit();
	}

	__KernelModuleInit();
	__KernelInit();
	
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (!info.exists) {
		ERROR_LOG(LOADER, "Failed to load executable %s - file doesn't exist", filename);
		*error_string = "Could not find executable";
		return false;
	}

	u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);

	u8 *temp = new u8[(int)info.size + 0x1000000];

	pspFileSystem.ReadFile(handle, temp, (size_t)info.size);

	Module *module = __KernelLoadModule(temp, 0, error_string);

	if (!module || module->isFake)
	{
		if (module)
			kernelObjects.Destroy<Module>(module->GetUID());
		ERROR_LOG(LOADER, "Failed to load module %s", filename);
		*error_string = "Failed to load executable: " + *error_string;
		delete [] temp;
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

	__KernelStartIdleThreads(module->GetUID());
	return true;
}

//TODO: second param
int sceKernelLoadExec(const char *filename, u32 paramPtr)
{
	std::string exec_filename = filename;
	SceKernelLoadExecParam *param = 0;
	if (paramPtr) {
		param = (SceKernelLoadExecParam *)Memory::GetPointer(paramPtr);
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(exec_filename);

	// If there's an EBOOT.BIN, redirect to that instead.
	if (info.exists && endsWith(exec_filename, "/BOOT.BIN")) {
		std::string eboot_filename = exec_filename.substr(0, exec_filename.length() - strlen("BOOT.BIN")) + "EBOOT.BIN";

		PSPFileInfo eboot_info = pspFileSystem.GetFileInfo(eboot_filename);
		if (eboot_info.exists) {
			exec_filename = eboot_filename;
			info = eboot_info;
		}
	}

	if (!info.exists) {
		ERROR_LOG(LOADER, "sceKernelLoadExec(%s, ...): File does not exist", filename);
		return SCE_KERNEL_ERROR_NOFILE;
	}

	s64 size = (s64)info.size;
	if (!size) {
		ERROR_LOG(LOADER, "sceKernelLoadExec(%s, ...): File is size 0", filename);
		return SCE_KERNEL_ERROR_ILLEGAL_OBJECT;
	}

	DEBUG_LOG(HLE, "sceKernelLoadExec(name=%s,...): loading %s", filename, exec_filename.c_str());
	std::string error_string;
	if (!__KernelLoadExec(exec_filename.c_str(), param, &error_string)) {
		ERROR_LOG(HLE, "sceKernelLoadExec failed: %s", error_string.c_str());
		Core_UpdateState(CORE_ERROR);
		return -1;
	}
	return 0;
}

u32 sceKernelLoadModule(const char *name, u32 flags, u32 optionAddr)
{
	if (!name) {
		ERROR_LOG(LOADER, "sceKernelLoadModule(NULL, %08x): Bad name", flags);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	for (size_t i = 0; i < ARRAY_SIZE(lieAboutSuccessModules); i++) {
		if (!strcmp(name, lieAboutSuccessModules[i])) {
			INFO_LOG(LOADER, "Tries to load module %s. We return a fake module.", lieAboutSuccessModules[i]);

			Module *module = new Module;
			kernelObjects.Create(module);
			memset(&module->nm, 0, sizeof(module->nm));
			module->isFake = true;
			return module->GetUID();
		}
	}

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
	if (optionAddr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(optionAddr);
	}

	Module *module = 0;
	u8 *temp = new u8[(int)size];
	u32 handle = pspFileSystem.OpenFile(name, FILEACCESS_READ);
	pspFileSystem.ReadFile(handle, temp, (size_t)size);
	u32 magic;
	module = __KernelLoadELFFromPtr(temp, 0, &error_string, &magic);
	delete [] temp;
	pspFileSystem.CloseFile(handle);

	if (!module) {
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			return -1;
		}

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

	// TODO: This is not the right timing and probably not the right wait type, just an approximation.
	return hleDelayResult(module->GetUID(), "module loaded", 500);
}

u32 sceKernelLoadModuleNpDrm(const char *name, u32 flags, u32 optionAddr)
{
	DEBUG_LOG(LOADER, "sceKernelLoadModuleNpDrm(%s, %08x)", name, flags);

	return sceKernelLoadModule(name, flags, optionAddr);
}

void sceKernelStartModule(u32 moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	u32 priority = 0x20;
	u32 stacksize = 0x40000; 
	u32 attr = 0;
	int stackPartition = 0;
	SceKernelSMOption smoption;
	if (optionAddr) {
		Memory::ReadStruct(optionAddr, &smoption);
	}
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module) {
		RETURN(error);
		return;
	} else if (module->isFake) {
		INFO_LOG(HLE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): faked (undecryptable module)",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		RETURN(moduleId);
		return;
	} else if (module->isStarted) {
		ERROR_LOG(HLE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x) : already started",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);
		// TODO: Maybe should be SCE_KERNEL_ERROR_ALREADY_STARTED, but I get SCE_KERNEL_ERROR_ERROR.
		// But I also get crashes...
		RETURN(SCE_KERNEL_ERROR_ERROR);
		return;
	} else {
		INFO_LOG(HLE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x)",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);

		int attribute = module->nm.attribute;
		u32 entryAddr = module->nm.entry_addr;

		if (module->nm.module_start_func != 0 && module->nm.module_start_func != (u32)-1)
		{
			entryAddr = module->nm.module_start_func;
			attribute = module->nm.module_start_thread_attr;
		}
		else if ((entryAddr == (u32)-1) || entryAddr == module->memoryBlockAddr - 1)
		{
			if (optionAddr)
			{
				// TODO: Does sceKernelStartModule() really give an error when no entry only if you pass options?
				attribute = smoption.attribute;
			}
			else
			{
				// TODO: Why are we just returning the module ID in this case?
				WARN_LOG(HLE, "sceKernelStartModule(): module has no start or entry func");
				module->isStarted = true;
				RETURN(moduleId);
				return;
			}
		}

		if (Memory::IsValidAddress(entryAddr))
		{
			if ((optionAddr) && smoption.priority > 0) {
				priority = smoption.priority;
			} else if (module->nm.module_start_thread_priority > 0) {
				priority = module->nm.module_start_thread_priority;
			}

			if ((optionAddr) && (smoption.stacksize > 0)) {
				stacksize = smoption.stacksize;
			} else if (module->nm.module_start_thread_stacksize > 0) {
				stacksize = module->nm.module_start_thread_stacksize;
			}

			SceUID threadID = __KernelCreateThread(module->nm.name, moduleId, entryAddr, priority, stacksize, attribute, 0);
			sceKernelStartThread(threadID, argsize, argAddr);
			__KernelSetThreadRA(threadID, NID_MODULERETURN);
			__KernelWaitCurThread(WAITTYPE_MODULE, moduleId, 1, 0, false, "started module");

			const ModuleWaitingThread mwt = {__KernelGetCurThread(), returnValueAddr};
			module->waitingThreads.push_back(mwt);
		}
		else if (entryAddr == 0)
		{
			INFO_LOG(HLE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): no entry address",
			moduleId,argsize,argAddr,returnValueAddr,optionAddr);
			module->isStarted = true;
		}
		else
		{
			ERROR_LOG(HLE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): invalid entry address",
			moduleId,argsize,argAddr,returnValueAddr,optionAddr);
			RETURN(-1);
			return;
		}
	}

	RETURN(moduleId);
}

u32 sceKernelStopModule(u32 moduleId, u32 argSize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	u32 priority = 0x20;
	u32 stacksize = 0x40000;
	u32 attr = 0;

	// TODO: In a lot of cases (even for errors), this should resched.  Needs testing.

	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module)
	{
		ERROR_LOG(HLE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): invalid module id", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		return error;
	}

	if (module->isFake)
	{
		INFO_LOG(HLE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x) - faking", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		return 0;
	}
	if (!module->isStarted)
	{
		ERROR_LOG(HLE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): already stopped", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		return SCE_KERNEL_ERROR_ALREADY_STOPPED;
	}

	u32 stopFunc = module->nm.module_stop_func;
	if (module->nm.module_stop_thread_priority != 0)
		priority = module->nm.module_stop_thread_priority;
	if (module->nm.module_stop_thread_stacksize != 0)
		stacksize = module->nm.module_stop_thread_stacksize;
	if (module->nm.module_stop_thread_attr != 0)
		attr = module->nm.module_stop_thread_attr;

	// TODO: Need to test how this really works.  Let's assume it's an override.
	if (Memory::IsValidAddress(optionAddr))
	{
		auto options = Memory::GetStruct<SceKernelSMOption>(optionAddr);
		// TODO: Check how size handling actually works.
		if (options->size != 0 && options->priority != 0)
			priority = options->priority;
		if (options->size != 0 && options->stacksize != 0)
			stacksize = options->stacksize;
		if (options->size != 0 && options->attribute != 0)
			attr = options->attribute;
		// TODO: Maybe based on size?
		else if (attr != 0)
			WARN_LOG_REPORT(HLE, "Stopping module with attr=%x, but options specify 0", attr);
	}

	if (Memory::IsValidAddress(stopFunc))
	{
		SceUID threadID = __KernelCreateThread(module->nm.name, moduleId, stopFunc, priority, stacksize, attr, 0);
		sceKernelStartThread(threadID, argSize, argAddr);
		__KernelSetThreadRA(threadID, NID_MODULERETURN);
		__KernelWaitCurThread(WAITTYPE_MODULE, moduleId, 1, 0, false, "stopped module");

		const ModuleWaitingThread mwt = {__KernelGetCurThread(), returnValueAddr};
		module->waitingThreads.push_back(mwt);
	}
	else if (stopFunc == 0)
	{
		INFO_LOG(HLE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): no stop func, skipping", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		module->isStarted = false;
	}
	else
	{
		ERROR_LOG_REPORT(HLE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): bad stop func address", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		module->isStarted = false;
	}

	return 0;
}

u32 sceKernelUnloadModule(u32 moduleId)
{
	INFO_LOG(HLE,"sceKernelUnloadModule(%i)", moduleId);
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module)
		return error;

	kernelObjects.Destroy<Module>(moduleId);
	return moduleId;
}

u32 sceKernelStopUnloadSelfModuleWithStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp, statusAddr, optionAddr);

	// Probably similar to sceKernelStopModule, but games generally call this when they die.
	return 0;
}

void __KernelReturnFromModuleFunc()
{
	// Return from the thread as normal.
	__KernelReturnFromThread();

	SceUID leftModuleID = __KernelGetCurThreadModuleId();
	SceUID leftThreadID = __KernelGetCurThread();
	int exitStatus = sceKernelGetThreadExitStatus(leftThreadID);

	// Reschedule immediately (to leave the thread) and delete it and its stack.
	__KernelReSchedule("returned from module");
	sceKernelDeleteThread(leftThreadID);

	u32 error;
	Module *module = kernelObjects.Get<Module>(leftModuleID, error);
	if (!module)
	{
		ERROR_LOG_REPORT(HLE, "Returned from deleted module start/stop func");
		return;
	}

	// We can't be starting and stopping at the same time, so no need to differentiate.
	module->isStarted = !module->isStarted;
	for (auto it = module->waitingThreads.begin(), end = module->waitingThreads.end(); it < end; ++it)
	{
		// Still waiting?
		SceUID waitingModuleID = __KernelGetWaitID(it->threadID, WAITTYPE_MODULE, error);
		if (waitingModuleID == leftModuleID)
		{
			if (it->statusPtr != 0)
				Memory::Write_U32(exitStatus, it->statusPtr);
			__KernelResumeThreadFromWait(it->threadID, 0);
		}
	}
	module->waitingThreads.clear();
}

struct GetModuleIdByAddressArg
{
	u32 addr;
	SceUID result;
};

bool __GetModuleIdByAddressIterator(Module *module, GetModuleIdByAddressArg *state)
{
	const u32 start = module->memoryBlockAddr, size = module->memoryBlockSize;
	if (start <= state->addr && start + size > state->addr)
	{
		state->result = module->GetUID();
		return false;
	}
	return true;
}

u32 sceKernelGetModuleIdByAddress(u32 moduleAddr)
{
	GetModuleIdByAddressArg state;
	state.addr = moduleAddr;
	state.result = SCE_KERNEL_ERROR_UNKNOWN_MODULE;

	kernelObjects.Iterate(&__GetModuleIdByAddressIterator, &state);
	if (state.result == SCE_KERNEL_ERROR_UNKNOWN_MODULE)
		ERROR_LOG(HLE, "sceKernelGetModuleIdByAddress(%08x): module not found", moduleAddr)
	else
		DEBUG_LOG(HLE, "%x=sceKernelGetModuleIdByAddress(%08x)", state.result, moduleAddr);
	return state.result;
}

u32 sceKernelGetModuleId()
{
	INFO_LOG(HLE,"sceKernelGetModuleId()");
	return __KernelGetCurThreadModuleId();
}

u32 sceKernelFindModuleByName(const char *name)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceKernelFindModuleByName(%s)", name);
	return 1;
}

u32 sceKernelLoadModuleByID(u32 id, u32 flags, u32 lmoptionPtr)
{
	u32 error;
	u32 handle = __IoGetFileHandleFromId(id, error);
	if (handle == (u32)-1) {
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
	u32 magic;
	module = __KernelLoadELFFromPtr(temp, 0, &error_string, &magic);
	delete [] temp;

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			return -1;
		}

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
	ERROR_LOG_REPORT(HLE, "UNIMPL 0=sceKernelLoadModuleDNAS()");
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
	{0x977DE386,&WrapU_CUU<sceKernelLoadModule>,"sceKernelLoadModule"},
	{0xb7f46618,&WrapU_UUU<sceKernelLoadModuleByID>,"sceKernelLoadModuleByID"},
	{0x50F0C1EC,&WrapV_UUUUU<sceKernelStartModule>,"sceKernelStartModule"},
	{0xD675EBB8,&sceKernelExitGame,"sceKernelSelfStopUnloadModule"}, //HACK
	{0xd1ff982a,&WrapU_UUUUU<sceKernelStopModule>,"sceKernelStopModule"},
	{0x2e0911aa,WrapU_U<sceKernelUnloadModule>,"sceKernelUnloadModule"},
	{0x710F61B5,0,"sceKernelLoadModuleMs"},
	{0xF9275D98,0,"sceKernelLoadModuleBufferUsbWlan"}, ///???
	{0xCC1D3699,0,"sceKernelStopUnloadSelfModule"},
	{0x748CBED9,WrapU_UU<sceKernelQueryModuleInfo>,"sceKernelQueryModuleInfo"},
	{0xd8b73127,&WrapU_U<sceKernelGetModuleIdByAddress>, "sceKernelGetModuleIdByAddress"},
	{0xf0a26395,WrapU_V<sceKernelGetModuleId>, "sceKernelGetModuleId"},
	{0x8f2df740,WrapU_UUUUU<sceKernelStopUnloadSelfModuleWithStatus>,"sceKernelStopUnloadSelfModuleWithStatus"},
	{0xfef27dc1,&WrapU_CU<sceKernelLoadModuleDNAS> , "sceKernelLoadModuleDNAS"},
	{0x644395e2,0,"sceKernelGetModuleIdList"},
	{0xf2d8d1b4,&WrapU_CUU<sceKernelLoadModuleNpDrm>,"sceKernelLoadModuleNpDrm"},
};


void Register_ModuleMgrForUser()
{
	RegisterModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}
