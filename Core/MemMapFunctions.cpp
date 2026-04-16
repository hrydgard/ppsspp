// Copyright (C) 2003 Dolphin Project / 2012 PPSSPP Project

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

#include "Common/CommonTypes.h"
#include "Common/LogReporting.h"
#include "Common/MemoryUtil.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelInterrupt.h"

#if !defined(_WIN32)
#include <sys/mman.h>
#endif

namespace Memory {

// ME HW register storage backed by the mapped page at base + 0xBC100000.
// Non-mapped registers still use static variables.

// ME eDRAM buffer (2MB at physical 0x00000000-0x001FFFFF, kseg0 0x80000000-0x801FFFFF)
static u8 *meEdram = nullptr;
static const u32 ME_EDRAM_SIZE = 0x00200000; // 2MB

// Returns a pointer to the mapped HW register page (uncached view 0xBC100000).
static inline u32 *MeHwRegPtr(u32 offset) {
	return (u32 *)(Memory::base + 0xBC100000 + offset);
}

// Register offsets within the 4KB HW register page (base 0xBC100000):
static constexpr u32 ME_HWREG_TACHYON    = 0x40; // 0xBC100040
static constexpr u32 ME_HWREG_SOFTINT    = 0x44; // 0xBC100044, software interrupt SC->ME
static constexpr u32 ME_HWREG_MUTEX      = 0x48; // 0xBC100048
static constexpr u32 ME_HWREG_RESET      = 0x4C; // 0xBC10004C
static constexpr u32 ME_HWREG_BUSCLK     = 0x50; // 0xBC100050

// ME interrupt controller (system-level) at 0xBC300000:
// These are software-maintained, not memory-mapped via MeHwRegPtr.
static u32 meIntrFlags_ = 0;   // 0xBC300000, interrupt flags (write 1 to clear)
static u32 meIntrEnable_ = 0;  // 0xBC300008, interrupt enable mask

// Pending external interrupt flag.  Set when the main CPU writes to
// 0xBC100044 (soft interrupt) while ME interrupts are enabled.  Checked
// at the top of each ME instruction in the interpreter / at block
// boundaries in IR/JIT.
static bool mePendingInterrupt_ = false;
static bool meCpuInterruptPending_ = false;

// ---------- DMACplus controller (0xBC800xxx) ----------
//
// Three DMA channels:
//   Ch0 (0xBC800180): SC->ME
//   Ch1 (0xBC8001A0): ME->SC
//   Ch2 (0xBC8001C0): SC->SC
// LCDC registers at 0xBC800100-0xBC80010C.
//
// Transfers execute synchronously when the status register is written
// with bit 0 (channel enable) set.  LLI chains are followed immediately.

struct DmacPlusChannel {
	u32 src;
	u32 dst;
	u32 next;   // pointer to next LLI descriptor (0 = end of chain)
	u32 ctrl;
	u32 status; // bit 0 = channel enable/active
};

static DmacPlusChannel dmacChannels_[3];
static u32 lcdcRegs_[4]; // [0]=bufAddr, [1]=pixFmt, [2]=bufSize, [3]=stride

// ---------- VME Color Space Conversion (0xBC800120–0xBC800160) ----------
//
// Hardware YCbCr->RGB conversion. ME eDRAM holds Y/Cb/Cr planes;
// the CSC engine reads them, applies a 3x3 color matrix, and writes
// 8888-format pixels to a VRAM destination buffer.
//
// Registers:
//   0xBC800120  Y source (ME eDRAM physical address)
//   0xBC800130  Cb source
//   0xBC800134  Cr source
//   0xBC800140  Control: blocks_h<<16 | blocks_w<<8 | flags
//   0xBC800144  RGB destination 1 (VRAM)
//   0xBC800148  RGB destination 2 (unused)
//   0xBC80014C  Stride/format: stride<<8 | pixfmt<<1 | separate_dst
//   0xBC800150  Matrix row R  (Cr_coeff<<20 | Cb_coeff<<10 | Y_coeff)
//   0xBC800154  Matrix row G
//   0xBC800158  Matrix row B
//   0xBC800160  Start CSC (write 1)

struct CscState {
	u32 yAddr;      // 0xBC800120
	u32 cbAddr;     // 0xBC800130
	u32 crAddr;     // 0xBC800134
	u32 control;    // 0xBC800140
	u32 dstAddr1;   // 0xBC800144
	u32 dstAddr2;   // 0xBC800148
	u32 strideCtrl; // 0xBC80014C
	u32 matrixR;    // 0xBC800150
	u32 matrixG;    // 0xBC800154
	u32 matrixB;    // 0xBC800158
};

static CscState cscState_;

// Implemented after GetPointerWrite() / GetMeEdramPointer().
static u8 *DmaResolveAddress(u32 addr, bool isMeBus);
static void DmaExecuteTransfer(u32 src, u32 dst, u32 ctrl, bool srcME, bool dstME);
static void DmaExecuteChannel(int ch);
static void CscExecute();

void InitMeEdram() {
	if (!meEdram) {
		meEdram = new u8[ME_EDRAM_SIZE]();
	}
	// Seed the reset register with a non-zero sentinel so that the
	// first write of 0x00 (ME boot) is detected as a transition.
	if (Memory::base) {
		*MeHwRegPtr(ME_HWREG_RESET) = 1;
		// Make the HW register page read-only so JIT writes fault through
		// HandleMeHwFault() -> WriteMeHwRegister() -> Core_EnableME().
		if (g_Config.iMECpuCore >= 0) {
			ME_ProtectHwPage(true);
		}
	}
}

void ShutdownMeEdram() {
	delete[] meEdram;
	meEdram = nullptr;
}

static inline bool IsMeEdramAddress(u32 address) {
	u32 phys = address & 0x1FFFFFFF;
	return phys < ME_EDRAM_SIZE && phys >= 0x00010000;
}

static inline u8 *GetMeEdramPointer(u32 address) {
	u32 phys = address & 0x1FFFFFFF;
	if (phys >= 0x00020000) {
		// Arena-backed region: physical view at 0x00020000, cached mirror at 0x80020000.
		return Memory::base + phys;
	}
	// Fallback for lower eDRAM (0x10000-0x1FFFF, not arena-backed due to scratchpad overlap).
	return meEdram + phys;
}

static inline bool IsMeSramAddress(u32 address) {
	return (address & 0x1FFFF000) == 0x1FC00000;
}

static inline bool IsMeHwRegister(u32 address) {
	u32 phys = address & 0x1FFFFFFF;
	return (phys >= 0x1C000000 && phys < 0x1D100000);
}

// Finer-grained check for specific ME hardware register pages.
// Used by IR backends and validation passes to detect MMIO accesses
// that must go through ReadFromHardware/WriteToHardware.
//
// Physical ranges (after masking with 0x1FFFFFFF):
//   0x1C000000 - System Controller (power, clock, reset control)
//   0x1C100000 - ME interrupt / soft-interrupt registers (0xBC100044, 0xBC100048, etc.)
//   0x1C200000 - ME/SC communication registers
//   0x1C300000 - Additional system control
//   0x1CC00000 - VME (Video ME) registers (CSC, etc.)
//   0x1D000000 - DMACplus registers
bool IsMeSensitiveHwPage(u32 address) {
	u32 phys = address & 0x1FFFFFFF;
	return (phys >= 0x1C000000 && phys < 0x1C001000) ||
		(phys >= 0x1C100000 && phys < 0x1C101000) ||
		(phys >= 0x1C200000 && phys < 0x1C201000) ||
		(phys >= 0x1C300000 && phys < 0x1C301000) ||
		(phys >= 0x1CC00000 && phys < 0x1CC01000) ||
		(phys >= 0x1D000000 && phys < 0x1D001000);
}

// The page at 0xBC100000 (physical 0x1C100000) is mapped in the arena.
// The JIT does raw LDR/STR there, and the interpreter goes through these
// helpers.  Per-CPU mutex masking is NOT applied because the emulator is
// single-threaded - there is no true concurrent access, so both CPUs'
// operations are naturally serialized.

// ---------- ME HW register page protection ----------
//
// When the ME is configured (iMECpuCore >= 0), the HW register page at
// 0xBC100000 (and its physical mirror at 0x1C100000) is initially mapped
// read-only.  The main CPU's JIT writes to 0xBC10004C (reset register)
// and 0xBC100048 (HW mutex) via raw STR instructions.  The first such
// write faults; HandleMeHwFault() emulates it through WriteMeHwRegister()
// (which detects the ME boot transition) and then makes the page RW so
// subsequent accesses are direct and zero-cost.

static bool meHwPageReadOnly_ = false;

// Use mprotect directly instead of ProtectMemoryPages because the latter
// is hijacked by pthread_jit_write_protect_np on macOS ARM64 (W^X mode),
// which only affects JIT pages, not data pages in the memory arena.
static void MeHwMprotect(bool readOnly) {
#if !defined(_WIN32)
	int prot = readOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
	mprotect(Memory::base + 0xBC100000, 0x1000, prot);
	mprotect(Memory::base + 0x1C100000, 0x1000, prot);
#endif
}

static u32 ReadMeHwRegister(u32 address) {
	// Normalise to uncached virtual address for the switch.
	u32 virt = 0xBC000000 | (address & 0x01FFFFFF);
	switch (virt) {
	case 0xBC100040: return *MeHwRegPtr(ME_HWREG_TACHYON);
	case 0xBC100044: return *MeHwRegPtr(ME_HWREG_SOFTINT);
	case 0xBC100048: return *MeHwRegPtr(ME_HWREG_MUTEX);
	case 0xBC10004C: return *MeHwRegPtr(ME_HWREG_RESET);
	case 0xBC100050: return *MeHwRegPtr(ME_HWREG_BUSCLK);
	case 0xBC300000: return meIntrFlags_;      // ME interrupt flags
	case 0xBC300008: return meIntrEnable_;     // ME interrupt enable mask
	case 0xBCC00010: return 0; // VME reset: 0 = reset complete

	// DMACplus LCDC registers
	case 0xBC800100: return lcdcRegs_[0];
	case 0xBC800104: return lcdcRegs_[1];
	case 0xBC800108: return lcdcRegs_[2];
	case 0xBC80010C: return lcdcRegs_[3];

	// CSC registers
	case 0xBC800120: return cscState_.yAddr;
	case 0xBC800130: return cscState_.cbAddr;
	case 0xBC800134: return cscState_.crAddr;
	case 0xBC800140: return cscState_.control;
	case 0xBC800144: return cscState_.dstAddr1;
	case 0xBC800148: return cscState_.dstAddr2;
	case 0xBC80014C: return cscState_.strideCtrl;
	case 0xBC800150: return cscState_.matrixR;
	case 0xBC800154: return cscState_.matrixG;
	case 0xBC800158: return cscState_.matrixB;
	case 0xBC800160: return 0; // CSC status: 0 = idle

	// DMACplus Channel 0 (SC->ME) 0xBC800180
	case 0xBC800180: return dmacChannels_[0].src;
	case 0xBC800184: return dmacChannels_[0].dst;
	case 0xBC800188: return dmacChannels_[0].next;
	case 0xBC80018C: return dmacChannels_[0].ctrl;
	case 0xBC800190: return dmacChannels_[0].status;

	// DMACplus Channel 1 (ME->SC) 0xBC8001A0
	case 0xBC8001A0: return dmacChannels_[1].src;
	case 0xBC8001A4: return dmacChannels_[1].dst;
	case 0xBC8001A8: return dmacChannels_[1].next;
	case 0xBC8001AC: return dmacChannels_[1].ctrl;
	case 0xBC8001B0: return dmacChannels_[1].status;

	// DMACplus Channel 2 (SC->SC) 0xBC8001C0
	case 0xBC8001C0: return dmacChannels_[2].src;
	case 0xBC8001C4: return dmacChannels_[2].dst;
	case 0xBC8001C8: return dmacChannels_[2].next;
	case 0xBC8001CC: return dmacChannels_[2].ctrl;
	case 0xBC8001D0: return dmacChannels_[2].status;
	default:
		return 0;
	}
}

static void WriteMeHwRegister(u32 address, u32 value) {
	// Temporarily lift page protection so the write doesn't fault.
	// This is needed for the interpreter path (which calls this directly)
	// when the page is still read-only before ME boot.
	bool wasReadOnly = meHwPageReadOnly_;
	if (wasReadOnly)
		MeHwMprotect(false);

	u32 virt = 0xBC000000 | (address & 0x01FFFFFF);
	switch (virt) {
	case 0xBC100050: *MeHwRegPtr(ME_HWREG_BUSCLK) = value; break;
	case 0xBC100040: *MeHwRegPtr(ME_HWREG_TACHYON) = value; break;
	case 0xBC100044:
		// Software interrupt register shared between SC->ME and ME->SC.
		// If the current writer is the ME, route it to the main CPU's MECODEC
		// interrupt instead of treating it as a self-interrupt on the ME.
		if (currentMIPS == &mipsMe) {
			if (value != 0) {
				meCpuInterruptPending_ = true;
			}
			*MeHwRegPtr(ME_HWREG_SOFTINT) = 0;
			break;
		}

		// SC->ME: main CPU writes here to signal ME.
		*MeHwRegPtr(ME_HWREG_SOFTINT) = value;
		meIntrFlags_ |= 0x80000000;  // Set pending external interrupt flag
		if (value != 0)
			mePendingInterrupt_ = true;
		break;
	case 0xBC10004C: {
		u32 prev = *MeHwRegPtr(ME_HWREG_RESET);
		if (prev != 0 && value == 0) {
			Core_EnableME();
		}
		*MeHwRegPtr(ME_HWREG_RESET) = value;
		break;
	}
	case 0xBC100048:
		*MeHwRegPtr(ME_HWREG_MUTEX) = value;
		break;
	case 0xBC300000:
		// ME interrupt flags: write 1 to clear bits.
		meIntrFlags_ &= ~value;
		// Also clear the raw softint register if the corresponding flag was cleared.
		if (value & 0x80000000)
			*MeHwRegPtr(ME_HWREG_SOFTINT) = 0;
		if (!(meIntrFlags_ & meIntrEnable_))
			mePendingInterrupt_ = false;
		break;
	case 0xBC300008:
		// ME interrupt enable mask.
		meIntrEnable_ = value;
		if (meIntrFlags_ & meIntrEnable_)
			mePendingInterrupt_ = true;
		else
			mePendingInterrupt_ = false;
		break;

	// DMACplus LCDC registers (absorb writes)
	case 0xBC800100: lcdcRegs_[0] = value; break;
	case 0xBC800104: lcdcRegs_[1] = value; break;
	case 0xBC800108: lcdcRegs_[2] = value; break;
	case 0xBC80010C: lcdcRegs_[3] = value; break;

	// CSC registers
	case 0xBC800120: cscState_.yAddr = value; break;
	case 0xBC800130: cscState_.cbAddr = value; break;
	case 0xBC800134: cscState_.crAddr = value; break;
	case 0xBC800140: cscState_.control = value; break;
	case 0xBC800144: cscState_.dstAddr1 = value; break;
	case 0xBC800148: cscState_.dstAddr2 = value; break;
	case 0xBC80014C: cscState_.strideCtrl = value; break;
	case 0xBC800150: cscState_.matrixR = value; break;
	case 0xBC800154: cscState_.matrixG = value; break;
	case 0xBC800158: cscState_.matrixB = value; break;
	case 0xBC800160:
		if (value & 1) CscExecute();
		break;

	// DMACplus Channel 0 (SC->ME) 0xBC800180
	case 0xBC800180: dmacChannels_[0].src = value; break;
	case 0xBC800184: dmacChannels_[0].dst = value; break;
	case 0xBC800188: dmacChannels_[0].next = value; break;
	case 0xBC80018C: dmacChannels_[0].ctrl = value; break;
	case 0xBC800190:
		dmacChannels_[0].status = value;
		if (value & 1) DmaExecuteChannel(0);
		break;

	// DMACplus Channel 1 (ME->SC) 0xBC8001A0
	case 0xBC8001A0: dmacChannels_[1].src = value; break;
	case 0xBC8001A4: dmacChannels_[1].dst = value; break;
	case 0xBC8001A8: dmacChannels_[1].next = value; break;
	case 0xBC8001AC: dmacChannels_[1].ctrl = value; break;
	case 0xBC8001B0:
		dmacChannels_[1].status = value;
		if (value & 1) DmaExecuteChannel(1);
		break;

	// DMACplus Channel 2 (SC->SC) 0xBC8001C0
	case 0xBC8001C0: dmacChannels_[2].src = value; break;
	case 0xBC8001C4: dmacChannels_[2].dst = value; break;
	case 0xBC8001C8: dmacChannels_[2].next = value; break;
	case 0xBC8001CC: dmacChannels_[2].ctrl = value; break;
	case 0xBC8001D0:
		dmacChannels_[2].status = value;
		if (value & 1) DmaExecuteChannel(2);
		break;

	// VME registers (stub - absorb writes silently)
	case 0xBCC00000: case 0xBCC00030: case 0xBCC00040: break;

	default:
		break;
	}

	// Re-protect if ME hasn't been enabled (Core_EnableME clears meHwPageReadOnly_).
	if (wasReadOnly && meHwPageReadOnly_)
		MeHwMprotect(true);
}

void ME_ProtectHwPage(bool readOnly) {
	if (!Memory::base)
		return;
	MeHwMprotect(readOnly);
	meHwPageReadOnly_ = readOnly;
}

bool HandleMeHwFault(u32 guestAddress, bool isWrite) {
	if (!meHwPageReadOnly_)
		return false;
	u32 phys = guestAddress & 0x1FFFFFFF;
	if (phys < 0x1C100000 || phys >= 0x1C101000)
		return false;
	if (!isWrite)
		return false;  // Reads shouldn't fault (page is readable).
	// Temporarily make page writable so the emulated store (via
	// Memory::Write_U32() -> WriteMeHwRegister() can update MeHwRegPtr().
	// WriteMeHwRegister may call Core_EnableME() which permanently
	// unprotects the page via ME_ProtectHwPage(false).
	MeHwMprotect(false);
	return true;  // Tell HandleFault to emulate the write and advance PC.
}

void HandleMeHwFaultPost() {
	// Called after HandleFault has emulated the write.  If ME_ProtectHwPage(false)
	// was NOT called (i.e. the write didn't trigger Core_EnableME), re-protect
	// the page so the next write also faults through.
	if (meHwPageReadOnly_) {
		MeHwMprotect(true);
	}
}

bool ME_HasPendingInterrupt() {
	return mePendingInterrupt_;
}

u32 ME_PeekSoftInterruptRaw() {
	return *MeHwRegPtr(ME_HWREG_SOFTINT);
}

bool ME_ConsumeCpuInterruptRequest() {
	bool pending = meCpuInterruptPending_;
	meCpuInterruptPending_ = false;
	return pending;
}

void ME_ClearSoftInterrupt() {
	// Called from ME_CheckAndDeliverInterrupt to consume the interrupt on delivery,
	// because the ME interpreter's write to 0xBC300000 (the normal ACK path) bypasses
	// WriteMeHwRegister and therefore never clears mePendingInterrupt_ or SOFTINT.
	*MeHwRegPtr(ME_HWREG_SOFTINT) = 0;
	meIntrFlags_ &= ~0x80000000u;
	mePendingInterrupt_ = false;
}

void ME_RaiseSoftInterrupt() {
	meIntrFlags_ |= 0x80000000;
	mePendingInterrupt_ = true;
}

void ME_ResetInterruptState() {
	meIntrFlags_ = 0;
	meIntrEnable_ = 0;
	mePendingInterrupt_ = false;
	meCpuInterruptPending_ = false;
	memset(dmacChannels_, 0, sizeof(dmacChannels_));
	memset(lcdcRegs_, 0, sizeof(lcdcRegs_));
	memset(&cscState_, 0, sizeof(cscState_));
	if (Memory::base) {
		bool wasReadOnly = meHwPageReadOnly_;
		if (wasReadOnly)
			MeHwMprotect(false);
		*MeHwRegPtr(ME_HWREG_SOFTINT) = 0;
		if (wasReadOnly)
			MeHwMprotect(true);
	}
}

u8 *GetPointerWrite(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) || // More RAM
		IsMeSramAddress(address)) { // ME SRAM
		return GetPointerWriteUnchecked(address);
	} else if (IsMeEdramAddress(address) && meEdram) {
		return GetMeEdramPointer(address);
	} else {
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
		return nullptr;
	}
}

