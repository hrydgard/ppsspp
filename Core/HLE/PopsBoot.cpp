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
#include "Core/System.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

static bool g_popsBoot = false;
static bool g_popsConvertedPBP = false;
// The game's PBP on the host.
static Path g_popsPBPPath;
static std::unique_ptr<HLEHelperThread> g_popsInitThread;
static u32 g_popsInitPath = 0;
static std::unique_ptr<HLEHelperThread> g_popsMeThread;
// See PopsMeReturnCycles.
static bool g_meRunning = false;
static u64 g_meRunningSince = 0;
// See PopsMeDue.
static bool g_meClockValid = false;
static u64 g_meClockUs = 0;
static s64 g_meClockSamples = 0;
static u32 g_ps1Clock = 0;
static s64 g_ps1Cycles = 0;
static s64 g_ps1ClockSamples = 0;
static u64 g_ps1ClockMovedUs = 0;

// The 6.60 POPS manager (6.61's has the same code), from its base: scePopsManLoadModule, the ME bootstrap template (whose sp
// words the manager patches), the ME loop it jumps to, and the loop's waits for the audio FIFO.
enum : u32 {
	POPSMAN_LOAD_MODULE = 0x1E20,
	POPSMAN_ME_BOOT = 0x2F28,
	POPSMAN_ME_BOOT_SP_HI = 0x2F2C,
	POPSMAN_ME_BOOT_SP_LO = 0x2F30,
	POPSMAN_ME_BOOT_JR = 0x2F80,
	POPSMAN_ME_LOOP = 0x2F88,
	POPSMAN_ME_PREFILL_POLL = 0x30C0,  // lw a2, 0x28(k0)
	POPSMAN_ME_HALT1 = 0x3100,         // one HALT per sample while playing
	POPSMAN_ME_DRAIN_POLL = 0x3164,    // lw t4, 0x28(k0)
	POPSMAN_ME_HALT2 = 0x31C8,         // stopped
	// The license check (path, keyOut, fingerprintOut). Its first success latches the version key and
	// the content ID fingerprint (+0124).
	POPSMAN_KEY_PROBE = 0x266C,
	POPSMAN_VERSION_KEY = 0x49E0,
	POPSMAN_KEY_LATCHED = 0x4A18,
	POPSMAN_KEY_FINGERPRINT = 0x4A1C,
	// Whether data matches its MAC: its own, and the one it exports as sceMeAudio_F6637A72.
	POPSMAN_CHECK_MAC = 0x0A28,
	POPSMAN_CHECK_MAC_EXPORT = 0x0AB8,
};

// The 6.60 emulator (pops_01g.prx and pops_02g.prx), from its text.
enum : u32 {
	// The ME's wait for the lock it shares with the emulator.
	POPS_ME_LOCK_WAIT = 0x5C,
	// The SPU interrupt poll, every 2153 PS1 cycles.
	POPS_SPU_IRQ_POLL = 0x8580,
};

// What the patched places hold in the 6.60/6.61 manager. Checked when it's loaded.
static const struct {
	u32 offset;
	u32 original;
} POPSMAN_CODE[] = {
	{ POPSMAN_ME_BOOT, 0x24090000 },
	{ POPSMAN_ME_BOOT_JR, 0x03E00008 },  // jr ra
	{ POPSMAN_ME_PREFILL_POLL, 0x8F460028 },
	{ POPSMAN_ME_HALT1, 0x70000000 },  // halt
	{ POPSMAN_ME_DRAIN_POLL, 0x8F4C0028 },
	{ POPSMAN_ME_HALT2, 0x70000000 },
	{ POPSMAN_CHECK_MAC, 0x27BDFFC0 },  // addiu sp, sp, -0x40
	{ POPSMAN_CHECK_MAC_EXPORT, 0x27BDFFB0 },
	{ POPSMAN_KEY_PROBE, 0x27BDFEF0 },
	{ POPSMAN_KEY_PROBE + 4, 0x2CA70001 },
};

