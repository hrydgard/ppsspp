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

#include <algorithm>
#include <set>

#include "zlib.h"

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeSet.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/Plugins.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/Reporting.h"
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
#include "Core/ConfigValues.h"
#include "Core/PSPLoaders.h"
#include "Core/System.h"
#include "Core/MemMapHelpers.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/scePsmf.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/HLE/sceIo.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/Debugger/Playback.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

enum : u32 {
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
static const char * const lieAboutSuccessModules[] = {
	"flash0:/kd/audiocodec.prx",
	"flash0:/kd/audiocodec_260.prx",
	"flash0:/kd/libatrac3plus.prx",
	"disc0:/PSP_GAME/SYSDIR/UPDATE/EBOOT.BIN",
	"flash0:/kd/ifhandle.prx",
	"flash0:/kd/pspnet.prx",
	"flash0:/kd/pspnet_inet.prx",
	"flash0:/kd/pspnet_apctl.prx",
	"flash0:/kd/pspnet_resolver.prx",
};

const char *NativeModuleStatusToString(NativeModuleStatus status) {
	switch (status) {
	case MODULE_STATUS_STARTING: return "STARTING";
	case MODULE_STATUS_STARTED: return "STARTED";
	case MODULE_STATUS_STOPPING: return "STOPPING";
	case MODULE_STATUS_STOPPED: return "STOPPED";
	case MODULE_STATUS_UNLOADING: return "UNLOADING";
	default: return "(err)";
	}
}

static void ImportVarSymbol(WriteVarSymbolState &state, const VarSymbolImport &var);
static void ExportVarSymbol(const VarSymbolExport &var);
static void UnexportVarSymbol(const VarSymbolExport &var);

static void ImportFuncSymbol(const FuncSymbolImport &func, bool reimporting, const char *importingModule);
static void ExportFuncSymbol(const FuncSymbolExport &func);
static void UnexportFuncSymbol(const FuncSymbolExport &func);

static bool KernelImportModuleFuncs(PSPModule *module, u32 *firstImportStubAddr, bool reimporting = false);

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

PSPModule::~PSPModule() {
	if (memoryBlockAddr) {
		// If it's either below user memory, or using a high kernel bit, it's in kernel.
		if (memoryBlockAddr < PSP_GetUserMemoryBase() || memoryBlockAddr > PSP_GetUserMemoryEnd()) {
			kernelMemory.Free(memoryBlockAddr);
		} else {
			userMemory.Free(memoryBlockAddr);
		}
		g_symbolMap->UnloadModule(memoryBlockAddr, memoryBlockSize);
	}

	if (modulePtr.ptr) {
		//Only alloc at kernel memory.
		kernelMemory.Free(modulePtr.ptr);
	}
}

u32 PSPModule::GetMissingErrorCode() {
	return SCE_KERNEL_ERROR_UNKNOWN_MODULE;
}

void PSPModule::DoState(PointerWrap &p) {
	auto s = p.Section("Module", 1, 6);
	if (!s)
		return;

	if (s >= 5) {
		Do(p, nm);
	} else {
		char temp[192];
		NativeModule *pnm = &nm;
		char *ptemp = temp;
		DoArray(p, ptemp, 0xC0);
		memcpy(pnm, ptemp, 0x2C);
		pnm->modid = GetUID();
		memcpy(((uint8_t *)pnm) + 0x30, ((uint8_t *)ptemp) + 0x2C, 0xC0 - 0x2C);
	}

	if (s >= 6)
		Do(p, crc);

	Do(p, memoryBlockAddr);
	Do(p, memoryBlockSize);
	Do(p, isFake);

	if (s < 2) {
		bool isStarted = false;
		Do(p, isStarted);
		if (isStarted)
			nm.status = MODULE_STATUS_STARTED;
		else
			nm.status = MODULE_STATUS_STOPPED;
	}

	if (s >= 3) {
		Do(p, textStart);
		Do(p, textEnd);
	}
	if (s >= 4) {
		Do(p, libstub);
		Do(p, libstubend);
	}

	if (s >= 5) {
		Do(p, modulePtr.ptr);
	}

	ModuleWaitingThread mwt = { 0 };
	Do(p, waitingThreads, mwt);
	FuncSymbolExport fsx = { {0} };
	Do(p, exportedFuncs, fsx);
	FuncSymbolImport fsi = { {0} };
	Do(p, importedFuncs, fsi);
	VarSymbolExport vsx = { {0} };
	Do(p, exportedVars, vsx);
	VarSymbolImport vsi = { {0} };
	Do(p, importedVars, vsi);

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
			for (const auto &func : importedFuncsState) {
				if (func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] != '\0' || !Memory::IsValidAddress(func.stubAddr)) {
					foundBroken = true;
				} else {
					ImportFunc(func, true);
				}
			}

			if (foundBroken) {
				ERROR_LOG(Log::Loader, "Broken stub import data while loading state");
			}
		}

		char moduleName[29] = { 0 };
		truncate_cpy(moduleName, nm.name);
		if (memoryBlockAddr != 0) {
			g_symbolMap->AddModule(moduleName, memoryBlockAddr, memoryBlockSize);
		}
	}

	HLEPlugins::DoState(p);

	RebuildImpExpModuleNames();
}

void PSPModule::ImportFunc(const FuncSymbolImport &func, bool reimporting) {
	if (!Memory::IsValidAddress(func.stubAddr)) {
		WARN_LOG_REPORT(Log::Loader, "Invalid address for syscall stub %s %08x", func.moduleName, func.nid);
		return;
	}

	DEBUG_LOG(Log::Loader, "Importing %s : %08x", GetHLEFuncName(func.moduleName, func.nid), func.stubAddr);

	// Add the symbol to the symbol map for debugging.
	char temp[256];
	snprintf(temp, sizeof(temp), "zz_%s", GetHLEFuncName(func.moduleName, func.nid));
	g_symbolMap->AddFunction(temp, func.stubAddr, 8);

	// Keep track and actually hook it up if possible.
	importedFuncs.push_back(func);
	impModuleNames.insert(func.moduleName);
	ImportFuncSymbol(func, reimporting, GetName());
}

void PSPModule::ImportVar(WriteVarSymbolState &state, const VarSymbolImport &var) {
	// Keep track and actually hook it up if possible.
	importedVars.push_back(var);
	impModuleNames.insert(var.moduleName);
	ImportVarSymbol(state, var);
}

void PSPModule::ExportFunc(const FuncSymbolExport &func) {
	if (isFake) {
		return;
	}
	exportedFuncs.push_back(func);
	expModuleNames.insert(func.moduleName);
	ExportFuncSymbol(func);
}

void PSPModule::ExportVar(const VarSymbolExport &var) {
	if (isFake) {
		return;
	}
	exportedVars.push_back(var);
	expModuleNames.insert(var.moduleName);
	ExportVarSymbol(var);
}

void PSPModule::GetQuickInfo(char *ptr, int size) {
	snprintf(ptr, size, "%d.%d %sname=%s gp=%08x entry=%08x",
		nm.version[1], nm.version[0],
		isFake ? "(faked) " : "",
		nm.name,
		nm.gp_value,
		nm.entry_addr);
}

void PSPModule::GetLongInfo(char *ptr, int bufSize) const {
	StringWriter w(ptr, bufSize);
	w.F("%s: Version %d.%d. %d segments", nm.name, nm.version[1], nm.version[0], nm.nsegment).endl();
	w.F("Memory block: %08x (%08x/%d bytes)", memoryBlockAddr, memoryBlockSize, memoryBlockSize).endl();
	for (int i = 0; i < (int)nm.nsegment; i++) {
		w.F("  %08x (%08x bytes)\n", nm.segmentaddr[i], nm.segmentsize[i]);
	}
	w.F("Text: %08x (%08x bytes)\n", nm.text_addr, nm.text_size);
	w.F("Data: %08x (%08x bytes)\n", GetDataAddr(), nm.data_size);
	w.F("BSS: % 08x(% 08x bytes)\n", GetBSSAddr(), nm.bss_size);
	w.F("Entry: %08x GP: %08x\n", nm.entry_addr, nm.gp_value);
	w.F("Status: %08x\n", nm.status);
}

KernelObject *__KernelModuleObject()
{
	return new PSPModule;
}

