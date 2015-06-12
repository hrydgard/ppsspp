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
#include <memory>
#include <algorithm>

#include "base/logging.h"
#include "base/timeutil.h"
#include "base/stringutil.h"
#include "file/file_util.h"
#include "file/zip_read.h"
#include "thin3d/thin3d.h"
#include "thread/prioritizedworkqueue.h"
#include "Common/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "Core/ELF/PBPReader.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "Core/Config.h"
#include "UI/GameInfoCache.h"

#ifdef __SYMBIAN32__
#define unique_ptr auto_ptr
#endif

GameInfoCache g_gameInfoCache;

GameInfo::~GameInfo() {
	delete fileLoader;
}

bool GameInfo::Delete() {
	switch (fileType) {
	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
		{
			// Just delete the one file (TODO: handle two-disk games as well somehow).
			const char *fileToRemove = filePath_.c_str();
			deleteFile(fileToRemove);
			auto i = std::find(g_Config.recentIsos.begin(), g_Config.recentIsos.end(), fileToRemove);
			if (i != g_Config.recentIsos.end()) {
				g_Config.recentIsos.erase(i);
			}
			return true;
		}
	case FILETYPE_PSP_PBP_DIRECTORY:
	case FILETYPE_PSP_SAVEDATA_DIRECTORY:
		{
			// TODO: This could be handled by Core/Util/GameManager too somehow.
			const char *directoryToRemove = filePath_.c_str();
			INFO_LOG(HLE, "Deleting %s", directoryToRemove);
			if (!File::DeleteDirRecursively(directoryToRemove)) {
				ERROR_LOG(HLE, "Failed to delete file");
				return false;
			}
			g_Config.CleanRecent();
			return true;
		}
	case FILETYPE_PSP_ELF:
	case FILETYPE_PPSSPP_SAVESTATE:
	case FILETYPE_UNKNOWN_BIN:
	case FILETYPE_UNKNOWN_ELF:
	case FILETYPE_ARCHIVE_RAR:
	case FILETYPE_ARCHIVE_ZIP:
	case FILETYPE_ARCHIVE_7Z:
		{
			const char *fileToRemove = filePath_.c_str();
			deleteFile(fileToRemove);
			return true;
		}

	default:
		return false;
	}
}

static int64_t GetDirectoryRecursiveSize(std::string path) {
	std::vector<FileInfo> fileInfo;
	getFilesInDir(path.c_str(), &fileInfo);
	int64_t sizeSum = 0;
	// Note: getFileInDir does not fill in fileSize properly.
	for (size_t i = 0; i < fileInfo.size(); i++) {
		FileInfo finfo;
		getFileInfo(fileInfo[i].fullName.c_str(), &finfo);
		if (!finfo.isDirectory)
			sizeSum += finfo.size;
		else
			sizeSum += GetDirectoryRecursiveSize(finfo.fullName);
	}
	return sizeSum;
}

u64 GameInfo::GetGameSizeInBytes() {
	switch (fileType) {
	case FILETYPE_PSP_PBP_DIRECTORY:
	case FILETYPE_PSP_SAVEDATA_DIRECTORY:
	{
		return GetDirectoryRecursiveSize(filePath_);
	}
	default:
		return GetFileLoader()->FileSize();
	}
}

// Not too meaningful if the object itself is a savedata directory...
std::vector<std::string> GameInfo::GetSaveDataDirectories() {
	std::string memc = GetSysDirectory(DIRECTORY_SAVEDATA);

	std::vector<FileInfo> dirs;
	getFilesInDir(memc.c_str(), &dirs);

	std::vector<std::string> directories;
	if (id.size() < 5) {
		return directories;
	}
	for (size_t i = 0; i < dirs.size(); i++) {
		if (startsWith(dirs[i].name, id)) {
			directories.push_back(dirs[i].fullName);
		}
	}

	return directories;
}