// The emulator's PS1 clock, in PS1 CPU cycles, in its scratchpad state.
static const u32 POPS_PS1_CLOCK = 0x000101AC;
static const s64 PS1_CLOCK_HZ = 33868800;
static const u32 SCE_POPSMAN_LOAD_MODULE_NID = 0x29B3FB24;

// Where the emulator keeps the PS1's VRAM, and the buffer it unpacks 24-bit pictures (FMV) into.
static const u32 PS1_VRAM_ADDRESS = 0x04000000;
static const u32 PS1_RGB24_ADDRESS = 0x04100000;

void __PopsBootInit() {
	g_popsBoot = true;
}

void __PopsBootShutdown() {
	g_popsBoot = false;
	g_popsConvertedPBP = false;
	g_popsPBPPath.clear();
	SetSnapHalfPixelSprites(false);
	g_meRunning = false;
	g_meClockValid = false;
	// The kernel objects and memory are already gone with the kernel.
	if (g_popsInitThread) {
		g_popsInitThread->Forget();
	}
	g_popsInitThread.reset();
	g_popsInitPath = 0;
	if (g_popsMeThread) {
		g_popsMeThread->Forget();
	}
	g_popsMeThread.reset();
}

bool PopsBootActive() {
	return g_popsBoot;
}

static void PopsBootSetupGPU() {
	if (gpu) {
		// One 1024x512 16-bit surface, also copied and read outside where it's drawn.
		gpu->SetFixedFramebuffer(PS1_VRAM_ADDRESS, 1024, 1024, 512);
		gpu->SetNativeResolutionFramebuffer(PS1_RGB24_ADDRESS);
		// It ORs 24-bit video together byte by byte.
		gpu->SetSeparateLogicOpDraws(true);
	}
	SetSnapHalfPixelSprites(true);
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
	DoHelperThread(p, g_popsMeThread);
	if (p.mode == PointerWrap::MODE_READ) {
		if (g_popsBoot) {
			PopsBootSetupGPU();
		}
		g_meRunning = g_popsMeThread && __KernelGetCurThread() == g_popsMeThread->Id();
		g_meRunningSince = CoreTiming::GetTicks(currentMIPS);
		g_meClockValid = false;
	}
}

static u32 PopsManBase() {
	const u32 loadModule = KernelFindModuleExport("scePopsMan", SCE_POPSMAN_LOAD_MODULE_NID);
	return loadModule ? loadModule - POPSMAN_LOAD_MODULE : 0;
}

// The ME is a processor of its own, but here its loop runs on a thread of the emulated CPU. Hand
// back the cycles it spends, at each of its syscalls and when it's switched out. Cycles from a
// slice that's already over can't be returned.
static void PopsMeReturnCycles() {
	if (!g_meRunning) {
		return;
	}
	s64 ran = (s64)(CoreTiming::GetTicks(currentMIPS) - g_meRunningSince);
	const int executed = CoreTiming::slicelength - currentMIPS->downcount;
	if (ran > executed) {
		ran = executed;
	}
	if (ran > 0) {
		currentMIPS->downcount += (int)ran;
	}
	g_meRunningSince = CoreTiming::GetTicks(currentMIPS);
}

void __PopsBootSwitchContext(SceUID from, SceUID to) {
	if (!g_popsMeThread) {
		return;
	}
	const SceUID me = g_popsMeThread->Id();
	if (from == me) {
		PopsMeReturnCycles();
		g_meRunning = false;
	}
	if (to == me) {
		g_meRunning = true;
		g_meRunningSince = CoreTiming::GetTicks(currentMIPS);
	}
}

// The ME raises the emulator's SPU interrupt as it plays, and a game streaming from the SPU's
// capture buffers (Vib-Ribbon) expects those a fixed amount of PS1 time apart. So the ME follows
// the emulator's PS1 clock: at most ME_LEAD samples ahead, and woken to catch up each time the
// emulator polls the SPU interrupt (__PopsMeSync). While that clock stands still (boot, a pause),
// the PSP's paces it. Either restarts when the ME falls far behind.
static const u32 ME_LEAD = 32;
static const u32 ME_BEHIND_MAX = 4096;
static const u32 ME_QUEUE_HIGH = 4096;
static const u32 ME_QUEUE_LOW = 2048;
static const u64 PS1_CLOCK_STOPPED_US = 50000;

