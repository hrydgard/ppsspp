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

#include "zlib.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/ELF/PBPReader.h"
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
static bool g_popsConvertedPBP = false;
static std::unique_ptr<HLEHelperThread> g_popsInitThread;
static u32 g_popsInitPath = 0;

// The 6.60 POPS manager (6.61's has the same code), from its base: scePopsManLoadModule, and the
// checks replaced for store and converted games. Its ME loop (+2F88) runs on the emulated ME.
enum : u32 {
	POPSMAN_LOAD_MODULE = 0x1E20,
	// Whether data matches its MAC: its own, and the one it exports as sceMeAudio_F6637A72.
	POPSMAN_CHECK_MAC = 0x0A28,
	POPSMAN_CHECK_MAC_EXPORT = 0x0AB8,
};

// What the places used here hold in the 6.60/6.61 manager. Checked when it's loaded.
static const struct {
	u32 offset;
	u32 original;
} POPSMAN_CODE[] = {
	{ POPSMAN_LOAD_MODULE, 0x27BDFEE0 },  // addiu sp, sp, -0x120
	{ POPSMAN_CHECK_MAC, 0x27BDFFC0 },  // addiu sp, sp, -0x40
	{ POPSMAN_CHECK_MAC_EXPORT, 0x27BDFFB0 },
};

static const u32 SCE_POPSMAN_LOAD_MODULE_NID = 0x29B3FB24;

void __PopsBootInit() {
	g_popsBoot = true;
}

void __PopsBootShutdown() {
	g_popsBoot = false;
	g_popsConvertedPBP = false;
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
	Do(p, g_popsConvertedPBP);
	Do(p, g_popsInitPath);
	DoHelperThread(p, g_popsInitThread);
}

static u32 PopsManBase() {
	const u32 loadModule = KernelFindModuleExport("scePopsMan", SCE_POPSMAN_LOAD_MODULE_NID);
	return loadModule ? loadModule - POPSMAN_LOAD_MODULE : 0;
}

// A converted PBP's disc block, raw DEFLATE. Returns the output capacity - 1, as the emulator's
// own decoder does.
static int __PopsDeflateDecompress(u32 destSize, u32 src, u32 dest) {
	if (!Memory::IsValidRange(dest, destSize) || !Memory::IsValidAddress(src)) {
		return hleLogError(Log::sceKernel, -1, "bad buffers");
	}
	z_stream stream{};
	stream.next_in = (Bytef *)Memory::GetPointerUnchecked(src);
	stream.avail_in = (uInt)Memory::ClampValidSizeAt(src, destSize * 2);
	stream.next_out = (Bytef *)Memory::GetPointerWriteUnchecked(dest);
	stream.avail_out = (uInt)destSize;
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
		return hleLogError(Log::sceKernel, -1, "inflateInit2 failed");
	}
	const int err = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	if (err != Z_STREAM_END) {
		return hleLogError(Log::sceKernel, -1, "inflate failed %d", err);
	}
	NotifyMemInfo(MemBlockFlags::WRITE, dest, (u32)stream.total_out, "PopsDeflate");
	return hleLogDebug(Log::sceKernel, (int)(destSize - 1));
}

// A PS1 PBP's disc image starts after the PSAR's header: 0x400 in, or 0x200 for a multi-disc one.
static bool ReadDiscImage(const PBPReader &pbp, size_t size, u8 *out) {
	char psar[8];
	if (!pbp.ReadSubFile(PBP_UNKNOWN_PSAR, 0, sizeof(psar), psar)) {
		return false;
	}
	return pbp.ReadSubFile(PBP_UNKNOWN_PSAR, memcmp(psar, "PSTITLE", 7) == 0 ? 0x200 : 0x400, size, out);
}

const HLEFunction PopsBootCalls[] = {
	{ 0x13370006, &WrapI_UUU<__PopsDeflateDecompress>,  "__PopsDeflateDecompress", 'i', "xxx" },
};

void Register_PopsBootCalls() {
	RegisterHLEModule("PopsBootCalls", ARRAY_SIZE(PopsBootCalls), PopsBootCalls);
}