u64 GameInfo::GetSaveDataSizeInBytes() {
	if (fileType == FILETYPE_PSP_SAVEDATA_DIRECTORY || fileType == FILETYPE_PPSSPP_SAVESTATE) {
		return 0;
	}
	std::vector<std::string> saveDataDir = GetSaveDataDirectories();

	u64 totalSize = 0;
	u64 filesSizeInDir = 0;
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(saveDataDir[j].c_str(), &fileInfo);
		// Note: getFileInDir does not fill in fileSize properly.
		for (size_t i = 0; i < fileInfo.size(); i++) {
			FileInfo finfo;
			getFileInfo(fileInfo[i].fullName.c_str(), &finfo);
			if (!finfo.isDirectory)
				filesSizeInDir += finfo.size;
		}
		if (filesSizeInDir < 0xA00000) {
			// HACK: Generally the savedata size in a dir shouldn't be more than 10MB.
			totalSize += filesSizeInDir;
		}
		filesSizeInDir = 0;
	}
	return totalSize;
}

u64 GameInfo::GetInstallDataSizeInBytes() {
	if (fileType == FILETYPE_PSP_SAVEDATA_DIRECTORY || fileType == FILETYPE_PPSSPP_SAVESTATE) {
		return 0;
	}
	std::vector<std::string> saveDataDir = GetSaveDataDirectories();

	u64 totalSize = 0;
	u64 filesSizeInDir = 0;
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<FileInfo> fileInfo;
		getFilesInDir(saveDataDir[j].c_str(), &fileInfo);
		// Note: getFileInDir does not fill in fileSize properly.
		for (size_t i = 0; i < fileInfo.size(); i++) {
			FileInfo finfo;
			getFileInfo(fileInfo[i].fullName.c_str(), &finfo);
			if (!finfo.isDirectory)
				filesSizeInDir += finfo.size;
		}
		if (filesSizeInDir >= 0xA00000) { 
			// HACK: Generally the savedata size in a dir shouldn't be more than 10MB.
			// This is probably GameInstall data.
			totalSize += filesSizeInDir;
		}
		filesSizeInDir = 0;
	}
	return totalSize;
}

bool GameInfo::LoadFromPath(const std::string &gamePath) {
	// No need to rebuild if we already have it loaded.
	if (filePath_ != gamePath) {
		delete fileLoader;
		fileLoader = ConstructFileLoader(gamePath);
		filePath_ = gamePath;
	}

	return GetFileLoader()->Exists();
}

FileLoader *GameInfo::GetFileLoader() {
	if (!fileLoader) {
		fileLoader = ConstructFileLoader(filePath_);
	}
	return fileLoader;
}

