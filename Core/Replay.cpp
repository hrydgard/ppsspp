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
#include <ctime>
#include <vector>
#include "Common/Common.h"
#include "Common/StringUtils.h"
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

static size_t replayDiskPos = 0;
static bool diskFailed = false;

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

static const ReplayItem *ReplayNextDisk(uint64_t t) {
	// TODO: Currently not checking t for timing purposes.  Should still be same order anyway.
	while (replayDiskPos < replayItems.size()) {
		const auto &item = replayItems[replayDiskPos++];
		if ((int)item.info.action & (int)ReplayAction::MASK_FILE) {
			return &item;
		}
	}

	return nullptr;
}

static const ReplayItem *ReplayNextDisk(ReplayAction action, uint64_t t) {
	// Bail early and ignore replay data if the disk data is out of sync.
	if (diskFailed) {
		return nullptr;
	}

	auto item = ReplayNextDisk(t);
	if (!item || item->info.action != action) {
		// If we got the wrong thing, or if there weren't any disk items then stop trying.
		diskFailed = true;
		return nullptr;
	}

	return item;
}

uint32_t ReplayApplyDisk(ReplayAction action, uint32_t result, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
	{
		auto item = ReplayNextDisk(action, t);
		if (item)
			return item->info.result;
		return result;
	}

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
	{
		auto item = ReplayNextDisk(action, t);
		if (item)
			return item->info.result64;
		return result;
	}

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
	{
		auto item = ReplayNextDisk(ReplayAction::FILE_READ, t);
		if (item && item->data.size() <= dataSize) {
			memcpy(data, &item->data[0], item->data.size());
			return item->info.result;
		}
		return readSize;
	}

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

static int64_t ConvertFromTm(const struct tm *t) {
	// Remember, mktime modifies.
	struct tm copy = *t;
	return (int64_t)mktime(&copy);
}

static void ConvertToTm(struct tm *t, int64_t sec) {
	time_t copy = sec;
	localtime_r(&copy, t);
}

ReplayFileInfo ConvertFileInfo(const PSPFileInfo &data) {
	ReplayFileInfo info;
	truncate_cpy(info.filename, data.name.c_str());
	info.size = data.size;
	info.access = (uint16_t)data.access;
	info.exists = data.exists ? 1 : 0;
	info.isDirectory = data.type == FILETYPE_DIRECTORY ? 1 : 0;
	info.atime = ConvertFromTm(&data.atime);
	info.ctime = ConvertFromTm(&data.ctime);
	info.mtime = ConvertFromTm(&data.mtime);
	return info;
}

PSPFileInfo ConvertFileInfo(const ReplayFileInfo &info) {
	PSPFileInfo data;
	data.name = std::string(info.filename, strnlen(info.filename, sizeof(info.filename)));
	data.size = info.size;
	data.access = info.access;
	data.exists = info.exists != 0;
	data.type = info.isDirectory ? FILETYPE_DIRECTORY : FILETYPE_NORMAL;
	ConvertToTm(&data.atime, info.atime);
	ConvertToTm(&data.ctime, info.ctime);
	ConvertToTm(&data.mtime, info.mtime);

	// Always a regular file read.
	data.isOnSectorSystem = false;
	data.startSector = 0;
	data.numSectors = 0;
	data.sectorSize = 0;

	return data;
}

PSPFileInfo ReplayApplyDiskFileInfo(const PSPFileInfo &data, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
	{
		auto item = ReplayNextDisk(ReplayAction::FILE_INFO, t);
		if (item && item->data.size() == sizeof(ReplayFileInfo)) {
			ReplayFileInfo info;
			memcpy(&info, &item->data[0], sizeof(info));
			return ConvertFileInfo(info);
		}
		return data;
	}

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
	{
		auto item = ReplayNextDisk(ReplayAction::FILE_LISTING, t);
		if (item && (item->data.size() % sizeof(ReplayFileInfo)) == 0) {
			std::vector<PSPFileInfo> results;
			size_t items = item->data.size() / sizeof(ReplayFileInfo);
			for (size_t i = 0; i < items; ++i) {
				ReplayFileInfo info;
				memcpy(&info, &item->data[i * sizeof(info)], sizeof(info));
				results.push_back(ConvertFileInfo(info));
			}
			return results;
		}
		return data;
	}

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