static void PatchCall(u32 addr, const char *func) {
	Memory::WriteUnchecked_U32(MIPS_MAKE_SYSCALL("PopsBootCalls", func), addr);
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

// A store PBP's disc image is a PGD, a converted one's isn't.
static bool IsConvertedPBP(FileLoader *loader) {
	PBPReader pbp(loader);
	u8 magic[4];
	return pbp.IsValid() && ReadDiscImage(pbp, sizeof(magic), magic) && memcmp(magic, "\0PGD", 4) != 0;
}

// Sends code at addr to a syscall through a trampoline, then on to resume. The emulator takes
// syscalls in its own text for import stubs, so they can't go in place.
static void PatchTrampoline(u32 addr, const char *func, u32 delaySlotOp, u32 resume) {
	u32 size = 16;
	const u32 trampoline = kernelMemory.Alloc(size, false, func);
	PatchCall(trampoline, func);
	Memory::WriteUnchecked_U32(delaySlotOp, trampoline + 4);
	Memory::WriteUnchecked_U32(MIPS_MAKE_J(resume), trampoline + 8);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), trampoline + 12);
	Memory::WriteUnchecked_U32(MIPS_MAKE_J(trampoline), addr);
	currentMIPS->InvalidateICacheRangeDeferred(addr, 8);
	currentMIPS->InvalidateICacheRangeDeferred(trampoline, size);
}

void __PopsBootPatchEmulator(u32 textAddr, u32 textSize, u32 decodeStub) {
	if (!g_popsConvertedPBP) {
		return;
	}
	if (!decodeStub) {
		ERROR_LOG(Log::sceKernel, "POPS: the emulator doesn't import scePopsMan_0090B2C8, the disc won't read");
		return;
	}
	// A converted PBP's disc blocks are raw DEFLATE. As CFW's popcorn does, send the decoder calls
	// (the jal two words after each "lw a2, 0xC(s3)") to the scePopsMan_0090B2C8 import, which
	// inflates.
	int sites = 0;
	for (u32 addr = textAddr; addr + 8 < textAddr + textSize; addr += 4) {
		const u32 op = Memory::ReadUnchecked_U32(addr);
		const u32 next = Memory::ReadUnchecked_U32(addr + 4);
		if (op == 0x8E66000C) {
			Memory::WriteUnchecked_U32(MIPS_MAKE_JAL(decodeStub), addr + 8);
			sites++;
		} else if ((op == 0x14C00014 && next == 0x24E2FFFF) || (op == 0x14A00014 && next == 0x24C2FFFF)) {
			// Also popcorn's: the track index's length doesn't count the CD audio tracks a converted
			// image adds, so always take the full-length branch (b +0x14; addiu v0, aN, 0).
			Memory::WriteUnchecked_U32(0x10000014, addr);
			Memory::WriteUnchecked_U32(next & 0xFFFF0000, addr + 4);
		}
	}
	Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), decodeStub);
	PatchCall(decodeStub + 4, "__PopsDeflateDecompress");
	currentMIPS->InvalidateICacheRangeDeferred(textAddr, textSize);
	currentMIPS->InvalidateICacheRangeDeferred(decodeStub, 8);
	INFO_LOG(Log::sceKernel, "POPS: converted PBP, %d disc decoder calls now inflate", sites);
}

static void PatchReturn(u32 addr, u32 delaySlotOp) {
	Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), addr);
	Memory::WriteUnchecked_U32(delaySlotOp, addr + 4);
	currentMIPS->InvalidateICacheRangeDeferred(addr, 8);
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
	g_popsConvertedPBP = IsConvertedPBP(pbp);
	const u32 loadModule = base + POPSMAN_LOAD_MODULE;
	if (g_popsConvertedPBP) {
		// A converted PBP has no MACs. As popcorn does, take its data as authentic.
		PatchReturn(base + POPSMAN_CHECK_MAC, 0x24020001);  // li v0, 1
		PatchReturn(base + POPSMAN_CHECK_MAC_EXPORT, 0x24020001);
	}

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
