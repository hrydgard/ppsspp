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
#include "ext/libkirk/amctrl.h"

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
#include "Core/HLE/sceChnnlsv.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HW/PopsMMIO.h"
#include "Core/Loaders.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/Screenshot.h"
#include "Core/System.h"

static bool g_popsBoot = false;
static bool g_popsConvertedPBP = false;
static bool g_popsIcon0Usable = true;
// The game's PBP on the host.
static Path g_popsPBPPath;
static std::unique_ptr<HLEHelperThread> g_popsInitThread;
static u32 g_popsInitPath = 0;

// The places in the POPS manager used here, from its base: scePopsManLoadModule, and the checks
// replaced for store and converted games. Its ME loop (6.60's +2F88) runs on the emulated ME.
struct PopsManLayout {
	u32 loadModule;
	u32 loadModuleFrame;  // Its first instruction, addiu sp, sp, -frame.
	// The license check (path, keyOut, fingerprintOut). Its first success latches the version key and
	// the content ID fingerprint (6.60's +0124).
	u32 keyProbe;
	u32 versionKey;
	u32 keyLatched;
	u32 keyFingerprint;
	// Whether data matches its MAC: its own, and the one it exports to the emulator (6.60's
	// sceMeAudio_F6637A72).
	u32 checkMac;
	u32 checkMacExport;
};

// The managers have the same code at these places, and older ones import kernel functions by older
// NIDs (see GetHLENidAlias()).
static const PopsManLayout POPSMAN_LAYOUTS[] = {
	{ 0x1E20, 0x120, 0x266C, 0x49E0, 0x4A18, 0x4A1C, 0x0A28, 0x0AB8 },  // 6.35-6.61
	{ 0x1E48, 0x120, 0x2694, 0x4AE0, 0x4B18, 0x4B1C, 0x0A28, 0x0AB8 },  // 6.30-6.31
	{ 0x1E48, 0x130, 0x26F8, 0x4B30, 0x4B68, 0x4B6C, 0x0A28, 0x0AB8 },  // 6.20
	{ 0x19E8, 0x130, 0x2270, 0x46D0, 0x4704, 0x4708, 0x07B8, 0x0848 },  // 6.10
};
static int g_popsManLayout = 0;

static const u32 SCE_POPSMAN_LOAD_MODULE_NID = 0x29B3FB24;

void __PopsBootInit() {
	g_popsBoot = true;
}

void __PopsBootShutdown() {
	g_popsBoot = false;
	g_popsConvertedPBP = false;
	g_popsIcon0Usable = true;
	g_popsManLayout = 0;
	g_popsPBPPath.clear();
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
	auto s = p.Section("PopsBoot", 0, 2);
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
	if (s >= 2) {
		Do(p, g_popsManLayout);
	} else {
		g_popsManLayout = 0;
	}
}

static const PopsManLayout &PopsMan() {
	return POPSMAN_LAYOUTS[g_popsManLayout];
}

static u32 PopsManBase() {
	const u32 loadModule = KernelFindModuleExport("scePopsMan", SCE_POPSMAN_LOAD_MODULE_NID);
	return loadModule ? loadModule - PopsMan().loadModule : 0;
}

