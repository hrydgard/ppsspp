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

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <set>

#include "Core/HLE/sceKernel.h"
#include "Core/MemMap.h"

class PointerWrap;
struct SceKernelSMOption;

struct PspModuleInfo {
	u16_le moduleAttrs; //0x0000 User Mode, 0x1000 Kernel Mode
	u16_le moduleVersion;
	// 28 bytes of module name, packed with 0's.
	char name[28];
	u32_le gp;               // ptr to MIPS GOT data  (global offset table)
	u32_le libent;           // ptr to .lib.ent section
	u32_le libentend;        // ptr to end of .lib.ent section
	u32_le libstub;          // ptr to .lib.stub section
	u32_le libstubend;       // ptr to end of .lib.stub section
};

enum NativeModuleStatus {
	MODULE_STATUS_STARTING = 4,
	MODULE_STATUS_STARTED = 5,
	MODULE_STATUS_STOPPING = 6,
	MODULE_STATUS_STOPPED = 7,
	MODULE_STATUS_UNLOADING = 8,
};

const char *NativeModuleStatusToString(NativeModuleStatus status);

struct NativeModule {
	u32_le next;
	u16_le attribute;
	u8 version[2];
	char name[28];
	u32_le status;
	u32_le unk1;
	u32_le modid; // 0x2C
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

struct WriteVarSymbolState;

struct ModuleWaitingThread {
	SceUID threadID;
	u32 statusPtr;
};

class PSPModule : public KernelObject {
public:
	~PSPModule();
	const char *GetName() override { return nm.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "Module"; }
	void GetQuickInfo(char *ptr, int size) override;
	void GetLongInfo(char *ptr, int bufSize) const override;
	static u32 GetMissingErrorCode();
	static int GetStaticIDType() { return PPSSPP_KERNEL_TMID_Module; }
	int GetIDType() const override { return PPSSPP_KERNEL_TMID_Module; }

	u32 GetDataAddr() const {
		return nm.text_addr + nm.text_size;
	}
	u32 GetBSSAddr() const {
		return nm.text_addr + nm.text_size + nm.data_size;
	}

	void DoState(PointerWrap &p) override;

	// We don't do this in the destructor to avoid annoying messages on game shutdown.
	void Cleanup();

	void ImportFunc(const FuncSymbolImport &func, bool reimporting);
	void ImportVar(WriteVarSymbolState &state, const VarSymbolImport &var);
	void ExportFunc(const FuncSymbolExport &func);
	void ExportVar(const VarSymbolExport &var);

	template <typename T>
	void RebuildExpList(const std::vector<T> &list) {
		for (size_t i = 0; i < list.size(); ++i) {
			expModuleNames.insert(list[i].moduleName);
		}
	}

	template <typename T>
	void RebuildImpList(const std::vector<T> &list) {
		for (size_t i = 0; i < list.size(); ++i) {
			impModuleNames.insert(list[i].moduleName);
		}
	}

	void RebuildImpExpModuleNames() {
		impModuleNames.clear();
		expModuleNames.clear();
		RebuildExpList(exportedFuncs);
		RebuildImpList(importedFuncs);
		RebuildExpList(exportedVars);
		RebuildImpList(importedVars);
	}

	bool ImportsOrExportsModuleName(const std::string &moduleName) {
		return impModuleNames.find(moduleName) != impModuleNames.end() ||
			expModuleNames.find(moduleName) != expModuleNames.end();
	}

	NativeModule nm{};
	std::vector<ModuleWaitingThread> waitingThreads;

	// From the plugin's perspective, this is the reference to the thread started by LoadExec
	SceUID pluginWaitingThread = 0;
	// Thread started by LoadExec is waiting for these plugins
	std::vector<SceUID> startingPlugins;

	// TODO: Should we store these grouped by moduleName instead? Seems more reasonable.
	std::vector<FuncSymbolExport> exportedFuncs;
	std::vector<FuncSymbolImport> importedFuncs;
	std::vector<VarSymbolExport> exportedVars;
	std::vector<VarSymbolImport> importedVars;
	std::set<std::string> impModuleNames;
	std::set<std::string> expModuleNames;

	// Keep track of the code region so we can throw out analysis results
	// when unloaded.
	u32 textStart = 0;
	u32 textEnd = 0;

	// Keep track of the libstub pointers so we can recheck on load state.
	u32 libstub = 0;
	u32 libstubend = 0;

	u32 memoryBlockAddr = 0;
	u32 memoryBlockSize = 0;
	u32 crc = 0;
	PSPPointer<NativeModule> modulePtr{};
	bool isFake = false;
};

KernelObject *__KernelModuleObject();
void __KernelModuleDoState(PointerWrap &p);
void __KernelModuleShutdown();

u32 __KernelGetModuleGP(SceUID module);
bool KernelModuleIsKernelMode(SceUID module);
bool __KernelLoadGEDump(std::string_view base_filename, std::string *error_string);
bool __KernelLoadExec(const char *filename, u32 paramPtr, std::string *error_string);
int __KernelGPUReplay();
void __KernelReturnFromModuleFunc();
SceUID KernelLoadModule(const std::string &filename, std::string *error_string);
int __KernelStartModule(SceUID moduleId, u32 argsize, u32 argAddr, u32 returnValueAddr, SceKernelSMOption *smoption, bool *needsWait);
u32 __KernelStopUnloadSelfModuleWithOrWithoutStatus(u32 exitCode, u32 argSize, u32 argp, u32 statusAddr, u32 optionAddr, bool WithStatus);
u32 sceKernelFindModuleByUID(u32 uid);

void Register_ModuleMgrForUser();
void Register_ModuleMgrForKernel();

// Expose for use by KUBridge.
u32 sceKernelLoadModule(const char *name, u32 flags, u32 optionAddr);
