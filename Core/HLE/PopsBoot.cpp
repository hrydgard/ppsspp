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

// PS1 EBOOTs, run by the firmware's own POPS manager (popsman.prx) and emulator (pops_0Xg.prx).
// This stands in for the parts of the POPS boot PPSSPP doesn't run: the kernel's init, and the
// Media Engine the manager's ME loop runs on. See docs/POPS.md.

#include <cstring>
#include <memory>
#include <string>


#include "Common/Serialize/Serializer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/PopsBoot.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HW/PopsMMIO.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"

static bool g_popsBoot = false;
static std::unique_ptr<HLEHelperThread> g_popsInitThread;
static u32 g_popsInitPath = 0;

// The 6.60 POPS manager (6.61's has the same code), from its base: scePopsManLoadModule. Its ME loop
// (+2F88) runs on the emulated ME.
enum : u32 {
	POPSMAN_LOAD_MODULE = 0x1E20,
};

// What the places used here hold in the 6.60/6.61 manager. Checked when it's loaded.
static const struct {
	u32 offset;
	u32 original;
} POPSMAN_CODE[] = {
	{ POPSMAN_LOAD_MODULE, 0x27BDFEE0 },  // addiu sp, sp, -0x120
};

static const u32 SCE_POPSMAN_LOAD_MODULE_NID = 0x29B3FB24;

void __PopsBootInit() {
	g_popsBoot = true;
}

void __PopsBootShutdown() {
	g_popsBoot = false;
	// The kernel objects and memory are already gone with the kernel.
	if (g_popsInitThread) {
		g_popsInitThread->Forget();
	}
	g_popsInitThread.reset();
	g_popsInitPath = 0;
}

bool PopsBootActive() {
	return g_popsBoot;
}

static void DoHelperThread(PointerWrap &p, std::unique_ptr<HLEHelperThread> &thread) {
	bool has = thread != nullptr;
	Do(p, has);
	if (p.mode == PointerWrap::MODE_READ) {
		// The kernel objects come back with the kernel's own state.
		if (thread) {
			thread->Forget();
		}
		thread.reset(has ? new HLEHelperThread() : nullptr);
	}
	if (thread) {
		thread->DoState(p);
	}
}

void __PopsBootDoState(PointerWrap &p) {
	auto s = p.Section("PopsBoot", 0, 1);
	if (!s) {
		if (p.mode == PointerWrap::MODE_READ) {
			__PopsBootShutdown();
		}
		return;
	}
	Do(p, g_popsBoot);
	Do(p, g_popsInitPath);
	DoHelperThread(p, g_popsInitThread);
}

static u32 PopsManBase() {
	const u32 loadModule = KernelFindModuleExport("scePopsMan", SCE_POPSMAN_LOAD_MODULE_NID);
	return loadModule ? loadModule - POPSMAN_LOAD_MODULE : 0;
}

// The user modules the 6.60 POPS boot configuration (pspbtcnf) starts after the manager.
void __PopsBootLoadModules() {
	static const char *const paths[] = {
		"flash0:/vsh/module/libfont_hv.prx",
		"flash0:/vsh/module/pafmini.prx",
		"flash0:/vsh/module/libpspvmc.prx",
	};
	for (const char *path : paths) {
		std::string error;
		const SceUID moduleId = KernelLoadModule(path, &error);
		if (moduleId < 0) {
			WARN_LOG(Log::sceKernel, "POPS: failed to load %s: %s", path, error.c_str());
			continue;
		}
		if (__KernelStartModule(moduleId, 0, 0, 0, nullptr, nullptr) < 0) {
			WARN_LOG(Log::sceKernel, "POPS: failed to start %s", path);
		}
	}
}

void __PopsBootPatchEmulator(u32 textAddr, u32 textSize) {
}

// The installed firmware's version, from flash0:/vsh/etc/version.txt ("release:6.61:"). Empty if there's
// none, as in a partial install.
static std::string FirmwareVersion() {
	std::vector<u8> data;
	if (pspFileSystem.ReadEntireFile("flash0:/vsh/etc/version.txt", data, true) < 0)
		return "";
	const std::string text(data.begin(), data.end());
	const size_t start = text.find("release:");
	if (start == std::string::npos)
		return "";
	const size_t end = text.find(':', start + 8);
	return end == std::string::npos ? "" : text.substr(start + 8, end - start - 8);
}

bool __PopsBootStartInit(FileLoader *pbp, const std::string &gamePath, std::string *error) {
	const u32 base = PopsManBase();
	bool known = base != 0;
	for (const auto &code : POPSMAN_CODE) {
		known = known && Memory::ReadUnchecked_U32(base + code.offset) == code.original;
	}
	// 6.35-6.39's manager has the same code at those places, but imports kernel functions by other
	// NIDs.
	const std::string version = FirmwareVersion();
	if (!version.empty() && version != "6.60" && version != "6.61") {
		known = false;
	}
	if (!known) {
		auto er = GetI18NCategory(I18NCat::ERRORS);
		*error = er->T("PS1FirmwareVersion", "So far, PS1 games only run with firmware 6.60 or 6.61, and a different version is installed. Install one of those from an official PSP update file in Settings > Tools > PSP Firmware.");
		return false;
	}
	const u32 loadModule = base + POPSMAN_LOAD_MODULE;

	const u32 pathSize = (u32)gamePath.size() + 1;
	u32 allocSize = pathSize;
	g_popsInitPath = kernelMemory.Alloc(allocSize, false, "PopsInitPath");
	Memory::Memcpy(g_popsInitPath, gamePath.c_str(), pathSize, "PopsInitPath");

	// The kernel's init: scePopsManLoadModule(path, 0). Starts with a1 = its copy of the path, below
	// the manager's priority so it runs once module_start is done.
	const u32 code[] = {
		0x00A02021,                // move a0, a1
		MIPS_MAKE_J(loadModule),   // returns to the thread's end
		0x24050000,                // li a1, 0
	};
	g_popsInitThread.reset(new HLEHelperThread("ScePopsInit", code, (u32)ARRAY_SIZE(code), 0x40, 0x4000));
	g_popsInitThread->Start(pathSize, g_popsInitPath);
	INFO_LOG(Log::sceKernel, "POPS: init will call scePopsManLoadModule(%s, 0)", gamePath.c_str());
	return true;
}