const u8 *GetPointer(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) || // More RAM
		IsMeSramAddress(address)) { // ME SRAM
		return GetPointerUnchecked(address);
	} else if (IsMeEdramAddress(address) && meEdram) {
		return GetMeEdramPointer(address);
	} else {
		Core_MemoryException(address, 0, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
		return nullptr;
	}
}

// ---------- DMACplus function implementations ----------

static u8 *DmaResolveAddress(u32 addr, bool isMeBus) {
	if (isMeBus) {
		u32 phys = addr & 0x1FFFFFFF;
		if (phys < 0x00200000) {
			return GetMeEdramPointer(phys);
		}
		return nullptr;
	}
	return GetPointerWrite(addr);
}

static void DmaExecuteTransfer(u32 src, u32 dst, u32 ctrl, bool srcME, bool dstME) {
	u32 widthShift = (ctrl >> 18) & 0x7;
	u32 widthBytes = 1u << widthShift;
	u32 count = ctrl & 0xFFF;
	u32 totalBytes = widthBytes * count;

	if (totalBytes == 0)
		return;

	u8 *srcPtr = DmaResolveAddress(src, srcME);
	u8 *dstPtr = DmaResolveAddress(dst, dstME);

	if (srcPtr && dstPtr) {
		memcpy(dstPtr, srcPtr, totalBytes);
	}
}