class AfterModuleEntryCall : public PSPAction {
public:
	AfterModuleEntryCall() {}
	SceUID moduleID_;
	u32 retValAddr;
	void run(MipsCall &call) override;
	void DoState(PointerWrap &p) override {
		auto s = p.Section("AfterModuleEntryCall", 1);
		if (!s)
			return;

		Do(p, moduleID_);
		Do(p, retValAddr);
	}
	static PSPAction *Create() {
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

	Do(p, actionAfterModule);
	__KernelRestoreActionType(actionAfterModule, AfterModuleEntryCall::Create);

	if (s >= 2) {
		Do(p, loadedModules);
	}

	if (p.mode == p.MODE_READ) {
		u32 error;
		// We process these late, since they depend on loadedModules for interlinking.
		for (SceUID moduleId : loadedModules) {
			PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
			if (module && module->libstub != 0) {
				if (!KernelImportModuleFuncs(module, nullptr, true)) {
					ERROR_LOG(Log::Loader, "Something went wrong loading imports on load state");
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
	HLEPlugins::Unload();
}

// Sometimes there are multiple LO16's or HI16's per pair, even though the ABI says nothing of this.
// For multiple LO16's, we need the original (unrelocated) instruction data of the HI16.
// For multiple HI16's, we just need to set each one.
struct HI16RelocInfo {
	u32 addr;
	u32 data;
};
// We have to post-process the HI16 part, since it might be +1 or not depending on the LO16 value.
// For that purpose, we use this state to track HI16s to adjust.
struct WriteVarSymbolState {
	u32 lastHI16ExportAddress = 0;
	std::vector<HI16RelocInfo> lastHI16Relocs;
	bool lastHI16Processed = true;
};

static void WriteVarSymbol(WriteVarSymbolState &state, u32 exportAddress, u32 relocAddress, u8 type, bool reverse = false) {
	u32 relocData = Memory::Read_Instruction(relocAddress, true).encoding;

	switch (type) {
	case R_MIPS_NONE:
		WARN_LOG_REPORT(Log::Loader, "Var relocation type NONE - %08x => %08x", exportAddress, relocAddress);
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
			WARN_LOG_REPORT(Log::Loader, "Bad var relocation addresses for type 26 - %08x => %08x", exportAddress, relocAddress)
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
		if (state.lastHI16ExportAddress != exportAddress) {
			if (!state.lastHI16Processed && !state.lastHI16Relocs.empty()) {
				WARN_LOG_REPORT(Log::Loader, "Unsafe unpaired HI16 variable relocation @ %08x / %08x", state.lastHI16Relocs[state.lastHI16Relocs.size() - 1].addr, relocAddress);
			}

			state.lastHI16ExportAddress = exportAddress;
			state.lastHI16Relocs.clear();
		}

		// After this will be an R_MIPS_LO16.  If that addition overflows, we need to account for it in HI16.
		// The R_MIPS_LO16 and R_MIPS_HI16 will often be *different* relocAddress values.
		HI16RelocInfo reloc;
		reloc.addr = relocAddress;
		reloc.data = Memory::Read_Instruction(relocAddress, true).encoding;
		state.lastHI16Relocs.push_back(reloc);
		state.lastHI16Processed = false;
		break;

	case R_MIPS_LO16:
		{
			// Sign extend the existing low value (e.g. from addiu.)
			const u32 offsetLo = SignExtend16ToU32(relocData);
			u32 full = exportAddress;
			// This is only used in the error case (no hi/wrong hi.)
			if (!reverse) {
				full = offsetLo + exportAddress;
			} else {
				full = offsetLo - exportAddress;
			}

			// The ABI requires that these come in pairs, at least.
			if (state.lastHI16Relocs.empty()) {
				ERROR_LOG_REPORT(Log::Loader, "LO16 without any HI16 variable import at %08x for %08x", relocAddress, exportAddress);
			// Try to process at least the low relocation...
			} else if (state.lastHI16ExportAddress != exportAddress) {
				ERROR_LOG_REPORT(Log::Loader, "HI16 and LO16 imports do not match at %08x for %08x (should be %08x)", relocAddress, state.lastHI16ExportAddress, exportAddress);
			} else {
				// Process each of the HI16.  Usually there's only one.
				for (const auto &reloc : state.lastHI16Relocs) {
					if (!reverse) {
						full = (reloc.data << 16) + offsetLo + exportAddress;
					} else {
						full = (reloc.data << 16) + offsetLo - exportAddress;
					}
					// The low instruction will be a signed add, which means (full & 0x8000) will subtract.
					// We add 1 in that case so that it ends up the right value.
					u16 high = (full >> 16) + ((full & 0x8000) ? 1 : 0);
					Memory::Write_U32((reloc.data & ~0xFFFF) | high, reloc.addr);
					currentMIPS->InvalidateICache(reloc.addr, 4);
				}
				state.lastHI16Processed = true;
			}

			// With full set above (hopefully), now we just need to correct the low instruction.
			relocData = (relocData & ~0xFFFF) | (full & 0xFFFF);
		}
		break;

	default:
		WARN_LOG_REPORT(Log::Loader, "Unsupported var relocation type %d - %08x => %08x", type, exportAddress, relocAddress);
	}

	Memory::Write_U32(relocData, relocAddress);
	currentMIPS->InvalidateICache(relocAddress, 4);
}

void ImportVarSymbol(WriteVarSymbolState &state, const VarSymbolImport &var) {
	if (var.nid == 0) {
		// TODO: What's the right thing for this?
		ERROR_LOG_REPORT(Log::Loader, "Var import with nid = 0, type = %d", var.type);
		return;
	}

	if (!Memory::IsValidAddress(var.stubAddr)) {
		ERROR_LOG_REPORT(Log::Loader, "Invalid address for var import nid = %08x, type = %d, addr = %08x", var.nid, var.type, var.stubAddr);
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for exports currently loaded modules already have.  Maybe it's available?
		for (const auto &exported : module->exportedVars) {
			if (exported.Matches(var)) {
				WriteVarSymbol(state, exported.symAddr, var.stubAddr, var.type);
				return;
			}
		}
	}

	// It hasn't been exported yet, but hopefully it will later.
	INFO_LOG(Log::Loader, "Variable (%s,%08x) unresolved, storing for later resolving", var.moduleName, var.nid);
}

void ExportVarSymbol(const VarSymbolExport &var) {
	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for imports currently loaded modules already have, hook it up right away.
		WriteVarSymbolState state;
		for (auto &imported : module->importedVars) {
			if (var.Matches(imported)) {
				INFO_LOG(Log::Loader, "Resolving var %s/%08x", var.moduleName, var.nid);
				WriteVarSymbol(state, var.symAddr, imported.stubAddr, imported.type);
			}
		}
	}
}

void UnexportVarSymbol(const VarSymbolExport &var) {
	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(var.moduleName)) {
			continue;
		}

		// Look for imports modules that are *still* loaded have, and reverse them.
		WriteVarSymbolState state;
		for (auto &imported : module->importedVars) {
			if (var.Matches(imported)) {
				INFO_LOG(Log::Loader, "Unresolving var %s/%08x", var.moduleName, var.nid);
				WriteVarSymbol(state, var.symAddr, imported.stubAddr, imported.type, true);
			}
		}
	}
}

static bool FuncImportIsHLE(std::string_view module, u32 nid) {
	// TODO: Take into account whether HLE is enabled for the module.
	// Also, this needs to be more efficient.
	if (!ShouldHLEModuleByImportName(module)) {
		return false;
	}

	return GetHLEFunc(module, nid) != nullptr;
}

void ImportFuncSymbol(const FuncSymbolImport &func, bool reimporting, const char *importingModule) {
	bool shouldHLE = ShouldHLEModuleByImportName(func.moduleName);

	// Prioritize HLE implementations, if we should HLE this.
	if (shouldHLE && GetHLEFunc(func.moduleName, func.nid)) {
		if (reimporting && Memory::Read_Instruction(func.stubAddr + 4) != GetSyscallOp(func.moduleName, func.nid)) {
			WARN_LOG(Log::Loader, "Reimporting updated syscall %s", GetHLEFuncName(func.moduleName, func.nid));
		}
		// TODO: There's some double lookup going on here (we already did the lookup in GetHLEFunc above).
		WriteHLESyscall(func.moduleName, func.nid, func.stubAddr);
		currentMIPS->InvalidateICache(func.stubAddr, 8);
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for exports currently loaded modules already have.  Maybe it's available?
		for (auto it = module->exportedFuncs.begin(), end = module->exportedFuncs.end(); it != end; ++it) {
			if (it->Matches(func)) {
				if (reimporting && Memory::Read_Instruction(func.stubAddr) != MIPS_MAKE_J(it->symAddr)) {
					WARN_LOG_REPORT(Log::Loader, "Reimporting: func import %s/%08x changed", func.moduleName, func.nid);
				}
				WriteFuncStub(func.stubAddr, it->symAddr);
				currentMIPS->InvalidateICache(func.stubAddr, 8);
				return;
			}
		}
	}

	// It hasn't been exported yet, but hopefully it will later. Check if we know about it through HLE.
	if (shouldHLE) {
		// We used to report this, but I don't think it's very interesting anymore.
		WARN_LOG(Log::Loader, "Unknown syscall from known HLE module '%s': 0x%08x (import for '%s')", func.moduleName, func.nid, importingModule);
	} else {
		INFO_LOG(Log::Loader, "Function (%s,%08x) unresolved in '%s', storing for later resolving", func.moduleName, func.nid, importingModule);
	}

	if (shouldHLE || !reimporting) {
		WriteFuncMissingStub(func.stubAddr, func.nid);
		currentMIPS->InvalidateICache(func.stubAddr, 8);
	}
}

void ExportFuncSymbol(const FuncSymbolExport &func) {
	if (FuncImportIsHLE(func.moduleName, func.nid)) {
		// HLE covers this already - let's ignore the function.
		// This means that we loaded a module that we are HLE:ing, which is kinda unnecessary, but not harmful. And might even be good.
		WARN_LOG(Log::Loader, "Ignoring func export %s/%08x, already implemented in HLE.", func.moduleName, func.nid);
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue; 
		}

		// Look for imports currently loaded modules already have, hook it up right away.
		for (auto it = module->importedFuncs.begin(), end = module->importedFuncs.end(); it != end; ++it) {
			if (func.Matches(*it)) {
				INFO_LOG(Log::Loader, "Resolving function %s/%08x", func.moduleName, func.nid);
				WriteFuncStub(it->stubAddr, func.symAddr);
				currentMIPS->InvalidateICache(it->stubAddr, 8);
			}
		}
	}
}

void UnexportFuncSymbol(const FuncSymbolExport &func) {
	if (FuncImportIsHLE(func.moduleName, func.nid)) {
		// Oops, HLE covers this.
		return;
	}

	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module || !module->ImportsOrExportsModuleName(func.moduleName)) {
			continue;
		}

		// Look for imports modules that are *still* loaded have, and write back stubs.
		for (auto it = module->importedFuncs.begin(), end = module->importedFuncs.end(); it != end; ++it) {
			if (func.Matches(*it)) {
				INFO_LOG(Log::Loader, "Unresolving function %s/%08x", func.moduleName, func.nid);
				WriteFuncMissingStub(it->stubAddr, it->nid);
				currentMIPS->InvalidateICache(it->stubAddr, 8);
			}
		}
	}
}

void PSPModule::Cleanup() {
	MIPSAnalyst::ForgetFunctions(textStart, textEnd);

	loadedModules.erase(GetUID());

	for (auto it = exportedVars.begin(), end = exportedVars.end(); it != end; ++it) {
		UnexportVarSymbol(*it);
	}
	for (auto it = exportedFuncs.begin(), end = exportedFuncs.end(); it != end; ++it) {
		UnexportFuncSymbol(*it);
	}

	if (memoryBlockAddr != 0 && nm.text_addr != 0 && memoryBlockSize >= nm.data_size + nm.bss_size + nm.text_size) {
		DEBUG_LOG(Log::Loader, "Zeroing out module %s memory: %08x - %08x", nm.name, memoryBlockAddr, memoryBlockAddr + memoryBlockSize);
		u32 clearSize = Memory::ClampValidSizeAt(nm.text_addr, (u32)nm.text_size + 3);
		for (u32 i = 0; i < clearSize; i += 4) {
			Memory::WriteUnchecked_U32(MIPS_MAKE_BREAK(1), nm.text_addr + i);
		}
		NotifyMemInfo(MemBlockFlags::WRITE, nm.text_addr, clearSize, "ModuleClear");
		Memory::Memset(nm.text_addr + nm.text_size, -1, nm.data_size + nm.bss_size, "ModuleClear");

		// Let's also invalidate, just to make sure it's cleared out for any future data.
		currentMIPS->InvalidateICache(memoryBlockAddr, memoryBlockSize);
	}
}

