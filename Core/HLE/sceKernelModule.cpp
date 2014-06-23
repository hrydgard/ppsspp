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
#include <set>

#include "native/base/stringutil.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLETables.h"
#include "Core/Reporting.h"
#include "Core/Host.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/ELF/ElfReader.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/CoreTiming.h"
#include "Core/PSPLoaders.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPS.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"

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
	"disc0:/PSP_GAME/SYSDIR/UPDATE/EBOOT.BIN",
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

struct VarSymbolImport {
	char moduleName[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 nid;
	u32 stubAddr;
	u8 type;
};

struct VarSymbolExport {
	bool Matches(const VarSymbolImport &other) const {
		return !strncmp(moduleName, other.moduleName, KERNELOBJECT_MAX_NAME_LENGTH) && nid == other.nid;
	}

	char moduleName[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 nid;
	u32 symAddr;
};

struct FuncSymbolImport {
	char moduleName[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 stubAddr;
	u32 nid;
};

struct FuncSymbolExport {
	bool Matches(const FuncSymbolImport &other) const {
		return !strncmp(moduleName, other.moduleName, KERNELOBJECT_MAX_NAME_LENGTH) && nid == other.nid;
	}

	char moduleName[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 symAddr;
	u32 nid;
};

void ImportVarSymbol(const VarSymbolImport &var);
void ExportVarSymbol(const VarSymbolExport &var);
void UnexportVarSymbol(const VarSymbolExport &var);

void ImportFuncSymbol(const FuncSymbolImport &func);
void ExportFuncSymbol(const FuncSymbolExport &func);
void UnexportFuncSymbol(const FuncSymbolExport &func);

struct NativeModule {
	u32_le next;
	u16_le attribute;
	u8 version[2];
	char name[28];
	u32_le status;
	u32_le unk1;
	u32_le usermod_thid;
	u32_le memid;
	u32_le mpidtext;
	u32_le mpiddata;
	u32_le ent_top;
	u32_le ent_size;
	u32_le stub_top;
	u32_le stub_size;
	u32_le module_start_func;
	u32_le module_stop_func;
	u32_le module_bootstart_func;
	u32_le module_reboot_before_func;
	u32_le module_reboot_phase_func;
	u32_le entry_addr;
	u32_le gp_value;
	u32_le text_addr;
	u32_le text_size;
	u32_le data_size;
	u32_le bss_size;
	u32_le nsegment;
	u32_le segmentaddr[4];
	u32_le segmentsize[4];
	u32_le module_start_thread_priority;
	u32_le module_start_thread_stacksize;
	u32_le module_start_thread_attr;
	u32_le module_stop_thread_priority;
	u32_le module_stop_thread_stacksize;
	u32_le module_stop_thread_attr;
	u32_le module_reboot_before_thread_priority;
	u32_le module_reboot_before_thread_stacksize;
	u32_le module_reboot_before_thread_attr;
};

// by QueryModuleInfo
struct ModuleInfo {
	SceSize_le size;
	u32_le nsegment;
	u32_le segmentaddr[4];
	u32_le segmentsize[4];
	u32_le entry_addr;
	u32_le gp_value;
	u32_le text_addr;
	u32_le text_size;
	u32_le data_size;
	u32_le bss_size;
	u16_le attribute;
	u8 version[2];
	char name[28];
};

struct ModuleWaitingThread {
	SceUID threadID;
	u32 statusPtr;
};

enum NativeModuleStatus {
	MODULE_STATUS_STARTING = 4,
	MODULE_STATUS_STARTED = 5,
	MODULE_STATUS_STOPPING = 6,
	MODULE_STATUS_STOPPED = 7,
	MODULE_STATUS_UNLOADING = 8,
};

class Module : public KernelObject {
public:
	Module() : textStart(0), textEnd(0), memoryBlockAddr(0), isFake(false) {}
	~Module() {
		if (memoryBlockAddr) {
			userMemory.Free(memoryBlockAddr);
			symbolMap.UnloadModule(memoryBlockAddr, memoryBlockSize);
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
		auto s = p.Section("Module", 1, 3);
		if (!s)
			return;

		p.Do(nm);
		p.Do(memoryBlockAddr);
		p.Do(memoryBlockSize);
		p.Do(isFake);

		if (s < 2) {
			bool isStarted = false;
			p.Do(isStarted);
			if (isStarted)
				nm.status = MODULE_STATUS_STARTED;
			else
				nm.status = MODULE_STATUS_STOPPED;
		}

		if (s >= 3) {
			p.Do(textStart);
			p.Do(textEnd);
		}

		ModuleWaitingThread mwt = {0};
		p.Do(waitingThreads, mwt);
		FuncSymbolExport fsx = {{0}};
		p.Do(exportedFuncs, fsx);
		FuncSymbolImport fsi = {{0}};
		p.Do(importedFuncs, fsi);
		VarSymbolExport vsx = {{0}};
		p.Do(exportedVars, vsx);
		VarSymbolImport vsi = {{0}};
		p.Do(importedVars, vsi);
		RebuildImpExpModuleNames();

		if (p.mode == p.MODE_READ) {
			char moduleName[29] = {0};
			strncpy(moduleName, nm.name, ARRAY_SIZE(nm.name));
			symbolMap.AddModule(moduleName, memoryBlockAddr, memoryBlockSize);
		}
	}

	// We don't do this in the destructor to avoid annoying messages on game shutdown.
	void Cleanup();

	void ImportFunc(const FuncSymbolImport &func) {
		if (!Memory::IsValidAddress(func.stubAddr)) {
			WARN_LOG_REPORT(LOADER, "Invalid address for syscall stub %s %08x", func.moduleName, func.nid);
			return;
		}

		DEBUG_LOG(LOADER, "Importing %s : %08x", GetFuncName(func.moduleName, func.nid), func.stubAddr);

		// Add the symbol to the symbol map for debugging.
		char temp[256];
		sprintf(temp,"zz_%s", GetFuncName(func.moduleName, func.nid));
		symbolMap.AddFunction(temp,func.stubAddr,8);

		// Keep track and actually hook it up if possible.
		importedFuncs.push_back(func);
		impExpModuleNames.insert(func.moduleName);
		ImportFuncSymbol(func);
	}

	void ImportVar(const VarSymbolImport &var) {
		// Keep track and actually hook it up if possible.
		importedVars.push_back(var);
		impExpModuleNames.insert(var.moduleName);
		ImportVarSymbol(var);
	}

	void ExportFunc(const FuncSymbolExport &func) {
		if (isFake) {
			return;
		}
		exportedFuncs.push_back(func);
		impExpModuleNames.insert(func.moduleName);
		ExportFuncSymbol(func);
	}

	void ExportVar(const VarSymbolExport &var) {
		if (isFake) {
			return;
		}
		exportedVars.push_back(var);
		impExpModuleNames.insert(var.moduleName);
		ExportVarSymbol(var);
	}

	template <typename T>
	void RebuildImpExpList(const std::vector<T> &list) {
		for (size_t i = 0; i < list.size(); ++i) {
			impExpModuleNames.insert(list[i].moduleName);
		}
	}

	void RebuildImpExpModuleNames() {
		impExpModuleNames.clear();
		RebuildImpExpList(exportedFuncs);
		RebuildImpExpList(importedFuncs);
		RebuildImpExpList(exportedVars);
		RebuildImpExpList(importedVars);
	}

	bool ImportsOrExportsModuleName(const std::string &moduleName) {
		return impExpModuleNames.find(moduleName) != impExpModuleNames.end();
	}

	NativeModule nm;
	std::vector<ModuleWaitingThread> waitingThreads;

	std::vector<FuncSymbolExport> exportedFuncs;
	std::vector<FuncSymbolImport> importedFuncs;
	std::vector<VarSymbolExport> exportedVars;
	std::vector<VarSymbolImport> importedVars;
	std::set<std::string> impExpModuleNames;

	// Keep track of the code region so we can throw out analysis results
	// when unloaded.
	u32 textStart;
	u32 textEnd;

	u32 memoryBlockAddr;
	u32 memoryBlockSize;
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
		auto s = p.Section("AfterModuleEntryCall", 1);
		if (!s)
			return;

		p.Do(moduleID_);
		p.Do(retValAddr);
	}
	static Action *Create() {
		return new AfterModuleEntryCall;
	}
};

void AfterModuleEntryCall::run(MipsCall &call) {
	Memory::Write_U32(retValAddr, currentMIPS->r[MIPS_REG_V0]);
}

//////////////////////////////////////////////////////////////////////////
// MODULES
//////////////////////////////////////////////////////////////////////////
struct StartModuleInfo
{
	u32_le size;
	u32_le mpidtext;
	u32_le mpiddata;
	u32_le threadpriority;
	u32_le threadattributes;
};

struct SceKernelLMOption {
	SceSize_le size;
	SceUID_le mpidtext;
	SceUID_le mpiddata;
	u32_le flags;
	char position;
	char access;
	char creserved[2];
};

struct SceKernelSMOption {
	SceSize_le size;
	SceUID_le mpidstack;
	SceSize_le stacksize;
	s32_le priority;
	u32_le attribute;
};

//////////////////////////////////////////////////////////////////////////
// STATE BEGIN
static int actionAfterModule;

static std::set<SceUID> loadedModules;
// STATE END
//////////////////////////////////////////////////////////////////////////

void __KernelModuleInit()
{
	actionAfterModule = __KernelRegisterActionType(AfterModuleEntryCall::Create);
}

void __KernelModuleDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelModule", 1, 2);
	if (!s)
		return;

