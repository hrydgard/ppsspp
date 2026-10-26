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
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HW/GpioMMIO.h"
#include "Core/HW/MMIO.h"

namespace GpioMMIO {

namespace {

constexpr u32 REG_INTR_ACK = 0x024;

class GpioDevice : public MMIODevice {
public:
	GpioDevice() : MMIODevice("GPIO", BASE_ADDRESS, SIZE) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (size != 4)
			return false;
		*value = regs_[offset / 4];
		return true;
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (size != 4)
			return false;
		if (offset == REG_INTR_ACK) {
			// Acknowledging an interrupt clears the corresponding bit(s) in the triggered-status
			// register, rather than storing the acknowledge value itself - matches uofw's usage
			// (GpioAcquireIntr writes the same bit it's acknowledging) and JPCSP's
			// acknowledgeInterrupt().
			regs_[REG_INTR_STATUS / 4] &= ~value;
			return true;
		}
		regs_[offset / 4] = value;
		return true;
	}

	void Reset() override {
		memset(regs_, 0, sizeof(regs_));
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("GpioMMIO", 1, 1);
		if (!s)
			return;
		DoArray(p, regs_, ARRAY_SIZE(regs_));
	}

	void SetInterruptStatusBits(u32 bits) {
		regs_[REG_INTR_STATUS / 4] |= bits;
	}

private:
	u32 regs_[SIZE / 4]{};
};

GpioDevice g_device;

}  // namespace

MMIODevice *Device() {
	return &g_device;
}

void SetInterruptStatusBits(u32 bits) {
	g_device.SetInterruptStatusBits(bits);
}

}  // namespace GpioMMIO

namespace SysconSerialMMIO {

namespace {

// Real PSP Syscon command bytes (from JPCSP's sceSyscon.java, which lists the full real set -
// only the ones vsh_module's boot path is known to need are actually handled specially here).
enum SysconCmd : u8 {
	CMD_NOP = 0x00,
	CMD_READ_CLOCK = 0x09,
	CMD_READ_ALARM = 0x0A,
	CMD_WRITE_CLOCK = 0x20,
	CMD_WRITE_ALARM = 0x22,
};

// Packet layout (shared between TX and RX, matching uofw's Syscon_cmd() and JPCSP's
// MMIOHandlerSyscon): byte 0 = command (TX) / status (RX), byte 1 = length in bytes
// (including these first two), bytes 2.. = payload, byte [length] = checksum
// (~sum(bytes[0..length-1]) & 0xFF).
constexpr int DATA_SIZE = 16;

// AC_SUPPLY | WLAN_POWER | POWER_SWITCH - matches JPCSP's MMIOHandlerSyscon.getBaryonStatus(),
// used as the RX status byte for every response (real hardware would report genuine baryon
// status bits here; this is just a plausible constant since PPSSPP doesn't model those).
constexpr u8 kBaryonStatus = 0x13;

class SysconSerialDevice : public MMIODevice {
public:
	SysconSerialDevice() : MMIODevice("syscon serial", BASE_ADDRESS, SIZE) {}

	bool Read(MIPSState *mips, u32 offset, int size, u32 *value) override {
		if (size != 4)
			return false;
		switch (offset) {
		case 0x008:
		{
			// Writes can move the index anywhere.
			*value = (dataIndex_ >= 0 && dataIndex_ + 1 < DATA_SIZE) ? (data_[dataIndex_] << 8) | data_[dataIndex_ + 1] : 0;
			dataIndex_ += 2;
			if (dataIndex_ > responseLen_) {
				dataIndex_ = 0;
				dataExhausted_ = true;
			}
			return true;
		}
		case 0x00C:
		{
			u32 flags = 1;  // bit 0: no error.
			if (!dataExhausted_)
				flags |= 4;  // bit 2: more response data available.
			*value = flags;
			return true;
		}
		default:
			*value = 0;
			return true;
		}
	}

	bool Write(MIPSState *mips, u32 offset, int size, u32 value) override {
		if (size != 4)
			return false;
		switch (offset) {
		case 0x004:
			if (value & 4) {
				dataIndex_ = 0;
			}
			if (value & 2) {
				ProcessCommand();
			}
			break;
		case 0x008:
			if (dataIndex_ + 1 < DATA_SIZE) {
				data_[dataIndex_] = (value >> 8) & 0xFF;
				data_[dataIndex_ + 1] = value & 0xFF;
			}
			if (dataIndex_ < DATA_SIZE)
				dataIndex_ += 2;
			break;
		default:
			break;
		}
		return true;
	}

	void Reset() override {
		memset(data_, 0, sizeof(data_));
		dataIndex_ = 0;
		responseLen_ = 0;
		dataExhausted_ = true;
		alarm_ = 0;
	}

	void DoState(PointerWrap &p) override {
		auto s = p.Section("SysconSerialMMIO", 1, 1);
		if (!s)
			return;
		DoArray(p, data_, DATA_SIZE);
		Do(p, dataIndex_);
		if (p.mode == PointerWrap::MODE_READ && (dataIndex_ < 0 || dataIndex_ > DATA_SIZE)) {
			dataIndex_ = 0;
		}
		Do(p, responseLen_);
		Do(p, dataExhausted_);
		Do(p, alarm_);
	}

private:
	void ProcessCommand();

	u8 data_[DATA_SIZE]{};
	int dataIndex_ = 0;
	int responseLen_ = 0;
	bool dataExhausted_ = true;
	// Persistent alarm value across sceSysconReadAlarm/WriteAlarm calls - mirrors JPCSP's
	// sceSyscon.java readAlarm()/writeAlarm(), which are just as simple (a stored int, no real
	// hardware backing).
	u32 alarm_ = 0;
};

void SysconSerialDevice::ProcessCommand() {
	u8 cmd = data_[0];

	// Sub-status byte: must not be 0x80 or 0x81 (uofw's Syscon_cmd() treats those as
	// SYSCON_RES_80/81 and retries the whole command) - 0x82 is what JPCSP's own fallback
	// path uses for exactly this reason.
	u8 payload[DATA_SIZE] = { 0x82 };
	int payloadLen = 1;

	switch (cmd) {
	case CMD_READ_CLOCK:
		// No real hardware clock modeled - always reports zero, matching JPCSP's fallback.
		memset(&payload[payloadLen], 0, 4);
		payloadLen += 4;
		break;
	case CMD_READ_ALARM:
		memcpy(&payload[payloadLen], &alarm_, 4);
		payloadLen += 4;
		break;
	case CMD_WRITE_CLOCK:
		// Not modeled - accept and ignore, same as JPCSP's writeClock().
		break;
	case CMD_WRITE_ALARM:
		memcpy(&alarm_, &data_[2], 4);
		break;
	case CMD_NOP:
	default:
		break;
	}

	data_[0] = kBaryonStatus;
	data_[1] = (u8)(payloadLen + 2);
	memcpy(&data_[2], payload, payloadLen);
	u8 sum = 0;
	for (int i = 0; i < data_[1]; i++)
		sum += data_[i];
	data_[data_[1]] = (u8)~sum;

	dataIndex_ = 0;
	responseLen_ = data_[1];
	dataExhausted_ = false;

	// Signals "response ready" the way real hardware raising GPIO4 would - see
	// uofw's Syscon_cmd(), which polls exactly this bit after triggering a command.
	GpioMMIO::SetInterruptStatusBits(0x10);
}

SysconSerialDevice g_device;

}  // namespace

MMIODevice *Device() {
	return &g_device;
}

}  // namespace SysconSerialMMIO
