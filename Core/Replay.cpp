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

// File data formats below.
#pragma pack(push, 1)

struct ReplayItemHeader {
	ReplayAction action;
	uint64_t timestamp;
	union {
		uint32_t buttons;
		uint8_t analog[2][2];
		uint32_t result;
		uint64_t result64;
		// NOTE: Certain action types have data, always sized by this/result.
		uint32_t size;
	};

	ReplayItemHeader(ReplayAction a, uint64_t t) {
		action = a;
		timestamp = t;
	}

	ReplayItemHeader(ReplayAction a, uint64_t t, uint32_t v) : ReplayItemHeader(a, t) {
		result = v;
	}

	ReplayItemHeader(ReplayAction a, uint64_t t, uint64_t v) : ReplayItemHeader(a, t) {
		result64 = v;
	}

	ReplayItemHeader(ReplayAction a, uint64_t t, uint8_t v[2][2]) : ReplayItemHeader(a, t) {
		memcpy(analog, v, sizeof(analog));
	}
};

static const int REPLAY_MAX_FILENAME = 256;

struct ReplayFileInfo {
	char filename[REPLAY_MAX_FILENAME]{};
	int64_t size = 0;
	uint16_t access = 0;
	uint8_t exists = 0;
	uint8_t isDirectory = 0;

	int64_t atime = 0;
	int64_t ctime = 0;
	int64_t mtime = 0;
};

#pragma pack(pop)

struct ReplayItem {
	ReplayItemHeader info;
	std::vector<u8> data;

	ReplayItem(ReplayItemHeader h) : info(h) {
	}
};

static std::vector<ReplayItem> replayItems;
static ReplayState replayState = ReplayState::IDLE;

static size_t replayCtrlPos = 0;
static uint32_t lastButtons;
static uint8_t lastAnalog[2][2];

static void ReplaySaveCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t) {
	if (lastButtons != buttons) {
		replayItems.push_back(ReplayItemHeader(ReplayAction::BUTTONS, t, buttons));
		lastButtons = buttons;
	}
	if (memcmp(lastAnalog, analog, sizeof(lastAnalog)) != 0) {
		replayItems.push_back(ReplayItemHeader(ReplayAction::ANALOG, t, analog));
		memcpy(lastAnalog, analog, sizeof(lastAnalog));
	}
}

static void ReplayExecuteCtrl(uint32_t &buttons, uint8_t analog[2][2], uint64_t t) {
	for (; replayCtrlPos < replayItems.size() && t >= replayItems[replayCtrlPos].info.timestamp; ++replayCtrlPos) {
		const auto &item = replayItems[replayCtrlPos];
		switch (item.info.action) {
		case ReplayAction::BUTTONS:
			buttons = item.info.buttons;
			break;

		case ReplayAction::ANALOG:
			memcpy(analog, item.info.analog, sizeof(analog));
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
		replayItems.push_back(ReplayItemHeader(action, t, result));
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
		replayItems.push_back(ReplayItemHeader(action, t, result));
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
	{
		ReplayItem item = ReplayItemHeader(ReplayAction::FILE_READ, t, readSize);
		item.data.resize(readSize);
		memcpy(&item.data[0], data, readSize);
		replayItems.push_back(item);
		return readSize;
	}

	case ReplayState::IDLE:
	default:
		return readSize;
	}
}

ReplayFileInfo ConvertFileInfo(const PSPFileInfo &data) {
	// TODO
	return ReplayFileInfo();
}

PSPFileInfo ConvertFileInfo(const ReplayFileInfo &data) {
	// TODO
	return PSPFileInfo();
}

PSPFileInfo ReplayApplyDiskFileInfo(const PSPFileInfo &data, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
		// TODO
		//return ReplayExecuteDiskFileInfo(data, t);
		return data;

	case ReplayState::SAVE:
	{
		ReplayFileInfo info = ConvertFileInfo(data);
		ReplayItem item = ReplayItemHeader(ReplayAction::FILE_INFO, t, (uint32_t)sizeof(info));
		item.data.resize(sizeof(info));
		memcpy(&item.data[0], &info, sizeof(info));
		replayItems.push_back(item);
		return data;
	}

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
	{
		size_t sz = sizeof(ReplayFileInfo) * data.size();
		ReplayItem item = ReplayItemHeader(ReplayAction::FILE_LISTING, t, (uint32_t)sz);
		item.data.resize(sz);
		for (size_t i = 0; i < data.size(); ++i) {
			ReplayFileInfo info = ConvertFileInfo(data[i]);
			memcpy(&item.data[i * sizeof(ReplayFileInfo)], &info, sizeof(info));
		}
		replayItems.push_back(item);
		return data;
	}

	case ReplayState::IDLE:
	default:
		return data;
	}
}