void GameInfo::DisposeFileLoader() {
	delete fileLoader;
	fileLoader = nullptr;
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

void GameInfo::ParseParamSFO() {
	title = paramSFO.GetValueString("TITLE");
	id = paramSFO.GetValueString("DISC_ID");
	id_version = paramSFO.GetValueString("DISC_ID") + "_" + paramSFO.GetValueString("DISC_VERSION");
	disc_total = paramSFO.GetValueInt("DISC_TOTAL");
	disc_number = paramSFO.GetValueInt("DISC_NUMBER");
	// region = paramSFO.GetValueInt("REGION");  // Always seems to be 32768?

	region = GAMEREGION_OTHER;
	if (id_version.size() >= 4) {
		std::string regStr = id_version.substr(0, 4);

		// Guesswork
		switch (regStr[2]) {
		case 'E': region = GAMEREGION_EUROPE; break;
		case 'U': region = GAMEREGION_USA; break;
		case 'J': region = GAMEREGION_JAPAN; break;
		case 'H': region = GAMEREGION_HONGKONG; break;
		case 'A': region = GAMEREGION_ASIA; break;
		}
		/*
		if (regStr == "NPEZ" || regStr == "NPEG" || regStr == "ULES" || regStr == "UCES" ||
			  regStr == "NPEX") {
			region = GAMEREGION_EUROPE;
		} else if (regStr == "NPUG" || regStr == "NPUZ" || regStr == "ULUS" || regStr == "UCUS") {
			region = GAMEREGION_USA;
		} else if (regStr == "NPJH" || regStr == "NPJG" || regStr == "ULJM"|| regStr == "ULJS") {
			region = GAMEREGION_JAPAN;
		} else if (regStr == "NPHG") {
			region = GAMEREGION_HONGKONG;
		} else if (regStr == "UCAS") {
			region = GAMEREGION_CHINA;
		}*/
	}

	paramSFOLoaded = true;
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
		if (!info_->LoadFromPath(gamePath_))
			return;

		std::string filename = gamePath_;
		info_->path = gamePath_;
		info_->fileType = Identify_File(info_->GetFileLoader());
		// Fallback title
		info_->title = getFilename(info_->path);

		switch (info_->fileType) {
		case FILETYPE_PSP_PBP:
		case FILETYPE_PSP_PBP_DIRECTORY:
			{
				std::string pbpFile = filename;
				if (info_->fileType == FILETYPE_PSP_PBP_DIRECTORY)
					pbpFile += "/EBOOT.PBP";

				PBPReader pbp(pbpFile.c_str());
				if (!pbp.IsValid()) {
					if (pbp.IsELF()) {
						goto handleELF;
					}
					ERROR_LOG(LOADER, "invalid pbp %s\n", pbpFile.c_str());
					return;
				}

				// First, PARAM.SFO.
				size_t sfoSize;
				u8 *sfoData = pbp.GetSubFile(PBP_PARAM_SFO, &sfoSize);
				{
					lock_guard lock(info_->lock);
					info_->paramSFO.ReadSFO(sfoData, sfoSize);
					info_->ParseParamSFO();
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
						DEBUG_LOG(LOADER, "Loading unknown.png because a PBP was missing an icon");
						uint8_t *contents = VFSReadFile("unknown.png", &sz);
						if (contents) {
							lock_guard lock(info_->lock);
							info_->iconTextureData = std::string((const char *)contents, sz);
						}
						delete [] contents;
					}
					info_->iconDataLoaded = true;
				}

				if (info_->wantFlags & GAMEINFO_WANTBG) {
					if (pbp.GetSubFileSize(PBP_PIC0_PNG) > 0) {
						lock_guard lock(info_->lock);
						pbp.GetSubFileAsString(PBP_PIC0_PNG, &info_->pic0TextureData);
						info_->pic0DataLoaded = true;
					}
					if (pbp.GetSubFileSize(PBP_PIC1_PNG) > 0) {
						lock_guard lock(info_->lock);
						pbp.GetSubFileAsString(PBP_PIC1_PNG, &info_->pic1TextureData);
						info_->pic1DataLoaded = true;
					}
				}
				if (info_->wantFlags & GAMEINFO_WANTSND) {
					if (pbp.GetSubFileSize(PBP_SND0_AT3) > 0) {
						lock_guard lock(info_->lock);
						pbp.GetSubFileAsString(PBP_SND0_AT3, &info_->sndFileData);
						info_->sndDataLoaded = true;
					}
				}
			}
			break;

		case FILETYPE_PSP_ELF:
handleELF:
			// An elf on its own has no usable information, no icons, no nothing.
			info_->title = getFilename(filename);
			info_->id = "ELF000000";
			info_->id_version = "ELF000000_1.00";
			info_->paramSFOLoaded = true;
			{
				// Read standard icon
				size_t sz;
				uint8_t *contents = VFSReadFile("unknown.png", &sz);
				DEBUG_LOG(LOADER, "Loading unknown.png because there was an ELF");
				if (contents) {
					lock_guard lock(info_->lock);
					info_->iconTextureData = std::string((const char *)contents, sz);
					info_->iconDataLoaded = true;
				}
				delete [] contents;
			}
			break;

		case FILETYPE_PSP_SAVEDATA_DIRECTORY:
		{
			SequentialHandleAllocator handles;
			VirtualDiscFileSystem umd(&handles, gamePath_.c_str());

			// Alright, let's fetch the PARAM.SFO.
			std::string paramSFOcontents;
			if (ReadFileToString(&umd, "/PARAM.SFO", &paramSFOcontents, 0)) {
				lock_guard lock(info_->lock);
				info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
				info_->ParseParamSFO();
			}

			ReadFileToString(&umd, "/ICON0.PNG", &info_->iconTextureData, &info_->lock);
			info_->iconDataLoaded = true;
			if (info_->wantFlags & GAMEINFO_WANTBG) {
				ReadFileToString(&umd, "/PIC1.PNG", &info_->pic1TextureData, &info_->lock);
				info_->pic1DataLoaded = true;
			}
			break;
		}

		case FILETYPE_PPSSPP_SAVESTATE:
		{
			break;
		}

		case FILETYPE_PSP_DISC_DIRECTORY:
			{
				info_->fileType = FILETYPE_PSP_ISO;
				SequentialHandleAllocator handles;
				VirtualDiscFileSystem umd(&handles, gamePath_.c_str());

				// Alright, let's fetch the PARAM.SFO.
				std::string paramSFOcontents;
				if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents, 0)) {
					lock_guard lock(info_->lock);
					info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
					info_->ParseParamSFO();
				}

				ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info_->iconTextureData, &info_->lock);
				info_->iconDataLoaded = true;
				if (info_->wantFlags & GAMEINFO_WANTBG) {
					ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info_->pic0TextureData, &info_->lock);
					info_->pic0DataLoaded = true;
					ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info_->pic1TextureData, &info_->lock);
					info_->pic1DataLoaded = true;
				}
				if (info_->wantFlags & GAMEINFO_WANTSND) {
					ReadFileToString(&umd, "/PSP_GAME/SND0.AT3", &info_->sndFileData, &info_->lock);
					info_->pic1DataLoaded = true;
				}
				break;
			}

		case FILETYPE_PSP_ISO:
		case FILETYPE_PSP_ISO_NP:
			{
				info_->fileType = FILETYPE_PSP_ISO;
				SequentialHandleAllocator handles;
				// Let's assume it's an ISO.
				// TODO: This will currently read in the whole directory tree. Not really necessary for just a
				// few files.
				BlockDevice *bd = constructBlockDevice(info_->GetFileLoader());
				if (!bd)
					return;  // nothing to do here..
				ISOFileSystem umd(&handles, bd, "/PSP_GAME");

				// Alright, let's fetch the PARAM.SFO.
				std::string paramSFOcontents;
				if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents, 0)) {
					lock_guard lock(info_->lock);
					info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
					info_->ParseParamSFO();

					if (info_->wantFlags & GAMEINFO_WANTBG) {
						ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info_->pic0TextureData, &info_->lock);
						info_->pic0DataLoaded = true;
						ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info_->pic1TextureData, &info_->lock);
						info_->pic1DataLoaded = true;
					}
					if (info_->wantFlags & GAMEINFO_WANTSND) {
						ReadFileToString(&umd, "/PSP_GAME/SND0.AT3", &info_->sndFileData, &info_->lock);
						info_->pic1DataLoaded = true;
					}
				}

				// Fall back to unknown icon if ISO is broken/is a homebrew ISO, override is allowed though
				if (!ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info_->iconTextureData, &info_->lock)) {
					size_t sz;
					uint8_t *contents = VFSReadFile("unknown.png", &sz);
					DEBUG_LOG(LOADER, "Loading unknown.png because no icon was found");
					if (contents) {
						lock_guard lock(info_->lock);
						info_->iconTextureData = std::string((const char *)contents, sz);
					}
					delete [] contents;
				}
				info_->iconDataLoaded = true;
				break;
			}

			case FILETYPE_ARCHIVE_ZIP:
				info_->paramSFOLoaded = true;
				{
					// Read standard icon
					size_t sz;
					uint8_t *contents = VFSReadFile("zip.png", &sz);
					if (contents) {
						lock_guard lock(info_->lock);
						info_->iconTextureData = std::string((const char *)contents, sz);
						info_->iconDataLoaded = true;
					}
					delete [] contents;
				}
				break;

			case FILETYPE_ARCHIVE_RAR:
				info_->paramSFOLoaded = true;
				{
					// Read standard icon
					size_t sz;
					uint8_t *contents = VFSReadFile("rargray.png", &sz);
					if (contents) {
						lock_guard lock(info_->lock);
						info_->iconTextureData = std::string((const char *)contents, sz);
						info_->iconDataLoaded = true;
					}
					delete [] contents;
				}
				break;

			case FILETYPE_ARCHIVE_7Z:
				info_->paramSFOLoaded = true;
				{
					// Read standard icon
					size_t sz;
					uint8_t *contents = VFSReadFile("7z.png", &sz);
					if (contents) {
						lock_guard lock(info_->lock);
						info_->iconTextureData = std::string((const char *)contents, sz);
						info_->iconDataLoaded = true;
					}
					delete[] contents;
				}
				break;

			case FILETYPE_NORMAL_DIRECTORY:
			default:
				info_->paramSFOLoaded = true;
				break;
		}

		if (info_->wantFlags & GAMEINFO_WANTSIZE) {
			info_->gameSize = info_->GetGameSizeInBytes();
			info_->saveDataSize = info_->GetSaveDataSizeInBytes();
			info_->installDataSize = info_->GetInstallDataSizeInBytes();
		}

		info_->DisposeFileLoader();
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