	p.Do(actionAfterModule);
	__KernelRestoreActionType(actionAfterModule, AfterModuleEntryCall::Create);

	if (s >= 2) {
		p.Do(loadedModules);
	}

	if (g_Config.bFuncReplacements) {
		MIPSAnalyst::ReplaceFunctions();
	}
}

void __KernelModuleShutdown()
{
	loadedModules.clear();
	MIPSAnalyst::Reset();
}

// Sometimes there are multiple LO16's or HI16's per pair, even though the ABI says nothing of this.
// For multiple LO16's, we need the original (unrelocated) instruction data of the HI16.
// For multiple HI16's, we just need to set each one.
struct HI16RelocInfo
{
	u32 addr;
	u32 data;
};
void WriteVarSymbol(u32 exportAddress, u32 relocAddress, u8 type, bool reverse = false)
{
	// We have to post-process the HI16 part, since it might be +1 or not depending on the LO16 value.
	static u32 lastHI16ExportAddress = 0;
	static std::vector<HI16RelocInfo> lastHI16Relocs;
	static bool lastHI16Processed = true;

	u32 relocData = Memory::Read_Instruction(relocAddress, true).encoding;

	switch (type)
	{
	case R_MIPS_NONE:
		WARN_LOG_REPORT(LOADER, "Var relocation type NONE - %08x => %08x", exportAddress, relocAddress);
		break;

	case R_MIPS_32:
		if (!reverse) {
			relocData += exportAddress;
		} else {
			relocData -= exportAddress;
		}
		break;

	// Not really tested, but should work...
	/*
	case R_MIPS_26:
		if (exportAddress % 4 || (exportAddress >> 28) != ((relocAddress + 4) >> 28)) {
			WARN_LOG_REPORT(LOADER, "Bad var relocation addresses for type 26 - %08x => %08x", exportAddress, relocAddress)
		} else {
			if (!reverse) {
				relocData = (relocData & ~0x03ffffff) | ((relocData + (exportAddress >> 2)) & 0x03ffffff);
			} else {
				relocData = (relocData & ~0x03ffffff) | ((relocData - (exportAddress >> 2)) & 0x03ffffff);
			}
		}
		break;
	*/

	case R_MIPS_HI16:
		if (lastHI16ExportAddress != exportAddress) {
			if (!lastHI16Processed && lastHI16Relocs.size() >= 1) {
				WARN_LOG_REPORT(LOADER, "Unsafe unpaired HI16 variable relocation @ %08x / %08x", lastHI16Relocs[lastHI16Relocs.size() - 1].addr, relocAddress);
			}

			lastHI16ExportAddress = exportAddress;
			lastHI16Relocs.clear();
		}

		// After this will be an R_MIPS_LO16.  If that addition overflows, we need to account for it in HI16.
		// The R_MIPS_LO16 and R_MIPS_HI16 will often be *different* relocAddress values.
		HI16RelocInfo reloc;
		reloc.addr = relocAddress;
		reloc.data = Memory::Read_Instruction(relocAddress, true).encoding;
		lastHI16Relocs.push_back(reloc);
		lastHI16Processed = false;
		break;

	case R_MIPS_LO16:
		{
			// Sign extend the existing low value (e.g. from addiu.)
			const u32 exportOffsetLo = exportAddress + (s16)(u16)(relocData & 0xFFFF);
			u32 full = exportOffsetLo;

			// The ABI requires that these come in pairs, at least.
			if (lastHI16Relocs.empty()) {
				ERROR_LOG_REPORT(LOADER, "LO16 without any HI16 variable import at %08x for %08x", relocAddress, exportAddress)
			// Try to process at least the low relocation...
			} else if (lastHI16ExportAddress != exportAddress) {
				ERROR_LOG_REPORT(LOADER, "HI16 and LO16 imports do not match at %08x for %08x (should be %08x)", relocAddress, lastHI16ExportAddress, exportAddress)
			} else {
				// Process each of the HI16.  Usually there's only one.
				for (auto it = lastHI16Relocs.begin(), end = lastHI16Relocs.end(); it != end; ++it)
				{
					if (!reverse) {
						full = (it->data << 16) + exportOffsetLo;
					} else {
						full = (it->data << 16) - exportOffsetLo;
					}
					// The low instruction will be a signed add, which means (full & 0x8000) will subtract.
					// We add 1 in that case so that it ends up the right value.
					u16 high = (full >> 16) + ((full & 0x8000) ? 1 : 0);
					Memory::Write_U32((it->data & ~0xFFFF) | high, it->addr);
					currentMIPS->InvalidateICache(it->addr, 4);
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
	currentMIPS->InvalidateICache(relocAddress, 4);
}

void ImportVarSymbol(const VarSymbolImport &var) {
	if (var.nid == 0) {
		// TODO: What's the right thing for this?
		ERROR_LOG_REPORT(LOADER, "Var import with nid = 0, type = %d", var.type);
		return;
	}

	if (!Memory::IsValidAddress(var.stubAddr)) {
		ERROR_LOG_REPORT(LOADER, "Invalid address for var import nid = %08x, type = %d, addr = %08x", var.nid, var.type, var.stubAddr);
		return;
	}

	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for exports currently loaded modules already have.  Maybe it's available?
		for (auto it = module->exportedVars.begin(), end = module->exportedVars.end(); it != end; ++it) {
			if (it->Matches(var)) {
				WriteVarSymbol(it->symAddr, var.stubAddr, var.type);
				return;
			}
		}
	}

	// It hasn't been exported yet, but hopefully it will later.
	INFO_LOG(LOADER, "Variable (%s,%08x) unresolved, storing for later resolving", var.moduleName, var.nid);
}

void ExportVarSymbol(const VarSymbolExport &var) {
	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for imports currently loaded modules already have, hook it up right away.
		for (auto it = module->importedVars.begin(), end = module->importedVars.end(); it != end; ++it) {
			if (var.Matches(*it)) {
				INFO_LOG(LOADER, "Resolving var %s/%08x", var.moduleName, var.nid);
				WriteVarSymbol(var.symAddr, it->stubAddr, it->type);
			}
		}
	}
}

void UnexportVarSymbol(const VarSymbolExport &var) {
	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for imports modules that are *still* loaded have, and reverse them.
		for (auto it = module->importedVars.begin(), end = module->importedVars.end(); it != end; ++it) {
			if (var.Matches(*it)) {
				INFO_LOG(LOADER, "Unresolving var %s/%08x", var.moduleName, var.nid);
				WriteVarSymbol(var.symAddr, it->stubAddr, it->type, true);
			}
		}
	}
}

void ImportFuncSymbol(const FuncSymbolImport &func) {
	// Prioritize HLE implementations.
	// TODO: Or not?
	if (FuncImportIsSyscall(func.moduleName, func.nid)) {
		WriteSyscall(func.moduleName, func.nid, func.stubAddr);
		currentMIPS->InvalidateICache(func.stubAddr, 8);
		return;
	}

	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for exports currently loaded modules already have.  Maybe it's available?
		for (auto it = module->exportedFuncs.begin(), end = module->exportedFuncs.end(); it != end; ++it) {
			if (it->Matches(func)) {
				WriteFuncStub(func.stubAddr, it->symAddr);
				currentMIPS->InvalidateICache(func.stubAddr, 8);
				return;
			}
		}
	}

	// It hasn't been exported yet, but hopefully it will later.
	if (GetModuleIndex(func.moduleName) != -1) {
		WARN_LOG_REPORT(LOADER, "Unknown syscall in known module: %s 0x%08x", func.moduleName, func.nid);
	} else {
		INFO_LOG(LOADER, "Function (%s,%08x) unresolved, storing for later resolving", func.moduleName, func.nid);
	}
	WriteFuncMissingStub(func.stubAddr, func.nid);
	currentMIPS->InvalidateICache(func.stubAddr, 8);
}

void ExportFuncSymbol(const FuncSymbolExport &func) {
	if (FuncImportIsSyscall(func.moduleName, func.nid)) {
		// Oops, HLE covers this.
		WARN_LOG_REPORT(LOADER, "Ignoring func export %s/%08x, already implemented in HLE.", func.moduleName, func.nid);
		return;
	}

	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for imports currently loaded modules already have, hook it up right away.
		for (auto it = module->importedFuncs.begin(), end = module->importedFuncs.end(); it != end; ++it) {
			if (func.Matches(*it)) {
				INFO_LOG(LOADER, "Resolving function %s/%08x", func.moduleName, func.nid);
				WriteFuncStub(it->stubAddr, func.symAddr);
				currentMIPS->InvalidateICache(it->stubAddr, 8);
			}
		}
	}
}

void UnexportFuncSymbol(const FuncSymbolExport &func) {
	if (FuncImportIsSyscall(func.moduleName, func.nid)) {
		// Oops, HLE covers this.
		return;
	}

	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for imports modules that are *still* loaded have, and write back stubs.
		for (auto it = module->importedFuncs.begin(), end = module->importedFuncs.end(); it != end; ++it) {
			if (func.Matches(*it)) {
				INFO_LOG(LOADER, "Unresolving function %s/%08x", func.moduleName, func.nid);
				WriteFuncMissingStub(it->stubAddr, it->nid);
				currentMIPS->InvalidateICache(it->stubAddr, 8);
			}
		}
	}
}

void Module::Cleanup() {
	MIPSAnalyst::ForgetFunctions(textStart, textEnd);

	loadedModules.erase(GetUID());

	for (auto it = exportedVars.begin(), end = exportedVars.end(); it != end; ++it) {
		UnexportVarSymbol(*it);
	}
	for (auto it = exportedFuncs.begin(), end = exportedFuncs.end(); it != end; ++it) {
		UnexportFuncSymbol(*it);
	}
}

void __SaveDecryptedEbootToStorageMedia(const u8 *decryptedEbootDataPtr, const u32 length) {
	if (!decryptedEbootDataPtr) {
		ERROR_LOG(SCEMODULE, "Error saving decrypted EBOOT.BIN: invalid pointer");
		return;
	}

	if (length == 0) {
		ERROR_LOG(SCEMODULE, "Error saving decrypted EBOOT.BIN: invalid length");
		return;
	}

	const std::string filenameToDumpTo = g_paramSFO.GetValueString("DISC_ID") + ".BIN";
	const std::string dumpDirectory = GetSysDirectory(DIRECTORY_DUMP);
	const std::string fullPath = dumpDirectory + filenameToDumpTo;

	// If the file already exists, don't dump it again.
	if (File::Exists(fullPath)) {
		INFO_LOG(SCEMODULE, "Decrypted EBOOT.BIN already exists for this game, skipping dump.");
		return;
	}

	// Make sure the dump directory exists before continuing.
	if (!File::Exists(dumpDirectory)) {
		if (!File::CreateDir(dumpDirectory)) {
			ERROR_LOG(SCEMODULE, "Unable to create directory for EBOOT dumping, aborting.");
			return;
		}
	}

	FILE *decryptedEbootFile = fopen(fullPath.c_str(), "wb");
	if (!decryptedEbootFile) {
		ERROR_LOG(SCEMODULE, "Unable to write decrypted EBOOT.");
		return;
	}

	const size_t lengthToWrite = length;

	fwrite(decryptedEbootDataPtr, sizeof(u8), lengthToWrite, decryptedEbootFile);
	fclose(decryptedEbootFile);
	INFO_LOG(SCEMODULE, "Successfully wrote decrypted EBOOT to %s", fullPath.c_str());
}

static bool IsHLEVersionedModule(const char *name) {
	// TODO: Only some of these are currently known to be versioned.
	// Potentially only sceMpeg_library matters.
	// For now, we're just reporting version numbers.
	for (size_t i = 0; i < ARRAY_SIZE(blacklistedModules); i++) {
		if (!strncmp(name, blacklistedModules[i], 28)) {
			return true;
		}
	}
	static const char *otherModules[] = {
		"sceAvcodec_driver",
		"sceAudiocodec_Driver",
		"sceAudiocodec",
		"sceVideocodec_Driver",
		"sceVideocodec",
		"sceMpegbase_Driver",
		"sceMpegbase",
		"scePsmf_library",
		"scePsmfP_library",
		"scePsmfPlayer",
		"sceSAScore",
		"sceCcc_Library",
		"SceParseHTTPheader_Library",
		"SceParseURI_Library",
		// Guessing.
		"sceJpeg",
		"sceJpeg_library",
		"sceJpeg_Library",
	};
	for (size_t i = 0; i < ARRAY_SIZE(otherModules); i++) {
		if (!strncmp(name, otherModules[i], 28)) {
			return true;
		}
	}
	return false;
}

Module *__KernelLoadELFFromPtr(const u8 *ptr, u32 loadAddress, std::string *error_string, u32 *magic) {
	Module *module = new Module;
	kernelObjects.Create(module);
	loadedModules.insert(module->GetUID());
	memset(&module->nm, 0, sizeof(module->nm));

	bool reportedModule = false;
	u32 devkitVersion = 0;
	u8 *newptr = 0;
	u32_le *magicPtr = (u32_le *) ptr;
	if (*magicPtr == 0x4543537e) { // "~SCE"
		INFO_LOG(SCEMODULE, "~SCE module, skipping header");
		ptr += *(u32_le*)(ptr + 4);
		magicPtr = (u32_le *)ptr;
	}
	*magic = *magicPtr;
	if (*magic == 0x5053507e) { // "~PSP"
		DEBUG_LOG(SCEMODULE, "Decrypting ~PSP file");
		PSP_Header *head = (PSP_Header*)ptr;
		devkitVersion = head->devkitversion;

		if (IsHLEVersionedModule(head->modname)) {
			int ver = (head->module_ver_hi << 8) | head->module_ver_lo;
			char temp[256];
			snprintf(temp, sizeof(temp), "Loading module %s with version %%04x, devkit %%08x", head->modname);
			INFO_LOG_REPORT(SCEMODULE, temp, ver, head->devkitversion);
			reportedModule = true;

			if (!strcmp(head->modname, "sceMpeg_library")) {
				__MpegLoadModule(ver);
			}
		}

		const u8 *in = ptr;
		u32 size = head->elf_size;
		if (head->psp_size > size)
		{
			size = head->psp_size;
		}
		newptr = new u8[head->elf_size + head->psp_size];
		ptr = newptr;
		magicPtr = (u32_le *)ptr;
		int ret = pspDecryptPRX(in, (u8*)ptr, head->psp_size);
		if (ret == MISSING_KEY) {
			// This should happen for all "kernel" modules so disabling.
			// Reporting::ReportMessage("Missing PRX decryption key!");
			*error_string = "Missing key";
			delete [] newptr;
			module->isFake = true;
			strncpy(module->nm.name, head->modname, ARRAY_SIZE(module->nm.name));
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;
			return module;
		} else if (ret <= 0) {
			ERROR_LOG(SCEMODULE, "Failed decrypting PRX! That's not normal! ret = %i\n", ret);
			Reporting::ReportMessage("Failed decrypting the PRX (ret = %i, size = %i, psp_size = %i)!", ret, head->elf_size, head->psp_size);
			// Fall through to safe exit in the next check.
		} else {
			// TODO: Is this right?
			module->nm.bss_size = head->bss_size;

			// If we've made it this far, it should be safe to dump.
			if (g_Config.bDumpDecryptedEboot) {
				INFO_LOG(SCEMODULE, "Dumping derypted EBOOT.BIN to file.");
				const u32 dumpLength = ret;
				__SaveDecryptedEbootToStorageMedia(ptr, dumpLength);
			}
		}
	}

	// DO NOT change to else if, see above.
	if (*magicPtr != 0x464c457f) {
		ERROR_LOG_REPORT(SCEMODULE, "Wrong magic number %08x", *magicPtr);
		*error_string = "File corrupt";
		if (newptr)
			delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	// Open ELF reader
	ElfReader reader((void*)ptr);

	int result = reader.LoadInto(loadAddress);
	if (result != SCE_KERNEL_ERROR_OK) 	{
		ERROR_LOG(SCEMODULE, "LoadInto failed with error %08x",result);
		if (newptr)
			delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<Module>(module->GetUID());
		return 0;
	}
	module->memoryBlockAddr = reader.GetVaddr();
	module->memoryBlockSize = reader.GetTotalSize();

	struct PspModuleInfo {
		// 0, 0, 1, 1 ?
		u16_le moduleAttrs; //0x0000 User Mode, 0x1000 Kernel Mode
		u16_le moduleVersion;
		// 28 bytes of module name, packed with 0's.
		char name[28];
		u32_le gp;					 // ptr to MIPS GOT data	(global offset table)
		u32_le libent;			 // ptr to .lib.ent section 
		u32_le libentend;		// ptr to end of .lib.ent section 
		u32_le libstub;			// ptr to .lib.stub section 
		u32_le libstubend;	 // ptr to end of .lib.stub section 
	};

	SectionID sceModuleInfoSection = reader.GetSectionByName(".rodata.sceModuleInfo");
	PspModuleInfo *modinfo;
	if (sceModuleInfoSection != -1)
		modinfo = (PspModuleInfo *)Memory::GetPointer(reader.GetSectionAddr(sceModuleInfoSection));
	else
		modinfo = (PspModuleInfo *)Memory::GetPointer(reader.GetSegmentVaddr(0) + (reader.GetSegmentPaddr(0) & 0x7FFFFFFF) - reader.GetSegmentOffset(0));

	module->nm.nsegment = reader.GetNumSegments();
	module->nm.attribute = modinfo->moduleAttrs;
	module->nm.version[0] = modinfo->moduleVersion & 0xFF;
	module->nm.version[1] = modinfo->moduleVersion >> 8;
	module->nm.data_size = 0;
	// TODO: Is summing them up correct?  Must not be since the numbers aren't exactly right.
	for (int i = 0; i < reader.GetNumSegments(); ++i) {
		module->nm.data_size += reader.GetSegmentDataSize(i);
	}
	module->nm.gp_value = modinfo->gp;
	strncpy(module->nm.name, modinfo->name, ARRAY_SIZE(module->nm.name));

	// Let's also get a truncated version.
	char moduleName[29] = {0};
	strncpy(moduleName, modinfo->name, ARRAY_SIZE(module->nm.name));

	// Check for module blacklist - we don't allow games to load these modules from disc
	// as we have HLE implementations and the originals won't run in the emu because they
	// directly access hardware or for other reasons.
	for (u32 i = 0; i < ARRAY_SIZE(blacklistedModules); i++) {
		if (strncmp(modinfo->name, blacklistedModules[i], ARRAY_SIZE(modinfo->name)) == 0) {
			module->isFake = true;
		}
	}

	if (!module->isFake) {
		symbolMap.AddModule(moduleName, module->memoryBlockAddr, module->memoryBlockSize);
	}

	SectionID textSection = reader.GetSectionByName(".text");

	if (textSection != -1) {
		module->textStart = reader.GetSectionAddr(textSection);
		u32 textSize = reader.GetSectionSize(textSection);
		module->textEnd = module->textStart + textSize;

		module->nm.text_addr = module->textStart;
		// TODO: This value appears to be wrong.  In one example, the PSP has a value > 0x1000 bigger.
		module->nm.text_size = textSize;
		// TODO: It seems like the data size excludes the text size, which kinda makes sense?
		module->nm.data_size -= textSize;

		if (!module->isFake) {
#if !defined(MOBILE_DEVICE)
			bool gotSymbols = reader.LoadSymbols();
			MIPSAnalyst::ScanForFunctions(module->textStart, module->textEnd, !gotSymbols);
#else
			if (g_Config.bFuncReplacements) {
				bool gotSymbols = reader.LoadSymbols();
				MIPSAnalyst::ScanForFunctions(module->textStart, module->textEnd, !gotSymbols);
			}
#endif
		}
	}

	INFO_LOG(LOADER, "Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent, modinfo->libstub);

	struct PspLibStubEntry {
		u32_le name;
		u16_le version;
		u16_le flags;
		u8 size;
		u8 numVars;
		u16_le numFuncs;
		// each symbol has an associated nid; nidData is a pointer
		// (in .rodata.sceNid section) to an array of longs, one
		// for each function, which identifies the function whose
		// address is to be inserted.
		//
		// The hash is the first 4 bytes of a SHA-1 hash of the function
		// name.	(Represented as a little-endian long, so the order
		// of the bytes is reversed.)
		u32_le nidData;
		// the address of the function stubs where the function address jumps
		// should be filled in
		u32_le firstSymAddr;
		// Optional, this is where var relocations are.
		// They use the format: u32 addr, u32 nid, ...
		// WARNING: May have garbage if size < 6.
		u32_le varData;
		// Not sure what this is yet, assume garbage for now.
		// TODO: Tales of the World: Radiant Mythology 2 has something here?
		u32_le extra;
	};

	DEBUG_LOG(LOADER,"===================================================");

	u32_le *entryPos = (u32_le *)Memory::GetPointer(modinfo->libstub);
	u32_le *entryEnd = (u32_le *)Memory::GetPointer(modinfo->libstubend);

	u32_le firstImportStubAddr = 0;

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
			if (entry->size != 7) {
				WARN_LOG_REPORT(LOADER, "Unexpected module entry size %d", entry->size);
				needReport = true;
			} else if (entry->extra != 0) {
				WARN_LOG_REPORT(LOADER, "Unexpected module entry with non-zero 7th value %08x", entry->extra);
				needReport = true;
			}
		}

		// If nidData is 0, only variables are being imported.
		if (entry->nidData != 0) {
			if (!Memory::IsValidAddress(entry->nidData)) {
				ERROR_LOG_REPORT(LOADER, "Crazy nidData address %08x, skipping entire module", entry->nidData);
				needReport = true;
				continue;
			}

			FuncSymbolImport func;
			strncpy(func.moduleName, modulename, KERNELOBJECT_MAX_NAME_LENGTH);
			func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

			u32_le *nidDataPtr = (u32_le *)Memory::GetPointer(entry->nidData);
			for (int i = 0; i < entry->numFuncs; ++i) {
				// This is the id of the import.
				func.nid = nidDataPtr[i];
				// This is the address to write the j and delay slot to.
				func.stubAddr = entry->firstSymAddr + i * 8;
				module->ImportFunc(func);
			}

			if (!firstImportStubAddr || firstImportStubAddr > entry->firstSymAddr)
				firstImportStubAddr = entry->firstSymAddr;
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

			VarSymbolImport var;
			strncpy(var.moduleName, modulename, KERNELOBJECT_MAX_NAME_LENGTH);
			var.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

			for (int i = 0; i < entry->numVars; ++i) {
				u32 varRefsPtr = Memory::Read_U32(entry->varData + i * 8);
				u32 nid = Memory::Read_U32(entry->varData + i * 8 + 4);
				if (!Memory::IsValidAddress(varRefsPtr)) {
					WARN_LOG_REPORT(LOADER, "Bad relocation list address for nid %08x in %s", nid, modulename);
					continue;
				}

				u32_le *varRef = (u32_le *)Memory::GetPointer(varRefsPtr);
				for (; *varRef != 0; ++varRef) {
					var.nid = nid;
					var.stubAddr = (*varRef & 0x03FFFFFF) << 2;
					var.type = *varRef >> 26;
					module->ImportVar(var);
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
		entryPos = (u32_le *)Memory::GetPointer(modinfo->libstub);
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

	if (textSection == -1) {
		module->textStart = reader.GetVaddr();
		module->textEnd = firstImportStubAddr - 4;

		if (!module->isFake) {
#if !defined(MOBILE_DEVICE)
			bool gotSymbols = reader.LoadSymbols();
			MIPSAnalyst::ScanForFunctions(module->textStart, module->textEnd, !gotSymbols);
#else
			if (g_Config.bFuncReplacements) {
				bool gotSymbols = reader.LoadSymbols();
				MIPSAnalyst::ScanForFunctions(module->textStart, module->textEnd, !gotSymbols);
			}
#endif
		}
	}

	// Look at the exports, too.

	struct PspLibEntEntry
	{
		u32_le name; /* ent's name (module name) address */
		u16_le version;
		u16_le flags;
		u8 size;
		u8 vcount;
		u16_le fcount;
		u32_le resident;
		u16_le vcountNew;
		u8 unknown1;
		u8 unknown2;
	};

	u32_le *entPos = (u32_le *)Memory::GetPointer(modinfo->libent);
	u32_le *entEnd = (u32_le *)Memory::GetPointer(modinfo->libentend);
	for (int m = 0; entPos < entEnd; ++m) {
		PspLibEntEntry *ent = (PspLibEntEntry *)entPos;
		entPos += ent->size;
		if (ent->size == 0) {
			WARN_LOG_REPORT(LOADER, "Invalid export entry size %d", ent->size);
			entPos += 4;
			continue;
		}

		u32 variableCount = ent->size <= 4 ? ent->vcount : std::max((u32)ent->vcount , (u32)ent->vcountNew);
		const char *name;
		if (Memory::IsValidAddress(ent->name)) {
			name = Memory::GetCharPointer(ent->name);
		} else if (ent->name == 0) {
			name = module->nm.name;
		} else {
			name = "invalid?";
		}

		INFO_LOG(LOADER, "Exporting ent %d named %s, %d funcs, %d vars, resident %08x", m, name, ent->fcount, ent->vcount, ent->resident);

		if (!Memory::IsValidAddress(ent->resident)) {
			if (ent->fcount + variableCount > 0) {
				WARN_LOG_REPORT(LOADER, "Invalid export resident address %08x", ent->resident);
			}
			continue;
		}

		u32_le *residentPtr = (u32_le *)Memory::GetPointer(ent->resident);
		u32_le *exportPtr = residentPtr + ent->fcount + variableCount;

		if (ent->size != 4 && ent->unknown1 != 0 && ent->unknown2 != 0) {
			WARN_LOG_REPORT(LOADER, "Unexpected export module entry size %d, vcountNew=%08x, unknown1=%08x, unknown2=%08x", ent->size, ent->vcountNew, ent->unknown1, ent->unknown2);
		}

		FuncSymbolExport func;
		strncpy(func.moduleName, name, KERNELOBJECT_MAX_NAME_LENGTH);
		func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

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
				func.nid = nid;
				func.symAddr = exportAddr;
				module->ExportFunc(func);
			}
		}

		VarSymbolExport var;
		strncpy(var.moduleName, name, KERNELOBJECT_MAX_NAME_LENGTH);
		var.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

		for (u32 j = 0; j < variableCount; j++) {
			u32 nid = residentPtr[ent->fcount + j];
			u32 exportAddr = exportPtr[ent->fcount + j];

			int size;
			switch (nid) {
			case NID_MODULE_INFO:
				// Points to a PspModuleInfo, often the exact one .rodata.sceModuleInfo points to.
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
				devkitVersion = Memory::Read_U32(exportAddr);
				break;
			default:
				var.nid = nid;
				var.symAddr = exportAddr;
				module->ExportVar(var);
				break;
			}
		}
	}

	if (!module->isFake) {
		module->nm.entry_addr = reader.GetEntryPoint();
	
		// use module_start_func instead of entry_addr if entry_addr is 0
		if (module->nm.entry_addr == 0)
			module->nm.entry_addr = module->nm.module_start_func;
	} else {
		module->nm.entry_addr = -1;
	}

	if (newptr)
		delete [] newptr;

	if (!reportedModule && IsHLEVersionedModule(modinfo->name)) {
		char temp[256];
		snprintf(temp, sizeof(temp), "Loading module %s with version %%04x, devkit %%08x", modinfo->name);
		INFO_LOG_REPORT(SCEMODULE, temp, modinfo->moduleVersion, devkitVersion);

		if (!strcmp(modinfo->name, "sceMpeg_library")) {
			__MpegLoadModule(modinfo->moduleVersion);
		}
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
		u32_le version;
		memcpy(&version, fileptr + 4, 4);
		u32_le offset0, offsets[16];
		int numfiles;

		memcpy(&offset0, fileptr + 8, 4);
		numfiles = (offset0 - 8)/4;
		offsets[0] = offset0;
		for (int i = 1; i < numfiles; i++)
			memcpy(&offsets[i], fileptr + 12 + 4*i, 4);
		u32 magic = 0;


		u8 *temp = 0;
		if (offsets[5] & 3) {
			// Our loader does NOT like to load from an unaligned address on ARM!
			size_t size = offsets[6] - offsets[5];
			temp = new u8[size];
			memcpy(temp, fileptr + offsets[5], size);
			INFO_LOG(LOADER, "Elf unaligned, aligning!")
		}

		module = __KernelLoadELFFromPtr(temp ? temp : fileptr + offsets[5], PSP_GetDefaultLoadAddress(), error_string, &magic);

		if (temp) {
			delete [] temp;
		}
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
	m->nm.status = MODULE_STATUS_STARTED;
	if (m->nm.module_start_func != 0 && m->nm.module_start_func != (u32)-1)
	{
		if (m->nm.module_start_func != m->nm.entry_addr)
			WARN_LOG_REPORT(LOADER, "Main module has start func (%08x) different from entry (%08x)?", m->nm.module_start_func, m->nm.entry_addr);
		// TODO: Should we try to run both?
		currentMIPS->pc = m->nm.module_start_func;
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

bool __KernelLoadExec(const char *filename, u32 paramPtr, std::string *error_string)
{
	SceKernelLoadExecParam param;

	if (paramPtr)
		Memory::ReadStruct(paramPtr, &param);
	else
		memset(&param, 0, sizeof(SceKernelLoadExecParam));

	u8 *param_argp = 0;
	u8 *param_key = 0;
	if (param.args > 0) {
		u32 argpAddr = param.argp;
		param_argp = new u8[param.args];
		Memory::Memcpy(param_argp, argpAddr, param.args);
	}
	if (param.keyp != 0) {
		u32 keyAddr = param.keyp;
		size_t keylen = strlen(Memory::GetCharPointer(keyAddr))+1;
		param_key = new u8[keylen];
		Memory::Memcpy(param_key, keyAddr, (u32)keylen);
	}

	// Wipe kernel here, loadexec should reset the entire system
	if (__KernelIsRunning())
	{
		__KernelShutdown();
		//HLE needs to be reset here
		HLEShutdown();
		HLEInit();
		GPU_Reinitialize();
	}

	__KernelModuleInit();
	__KernelInit();

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	if (!info.exists) {
		ERROR_LOG(LOADER, "Failed to load executable %s - file doesn't exist", filename);
		*error_string = StringFromFormat("Could not find executable %s", filename);
		if (paramPtr) {
			if (param_argp) delete[] param_argp;
			if (param_key) delete[] param_key;
		}
		return false;
	}

	u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ);

	u8 *temp = new u8[(int)info.size + 0x01000000];

	pspFileSystem.ReadFile(handle, temp, (size_t)info.size);

	Module *module = __KernelLoadModule(temp, 0, error_string);

	if (!module || module->isFake) {
		if (module) {
			module->Cleanup();
			kernelObjects.Destroy<Module>(module->GetUID());
		}
		ERROR_LOG(LOADER, "Failed to load module %s", filename);
		*error_string = "Failed to load executable: " + *error_string;
		delete [] temp;
		if (paramPtr) {
			if (param_argp) delete[] param_argp;
			if (param_key) delete[] param_key;
		}
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

	if (paramPtr)
		__KernelStartModule(module, param.args, (const char*)param_argp, &option);
	else
		__KernelStartModule(module, (u32)strlen(filename) + 1, filename, &option);

	__KernelStartIdleThreads(module->GetUID());

	if (param_argp) delete[] param_argp;
	if (param_key) delete[] param_key;

	hleSkipDeadbeef();
	return true;
}

int sceKernelLoadExec(const char *filename, u32 paramPtr)
{
	std::string exec_filename = filename;
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

	DEBUG_LOG(SCEMODULE, "sceKernelLoadExec(name=%s,...): loading %s", filename, exec_filename.c_str());
	std::string error_string;
	if (!__KernelLoadExec(exec_filename.c_str(), paramPtr, &error_string)) {
		ERROR_LOG(SCEMODULE, "sceKernelLoadExec failed: %s", error_string.c_str());
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
			loadedModules.insert(module->GetUID());
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

	if (flags != 0) {
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModule: unsupported flags: %08x", flags);
	}
	SceKernelLMOption *lmoption = 0;
	int position = 0;
	// TODO: Use position to decide whether to load high or low
	if (optionAddr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(optionAddr);
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModule: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
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

		if (info.name == "BOOT.BIN")
		{
			NOTICE_LOG(LOADER, "Module %s is blacklisted or undecryptable - we try __KernelLoadExec", name)
			return __KernelLoadExec(name, 0, &error_string);
		}
		else
		{
			// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run..
			// Let's just act as if it worked.
			NOTICE_LOG(LOADER, "Module %s is blacklisted or undecryptable - we lie about success", name)
			return 1;
		}
	}

	if (lmoption) {
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModule(name=%s,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),name,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModule(name=%s,flag=%08x,(...))", module->GetUID(), name, flags);
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
	SceKernelSMOption smoption = {0};
	if (optionAddr) {
		Memory::ReadStruct(optionAddr, &smoption);
	}
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module) {
		RETURN(error);
		return;
	} else if (module->isFake) {
		INFO_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): faked (undecryptable module)",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		RETURN(moduleId);
		return;
	} else if (module->nm.status == MODULE_STATUS_STARTED) {
		ERROR_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x) : already started",
		moduleId,argsize,argAddr,returnValueAddr,optionAddr);
		// TODO: Maybe should be SCE_KERNEL_ERROR_ALREADY_STARTED, but I get SCE_KERNEL_ERROR_ERROR.
		// But I also get crashes...
		RETURN(SCE_KERNEL_ERROR_ERROR);
		return;
	} else {
		INFO_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x)",
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
				WARN_LOG(SCEMODULE, "sceKernelStartModule(): module has no start or entry func");
				module->nm.status = MODULE_STATUS_STARTED;
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
			module->nm.status = MODULE_STATUS_STARTING;
			module->waitingThreads.push_back(mwt);
		}
		else if (entryAddr == 0)
		{
			INFO_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): no entry address",
			moduleId,argsize,argAddr,returnValueAddr,optionAddr);
			module->nm.status = MODULE_STATUS_STARTED;
		}
		else
		{
			ERROR_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): invalid entry address",
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
		ERROR_LOG(SCEMODULE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): invalid module id", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		return error;
	}

