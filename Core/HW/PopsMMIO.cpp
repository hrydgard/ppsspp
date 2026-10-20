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

// Whether there's state to save: only during a POPS boot, so other games' states don't carry it.
bool DoActive(PointerWrap &p, MMIODevice *device) {
	bool active = PopsBootActive();
	Do(p, active);
	if (!active && p.mode == PointerWrap::MODE_READ) {
		device->Reset();
	}
	return active;
}

// The bootstrap goes at 0x40, the mailbox is at 0x7F0-0x7FF.
class MeRamDevice : public MMIODevice {
public:
	MeRamDevice() : MMIODevice("ME RAM", 0xBFC00000, SIZE) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!PopsBootActive()) {
			return false;
		}
		*value = 0;
		memcpy(value, ram_ + offset, size);
		// The manager polls the cleared acknowledgement after releasing the ME's reset.
		if (offset == REG_ACK && size == 4 && *value == 0 && bootstrapWritten_ && !meStarted_) {
			meStarted_ = true;
			__PopsBootStartMe();
		}
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!PopsBootActive()) {
			return false;
		}
		memcpy(ram_ + offset, &value, size);
		if (offset == BOOTSTRAP) {
			bootstrapWritten_ = true;
		}
		return true;
	}

	void Reset() override {
		memset(ram_, 0, sizeof(ram_));
		bootstrapWritten_ = false;
		meStarted_ = false;
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("PopsMeRam", 1);
		if (!s || !DoActive(p, this)) {
			return;
		}
		DoArray(p, ram_, SIZE);
		Do(p, bootstrapWritten_);
		Do(p, meStarted_);
	}

private:
	static constexpr u32 SIZE = 0x1000;
	static constexpr u32 BOOTSTRAP = 0x40;
	static constexpr u32 REG_ACK = 0x7F0;

	u8 ram_[SIZE]{};
	bool bootstrapWritten_ = false;
	bool meStarted_ = false;
};

// A FIFO that always has room. Each word written to the data port is a stereo sample.
class MeAudioDevice : public MMIODevice {
public:
	MeAudioDevice() : MMIODevice("ME audio", 0xBE000000, SIZE) {}

	static constexpr u32 FIFO_SIZE = 0x20;

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!PopsBootActive()) {
			return false;
		}
		const u32 word = (offset & ~3) == REG_FREE_SLOTS ? FIFO_SIZE : 0;
		*value = size == 4 ? word : (size == 2 ? (u16)word : (u8)word);
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!PopsBootActive()) {
			return false;
		}
		if (size != 4) {
			return true;
		}
		if (offset == REG_DATA) {
			samplesOut_++;
			samplesSinceHalt_++;
			// Drop the oldest when the mixer is a whole buffer behind.
			if (playedWrite_ - playedRead_ == PLAYED_SIZE) {
				playedRead_++;
			}
			played_[playedWrite_++ % PLAYED_SIZE] = value;
		} else {
			regs_[offset / 4] = value;
		}
		return true;
	}

	void Reset() override {
		memset(regs_, 0, sizeof(regs_));
		samplesOut_ = 0;
		samplesSinceHalt_ = 0;
		playedRead_ = 0;
		playedWrite_ = 0;
		mixPrimed_ = false;
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("PopsMeAudio", 1);
		if (!s || !DoActive(p, this)) {
			return;
		}
		DoArray(p, regs_, ARRAY_SIZE(regs_));
		Do(p, samplesOut_);
		Do(p, samplesSinceHalt_);
		DoArray(p, played_, PLAYED_SIZE);
		Do(p, playedRead_);
		Do(p, playedWrite_);
		Do(p, mixPrimed_);
	}

	void MixSamples(s32 *mix, int count) {
		if (!mixPrimed_) {
			if (playedWrite_ - playedRead_ < MIX_PRIME) {
				return;
			}
			mixPrimed_ = true;
		}
		if (playedWrite_ - playedRead_ > MIX_MAX) {
			playedRead_ = playedWrite_ - MIX_PRIME;
		}
		if (playedWrite_ - playedRead_ < (u32)count) {
			mixPrimed_ = false;
		}
		for (int i = 0; i < count && playedRead_ != playedWrite_; i++) {
			// Left in the low half.
			const u32 sample = played_[playedRead_++ % PLAYED_SIZE];
			mix[i * 2] += (s16)(sample & 0xFFFF);
			mix[i * 2 + 1] += (s16)(sample >> 16);
		}
	}

	u64 SamplesOut() const { return samplesOut_; }
	u32 PendingSamples() const { return samplesSinceHalt_; }
	void TakePendingSamples() { samplesSinceHalt_ = 0; }
	u32 QueuedSamples() const { return playedWrite_ - playedRead_; }