// Picks the layout of the loaded manager, by the code at its places. False if none matches.
static bool FindPopsManLayout() {
	const u32 loadModule = KernelFindModuleExport("scePopsMan", SCE_POPSMAN_LOAD_MODULE_NID);
	if (!loadModule) {
		return false;
	}
	for (int i = 0; i < (int)ARRAY_SIZE(POPSMAN_LAYOUTS); i++) {
		const PopsManLayout &layout = POPSMAN_LAYOUTS[i];
		const u32 base = loadModule - layout.loadModule;
		const struct {
			u32 offset;
			u32 original;
		} code[] = {
			{ layout.loadModule, 0x27BD0000 | ((0x10000 - layout.loadModuleFrame) & 0xFFFF) },
			{ layout.checkMac, 0x27BDFFC0 },  // addiu sp, sp, -0x40
			{ layout.checkMacExport, 0x27BDFFB0 },
			{ layout.keyProbe, 0x27BDFEF0 },
			{ layout.keyProbe + 4, 0x2CA70001 },
		};
		bool match = true;
		for (const auto &c : code) {
			match = match && Memory::ReadUnchecked_U32(base + c.offset) == c.original;
		}
		if (match) {
			g_popsManLayout = i;
			return true;
		}
	}
	return false;
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

// The content ID (DATA.PSP + 0x560, as in the license) and the disc image's PGD header.
static bool ReadPBPDrmHeaders(u8 *pgdHeader, u8 *contentId) {
	std::unique_ptr<FileLoader> loader(ConstructFileLoader(g_popsPBPPath));
	PBPReader pbp(loader.get());
	return pbp.IsValid() && pbp.ReadSubFile(PBP_EXECUTABLE_PSP, 0x560, 0x30, contentId) &&
		ReadDiscImage(pbp, 0x90, pgdHeader) && memcmp(pgdHeader, "\0PGD", 4) == 0;
}

// Replaces the manager's license check, which needs the game's license and activation. The
// version key comes from the disc image's PGD header instead (its MAC_70 is the key's MAC), as
// CFW's popcorn does. Returns what the check does on failure.
static int __PopsGetVersionKey(u32 pathAddr, u32 keyOut, u32 fingerprintOut) {
	const char *path = Memory::GetCharPointer(pathAddr);
	if (!path || !Memory::IsValidRange(keyOut, 16)) {
		return hleLogError(Log::sceKernel, 0x80020148, "bad arguments");
	}
	u8 pgdHeader[0x90];
	u8 contentId[0x30]{};
	if (!ReadPBPDrmHeaders(pgdHeader, contentId)) {
		return hleLogWarning(Log::sceKernel, 0x80020148, "%s has no PGD disc image", path);
	}
	PGD_DESC *pgd = pgd_open(__ChnnlsvKirkState(), pgdHeader, 2, nullptr);
	if (!pgd) {
		return hleLogError(Log::sceKernel, 0x80020148, "%s: bad PGD header", path);
	}
	u8 versionKey[16];
	memcpy(versionKey, pgd->vkey, 16);
	pgd_close(pgd);

	Memory::Memcpy(keyOut, versionKey, 16, "PopsVersionKey");
	if (Memory::IsValidRange(fingerprintOut, sizeof(contentId))) {
		Memory::Memcpy(fingerprintOut, contentId, sizeof(contentId), "PopsVersionKey");
	}
	const u32 base = PopsManBase();
	if (Memory::ReadUnchecked_U32(base + PopsMan().keyLatched) == 0) {
		Memory::WriteUnchecked_U32(1, base + PopsMan().keyLatched);
		Memory::Memcpy(base + PopsMan().versionKey, versionKey, 16, "PopsVersionKey");
		Memory::Memcpy(base + PopsMan().keyFingerprint, contentId, sizeof(contentId), "PopsVersionKey");
	}
	return hleLogInfo(Log::sceKernel, 0, "%.36s", (const char *)contentId);
}

// An 80x80 PNG with nothing in it.
static const std::vector<u8> &BlankIcon0() {
	static std::vector<u8> png;
	if (png.empty()) {
		const std::vector<u8> pixels(80 * 80 * 4, 0);
		Save8888RGBAScreenshot(png, pixels.data(), 80, 80);
	}
	return png;
}

// The emulator's read of an ICON0 it can't use (see __PopsBootPatchEmulator): a blank one instead.
static int __PopsReadIcon0(u32 file, u32 buf, u32 offset, u32 size) {
	const std::vector<u8> &png = BlankIcon0();
	if (size != png.size() || !Memory::IsValidRange(buf, size)) {
		return hleLogError(Log::sceKernel, -1, "unexpected read");
	}
	Memory::Memcpy(buf, png.data(), size, "PopsIcon0");
	return hleLogDebug(Log::sceKernel, (int)size);
}

const HLEFunction PopsBootCalls[] = {
	{ 0x13370006, &WrapI_UUU<__PopsDeflateDecompress>,  "__PopsDeflateDecompress", 'i', "xxx" },
	{ 0x13370007, &WrapI_UUU<__PopsGetVersionKey>,      "__PopsGetVersionKey",     'i', "xxx" },
	{ 0x13370008, &WrapI_UUUU<__PopsReadIcon0>,         "__PopsReadIcon0",         'i', "xxxx" },
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

// A function that makes a syscall (jr ra; syscall), for the emulator to call in place of one of its own.
static u32 MakeCallStub(const char *func) {
	u32 size = 8;
	const u32 stub = kernelMemory.Alloc(size, false, func);
	Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), stub);
	PatchCall(stub + 4, func);
	currentMIPS->InvalidateICacheRangeDeferred(stub, size);
	return stub;
}