	if (module->isFake)
	{
		INFO_LOG(SCEMODULE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x) - faking", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		return 0;
	}
	if (module->nm.status != MODULE_STATUS_STARTED)
	{
		ERROR_LOG(SCEMODULE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): already stopped", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
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
		auto options = PSPPointer<SceKernelSMOption>::Create(optionAddr);
		// TODO: Check how size handling actually works.
		if (options->size != 0 && options->priority != 0)
			priority = options->priority;
		if (options->size != 0 && options->stacksize != 0)
			stacksize = options->stacksize;
		if (options->size != 0 && options->attribute != 0)
			attr = options->attribute;
		// TODO: Maybe based on size?
		else if (attr != 0)
			WARN_LOG_REPORT(SCEMODULE, "Stopping module with attr=%x, but options specify 0", attr);
	}

	if (Memory::IsValidAddress(stopFunc))
	{
		SceUID threadID = __KernelCreateThread(module->nm.name, moduleId, stopFunc, priority, stacksize, attr, 0);
		sceKernelStartThread(threadID, argSize, argAddr);
		__KernelSetThreadRA(threadID, NID_MODULERETURN);
		__KernelWaitCurThread(WAITTYPE_MODULE, moduleId, 1, 0, false, "stopped module");

		const ModuleWaitingThread mwt = {__KernelGetCurThread(), returnValueAddr};
		module->nm.status = MODULE_STATUS_STOPPING;
		module->waitingThreads.push_back(mwt);
	}
	else if (stopFunc == 0)
	{
		INFO_LOG(SCEMODULE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): no stop func, skipping", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		module->nm.status = MODULE_STATUS_STOPPED;
	}
	else
	{
		ERROR_LOG_REPORT(SCEMODULE, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): bad stop func address", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
		module->nm.status = MODULE_STATUS_STOPPED;
	}

