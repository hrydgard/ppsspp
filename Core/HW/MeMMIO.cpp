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
#include <cstring>

#include "Common/Log.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/ge_constants.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/MeMMIO.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MeCore.h"
#include "Core/MIPS/MIPS.h"

namespace MeMMIO {

using MeCore::g_meState;

static bool IsME(const MIPSState *mips) {
	return mips && mips->IsMediaEngine();
}

class MeSysregDevice : public MMIODevice {
public:
	MeSysregDevice() : MMIODevice("ME sysreg", 0xBC100000, 0x100) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		u32 *reg = Register(offset);
		if (!reg) {
			return false;
		}
		*value = *reg;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState) {
			return false;
		}
		switch (offset) {
		case 0x44:
			// Interrupts the other CPU.
			if (value & 1) {
				if (IsME(mips)) {
					MeCore::InterruptMainCPU();
				} else {
					MeCore::RaiseInterrupt(MeCore::ME_INTR_SOFT);
				}
			}
			return true;
		case 0x48:
		{
			// Each CPU sets or clears its own bit, and can only set it while the other's is clear.
			const u32 own = IsME(mips) ? 2 : 1;
			const u32 other = own ^ 3;
			if (!(value & own)) {
				g_meState->mutex &= ~own;
			} else if (!(g_meState->mutex & other)) {
				g_meState->mutex |= own;
			}
			return true;
		}
		case 0x4C:
			g_meState->resetFlags = value;
			MeCore::SetReset((value & 4) != 0);
			return true;
		default:
			if (u32 *reg = Register(offset)) {
				*reg = value;
				return true;
			}
			return false;
		}
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

private:
	static u32 *Register(u32 offset) {
		if (!g_meState) {
			return nullptr;
		}
		switch (offset) {
		case 0x04: return &g_meState->nmiFlags;
		case 0x40: return &g_meState->ramConfig;
		case 0x48: return &g_meState->mutex;
		case 0x4C: return &g_meState->resetFlags;
		case 0x50: return &g_meState->busClock;
		default: return nullptr;
		}
	}
};

class MeInterruptDevice : public MMIODevice {
public:
	MeInterruptDevice() : MMIODevice("ME interrupts", 0xBC300000, 0x10) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || !IsME(mips)) {
			return false;
		}
		switch (offset) {
		case 0x0: *value = g_meState->intcRaised; return true;
		case 0x8: *value = g_meState->intcEnabled; return true;
		default: return false;
		}
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || !IsME(mips)) {
			return false;
		}
		switch (offset) {
		case 0x0:
			// Writing a source's bit clears it.
			g_meState->intcRaised &= ~value;
			return true;
		case 0x8:
			g_meState->intcEnabled = value;
			return true;
		default:
			return false;
		}
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

class MeMemProtectDevice : public MMIODevice {
public:
	MeMemProtectDevice() : MMIODevice("ME memory protection", 0xBC000000, 0x50) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!g_meState || (offset & 3)) {
			return false;
		}
		*value = g_meState->memProtect[offset / 4];
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3)) {
			return false;
		}
		g_meState->memProtect[offset / 4] = value;
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

// A PL080-like DMA controller with the LCD and colour space conversion registers below its
// channels (4-7, at 0x180). Transfers run at once, when a channel is enabled. Addresses are physical;
// on the ME's bus (a master bit in the control word), the low ones are its eDRAM. Each linked list
// item is five words (source, destination, next, control, configuration).
class MeDmacplusDevice : public MMIODevice {
public:
	MeDmacplusDevice() : MMIODevice("DMACplus", 0xBC800000, 0x200) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		u32 *reg = Register(offset);
		if (!reg)
			return false;
		*value = *reg;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!g_meState || (offset & 3))
			return false;
		MeCore::MEState::Dmacplus &d = g_meState->dmacplus;
		if (offset == 0x08) {
			d.intTCStatus &= ~value;
			return true;
		}
		if (offset == 0x10) {
			// No errors to clear.
			return true;
		}
		u32 *reg = Register(offset);
		if (!reg)
			return false;
		*reg = value;
		if (offset == 0x100) {
			SetLCD(d);
		} else if (offset == 0x160 && (value & 1)) {
			WARN_LOG(Log::ME, "DMACplus: colour space conversion not implemented");
		} else if (offset >= 0x180 && (offset & 0x1F) == 0x10 && (value & 1)) {
			RunChannel(d, (offset - 0x180) / 0x20);
		}
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

