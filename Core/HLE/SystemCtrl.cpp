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

// SystemCtrlForKernel/SystemCtrlForUser: the API of custom firmware's SystemControl module (PRO,
// ARK-4). On a PSP it's loaded at boot and works by patching the firmware's own kernel modules,
// which we HLE, so instead the API is HLE too. That's enough for modules written against it:
// translation-patch loaders, and ARK-4's vshctrl, which patches the XMB as it starts (see
// LoadArkVshCtrl). NIDs and behavior follow ARK-4's systemctrl (core/systemctrl in its source).
//
// The functions that work on loaded modules (start-module handler, finding exports, hooking
// imports and syscalls) are in sceKernelModule.cpp, next to the modules.

#include <cstring>
#include <vector>

#include "zlib.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/SystemCtrl.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"

// ARK-4's ARKConfig (common/include/ark.h).
struct ArkConfig {
	u32_le magic;
	char arkpath[108];
	char exploit_id[12];
	char launcher[20];
	u8 exec_mode;
	u8 recovery;
	u8 pad[2];
};
static_assert(sizeof(ArkConfig) == 148, "ArkConfig layout");

static const u32 ARK_CONFIG_MAGIC = 0xB00B1E55;
static const u8 ARK_EXEC_MODE_PSP = 0x04;  // PSP_ORIG
// SEConfig (common/include/systemctrl_se.h) is a magic and then a few dozen byte-sized options;
// zero is the default for all of them. Room to spare for a bigger one in a later ARK.
static const u32 SE_CONFIG_SIZE = 0x40;
// rebootex_config.iso_path
static const u32 UMD_FILE_SIZE = 0x100;
// ARK's version, as its systemctrl reports it (sctrlSEGetVersion, sctrlHENGetVersion,
// sctrlHENGetMinorVersion: major, minor, micro). Read from the systemctrl in FLASH0.ARK, see
// __SystemCtrlReadArkVersion. vshctrl shows the major in System Information ("ARK-5").
static const int ARK_DEFAULT_MAJOR = 4;
static int g_arkVersion[3] = { ARK_DEFAULT_MAJOR, 0, 0 };

static u32 g_arkConfigAddr;
static u32 g_seConfigAddr;
static u32 g_umdFileAddr;
static int g_bootConfIndex;
static int g_discType;
static int g_userLevel;
// sctrlSetStartModuleExtra's hook, kept but not called (see there).
static u32 g_startModuleExtra;
// What sceKernelGetGameInfo points to.
static u32 g_gameInfoAddr;

// sctrlKernelQuerySystemCall makes a syscall out of a guest function (vshctrl writes one into the
// shell's code). Ours only reach HLE functions, so each such function gets one of these slots,
// whose syscall calls it.
static const int GUEST_CALL_SLOTS = 8;
static u32 g_guestCallSlots[GUEST_CALL_SLOTS];
static int actionGuestCallDone = -1;

// Returns the guest function's result as the syscall's.
class GuestCallDone : public PSPAction {
public:
	static PSPAction *Create() { return new GuestCallDone(); }
	void run(MipsCall &call) override {
		call.savedV0 = currentMIPS->r[MIPS_REG_V0];
		call.savedV1 = currentMIPS->r[MIPS_REG_V1];
	}
	void DoState(PointerWrap &p) override {
		auto s = p.Section("GuestCallDone", 1);
		(void)s;
	}
};

void __SystemCtrlInit() {
	g_arkConfigAddr = 0;
	g_seConfigAddr = 0;
	g_umdFileAddr = 0;
	g_bootConfIndex = 0;
	g_discType = 0;
	g_userLevel = 0;
	g_startModuleExtra = 0;
	g_gameInfoAddr = 0;
	g_arkVersion[0] = ARK_DEFAULT_MAJOR;
	g_arkVersion[1] = 0;
	g_arkVersion[2] = 0;
	memset(g_guestCallSlots, 0, sizeof(g_guestCallSlots));
	actionGuestCallDone = __KernelRegisterActionType(GuestCallDone::Create);
}

void __SystemCtrlDoState(PointerWrap &p) {
	auto s = p.Section("SystemCtrl", 0, 3);
	if (!s) {
		if (p.mode == p.MODE_READ) {
			__SystemCtrlInit();
		}
		return;
	}
	Do(p, g_arkConfigAddr);
	Do(p, g_seConfigAddr);
	Do(p, g_umdFileAddr);
	Do(p, g_bootConfIndex);
	Do(p, g_discType);
	Do(p, g_userLevel);
	DoArray(p, g_guestCallSlots, GUEST_CALL_SLOTS);
	Do(p, actionGuestCallDone);
	__KernelRestoreActionType(actionGuestCallDone, GuestCallDone::Create);
	if (s >= 2) {
		Do(p, g_startModuleExtra);
		Do(p, g_gameInfoAddr);
	} else {
		g_startModuleExtra = 0;
		g_gameInfoAddr = 0;
	}
	if (s >= 3) {
		DoArray(p, g_arkVersion, 3);
	} else {
		g_arkVersion[0] = ARK_DEFAULT_MAJOR;
		g_arkVersion[1] = 0;
		g_arkVersion[2] = 0;
	}
}

static u32 AllocZeroed(u32 size, const char *tag) {
	const u32 addr = kernelMemory.Alloc(size, false, tag);
	if (addr == (u32)-1) {
		return 0;
	}
	Memory::Memset(addr, 0, size, tag);
	return addr;
}