	return 0;
}

u32 sceKernelUnloadModule(u32 moduleId)
{
	INFO_LOG(SCEMODULE,"sceKernelUnloadModule(%i)", moduleId);
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module)
		return error;

	module->Cleanup();
	kernelObjects.Destroy<Module>(moduleId);
	return moduleId;
}

u32 sceKernelStopUnloadSelfModuleWithStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr) {
	if (loadedModules.size() > 1) {
		ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game may have crashed", exitCode, argSize, argp, statusAddr, optionAddr);

		SceUID moduleID = __KernelGetCurThreadModuleId();
		u32 priority = 0x20;
		u32 stacksize = 0x40000;
		u32 attr = 0;

		// TODO: In a lot of cases (even for errors), this should resched.  Needs testing.

		u32 error;
		Module *module = kernelObjects.Get<Module>(moduleID, error);
		if (!module) {
			ERROR_LOG(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): invalid module id", exitCode, argSize, argp, statusAddr, optionAddr);
			return error;
		}

		u32 stopFunc = module->nm.module_stop_func;
		if (module->nm.module_stop_thread_priority != 0)
			priority = module->nm.module_stop_thread_priority;
		if (module->nm.module_stop_thread_stacksize != 0)
			stacksize = module->nm.module_stop_thread_stacksize;
		if (module->nm.module_stop_thread_attr != 0)
			attr = module->nm.module_stop_thread_attr;

		// TODO: Need to test how this really works.  Let's assume it's an override.
		if (Memory::IsValidAddress(optionAddr)) {
			auto options = PSPPointer<SceKernelSMOption>::Create(optionAddr);
			// TODO: Check how size handling actually works.
			if (options->size != 0 && options->priority != 0)
				priority = options->priority;
			if (options->size != 0 && options->stacksize != 0)
				stacksize = options->stacksize;
			if (options->size != 0 && options->attribute != 0)
				attr = options->attribute;
			// TODO: Maybe based on size?
			else if (attr != 0)
				WARN_LOG_REPORT(SCEMODULE, "Stopping module with attr=%x, but options specify 0", attr);
		}

		if (Memory::IsValidAddress(stopFunc)) {
			SceUID threadID = __KernelCreateThread(module->nm.name, moduleID, stopFunc, priority, stacksize, attr, 0);
			sceKernelStartThread(threadID, argSize, argp);
			__KernelSetThreadRA(threadID, NID_MODULERETURN);
			__KernelWaitCurThread(WAITTYPE_MODULE, moduleID, 1, 0, false, "unloadstopped module");

			const ModuleWaitingThread mwt = {__KernelGetCurThread(), statusAddr};
			module->nm.status = MODULE_STATUS_UNLOADING;
			module->waitingThreads.push_back(mwt);
		} else if (stopFunc == 0) {
			INFO_LOG(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): no stop func", exitCode, argSize, argp, statusAddr, optionAddr);
			sceKernelExitDeleteThread(exitCode);
			module->Cleanup();
			kernelObjects.Destroy<Module>(moduleID);
		} else {
			ERROR_LOG_REPORT(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): bad stop func address", exitCode, argSize, argp, statusAddr, optionAddr);
			sceKernelExitDeleteThread(exitCode);
			module->Cleanup();
			kernelObjects.Destroy<Module>(moduleID);
		}
	} else {
		ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp, statusAddr, optionAddr);
	}

	// Probably similar to sceKernelStopModule, but games generally call this when they die.
	return 0;
}

