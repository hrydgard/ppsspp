// Copyright (c) 2018- PPSSPP Project.

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
#include <vector>

enum class ReplayAction : uint8_t {
	BUTTONS,
	ANALOG,
	// All of these are just save results of memory stick operations, e.g. if mkdir succeeded, etc.
	// We assume the game will generate the same data and syscalls.
	FILE_RENAME,
	FILE_REMOVE,
	// For FILE_READ, we do save the read data, in case it could change.
	FILE_READ,
	FILE_OPEN,
	FILE_SEEK,
	FILE_INFO,
	FILE_LISTING,
	MKDIR,
	RMDIR,
	FREESPACE,
};

struct PSPFileInfo;

void ReplayApplyCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t);
uint32_t ReplayApplyDisk(ReplayAction action, uint32_t result, uint64_t t);
uint64_t ReplayApplyDisk64(ReplayAction action, uint64_t result, uint64_t t);
uint32_t ReplayApplyDiskRead(void *data, uint32_t readSize, uint32_t dataSize, uint64_t t);
PSPFileInfo ReplayApplyDiskFileInfo(const PSPFileInfo &data, uint64_t t);
std::vector<PSPFileInfo> ReplayApplyDiskListing(const std::vector<PSPFileInfo> &data, uint64_t t);
