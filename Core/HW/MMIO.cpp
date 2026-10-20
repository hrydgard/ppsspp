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

#include <vector>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Core/HW/GpioMMIO.h"
#include "Core/HW/MMIO.h"
#include "Core/MIPS/MIPS.h"

namespace MMIO {

static std::vector<MMIODevice *> g_devices;

void Init() {
	g_devices = {
		GpioMMIO::Device(),
		SysconSerialMMIO::Device(),
	};
	for (MMIODevice *device : g_devices) {
		device->Reset();
	}
}

void Shutdown() {
	g_devices.clear();
}

void DoState(PointerWrap &p) {
	auto s = p.Section("MMIO", 1, 1);
	if (!s)
		return;
	// Every device, whether or not this boot registered it, so the layout doesn't depend on that.
	GpioMMIO::Device()->DoState(p);
	SysconSerialMMIO::Device()->DoState(p);
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