static u32 ArkConfigAddr() {
	if (!g_arkConfigAddr) {
		g_arkConfigAddr = AllocZeroed(sizeof(ArkConfig), "ARKConfig");
		if (!g_arkConfigAddr) {
			return 0;
		}
		ArkConfig *config = (ArkConfig *)Memory::GetPointerWriteUnchecked(g_arkConfigAddr);
		config->magic = ARK_CONFIG_MAGIC;
		strcpy(config->arkpath, "ms0:/PSP/SAVEDATA/ARK_01234/");
		// What ARK shows as how it was started, in System Information.
		strcpy(config->exploit_id, "PPSSPP");
		config->exec_mode = ARK_EXEC_MODE_PSP;
	}
	return g_arkConfigAddr;
}

static u32 SEConfigAddr() {
	if (!g_seConfigAddr) {
		g_seConfigAddr = AllocZeroed(SE_CONFIG_SIZE, "SEConfig");
		if (g_seConfigAddr) {
			Memory::WriteUnchecked_U32(ARK_CONFIG_MAGIC, g_seConfigAddr);
		}
	}
	return g_seConfigAddr;
}

static u32 UmdFileAddr() {
	if (!g_umdFileAddr) {
		g_umdFileAddr = AllocZeroed(UMD_FILE_SIZE, "SEUmdFile");
	}
	return g_umdFileAddr;
}

// Both copy the config out to conf if given, and return where it lives.
static u32 sctrlArkGetConfig(u32 confAddr) {
	const u32 addr = ArkConfigAddr();
	if (addr && confAddr && Memory::IsValidRange(confAddr, sizeof(ArkConfig))) {
		Memory::Memcpy(confAddr, addr, sizeof(ArkConfig), "sctrlArkGetConfig");
	}
	return hleLogDebug(Log::sceKernel, addr);
}

static u32 sctrlSEGetConfig(u32 confAddr) {
	const u32 addr = SEConfigAddr();
	if (addr && confAddr && Memory::IsValidRange(confAddr, SE_CONFIG_SIZE)) {
		Memory::Memcpy(confAddr, addr, SE_CONFIG_SIZE, "sctrlSEGetConfig");
	}
	return hleLogDebug(Log::sceKernel, addr);
}

static u32 sctrlSEGetUmdFile() {
	return hleLogDebug(Log::sceKernel, UmdFileAddr());
}

static int sctrlSESetUmdFile(const char *file) {
	const u32 addr = UmdFileAddr();
	if (addr) {
		char *dest = (char *)Memory::GetPointerWriteUnchecked(addr);
		memset(dest, 0, UMD_FILE_SIZE);
		if (file) {
			strncpy(dest, file, UMD_FILE_SIZE - 1);
		}
	}
	return hleLogInfo(Log::sceKernel, 0, "%s", file ? file : "(null)");
}

static int sctrlSEGetBootConfFileIndex() {
	return hleLogDebug(Log::sceKernel, g_bootConfIndex);
}

static int sctrlSESetBootConfFileIndex(int index) {
	g_bootConfIndex = index;
	return hleLogDebug(Log::sceKernel, 0);
}

static int sctrlSESetDiscType(int type) {
	g_discType = type;
	return hleLogDebug(Log::sceKernel, 0);
}

static int sctrlSEGetVersion() {
	return hleLogDebug(Log::sceKernel, g_arkVersion[0]);
}

static int sctrlHENGetVersion() {
	return hleLogDebug(Log::sceKernel, g_arkVersion[1]);
}

static int sctrlHENGetMinorVersion() {
	return hleLogDebug(Log::sceKernel, g_arkVersion[2]);
}

// The value a leaf function like "return 5;" returns: jr ra with the one instruction setting v0 in
// its delay slot, or just before it. ARK-5 builds have li v0 and move v0, zero, ARK-4.18 lui v0.
static bool ConstantReturned(const u8 *code, size_t size, int *value) {
	if (size < 8) {
		return false;
	}
	u32 ops[2];
	memcpy(ops, code, sizeof(ops));
	for (int i = 0; i < 2; ++i) {
		if (ops[i] != MIPS_MAKE_JR_RA()) {
			continue;
		}
		const u32 op = ops[i ^ 1];
		if ((op >> 16) == 0x2402) {  // addiu v0, zero, imm
			*value = (int)(s16)(op & 0xFFFF);
		} else if ((op >> 16) == 0x3402) {  // ori v0, zero, imm
			*value = (int)(op & 0xFFFF);
		} else if ((op >> 16) == 0x3C02) {  // lui v0, imm
			*value = (int)((op & 0xFFFF) << 16);
		} else if (op == 0x00001021 || op == 0x00001025) {  // move v0, zero
			*value = 0;
		} else {
			return false;
		}
		return true;
	}
	return false;
}