static void DmaExecuteChannel(int ch) {
	DmacPlusChannel &c = dmacChannels_[ch];
	bool srcME = (c.ctrl >> 24) & 1;
	bool dstME = (c.ctrl >> 25) & 1;

	DmaExecuteTransfer(c.src, c.dst, c.ctrl, srcME, dstME);

	u32 nextAddr = c.next;
	int safety = 1024;
	while (nextAddr != 0 && --safety > 0) {
		u32 lli_src  = Read_U32(nextAddr + 0);
		u32 lli_dst  = Read_U32(nextAddr + 4);
		u32 lli_next = Read_U32(nextAddr + 8);
		u32 lli_ctrl = Read_U32(nextAddr + 12);

		bool lli_srcME = (lli_ctrl >> 24) & 1;
		bool lli_dstME = (lli_ctrl >> 25) & 1;
		DmaExecuteTransfer(lli_src, lli_dst, lli_ctrl, lli_srcME, lli_dstME);

		nextAddr = lli_next;
	}

	c.status = 0;
}

// ---------- VME CSC function implementation ----------

// Decode a Q3.7 fixed-point coefficient (10-bit, signed via 2's complement).
static inline float Q37ToFloat(u32 val) {
	val &= 0x3FF;
	if (val & 0x200)
		return (float)((int)val - 1024) / 128.0f;
	return (float)val / 128.0f;
}