void __KernelReturnFromModuleFunc()
{
	// Return from the thread as normal.
	hleSkipDeadbeef();
	__KernelReturnFromThread();

	SceUID leftModuleID = __KernelGetCurThreadModuleId();
	SceUID leftThreadID = __KernelGetCurThread();
	int exitStatus = sceKernelGetThreadExitStatus(leftThreadID);

	// Reschedule immediately (to leave the thread) and delete it and its stack.
	__KernelReSchedule("returned from module");
	sceKernelDeleteThread(leftThreadID);

	u32 error;
	Module *module = kernelObjects.Get<Module>(leftModuleID, error);
	if (!module) {
		ERROR_LOG_REPORT(SCEMODULE, "Returned from deleted module start/stop func");
		return;
	}

	// We can't be starting and stopping at the same time, so no need to differentiate.
	if (module->nm.status == MODULE_STATUS_STARTING)
		module->nm.status = MODULE_STATUS_STARTED;
	if (module->nm.status == MODULE_STATUS_STOPPING)
		module->nm.status = MODULE_STATUS_STOPPED;
	for (auto it = module->waitingThreads.begin(), end = module->waitingThreads.end(); it < end; ++it) {
		// Still waiting?
		if (HLEKernel::VerifyWait(it->threadID, WAITTYPE_MODULE, leftModuleID))
		{
			if (module->nm.status == MODULE_STATUS_UNLOADING) {
				// TODO: Maybe should maintain the exitCode?
				sceKernelDeleteThread(it->threadID);
			} else {
				if (it->statusPtr != 0)
					Memory::Write_U32(exitStatus, it->statusPtr);
				__KernelResumeThreadFromWait(it->threadID, 0);
			}
		}
	}
	module->waitingThreads.clear();

	if (module->nm.status == MODULE_STATUS_UNLOADING) {
		// TODO: Delete the waiting thread?
		kernelObjects.Destroy<Module>(leftModuleID);
	}
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
	if (state.result == (SceUID)SCE_KERNEL_ERROR_UNKNOWN_MODULE)
		ERROR_LOG(SCEMODULE, "sceKernelGetModuleIdByAddress(%08x): module not found", moduleAddr)
	else
		DEBUG_LOG(SCEMODULE, "%x=sceKernelGetModuleIdByAddress(%08x)", state.result, moduleAddr);
	return state.result;
}