void __SystemCtrlReadArkVersion(const std::vector<u8> &prx) {
	// ARK's modules are a ~PSP header around a gzipped ELF.
	if (prx.size() < 0x152 || memcmp(prx.data(), "~PSP", 4) != 0 || prx[0x150] != 0x1F || prx[0x151] != 0x8B) {
		return;
	}
	std::vector<u8> elf(4 * 1024 * 1024);
	z_stream stream{};
	stream.next_in = (Bytef *)prx.data() + 0x150;
	stream.avail_in = (uInt)(prx.size() - 0x150);
	stream.next_out = elf.data();
	stream.avail_out = (uInt)elf.size();
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
		return;
	}
	const int result = inflate(&stream, Z_FINISH);
	elf.resize(stream.total_out);
	inflateEnd(&stream);
	if (result != Z_STREAM_END || elf.size() < 0x34 || memcmp(elf.data(), "\x7F" "ELF", 4) != 0) {
		return;
	}

	auto read32 = [&](size_t off) -> u32 {
		u32 v = 0;
		if (off + 4 <= elf.size()) {
			memcpy(&v, &elf[off], 4);
		}
		return v;
	};
	// A PRX's first program header is its one loadable segment, which starts at vaddr 0, and its
	// p_paddr is where the module info is in the file.
	const u32 phoff = read32(28);
	const u32 segOffset = read32(phoff + 4);
	const u32 segSize = read32(phoff + 16);
	const u32 modInfo = read32(phoff + 12) & 0x7FFFFFFF;
	auto offsetOf = [&](u32 vaddr) -> size_t {
		return vaddr < segSize ? segOffset + vaddr : elf.size();
	};
	const u32 entTop = read32(modInfo + 36);
	const u32 entEnd = read32(modInfo + 40);

	static const struct {
		u32 nid;
		int index;
	} versionFuncs[] = {
		{ 0xB47C9D77, 0 },  // sctrlSEGetVersion
		{ 0x1090A2E1, 1 },  // sctrlHENGetVersion
		{ 0x5328B431, 2 },  // sctrlHENGetMinorVersion
	};
	int found = 0;
	int version[3] = { g_arkVersion[0], g_arkVersion[1], g_arkVersion[2] };
	for (u32 ent = entTop; ent + 16 <= entEnd;) {
		const size_t e = offsetOf(ent);
		if (e + 16 > elf.size() || elf[e + 8] == 0) {
			break;
		}
		const u32 nameAddr = read32(e);
		const size_t nameOff = offsetOf(nameAddr);
		const bool isKernelLib = nameAddr != 0 && nameOff < elf.size() &&
			!strncmp((const char *)&elf[nameOff], "SystemCtrlForKernel", std::min<size_t>(20, elf.size() - nameOff));
		if (isKernelLib) {
			const u32 funcs = elf[e + 10] | (elf[e + 11] << 8);
			const u32 vars = elf[e + 9];
			const size_t table = offsetOf(read32(e + 12));
			for (u32 i = 0; i < funcs; ++i) {
				const u32 nid = read32(table + i * 4);
				const size_t code = offsetOf(read32(table + (funcs + vars + i) * 4));
				for (const auto &func : versionFuncs) {
					if (func.nid == nid && code < elf.size() && ConstantReturned(&elf[code], elf.size() - code, &version[func.index])) {
						found++;
					}
				}
			}
		}
		ent += elf[e + 8] * 4;
	}
	if (found > 0) {
		memcpy(g_arkVersion, version, sizeof(g_arkVersion));
		INFO_LOG(Log::sceKernel, "ARK's systemctrl says it's version %d.%d.%d", version[0], version[1], version[2]);
	}
}

// 1 for a TestTool, 2 for a DevTool, 0 for a retail PSP.
static int sctrlHENIsToolKit() {
	return hleLogDebug(Log::sceKernel, 0);
}

// We don't enforce privilege levels, so only the value is kept.
static int sctrlKernelSetUserLevel(int level) {
	const int previous = g_userLevel;
	g_userLevel = level;
	return hleLogDebug(Log::sceKernel, previous);
}

// The VSH's LoadExec, with the API type the reboot is told to use. We don't reboot, and PPSSPP's
// LoadExec boots what it's given (the frontend picks the API type), so they all end up there.
static int sctrlKernelLoadExecVSHWithApitype(MIPSState *mips, int apitype, const char *file, u32 paramPtr) {
	INFO_LOG(Log::sceKernel, "sctrlKernelLoadExecVSHWithApitype(%03x, %s, %08x)", apitype, file ? file : "(null)", paramPtr);
	if (!file) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	return sceKernelLoadExec(mips, file, paramPtr);
}