static bool KernelImportModuleFuncs(PSPModule *module, u32 *firstImportStubAddr, bool reimporting) {
	// Can't run - we didn't keep track of the libstub entry.
	if (module->libstub == 0) {
		return false;
	}
	if (!Memory::IsValidRange(module->libstub, module->libstubend - module->libstub)) {
		ERROR_LOG_REPORT(Log::Loader, "Garbage libstub address %08x or end %08x", module->libstub, module->libstubend);
		return false;
	}

	const u32_le *entryPos = (const u32_le *)Memory::GetPointerUnchecked(module->libstub);
	const u32_le *entryEnd = (const u32_le *)Memory::GetPointerUnchecked(module->libstubend);

	bool needReport = false;
	while (entryPos < entryEnd) {
		const PspLibStubEntry *entry = (const PspLibStubEntry *)entryPos;
		entryPos += entry->size;

		const char *modulename;
		if (Memory::IsValidAddress(entry->name)) {
			modulename = Memory::GetCharPointer(entry->name);
		} else {
			modulename = "(invalidname)";
			needReport = true;
		}

		DEBUG_LOG(Log::Loader, "Importing Module %s, stubs at %08x", modulename, entry->firstSymAddr);
		if (entry->size != 5 && entry->size != 6) {
			if (entry->size != 7) {
				WARN_LOG_REPORT(Log::Loader, "Unexpected module entry size %d", entry->size);
				needReport = true;
			} else if (entry->extra != 0) {
				WARN_LOG_REPORT(Log::Loader, "Unexpected module entry with non-zero 7th value %08x", entry->extra);
				needReport = true;
			}
		}

		// Prevent infinite spin on bad data.
		if (entry->size == 0)
			break;

		// If nidData is 0, only variables are being imported.
		if (entry->numFuncs > 0 && entry->nidData != 0) {
			if (!Memory::IsValidAddress(entry->nidData)) {
				ERROR_LOG_REPORT(Log::Loader, "Crazy nidData address %08x, skipping entire module", entry->nidData);
				needReport = true;
				continue;
			}

			FuncSymbolImport func;
			strncpy(func.moduleName, modulename, KERNELOBJECT_MAX_NAME_LENGTH);
			func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

			u32_le *nidDataPtr = (u32_le *)Memory::GetPointerUnchecked(entry->nidData);
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
			WARN_LOG_REPORT(Log::Loader, "Module entry with %d imports but no valid address", entry->numFuncs);
			needReport = true;
		}

		// We skip vars when reimporting, since we might double-offset.
		// We only reimport funcs, which can't be double-offset.
		if (entry->numVars > 0 && entry->varData != 0 && !reimporting) {
			if (!Memory::IsValidAddress(entry->varData)) {
				ERROR_LOG_REPORT(Log::Loader, "Crazy varData address %08x, skipping rest of module", entry->varData);
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
					WARN_LOG_REPORT(Log::Loader, "Bad relocation list address for nid %08x in %s", nid, modulename);
					continue;
				}

				WriteVarSymbolState state;
				const u32_le *varRef = (const u32_le *)Memory::GetPointerUnchecked(varRefsPtr);
				for (; *varRef != 0; ++varRef) {
					var.nid = nid;
					var.stubAddr = (*varRef & 0x03FFFFFF) << 2;
					var.type = *varRef >> 26;
					module->ImportVar(state, var);
				}
			}
		} else if (entry->numVars > 0 && !reimporting) {
			WARN_LOG_REPORT(Log::Loader, "Module entry with %d var imports but no valid address", entry->numVars);
			needReport = true;
		}

		DEBUG_LOG(Log::Loader, "-------------------------------------------------------------");
	}

	if (needReport) {
		std::string debugInfo;
		entryPos = (const u32_le *)Memory::GetPointer(module->libstub);
		while (entryPos < entryEnd) {
			const PspLibStubEntry *entry = (const PspLibStubEntry *)entryPos;
			entryPos += entry->size;

			char temp[512];
			const char *modulename;
			if (Memory::IsValidAddress(entry->name)) {
				modulename = Memory::GetCharPointerUnchecked(entry->name);
			} else {
				modulename = "(invalidname)";
			}

			snprintf(temp, sizeof(temp), "%s ver=%04x, flags=%04x, size=%d, numVars=%d, numFuncs=%d, nidData=%08x, firstSym=%08x, varData=%08x, extra=%08x\n",
				modulename, entry->version, entry->flags, entry->size, entry->numVars, entry->numFuncs, entry->nidData, entry->firstSymAddr, entry->size >= 6 ? (u32)entry->varData : 0, entry->size >= 7 ? (u32)entry->extra : 0);
			debugInfo += temp;
		}

		Reporting::ReportMessage("Module linking debug info:\n%s", debugInfo.c_str());
	}

	return true;
}

static int gzipDecompress(u8 *OutBuffer, int OutBufferLength, u8 *InBuffer) {
	int err;
	z_stream stream;
	u8 *outBufferPtr;

	outBufferPtr = OutBuffer;
	stream.next_in = InBuffer;
	stream.avail_in = (uInt)OutBufferLength;
	stream.next_out = outBufferPtr;
	stream.avail_out = (uInt)OutBufferLength;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	err = inflateInit2(&stream, 16+MAX_WBITS);
	if (err != Z_OK) {
		return -1;
	}
	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		ERROR_LOG(Log::Loader, "gzipDecompress: Didn't reach the end of the input, output buffer too small?");
		inflateEnd(&stream);
		return -2;
	}
	inflateEnd(&stream);
	return stream.total_out;
}

static void parsePrxLibInfo(const u8* ptr, u32 headerSize) {
	// 0x0 - ~SCE
	// 0x4 - the header's size
	// At some offset (starting from 0x8) - the lib info
	const u8 start = 0x8;
	const u8 lib_info_size = 8 + 12 + 4; // The prefix, libname and version

	if (headerSize - start < lib_info_size) {
		// That's very wrong!
		WARN_LOG(Log::sceModule, "~SCE module, header too small (0x%x bytes) to fit a lib info", headerSize);
		return;
	}

	auto end = ptr + headerSize;
	ptr += start;
	while (*ptr == 0x0 && ptr < end) {
		++ptr;
	}

	// Now that we have found the start, let's do one more check
	if (end - ptr < lib_info_size) {
		// That's very wrong!
		WARN_LOG(Log::sceModule, "~SCE module, unexpected header (not an error)");
		return;
	}

	// The lib info prefix looks like {'\', 'y', 'r', '=', '`', 'c', '`', '0'} (Big Endian)
	const u64_le lib_info_prefix = 0x306063603D72795C;

	// 'ptr' can potentially be misaligned here so let's use a memcmp instead of dereferencing 8 bytes at 'ptr'
	if (memcmp(ptr, &lib_info_prefix, 8) != 0) {
		// That's very wrong!
		WARN_LOG(Log::sceModule, "~SCE module, unexpected header (not an error)");
		return;
	}
	ptr += sizeof(lib_info_prefix);

	// Decipher the Caesar cipher with sanity checks (isprint)

	u8 nameBuffer[12 + 1];
	for (int i = 0; i < 12; ++i, ++ptr) {
		u8 symbol = *ptr - 0x12u;
		nameBuffer[i] = isprint(symbol) ? symbol : '?';
	}
	nameBuffer[12] = '\0';

	u8 versionBuffer[7 + 1] = "?.?.?.?";
	for (int i = 0; i < 4; ++i, ++ptr) {
		u8 symbol = *ptr - 0x14u;
		if (isprint(symbol)) {
			versionBuffer[2 * i] = symbol;
		}
	}
	// The null byte is already in its place, no need to assign it manually
	
	INFO_LOG(Log::sceModule, "~SCE module: Lib-PSP %s (SDK %s)", nameBuffer, versionBuffer);
}

inline u32 Read32(const u8 *ptr) {
	u32 value;
	memcpy(&value, ptr, 4);
	return value;
}

enum : u32 {
	SCE_MAGIC = 0x4543537e,
	PSP_MAGIC = 0x5053507e,
	ELF_MAGIC = 0x464c457f,
};