// Where the ME should be by now, in samples.
static s64 PopsMeDue(u64 nowUs, s64 out, bool *ps1Paced) {
	const u32 ps1 = Memory::ReadUnchecked_U32(POPS_PS1_CLOCK);
	if (!g_meClockValid) {
		g_meClockValid = true;
		g_meClockUs = nowUs;
		g_meClockSamples = out;
		g_ps1Clock = ps1;
		g_ps1Cycles = 0;
		g_ps1ClockSamples = out;
		g_ps1ClockMovedUs = nowUs;
	}
	const s32 step = (s32)(ps1 - g_ps1Clock);
	g_ps1Clock = ps1;
	if (step > 0 && step < PS1_CLOCK_HZ) {
		g_ps1Cycles += step;
		g_ps1ClockMovedUs = nowUs;
	}
	s64 due;
	*ps1Paced = nowUs - g_ps1ClockMovedUs < PS1_CLOCK_STOPPED_US;
	if (*ps1Paced) {
		due = g_ps1ClockSamples + g_ps1Cycles * 44100 / PS1_CLOCK_HZ;
		g_meClockUs = nowUs;
		g_meClockSamples = due;
	} else {
		due = g_meClockSamples + (s64)(nowUs - g_meClockUs) * 44100 / 1000000;
		g_ps1ClockSamples = due;
		g_ps1Cycles = 0;
	}
	if (due - out > (s64)ME_BEHIND_MAX) {
		g_meClockUs = nowUs;
		g_meClockSamples = out;
		g_ps1ClockSamples = out;
		g_ps1Cycles = 0;
		due = out;
	}
	return due;
}

// Where the ME loop would wait for the audio FIFO. Our FIFO always has room, so wait here instead
// when the ME is ahead. These stand in for instructions, so the ME's registers must survive.
static u32 PopsMePace(const char *reason) {
	hleSkipDeadbeef();
	PopsMeReturnCycles();
	const s64 out = (s64)PopsMMIO::SamplesOut();
	bool ps1Paced;
	const s64 due = PopsMeDue(CoreTiming::GetGlobalTimeUs(), out, &ps1Paced);
	const u32 queued = PopsMMIO::QueuedSamples();
	if (!ps1Paced && queued >= ME_QUEUE_HIGH) {
		return hleDelayResult(currentMIPS->r[MIPS_REG_V0], reason, (int)((u64)(queued - ME_QUEUE_LOW) * 1000000 / 44100));
	}
	const s64 ahead = out - due;
	if (ahead < (s64)ME_LEAD) {
		return currentMIPS->r[MIPS_REG_V0];
	}
	if (ps1Paced) {
		// Until the emulator's next SPU interrupt poll wakes it.
		return hleDelayResult(currentMIPS->r[MIPS_REG_V0], reason, 2000);
	}
	return hleDelayResult(currentMIPS->r[MIPS_REG_V0], reason, (int)((ahead - ME_LEAD / 2) * 1000000 / 44100));
}

static u32 __PopsMeSync() {
	hleSkipDeadbeef();
	if (!g_popsMeThread) {
		return currentMIPS->r[MIPS_REG_V0];
	}
	bool ps1Paced;
	const s64 out = (s64)PopsMMIO::SamplesOut();
	const s64 due = PopsMeDue(CoreTiming::GetGlobalTimeUs(), out, &ps1Paced);
	if (ps1Paced && out < due && hleWakeDelayedThread(g_popsMeThread->Id())) {
		hleReSchedule("ME catches up");
	}
	return currentMIPS->r[MIPS_REG_V0];
}