u32 sceKernelGetModuleId()
{
	INFO_LOG(SCEMODULE,"sceKernelGetModuleId()");
	return __KernelGetCurThreadModuleId();
}

u32 sceKernelFindModuleByName(const char *name)
{
	ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelFindModuleByName(%s)", name);
	
	int index = GetModuleIndex(name);

	if (index == -1)
		return 0;
	
	return 1;
}

u32 sceKernelLoadModuleByID(u32 id, u32 flags, u32 lmoptionPtr)
{
	u32 error;
	u32 handle = __IoGetFileHandleFromId(id, error);
	if (handle == (u32)-1) {
		ERROR_LOG(SCEMODULE,"sceKernelLoadModuleByID(%08x, %08x, %08x): could not open file id",id,flags,lmoptionPtr);
		return error;
	}
	if (flags != 0) {
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModuleByID: unsupported flags: %08x", flags);
	}
	SceKernelLMOption *lmoption = 0;
	if (lmoptionPtr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(lmoptionPtr);
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModuleByID: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
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
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModuleByID(%d,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),id,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModuleByID(%d,flag=%08x,(...))", module->GetUID(), id, flags);
	}

	return module->GetUID();
}

u32 sceKernelLoadModuleDNAS(const char *name, u32 flags)
{
	ERROR_LOG_REPORT(SCEMODULE, "UNIMPL 0=sceKernelLoadModuleDNAS()");
	return 0;
}

