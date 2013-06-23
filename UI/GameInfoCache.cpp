// Copyright (c) 2013- PPSSPP Project.

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

#include <string>
#include <map>

#include "base/timeutil.h"
#include "base/stringutil.h"
#include "file/file_util.h"
#include "file/zip_read.h"
#include "image/png_load.h"
#include "thread/prioritizedworkqueue.h"
#include "GameInfoCache.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/ELF/PBPReader.h"
#include "Core/System.h"

#include "Core/Config.h"

GameInfoCache g_gameInfoCache;

bool GameInfo::DeleteGame() {
	switch (fileType) {
	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
		// Just delete the one file (TODO: handle two-disk games as well somehow).
		deleteFile(fileInfo.fullName.c_str());
		return true;

	case FILETYPE_PSP_PBP_DIRECTORY:
		// Recursively deleting directories not yet supported
		return false;

	default:
		return false;
	}
}

u64 GameInfo::GetGameSizeInBytes() {
	switch (fileType) {
	case FILETYPE_PSP_PBP_DIRECTORY:
		// TODO: Need to recurse here.
		return 0;
	default:
		return fileInfo.size;
	}
}

std::vector<std::string> GameInfo::GetSaveDataDirectories() {
	std::string memc, flash;
	GetSysDirectories(memc, flash);

	std::vector<FileInfo> dirs;
	getFilesInDir((memc + "PSP/SAVEDATA/").c_str(), &dirs);
	
	std::vector<std::string> directories;
	for (size_t i = 0; i < dirs.size(); i++) {
		if (startsWith(dirs[i].name, id)) {
			directories.push_back(dirs[i].fullName);
		}
	}

	return directories;
}

u64 GameInfo::GetSaveDataSizeInBytes() {
	std::vector<std::string> saveDataDir = GetSaveDataDirectories();

	u64 totalSize = 0;
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(saveDataDir[j].c_str(), &fileInfo);
		// Note: getFileInDir does not fill in fileSize properly.
		for (size_t i = 0; i < fileInfo.size(); i++) {
			FileInfo finfo;
			getFileInfo(fileInfo[i].fullName.c_str(), &finfo);
			if (!finfo.isDirectory)
				totalSize += finfo.size;
		}
	}
	return totalSize;
}

bool GameInfo::DeleteAllSaveData() {
	std::vector<std::string> saveDataDir = GetSaveDataDirectories();
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(saveDataDir[j].c_str(), &fileInfo);

		u64 totalSize = 0;
		for (size_t i = 0; i < fileInfo.size(); i++) {
			deleteFile(fileInfo[i].fullName.c_str());
		}

		deleteDir(saveDataDir[j].c_str());
	}
	return true;
}

static bool ReadFileToString(IFileSystem *fs, const char *filename, std::string *contents, recursive_mutex *mtx) {
	PSPFileInfo info = fs->GetFileInfo(filename);
	if (!info.exists) {
		return false;
	}

	int handle = fs->OpenFile(filename, FILEACCESS_READ);
	if (!handle) {
		return false;
	}

	if (mtx) {
		lock_guard lock(*mtx);
		contents->resize(info.size);
		fs->ReadFile(handle, (u8 *)contents->data(), info.size);
	} else {
		contents->resize(info.size);
		fs->ReadFile(handle, (u8 *)contents->data(), info.size);
	}
	fs->CloseFile(handle);
	return true;
}


class GameInfoWorkItem : public PrioritizedWorkQueueItem {
public:
	GameInfoWorkItem(const std::string &gamePath, GameInfo *info)
		: gamePath_(gamePath), info_(info) {
	}