// filename is only used for dumping/metadata.
static PSPModule *__KernelLoadELFFromPtr(const u8 *ptr, size_t elfSize, u32 loadAddress, bool fromTop, std::string *error_string, u32 *magic, std::string_view filename, u32 &error) {
	PSPModule *module = new PSPModule();
	kernelObjects.Create(module);
	loadedModules.insert(module->GetUID());
	memset(&module->nm, 0, sizeof(module->nm));

	module->crc = crc32(0, ptr, (uInt)elfSize);
	module->nm.modid = module->GetUID();

	bool reportedModule = false;
	bool fakeLoadedModule = false;
	u32 devkitVersion = 0;
	u8 *newptr = 0;
	const PSP_Header *head = nullptr;

	if (Read32(ptr) == SCE_MAGIC) { // "~SCE"
		const u32 headerSize = *(const u32_le*)(ptr + 4);
		if (headerSize < elfSize) {
			// Parse and print the lib info
			parsePrxLibInfo(ptr, headerSize);

			// Advance the pointer to the relevant data
			ptr += headerSize;
			elfSize -= headerSize;
		}
		else {
			ERROR_LOG(Log::sceModule, "~SCE module: bad data");
		}
	}
	*magic = Read32(ptr);
	if (*magic == PSP_MAGIC && elfSize > sizeof(PSP_Header)) { // "~PSP"
		DEBUG_LOG(Log::sceModule, "Decrypting ~PSP file");
		head = (const PSP_Header *)ptr;
		devkitVersion = head->devkitversion;

		bool wasDisabled;
		if (ShouldHLEModule(head->modname, &wasDisabled)) {
			int ver = (head->module_ver_hi << 8) | head->module_ver_lo;
			INFO_LOG(Log::sceModule, "Loading module %s with version %04x, devkit %08x, crc %x", head->modname, ver, head->devkitversion, module->crc);

			// TODO: Don't conflate these!
			reportedModule = true;
			fakeLoadedModule = true;
		}

		if (wasDisabled) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, StringFromFormat("HLE for '%s' has been manually disabled", head->modname));
		}
		const u8 *in = ptr;
		const auto isGzip = head->comp_attribute & 1;
		// Kind of odd.
		u32 size = head->psp_size;
		if (size > elfSize) {
			*error_string = StringFromFormat("ELF/PRX truncated: %d > %d", (int)size, (int)elfSize);
			module->Cleanup();
			kernelObjects.Destroy<PSPModule>(module->GetUID());
			// TODO: Might be the wrong error code.
			error = SCE_KERNEL_ERROR_FILEERR;
			return nullptr;
		}
		const auto maxElfSize = std::max(head->elf_size, head->psp_size);
		newptr = new u8[maxElfSize];
		elfSize = maxElfSize;
		ptr = newptr;
		int decryptedSize = pspDecryptPRX(in, (u8*)ptr, head->psp_size);
		_dbg_assert_(decryptedSize <= (int)maxElfSize);
		if (decryptedSize <= 0 && Read32(ptr + 0x150) == ELF_MAGIC) {
			decryptedSize = head->psp_size - 0x150;
			memcpy(newptr, in + 0x150, decryptedSize);
			// In this case it's definitely not compressed. Added assert below.
		}

		// Don't accept ELFs over 24MB.
		if (decryptedSize > 24 * 1024 * 1024) {
			*error_string = StringFromFormat("ELF/PRX corrupt, unreasonable decrypted size: %d", (u32)decryptedSize);
			// TODO: Might be the wrong error code.
			error = SCE_KERNEL_ERROR_FILEERR;
			return nullptr;
		}

		// decompress if required.
		if (isGzip) {
			_dbg_assert_(Read32(ptr + 0x150) != ELF_MAGIC);

			// Can't decompress in place so we need a temporary buffer.
			u8 *temp = (u8 *)malloc(decryptedSize);
			_assert_msg_(temp != nullptr, "Failed to allocate gzip decompression buffer");
			memcpy(temp, ptr, decryptedSize);
			int outBytes = gzipDecompress((u8 *)ptr, maxElfSize, temp);
			if (outBytes < 0) {
				ERROR_LOG(Log::sceModule, "Module gzip decompression failed!");
			}
			free(temp);
			INFO_LOG(Log::sceModule, "gzip is enabled in '%s', decompressing (%d -> %d bytes, bufmax=%d).", head->modname, decryptedSize, outBytes, maxElfSize);
		}

		if (fakeLoadedModule) {
			// Opportunity to dump the decrypted elf, even if we choose to fake it.
			// NOTE: filename is not necessarily a good choice!
			std::string elfFilename(KeepAfterLast(filename, '/'));
			if (elfFilename.empty() || startsWith(elfFilename, "sce_lbn")) {
				// Use the name from the header.
				elfFilename = head->modname;
			}
			DumpFileIfEnabled(ptr, (u32)elfSize, elfFilename.c_str(), DumpFileType::PRX);

			// This should happen for all "kernel" modules.
			*error_string = "Missing key";
			delete [] newptr;
			module->isFake = true;
			strncpy(module->nm.name, head->modname, ARRAY_SIZE(module->nm.name));
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;

			// Let's still try to allocate it properly. It may use user memory.
			// Also, this memory can be used by the fake implementation, like sceAtrac does for contexts.
			u32 totalSize = 0;
			for (int i = 0; i < 4; ++i) {
				if (head->seg_size[i]) {
					module->nm.segmentsize[i] = head->seg_size[i];
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
				kernelObjects.Destroy<PSPModule>(module->GetUID());
				return nullptr;
			}

			error = 0;
			module->memoryBlockAddr = addr;
			module->memoryBlockSize = totalSize;
			module->nm.text_addr = addr;
			module->nm.bss_size = head->bss_size;
			module->nm.nsegment = head->nsegments;
			module->nm.version[0] = head->module_ver_hi;
			module->nm.version[1] = head->module_ver_lo;

			u32 seg_addr = addr;
			for (int i = 0; i < 4; ++i) {
				if (module->nm.segmentsize[i]) {
					module->nm.segmentaddr[i] = seg_addr;
					seg_addr += module->nm.segmentsize[i];
				}
			}

			// TODO: Now that we can decrypt, we could/should actually let the ELF loader run!
			// But at least here we have a usable address.

			int ver = (head->module_ver_hi << 8) | head->module_ver_lo;
			if (!strcmp(head->modname, "sceMpeg_library")) {
				__MpegLoadModule(ver, module->crc);
			}
			if (!strcmp(head->modname, "scePsmfP_library") || !strcmp(head->modname, "scePsmfPlayer") || !strcmp(head->modname, "libpsmfplayer") || !strcmp(head->modname, "psmf_jk") || !strcmp(head->modname, "jkPsmfP_library")) {
				__PsmfPlayerLoadModule(devkitVersion, module->crc);
			}
			if (!strcmp(head->modname, "sceATRAC3plus_Library")) {
				__AtracNotifyLoadModule(ver, module->crc, module->GetBSSAddr(), head->bss_size);
			}

			return module;
		} else if (decryptedSize <= 0) {
			ERROR_LOG(Log::sceModule, "Failed decrypting PRX! That's not normal! ret = %i\n", decryptedSize);
			Reporting::ReportMessage("Failed decrypting the PRX (ret = %i, size = %i, psp_size = %i)!", decryptedSize, head->elf_size, head->psp_size);
			// Fall through to safe exit in the next check.
		} else {
			// TODO: Is this right?
			module->nm.bss_size = head->bss_size;

			// If we've made it this far, it should be safe to dump.
			// Copy the name to ensure it's null terminated.
			// TODO: How do we determine if it's the eboot?
			if (endsWith(filename, "BOOT.BIN")) {
				DumpFileIfEnabled(ptr, (u32)elfSize, "EBOOT.BIN", DumpFileType::EBOOT);
			} else {
				char name[32]{};
				strncpy(name, head->modname, ARRAY_SIZE(head->modname));

				DumpFileIfEnabled(ptr, (u32)elfSize, name, DumpFileType::PRX);
			}
		}
	}

	// We keep reading from 'ptr' here, even though it might now point to a decompressed / decrypted buffer.

	// DO NOT change to else if, see above.
	if (Read32(ptr) != ELF_MAGIC) {
		ERROR_LOG(Log::sceModule, "Wrong magic number %08x", Read32(ptr));
		*error_string = "File corrupt";
		delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<PSPModule>(module->GetUID());
		error = SCE_KERNEL_ERROR_UNSUPPORTED_PRX_TYPE;
		return nullptr;
	}

	// Open ELF reader
	ElfReader reader((const void *)ptr, elfSize);

	int result = reader.LoadInto(loadAddress, fromTop);
	if (result != SCE_KERNEL_ERROR_OK) {
		ERROR_LOG(Log::sceModule, "LoadInto failed with error %08x",result);
		delete [] newptr;
		module->Cleanup();
		kernelObjects.Destroy<PSPModule>(module->GetUID());
		error = result;
		return nullptr;
	}
	module->memoryBlockAddr = reader.GetVaddr();
	module->memoryBlockSize = reader.GetTotalSize();

	currentMIPS->InvalidateICache(module->memoryBlockAddr, module->memoryBlockSize);

	SectionID sceModuleInfoSection = reader.GetSectionByName(".rodata.sceModuleInfo");
	const PspModuleInfo *modinfo;

	u32 modinfoaddr;

	if (sceModuleInfoSection != -1)
		modinfoaddr = reader.GetSectionAddr(sceModuleInfoSection);
	else
		modinfoaddr = reader.GetSegmentVaddr(0) + (reader.GetSegmentPaddr(0) & 0x7FFFFFFF) - reader.GetSegmentOffset(0);

	if (!Memory::IsValidAddress(modinfoaddr)) {
		*error_string = StringFromFormat("Bad module info address %08x", modinfoaddr);
		ERROR_LOG(Log::sceModule, "Bad module info address %08x", modinfoaddr);
		delete[] newptr;
		module->Cleanup();
		kernelObjects.Destroy<PSPModule>(module->GetUID());
		error = SCE_KERNEL_ERROR_BAD_FILE;  // Probably not the right error code.
		return nullptr;
	}

	modinfo = (const PspModuleInfo *)Memory::GetPointerUnchecked(modinfoaddr);

	// OK, even if it's an ELF module, it might be one we shouldn't fully load and execute!
	// This is seen with mpeg.prx in Tony Hawk's Underground 2, see #20568.
	if (ShouldHLEModule(modinfo->name)) {
		// We load it, but at least we don't run any part of it.
		module->isFake = true;
	}

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

	if (module->memoryBlockAddr != 0) {
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

	INFO_LOG(Log::Loader, "Module %s: %08x %08x %08x", modinfo->name, modinfo->gp, modinfo->libent, modinfo->libstub);
	DEBUG_LOG(Log::Loader,"===================================================");

	u32 firstImportStubAddr = 0;
	KernelImportModuleFuncs(module, &firstImportStubAddr);

	if (textSection == -1) {
		module->textStart = reader.GetVaddr();
		module->textEnd = firstImportStubAddr == 0 ? reader.GetVaddr() : firstImportStubAddr - 4;
		// Reference Jpcsp.
		if (reader.GetFirstSegmentAlign() > 0)
			module->textStart &= ~(reader.GetFirstSegmentAlign() - 1);
		// PSP set these values even if no section.
		module->nm.text_addr = module->textStart;
		module->nm.text_size = reader.GetTotalTextSizeFromSeg();
	}

	if (!module->isFake) {
		bool scan = true;
		// If the ELF has debug symbols, don't add entries to the symbol table.
		bool insertSymbols = scan && !reader.LoadSymbols();
		std::vector<SectionID> codeSections = reader.GetCodeSections();
		for (SectionID id : codeSections) {
			const u32 start = reader.GetSectionAddr(id);
			// Note: scan end is inclusive.
			const u32 end = start + reader.GetSectionSize(id) - 4;
			const u32 len = end + 4 - start;
			if (len == 0) {
				// Seen in WWE: Smackdown vs Raw 2009. See #17435.
				continue;
			}
			if (!Memory::IsValid4AlignedRange(start, len)) {
				ERROR_LOG(Log::Loader, "Bad section %08x (len %08x) of section %d", start, len, id);
				continue;
			}

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

			if (Memory::IsValid4AlignedRange(scanStart, scanEnd - scanStart)) {
				// Skip the exports and imports sections, they're not code.
				if (scanEnd >= std::min(modinfo->libent, modinfo->libstub)) {
					insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, std::min(modinfo->libent, modinfo->libstub), insertSymbols);
					scanStart = std::min(modinfo->libentend, modinfo->libstubend);
				}
				if (scanEnd >= std::max(modinfo->libent, modinfo->libstub)) {
					insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, std::max(modinfo->libent, modinfo->libstub), insertSymbols);
					scanStart = std::max(modinfo->libentend, modinfo->libstubend);
				}
				insertSymbols = MIPSAnalyst::ScanForFunctions(scanStart, scanEnd, insertSymbols);
			} else {
				ERROR_LOG(Log::Loader, "Bad text scan range %08x-%08x", scanStart, scanEnd);
			}
		}

		if (scan) {
			// TODO: Limit this to the newly loaded range! This is expensive, well, at least in debug builds
			// and the cause of stutter during Wipeout Pure initialization.
			MIPSAnalyst::FinalizeScan(insertSymbols);
		}
	}

	// Look at the exports, too.
	// TODO: Add them to the symbol map!

	struct PspLibEntEntry {
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

	module->nm.ent_top = modinfo->libent;
	module->nm.ent_size = modinfo->libentend - modinfo->libent;
	module->nm.stub_top = modinfo->libstub;
	module->nm.stub_size = modinfo->libstubend - modinfo->libstub;

	const u32_le *entPos = (u32_le *)Memory::GetPointer(modinfo->libent);
	const u32_le *entEnd = (u32_le *)Memory::GetPointer(modinfo->libentend);

	for (int m = 0; entPos < entEnd; ++m) {
		const PspLibEntEntry *ent = (const PspLibEntEntry *)entPos;
		entPos += ent->size;
		if (ent->size == 0) {
			WARN_LOG_REPORT(Log::Loader, "Invalid export entry size %d", ent->size);
			entPos += 4;
			continue;
		}

		const u32 variableCount = ent->size <= 4 ? ent->vcount : std::max((u32)ent->vcount , (u32)ent->vcountNew);
		const char *name;
		if (Memory::IsValidAddress(ent->name)) {
			name = Memory::GetCharPointer(ent->name);
		} else if (ent->name == 0) {
			name = module->nm.name;
		} else {
			name = "invalid?";
		}

		INFO_LOG(Log::Loader, "Exporting ent %d named %s, %d funcs, %d vars, resident %08x", m, name, ent->fcount, ent->vcount, ent->resident);

		if (!Memory::IsValidAddress(ent->resident)) {
			if (ent->fcount + variableCount > 0) {
				WARN_LOG_REPORT(Log::Loader, "Invalid export resident address %08x", ent->resident);
			}
			continue;
		}

		const u32_le *residentPtr = (const u32_le *)Memory::GetPointerUnchecked(ent->resident);
		const u32_le *exportPtr = residentPtr + ent->fcount + variableCount;

		if (ent->size != 4 && ent->unknown1 != 0 && ent->unknown2 != 0) {
			WARN_LOG_REPORT(Log::Loader, "Unexpected export module entry size %d, vcountNew=%08x, unknown1=%08x, unknown2=%08x", ent->size, ent->vcountNew, ent->unknown1, ent->unknown2);
		}

		FuncSymbolExport func;
		strncpy(func.moduleName, name, KERNELOBJECT_MAX_NAME_LENGTH);
		func.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

		for (u32 j = 0; j < ent->fcount; j++) {
			const u32 nid = residentPtr[j];
			const u32 exportAddr = exportPtr[j];

			if (exportAddr & 3) {
				ERROR_LOG(Log::Loader, "Bad fn export address %08x", exportAddr);
				// We should probably reject it, but risky.
			}

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
					WARN_LOG(Log::HLE, "Exporting func from syslib export: %08x", nid);
				}
				module->ExportFunc(func);
			}
		}

		VarSymbolExport var;
		strncpy(var.moduleName, name, KERNELOBJECT_MAX_NAME_LENGTH);
		var.moduleName[KERNELOBJECT_MAX_NAME_LENGTH] = '\0';

		for (u32 j = 0; j < variableCount; j++) {
			const u32 nid = residentPtr[ent->fcount + j];
			const u32 exportAddr = exportPtr[ent->fcount + j];  // These can be unaligned (small varables or char arrays).

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
					WARN_LOG_REPORT(Log::Loader, "Strange value at module_start_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_start_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_start_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_start_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_STOP_THREAD_PARAMETER:
				size = Memory::Read_U32(exportAddr);
				if (size == 0)
					break;
				else if (size != 3)
					WARN_LOG_REPORT(Log::Loader, "Strange value at module_stop_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_stop_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_stop_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_stop_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_REBOOT_BEFORE_THREAD_PARAMETER:
				size = Memory::Read_U32(exportAddr);
				if (size == 0)
					break;
				else if (size != 3)
					WARN_LOG_REPORT(Log::Loader, "Strange value at module_reboot_before_thread_parameter export: %08x", Memory::Read_U32(exportAddr));
				module->nm.module_reboot_before_thread_priority = Memory::Read_U32(exportAddr + 4);
				module->nm.module_reboot_before_thread_stacksize = Memory::Read_U32(exportAddr + 8);
				module->nm.module_reboot_before_thread_attr = Memory::Read_U32(exportAddr + 12);
				break;
			case NID_MODULE_SDK_VERSION:
				DEBUG_LOG(Log::Loader, "Module SDK: %08x", Memory::Read_U32(exportAddr));
				devkitVersion = Memory::Read_U32(exportAddr);
				break;
			default:
				var.nid = nid;
				var.symAddr = exportAddr;
				if (ent->name == 0) {
					WARN_LOG_REPORT(Log::HLE, "Exporting var from syslib export: %08x", nid);
				}
				module->ExportVar(var);
				break;
			}
		}
	}

	if (head) {
		// Here we have the opportunity to check if the information in the header and the information
		// in the module actually corresponds.
		if (devkitVersion != head->devkitversion) {
			WARN_LOG(Log::sceModule, "Devkitversion in module %08x doesn't match header %08x", devkitVersion, head->devkitversion);
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

	delete [] newptr;

	if (!reportedModule && ShouldHLEModule(modinfo->name)) {
		INFO_LOG(Log::sceModule, "Loading module %s with version %04x, devkit %08x", modinfo->name, modinfo->moduleVersion, devkitVersion);

		if (!strcmp(modinfo->name, "sceMpeg_library")) {
			__MpegLoadModule(modinfo->moduleVersion, module->crc);
		}
		if (!strcmp(modinfo->name, "scePsmfP_library") || !strcmp(modinfo->name, "scePsmfPlayer") || !strcmp(modinfo->name, "libpsmfplayer") || !strcmp(modinfo->name, "psmf_jk") || !strcmp(modinfo->name, "jkPsmfP_library")){
			__PsmfPlayerLoadModule(devkitVersion, module->crc);
		}
		if (!strcmp(modinfo->name, "sceATRAC3plus_Library")) {
			__AtracNotifyLoadModule(modinfo->moduleVersion, module->crc, module->GetBSSAddr(), module->nm.bss_size);
		}
	}

	System_Notify(SystemNotification::SYMBOL_MAP_UPDATED);

	u32 moduleSize = sizeof(module->nm);
	char tag[32];
	snprintf(tag, sizeof(tag), "SceModule-%d", module->nm.modid);
	module->modulePtr.ptr = kernelMemory.Alloc(moduleSize, true, tag);

	// Fill the struct.
	if (module->modulePtr.IsValid()) {
		*module->modulePtr = module->nm;
		module->modulePtr.NotifyWrite("KernelModule");
	}

	error = 0;
	return module;
}

SceUID KernelLoadModule(const std::string &filename, std::string *error_string) {
	std::vector<uint8_t> buffer;
	if (pspFileSystem.ReadEntireFile(filename, buffer) < 0)
		return SCE_KERNEL_ERROR_NOFILE;

	u32 error = SCE_KERNEL_ERROR_ILLEGAL_OBJECT;
	u32 magic;
	PSPModule *module = __KernelLoadELFFromPtr(&buffer[0], buffer.size(), 0, false, error_string, &magic, filename, error);

	if (module == nullptr)
		return error;
	return module->GetUID();
}

// filename is only used for dumping etc.
static PSPModule *__KernelLoadModule(u8 *fileptr, size_t fileSize, SceKernelLMOption *options, std::string_view filename, std::string *error_string) {
	PSPModule *module = nullptr;
	// Check for PBP
	if (fileSize >= sizeof(PSP_Header) && memcmp(fileptr, "\0PBP", 4) == 0) {
		// PBP!
		u32_le version;
		memcpy(&version, fileptr + 4, 4);
		u32_le offset0, offsets[16];

		memcpy(&offset0, fileptr + 8, 4);
		int numfiles = (offset0 - 8) / 4;
		if (numfiles <= 0 || numfiles > 16) {
			*error_string = "Invalid PBP header - can't load";
			return nullptr;
		}
		offsets[0] = offset0;
		if (12 + 4 * numfiles > fileSize) {
			*error_string = "ELF file truncated - can't load";
			return nullptr;
		}
		for (int i = 1; i < numfiles; i++) {
			memcpy(&offsets[i], fileptr + 12 + 4 * i, 4);
		}

		if (offsets[6] > fileSize || offsets[5] > offsets[6]) {
			// File is too small to fully contain the ELF! Must have been truncated.
			*error_string = "ELF file truncated - can't load";
			return nullptr;
		}

		u32 magic = 0;
		u8 *temp = nullptr;
		size_t elfSize = offsets[6] - offsets[5];
		if (offsets[5] & 3) {
			// Our loader does NOT like to load from an unaligned address on ARM! Copy to a new block.
			// TODO: Actually, this is not really a concern on modern ARM CPUs, but let's keep it for now.
			temp = new u8[elfSize];

			memcpy(temp, fileptr + offsets[5], elfSize);
			INFO_LOG(Log::Loader, "PBP: ELF unaligned (%d: %d), aligning!", offsets[5], offsets[5] & 3);
		}

		u32 error;
		module = __KernelLoadELFFromPtr(temp ? temp : fileptr + offsets[5], elfSize, PSP_GetDefaultLoadAddress(), false, error_string, &magic, filename, error);

		delete [] temp;
	} else if (fileSize > sizeof(PSP_Header)) {
		u32 error;
		u32 magic = 0;
		module = __KernelLoadELFFromPtr(fileptr, fileSize, PSP_GetDefaultLoadAddress(), false, error_string, &magic, filename, error);
	} else {
		*error_string = "ELF file truncated - can't load";
		return nullptr;
	}

	return module;
}

static void __KernelStartModule(PSPModule *m, int args, const char *argp, SceKernelSMOption *options) {
	m->nm.status = MODULE_STATUS_STARTED;
	if (m->nm.module_start_func != 0 && m->nm.module_start_func != (u32)-1)
	{
		if (m->nm.module_start_func != m->nm.entry_addr)
			WARN_LOG_REPORT(Log::Loader, "Main module has start func (%08x) different from entry (%08x)?", m->nm.module_start_func, m->nm.entry_addr);
		// TODO: Should we try to run both?
		currentMIPS->pc = m->nm.module_start_func;
	}

	SceUID threadID = __KernelSetupRootThread(m->GetUID(), args, argp, options->priority, options->stacksize, options->attribute);
	__KernelSetThreadRA(threadID, NID_MODULERETURN);
}


u32 __KernelGetModuleGP(SceUID uid) {
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(uid, error);
	if (module) {
		return module->nm.gp_value;
	} else {
		return 0;
	}
}

bool KernelModuleIsKernelMode(SceUID uid) {
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(uid, error);
	if (module) {
		return (module->nm.attribute & 0x1000) != 0;
	} else {
		return false;
	}
}

void __KernelLoadReset() {
	// Wipe kernel here, loadexec should reset the entire system
	if (__KernelIsRunning()) {
		u32 error;
		while (!loadedModules.empty()) {
			SceUID moduleID = *loadedModules.begin();
			PSPModule *module = kernelObjects.Get<PSPModule>(moduleID, error);
			if (module) {
				module->Cleanup();
			} else {
				// An invalid module.  We need to remove it or we'll loop forever.
				WARN_LOG(Log::Loader, "Invalid module still marked as loaded on loadexec");
				loadedModules.erase(moduleID);
			}
		}

		Replacement_Shutdown();
		__KernelShutdown();
		// HLE needs to be reset here
		HLEShutdown();
		Replacement_Init();
		HLEInit();
	}

	__KernelModuleInit();
	__KernelInit();
}

bool __KernelLoadExec(const char *filename, u32 paramPtr, std::string *error_string) {
	SceKernelLoadExecParam param{};

	auto paramData = PSPPointer<SceKernelLoadExecParam>::Create(paramPtr);
	if (paramData.IsValid()) {
		param = *paramData;
		paramData.NotifyRead("KernelLoadExec");
	}

	u8 *param_argp = nullptr;
	u8 *param_key = nullptr;
	if (param.args > 0) {
		u32 argpAddr = param.argp;
		param_argp = new u8[param.args];
		Memory::Memcpy(param_argp, argpAddr, param.args, "KernelLoadParam");
	}
	if (param.keyp != 0) {
		u32 keyAddr = param.keyp;
		size_t keylen = strlen(Memory::GetCharPointer(keyAddr)) + 1;
		param_key = new u8[keylen];
		Memory::Memcpy(param_key, keyAddr, (u32)keylen, "KernelLoadParam");
	}

	__KernelLoadReset();

	std::vector<uint8_t> fileData;
	if (pspFileSystem.ReadEntireFile(filename, fileData) < 0) {
		ERROR_LOG(Log::Loader, "Failed to load executable %s - file doesn't exist", filename);
		*error_string = StringFromFormat("Could not find executable %s", filename);
		delete[] param_argp;
		delete[] param_key;
		__KernelShutdown();
		return false;
	}

	size_t size = fileData.size();
	PSPModule *module = __KernelLoadModule(fileData.data(), size, 0, filename, error_string);

	if (!module || module->isFake) {
		if (module) {
			module->Cleanup();
			kernelObjects.Destroy<PSPModule>(module->GetUID());
		}
		ERROR_LOG(Log::Loader, "Failed to load module %s", filename);
		*error_string = "Failed to load executable: " + *error_string;
		delete[] param_argp;
		delete[] param_key;
		return false;
	}

	char moduleName[29] = { 0 };
	int moduleVersion = module->nm.version[0] | (module->nm.version[1] << 8);
	truncate_cpy(moduleName, module->nm.name);
	Reporting::NotifyExecModule(moduleName, moduleVersion, module->crc);

	mipsr4k.pc = module->nm.entry_addr;

	INFO_LOG(Log::Loader, "Module entry: %08x (%s %04x)", mipsr4k.pc, moduleName, moduleVersion);

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

	INFO_LOG(Log::System, "Starting modules...");
	if (paramPtr)
		__KernelStartModule(module, param.args, (const char*)param_argp, &option);
	else
		__KernelStartModule(module, (u32)strlen(filename) + 1, filename, &option);

	__KernelStartIdleThreads(module->GetUID());

	// Wait until plugins are loaded
	module->startingPlugins.clear();
	if (HLEPlugins::Load(module, __KernelGetCurThread())) {
		__KernelWaitCurThread(WAITTYPE_PLUGIN, module->GetUID(), 1, 0, false, "started plugins");
		__KernelReSchedule("Started plugins");
	}

	delete[] param_argp;
	delete[] param_key;

	hleSkipDeadbeef();
	return true;
}

bool __KernelLoadGEDump(std::string_view base_filename, std::string *error_string) {
	__KernelLoadReset();

	constexpr u32 codeStartAddr = PSP_GetUserMemoryBase();
	mipsr4k.pc = codeStartAddr;

	GPURecord::WriteRunDumpCode(codeStartAddr);

	PSPModule *module = new PSPModule();
	kernelObjects.Create(module);
	loadedModules.insert(module->GetUID());
	memset(&module->nm, 0, sizeof(module->nm));
	module->isFake = true;
	module->nm.entry_addr = codeStartAddr;
	module->nm.gp_value = -1;

	SceUID threadID = __KernelSetupRootThread(module->GetUID(), (int)base_filename.size(), base_filename.data(), 0x20, 0x1000, 0);
	__KernelSetThreadRA(threadID, NID_MODULERETURN);

	__KernelStartIdleThreads(module->GetUID());
	return true;
}

int __KernelGPUReplay() {
	// Special ABI: s0 and s1 are the "args".  Not null terminated.
	const char *filenamep = Memory::GetCharPointer(currentMIPS->r[MIPS_REG_S1]);
	if (!filenamep) {
		ERROR_LOG(Log::G3D, "__KernelGPUReplay: Failed to load dump filename");
		Core_Stop();
		return 0;
	}

	std::string filename(filenamep, currentMIPS->r[MIPS_REG_S0]);
	GPURecord::ReplayResult result = GPURecord::RunMountedReplay(filename);

	if (result == GPURecord::ReplayResult::Error) {
		ERROR_LOG(Log::G3D, "__KernelGPUReplay: Failed running replay.");
		Core_Stop();
	}

	if (PSP_CoreParameter().headLess && !PSP_CoreParameter().startBreak) {
		PSPPointer<u8> topaddr;
		u32 linesize = 512;
		__DisplayGetFramebuf(&topaddr, &linesize, nullptr, 0);
		System_SendDebugScreenshot(std::string((const char *)&topaddr[0], linesize * 272), 272);
		Core_Stop();
	}

	// Return 0 for normal looping, 1 for break.
	return hleNoLog(result == GPURecord::ReplayResult::Break ? 1 : 0);
}

int sceKernelLoadExec(const char *filename, u32 paramPtr) {
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
		return hleLogError(Log::Loader, SCE_KERNEL_ERROR_NOFILE, "File does not exist");
	}

	s64 size = (s64)info.size;
	if (!size) {
		return hleLogError(Log::Loader, SCE_KERNEL_ERROR_ILLEGAL_OBJECT, "File is size 0");
	}

	DEBUG_LOG(Log::sceModule, "sceKernelLoadExec(name=%s,...): loading %s", filename, exec_filename.c_str());
	std::string error_string;
	if (!__KernelLoadExec(exec_filename.c_str(), paramPtr, &error_string)) {
		Core_UpdateState(CORE_RUNTIME_ERROR);
		return hleLogError(Log::sceModule, -1, "failed: %s", error_string.c_str());;
	}
	if (gpu) {
		gpu->Reinitialize();
	}
	return hleLogDebug(Log::sceModule, 0);
}

