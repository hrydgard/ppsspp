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

#include "Core/HW/GpioMMIO.h"

namespace GpioMMIO {

namespace {

u32 g_regs[SIZE / 4];

constexpr u32 REG_INTR_ACK = 0x024;

}  // namespace

u32 Read32(u32 address) {
	u32 offset = address - BASE_ADDRESS;
	return g_regs[offset / 4];
}

void Write32(u32 address, u32 value) {
	u32 offset = address - BASE_ADDRESS;
	if (offset == REG_INTR_ACK) {
		// Acknowledging an interrupt clears the corresponding bit(s) in the triggered-status
		// register, rather than storing the acknowledge value itself - matches uofw's usage
		// (GpioAcquireIntr writes the same bit it's acknowledging) and JPCSP's
		// acknowledgeInterrupt().
		g_regs[REG_INTR_STATUS / 4] &= ~value;
		return;
	}
	g_regs[offset / 4] = value;
}

void SetInterruptStatusBits(u32 bits) {
	g_regs[REG_INTR_STATUS / 4] |= bits;
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
u8 g_data[DATA_SIZE];
int g_dataIndex = 0;
int g_responseLen = 0;
bool g_dataExhausted = true;

// AC_SUPPLY | WLAN_POWER | POWER_SWITCH - matches JPCSP's MMIOHandlerSyscon.getBaryonStatus(),
// used as the RX status byte for every response (real hardware would report genuine baryon
// status bits here; this is just a plausible constant since PPSSPP doesn't model those).
constexpr u8 kBaryonStatus = 0x13;

// Persistent alarm value across sceSysconReadAlarm/WriteAlarm calls - mirrors JPCSP's
// sceSyscon.java readAlarm()/writeAlarm(), which are just as simple (a stored int, no real
// hardware backing).
u32 g_alarm = 0;

void ProcessCommand() {
	u8 cmd = g_data[0];

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
		memcpy(&payload[payloadLen], &g_alarm, 4);
		payloadLen += 4;
		break;
	case CMD_WRITE_CLOCK:
		// Not modeled - accept and ignore, same as JPCSP's writeClock().
		break;
	case CMD_WRITE_ALARM:
		memcpy(&g_alarm, &g_data[2], 4);
		break;
	case CMD_NOP:
	default:
		break;
	}

	g_data[0] = kBaryonStatus;
	g_data[1] = (u8)(payloadLen + 2);
	memcpy(&g_data[2], payload, payloadLen);
	u8 sum = 0;
	for (int i = 0; i < g_data[1]; i++)
		sum += g_data[i];
	g_data[g_data[1]] = (u8)~sum;

	g_dataIndex = 0;
	g_responseLen = g_data[1];
	g_dataExhausted = false;

	// Signals "response ready" the way real hardware raising GPIO4 would - see
	// uofw's Syscon_cmd(), which polls exactly this bit after triggering a command.
	GpioMMIO::SetInterruptStatusBits(0x10);
}

}  // namespace

u32 Read32(u32 address) {
	u32 offset = address - BASE_ADDRESS;
	switch (offset) {
	case 0x004:
		return 0;
	case 0x008:
	{
		u32 value = (g_data[g_dataIndex] << 8) | g_data[g_dataIndex + 1];
		g_dataIndex += 2;
		if (g_dataIndex > g_responseLen) {
			g_dataIndex = 0;
			g_dataExhausted = true;
		}
		return value;
	}
	case 0x00C:
	{
		u32 flags = 1;  // bit 0: no error.
		if (!g_dataExhausted)
			flags |= 4;  // bit 2: more response data available.
		return flags;
	}
	case 0x018:
		return 0;
	default:
		return 0;
	}
}

void Write32(u32 address, u32 value) {
	u32 offset = address - BASE_ADDRESS;
	switch (offset) {
	case 0x004:
		if (value & 4) {
			g_dataIndex = 0;
		}
		if (value & 2) {
			ProcessCommand();
		}
		break;
	case 0x008:
		if (g_dataIndex + 1 < DATA_SIZE) {
			g_data[g_dataIndex] = (value >> 8) & 0xFF;
			g_data[g_dataIndex + 1] = value & 0xFF;
		}
		g_dataIndex += 2;
		break;
	default:
		break;
	}
}

}  // namespace SysconSerialMMIO