void GameInfoCache::Clear() {
	if (gameInfoWQ_)
		gameInfoWQ_->Flush();
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		lock_guard lock(iter->second->lock);
		if (!iter->second->pic0TextureData.empty()) {
			iter->second->pic0TextureData.clear();
			iter->second->pic0DataLoaded = false;
		}
		if (iter->second->pic0Texture) {
			delete iter->second->pic0Texture;
			iter->second->pic0Texture = 0;
		}
		if (!iter->second->pic1TextureData.empty()) {
			iter->second->pic1TextureData.clear();
			iter->second->pic1DataLoaded = false;
		}
		if (iter->second->pic1Texture) {
			delete iter->second->pic1Texture;
			iter->second->pic1Texture = 0;
		}
		if (!iter->second->iconTextureData.empty()) {
			iter->second->iconTextureData.clear();
			iter->second->iconDataLoaded = false;
		}
		if (iter->second->iconTexture) {
			delete iter->second->iconTexture;
			iter->second->iconTexture = 0;
		}

		if (!iter->second->sndFileData.empty()) {
			iter->second->sndFileData.clear();
			iter->second->sndDataLoaded = false;
		}
	}
	info_.clear();
}

void GameInfoCache::FlushBGs() {
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		lock_guard lock(iter->second->lock);
		if (!iter->second->pic0TextureData.empty()) {
			iter->second->pic0TextureData.clear();
			iter->second->pic0DataLoaded = false;
		}
		if (iter->second->pic0Texture) {
			delete iter->second->pic0Texture;
			iter->second->pic0Texture = 0;
		}

		if (!iter->second->pic1TextureData.empty()) {
			iter->second->pic1TextureData.clear();
			iter->second->pic1DataLoaded = false;
		}
		if (iter->second->pic1Texture) {
			delete iter->second->pic1Texture;
			iter->second->pic1Texture = 0;
		}

		if (!iter->second->sndFileData.empty()) {
			iter->second->sndFileData.clear();
			iter->second->sndDataLoaded = false;
		}
		iter->second->wantFlags &= ~(GAMEINFO_WANTBG | GAMEINFO_WANTSND);
	}
}

