// Copyright (c) 2017- PPSSPP Project.

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

#pragma once

#include <functional>
#include <string>
#include "Common/CommonTypes.h"

namespace GPURecord {

bool IsActive();
bool IsActivePending();
bool Activate();
// Call only if Activate() returns true.
void SetCallback(const std::function<void(const std::string &)> callback);

void NotifyCommand(u32 pc);
void NotifyMemcpy(u32 dest, u32 src, u32 sz);
void NotifyMemset(u32 dest, int v, u32 sz);
void NotifyUpload(u32 dest, u32 sz);
void NotifyDisplay(u32 addr, int stride, int fmt);
void NotifyFrame();

bool RunMountedReplay(const std::string &filename);

};