u32 sceKernelLoadModule(const char *name, u32 flags, u32 optionAddr) {
	if (!name) {
		return hleLogError(Log::Loader, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "bad filename");
	}

	for (size_t i = 0; i < ARRAY_SIZE(lieAboutSuccessModules); i++) {
		if (!strcmp(name, lieAboutSuccessModules[i])) {
			PSPModule *module = new PSPModule();
			kernelObjects.Create(module);
			loadedModules.insert(module->GetUID());
			memset(&module->nm, 0, sizeof(module->nm));
			module->isFake = true;
			module->nm.entry_addr = -1;
			module->nm.gp_value = -1;

			u32 moduleSize = sizeof(module->nm);
			char tag[32];
			snprintf(tag, sizeof(tag), "SceModule-%d", module->nm.modid);
			module->modulePtr.ptr = kernelMemory.Alloc(moduleSize, true, tag);

			// Fill the struct.
			if (module->modulePtr.IsValid()) {
				*module->modulePtr = module->nm;
				module->modulePtr.NotifyWrite("KernelModule");
			}

			// TODO: It would be more ideal to allocate memory for this module.
			return hleLogInfo(Log::Loader, module->GetUID(), "created fake module");
		}
	}

	std::vector<uint8_t> fileData;
	if (pspFileSystem.ReadEntireFile(name, fileData) < 0) {
		const u32 error = hleLogError(Log::Loader, SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND, "file does not exist");
		return hleDelayResult(error, "module loaded", 500);
	}
	if (fileData.empty()) {
		const u32 error = hleLogError(Log::Loader, SCE_KERNEL_ERROR_FILEERR, "module file size is 0");
		return hleDelayResult(error, "module loaded", 500);
	}

	// We log before hand because ELF loading logs a bunch.
	DEBUG_LOG(Log::Loader, "sceKernelLoadModule(%s, %08x)", name, flags);

	if (flags != 0) {
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModule: unsupported flags: %08x", flags);
	}
	const SceKernelLMOption *lmoption = 0;
	if (optionAddr) {
		lmoption = (const SceKernelLMOption *)Memory::GetPointer(optionAddr);
		if (lmoption->position < PSP_SMEM_Low || lmoption->position > PSP_SMEM_HighAligned) {
			ERROR_LOG_REPORT(Log::Loader, "sceKernelLoadModule(%s): invalid position (%i)", name, (int)lmoption->position);
			return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE, "module loaded", 500);
		}
		if (lmoption->position == PSP_SMEM_LowAligned || lmoption->position == PSP_SMEM_HighAligned) {
			ERROR_LOG_REPORT(Log::Loader, "sceKernelLoadModule(%s): invalid position (aligned)", name);
			return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ALIGNMENT_SIZE, "module loaded", 500);
		}
		if (lmoption->position == PSP_SMEM_Addr) {
			ERROR_LOG_REPORT(Log::Loader, "sceKernelLoadModule(%s): invalid position (fixed)", name);
			return hleDelayResult(SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED, "module loaded", 500);
		}
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModule: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
	}

	PSPModule *module = nullptr;
	u32 magic;
	u32 error;
	std::string error_string;
	module = __KernelLoadELFFromPtr(fileData.data(), fileData.size(), 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, name, error);

	if (!module) {
		if (magic == 0x46535000) {
			// TODO: What's actually going on here? This is needed to keep Tekken 6 working, the "proper" error breaks it, when it tries to load PARAM.SFO as a module.
			error = -1;
			return hleDelayResult(hleLogError(Log::Loader, error, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic), "module loaded", 500);
		}

		PSPFileInfo info = pspFileSystem.GetFileInfo(name);
		if (info.name == "BOOT.BIN") {
			NOTICE_LOG_REPORT(Log::Loader, "Module %s is blacklisted or undecryptable - we try __KernelLoadExec", name);
			// Name might get deleted.
			const std::string safeName = name;
			if (gpu) {
				gpu->Reinitialize();
			}
			return __KernelLoadExec(safeName.c_str(), 0, &error_string);
		} else {
			return hleDelayResult(hleLogError(Log::Loader, error, "failed to load"), "module loaded", 500);
		}
	}

	if (lmoption) {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModule(name=%s,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),name,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModule(name=%s,flag=%08x,(...))", module->GetUID(), name, flags);
	}

	// TODO: This is not the right timing and probably not the right wait type, just an approximation.
	return hleDelayResult(hleNoLog(module->GetUID()), "module loaded", 500);
}

