// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _JIT_ARMABI_H_
#define _JIT_ARMABI_H_

#include "Common.h"

// I've been using R8 as a trash register, I don't know if I should choose a
// better register or statically allocate some later, for now I'll just thrash
// R8 and down the road think about the other ones.
// TODO: Look at what all registers are being used for.

// ARMv7 uses registers for arguments to instructions
// R0 is also used for returns from instructions
// R1 is used as return along with R0 if it is a double size

#define ARM_PARAM1 R0
#define ARM_PARAM2 R1
#define ARM_PARAM3 R2
#define ARM_PARAM4 R3
#endif