static inline int Clamp255(int v) {
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void CscExecute() {
	u32 blocksW = (cscState_.control >> 8) & 0xFF;
	u32 blocksH = (cscState_.control >> 16) & 0xFFFF;
	u32 width  = blocksW * 16;
	u32 height = blocksH * 8;
	u32 stride = (cscState_.strideCtrl >> 8) & 0xFFFFFF;

	// Decode 3x3 colour matrix from registers (each row: Cr<<20 | Cb<<10 | Y).
	float yR  = Q37ToFloat(cscState_.matrixR);
	float cbR = Q37ToFloat(cscState_.matrixR >> 10);
	float crR = Q37ToFloat(cscState_.matrixR >> 20);
	float yG  = Q37ToFloat(cscState_.matrixG);
	float cbG = Q37ToFloat(cscState_.matrixG >> 10);
	float crG = Q37ToFloat(cscState_.matrixG >> 20);
	float yB  = Q37ToFloat(cscState_.matrixB);
	float cbB = Q37ToFloat(cscState_.matrixB >> 10);
	float crB = Q37ToFloat(cscState_.matrixB >> 20);

	// Resolve source pointers (ME eDRAM physical addresses).
	u8 *yPtr  = GetMeEdramPointer(cscState_.yAddr & 0x1FFFFFFF);
	u8 *cbPtr = GetMeEdramPointer(cscState_.cbAddr & 0x1FFFFFFF);
	u8 *crPtr = GetMeEdramPointer(cscState_.crAddr & 0x1FFFFFFF);

	// Resolve destination pointer (VRAM address, strip upper bits).
	u32 *dstPtr = (u32 *)GetPointerWrite(cscState_.dstAddr1 & 0x3FFFFFFF);
	if (!yPtr || !cbPtr || !crPtr || !dstPtr)
		return;

	// BT.601 YCbCr->RGB: subtract 16 from Y and 128 from Cb/Cr.
	u32 halfW = width / 2;
	for (u32 py = 0; py < height; py++) {
		for (u32 px = 0; px < width; px++) {
			float fY  = (float)yPtr[py * width + px] - 16.0f;
			float fCb = (float)cbPtr[(py / 2) * halfW + (px / 2)] - 128.0f;
			float fCr = (float)crPtr[(py / 2) * halfW + (px / 2)] - 128.0f;

			int R = Clamp255((int)(yR * fY + cbR * fCb + crR * fCr));
			int G = Clamp255((int)(yG * fY + cbG * fCb + crG * fCr));
			int B = Clamp255((int)(yB * fY + cbB * fCb + crB * fCr));

			dstPtr[py * stride + px] = 0xFF000000u | ((u32)B << 16) | ((u32)G << 8) | (u32)R;
		}
	}
}

u8 *GetPointerWriteRange(const u32 address, const u32 size) {
	u8 *ptr = GetPointerWrite(address);
	if (ptr) {
		if (ClampValidSizeAt(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::WRITE_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was handled in GetPointerWrite already, if we're not ignoring errors.
		return nullptr;
	}
}

const u8 *GetPointerRange(const u32 address, const u32 size) {
	const u8 *ptr = GetPointer(address);
	if (ptr) {
		if (ClampValidSizeAt(address, size) != size) {
			// That's a memory exception! TODO: Adjust reported address to the end of the range?
			Core_MemoryException(address, size, currentMIPS->pc, MemoryExceptionType::READ_BLOCK);
			return nullptr;
		} else {
			return ptr;
		}
	} else {
		// Error was handled in GetPointer already, if we're not ignoring errors.
		return nullptr;
	}
}

template <typename T>
inline void ReadFromHardware(T &var, const u32 address) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) || // More RAM
		IsMeSramAddress(address)) { // ME SRAM
		var = *((const T*)GetPointerUnchecked(address));
	} else if (IsMeEdramAddress(address) && meEdram) {
		var = *(const T*)GetMeEdramPointer(address);
	} else if (IsMeHwRegister(address) && sizeof(T) == 4) {
		var = (T)ReadMeHwRegister(address);
	} else {
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::READ_WORD);
		var = 0;
	}
}