static u32 sceKernelLoadModuleNpDrm(const char *name, u32 flags, u32 optionAddr) {
	// Just forward it, same parameters so the logging will make sense.
	return sceKernelLoadModule(name, flags, optionAddr);
}

int __KernelStartModule(SceUID moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, SceKernelSMOption *smoption, bool *needsWait) {
	if (needsWait) {
		*needsWait = false;
	}

	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
	if (!module) {
		return error;
	}

	u32 priority = 0x20;
	u32 stacksize = 0x40000;
	int attribute = module->nm.attribute;
	u32 entryAddr = module->nm.entry_addr;

	if (module->nm.module_start_func != 0 && module->nm.module_start_func != (u32)-1) {
		entryAddr = module->nm.module_start_func;
		if (module->nm.module_start_thread_attr != 0)
			attribute = module->nm.module_start_thread_attr;
	}

	if (Memory::IsValidAddress(entryAddr)) {
		if (smoption && smoption->priority > 0) {
			priority = smoption->priority;
		} else if (module->nm.module_start_thread_priority > 0) {
			priority = module->nm.module_start_thread_priority;
		}

		if (smoption && smoption->stacksize > 0) {
			stacksize = smoption->stacksize;
		} else if (module->nm.module_start_thread_stacksize > 0) {
			stacksize = module->nm.module_start_thread_stacksize;
		}

		// TODO: Why do we skip smoption->attribute here?

		SceUID threadID = __KernelCreateThread(module->nm.name, moduleId, entryAddr, priority, stacksize, attribute, 0, (module->nm.attribute & 0x1000) != 0);
		_dbg_assert_(threadID > 0);
		// TOOD: Check the return value and bail?
		__KernelStartThreadValidate(threadID, argsize, argAddr);
		__KernelSetThreadRA(threadID, NID_MODULERETURN);

		if (needsWait) {
			*needsWait = true;
		}
	} else if (entryAddr == 0 || entryAddr == (u32)-1) {
		INFO_LOG(Log::sceModule, "__KernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x): no entry address", moduleId, argsize, argAddr, returnValueAddr);
		module->nm.status = MODULE_STATUS_STARTED;
	} else {
		ERROR_LOG(Log::sceModule, "__KernelStartModule(%d,asize=%08x,aptr=%08x,retptr=%08x): invalid entry address", moduleId, argsize, argAddr, returnValueAddr);
		return -1;
	}
	return moduleId;
}