static u32 __PopsMeHalt() {
	// Nothing played since the last HALT: the ME is stopped.
	if (PopsMMIO::PendingSamples() == 0) {
		hleSkipDeadbeef();
		PopsMeReturnCycles();
		return hleDelayResult(currentMIPS->r[MIPS_REG_V0], "ME halted", 1000);
	}
	PopsMMIO::TakePendingSamples();
	return PopsMePace("ME halt");
}

static u32 __PopsMePrefillPoll() {
	currentMIPS->r[MIPS_REG_A2] = PopsMMIO::AudioFreeSlots();
	return PopsMePace("ME prefill");
}

static u32 __PopsMeDrainPoll() {
	currentMIPS->r[MIPS_REG_T4] = PopsMMIO::AudioFreeSlots();
	return PopsMePace("ME drain");
}

// The ME's spin on the Peterson lock it shares with the emulator, which would keep the emulator
// from ever releasing it. Does the original loop's compare into at.
static u32 __PopsMeLockWait() {
	hleSkipDeadbeef();
	const u32 lock = currentMIPS->r[MIPS_REG_A0] + 0x29C;
	currentMIPS->r[MIPS_REG_COMPILER_SCRATCH] = Memory::IsValidAddress(lock) ? (Memory::ReadUnchecked_U32(lock) & 0xFF00FFFF) : 0;
	if (currentMIPS->r[MIPS_REG_COMPILER_SCRATCH] != currentMIPS->r[MIPS_REG_V1]) {
		return currentMIPS->r[MIPS_REG_V0];
	}
	PopsMeReturnCycles();
	return hleDelayResult(currentMIPS->r[MIPS_REG_V0], "ME lock", 10);
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
	if (Memory::ReadUnchecked_U32(base + POPSMAN_KEY_LATCHED) == 0) {
		Memory::WriteUnchecked_U32(1, base + POPSMAN_KEY_LATCHED);
		Memory::Memcpy(base + POPSMAN_VERSION_KEY, versionKey, 16, "PopsVersionKey");
		Memory::Memcpy(base + POPSMAN_KEY_FINGERPRINT, contentId, sizeof(contentId), "PopsVersionKey");
	}
	return hleLogInfo(Log::sceKernel, 0, "%.36s", (const char *)contentId);
}

const HLEFunction PopsBootCalls[] = {
	{ 0x13370001, &WrapU_V<__PopsMeHalt>,               "__PopsMeHalt",            'x', ""    },
	{ 0x13370002, &WrapU_V<__PopsMePrefillPoll>,        "__PopsMePrefillPoll",     'x', ""    },
	{ 0x13370003, &WrapU_V<__PopsMeDrainPoll>,          "__PopsMeDrainPoll",       'x', ""    },
	{ 0x13370004, &WrapU_V<__PopsMeLockWait>,           "__PopsMeLockWait",        'x', ""    },
	{ 0x13370005, &WrapU_V<__PopsMeSync>,               "__PopsMeSync",            'x', ""    },
	{ 0x13370006, &WrapI_UUU<__PopsDeflateDecompress>,  "__PopsDeflateDecompress", 'i', "xxx" },
	{ 0x13370007, &WrapI_UUU<__PopsGetVersionKey>,      "__PopsGetVersionKey",     'i', "xxx" },
};

void Register_PopsBootCalls() {
	RegisterHLEModule("PopsBootCalls", ARRAY_SIZE(PopsBootCalls), PopsBootCalls);
}

static void PatchCall(u32 addr, const char *func) {
	Memory::WriteUnchecked_U32(MIPS_MAKE_SYSCALL("PopsBootCalls", func), addr);
}