template <typename T>
inline void WriteToHardware(u32 address, const T data) {
	if ((address & 0x3E000000) == 0x08000000 || // RAM
		(address & 0x3F800000) == 0x04000000 || // VRAM
		(address & 0xBFFFC000) == 0x00010000 || // Scratchpad
		((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) || // More RAM
		IsMeSramAddress(address)) { // ME SRAM
		*(T*)GetPointerUnchecked(address) = data;
	} else if (IsMeEdramAddress(address) && meEdram) {
		*(T*)GetMeEdramPointer(address) = data;
	} else if (IsMeHwRegister(address) && sizeof(T) == 4) {
		WriteMeHwRegister(address, (u32)data);
	} else {
		Core_MemoryException(address, sizeof(T), currentMIPS->pc, MemoryExceptionType::WRITE_WORD);
	}
}

bool IsRAMAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return true;
	} else {
		return false;
	}
}

bool IsScratchpadAddress(const u32 address) {
	return (address & 0xBFFFC000) == 0x00010000;
}

u8 Read_U8(const u32 address) {
	u8 value = 0;
	ReadFromHardware<u8>(value, address);
	return (u8)value;
}

u16 Read_U16(const u32 address) {
	u16_le value = 0;
	ReadFromHardware<u16_le>(value, address);
	return (u16)value;
}

u32 Read_U32(const u32 address) {
	u32_le value = 0;
	ReadFromHardware<u32_le>(value, address);
	return value;
}

u64 Read_U64(const u32 address) {
	u64_le value = 0;
	ReadFromHardware<u64_le>(value, address);
	return value;
}

u32 Read_U8_ZX(const u32 address) {
	return (u32)Read_U8(address);
}

u32 Read_U16_ZX(const u32 address) {
	return (u32)Read_U16(address);
}

void Write_U8(const u8 _Data, const u32 address) {
	WriteToHardware<u8>(address, _Data);
}

void Write_U16(const u16 _Data, const u32 address) {
	WriteToHardware<u16_le>(address, _Data);
}

void Write_U32(const u32 _Data, const u32 address) {
	WriteToHardware<u32_le>(address, _Data);
}

void Write_U64(const u64 _Data, const u32 address) {
	WriteToHardware<u64_le>(address, _Data);
}

}	// namespace Memory
