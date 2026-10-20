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
#include "Core/HW/GpioMMIO.h"
#include "Core/HW/MMIO.h"
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

	void Reset() override {
		memset(Memory::GetBootPage(), 0, Memory::BOOT_PAGE_SIZE);
	}

	void DoState(PointerWrap &p) override {
		DoArray(p, Memory::GetBootPage(), Memory::BOOT_PAGE_SIZE);
	}
};

static BootPageDevice g_bootPage;

static std::vector<MMIODevice *> g_devices;

void Init() {
	g_devices = {
		GpioMMIO::Device(),
		SysconSerialMMIO::Device(),
		&g_bootPage,
	};
	for (MMIODevice *device : g_devices) {
		device->Reset();
	}
}

void Shutdown() {
	g_devices.clear();
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