// Runs the ME bootstrap's part that matters here (take the registered stack, jump to the manager's
// ME loop) on a helper thread below the emulator's, so the ME runs while they wait.
void __PopsBootStartMe() {
	const u32 base = PopsManBase();
	static const struct {
		u32 offset;
		const char *replacement;
	} patches[] = {
		{ POPSMAN_ME_PREFILL_POLL, "__PopsMePrefillPoll" },
		{ POPSMAN_ME_HALT1, "__PopsMeHalt" },
		{ POPSMAN_ME_DRAIN_POLL, "__PopsMeDrainPoll" },
		{ POPSMAN_ME_HALT2, "__PopsMeHalt" },
	};
	for (const auto &patch : patches) {
		PatchCall(base + patch.offset, patch.replacement);
	}
	currentMIPS->InvalidateICacheRangeDeferred(base + POPSMAN_ME_PREFILL_POLL, POPSMAN_ME_HALT2 + 4 - POPSMAN_ME_PREFILL_POLL);

	const u32 code[] = {
		Memory::ReadUnchecked_U32(base + POPSMAN_ME_BOOT_SP_HI),  // lui sp, stack >> 16
		Memory::ReadUnchecked_U32(base + POPSMAN_ME_BOOT_SP_LO),  // ori sp, sp, stack & 0xFFFF
		MIPS_MAKE_J(base + POPSMAN_ME_LOOP),
		MIPS_MAKE_NOP(),
	};
	g_popsMeThread.reset(new HLEHelperThread("ScePopsMe", code, (u32)ARRAY_SIZE(code), 0x08, 0x400));
	g_popsMeThread->Start(0, 0);
	INFO_LOG(Log::sceKernel, "POPS: ME started, loop at %08x", base + POPSMAN_ME_LOOP);
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
	PopsBootSetupGPU();

	// lw at, 0x29C(a0); ins at, zero, 16, 8; beq at, v1, back
	const u32 lockWait = textAddr + POPS_ME_LOCK_WAIT;
	if (Memory::ReadUnchecked_U32(lockWait) == 0x8C81029C && Memory::ReadUnchecked_U32(lockWait + 4) == 0x7C01BC04 &&
		Memory::ReadUnchecked_U32(lockWait + 8) == 0x1023FFFD) {
		Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), lockWait + 4);
		PatchTrampoline(lockWait, "__PopsMeLockWait", MIPS_MAKE_NOP(), lockWait + 8);
	} else {
		ERROR_LOG(Log::sceKernel, "POPS: not the 6.60/6.61 emulator, its ME may wait forever");
	}

	// lui v0, 0x49F4; addiu v1, v0, 0. The addiu in the jump's delay slot runs on a stale v0, and
	// again after the trampoline.
	const u32 irqPoll = textAddr + POPS_SPU_IRQ_POLL;
	if (Memory::ReadUnchecked_U32(irqPoll) == 0x3C0249F4 && Memory::ReadUnchecked_U32(irqPoll + 4) == 0x24430000) {
		PatchTrampoline(irqPoll, "__PopsMeSync", 0x3C0249F4, irqPoll + 4);
	} else {
		ERROR_LOG(Log::sceKernel, "POPS: not the 6.60/6.61 emulator, its SPU interrupts may bunch up");
	}

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

bool __PopsBootStartInit(FileLoader *pbp, const std::string &gamePath, std::string *error) {
	const u32 base = PopsManBase();
	bool known = base != 0;
	for (const auto &code : POPSMAN_CODE) {
		known = known && Memory::ReadUnchecked_U32(base + code.offset) == code.original;
	}
	if (!known) {
		*error = "PS1 EBOOTs need the POPS manager of firmware 6.60 or 6.61, and this firmware has a different one.";
		return false;
	}
	g_popsPBPPath = pbp->GetPath();
	g_popsConvertedPBP = IsConvertedPBP(pbp);
	const u32 loadModule = base + POPSMAN_LOAD_MODULE;
	PatchReturn(base + POPSMAN_KEY_PROBE, MIPS_MAKE_SYSCALL("PopsBootCalls", "__PopsGetVersionKey"));
	if (g_popsConvertedPBP) {
		// A converted PBP has no MACs. As popcorn does, take its data as authentic.
		PatchReturn(base + POPSMAN_CHECK_MAC, 0x24020001);  // li v0, 1
		PatchReturn(base + POPSMAN_CHECK_MAC_EXPORT, 0x24020001);
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