SceUID sceKernelLoadModuleBufferUsbWlan(u32 size, u32 bufPtr, u32 flags, u32 lmoptionPtr)
{
	if (flags != 0) {
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModuleBufferUsbWlan: unsupported flags: %08x", flags);
	}
	SceKernelLMOption *lmoption = 0;
	if (lmoptionPtr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(lmoptionPtr);
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModuleBufferUsbWlan: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
	}
	std::string error_string;
	Module *module = 0;
	u32 magic;
	module = __KernelLoadELFFromPtr(Memory::GetPointer(bufPtr), 0, &error_string, &magic);

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			return -1;
		}

		// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run.
		// Let's just act as if it worked.

		NOTICE_LOG(LOADER, "Module is blacklisted or undecryptable - we lie about success");
		return 1;
	}

	if (lmoption) {
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModuleBufferUsbWlan(%x,%08x,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),size,bufPtr,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(SCEMODULE,"%i=sceKernelLoadModuleBufferUsbWlan(%x,%08x,flag=%08x,(...))", module->GetUID(), size,bufPtr, flags);
	}

	return module->GetUID();
}

u32 sceKernelQueryModuleInfo(u32 uid, u32 infoAddr)
{
	INFO_LOG(SCEMODULE, "sceKernelQueryModuleInfo(%i, %08x)", uid, infoAddr);
	u32 error;
	Module *module = kernelObjects.Get<Module>(uid, error);
	if (!module)
		return error;
	if (!Memory::IsValidAddress(infoAddr)) {
		ERROR_LOG(SCEMODULE, "sceKernelQueryModuleInfo(%i, %08x) - bad infoAddr", uid, infoAddr);
		return -1;
	}

	auto info = PSPPointer<ModuleInfo>::Create(infoAddr);

	memcpy(info->segmentaddr, module->nm.segmentaddr, sizeof(info->segmentaddr));
	memcpy(info->segmentsize, module->nm.segmentsize, sizeof(info->segmentsize));
	info->nsegment = module->nm.nsegment;
	info->entry_addr = module->nm.entry_addr;
	info->gp_value = module->nm.gp_value;
	info->text_addr = module->nm.text_addr;
	info->text_size = module->nm.text_size;
	info->data_size = module->nm.data_size;
	info->bss_size = module->nm.bss_size;

	// Even if it's bigger, if it's not exactly 96, skip this extra data.
	// Even if it's 0, the above are all written though.
	if (info->size == 96) {
		info->attribute = module->nm.attribute;
		info->version[0] = module->nm.version[0];
		info->version[1] = module->nm.version[1];
		memcpy(info->name, module->nm.name, 28);
	}

	return 0;
}

