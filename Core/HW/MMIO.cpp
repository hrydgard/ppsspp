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

#include <cstring>
#include <vector>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/HW/GpioMMIO.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/MeMMIO.h"
#include "Core/HW/PopsMMIO.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

namespace MMIO {

// The boot page (see Memory::GetBootPage) is RAM, which the main CPU reaches through here.
class BootPageDevice : public MMIODevice {
public:
	BootPageDevice() : MMIODevice("boot page", Memory::BOOT_PAGE_ADDRESS, Memory::BOOT_PAGE_SIZE) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (offset + size > Memory::BOOT_PAGE_SIZE)
			return false;
		// What the ME has written by now: a main CPU polling a mailbox here sees it as on a PSP.
		if (mips == currentMIPS)
			CoreTiming::CatchUpOtherCPUs(mips);
		const u8 *ptr = Memory::GetBootPage() + offset;
		switch (size) {
		case 1: *value = *ptr; break;
		case 2: *value = *(const u16_le *)ptr; break;
		default: *value = *(const u32_le *)ptr; break;
		}
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (offset + size > Memory::BOOT_PAGE_SIZE)
			return false;
		u8 *ptr = Memory::GetBootPage() + offset;
		switch (size) {
		case 1: *ptr = (u8)value; break;
		case 2: *(u16_le *)ptr = (u16)value; break;
		default: *(u32_le *)ptr = value; break;
		}
		// The ME runs code from here, uncached, so it sees new code at once (MIST patches the
		// bootstrap, me/vram puts it back after its own stub). Reaches the ME's JIT too.
		if (mips)
			mips->InvalidateICacheRangeDeferred(Memory::BOOT_PAGE_ADDRESS + offset, size);
		return true;
	}

	// Both CPUs reset to the start of the page. The firmware leaves code there that sends the ME
	// (whose COP0 register 22 is nonzero) on to 0x40, where homebrew puts its ME code (read on a
	// PSP-1000 under PSPLink). The main CPU's half isn't needed, it never resets under HLE.
	void Reset() override {
		u8 *page = Memory::GetBootPage();
		memset(page, 0, Memory::BOOT_PAGE_SIZE);
		const u32 code[] = {
			0x4002B000,  // mfc0 v0, $22
			0x1440000E,  // bnez v0, 0xBFC00040
			0x00000000,  // nop
			0x1000FFFF,  // b .
			0x00000000,  // nop
		};
		for (size_t i = 0; i < ARRAY_SIZE(code); ++i) {
			((u32_le *)page)[i] = code[i];
		}
	}

	void DoState(PointerWrap &p) override {
		DoArray(p, Memory::GetBootPage(), Memory::BOOT_PAGE_SIZE);
	}
};

static BootPageDevice g_bootPage;

// Kernel code reaches RAM and VRAM uncached at 0xA8000000 and 0xA4000000 (Yabause's me.prx does, by
// ORing 0xA0000000 onto its own addresses). The main CPU's space has no views there, so it goes
// through the uncached mirrors at 0x40000000 instead. The Media Engine's space maps them.
class KernelUncachedDevice : public MMIODevice {
public:
	KernelUncachedDevice() : MMIODevice("kseg1 memory", 0xA0000000, 0x1C000000) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		const u32 address = 0x40000000 + offset;
		if ((mips && mips->IsMediaEngine()) || !Memory::IsValidRange(address, size))
			return false;
		switch (size) {
		case 1: *value = Memory::ReadUnchecked_U8(address); break;
		case 2: *value = Memory::ReadUnchecked_U16(address); break;
		default: *value = Memory::ReadUnchecked_U32(address); break;
		}
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		const u32 address = 0x40000000 + offset;
		if ((mips && mips->IsMediaEngine()) || !Memory::IsValidRange(address, size))
			return false;
		switch (size) {
		case 1: Memory::WriteUnchecked_U8((u8)value, address); break;
		case 2: Memory::WriteUnchecked_U16((u16)value, address); break;
		default: Memory::WriteUnchecked_U32(value, address); break;
		}
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}
};

static KernelUncachedDevice g_kernelUncached;

static std::vector<MMIODevice *> g_devices;

void Init() {
	g_devices = {
		GpioMMIO::Device(),
		SysconSerialMMIO::Device(),
		&g_bootPage,
		MeMMIO::SysregDevice(),
		MeMMIO::InterruptDevice(),
		MeMMIO::MemProtectDevice(),
		MeMMIO::DmacplusDevice(),
		MeMMIO::VmeControlDevice(),
		MeMMIO::ClockDevice(),
		MeMMIO::DdrDevice(),
		MeMMIO::AudioDevice(),
		&g_kernelUncached,
	};
	for (MMIODevice *device : PopsMMIO::Devices()) {
		g_devices.push_back(device);
	}
	for (MMIODevice *device : g_devices) {
		device->Reset();
	}
}

void Shutdown() {
	g_devices.clear();
}

bool Copy(MIPSState *mips, u32 destPtr, u32 srcPtr, u32 bytes) {
	if (!Memory::IsMMIOAccess(destPtr) && !Memory::IsMMIOAccess(srcPtr)) {
		return false;
	}
	const bool backward = destPtr > srcPtr && destPtr < srcPtr + bytes;
	for (u32 i = 0; i < bytes; ++i) {
		const u32 offset = backward ? bytes - 1 - i : i;
		const u32 src = srcPtr + offset;
		const u32 dest = destPtr + offset;
		u8 value = 0;
		if (Memory::IsMMIOAccess(src)) {
			value = (u8)Read(mips, src, 1);
		} else if (Memory::IsValidAddress(src)) {
			value = Memory::ReadUnchecked_U8(src);
		}
		if (Memory::IsMMIOAccess(dest)) {
			Write(mips, dest, 1, value);
		} else if (Memory::IsValidAddress(dest)) {
			Memory::WriteUnchecked_U8(value, dest);
		}
	}
	return true;
}

void DoState(PointerWrap &p) {
	auto s = p.Section("MMIO", 1, 2);
	if (!s)
		return;
	// Every device, whether or not this boot registered it, so the layout doesn't depend on that.
	GpioMMIO::Device()->DoState(p);
	SysconSerialMMIO::Device()->DoState(p);
	if (s >= 2) {
		g_bootPage.DoState(p);
	} else if (p.mode == PointerWrap::MODE_READ) {
		g_bootPage.Reset();
	}
}

static MMIODevice *FindDevice(u32 address) {
	for (MMIODevice *device : g_devices) {
		if (device->Contains(address))
			return device;
	}
	return nullptr;
}

u32 Read(MIPSState *mips, u32 address, int size) {
	MMIODevice *device = FindDevice(address);
	u32 value;
	if (device && device->Read(mips, address - device->Base(), size, &value))
		return value;
	WARN_LOG(Log::CPU, "Unhandled MMIO Read%d at %08x%s%s", size * 8, address, device ? " in " : "", device ? device->Name() : "");
	return UNHANDLED_READ;
}

void Write(MIPSState *mips, u32 address, int size, u32 value) {
	MMIODevice *device = FindDevice(address);
	if (device && device->Write(mips, address - device->Base(), size, value))
		return;
	WARN_LOG(Log::CPU, "Unhandled MMIO Write%d at %08x = %08x%s%s", size * 8, address, value, device ? " in " : "", device ? device->Name() : "");
}

}  // namespace MMIO
