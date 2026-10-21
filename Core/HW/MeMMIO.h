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

// The registers homebrew's Media Engine code uses to run the ME and talk to it. Their state is the
// ME's (MeCore::MEState); without an ME they're left unhandled, as before.

#pragma once

class MMIODevice;

namespace MeMMIO {

// Reset, bus clock, soft interrupt and hardware mutex (0xBC100000).
MMIODevice *SysregDevice();
// The ME's interrupt controller (0xBC300000). The main CPU has its own there.
MMIODevice *InterruptDevice();
// DMACplus (0xBC800000): DMA between the CPUs' memories, and the LCD.
MMIODevice *DmacplusDevice();
// Clock dividers (0xBC200000).
MMIODevice *ClockDevice();
// The VME's control registers (0xBCC00000), its status always done.
MMIODevice *VmeControlDevice();
// Memory protection (0xBC000000), which the ME's startup code unlocks.
MMIODevice *MemProtectDevice();
// The DDR controller's flush register (0xBD000004), always done.
MMIODevice *DdrDevice();

}  // namespace MeMMIO