u32 sceKernelGetModuleIdList(u32 resultBuffer, u32 resultBufferSize, u32 idCountAddr)
{
	ERROR_LOG(SCEMODULE, "UNTESTED sceKernelGetModuleIdList(%08x, %i, %08x)", resultBuffer, resultBufferSize, idCountAddr);
	
	int idCount = 0;
	u32 resultBufferOffset = 0;

	u32 error;
	for (auto mod = loadedModules.begin(), modend = loadedModules.end(); mod != modend; ++mod) {		
		Module *module = kernelObjects.Get<Module>(*mod, error);
		if (!module->isFake) {
			if (resultBufferOffset < resultBufferSize) {
				Memory::Write_U32(module->GetUID(), resultBuffer + resultBufferOffset);
				resultBufferOffset += 4;
			}
			idCount++;
		}
	}

	Memory::Write_U32(idCount, idCountAddr);
	
	return 0;
}

u32 ModuleMgrForKernel_977de386(const char *name, u32 flags, u32 optionAddr)
{
	WARN_LOG(SCEMODULE,"ModuleMgrForKernel_977de386:Not support this patcher");
	return sceKernelLoadModule(name, flags, optionAddr);
}

void ModuleMgrForKernel_50f0c1ec(u32 moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	WARN_LOG(SCEMODULE,"ModuleMgrForKernel_50f0c1ec:Not support this patcher");
	sceKernelStartModule(moduleId, argsize, argAddr, returnValueAddr, optionAddr);
}

//fix for tiger x dragon
u32 ModuleMgrForKernel_a1a78c58(const char *name, u32 flags, u32 optionAddr)
{
	WARN_LOG(SCEMODULE,"ModuleMgrForKernel_a1a78c58:Not support this patcher");
	return sceKernelLoadModule(name, flags, optionAddr);
}

const HLEFunction ModuleMgrForUser[] = 
{
	{0x977DE386,&WrapU_CUU<sceKernelLoadModule>,"sceKernelLoadModule"},
	{0xb7f46618,&WrapU_UUU<sceKernelLoadModuleByID>,"sceKernelLoadModuleByID"},
	{0x50F0C1EC,&WrapV_UUUUU<sceKernelStartModule>,"sceKernelStartModule", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xD675EBB8,&sceKernelExitGame,"sceKernelSelfStopUnloadModule"}, //HACK
	{0xd1ff982a,&WrapU_UUUUU<sceKernelStopModule>,"sceKernelStopModule", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x2e0911aa,WrapU_U<sceKernelUnloadModule>,"sceKernelUnloadModule"},
	{0x710F61B5,0,"sceKernelLoadModuleMs"},
	{0xF9275D98,WrapI_UUUU<sceKernelLoadModuleBufferUsbWlan>,"sceKernelLoadModuleBufferUsbWlan"}, ///???
	{0xCC1D3699,0,"sceKernelStopUnloadSelfModule"},
	{0x748CBED9,WrapU_UU<sceKernelQueryModuleInfo>,"sceKernelQueryModuleInfo"},
	{0xd8b73127,&WrapU_U<sceKernelGetModuleIdByAddress>, "sceKernelGetModuleIdByAddress"},
	{0xf0a26395,WrapU_V<sceKernelGetModuleId>, "sceKernelGetModuleId"},
	{0x8f2df740,WrapU_UUUUU<sceKernelStopUnloadSelfModuleWithStatus>,"sceKernelStopUnloadSelfModuleWithStatus"},
	{0xfef27dc1,&WrapU_CU<sceKernelLoadModuleDNAS> , "sceKernelLoadModuleDNAS"},
	{0x644395e2,WrapU_UUU<sceKernelGetModuleIdList>,"sceKernelGetModuleIdList"},
	{0xf2d8d1b4,&WrapU_CUU<sceKernelLoadModuleNpDrm>,"sceKernelLoadModuleNpDrm"},
	{0xe4c4211c,0,"ModuleMgrForUser_E4C4211C"},
	{0xfbe27467,0,"ModuleMgrForUser_FBE27467"},
};


const HLEFunction ModuleMgrForKernel[] =
{
	{0x50f0c1ec,&WrapV_UUUUU<ModuleMgrForKernel_50f0c1ec>, "ModuleMgrForKernel_50f0c1ec"},//Not sure right
	{0x977de386, &WrapU_CUU<ModuleMgrForKernel_977de386>, "ModuleMgrForKernel_977de386"},//Not sure right
	{0xa1a78c58, &WrapU_CUU<ModuleMgrForKernel_a1a78c58>, "ModuleMgrForKernel_a1a78c58"}, //fix for tiger x dragon
	{0x748CBED9, WrapU_UU<sceKernelQueryModuleInfo>, "sceKernelQueryModuleInfo" },//Bugz Homebrew
	{0x644395e2, WrapU_UUU<sceKernelGetModuleIdList>, "sceKernelGetModuleIdList" },//Bugz Homebrew
};

void Register_ModuleMgrForUser()
{
	RegisterModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}

void Register_ModuleMgrForKernel()
{
	RegisterModule("ModuleMgrForKernel", ARRAY_SIZE(ModuleMgrForKernel), ModuleMgrForKernel);		

};
