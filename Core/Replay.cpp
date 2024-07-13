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

#ifdef __MINGW32__
#include <unistd.h>
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L
#endif
#endif

#include <cstring>
#include <ctime>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Replay.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceKernelTime.h"
#include "Core/HLE/sceRtc.h"

enum class ReplayState {
	IDLE,
	EXECUTE,
	SAVE,
};

// Overall structure of file format:
//
// - ReplayFileHeader with basic data about replay (mostly timestamp for sync.)
// - An indeterminate sequence of events:
//   - ReplayItemHeader (primary event details)
//   - Side data of bytes listed in header, if SIDEDATA flag set on action.
//
// The header doesn't say how long the replay is, because new events are
// appended to the file as they occur.  It is usually near, and always less than:
//
// (fileSize - sizeof(ReplayFileHeader)) / sizeof(ReplayItemHeader)

// File data formats below.
#pragma pack(push, 1)

static const char * const REPLAY_MAGIC = "PPREPLAY";
static const int REPLAY_VERSION_MIN = 1;
static const int REPLAY_VERSION_CURRENT = 1;

struct ReplayFileHeader {
	char magic[8];
	u32_le version = REPLAY_VERSION_CURRENT;
	u32_le reserved[3]{};
	u64_le rtcBaseSeconds;
};

struct ReplayItemHeader {
	ReplayAction action;
	u64_le timestamp;
	union {
		u32_le buttons;
		uint8_t analog[2][2];
		u32_le result;
		u64_le result64;
		// NOTE: Certain action types have data, always sized by this/result.
		u32_le size;
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
	s64_le size = 0;
	u16_le access = 0;
	uint8_t exists = 0;
	uint8_t isDirectory = 0;

	s64_le atime = 0;
	s64_le ctime = 0;
	s64_le mtime = 0;
};

#pragma pack(pop)

struct ReplayItem {
	ReplayItemHeader info;
	std::vector<uint8_t> data;

