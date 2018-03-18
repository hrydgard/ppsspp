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

#include <cstring>
#include <vector>
#include "Common/Common.h"
#include "Core/Replay.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/HLE/sceCtrl.h"

enum class ReplayState {
	IDLE,
	EXECUTE,
	SAVE,
};

#pragma pack(push, 1)

struct ReplayItem {
	ReplayAction action;
	uint64_t timestamp;
	union {
		uint32_t buttons;
		uint8_t analog[2][2];
		uint32_t result;
		uint64_t result64;
		// TODO: Where do we store read data?
		struct {
			uint32_t offset;
			uint32_t size;
		} read;
	};

	ReplayItem(ReplayAction a, uint64_t t) {
		action = a;
		timestamp = t;
	}

	ReplayItem(ReplayAction a, uint64_t t, uint32_t v) : ReplayItem(a, t) {
		result = v;
	}

	ReplayItem(ReplayAction a, uint64_t t, uint64_t v) : ReplayItem(a, t) {
		result64 = v;
	}

	ReplayItem(ReplayAction a, uint64_t t, uint8_t v[2][2]) : ReplayItem(a, t) {
		memcpy(analog, v, sizeof(analog));
	}
};

#pragma pack(pop)

static std::vector<ReplayItem> replayItems;
static ReplayState replayState = ReplayState::IDLE;

static size_t replayCtrlPos = 0;
static uint32_t lastButtons;
static uint8_t lastAnalog[2][2];

static void ReplaySaveCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t) {
	if (lastButtons != buttons) {
		replayItems.push_back({ ReplayAction::BUTTONS, t, buttons });
		lastButtons = buttons;
	}
	if (memcmp(lastAnalog, analog, sizeof(lastAnalog)) != 0) {
		replayItems.push_back({ ReplayAction::ANALOG, t, analog });
		memcpy(lastAnalog, analog, sizeof(lastAnalog));
	}
}

static void ReplayExecuteCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t) {
	for (; replayCtrlPos < replayItems.size() && t >= replayItems[replayCtrlPos].timestamp; ++replayCtrlPos) {
		const auto &item = replayItems[replayCtrlPos];
		switch (item.action) {
		case ReplayAction::BUTTONS:
			buttons = item.buttons;
			break;

		case ReplayAction::ANALOG:
			memcpy(analog, item.analog, sizeof(analog));
			break;

		default:
			// Ignore non ctrl types.
			break;
		}
	}
}

void ReplayApplyCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		ReplayExecuteCtrl(buttons, analog, t);
		break;

	case ReplayState::SAVE:
		ReplaySaveCtrl(buttons, analog, t);
		break;

	case ReplayState::IDLE:
	default:
		break;
	}
}

uint32_t ReplayApplyDisk(ReplayAction action, uint32_t result, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDisk32(action, result, t);
		return result;

	case ReplayState::SAVE:
		replayItems.push_back({ action, t, result });
		return result;

	case ReplayState::IDLE:
	default:
		return result;
	}
}

uint64_t ReplayApplyDisk64(ReplayAction action, uint64_t result, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDisk64(action, result, t);
		return result;

	case ReplayState::SAVE:
		replayItems.push_back({ action, t, result });
		return result;

	case ReplayState::IDLE:
	default:
		return result;
	}
}

uint32_t ReplayApplyDiskRead(void *data, uint32_t readSize, uint32_t dataSize, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDiskRead(data, readSize, dataSize, t);
		return readSize;

	case ReplayState::SAVE:
		// TODO
		//replayItems.push_back({ action, t, readSize });
		//data?
		return readSize;

	case ReplayState::IDLE:
	default:
		return readSize;
	}
}

PSPFileInfo ReplayApplyDiskFileInfo(const PSPFileInfo &data, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDiskFileInfo(data, t);
		return data;

	case ReplayState::SAVE:
		// TODO
		//replayItems.push_back({ action, t });
		//data?
		return data;

	case ReplayState::IDLE:
	default:
		return data;
	}
}

std::vector<PSPFileInfo> ReplayApplyDiskListing(const std::vector<PSPFileInfo> &data, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDiskListing(data, t);
		return data;

	case ReplayState::SAVE:
		// TODO
		//replayItems.push_back({ action, t });
		//data?
		return data;

	case ReplayState::IDLE:
	default:
		return data;
	}
}