static int sctrlKernelLoadExecVSHMs2(MIPSState *mips, const char *file, u32 paramPtr) {
	if (!file) {
		return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	return sceKernelLoadExec(mips, file, paramPtr);
}

// Restarts the VSH, which vshctrl does after deleting the ISO that's mounted. Nothing to reset here.
static int sctrlKernelExitVSH(u32 paramPtr) {
	return hleLogWarning(Log::sceKernel, 0, "not restarting the VSH");
}

template <int N>
static int SystemCtrlGuestCall() {
	if (!g_guestCallSlots[N]) {
		return hleLogError(Log::sceKernel, -1, "slot %d unused", N);
	}
	u32 args[8];
	for (int i = 0; i < 4; ++i) {
		args[i] = currentMIPS->r[MIPS_REG_A0 + i];
		args[4 + i] = currentMIPS->r[MIPS_REG_T0 + i];
	}
	hleEnqueueCall(g_guestCallSlots[N], 8, args, __KernelCreateAction(actionGuestCallDone));
	return hleLogDebug(Log::sceKernel, 0, "calling %08x", g_guestCallSlots[N]);
}

static const char *const GUEST_CALL_MODULE = "SystemCtrlGuestCalls";
static const u32 GUEST_CALL_NID_BASE = 0x5C7C0000;

static const HLEFunction SystemCtrlGuestCalls[] = {
	{GUEST_CALL_NID_BASE + 0, &WrapI_V<SystemCtrlGuestCall<0>>, "SystemCtrlGuestCall0", 'i', ""},
	{GUEST_CALL_NID_BASE + 1, &WrapI_V<SystemCtrlGuestCall<1>>, "SystemCtrlGuestCall1", 'i', ""},
	{GUEST_CALL_NID_BASE + 2, &WrapI_V<SystemCtrlGuestCall<2>>, "SystemCtrlGuestCall2", 'i', ""},
	{GUEST_CALL_NID_BASE + 3, &WrapI_V<SystemCtrlGuestCall<3>>, "SystemCtrlGuestCall3", 'i', ""},
	{GUEST_CALL_NID_BASE + 4, &WrapI_V<SystemCtrlGuestCall<4>>, "SystemCtrlGuestCall4", 'i', ""},
	{GUEST_CALL_NID_BASE + 5, &WrapI_V<SystemCtrlGuestCall<5>>, "SystemCtrlGuestCall5", 'i', ""},
	{GUEST_CALL_NID_BASE + 6, &WrapI_V<SystemCtrlGuestCall<6>>, "SystemCtrlGuestCall6", 'i', ""},
	{GUEST_CALL_NID_BASE + 7, &WrapI_V<SystemCtrlGuestCall<7>>, "SystemCtrlGuestCall7", 'i', ""},
};
static_assert(ARRAY_SIZE(SystemCtrlGuestCalls) == GUEST_CALL_SLOTS, "one function per slot");

// The syscall number (the instruction's code field, as SYSCALL(n) = n << 6 | 0xC builds it)
// that calls func.
static int sctrlKernelQuerySystemCall(u32 func) {
	if (!Memory::IsValid4AlignedAddress(func)) {
		return hleLogError(Log::sceKernel, -1, "bad address");
	}
	int slot = -1;
	for (int i = 0; i < GUEST_CALL_SLOTS; ++i) {
		if (g_guestCallSlots[i] == func) {
			slot = i;
			break;
		}
		if (!g_guestCallSlots[i] && slot < 0) {
			slot = i;
		}
	}
	if (slot < 0) {
		return hleLogError(Log::sceKernel, -1, "out of slots");
	}
	g_guestCallSlots[slot] = func;
	return hleLogInfo(Log::sceKernel, GetSyscallOp(GUEST_CALL_MODULE, GUEST_CALL_NID_BASE + slot) >> 6, "slot %d", slot);
}

// systemctrl's heaps: oe_malloc from the kernel's, user_malloc (exported in SysclibForUser) from
// the user partition. vshctrl keeps its ISO caches and directory listings in them.
static u32 oe_malloc(u32 size) {
	if (size == 0) {
		return hleLogWarning(Log::sceKernel, 0, "size 0");
	}
	const u32 addr = kernelMemory.Alloc(size, false, "oe_malloc");
	return hleLogDebug(Log::sceKernel, addr == (u32)-1 ? 0 : addr);
}

static u32 user_malloc(u32 size) {
	if (size == 0) {
		return hleLogWarning(Log::sceKernel, 0, "size 0");
	}
	const u32 addr = userMemory.Alloc(size, true, "user_malloc");
	return hleLogDebug(Log::sceKernel, addr == (u32)-1 ? 0 : addr);
}

static int oe_free(u32 ptr) {
	if (ptr == 0) {
		return hleLogDebug(Log::sceKernel, 0);
	}
	const u32 addr = ptr & 0x3FFFFFFF;
	BlockAllocator &heap = addr >= PSP_GetUserMemoryBase() ? userMemory : kernelMemory;
	if (!heap.Free(addr)) {
		return hleLogWarning(Log::sceKernel, 0, "not allocated");
	}
	return hleLogDebug(Log::sceKernel, 0);
}

// The VSH menu saving its settings. SEConfig as of ARK-4.18: a u32, two s16 and 26 bytes.
static const u32 SE_CONFIG_USED_SIZE = 34;

static int sctrlSESetConfig(u32 confAddr) {
	const u32 addr = SEConfigAddr();
	if (!addr || !Memory::IsValidRange(confAddr, SE_CONFIG_USED_SIZE)) {
		return hleLogError(Log::sceKernel, -1, "bad config");
	}
	Memory::Memcpy(addr, confAddr, SE_CONFIG_USED_SIZE, "sctrlSESetConfig");
	return hleLogInfo(Log::sceKernel, 0);
}

// An LZ4 block, which has to decompress to exactly originalSize bytes. Returns how much of the
// input it read, or a negative number on bad data. For ZSO images.
static int LZ4_decompress_fast(u32 src, u32 dst, int originalSize) {
	if (originalSize < 0 || !Memory::IsValidRange(dst, originalSize)) {
		return hleLogError(Log::sceKernel, -1, "bad output");
	}
	u8 *out = Memory::GetPointerWriteUnchecked(dst);
	u32 ip = src;
	int op = 0;
	bool ok = true;
	auto next = [&]() -> u8 {
		if (!Memory::IsValidAddress(ip)) {
			ok = false;
			return 0;
		}
		return Memory::ReadUnchecked_U8(ip++);
	};
	auto length = [&](int len) {
		if (len == 15) {
			u8 b;
			do {
				b = next();
				len += b;
			} while (b == 255 && ok);
		}
		return len;
	};
	while (ok) {
		const u8 token = next();
		const int literals = length(token >> 4);
		if (!ok || op + literals > originalSize || !Memory::IsValidRange(ip, literals)) {
			return hleLogError(Log::sceKernel, -(int)(ip - src) - 1, "bad literals");
		}
		memcpy(out + op, Memory::GetPointerUnchecked(ip), literals);
		ip += literals;
		op += literals;
		if (op == originalSize) {
			// The last sequence is literals only.
			break;
		}
		const u32 offset = next() | (next() << 8);
		const int matchLen = length(token & 15) + 4;
		if (!ok || offset == 0 || (int)offset > op || op + matchLen > originalSize) {
			return hleLogError(Log::sceKernel, -(int)(ip - src) - 1, "bad match");
		}
		// Byte by byte: the match may overlap what it's writing.
		for (int i = 0; i < matchLen; ++i, ++op) {
			out[op] = out[op - offset];
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, dst, originalSize, "LZ4_decompress_fast");
	return hleLogDebug(Log::sceKernel, (int)(ip - src));
}

// Used for some CSO v2 blocks. Not implemented: fail like corrupt data (LZO_E_ERROR).
static int lzo1x_decompress(u32 src, u32 srcLen, u32 dst, u32 dstLenPtr, u32 wrk) {
	return hleLogWarning(Log::sceKernel, -1, "not implemented");
}

// ARK-5 (PSP-Arkfive) additions.

static int sctrlFlushCache(MIPSState *mips) {
	mips->InvalidateICacheRangeDeferred(0, 0x3FFFFFFF);
	Core_ReenterDispatcher();
	return hleLogDebug(Log::sceKernel, 0);
}

// Copies ARK's reboot configuration out. Ours has nothing in it, and ARK-5's struct is longer than
// ARK-4's, so leave the caller's (XMB Control's, zeroed) copy alone: no last played game.
static u32 sctrlHENGetRebootexConfig(u32 configAddr) {
	return hleLogDebug(Log::sceKernel, 0);
}

// A hook around every sceKernelStartModule. vshctrl uses it only to load XMB Control as vsh_module
// starts, which on the PSP isn't the main module. Here it is, so we load XMB Control at boot
// instead (LoadArkXmbCtrl) and don't run the hook.
static u32 sctrlSetStartModuleExtra(u32 func) {
	const u32 previous = g_startModuleExtra;
	g_startModuleExtra = func;
	return hleLogInfo(Log::sceKernel, previous, "not called");
}

static int sctrlGetThreadUIDByName(const char *name) {
	if (!name) {
		return hleLogError(Log::sceKernel, -1, "no name");
	}
	for (const DebugThreadInfo &info : GetThreadsInfo()) {
		if (!strcmp(info.name, name)) {
			return hleLogDebug(Log::sceKernel, info.id);
		}
	}
	return hleLogDebug(Log::sceKernel, -1, "%s not found", name);
}

static u32 sctrlSEGetConfigInternal() {
	return hleLogDebug(Log::sceKernel, SEConfigAddr());
}

// Moves the first two instructions of func to patchData (a 20-byte trampoline that continues at
// func + 8), points func at patchFunc, and returns the trampoline through origFunc
// (sctrl_hen.c). The two instructions mustn't be PC-relative.
static int sctrlHENHijackFunction(u32 patchData, u32 func, u32 patchFunc, u32 origFuncPtr) {
	if (!Memory::IsValid4AlignedRange(patchData, 20) || !Memory::IsValid4AlignedRange(func, 8) || !Memory::IsValid4AlignedAddress(origFuncPtr)) {
		return hleLogError(Log::sceKernel, -1, "bad address");
	}
	Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(func), patchData);
	Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(func + 4), patchData + 4);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), patchData + 8);
	Memory::WriteUnchecked_U32(MIPS_MAKE_J(func + 8), patchData + 12);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), patchData + 16);
	Memory::WriteUnchecked_U32(MIPS_MAKE_J(patchFunc), func);
	Memory::WriteUnchecked_U32(MIPS_MAKE_NOP(), func + 4);
	Memory::WriteUnchecked_U32(patchData, origFuncPtr);
	currentMIPS->InvalidateICacheRangeDeferred(patchData, 20);
	currentMIPS->InvalidateICacheRangeDeferred(func, 8);
	// Callers of an HLE function don't go through our stub (XMB Control hooks sceDisplaySetFrameBuf
	// to draw the VSH menu), so point their imports at patchFunc too. The trampoline still makes the
	// syscall.
	__KernelHijackHLEFunction(func, patchFunc);
	return hleLogInfo(Log::sceKernel, 0, "%08x now goes to %08x", func, patchFunc);
}