void __PopsBootPatchEmulator(u32 textAddr, u32 textSize, u32 decodeStub) {
	// As CFW's popcorn does: the manual's name check always passes, and an ICON0 the emulator can't
	// use reads as a blank one.
	for (u32 addr = textAddr + 0x30; addr + 0x30 < textAddr + textSize; addr += 4) {
		const u32 op = Memory::ReadUnchecked_U32(addr);
		if (op == 0x24050080 && Memory::ReadUnchecked_U32(addr + 24) == 0x24030001) {
			// li a1, 0x80 (the size of the name it gets), then the check's call becomes li v0, 1.
			Memory::WriteUnchecked_U32(0x24020001, addr + 8);
		} else if (!g_popsIcon0Usable && op == 0x00432823 && Memory::ReadUnchecked_U32(addr - 8) == 0x8FA20010 &&
			Memory::ReadUnchecked_U32(addr - 4) == 0x8FA3000C && Memory::ReadUnchecked_U32(addr + 0x2C) == Memory::ReadUnchecked_U32(addr - 0x30)) {
			// subu a1, v0, v1: the ICON0's size, from the offsets in the PBP header it read 0x30 earlier
			// with the same call it then reads the ICON0 with.
			Memory::WriteUnchecked_U32(0x24050000 | (u32)BlankIcon0().size(), addr);
			Memory::WriteUnchecked_U32(MIPS_MAKE_JAL(MakeCallStub("__PopsReadIcon0")), addr + 0x2C);
		}
	}
	currentMIPS->InvalidateICacheRangeDeferred(textAddr, textSize);

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

// After the game path, a word for the version key function the manager registers.
static u32 KeyFuncSlot(const std::string &gamePath) {
	return g_popsInitPath + (((u32)gamePath.size() + 1 + 3) & ~3);
}

void __PopsBootSetVersionKeyFunc(u32 func) {
	if (g_popsInitPath) {
		Memory::WriteUnchecked_U32(func, KeyFuncSlot(Memory::GetCharPointer(g_popsInitPath)));
	}
}

static void PatchReturn(u32 addr, u32 delaySlotOp) {
	Memory::WriteUnchecked_U32(MIPS_MAKE_JR_RA(), addr);
	Memory::WriteUnchecked_U32(delaySlotOp, addr + 4);
	currentMIPS->InvalidateICacheRangeDeferred(addr, 8);
}

// The emulator only takes an 80x80 PNG as ICON0, and goes wrong on others. A 144x80 one (the size of
// a PSP game's) crashes it.
static bool IsUsableIcon0(FileLoader *loader) {
	PBPReader pbp(loader);
	u8 header[24];
	if (!pbp.IsValid() || !pbp.ReadSubFile(PBP_ICON0_PNG, 0, sizeof(header), header)) {
		return false;
	}
	static const u8 pngStart[16] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
	static const u8 size80x80[8] = { 0, 0, 0, 80, 0, 0, 0, 80 };
	return memcmp(header, pngStart, 16) == 0 && memcmp(header + 16, size80x80, 8) == 0;
}

bool __PopsBootStartInit(FileLoader *pbp, const std::string &gamePath, std::string *error) {
	if (!FindPopsManLayout()) {
		auto er = GetI18NCategory(I18NCat::ERRORS);
		*error = er->T("PS1FirmwareVersion", "So far, PS1 games only run with firmware 6.10 to 6.61, and a different version is installed. Install one of those from an official PSP update file in Settings > Tools > PSP Firmware.");
		return false;
	}
	const u32 base = PopsManBase();
	g_popsPBPPath = pbp->GetPath();
	g_popsConvertedPBP = IsConvertedPBP(pbp);
	g_popsIcon0Usable = IsUsableIcon0(pbp);
	const u32 loadModule = base + PopsMan().loadModule;
	PatchReturn(base + PopsMan().keyProbe, MIPS_MAKE_SYSCALL("PopsBootCalls", "__PopsGetVersionKey"));
	if (g_popsConvertedPBP) {
		// A converted PBP has no MACs. As popcorn does, take its data as authentic.
		PatchReturn(base + PopsMan().checkMac, 0x24020001);  // li v0, 1
		PatchReturn(base + PopsMan().checkMacExport, 0x24020001);
	}

	const u32 pathSize = (u32)gamePath.size() + 1;
	u32 allocSize = ((pathSize + 3) & ~3) + 4;
	g_popsInitPath = kernelMemory.Alloc(allocSize, false, "PopsInitPath");
	Memory::Memcpy(g_popsInitPath, gamePath.c_str(), pathSize, "PopsInitPath");
	const u32 keyFuncSlot = KeyFuncSlot(gamePath);
	Memory::WriteUnchecked_U32(0, keyFuncSlot);

	// The kernel's init: get the version key through the registered function, if any, then
	// scePopsManLoadModule(path, 0). Starts with a1 = its copy of the path, below the manager's
	// priority so it runs once module_start is done.
	const u32 code[] = {
		0x00A08021,                // move s0, a1
		0x03E08821,                // move s1, ra
		0x3C190000 | (((keyFuncSlot + 0x8000) >> 16) & 0xFFFF),  // lui t9, slot
		0x8F390000 | (keyFuncSlot & 0xFFFF),                     // lw t9, slot(t9)
		0x27BDFFF0,                // addiu sp, sp, -16
		0x13200003,                // beqz t9, +3
		0x02002021,                // move a0, s0
		0x0320F809,                // jalr t9: func(path, key)
		0x03A02821,                // move a1, sp
		0x02002021,                // move a0, s0
		0x24050000,                // li a1, 0
		0x0220F821,                // move ra, s1
		MIPS_MAKE_J(loadModule),   // returns to the thread's end
		0x27BD0010,                // addiu sp, sp, 16
	};
	g_popsInitThread.reset(new HLEHelperThread("ScePopsInit", code, (u32)ARRAY_SIZE(code), 0x40, 0x4000));
	g_popsInitThread->Start(pathSize, g_popsInitPath);
	INFO_LOG(Log::sceKernel, "POPS: init will call scePopsManLoadModule(%s, 0)", gamePath.c_str());
	return true;
}