static u32 sceKernelStartModule(u32 moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, u32 optionAddr) {
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
	if (!module) {
		return hleLogWarning(Log::sceModule, error, "error %08x", error);
	} else if (module->isFake) {
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		return hleLogInfo(Log::sceModule, moduleId, "Faked module");
	} else if (module->nm.status == MODULE_STATUS_STARTED) {
		// TODO: Maybe should be SCE_KERNEL_ERROR_ALREADY_STARTED, but I get SCE_KERNEL_ERROR_ERROR.
		// But I also get crashes...
		return hleLogError(Log::sceModule, SCE_KERNEL_ERROR_ERROR);
	} else {
		bool needsWait;
		auto smoption = PSPPointer<SceKernelSMOption>::Create(optionAddr);
		int ret = __KernelStartModule(moduleId, argsize, argAddr, returnValueAddr, smoption.PtrOrNull(), &needsWait);
		if (needsWait) {
			__KernelWaitCurThread(WAITTYPE_MODULE, moduleId, 1, 0, false, "started module");

			const ModuleWaitingThread mwt = {__KernelGetCurThread(), returnValueAddr};
			module->nm.status = MODULE_STATUS_STARTING;
			module->waitingThreads.push_back(mwt);
		}
		return hleLogInfo(Log::sceModule, ret, "'%.*s'", (int)sizeof(module->nm.name), module->nm.name);
	}
}

static u32 sceKernelStopModule(u32 moduleId, u32 argSize, u32 argAddr, u32 returnValueAddr, u32 optionAddr)
{
	u32 priority = 0x20;
	u32 stacksize = 0x40000;
	u32 attr = 0;

	// TODO: In a lot of cases (even for errors), this should resched.  Needs testing.

	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
	if (!module)
	{
		return hleLogError(Log::sceModule, error, "invalid module id");
	}

	if (module->isFake) {
		if (returnValueAddr)
			Memory::Write_U32(0, returnValueAddr);
		return hleLogInfo(Log::sceModule, 0, "faking");
	}
	if (module->nm.status != MODULE_STATUS_STARTED) {
		return hleLogError(Log::sceModule, SCE_KERNEL_ERROR_ALREADY_STOPPED, "already stopped");
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
			WARN_LOG_REPORT(Log::sceModule, "Stopping module with attr=%x, but options specify 0", attr);
	}

	if (Memory::IsValidAddress(stopFunc))
	{
		SceUID threadID = __KernelCreateThread(module->nm.name, moduleId, stopFunc, priority, stacksize, attr, 0, (module->nm.attribute & 0x1000) != 0);
		_dbg_assert_(threadID > 0);
		// TOOD: Check the return value and bail?
		__KernelStartThreadValidate(threadID, argSize, argAddr);
		__KernelSetThreadRA(threadID, NID_MODULERETURN);
		__KernelWaitCurThread(WAITTYPE_MODULE, moduleId, 1, 0, false, "stopped module");

		const ModuleWaitingThread mwt = {__KernelGetCurThread(), returnValueAddr};
		module->nm.status = MODULE_STATUS_STOPPING;
		module->waitingThreads.push_back(mwt);
	}
	else if (stopFunc == 0)
	{
		module->nm.status = MODULE_STATUS_STOPPED;
		return hleLogInfo(Log::sceModule, 0, "no stop func, skipping");
	}
	else
	{
		module->nm.status = MODULE_STATUS_STOPPED;
		return hleLogError(Log::sceModule, 0, "sceKernelStopModule(%08x, %08x, %08x, %08x, %08x): bad stop func address", moduleId, argSize, argAddr, returnValueAddr, optionAddr);
	}
	return hleLogDebug(Log::sceModule, 0);
}

static u32 sceKernelUnloadModule(u32 moduleId) {
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
	if (!module)
		return hleDelayResult(hleLogError(Log::sceModule, error), "module unloaded", 150);

	module->Cleanup();
	kernelObjects.Destroy<PSPModule>(moduleId);
	return hleDelayResult(hleLogDebug(Log::sceModule, moduleId), "module unloaded", 500);
}

u32 __KernelStopUnloadSelfModuleWithOrWithoutStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr, bool WithStatus) {
	if (loadedModules.size() > 1) {
		if (WithStatus) {
			ERROR_LOG_REPORT(Log::sceModule, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game may have crashed", exitCode, argSize, argp, statusAddr, optionAddr);
		} else {
			// NOTE: The previous "may have crashed" message is not accurate, Splinter Cell Essentials uses this normally when leaving/entering in-game.
			// We should not report this.
			WARN_LOG(Log::sceModule, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x)", exitCode, argSize, argp);
		}
		SceUID moduleID = __KernelGetCurThreadModuleId();
		u32 priority = 0x20;
		u32 stacksize = 0x40000;
		u32 attr = 0;
		// TODO: In a lot of cases (even for errors), this should resched.  Needs testing.

		u32 error;
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleID, error);
		if (!module) {
			if (WithStatus)
				ERROR_LOG(Log::sceModule, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): invalid module id", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				ERROR_LOG(Log::sceModule, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): invalid module id", exitCode, argSize, argp);
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
				WARN_LOG_REPORT(Log::sceModule, "Stopping module with attr=%x, but options specify 0", attr);
		}

		if (Memory::IsValidAddress(stopFunc)) {
			SceUID threadID = __KernelCreateThread(module->nm.name, moduleID, stopFunc, priority, stacksize, attr, 0, (module->nm.attribute & 0x1000) != 0);
			_dbg_assert_(threadID > 0);
			// TOOD: Check the return value and bail?
			__KernelStartThreadValidate(threadID, argSize, argp);
			__KernelSetThreadRA(threadID, NID_MODULERETURN);
			__KernelWaitCurThread(WAITTYPE_MODULE, moduleID, 1, 0, false, "unloadstopped module");

			const ModuleWaitingThread mwt = {__KernelGetCurThread(), statusAddr};
			module->nm.status = MODULE_STATUS_UNLOADING;
			module->waitingThreads.push_back(mwt);
		} else if (stopFunc == 0) {
			if (WithStatus)
				INFO_LOG(Log::sceModule, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): no stop func", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				INFO_LOG(Log::sceModule, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): no stop func", exitCode, argSize, argp);
			hleCall(ThreadManForKernel, int, sceKernelExitDeleteThread, exitCode);
			module->Cleanup();
			kernelObjects.Destroy<PSPModule>(moduleID);
		} else {
			if (WithStatus)
				ERROR_LOG_REPORT(Log::sceModule, "sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): bad stop func address", exitCode, argSize, argp, statusAddr, optionAddr);
			else
				ERROR_LOG_REPORT(Log::sceModule, "sceKernelSelfStopUnloadModule(%08x, %08x, %08x): bad stop func address", exitCode, argSize, argp);
			hleCall(ThreadManForKernel, int, sceKernelExitDeleteThread, exitCode);
			module->Cleanup();
			kernelObjects.Destroy<PSPModule>(moduleID);
		}
	} else {
		if (WithStatus)
			ERROR_LOG_REPORT(Log::sceModule, "UNIMPL sceKernelStopUnloadSelfModuleWithStatus(%08x, %08x, %08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp, statusAddr, optionAddr);
		else
			ERROR_LOG_REPORT(Log::sceModule, "UNIMPL sceKernelSelfStopUnloadModule(%08x, %08x, %08x): game has likely crashed", exitCode, argSize, argp);
	}

	return hleNoLog(0);
}

static u32 sceKernelSelfStopUnloadModule(u32 exitCode, u32 argSize, u32 argp) {
	// Used in Tom Clancy's Splinter Cell Essentials, Ghost in the Shell Stand Alone Complex
	return __KernelStopUnloadSelfModuleWithOrWithoutStatus(exitCode, argSize, argp, 0, 0, false);
}

static u32 sceKernelStopUnloadSelfModuleWithStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr) {
	return __KernelStopUnloadSelfModuleWithOrWithoutStatus(exitCode, argSize, argp, statusAddr, optionAddr, true);
}

void __KernelReturnFromModuleFunc() {
	// Return from the thread as normal.
	hleSkipDeadbeef();
	__KernelReturnFromThread();

	SceUID leftModuleID = __KernelGetCurThreadModuleId();
	SceUID leftThreadID = __KernelGetCurThread();
	int exitStatus = __KernelGetThreadExitStatus(leftThreadID);
	if (exitStatus < 0) {
		ERROR_LOG(Log::sceModule, "%s=GetThreadExitStatus(%d)", KernelErrorToString(exitStatus), leftThreadID);
	}
	// What else should happen with the exit status?

	// Reschedule immediately (to leave the thread) and delete it and its stack.
	__KernelReSchedule("returned from module");
	hleCall(ThreadManForKernel, int, sceKernelDeleteThread, leftThreadID);

	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(leftModuleID, error);
	if (!module) {
		ERROR_LOG_REPORT(Log::sceModule, "Returned from deleted module start/stop func");
		hleNoLogVoid();
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
				hleCall(ThreadManForKernel, int, sceKernelTerminateDeleteThread, it->threadID);
			} else {
				if (it->statusPtr != 0)
					Memory::Write_U32(exitStatus, it->statusPtr);
				__KernelResumeThreadFromWait(it->threadID, module->nm.status == MODULE_STATUS_STARTED ? leftModuleID : 0);
			}
		}
	}
	module->waitingThreads.clear();

	// Check if we need to wake up a plugin waiting thread
	if (module->pluginWaitingThread) {
		u32 error;
		PSPThread *plugin_waiting_thread = kernelObjects.Get<PSPThread>(module->pluginWaitingThread, error);
		if (plugin_waiting_thread && HLEKernel::VerifyWait(module->pluginWaitingThread, WAITTYPE_PLUGIN, plugin_waiting_thread->moduleId)) {
			PSPModule *plugin_waiting_module = kernelObjects.Get<PSPModule>(plugin_waiting_thread->moduleId, error);
			if (plugin_waiting_module) {
				for (auto it = plugin_waiting_module->startingPlugins.begin(), end = plugin_waiting_module->startingPlugins.end(); it < end; ++it) {
					if (*it == leftModuleID) {
						plugin_waiting_module->startingPlugins.erase(it);
						break;
					}
				}
				if (plugin_waiting_module->startingPlugins.empty()) {
					INFO_LOG(Log::sceModule, "Resuming LoadExec thread %d", module->pluginWaitingThread);
					__KernelResumeThreadFromWait(module->pluginWaitingThread, 0);
				} else {
					INFO_LOG(Log::sceModule, "LoadExec thread %d still waiting for %d plugin(s)", module->pluginWaitingThread, (int)plugin_waiting_module->startingPlugins.size());
				}
			}
		}
	}

	if (module->nm.status == MODULE_STATUS_UNLOADING) {
		// TODO: Delete the waiting thread?
		module->Cleanup();
		kernelObjects.Destroy<PSPModule>(leftModuleID);
	}
	hleNoLogVoid();
}