static int sctrlArkSetConfig(u32 confAddr) {
	const u32 addr = ArkConfigAddr();
	if (!addr || !Memory::IsValidRange(confAddr, sizeof(ArkConfig))) {
		return hleLogError(Log::sceKernel, -1, "bad config");
	}
	Memory::Memcpy(addr, confAddr, sizeof(ArkConfig), "sctrlArkSetConfig");
	return hleLogInfo(Log::sceKernel, 0);
}

// Whether ARK's launcher, rather than the XMB, is what's running. ARK compares the init file name
// with the launcher's path. We don't boot into it.
static int sctrlArkIsLauncher() {
	return hleLogDebug(Log::sceKernel, 0);
}

static int sctrlArkExitLauncher() {
	return hleLogWarning(Log::sceKernel, 0, "not implemented");
}

static u32 user_memalign(u32 align, u32 size) {
	if (size == 0) {
		return hleLogWarning(Log::sceKernel, 0, "size 0");
	}
	// The user partition hands out 256-byte aligned blocks, which covers what's asked in practice.
	if (align > 0x100) {
		WARN_LOG(Log::sceKernel, "user_memalign: alignment %08x not supported", align);
	}
	const u32 addr = userMemory.Alloc(size, true, "user_memalign");
	return hleLogDebug(Log::sceKernel, addr == (u32)-1 ? 0 : addr);
}

static int sysclib_strcasecmp(const char *a, const char *b) {
	if (!a || !b) {
		return hleLogError(Log::sceKernel, 0, "bad string");
	}
	int i = 0;
	while (tolower((u8)a[i]) == tolower((u8)b[i]) && a[i]) {
		i++;
	}
	return hleLogVerbose(Log::sceKernel, tolower((u8)a[i]) - tolower((u8)b[i]));
}

