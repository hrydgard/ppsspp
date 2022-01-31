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

#include "Common/File/Path.h"

// Be careful about changing these values (used in file data.)
enum class ReplayAction : uint8_t {
	BUTTONS = 0x00,
	ANALOG = 0x01,

	// All of these are just save results of memory stick operations, e.g. if mkdir succeeded, etc.
	// We assume the game will generate the same data and syscalls.
	FILE_RENAME = 0x40,
	FILE_REMOVE = 0x41,
	// For FILE_READ, we do save the read data, in case it could change.
	FILE_READ = 0xC2,
	FILE_OPEN = 0x43,
	FILE_SEEK = 0x44,
	FILE_INFO = 0xC5,
	FILE_LISTING = 0xC6,
	MKDIR = 0x47,
	RMDIR = 0x48,
	FREESPACE = 0x49,

	MASK_FILE = 0x40,
	MASK_SIDEDATA = 0x80,
};

struct PSPFileInfo;

// Replay from data in memory.  Does not manipulate base time / RNG state.
bool ReplayExecuteBlob(int version, const std::vector<uint8_t> &data);
// Replay from data in a file.  Returns false if invalid.
bool ReplayExecuteFile(const Path &filename);
// Returns whether there are unexecuted events to replay.
bool ReplayHasMoreEvents();

// Begin recording.  If currently executing, discards unexecuted events.
void ReplayBeginSave();
// Flush buffered events to memory.  Continues recording (next call will receive new events only.)
// No header is flushed with this operation - don't mix with ReplayFlushFile().
void ReplayFlushBlob(std::vector<uint8_t> *data);
// Flush buffered events to file.  Continues recording (next call will receive new events only.)
// Do not call with a different filename before ReplayAbort().
bool ReplayFlushFile(const Path &filename);
// Get current replay data version.
int ReplayVersion();

// Abort any execute or record operation in progress.
void ReplayAbort();

// Check if replay data is being executed or saved.
bool ReplayIsExecuting();
bool ReplayIsSaving();

void ReplayApplyCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t);
uint32_t ReplayApplyDisk(ReplayAction action, uint32_t result, uint64_t t);
uint64_t ReplayApplyDisk64(ReplayAction action, uint64_t result, uint64_t t);
uint32_t ReplayApplyDiskRead(void *data, uint32_t readSize, uint32_t dataSize, bool inGameDir, uint64_t t);
uint64_t ReplayApplyDiskWrite(const void *data, uint64_t writeSize, uint64_t dataSize, bool *diskFull, bool inGameDir, uint64_t t);
PSPFileInfo ReplayApplyDiskFileInfo(const PSPFileInfo &data, uint64_t t);
std::vector<PSPFileInfo> ReplayApplyDiskListing(const std::vector<PSPFileInfo> &data, uint64_t t);