	ReplayItem(ReplayItemHeader h) : info(h) {
	}
};

static std::vector<ReplayItem> replayItems;
// One more than the last executed item.
static size_t replayExecPos = 0;
static bool replaySaveWroteHeader = false;
static ReplayState replayState = ReplayState::IDLE;
static bool replaySawGameDirWrite = false;

static size_t replayCtrlPos = 0;
static uint32_t lastButtons = 0;
static uint8_t lastAnalog[2][2]{};

static size_t replayDiskPos = 0;
static bool diskFailed = false;

bool ReplayExecuteBlob(int version, const std::vector<uint8_t> &data) {
	if (version < REPLAY_VERSION_MIN || version > REPLAY_VERSION_CURRENT) {
		ERROR_LOG(Log::System, "Bad replay data version: %d", version);
		return false;
	}
	if (data.size() == 0) {
		ERROR_LOG(Log::System, "Empty replay data");
		return false;
	}

	ReplayAbort();

	// Rough estimate.
	replayItems.reserve(data.size() / sizeof(ReplayItemHeader));
	for (size_t i = 0, sz = data.size(); i < sz; ) {
		if (i + sizeof(ReplayItemHeader) > sz) {
			ERROR_LOG(Log::System, "Truncated replay data at %lld during item header", (long long)i);
			break;
		}
		ReplayItemHeader *info = (ReplayItemHeader *)&data[i];
		ReplayItem item(*info);
		i += sizeof(ReplayItemHeader);

		if ((int)item.info.action & (int)ReplayAction::MASK_SIDEDATA) {
			if (i + item.info.size > sz) {
				ERROR_LOG(Log::System, "Truncated replay data at %lld during side data", (long long)i);
				break;
			}
			if (item.info.size != 0) {
				item.data.resize(item.info.size);
				memcpy(&item.data[0], &data[i], item.info.size);
				i += item.info.size;
			}
		}

		replayItems.push_back(item);
	}

	replayState = ReplayState::EXECUTE;
	INFO_LOG(Log::System, "Executing replay with %lld items", (long long)replayItems.size());
	return true;
}

bool ReplayExecuteFile(const Path &filename) {
	ReplayAbort();

	FILE *fp = File::OpenCFile(filename, "rb");
	if (!fp) {
		DEBUG_LOG(Log::System, "Failed to open replay file: %s", filename.c_str());
		return false;
	}

	int version = -1;
	std::vector<uint8_t> data;
	auto loadData = [&]() {
		// TODO: Maybe stream instead.
		size_t sz = File::GetFileSize(fp);
		if (sz <= sizeof(ReplayFileHeader)) {
			ERROR_LOG(Log::System, "Empty replay data");
			return false;
		}

		ReplayFileHeader fh;
		if (fread(&fh, sizeof(fh), 1, fp) != 1) {
			ERROR_LOG(Log::System, "Could not read replay file header");
			return false;
		}
		sz -= sizeof(fh);

		if (memcmp(fh.magic, REPLAY_MAGIC, sizeof(fh.magic)) != 0) {
			ERROR_LOG(Log::System, "Replay header corrupt");
			return false;
		}

		if (fh.version < REPLAY_VERSION_MIN) {
			ERROR_LOG(Log::System, "Replay version %d unsupported", fh.version);
			return false;
		} else if (fh.version > REPLAY_VERSION_CURRENT) {
			WARN_LOG(Log::System, "Replay version %d scary and futuristic, trying anyway", fh.version);
		}

		RtcSetBaseTime((int32_t)fh.rtcBaseSeconds, 0);
		version = fh.version;

		data.resize(sz);

		if (fread(&data[0], sz, 1, fp) != 1) {
			ERROR_LOG(Log::System, "Could not read replay data");
			return false;
		}

		return true;
	};

	if (loadData()) {
		fclose(fp);
		ReplayExecuteBlob(version, data);
		return true;
	}

	fclose(fp);
	return false;
}

bool ReplayHasMoreEvents() {
	return replayExecPos < replayItems.size();
}

void ReplayBeginSave() {
	if (replayState != ReplayState::EXECUTE) {
		// Restart any save operation.
		ReplayAbort();
	} else {
		// Discard any unexecuted items, but resume from there.
		// The parameter isn't used here, since we'll always be resizing down.
		replayItems.resize(replayExecPos, ReplayItem(ReplayItemHeader(ReplayAction::BUTTONS, 0)));
	}

	replayState = ReplayState::SAVE;
}

void ReplayFlushBlob(std::vector<uint8_t> *data) {
	size_t sz = replayItems.size() * sizeof(ReplayItemHeader);
	// Add in any side data.
	for (const auto &item : replayItems) {
		if ((int)item.info.action & (int)ReplayAction::MASK_SIDEDATA) {
			sz += item.info.size;
		}
	}

	data->resize(sz);

	size_t pos = 0;
	for (const auto &item : replayItems) {
		memcpy(&(*data)[pos], &item.info, sizeof(item.info));
		pos += sizeof(item.info);

		if ((int)item.info.action & (int)ReplayAction::MASK_SIDEDATA) {
			memcpy(&(*data)[pos], &item.data[0], item.data.size());
			pos += item.data.size();
		}
	}

	// Keep recording, but throw away our buffered items.
	replayItems.clear();
}

bool ReplayFlushFile(const Path &filename) {
	FILE *fp = File::OpenCFile(filename, replaySaveWroteHeader ? "ab" : "wb");
	if (!fp) {
		ERROR_LOG(Log::System, "Failed to open replay file: %s", filename.c_str());
		return false;
	}

	bool success = true;
	if (!replaySaveWroteHeader) {
		ReplayFileHeader fh;
		memcpy(fh.magic, REPLAY_MAGIC, sizeof(fh.magic));
		fh.rtcBaseSeconds = RtcBaseTime();

		success = fwrite(&fh, sizeof(fh), 1, fp) == 1;
		replaySaveWroteHeader = true;
	}

	size_t c = replayItems.size();
	if (success && c != 0) {
		// TODO: Maybe stream instead.
		std::vector<uint8_t> data;
		ReplayFlushBlob(&data);

		success = fwrite(&data[0], data.size(), 1, fp) == 1;
	}
	fclose(fp);

	if (success) {
		DEBUG_LOG(Log::System, "Flushed %lld replay items", (long long)c);
	} else {
		ERROR_LOG(Log::System, "Could not write %lld replay items (disk full?)", (long long)c);
	}
	return success;
}

int ReplayVersion() {
	return REPLAY_VERSION_CURRENT;
}

void ReplayAbort() {
	replayItems.clear();
	replayExecPos = 0;
	replaySaveWroteHeader = false;
	replayState = ReplayState::IDLE;
	replaySawGameDirWrite = false;

	replayCtrlPos = 0;
	lastButtons = 0;
	memset(lastAnalog, 0, sizeof(lastAnalog));

	replayDiskPos = 0;
	diskFailed = false;
}

bool ReplayIsExecuting() {
	return replayState == ReplayState::EXECUTE;
}

bool ReplayIsSaving() {
	return replayState == ReplayState::SAVE;
}

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
	if (replayCtrlPos >= replayItems.size()) {
		// Don't assert buttons, let the user input prevail.
		return;
	}

	for (; replayCtrlPos < replayItems.size() && t >= replayItems[replayCtrlPos].info.timestamp; ++replayCtrlPos) {
		const auto &item = replayItems[replayCtrlPos];
		switch (item.info.action) {
		case ReplayAction::BUTTONS:
			lastButtons = item.info.buttons;
			break;

		case ReplayAction::ANALOG:
			memcpy(lastAnalog, item.info.analog, sizeof(lastAnalog));
			break;

		default:
			// Ignore non ctrl types.
			break;
		}
	}

	// We have to always apply the latest state here, because otherwise real input is used between changes.
	buttons = lastButtons;
	memcpy(analog, lastAnalog, sizeof(lastAnalog));

	if (replayExecPos < replayCtrlPos) {
		replayExecPos = replayCtrlPos;
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

	if (replayExecPos < replayDiskPos) {
		replayExecPos = replayDiskPos;
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

uint32_t ReplayApplyDiskRead(void *data, uint32_t readSize, uint32_t dataSize, bool inGameDir, uint64_t t) {
	// Ignore PSP/GAME reads if we haven't seen a write there.
	if (inGameDir && !replaySawGameDirWrite) {
		return readSize;
	}

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

uint64_t ReplayApplyDiskWrite(const void *data, uint64_t writeSize, uint64_t dataSize, bool *diskFull, bool inGameDir, uint64_t t) {
	switch (replayState) {
	case ReplayState::EXECUTE:
	case ReplayState::SAVE:
		// Never let the disk return full during replay.
		if (diskFull)
			*diskFull = false;
		if (inGameDir)
			replaySawGameDirWrite = true;
		return writeSize;

	case ReplayState::IDLE:
	default:
		return writeSize;
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