static int sysclib_strncasecmp(u32 aAddr, u32 bAddr, u32 n) {
	if (!Memory::IsValidAddress(aAddr) || !Memory::IsValidAddress(bAddr)) {
		return hleLogError(Log::sceKernel, 0, "bad string");
	}
	for (u32 i = 0; i < n; ++i) {
		if (!Memory::IsValidAddress(aAddr + i) || !Memory::IsValidAddress(bAddr + i)) {
			return hleLogError(Log::sceKernel, 0, "bad string");
		}
		const int ca = tolower(Memory::ReadUnchecked_U8(aAddr + i));
		const int cb = tolower(Memory::ReadUnchecked_U8(bAddr + i));
		if (ca != cb || ca == 0) {
			return hleLogVerbose(Log::sceKernel, ca - cb);
		}
	}
	return hleLogVerbose(Log::sceKernel, 0);
}

static int sysclib_bcmp(u32 a, u32 b, u32 n) {
	if (!Memory::IsValidRange(a, n) || !Memory::IsValidRange(b, n)) {
		return hleLogError(Log::sceKernel, 0, "bad range");
	}
	return hleLogVerbose(Log::sceKernel, memcmp(Memory::GetPointerUnchecked(a), Memory::GetPointerUnchecked(b), n) != 0 ? 1 : 0);
}

// SysMemForKernel's game info block (SceKernelGameInfo). vshctrl sets the "opnssmp" type in it
// when it boots an ISO, which nothing here reads.
u32 sceKernelGetGameInfo() {
	if (!g_gameInfoAddr) {
		g_gameInfoAddr = AllocZeroed(0x220, "SceKernelGameInfo");
	}
	return hleLogDebug(Log::sceKernel, g_gameInfoAddr);
}

// Driver functions that talk to hardware we don't emulate, for what CFW looks up with
// sctrlHENFindFunction (ARK-5's XMB Control, opening Custom Firmware Settings): the real syscon
// driver's command waits for an interrupt that never comes. Same NIDs, in a library of our own so
// the firmware's own modules keep theirs.
static const char *const HARDWARE_MODULE = "SystemCtrlHardware";

// The motherboard's chip versions, for the model we emulate (TA-079 for a PSP-1000, TA-085 for
// a PSP-2000). XMB Control only reads the battery's EEPROM (by syscon command) when they could be
// a model that takes a Pandora battery.
static u32 HardwareTachyonVersion() {
	return g_Config.iPSPModel == PSP_MODEL_FAT ? 0x00140000 : 0x00500000;
}

static u32 HardwareBaryonVersion() {
	return g_Config.iPSPModel == PSP_MODEL_FAT ? 0x00030600 : 0x0022B200;
}

static int sceSysconGetBaryonVersion(u32 outPtr) {
	if (Memory::IsValid4AlignedAddress(outPtr)) {
		Memory::WriteUnchecked_U32(HardwareBaryonVersion(), outPtr);
	}
	return hleLogDebug(Log::sceKernel, 0);
}

static u32 sceSysregGetTachyonVersion() {
	return hleLogDebug(Log::sceKernel, HardwareTachyonVersion());
}

// No syscon to send it to: fail like a command the chip didn't answer.
static int sceSysconCmdExec(u32 paramPtr, int flags) {
	return hleLogWarning(Log::sceKernel, (int)SCE_KERNEL_ERROR_ERROR, "no syscon");
}

static const HLEFunction SystemCtrlHardware[] = {
	{0x7EC5A957, &WrapI_U<sceSysconGetBaryonVersion>,               "sceSysconGetBaryonVersion",         'i', "x",    HLE_KERNEL_SYSCALL},
	{0xE2A5D1EE, &WrapU_V<sceSysregGetTachyonVersion>,              "sceSysregGetTachyonVersion",        'x', "",     HLE_KERNEL_SYSCALL},
	{0x5B9ACC97, &WrapI_UI<sceSysconCmdExec>,                       "sceSysconCmdExec",                  'i', "xi",   HLE_KERNEL_SYSCALL},
};

const char *__SystemCtrlHardwareLibrary(const char *library, u32 nid) {
	const bool syscon = !strcmp(library, "sceSyscon_driver") && (nid == 0x7EC5A957 || nid == 0x5B9ACC97);
	const bool sysreg = !strcmp(library, "sceSysreg_driver") && nid == 0xE2A5D1EE;
	return syscon || sysreg ? HARDWARE_MODULE : nullptr;
}

static u32 WrapHookImportByNID(u32 modPtr, const char *library, u32 nid, u32 func) {
	return sctrlHookImportByNID(modPtr, library, nid, func);
}