struct GetModuleIdByAddressArg
{
	u32 addr;
	SceUID result;
};

static u32 sceKernelGetModuleIdByAddress(u32 moduleAddr)
{
	GetModuleIdByAddressArg state;
	state.addr = moduleAddr;
	state.result = SCE_KERNEL_ERROR_UNKNOWN_MODULE;

	kernelObjects.Iterate<PSPModule>([&state](int id, PSPModule *module) -> bool {
		const u32 start = module->memoryBlockAddr, size = module->memoryBlockSize;
		if (start != 0 && start <= state.addr && start + size > state.addr) {
			state.result = module->GetUID();
			return false;
		}
		return true;
	});

	if (state.result == (SceUID)SCE_KERNEL_ERROR_UNKNOWN_MODULE) {
		return hleLogError(Log::sceModule, state.result, "module not found at address");
	} else {
		return hleLogDebugOrError(Log::sceModule, state.result, "%08x", state.result);
	}
}

static u32 sceKernelGetModuleId()
{
	return hleLogDebug(Log::sceModule, __KernelGetCurThreadModuleId());
}

u32 sceKernelFindModuleByUID(u32 uid)
{
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(uid, error);
	if (!module || module->isFake) {
		return hleLogError(Log::sceModule, 0, "Module Not Found or Fake");
	}
	return hleLogInfo(Log::sceModule, module->modulePtr.ptr);
}

u32 sceKernelFindModuleByName(const char *name)
{
	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module)
			continue;
		if (strcmp(name, module->nm.name) == 0) {
			if (!module->isFake) {
				INFO_LOG(Log::sceModule, "%d = sceKernelFindModuleByName(%s)", module->modulePtr.ptr, name);
				return hleLogInfo(Log::sceModule, module->modulePtr.ptr);
			} else {
				return hleDelayResult(hleLogWarning(Log::sceModule, 0, "Module Fake"), "Module Fake", 1000 * 1000);
			}
		}
	}
	return hleLogWarning(Log::sceModule, 0, "Module Not Found");
}

// The id in question here is a file handle.
static u32 sceKernelLoadModuleByID(u32 id, u32 flags, u32 lmoptionPtr) {
	u32 error;
	u32 handle = __IoGetFileHandleFromId(id, error);
	if (handle == (u32)-1) {
		return hleLogError(Log::sceModule, error, "couldn't open file");
	}
	if (flags != 0) {
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModuleByID: unsupported flags: %08x", flags);
	}
	const SceKernelLMOption *lmoption = 0;
	if (lmoptionPtr) {
		lmoption = (const SceKernelLMOption *)Memory::GetPointer(lmoptionPtr);
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModuleByID: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
	}
	u32 pos = (u32)pspFileSystem.SeekFile(handle, 0, FILEMOVE_CURRENT);
	size_t size = pspFileSystem.SeekFile(handle, 0, FILEMOVE_END);
	std::string error_string;
	pspFileSystem.SeekFile(handle, pos, FILEMOVE_BEGIN);
	PSPModule *module = nullptr;
	u8 *temp = new u8[size - pos];
	pspFileSystem.ReadFile(handle, temp, size - pos);

	u32 magic;
	module = __KernelLoadELFFromPtr(temp, size - pos, 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, "", error);
	delete [] temp;

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			return hleLogError(Log::Loader, error, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
		}

		if ((int)error >= 0)
		{
			// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run..
			// Let's just act as if it worked.
			NOTICE_LOG(Log::Loader, "Module %d is blacklisted or undecryptable - we lie about success", id);
			return 1;
		}
		else
		{
			NOTICE_LOG(Log::Loader, "Module %d failed to load: %08x", id, error);
			return hleLogError(Log::Loader, error);
		}
	}

	if (lmoption) {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModuleByID(%d,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),id,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModuleByID(%d,flag=%08x,(...))", module->GetUID(), id, flags);
	}

	return hleNoLog(module->GetUID());
}

static u32 sceKernelLoadModuleDNAS(const char *name, u32 flags)
{
	ERROR_LOG_REPORT(Log::sceModule, "UNIMPL 0=sceKernelLoadModuleDNAS()");
	return hleNoLog(0);
}

// Pretty sure this is a badly brute-forced function name...
static SceUID sceKernelLoadModuleBufferUsbWlan(u32 size, u32 bufPtr, u32 flags, u32 lmoptionPtr)
{
	if (flags != 0) {
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModuleBufferUsbWlan: unsupported flags: %08x", flags);
	}
	const SceKernelLMOption *lmoption = 0;
	if (lmoptionPtr) {
		lmoption = (const SceKernelLMOption *)Memory::GetPointer(lmoptionPtr);
		WARN_LOG_REPORT(Log::Loader, "sceKernelLoadModuleBufferUsbWlan: unsupported options size=%08x, flags=%08x, pos=%d, access=%d, data=%d, text=%d", lmoption->size, lmoption->flags, lmoption->position, lmoption->access, lmoption->mpiddata, lmoption->mpidtext);
	}
	std::string error_string;
	PSPModule *module = nullptr;
	u32 magic;
	u32 error;

	// For dumping only.
	char fakeDebugFilename[512];
	snprintf(fakeDebugFilename, sizeof(fakeDebugFilename), "moduleByPtr_%08x_%d", bufPtr, (int)size);

	module = __KernelLoadELFFromPtr(Memory::GetPointer(bufPtr), size, 0, lmoption ? lmoption->position == PSP_SMEM_High : false, &error_string, &magic, fakeDebugFilename, error);

	if (!module) {
		// Some games try to load strange stuff as PARAM.SFO as modules and expect it to fail.
		// This checks for the SFO magic number.
		if (magic == 0x46535000) {
			return hleLogError(Log::Loader, error, "Game tried to load an SFO as a module. Go figure? Magic = %08x", magic);
		}

		if ((int)error >= 0) {
			// Module was blacklisted or couldn't be decrypted, which means it's a kernel module we don't want to run..
			// Let's just act as if it worked.
			NOTICE_LOG(Log::Loader, "Module is blacklisted or undecryptable - we lie about success");
			return 1;
		}
		else
		{
			NOTICE_LOG(Log::Loader, "Module failed to load: %08x", error);
			return error;
		}
	}

	if (lmoption) {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModuleBufferUsbWlan(%x,%08x,flag=%08x,%08x,%08x,%08x,position = %08x)",
			module->GetUID(),size,bufPtr,flags,
			lmoption->size,lmoption->mpidtext,lmoption->mpiddata,lmoption->position);
	} else {
		INFO_LOG(Log::sceModule,"%i=sceKernelLoadModuleBufferUsbWlan(%x,%08x,flag=%08x,(...))", module->GetUID(), size,bufPtr, flags);
	}

	return hleNoLog(module->GetUID());
}

static u32 sceKernelQueryModuleInfo(u32 uid, u32 infoAddr)
{
	DEBUG_LOG(Log::sceModule, "sceKernelQueryModuleInfo(%i, %08x)", uid, infoAddr);
	u32 error;
	PSPModule *module = kernelObjects.Get<PSPModule>(uid, error);
	if (!module) {
		return error;
	}
	if (!Memory::IsValidAddress(infoAddr)) {
		return hleLogError(Log::sceModule, -1, "bad infoAddr");
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

	return hleNoLog(0);
}

static u32 sceKernelGetModuleIdList(u32 resultBuffer, u32 resultBufferSize, u32 idCountAddr)
{
	ERROR_LOG(Log::sceModule, "UNTESTED sceKernelGetModuleIdList(%08x, %i, %08x)", resultBuffer, resultBufferSize, idCountAddr);
	
	int idCount = 0;
	u32 resultBufferOffset = 0;

	u32 error;
	for (SceUID moduleId : loadedModules) {
		PSPModule *module = kernelObjects.Get<PSPModule>(moduleId, error);
		if (!module->isFake) {
			if (resultBufferOffset < resultBufferSize) {
				Memory::Write_U32(module->GetUID(), resultBuffer + resultBufferOffset);
				resultBufferOffset += 4;
			}
			idCount++;
		}  // Actually, should we return fake modules too? They wouldn't be fake on the real hardware. Not like any games use this function though.
	}

	Memory::Write_U32(idCount, idCountAddr);
	
	return hleNoLog(0);
}

//fix for tiger x dragon
static u32 sceKernelLoadModuleForLoadExecVSHDisc(const char *name, u32 flags, u32 optionAddr) {
	return sceKernelLoadModule(name, flags, optionAddr);
}

const HLEFunction ModuleMgrForUser[] = {
	{0X977DE386, &WrapU_CUU<sceKernelLoadModule>,                       "sceKernelLoadModule",                     'x', "sxx"    },
	{0XB7F46618, &WrapU_UUU<sceKernelLoadModuleByID>,                   "sceKernelLoadModuleByID",                 'x', "xxx"    },
	{0X50F0C1EC, &WrapU_UUUUU<sceKernelStartModule>,                    "sceKernelStartModule",                    'v', "xxxxx", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
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


const HLEFunction ModuleMgrForKernel[] = {
	{0x50F0C1EC, &WrapU_UUUUU<sceKernelStartModule>,                    "sceKernelStartModule",                    'v', "xxxxx", HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED | HLE_KERNEL_SYSCALL },
	{0x977DE386, &WrapU_CUU<sceKernelLoadModule>,                       "sceKernelLoadModule",                     'x', "sxx",   HLE_KERNEL_SYSCALL },
	{0xA1A78C58, &WrapU_CUU<sceKernelLoadModuleForLoadExecVSHDisc>,     "sceKernelLoadModuleForLoadExecVSHDisc",   'x', "sxx",   HLE_KERNEL_SYSCALL }, //fix for tiger x dragon
	{0xCC1D3699, &WrapU_UUU<sceKernelSelfStopUnloadModule>,             "sceKernelStopUnloadSelfModule",           'x', "xxx",   HLE_KERNEL_SYSCALL }, // used in Dissidia final fantasy chinese patch
	{0XD1FF982A, &WrapU_UUUUU<sceKernelStopModule>,                     "sceKernelStopModule",                     'x', "xxxxx", HLE_KERNEL_SYSCALL | HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED }, // used in Dissidia final fantasy chinese patch
	{0x748CBED9, &WrapU_UU<sceKernelQueryModuleInfo>,                   "sceKernelQueryModuleInfo",                'x', "xx",    HLE_KERNEL_SYSCALL },
	{0x644395E2, &WrapU_UUU<sceKernelGetModuleIdList>,                  "sceKernelGetModuleIdList",                'x', "xxx",   HLE_KERNEL_SYSCALL },
	{0X2E0911AA, &WrapU_U<sceKernelUnloadModule>,                       "sceKernelUnloadModule",                   'x', "x" ,    HLE_KERNEL_SYSCALL },
};

void Register_ModuleMgrForUser() {
	RegisterHLEModule("ModuleMgrForUser", ARRAY_SIZE(ModuleMgrForUser), ModuleMgrForUser);
}

void Register_ModuleMgrForKernel() {
	RegisterHLEModule("ModuleMgrForKernel", ARRAY_SIZE(ModuleMgrForKernel), ModuleMgrForKernel);		
}
