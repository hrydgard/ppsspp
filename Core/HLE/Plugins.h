// Copyright (c) 2020- PPSSPP Project.

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

#include <cstdint>
#include <string>
#include "Input/KeyCodes.h"

class PointerWrap;

namespace HLEPlugins {

void Init();
void Shutdown();

bool Load();
void Unload();

void DoState(PointerWrap &p);

bool HasEnabled();

enum class PluginType {
	INVALID = 0,
	PRX,
};

struct PluginInfo {
	PluginType type;
	std::string name;
	std::string filename;  // PSP-space path. So we can't use a Path object.
	int version;
	uint32_t memory;
};

std::vector<PluginInfo> FindPlugins(const std::string &gameID, const std::string &lang);

void SetKey(int key, uint8_t value);
uint8_t GetKey(int key);

extern float PluginDataAxis[JOYSTICK_AXIS_MAX];

}  // namespace