static const HLEFunction SystemCtrlForKernel[] = {
	{0x1C90BECB, &WrapU_U<sctrlHENSetStartModuleHandler>,           "sctrlHENSetStartModuleHandler",     'x', "x",    HLE_KERNEL_SYSCALL},
	{0x78E46415, &WrapU_C<sctrlHENFindDriver>,                      "sctrlHENFindDriver",                'x', "s",    HLE_KERNEL_SYSCALL},
	{0x159AF5CC, &WrapU_CCU<sctrlHENFindFunction>,                  "sctrlHENFindFunction",              'x', "ssx",  HLE_KERNEL_SYSCALL},
	{0x72F47790, &WrapU_UCUU<WrapHookImportByNID>,                  "sctrlHookImportByNID",              'i', "xsxx", HLE_KERNEL_SYSCALL},
	{0xF988C1DC, &WrapV_UU<sctrlHENPatchSyscall>,                   "sctrlHENPatchSyscall",              'v', "xx",   HLE_KERNEL_SYSCALL},
	{0x56CEAF00, &WrapI_U<sctrlKernelQuerySystemCall>,              "sctrlKernelQuerySystemCall",        'i', "x",    HLE_KERNEL_SYSCALL},
	{0xB00B1E55, &WrapU_U<sctrlArkGetConfig>,                       "sctrlArkGetConfig",                 'x', "x",    HLE_KERNEL_SYSCALL},
	{0x16C3B7EE, &WrapU_U<sctrlSEGetConfig>,                        "sctrlSEGetConfig",                  'x', "x",    HLE_KERNEL_SYSCALL},
	{0xBA21998E, &WrapU_V<sctrlSEGetUmdFile>,                       "sctrlSEGetUmdFile",                 'x', "",     HLE_KERNEL_SYSCALL},
	{0x5A35C948, &WrapI_C<sctrlSESetUmdFile>,                       "sctrlSESetUmdFile",                 'i', "s",    HLE_KERNEL_SYSCALL},
	{0x70B92B45, &WrapI_V<sctrlSEGetBootConfFileIndex>,             "sctrlSEGetBootConfFileIndex",       'i', "",     HLE_KERNEL_SYSCALL},
	{0xBC939DC1, &WrapI_I<sctrlSESetBootConfFileIndex>,             "sctrlSESetBootConfFileIndex",       'i', "i",    HLE_KERNEL_SYSCALL},
	{0x31C6160D, &WrapI_I<sctrlSESetDiscType>,                      "sctrlSESetDiscType",                'i', "i",    HLE_KERNEL_SYSCALL},
	{0xB47C9D77, &WrapI_V<sctrlSEGetVersion>,                       "sctrlSEGetVersion",                 'i', "",     HLE_KERNEL_SYSCALL},
	{0x1090A2E1, &WrapI_V<sctrlHENGetVersion>,                      "sctrlHENGetVersion",                'i', "",     HLE_KERNEL_SYSCALL},
	{0x5328B431, &WrapI_V<sctrlHENGetMinorVersion>,                 "sctrlHENGetMinorVersion",           'i', "",     HLE_KERNEL_SYSCALL},
	{0x22A45DB7, &WrapI_V<sctrlHENIsToolKit>,                       "sctrlHENIsToolKit",                 'i', "",     HLE_KERNEL_SYSCALL},
	{0xEB74FE45, &WrapI_I<sctrlKernelSetUserLevel>,                 "sctrlKernelSetUserLevel",           'i', "i",    HLE_KERNEL_SYSCALL},
	{0x2D10FB28, &WrapI_ICU<sctrlKernelLoadExecVSHWithApitype>,     "sctrlKernelLoadExecVSHWithApitype", 'i', "isx", HLE_KERNEL_SYSCALL},
	{0xABA7F1B0, &WrapI_CU<sctrlKernelLoadExecVSHMs2>,              "sctrlKernelLoadExecVSHMs2",         'i', "sx",   HLE_KERNEL_SYSCALL},
	{0xAF22D576, &WrapI_CU<sctrlKernelLoadExecVSHMs2>,              "sctrlKernelLoadExecVSHEf2",         'i', "sx",   HLE_KERNEL_SYSCALL},
	{0x2794CCF4, &WrapI_U<sctrlKernelExitVSH>,                      "sctrlKernelExitVSH",                'i', "x",    HLE_KERNEL_SYSCALL},
	{0xF9584CAD, &WrapU_U<oe_malloc>,                               "oe_malloc",                         'x', "x",    HLE_KERNEL_SYSCALL},
	{0xA65E8BC4, &WrapI_U<oe_free>,                                 "oe_free",                           'i', "x",    HLE_KERNEL_SYSCALL},
	// Before ARK-4's 2025 API cleanup this was hookImportByNID.
	{0x869F24E9, &WrapU_UCUU<WrapHookImportByNID>,                  "hookImportByNID",                   'i', "xsxx", HLE_KERNEL_SYSCALL},
	{0x1DDDAD0C, &WrapI_U<sctrlSESetConfig>,                        "sctrlSESetConfig",                  'i', "x",    HLE_KERNEL_SYSCALL},
	{0x16100529, &WrapI_UUI<LZ4_decompress_fast>,                   "LZ4_decompress_fast",               'i', "xxi",  HLE_KERNEL_SYSCALL},
	{0x76C382FF, &WrapI_UUUUU<lzo1x_decompress>,                    "lzo1x_decompress",                  'i', "xxxxx", HLE_KERNEL_SYSCALL},
	{0x17691875, &WrapI_V<sctrlFlushCache>,                         "sctrlFlushCache",                   'i', "",     HLE_KERNEL_SYSCALL},
	{0x221400A6, &WrapU_U<sctrlSetStartModuleExtra>,                "sctrlSetStartModuleExtra",          'x', "x",    HLE_KERNEL_SYSCALL},
	{0x23833651, &WrapI_C<sctrlGetThreadUIDByName>,                 "sctrlGetThreadUIDByName",           'i', "s",    HLE_KERNEL_SYSCALL},
	{0xD67E7551, &WrapU_V<sctrlSEGetConfigInternal>,                "sctrlSEGetConfigInternal",          'x', "",     HLE_KERNEL_SYSCALL},
	{0xDB39A3EC, &WrapI_UUUU<sctrlHENHijackFunction>,               "sctrlHENHijackFunction",            'i', "xxxx", HLE_KERNEL_SYSCALL},
	{0x18B687A6, &WrapU_U<sctrlHENGetRebootexConfig>,               "sctrlHENGetRebootexConfig",         'x', "x",    HLE_KERNEL_SYSCALL},
};

