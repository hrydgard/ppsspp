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

// Hardware the firmware's POPS manager (flash0:/kd/popsman.prx) drives directly during a PS1 boot
// (Core/HLE/PopsBoot.cpp), besides the Media Engine's: the GE's stall and interrupt registers. The
// devices only answer during that boot.

#pragma once

#include <vector>

#include "Common/CommonTypes.h"

class MMIODevice;

namespace PopsMMIO {

const std::vector<MMIODevice *> &Devices();

}  // namespace PopsMMIO
