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

#include "base/stringutil.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/Reporting.h"
#include "Core/Host.h"
#include "Core/Loaders.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/ELF/ElfReader.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/CoreTiming.h"
#include "Core/PSPLoaders.h"
#include "Core/System.h"
#include "Core/MemMapHelpers.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPS.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/scePsmf.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/Debugger/Record.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
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
		return nid == other.nid && !strncmp(moduleName, other.moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
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
		return nid == other.nid && !strncmp(moduleName, other.moduleName, KERNELOBJECT_MAX_NAME_LENGTH);
	}

	char moduleName[KERNELOBJECT_MAX_NAME_LENGTH + 1];
	u32 symAddr;
	u32 nid;
};

void ImportVarSymbol(const VarSymbolImport &var);
void ExportVarSymbol(const VarSymbolExport &var);
void UnexportVarSymbol(const VarSymbolExport &var);

void ImportFuncSymbol(const FuncSymbolImport &func, bool reimporting);
void ExportFuncSymbol(const FuncSymbolExport &func);
void UnexportFuncSymbol(const FuncSymbolExport &func);

class Module;
static bool KernelImportModuleFuncs(Module *module, u32 *firstImportStubAddr, bool reimporting = false);

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
	Module() : textStart(0), textEnd(0), libstub(0), libstubend(0), memoryBlockAddr(0), isFake(false) {}
	~Module() {
		if (memoryBlockAddr) {
			// If it's either below user memory, or using a high kernel bit, it's in kernel.
			if (memoryBlockAddr < PSP_GetUserMemoryBase() || memoryBlockAddr > PSP_GetUserMemoryEnd()) {
				kernelMemory.Free(memoryBlockAddr);
			} else {
				userMemory.Free(memoryBlockAddr);
			}
			g_symbolMap->UnloadModule(memoryBlockAddr, memoryBlockSize);
		}
	}
	const char *GetName() override { return nm.name; }
	const char *GetTypeName() override { return "Module"; }
	void GetQuickInfo(char *ptr, int size) override
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
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_Module; }

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("Module", 1, 4);
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
		if (s >= 4) {
			p.Do(libstub);
			p.Do(libstubend);
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

		if (p.mode == p.MODE_READ) {
			// On load state, we re-examine in case our syscall ids changed.
			if (libstub != 0) {
				importedFuncs.clear();
				// Imports reloaded in KernelModuleDoState.
			} else {
				// Older save state.  Let's still reload, but this may not pick up new flags, etc.
				bool foundBroken = false;
				auto importedFuncsState = importedFuncs;
				importedFuncs.clear();
				for (auto func : importedFuncsState) {
					if (func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] != '\0' || !Memory::IsValidAddress(func.stubAddr)) {
						foundBroken = true;
					} else {
						ImportFunc(func, true);
					}
				}

				if (foundBroken) {
					ERROR_LOG(LOADER, "Broken stub import data while loading state");
				}
			}

			char moduleName[29] = {0};
			truncate_cpy(moduleName, nm.name);
			if (memoryBlockAddr != 0) {
				g_symbolMap->AddModule(moduleName, memoryBlockAddr, memoryBlockSize);
			}
		}

		RebuildImpExpModuleNames();
	}

	// We don't do this in the destructor to avoid annoying messages on game shutdown.
	void Cleanup();

	void ImportFunc(const FuncSymbolImport &func, bool reimporting) {
		if (!Memory::IsValidAddress(func.stubAddr)) {
			WARN_LOG_REPORT(LOADER, "Invalid address for syscall stub %s %08x", func.moduleName, func.nid);
			return;
		}

		DEBUG_LOG(LOADER, "Importing %s : %08x", GetFuncName(func.moduleName, func.nid), func.stubAddr);

		// Add the symbol to the symbol map for debugging.
		char temp[256];
		sprintf(temp,"zz_%s", GetFuncName(func.moduleName, func.nid));
		g_symbolMap->AddFunction(temp,func.stubAddr,8);

		// Keep track and actually hook it up if possible.
		importedFuncs.push_back(func);
		impExpModuleNames.insert(func.moduleName);
		ImportFuncSymbol(func, reimporting);
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

	// Keep track of the libstub pointers so we can recheck on load state.
	u32 libstub;
	u32 libstubend;

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
	void run(MipsCall &call) override;
	void DoState(PointerWrap &p) override {
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

static void __KernelModuleInit()
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

	if (p.mode == p.MODE_READ) {
		u32 error;
		// We process these late, since they depend on loadedModules for interlinking.
		for (SceUID moduleId : loadedModules) {
			Module *module = kernelObjects.Get<Module>(moduleId, error);
			if (module && module->libstub != 0) {
				if (!KernelImportModuleFuncs(module, nullptr, true)) {
					ERROR_LOG(LOADER, "Something went wrong loading imports on load state");
				}
			}
		}
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
static void WriteVarSymbol(u32 exportAddress, u32 relocAddress, u8 type, bool reverse = false)
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
			const u32 offsetLo = (s32)(s16)(u16)(relocData & 0xFFFF);
			u32 full = exportAddress;
			// This is only used in the error case (no hi/wrong hi.)
			if (!reverse) {
				full = offsetLo + exportAddress;
			} else {
				full = offsetLo - exportAddress;
			}

			// The ABI requires that these come in pairs, at least.
			if (lastHI16Relocs.empty()) {
				ERROR_LOG_REPORT(LOADER, "LO16 without any HI16 variable import at %08x for %08x", relocAddress, exportAddress);
			// Try to process at least the low relocation...
			} else if (lastHI16ExportAddress != exportAddress) {
				ERROR_LOG_REPORT(LOADER, "HI16 and LO16 imports do not match at %08x for %08x (should be %08x)", relocAddress, lastHI16ExportAddress, exportAddress);
			} else {
				// Process each of the HI16.  Usually there's only one.
				for (auto it = lastHI16Relocs.begin(), end = lastHI16Relocs.end(); it != end; ++it)
				{
					if (!reverse) {
						full = (it->data << 16) + offsetLo + exportAddress;
					} else {
						full = (it->data << 16) + offsetLo - exportAddress;
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
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
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
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
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
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
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

void ImportFuncSymbol(const FuncSymbolImport &func, bool reimporting) {
	// Prioritize HLE implementations.
	// TODO: Or not?
	if (FuncImportIsSyscall(func.moduleName, func.nid)) {
		if (reimporting && Memory::Read_Instruction(func.stubAddr + 4) != GetSyscallOp(func.moduleName, func.nid)) {
			WARN_LOG(LOADER, "Reimporting updated syscall %s", GetFuncName(func.moduleName, func.nid));
		}
		WriteSyscall(func.moduleName, func.nid, func.stubAddr);
		currentMIPS->InvalidateICache(func.stubAddr, 8);
		MIPSAnalyst::PrecompileFunction(func.stubAddr, 8);
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for exports currently loaded modules already have.  Maybe it's available?
		for (auto it = module->exportedFuncs.begin(), end = module->exportedFuncs.end(); it != end; ++it) {
			if (it->Matches(func)) {
				if (reimporting && Memory::Read_Instruction(func.stubAddr) != MIPS_MAKE_J(it->symAddr)) {
					WARN_LOG_REPORT(LOADER, "Reimporting: func import %s/%08x changed", func.moduleName, func.nid);
				}
				WriteFuncStub(func.stubAddr, it->symAddr);
				currentMIPS->InvalidateICache(func.stubAddr, 8);
				MIPSAnalyst::PrecompileFunction(func.stubAddr, 8);
				return;
			}
		}
	}

	// It hasn't been exported yet, but hopefully it will later.
	bool isKnownModule = GetModuleIndex(func.moduleName) != -1;
	if (isKnownModule) {
		WARN_LOG_REPORT(LOADER, "Unknown syscall in known module '%s': 0x%08x", func.moduleName, func.nid);
	} else {
		INFO_LOG(LOADER, "Function (%s,%08x) unresolved, storing for later resolving", func.moduleName, func.nid);
	}
	if (isKnownModule || !reimporting) {
		WriteFuncMissingStub(func.stubAddr, func.nid);
		currentMIPS->InvalidateICache(func.stubAddr, 8);
	}
}

void ExportFuncSymbol(const FuncSymbolExport &func) {
	if (FuncImportIsSyscall(func.moduleName, func.nid)) {
		// Oops, HLE covers this.
		WARN_LOG_REPORT(LOADER, "Ignoring func export %s/%08x, already implemented in HLE.", func.moduleName, func.nid);
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for imports currently loaded modules already have, hook it up right away.
		for (auto it = module->importedFuncs.begin(), end = module->importedFuncs.end(); it != end; ++it) {
			if (func.Matches(*it)) {
				INFO_LOG(LOADER, "Resolving function %s/%08x", func.moduleName, func.nid);
				WriteFuncStub(it->stubAddr, func.symAddr);
				currentMIPS->InvalidateICache(it->stubAddr, 8);
				MIPSAnalyst::PrecompileFunction(it->stubAddr, 8);
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
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
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

	if (memoryBlockAddr != 0 && nm.text_addr != 0 && memoryBlockSize >= nm.data_size + nm.bss_size + nm.text_size) {
		DEBUG_LOG(LOADER, "Zeroing out module %s memory: %08x - %08x", nm.name, memoryBlockAddr, memoryBlockAddr + memoryBlockSize);
		for (u32 i = 0; i < (u32)(nm.text_size + 3); i += 4) {
			Memory::Write_U32(MIPS_MAKE_BREAK(1), nm.text_addr + i);
		}
		Memory::Memset(nm.text_addr + nm.text_size, -1, nm.data_size + nm.bss_size);

		// Let's also invalidate, just to make sure it's cleared out for any future data.
		currentMIPS->InvalidateICache(memoryBlockAddr, memoryBlockSize);
	}
}

static void __SaveDecryptedEbootToStorageMedia(const u8 *decryptedEbootDataPtr, const u32 length) {
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

	FILE *decryptedEbootFile = File::OpenCFile(fullPath, "wb");
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

static bool KernelImportModuleFuncs(Module *module, u32 *firstImportStubAddr, bool reimporting) {
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

	// Can't run - we didn't keep track of the libstub entry.
	if (module->libstub == 0) {
		return false;
	}
	if (!Memory::IsValidRange(module->libstub, module->libstubend - module->libstub)) {
		ERROR_LOG_REPORT(LOADER, "Garbage libstub address or end");
		return false;
	}

	u32_le *entryPos = (u32_le *)Memory::GetPointerUnchecked(module->libstub);
	u32_le *entryEnd = (u32_le *)Memory::GetPointerUnchecked(module->libstubend);

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
				module->ImportFunc(func, reimporting);
			}

			if (firstImportStubAddr && (!*firstImportStubAddr || *firstImportStubAddr > (u32)entry->firstSymAddr))
				*firstImportStubAddr = entry->firstSymAddr;
		} else if (entry->numFuncs > 0) {
			WARN_LOG_REPORT(LOADER, "Module entry with %d imports but no valid address", entry->numFuncs);
			needReport = true;
		}

		// We skip vars when reimporting, since we might double-offset.
		// We only reimport funcs, which can't be double-offset.
		if (entry->varData != 0 && !reimporting) {
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
		} else if (entry->numVars > 0 && !reimporting) {
			WARN_LOG_REPORT(LOADER, "Module entry with %d var imports but no valid address", entry->numVars);
			needReport = true;
		}

		DEBUG_LOG(LOADER, "-------------------------------------------------------------");
	}

	if (needReport) {
		std::string debugInfo;
		entryPos = (u32_le *)Memory::GetPointer(module->libstub);
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

	return true;
}

static Module *__KernelLoadELFFromPtr(const u8 *ptr, size_t elfSize, u32 loadAddress, bool fromTop, std::string *error_string, u32 *magic, u32 &error) {
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
		u32 headerSize = *(u32_le*)(ptr + 4);
		ptr += headerSize;
		elfSize -= headerSize;
		magicPtr = (u32_le *)ptr;
	}
	*magic = *magicPtr;
	if (*magic == 0x5053507e) { // "~PSP"
		DEBUG_LOG(SCEMODULE, "Decrypting ~PSP file");
		PSP_Header *head = (PSP_Header*)ptr;
		devkitVersion = head->devkitversion;

		if (IsHLEVersionedModule(head->modname)) {
			int ver = (head->module_ver_hi << 8) | head->module_ver_lo;
			INFO_LOG(SCEMODULE, "Loading module %s with version %04x, devkit %08x", head->modname, ver, head->devkitversion);
			reportedModule = true;

			if (!strcmp(head->modname, "sceMpeg_library")) {
				__MpegLoadModule(ver);
			}
			if (!strcmp(head->modname, "scePsmfP_library") || !strcmp(head->modname, "scePsmfPlayer")) {
				__PsmfPlayerLoadModule(head->devkitversion);
			}
		}

		const u8 *in = ptr;
		// Kind of odd.
		u32 size = head->psp_size;
		if (size > elfSize) {
			*error_string = StringFromFormat("ELF/PRX truncated: %d > %d", (int)size, (int)elfSize);
			module->Cleanup();
			kernelObjects.Destroy<Module>(module->GetUID());
			return nullptr;
		}
		newptr = new u8[std::max(head->elf_size, head->psp_size)];
		ptr = newptr;
		magicPtr = (u32_le *)ptr;
		int ret = pspDecryptPRX(in, (u8*)ptr, head->psp_size);
		if (ret == MISSING_KEY) {
			// This should happen for all "kernel" modules.
			*error_string = "Missing key";
			delete [] newptr;
			module->isFake = true;
			strncpy(module->nm.name, head->modname, ARRAY_SIZE(module->nm.name));
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;

			// Let's still try to allocate it.  It may use user memory.
			u32 totalSize = 0;
			for (int i = 0; i < 4; ++i) {
				if (head->seg_size[i]) {
					const u32 align = head->seg_align[i] - 1;
					totalSize = ((totalSize + align) & ~align) + head->seg_size[i];
				}
			}
			bool kernelModule = (head->attribute & 0x1000) != 0;
			BlockAllocator &memblock = kernelModule ? kernelMemory : userMemory;
			size_t n = strnlen(head->modname, 28);
			const std::string modName = "ELF/" + std::string(head->modname, n);
			u32 addr = memblock.Alloc(totalSize, fromTop, modName.c_str());
			if (addr == (u32)-1) {
				error = SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
				module->Cleanup();
				kernelObjects.Destroy<Module>(module->GetUID());
			} else {
				error = 0;
				module->memoryBlockAddr = addr;
				module->memoryBlockSize = totalSize;
			}

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
				INFO_LOG(SCEMODULE, "Dumping decrypted EBOOT.BIN to file.");
				const u32 dumpLength = ret;
				__SaveDecryptedEbootToStorageMedia(ptr, dumpLength);
			}
		}
	}

	// DO NOT change to else if, see above.
	if (*magicPtr != 0x464c457f) {
		ERROR_LOG(SCEMODULE, "Wrong magic number %08x", *magicPtr);
		*error_string = "File corrupt";
		if (newptr)
			delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<Module>(module->GetUID());
		error = SCE_KERNEL_ERROR_UNSUPPORTED_PRX_TYPE;
		return nullptr;
	}

	// Open ELF reader
	ElfReader reader((void*)ptr, elfSize);

	int result = reader.LoadInto(loadAddress, fromTop);
	if (result != SCE_KERNEL_ERROR_OK) 	{
		ERROR_LOG(SCEMODULE, "LoadInto failed with error %08x",result);
		if (newptr)
			delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<Module>(module->GetUID());
		error = result;
		return nullptr;
	}
	module->memoryBlockAddr = reader.GetVaddr();
	module->memoryBlockSize = reader.GetTotalSize();

	currentMIPS->InvalidateICache(module->memoryBlockAddr, module->memoryBlockSize);

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
		if (i < (int)ARRAY_SIZE(module->nm.segmentaddr)) {
			module->nm.segmentsize[i] = reader.GetSegmentMemSize(i);
			if (module->nm.segmentsize[i] != 0) {
				module->nm.segmentaddr[i] = reader.GetSegmentVaddr(i);
			} else {
				module->nm.segmentaddr[i] = 0;
			}
		}
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

	if (!module->isFake && module->memoryBlockAddr != 0) {
		g_symbolMap->AddModule(moduleName, module->memoryBlockAddr, module->memoryBlockSize);
	}

	SectionID textSection = reader.GetSectionByName(".text");

	if (textSection != -1) {
		module->textStart = reader.GetSectionAddr(textSection);
		u32 textSize = reader.GetSectionSize(textSection);
		module->textEnd = module->textStart + textSize - 4;

		module->nm.text_addr = module->textStart;
		module->nm.text_size = reader.GetTotalTextSize();
	} else {
		module->nm.text_addr = 0;
		module->nm.text_size = 0;
	}

	module->nm.bss_size = reader.GetTotalSectionSizeByPrefix(".bss");
	module->nm.data_size = reader.GetTotalDataSize() - module->nm.bss_size;
	module->libstub = modinfo->libstub;
	module->libstubend = modinfo->libstubend;

	INFO_LOG(LOADER, "Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent, modinfo->libstub);
	DEBUG_LOG(LOADER,"===================================================");

	u32 firstImportStubAddr = 0;
	KernelImportModuleFuncs(module, &firstImportStubAddr);

	if (textSection == -1) {
		module->textStart = reader.GetVaddr();
		module->textEnd = firstImportStubAddr - 4;
	}

	if (!module->isFake) {
		bool scan = true;
#if defined(MOBILE_DEVICE)
		scan = g_Config.bFuncReplacements;
#endif

		// If the ELF has debug symbols, don't add entries to the symbol table.
		bool insertSymbols = scan && !reader.LoadSymbols();
		std::vector<SectionID> codeSections = reader.GetCodeSections();
		for (SectionID id : codeSections) {
			u32 start = reader.GetSectionAddr(id);
			// Note: scan end is inclusive.
			u32 end = start + reader.GetSectionSize(id) - 4;

			if (start < module->textStart)
				module->textStart = start;
			if (end > module->textEnd)
				module->textEnd = end;

			if (scan) {
				insertSymbols = MIPSAnalyst::ScanForFunctions(start, end, insertSymbols);
			}
		}

		// Some games don't have any sections at all.
		if (scan && codeSections.empty()) {
			u32 scanStart = module->textStart;
			u32 scanEnd = module->textEnd;
			// Skip the exports and imports sections, they're not code.
			if (scanEnd >= std::min(modinfo->libent, modinfo->libstub)) {
				insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, std::min(modinfo->libent, modinfo->libstub) - 4, insertSymbols);
				scanStart = std::min(modinfo->libentend, modinfo->libstubend);
			}
			if (scanEnd >= std::max(modinfo->libent, modinfo->libstub)) {
				insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, std::max(modinfo->libent, modinfo->libstub) - 4, insertSymbols);
				scanStart = std::max(modinfo->libentend, modinfo->libstubend);
			}
			insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, scanEnd, insertSymbols);
		}

		if (scan) {
			MIPSAnalyst::FinalizeScan(insertSymbols);
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

	const u32_le *entPos = (u32_le *)Memory::GetPointer(modinfo->libent);
	const u32_le *entEnd = (u32_le *)Memory::GetPointer(modinfo->libentend);
	for (int m = 0; entPos < entEnd; ++m) {
		const PspLibEntEntry *ent = (const PspLibEntEntry *)entPos;
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
				if (ent->name == 0) {
					WARN_LOG_REPORT(HLE, "Exporting func from syslib export: %08x", nid);
				}
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
				if (ent->name == 0) {
					WARN_LOG_REPORT(HLE, "Exporting var from syslib export: %08x", nid);
				}
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

		MIPSAnalyst::PrecompileFunctions();

	} else {
		module->nm.entry_addr = -1;
	}

	if (newptr)
		delete [] newptr;

	if (!reportedModule && IsHLEVersionedModule(modinfo->name)) {
		INFO_LOG(SCEMODULE, "Loading module %s with version %04x, devkit %08x", modinfo->name, modinfo->moduleVersion, devkitVersion);

		if (!strcmp(modinfo->name, "sceMpeg_library")) {
			__MpegLoadModule(modinfo->moduleVersion);
		}
		if (!strcmp(modinfo->name, "scePsmfP_library") || !strcmp(modinfo->name, "scePsmfPlayer")) {
			__PsmfPlayerLoadModule(devkitVersion);
		}
	}

	error = 0;
	return module;
}

static Module *__KernelLoadModule(u8 *fileptr, size_t fileSize, SceKernelLMOption *options, std::string *error_string) {
	Module *module = 0;
	// Check for PBP
	if (memcmp(fileptr, "\0PBP", 4) == 0) {
		// PBP!
		u32_le version;
		memcpy(&version, fileptr + 4, 4);
		u32_le offset0, offsets[16];

		memcpy(&offset0, fileptr + 8, 4);
		int numfiles = (offset0 - 8)/4;
		offsets[0] = offset0;
		for (int i = 1; i < numfiles; i++)
			memcpy(&offsets[i], fileptr + 12 + 4*i, 4);

		if (offsets[6] > fileSize) {
			// File is too small to fully contain the ELF! Must have been truncated.
			*error_string = "ELF file truncated - can't load";
			return nullptr;
		}

		u32 magic = 0;
		u8 *temp = nullptr;
		size_t elfSize = offsets[6] - offsets[5];
		if (offsets[5] & 3) {
			// Our loader does NOT like to load from an unaligned address on ARM! Copy to a new block.
			temp = new u8[elfSize];

			memcpy(temp, fileptr + offsets[5], elfSize);
			INFO_LOG(LOADER, "PBP: ELF unaligned (%d: %d), aligning!", offsets[5], offsets[5] & 3);
		}

		u32 error;
		module = __KernelLoadELFFromPtr(temp ? temp : fileptr + offsets[5], elfSize, PSP_GetDefaultLoadAddress(), false, error_string, &magic, error);

		if (temp) {
			delete [] temp;
		}
	} else {
		u32 error;
		u32 magic = 0;
		module = __KernelLoadELFFromPtr(fileptr, fileSize, PSP_GetDefaultLoadAddress(), false, error_string, &magic, error);
	}

	return module;
}

static void __KernelStartModule(Module *m, int args, const char *argp, SceKernelSMOption *options)
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

void __KernelLoadReset() {
	// Wipe kernel here, loadexec should reset the entire system
	if (__KernelIsRunning()) {
		u32 error;
		while (!loadedModules.empty()) {
			SceUID moduleID = *loadedModules.begin();
			Module *module = kernelObjects.Get<Module>(moduleID, error);
			if (module) {
				module->Cleanup();
			} else {
				// An invalid module.  We need to remove it or we'll loop forever.
				WARN_LOG(LOADER, "Invalid module still marked as loaded on loadexec");
				loadedModules.erase(moduleID);
			}
		}

		Replacement_Shutdown();
		__KernelShutdown();
		// HLE needs to be reset here
		HLEShutdown();
		Replacement_Init();
		HLEInit();
		assert(gpu);
		gpu->Reinitialize();
	}

	__KernelModuleInit();
	__KernelInit();
}

bool __KernelLoadExec(const char *filename, u32 paramPtr, std::string *error_string) {
	SceKernelLoadExecParam param;

	PSP_SetLoading("Loading game...");

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

	__KernelLoadReset();

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

	PSP_SetLoading("Loading modules...");
	Module *module = __KernelLoadModule(temp, (size_t)info.size, 0, error_string);

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

	PSP_SetLoading("Starting modules...");
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

bool __KernelLoadGEDump(const std::string &base_filename, std::string *error_string) {
	__KernelLoadReset();
	PSP_SetLoading("Generating code...");

	mipsr4k.pc = PSP_GetUserMemoryBase();

	const static u32_le runDumpCode[] = {
		// Save the filename.
		MIPS_MAKE_ORI(MIPS_REG_S0, MIPS_REG_A0, 0),
		MIPS_MAKE_ORI(MIPS_REG_S1, MIPS_REG_A1, 0),
		// Call the actual render.
		MIPS_MAKE_SYSCALL("FakeSysCalls", "__KernelGPUReplay"),
		// Make sure we don't get out of sync.
		MIPS_MAKE_LUI(MIPS_REG_A0, 0),
		MIPS_MAKE_SYSCALL("sceGe_user", "sceGeDrawSync"),
		// Set the return address after the entry which saved the filename.
		MIPS_MAKE_LUI(MIPS_REG_RA, mipsr4k.pc >> 16),
		MIPS_MAKE_ADDIU(MIPS_REG_RA, MIPS_REG_RA, 8),
		// Wait for the next vblank to render again.
		MIPS_MAKE_JR_RA(),
		MIPS_MAKE_SYSCALL("sceDisplay", "sceDisplayWaitVblankStart"),
		// This never gets reached, just here to be safe.
		MIPS_MAKE_BREAK(0),
	};

	for (size_t i = 0; i < ARRAY_SIZE(runDumpCode); ++i) {
		Memory::WriteUnchecked_U32(runDumpCode[i], mipsr4k.pc + (int)i * sizeof(u32_le));
	}

	Module *module = new Module;
	kernelObjects.Create(module);
	loadedModules.insert(module->GetUID());
	memset(&module->nm, 0, sizeof(module->nm));
	module->isFake = true;
	module->nm.entry_addr = mipsr4k.pc;
	module->nm.gp_value = -1;

	SceUID threadID = __KernelSetupRootThread(module->GetUID(), (int)base_filename.size(), base_filename.data(), 0x20, 0x1000, 0);
	__KernelSetThreadRA(threadID, NID_MODULERETURN);

	__KernelStartIdleThreads(module->GetUID());
	return true;
}

void __KernelGPUReplay() {
	// Special ABI: s0 and s1 are the "args".  Not null terminated.
	const char *filenamep = Memory::GetCharPointer(currentMIPS->r[MIPS_REG_S1]);
	if (!filenamep) {
		ERROR_LOG(SYSTEM, "Failed to load dump filename");
		Core_Stop();
		return;
	}

	std::string filename(filenamep, currentMIPS->r[MIPS_REG_S0]);
	if (!GPURecord::RunMountedReplay(filename)) {
		Core_Stop();
	}
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

u32 sceKernelLoadModule(const char *name, u32 flags, u32 optionAddr) {
	if (!name) {
		return hleLogError(LOADER, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "bad filename");
	}

	for (size_t i = 0; i < ARRAY_SIZE(lieAboutSuccessModules); i++) {
		if (!strcmp(name, lieAboutSuccessModules[i])) {
			Module *module = new Module;
			kernelObjects.Create(module);
			loadedModules.insert(module->GetUID());
			memset(&module->nm, 0, sizeof(module->nm));
			module->isFake = true;
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;

			// TODO: It would be more ideal to allocate memory for this module.

			return hleLogSuccessInfoI(LOADER, module->GetUID(), "created fake module");
		}
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(name);
	s64 size = (s64)info.size;

	if (!info.exists) {
		const u32 error = hleLogError(LOADER, SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "file does not exist");
		return hleDelayResult(error, "module loaded", 500);
	}

	if (!size) {
		const u32 error = hleLogError(LOADER, SCE_KERNEL_ERROR_FILEERR, "module file size is 0");
		return hleDelayResult(error, "module loaded", 500);
	}

	// We log before hand because ELF loading logs a bunch.
	DEBUG_LOG(LOADER, "sceKernelLoadModule(%s, %08x)", name, flags);

	if (flags != 0) {
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModule: unsupported flags: %08x", flags);
	}
	SceKernelLMOption *lmoption = 0;
	if (optionAddr) {
		lmoption = (SceKernelLMOption *)Memory::GetPointer(optionAddr);
		if (lmoption->position < PSP_SMEM_Low || lmoption->position > PSP_SMEM_HighAligned) {
			ERROR_LOG_REPORT(LOADER, "sceKernelLoadModule(%s): invalid position (%i)", name, (int)lmoption->position);
			return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE, "module loaded", 500);
		}
		if (lmoption->position == PSP_SMEM_LowAligned || lmoption->position == PSP_SMEM_HighAligned) {
			ERROR_LOG_REPORT(LOADER, "sceKernelLoadModule(%s): invalid position (aligned)", name);
			return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE, "module loaded", 500);
		}
		if (lmoption->position == PSP_SMEM_Addr) {
			ERROR_LOG_REPORT(LOADER, "sceKernelLoadModule(%s): invalid position (fixed)", name);
			return hleDelayResult(SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED, "module loaded", 500);
		}
		WARN_LOG_REPORT(LOADER, "sceKernelLoadModule: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
	}

	Module *module = 0;
	u8 *temp = new u8[(int)size];
	u32 handle = pspFileSystem.OpenFile(name, FILEACCESS_READ);
	pspFileSystem.ReadFile(handle, temp, (size_t)size);
	u32 magic;
	u32 error;
	std::string error_string;
	module = __KernelLoadELFFromPtr(temp, (size_t)size, 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, error);
	delete [] temp;
	pspFileSystem.CloseFile(handle);

	if (!module) {
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			// TODO: What's actually going on here?
			error = -1;
			return hleDelayResult(error, "module loaded", 500);
		}

		if (info.name == "BOOT.BIN") {
			NOTICE_LOG_REPORT(LOADER, "Module %s is blacklisted or undecryptable - we try __KernelLoadExec", name);
			// Name might get deleted.
			const std::string safeName = name;
			return __KernelLoadExec(safeName.c_str(), 0, &error_string);
		} else {
			hleLogError(LOADER, error, "failed to load");
			return hleDelayResult(error, "module loaded", 500);
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

static u32 sceKernelLoadModuleNpDrm(const char *name, u32 flags, u32 optionAddr)
{
	DEBUG_LOG(LOADER, "sceKernelLoadModuleNpDrm(%s, %08x)", name, flags);

	return sceKernelLoadModule(name, flags, optionAddr);
}

static void sceKernelStartModule(u32 moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
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
		INFO_LOG(SCEMODULE, "sceKernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x,%08x): error %08x", moduleId, argsize, argAddr, returnValueAddr, optionAddr, error);
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
			__KernelStartThreadValidate(threadID, argsize, argAddr);
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

static u32 sceKernelStopModule(u32 moduleId, u32 argSize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
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
		__KernelStartThreadValidate(threadID, argSize, argAddr);
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

static u32 sceKernelUnloadModule(u32 moduleId)
{
	INFO_LOG(SCEMODULE,"sceKernelUnloadModule(%i)", moduleId);
	u32 error;
	Module *module = kernelObjects.Get<Module>(moduleId, error);
	if (!module)
		return hleDelayResult(error, "module unloaded", 150);

	module->Cleanup();
	kernelObjects.Destroy<Module>(moduleId);
	return hleDelayResult(moduleId, "module unloaded", 500);
}

u32 hleKernelStopUnloadSelfModuleWithOrWithoutStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr, bool WithStatus) {
	if (loadedModules.size() > 1) {
		if (WithStatus)
			ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game may have crashed", exitCode, argSize, argp, statusAddr, optionAddr);
		else
			ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelSelfStopUnloadModule(%08x, %08x, %08x): game may have crashed", exitCode, argSize,  argp);
		SceUID moduleID = __KernelGetCurThreadModuleId();
		u32 priority = 0x20;
		u32 stacksize = 0x40000;
		u32 attr = 0;
		// TODO: In a lot of cases (even for errors), this should resched.  Needs testing.

		u32 error;
		Module *module = kernelObjects.Get<Module>(moduleID, error);
		if (!module) {
			if (WithStatus)
				ERROR_LOG(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): invalid module id", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				ERROR_LOG(SCEMODULE, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): invalid module id", exitCode, argSize, argp);
			
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
			__KernelStartThreadValidate(threadID, argSize, argp);
			__KernelSetThreadRA(threadID, NID_MODULERETURN);
			__KernelWaitCurThread(WAITTYPE_MODULE, moduleID, 1, 0, false, "unloadstopped module");

			const ModuleWaitingThread mwt = {__KernelGetCurThread(), statusAddr};
			module->nm.status = MODULE_STATUS_UNLOADING;
			module->waitingThreads.push_back(mwt);
		} else if (stopFunc == 0) {
			if (WithStatus)
				INFO_LOG(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): no stop func", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				INFO_LOG(SCEMODULE, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): no stop func", exitCode, argSize, argp);
			sceKernelExitDeleteThread(exitCode);
			module->Cleanup();
			kernelObjects.Destroy<Module>(moduleID);
		} else {
			if (WithStatus)
				ERROR_LOG_REPORT(SCEMODULE, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): bad stop func address", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				ERROR_LOG_REPORT(SCEMODULE, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): bad stop func address", exitCode, argSize, argp);
			sceKernelExitDeleteThread(exitCode);
			module->Cleanup();
			kernelObjects.Destroy<Module>(moduleID);
		}
	} else {
		if (WithStatus)
			ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp, statusAddr, optionAddr);
		else
			ERROR_LOG_REPORT(SCEMODULE, "UNIMPL sceKernelSelfStopUnloadModule(%08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp);
	}

	return 0;
}

static u32 sceKernelSelfStopUnloadModule(u32 exitCode, u32 argSize, u32 argp) {
	// Used in Tom Clancy's Splinter Cell Essentials,Ghost in the Shell Stand Alone Complex
	return hleKernelStopUnloadSelfModuleWithOrWithoutStatus(exitCode, argSize, argp, 0, 0, false);
}

static u32 sceKernelStopUnloadSelfModuleWithStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr) {
	return hleKernelStopUnloadSelfModuleWithOrWithoutStatus(exitCode, argSize, argp, statusAddr, optionAddr, true);
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
				__KernelResumeThreadFromWait(it->threadID, module->nm.status == MODULE_STATUS_STARTED ? leftModuleID : 0);
			}
		}
	}
	module->waitingThreads.clear();

	if (module->nm.status == MODULE_STATUS_UNLOADING) {
		// TODO: Delete the waiting thread?
		module->Cleanup();
		kernelObjects.Destroy<Module>(leftModuleID);
	}
}

struct GetModuleIdByAddressArg
{
	u32 addr;
	SceUID result;
};

static bool __GetModuleIdByAddressIterator(Module *module, GetModuleIdByAddressArg *state)
{
	const u32 start = module->memoryBlockAddr, size = module->memoryBlockSize;
	if (start != 0 && start <= state->addr && start + size > state->addr)
	{
		state->result = module->GetUID();
		return false;
	}
	return true;
}

static u32 sceKernelGetModuleIdByAddress(u32 moduleAddr)
{
	GetModuleIdByAddressArg state;
	state.addr = moduleAddr;
	state.result = SCE_KERNEL_ERROR_UNKNOWN_MODULE;

	kernelObjects.Iterate(&__GetModuleIdByAddressIterator, &state);
	if (state.result == (SceUID)SCE_KERNEL_ERROR_UNKNOWN_MODULE)
		ERROR_LOG(SCEMODULE, "sceKernelGetModuleIdByAddress(%08x): module not found", moduleAddr);
	else
		DEBUG_LOG(SCEMODULE, "%x=sceKernelGetModuleIdByAddress(%08x)", state.result, moduleAddr);
	return state.result;
}

static u32 sceKernelGetModuleId()
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

static u32 sceKernelLoadModuleByID(u32 id, u32 flags, u32 lmoptionPtr)
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
	u8 *temp = new u8[size - pos];
	pspFileSystem.ReadFile(handle, temp, size - pos);
	u32 magic;
	module = __KernelLoadELFFromPtr(temp, size - pos, 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, error);
	delete [] temp;

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			return error;
		}

		if ((int)error >= 0)
		{
			// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run..
			// Let's just act as if it worked.
			NOTICE_LOG(LOADER, "Module %d is blacklisted or undecryptable - we lie about success", id);
			return 1;
		}
		else
		{
			NOTICE_LOG(LOADER, "Module %d failed to load: %08x", id, error);
			return error;
		}
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

static u32 sceKernelLoadModuleDNAS(const char *name, u32 flags)
{
	ERROR_LOG_REPORT(SCEMODULE, "UNIMPL 0=sceKernelLoadModuleDNAS()");
	return 0;
}

// Pretty sure this is a badly brute-forced function name...
static SceUID sceKernelLoadModuleBufferUsbWlan(u32 size, u32 bufPtr, u32 flags, u32 lmoptionPtr)
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
	u32 error;
	module = __KernelLoadELFFromPtr(Memory::GetPointer(bufPtr), size, 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, error);

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			ERROR_LOG(LOADER, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
			return error;
		}

		if ((int)error >= 0)
		{
			// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run..
			// Let's just act as if it worked.
			NOTICE_LOG(LOADER, "Module is blacklisted or undecryptable - we lie about success");
			return 1;
		}
		else
		{
			NOTICE_LOG(LOADER, "Module failed to load: %08x", error);
			return error;
		}
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

static u32 sceKernelQueryModuleInfo(u32 uid, u32 infoAddr)
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

static u32 sceKernelGetModuleIdList(u32 resultBuffer, u32 resultBufferSize, u32 idCountAddr)
{
	ERROR_LOG(SCEMODULE, "UNTESTED sceKernelGetModuleIdList(%08x, %i, %08x)", resultBuffer, resultBufferSize, idCountAddr);
	
	int idCount = 0;
	u32 resultBufferOffset = 0;

	u32 error;
	for (SceUID moduleId : loadedModules) {
		Module *module = kernelObjects.Get<Module>(moduleId, error);
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

//fix for tiger x dragon
static u32 sceKernelLoadModuleForLoadExecVSHDisc(const char *name, u32 flags, u32 optionAddr) {
	return sceKernelLoadModule(name, flags, optionAddr);
}

const HLEFunction ModuleMgrForUser[] = 
{
	{0X977DE386, &WrapU_CUU<sceKernelLoadModule>,                       "sceKernelLoadModule",                     'x', "sxx"    },
	{0XB7F46618, &WrapU_UUU<sceKernelLoadModuleByID>,                   "sceKernelLoadModuleByID",                 'x', "xxx"    },
	{0X50F0C1EC, &WrapV_UUUUU<sceKernelStartModule>,                    "sceKernelStartModule",                    'v', "xxxxx", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XD675EBB8, &WrapU_UUU<sceKernelSelfStopUnloadModule>,             "sceKernelSelfStopUnloadModule",           'x', "xxx"    },
	{0XD1FF982A, &WrapU_UUUUU<sceKernelStopModule>,                     "sceKernelStopModule",                     'x', "xxxxx", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X2E0911AA, &WrapU_U<sceKernelUnloadModule>,                       "sceKernelUnloadModule",                   'x', "x"      },
	{0X710F61B5, nullptr,                                               "sceKernelLoadModuleMs",                   '?', ""       },
	{0XF9275D98, &WrapI_UUUU<sceKernelLoadModuleBufferUsbWlan>,         "sceKernelLoadModuleBufferUsbWlan",        'i', "xxxx"   }, /// ??
	{0XCC1D3699, nullptr,                                               "sceKernelStopUnloadSelfModule",           '?', ""       },
	{0X748CBED9, &WrapU_UU<sceKernelQueryModuleInfo>,                   "sceKernelQueryModuleInfo",                'x', "xx"     },
	{0XD8B73127, &WrapU_U<sceKernelGetModuleIdByAddress>,               "sceKernelGetModuleIdByAddress",           'x', "x"      },
	{0XF0A26395, &WrapU_V<sceKernelGetModuleId>,                        "sceKernelGetModuleId",                    'x', ""       },
	{0X8F2DF740, &WrapU_UUUUU<sceKernelStopUnloadSelfModuleWithStatus>, "sceKernelStopUnloadSelfModuleWithStatus", 'x', "xxxxx"  },
	{0XFEF27DC1, &WrapU_CU<sceKernelLoadModuleDNAS>,                    "sceKernelLoadModuleDNAS",                 'x', "sx"     },
	{0X644395E2, &WrapU_UUU<sceKernelGetModuleIdList>,                  "sceKernelGetModuleIdList",                'x', "xxx"    },
	{0XF2D8D1B4, &WrapU_CUU<sceKernelLoadModuleNpDrm>,                  "sceKernelLoadModuleNpDrm",                'x', "sxx"    },
	{0XE4C4211C, nullptr,                                               "ModuleMgrForUser_E4C4211C",               '?', ""       },
	{0XFBE27467, nullptr,                                               "ModuleMgrForUser_FBE27467",               '?', ""       },
};


const HLEFunction ModuleMgrForKernel[] =
{
	{0x50F0C1EC, &WrapV_UUUUU<sceKernelStartModule>,                    "sceKernelStartModule",                    'v', "xxxxx", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED | HLE_KERNEL_SYSCALL },
	{0x977DE386, &WrapU_CUU<sceKernelLoadModule>,                       "sceKernelLoadModule",                     'x', "sxx",   HLE_KERNEL_SYSCALL },
	{0xA1A78C58, &WrapU_CUU<sceKernelLoadModuleForLoadExecVSHDisc>,     "sceKernelLoadModuleForLoadExecVSHDisc",   'x', "sxx",   HLE_KERNEL_SYSCALL }, //fix for tiger x dragon
	{0x748CBED9, &WrapU_UU<sceKernelQueryModuleInfo>,                   "sceKernelQueryModuleInfo",                'x', "xx",    HLE_KERNEL_SYSCALL },
	{0x644395E2, &WrapU_UUU<sceKernelGetModuleIdList>,                  "sceKernelGetModuleIdList",                'x', "xxx",   HLE_KERNEL_SYSCALL },
};

void Register_ModuleMgrForUser()
{
	RegisterModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}

void Register_ModuleMgrForKernel()
{
	RegisterModule("ModuleMgrForKernel", ARRAY_SIZE(ModuleMgrForKernel), ModuleMgrForKernel);		

};
