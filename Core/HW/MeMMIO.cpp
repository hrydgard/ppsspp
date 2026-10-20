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

#include "Common/Log.h"
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

static MeSysregDevice g_sysreg;
static MeInterruptDevice g_interrupts;
static MeMemProtectDevice g_memProtect;

MMIODevice *SysregDevice() {
	return &g_sysreg;
}

MMIODevice *InterruptDevice() {
	return &g_interrupts;
}

MMIODevice *MemProtectDevice() {
	return &g_memProtect;
}

}  // namespace MeMMIO