// ARK's VSH menu is a user module, so it goes through these.
static const HLEFunction SystemCtrlForUser[] = {
	{0x159AF5CC, &WrapU_CCU<sctrlHENFindFunction>,                  "sctrlHENFindFunction",              'x', "ssx"},
	{0x2D10FB28, &WrapI_ICU<sctrlKernelLoadExecVSHWithApitype>,     "sctrlKernelLoadExecVSHWithApitype", 'i', "isx"},
	{0x5328B431, &WrapI_V<sctrlHENGetMinorVersion>,                 "sctrlHENGetMinorVersion",           'i', ""},
	{0x5A35C948, &WrapI_C<sctrlSESetUmdFile>,                       "sctrlSESetUmdFile",                 'i', "s"},
	{0xBC939DC1, &WrapI_I<sctrlSESetBootConfFileIndex>,             "sctrlSESetBootConfFileIndex",       'i', "i"},
	{0xB00B1E55, &WrapU_U<sctrlArkGetConfig>,                       "sctrlArkGetConfig",                 'x', "x"},
	{0x2794CCF4, &WrapI_U<sctrlKernelExitVSH>,                      "sctrlKernelExitVSH",                'i', "x"},
	{0x1090A2E1, &WrapI_V<sctrlHENGetVersion>,                      "sctrlHENGetVersion",                'i', ""},
	{0x16C3B7EE, &WrapU_U<sctrlSEGetConfig>,                        "sctrlSEGetConfig",                  'x', "x"},
	{0x1DDDAD0C, &WrapI_U<sctrlSESetConfig>,                        "sctrlSESetConfig",                  'i', "x"},
	{0xBA21998E, &WrapU_V<sctrlSEGetUmdFile>,                       "sctrlSEGetUmdFile",                 'x', ""},
	{0x70B92B45, &WrapI_V<sctrlSEGetBootConfFileIndex>,             "sctrlSEGetBootConfFileIndex",       'i', ""},
	{0x17691875, &WrapI_V<sctrlFlushCache>,                         "sctrlFlushCache",                   'i', ""},
	{0x1C90BECB, &WrapU_U<sctrlHENSetStartModuleHandler>,           "sctrlHENSetStartModuleHandler",     'x', "x"},
	{0x22A45DB7, &WrapI_V<sctrlHENIsToolKit>,                       "sctrlHENIsToolKit",                 'i', ""},
	{0x72F47790, &WrapU_UCUU<WrapHookImportByNID>,                  "sctrlHookImportByNID",              'i', "xsxx"},
	{0xB47C9D77, &WrapI_V<sctrlSEGetVersion>,                       "sctrlSEGetVersion",                 'i', ""},
	{0xDB39A3EC, &WrapI_UUUU<sctrlHENHijackFunction>,               "sctrlHENHijackFunction",            'i', "xxxx"},
};

// ARK-5 moved its config calls here.
static const HLEFunction ArkCtrl[] = {
	{0xB00B1E55, &WrapU_U<sctrlArkGetConfig>,                       "sctrlArkGetConfig",                 'x', "x"},
	{0x6EAFC03D, &WrapI_U<sctrlArkSetConfig>,                       "sctrlArkSetConfig",                 'i', "x"},
	{0x8476E2F1, &WrapI_V<sctrlArkExitLauncher>,                    "sctrlArkExitLauncher",              'i', ""},
	{0x65C8280F, &WrapI_V<sctrlArkIsLauncher>,                      "sctrlArkIsLauncher",                'i', ""},
};

// ARK-4's, not the firmware's: systemctrl exports a libc for modules to share.
static const HLEFunction SysclibForUser[] = {
	{0x4C0FE24C, &WrapU_U<user_malloc>,                             "user_malloc",                       'x', "x"},
	{0xA65E8BC4, &WrapI_U<oe_free>,                                 "oe_free",                           'i', "x"},
	{0x12BC667F, &WrapI_U<oe_free>,                                 "oe_free",                           'i', "x"},
	{0xB19C939A, &WrapU_UU<user_memalign>,                          "user_memalign",                     'x', "xx"},
	{0xBC7554DF, &WrapI_CC<sysclib_strcasecmp>,                     "strcasecmp",                        'i', "ss"},
	{0xAC7554DF, &WrapI_UUU<sysclib_strncasecmp>,                   "strncasecmp",                       'i', "xxx"},
	{0x7F8A6F23, &WrapI_UUU<sysclib_bcmp>,                          "bcmp",                              'i', "xxx"},
};

void Register_SystemCtrlForKernel() {
	RegisterHLEModule("SystemCtrlForKernel", ARRAY_SIZE(SystemCtrlForKernel), SystemCtrlForKernel);
}

void Register_SystemCtrlForUser() {
	RegisterHLEModule("SystemCtrlForUser", ARRAY_SIZE(SystemCtrlForUser), SystemCtrlForUser);
}

void Register_SystemCtrlGuestCalls() {
	RegisterHLEModule(GUEST_CALL_MODULE, ARRAY_SIZE(SystemCtrlGuestCalls), SystemCtrlGuestCalls);
}

void Register_SysclibForUser() {
	RegisterHLEModule("SysclibForUser", ARRAY_SIZE(SysclibForUser), SysclibForUser);
}

void Register_ArkCtrl() {
	RegisterHLEModule("ArkCtrl", ARRAY_SIZE(ArkCtrl), ArkCtrl);
}

void Register_SystemCtrlHardware() {
	RegisterHLEModule(HARDWARE_MODULE, ARRAY_SIZE(SystemCtrlHardware), SystemCtrlHardware);
}