private:
	static constexpr u32 SIZE = 0x80;
	static constexpr u32 REG_FREE_SLOTS = 0x28;
	static constexpr u32 REG_DATA = 0x70;
	static constexpr u32 PLAYED_SIZE = 16384;
	// The ME plays in bursts, a PS1 frame at a time. Keep a little more than a frame queued.
	static constexpr u32 MIX_PRIME = 1536;
	static constexpr u32 MIX_MAX = 8192;

	u32 regs_[SIZE / 4]{};
	u64 samplesOut_ = 0;
	u32 samplesSinceHalt_ = 0;
	u32 played_[PLAYED_SIZE]{};
	u32 playedRead_ = 0;
	u32 playedWrite_ = 0;
	bool mixPrimed_ = false;
};

// Registers the manager and the ME loop only need to read as idle. Sysreg's ME reset (0x70) reads back.
class MeControlDevice : public MMIODevice {
public:
	MeControlDevice(const char *name, u32 base, u32 size, std::vector<u32> offsets = {})
		: MMIODevice(name, base, size), offsets_(std::move(offsets)) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (!Handles(offset)) {
			return false;
		}
		*value = (offset & ~3) == REG_ME_RESET && Base() == SYSREG ? meReset_ : 0;
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (!Handles(offset)) {
			return false;
		}
		if (offset == REG_ME_RESET && size == 4 && Base() == SYSREG) {
			meReset_ = value;
		}
		return true;
	}

	void Reset() override {
		meReset_ = 0;
	}

	void DoState(PointerWrap &p) override {
		if (Base() != SYSREG) {
			return;
		}
		auto s = p.Section("PopsSysreg", 1);
		if (!s) {
			return;
		}
		Do(p, meReset_);
	}

	static constexpr u32 SYSREG = 0xBC100000;

private:
	static constexpr u32 REG_ME_RESET = 0x70;

	bool Handles(u32 offset) const {
		if (!PopsBootActive()) {
			return false;
		}
		if (offsets_.empty()) {
			return true;
		}
		for (u32 o : offsets_) {
			if (o == (offset & ~3)) {
				return true;
			}
		}
		return false;
	}

	std::vector<u32> offsets_;
	u32 meReset_ = 0;
};

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

MeRamDevice g_meRam;
MeAudioDevice g_meAudio;
MeControlDevice g_memoryProtection("memory protection", 0xBC000000, 0x48);
MeControlDevice g_sysreg("sysreg", MeControlDevice::SYSREG, 0x80, { 0x04, 0x40, 0x50, 0x6C, 0x70 });
MeControlDevice g_clock("clock", 0xBC200000, 0x08);
MeControlDevice g_interruptController("interrupt controller", 0xBC300008, 0x04);
GeDevice g_ge;

}  // namespace

const std::vector<MMIODevice *> &Devices() {
	static const std::vector<MMIODevice *> devices = {
		&g_meRam, &g_meAudio, &g_memoryProtection, &g_sysreg, &g_clock, &g_interruptController, &g_ge,
	};
	return devices;
}

u32 AudioFreeSlots() {
	return MeAudioDevice::FIFO_SIZE;
}

void MixSamples(s32 *mix, int count) {
	g_meAudio.MixSamples(mix, count);
}

u64 SamplesOut() {
	return g_meAudio.SamplesOut();
}

u32 PendingSamples() {
	return g_meAudio.PendingSamples();
}

void TakePendingSamples() {
	g_meAudio.TakePendingSamples();
}

u32 QueuedSamples() {
	return g_meAudio.QueuedSamples();
}

}  // namespace PopsMMIO
