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

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/PopsBoot.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/PopsMMIO.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

namespace PopsMMIO {

namespace {

// The manager extends the emulator's stalled display list each frame: a FINISH, then the stall.
class GeDevice : public MMIODevice {
public:
	GeDevice() : MMIODevice("GE", 0xBD400000, 0x400) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!PopsBootActive() || size != 4) {
			return false;
		}
		DisplayList list;
		const bool running = gpu && gpu->GetCurrentDisplayList(list);
		if (offset == REG_STALL) {
			*value = running ? list.stall : 0;
			return true;
		}
		if (offset == REG_INTR_STATUS) {
			// The HLE GPU runs up to the stall right away, so the FINISH before it is done.
			const bool atStall = running && list.stall != 0 && (list.pc & 0x0FFFFFFF) == (list.stall & 0x0FFFFFFF);
			*value = atStall ? INTR_FINISH : 0;
			return true;
		}
		return false;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!PopsBootActive() || size != 4) {
			return false;
		}
		if (offset == REG_INTR_ACK) {
			return true;
		}
		if (offset != REG_STALL) {
			return false;
		}
		DisplayList list;
		if (!gpu || !gpu->GetCurrentDisplayList(list)) {
			WARN_LOG(Log::G3D, "GE stall register write %08x with no list running", value);
			return true;
		}
		bool runList = false;
		const u32 result = gpu->UpdateStall(list.id, value, &runList);
		if (result & 0x80000000) {
			WARN_LOG(Log::G3D, "GE stall register write %08x: list %d refused it (%08x)", value, list.id, result);
		} else if (runList) {
			gpu->ProcessDLQueue();
		}
		return true;
	}

	void Reset() override {}
	void DoState(PointerWrap &p) override {}

private:
	static constexpr u32 REG_STALL = 0x10C;
	static constexpr u32 REG_INTR_STATUS = 0x304;
	static constexpr u32 REG_INTR_ACK = 0x310;
	static constexpr u32 INTR_FINISH = 4;
};

GeDevice g_ge;

}  // namespace

const std::vector<MMIODevice *> &Devices() {
	static const std::vector<MMIODevice *> devices = { &g_ge };
	return devices;
}

}  // namespace PopsMMIO