void GameInfoCache::PurgeType(IdentifiedFileType fileType) {
	if (gameInfoWQ_)
		gameInfoWQ_->Flush();
	restart:
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		if (iter->second->fileType == fileType) {
			info_.erase(iter);
			goto restart;
		}
	}
}


// Runs on the main thread.
GameInfo *GameInfoCache::GetInfo(Thin3DContext *thin3d, const std::string &gamePath, int wantFlags) {
	GameInfo *info = 0;

	auto iter = info_.find(gamePath);
	if (iter != info_.end()) {
		info = iter->second;
		if ((info->wantFlags & wantFlags) != wantFlags) {
			// Need to start over. We'll just add a new work item.
			goto again;
		}
		if (thin3d && info->iconDataLoaded) {
			SetupTexture(info, info->iconTextureData, thin3d, info->iconTexture, info->timeIconWasLoaded);
			info->iconDataLoaded = false;
		}
		if (thin3d && info->pic0DataLoaded) {
			SetupTexture(info, info->pic0TextureData, thin3d, info->pic0Texture, info->timePic0WasLoaded);
			info->pic0DataLoaded = false;
		}
		if (thin3d && info->pic1DataLoaded) {
			SetupTexture(info, info->pic1TextureData, thin3d, info->pic1Texture, info->timePic1WasLoaded);
			info->pic1DataLoaded = false;
		}
		iter->second->lastAccessedTime = time_now_d();
		return iter->second;
	}

again:

	if (!info) {
		info = new GameInfo();
	}
	{
		lock_guard lock(info->lock);
		info->wantFlags |= wantFlags;
	}

	GameInfoWorkItem *item = new GameInfoWorkItem(gamePath, info);
	gameInfoWQ_->Add(item);

	info_[gamePath] = info;
	return info;
}

void GameInfoCache::SetupTexture(GameInfo *info, std::string &textureData, Thin3DContext *thin3d, Thin3DTexture *&tex, double &loadTime) {
	if (textureData.size()) {
		if (!tex) {
			tex = thin3d->CreateTextureFromFileData((const uint8_t *)textureData.data(), (int)textureData.size(), T3DImageType::PNG);
			if (tex) {
				loadTime = time_now_d();
			}
		}
		textureData.clear();
	}
}