private:
	static u32 *Register(u32 offset) {
		if (!g_meState || (offset & 3))
			return nullptr;
		MeCore::MEState::Dmacplus &d = g_meState->dmacplus;
		if (offset == 0x00 || offset == 0x04 || offset == 0x14)
			return &d.intTCStatus;
		if (offset >= 0x100 && offset < 0x110)
			return &d.lcd[(offset - 0x100) / 4];
		if (offset >= 0x120 && offset <= 0x160)
			return &d.csc[(offset - 0x120) / 4];
		if (offset >= 0x180 && (offset & 0x1F) < 0x14)
			return &d.channels[(offset - 0x180) / 0x20][(offset & 0x1F) / 4];
		return nullptr;
	}

	// Pixel format 0 is 8888 (mcidclan's samples). The others aren't known.
	static void SetLCD(const MeCore::MEState::Dmacplus &d) {
		if (d.lcd[1] != 0) {
			WARN_LOG(Log::ME, "DMACplus: LCD pixel format %d", d.lcd[1]);
		}
		__DisplaySetFramebuf(d.lcd[0], d.lcd[3], GE_FORMAT_8888, 1);
	}

	static u8 *PhysicalPointer(u32 addr, u32 size, bool meBus) {
		addr &= 0x1FFFFFFF;
		const Memory::AddressSpace *space = g_meState->cpu.space;
		if (meBus && addr + size <= space->edramSize)
			return space->GetPointerUnchecked(addr);
		if (Memory::IsValidRange(addr, size))
			return Memory::GetPointerWriteUnchecked(addr);
		return nullptr;
	}

	static void RunChannel(MeCore::MEState::Dmacplus &d, int channel) {
		u32 *regs = d.channels[channel];
		// Bounded, in case of a list that loops.
		for (int item = 0; item < 0x10000; ++item) {
			const u32 control = regs[3];
			const u32 width = 1 << std::min((control >> 18) & 7, 4U);
			const u32 bytes = (control & 0xFFF) * width;
			u8 *src = PhysicalPointer(regs[0], bytes, (control >> 24) & 1);
			u8 *dst = PhysicalPointer(regs[1], bytes, (control >> 25) & 1);
			if (src && dst) {
				memmove(dst, src, bytes);
				NotifyMemInfo(MemBlockFlags::WRITE, regs[1] & 0x1FFFFFFF, bytes, "DMACplus");
			} else {
				ERROR_LOG(Log::ME, "DMACplus: bad transfer of %d bytes from %08x to %08x", bytes, regs[0], regs[1]);
			}
			if (control & 0x80000000)
				d.intTCStatus |= 1 << (channel + 4);
			const u32 next = regs[2];
			if (next == 0)
				break;
			const u8 *lli = PhysicalPointer(next & ~3, 20, false);
			if (!lli) {
				ERROR_LOG(Log::ME, "DMACplus: bad linked list item at %08x", next);
				break;
			}
			for (int i = 0; i < 5; ++i)
				regs[i] = ((const u32_le *)lli)[i];
		}
		regs[4] &= ~1;
	}
};

static MeDmacplusDevice g_dmacplus;
static MeSysregDevice g_sysreg;
static MeInterruptDevice g_interrupts;
static MeMemProtectDevice g_memProtect;

MMIODevice *SysregDevice() {
	return &g_sysreg;
}

MMIODevice *InterruptDevice() {
	return &g_interrupts;
}

MMIODevice *DmacplusDevice() {
	return &g_dmacplus;
}

MMIODevice *MemProtectDevice() {
	return &g_memProtect;
}

}  // namespace MeMMIO