	virtual void run() {
		getFileInfo(gamePath_.c_str(), &info_->fileInfo);
		if (!info_->fileInfo.exists)
			return;

		std::string filename = gamePath_;
		info_->fileType = Identify_File(filename);

		switch (info_->fileType) {
		case FILETYPE_PSP_PBP:
		case FILETYPE_PSP_PBP_DIRECTORY:
			{
				std::string pbpFile = filename;
				if (info_->fileType == FILETYPE_PSP_PBP_DIRECTORY)
					pbpFile += "/EBOOT.PBP";

				PBPReader pbp(pbpFile.c_str());
				if (!pbp.IsValid())
					return;

				// First, PARAM.SFO.
				size_t sfoSize;
				u8 *sfoData = pbp.GetSubFile(PBP_PARAM_SFO, &sfoSize);
				{
					lock_guard lock(info_->lock);
					info_->paramSFO.ReadSFO(sfoData, sfoSize);
					info_->title = info_->paramSFO.GetValueString("TITLE");
					info_->id = info_->paramSFO.GetValueString("DISC_ID");
					info_->id_version = info_->paramSFO.GetValueString("DISC_ID") + "_" + info_->paramSFO.GetValueString("DISC_VERSION");

					info_->paramSFOLoaded = true;
				}
				delete [] sfoData;

				// Then, ICON0.PNG.
				{
					lock_guard lock(info_->lock);
					if (pbp.GetSubFileSize(PBP_ICON0_PNG) > 0) {
						pbp.GetSubFileAsString(PBP_ICON0_PNG, &info_->iconTextureData);
					} else {
						// Read standard icon
						size_t sz;
						INFO_LOG(HLE, "Loading unknown.png because a PBP was missing an icon");
						uint8_t *contents = VFSReadFile("unknown.png", &sz);
						if (contents) {
							lock_guard lock(info_->lock);
							info_->iconTextureData = std::string((const char *)contents, sz);
						}
						delete [] contents;
					}
				}

				if (info_->wantBG) {
					if (pbp.GetSubFileSize(PBP_PIC1_PNG) > 0) {
						lock_guard lock(info_->lock);
						pbp.GetSubFileAsString(PBP_PIC1_PNG, &info_->pic1TextureData);
					}
				}
			}
		case FILETYPE_PSP_ELF:
			// An elf on its own has no usable information, no icons, no nothing.
			info_->title = getFilename(filename);
			info_->id = "ELF000000";
			info_->id_version = "ELF000000_1.00";
			info_->paramSFOLoaded = true;

			{
				// Read standard icon
				size_t sz;
				uint8_t *contents = VFSReadFile("unknown.png", &sz);
				INFO_LOG(HLE, "Loading unknown.png because there was an ELF");
				if (contents) {
					lock_guard lock(info_->lock);
					info_->iconTextureData = std::string((const char *)contents, sz);
				}
				delete [] contents;
			}

			return;

		case FILETYPE_PSP_ISO:
		case FILETYPE_PSP_ISO_NP:
			{
				info_->fileType = FILETYPE_PSP_ISO;
				SequentialHandleAllocator handles;
				// Let's assume it's an ISO.
				// TODO: This will currently read in the whole directory tree. Not really necessary for just a
				// few files.
				BlockDevice *bd = constructBlockDevice(gamePath_.c_str());
				if (!bd)
					return;  // nothing to do here..
				ISOFileSystem umd(&handles, bd, "/PSP_GAME");

				// Alright, let's fetch the PARAM.SFO.
				std::string paramSFOcontents;
				if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents, 0)) {
					lock_guard lock(info_->lock);
					info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
					info_->title = info_->paramSFO.GetValueString("TITLE");
					info_->id = info_->paramSFO.GetValueString("DISC_ID");
					info_->id_version = info_->paramSFO.GetValueString("DISC_ID") + "_" + info_->paramSFO.GetValueString("DISC_VERSION");

					info_->paramSFOLoaded = true;
				}

				ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info_->iconTextureData, &info_->lock);
				if (info_->wantBG) {
					ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info_->pic0TextureData, &info_->lock);
				}
				ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info_->pic1TextureData, &info_->lock);
				break;
			}
		default:
			;
		}
		// probably only want these when we ask for the background image...
		// should maybe flip the flag to "onlyIcon"
		if (info_->wantBG) {
			info_->gameSize = info_->GetGameSizeInBytes();
			info_->saveDataSize = info_->GetSaveDataSizeInBytes();
		}
	}

	virtual float priority() {
		return info_->lastAccessedTime;
	}

private:
	std::string gamePath_;
	GameInfo *info_;
	DISALLOW_COPY_AND_ASSIGN(GameInfoWorkItem);
};



GameInfoCache::~GameInfoCache() {
	Clear();
}

void GameInfoCache::Init() {
	gameInfoWQ_ = new PrioritizedWorkQueue();
	ProcessWorkQueueOnThreadWhile(gameInfoWQ_);
}

void GameInfoCache::Shutdown() {
	StopProcessingWorkQueue(gameInfoWQ_);
}

void GameInfoCache::Save()
{
	// TODO
}

void GameInfoCache::Load() {
	// TODO
}

void GameInfoCache::Decimate() {
	// TODO
}

void GameInfoCache::Clear() {
	gameInfoWQ_->Flush();
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		lock_guard lock(iter->second->lock);
		if (!iter->second->pic0TextureData.empty()) {
			iter->second->pic0TextureData.clear();
		}
		if (iter->second->pic0Texture) {
			delete iter->second->pic0Texture;
			iter->second->pic0Texture = 0;
		}
		if (!iter->second->pic1TextureData.empty()) {
			iter->second->pic1TextureData.clear();
		}
		if (iter->second->pic1Texture) {
			delete iter->second->pic1Texture;
			iter->second->pic1Texture = 0;
		}
	}
	info_.clear();
}

void GameInfoCache::FlushBGs() {
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		lock_guard lock(iter->second->lock);
		if (!iter->second->pic0TextureData.empty()) {
			iter->second->pic0TextureData.clear();
		}
		if (iter->second->pic0Texture) {
			delete iter->second->pic0Texture;
			iter->second->pic0Texture = 0;
		}
		if (!iter->second->pic1TextureData.empty()) {
			iter->second->pic1TextureData.clear();
		}
		if (iter->second->pic1Texture) {
			delete iter->second->pic1Texture;
			iter->second->pic1Texture = 0;
		}
		iter->second->wantBG = false;
	}
}

void GameInfoCache::Add(const std::string &key, GameInfo *info_) {

}

// This may run off-main-thread and we thus can't use the global
// pspFileSystem (well, we could with synchronization but there might not
// even be a game running).
GameInfo *GameInfoCache::GetInfo(const std::string &gamePath, bool wantBG) {
	auto iter = info_.find(gamePath);
	if (iter != info_.end()) {
		GameInfo *info = iter->second;
		if (!info->wantBG && wantBG) {
			// Need to start over. We'll just add a new work item.
			delete info;  // Hm, how dangerous is this? There might be a race condition here.
			goto again;
		}
		{
			lock_guard lock(info->lock);
			if (info->iconTextureData.size()) {
				info->iconTexture = new Texture();
				// TODO: We could actually do the PNG decoding as well on the async thread.
				// We'd have to split up Texture->LoadPNG though, creating some intermediate Image class maybe.
				if (info->iconTexture->LoadPNG((const u8 *)info->iconTextureData.data(), info->iconTextureData.size(), false)) {
					info->timeIconWasLoaded = time_now_d();
				} else {
					delete info->iconTexture;
					info->iconTexture = 0;
				}
				info->iconTextureData.clear();
			}
		}
		{
			lock_guard lock(info->lock);
			if (info->pic0TextureData.size()) {
				info->pic0Texture = new Texture();
				if (info->pic0Texture->LoadPNG((const u8 *)info->pic0TextureData.data(), info->pic0TextureData.size(), false)) {
					info->timePic0WasLoaded = time_now_d();
				} else {
					delete info->pic0Texture;
					info->pic0Texture = 0;
				}
				info->pic0TextureData.clear();
			}
		}
		{
			lock_guard lock(info->lock);
			if (info->pic1TextureData.size()) {
				info->pic1Texture = new Texture();
				if (info->pic1Texture->LoadPNG((const u8 *)info->pic1TextureData.data(), info->pic1TextureData.size(), false)) {
					info->timePic1WasLoaded = time_now_d();
				} else {
					delete info->pic1Texture;
					info->pic1Texture = 0;
				}
				info->pic1TextureData.clear();
			}
		}
		iter->second->lastAccessedTime = time_now_d();
		return iter->second;
	}

again:

	GameInfo *info = new GameInfo();
	info->wantBG = wantBG;

	GameInfoWorkItem *item = new GameInfoWorkItem(gamePath, info);
	gameInfoWQ_->Add(item);

	info_[gamePath] = info;
	return info;
}
