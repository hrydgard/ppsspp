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

#include "Common/Common.h"

#include <string>
#include <map>
#include <memory>
#include <algorithm>

#include "Common/GPU/thin3d.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"
#include "Core/HLE/sceUtility.h"
#include "Core/ELF/PBPReader.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Loaders.h"
#include "Core/Util/GameManager.h"
#include "Core/Config.h"
#include "UI/GameInfoCache.h"

GameInfoCache *g_gameInfoCache;

void GameInfoTex::Clear() {
	if (!data.empty()) {
		data.clear();
		dataLoaded = false;
	}
	if (texture) {
		texture->Release();
		texture = nullptr;
	}
	timeLoaded = 0.0;
}

GameInfo::GameInfo(const Path &gamePath) : filePath_(gamePath) {
	// here due to a forward decl.
	fileType = IdentifiedFileType::UNKNOWN;
}

GameInfo::~GameInfo() {
	std::lock_guard<std::mutex> guard(lock);
	sndDataLoaded = false;
	icon.Clear();
	pic0.Clear();
	pic1.Clear();
	fileLoader.reset();
}

bool GameInfo::Delete() {
	switch (fileType) {
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
		{
			// Just delete the one file (TODO: handle two-disk games as well somehow).
			Path fileToRemove = filePath_;
			INFO_LOG(Log::System, "Deleting file %s", fileToRemove.c_str());
			File::Delete(fileToRemove);
			g_Config.RemoveRecent(filePath_.ToString());
			return true;
		}
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		{
			// TODO: This could be handled by Core/Util/GameManager too somehow.
			Path directoryToRemove = ResolvePBPDirectory(filePath_);
			INFO_LOG(Log::System, "Deleting directory %s", directoryToRemove.c_str());
			if (!File::DeleteDirRecursively(directoryToRemove)) {
				ERROR_LOG(Log::System, "Failed to delete file");
				return false;
			}
			g_Config.CleanRecent();
			return true;
		}
	case IdentifiedFileType::PSP_ELF:
	case IdentifiedFileType::UNKNOWN_BIN:
	case IdentifiedFileType::UNKNOWN_ELF:
	case IdentifiedFileType::UNKNOWN_ISO:
	case IdentifiedFileType::ARCHIVE_RAR:
	case IdentifiedFileType::ARCHIVE_ZIP:
	case IdentifiedFileType::ARCHIVE_7Z:
	case IdentifiedFileType::PPSSPP_GE_DUMP:
		{
			const Path &fileToRemove = filePath_;
			INFO_LOG(Log::System, "Deleting file %s", fileToRemove.c_str());
			File::Delete(fileToRemove);
			g_Config.RemoveRecent(filePath_.ToString());
			return true;
		}

	case IdentifiedFileType::PPSSPP_SAVESTATE:
		{
			const Path &ppstPath = filePath_;
			INFO_LOG(Log::System, "Deleting file %s", ppstPath.c_str());
			File::Delete(ppstPath);
			const Path screenshotPath = filePath_.WithReplacedExtension(".ppst", ".jpg");
			if (File::Exists(screenshotPath)) {
				File::Delete(screenshotPath);
			}
			return true;
		}

	default:
		INFO_LOG(Log::System, "Don't know how to delete this type of file: %s", filePath_.c_str());
		return false;
	}
}

u64 GameInfo::GetSizeOnDiskInBytes() {
	switch (fileType) {
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		return File::ComputeRecursiveDirectorySize(ResolvePBPDirectory(filePath_));
	case IdentifiedFileType::PSP_DISC_DIRECTORY:
		return File::ComputeRecursiveDirectorySize(GetFileLoader()->GetPath());
	default:
		return GetFileLoader()->FileSize();
	}
}

u64 GameInfo::GetSizeUncompressedInBytes() {
	switch (fileType) {
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		return File::ComputeRecursiveDirectorySize(ResolvePBPDirectory(filePath_));
	case IdentifiedFileType::PSP_DISC_DIRECTORY:
		return File::ComputeRecursiveDirectorySize(GetFileLoader()->GetPath());
	default:
	{
		BlockDevice *blockDevice = constructBlockDevice(GetFileLoader().get());
		if (blockDevice) {
			u64 size = blockDevice->GetUncompressedSize();
			delete blockDevice;
			return size;
		} else {
			return GetFileLoader()->FileSize();
		}
	}
	}
}

std::string GetFileDateAsString(const Path &filename) {
	tm time;
	if (File::GetModifTime(filename, time)) {
		char buf[256];
		switch (g_Config.iDateFormat) {
		case PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD:
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &time);
			break;
		case PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY:
			strftime(buf, sizeof(buf), "%m-%d-%Y %H:%M:%S", &time);
			break;
		case PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY:
			strftime(buf, sizeof(buf), "%d-%m-%Y %H:%M:%S", &time);
			break;
		default: // Should never happen
			return "";
		}
		return std::string(buf);
	}
	return "";
}

std::string GameInfo::GetMTime() const {
	switch (fileType) {
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		return GetFileDateAsString(GetFilePath() / "PARAM.SFO");
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
		return GetFileDateAsString(GetFilePath() / "EBOOT.PBP");
	default:
		return GetFileDateAsString(GetFilePath());
	}
}

// Not too meaningful if the object itself is a savedata directory...
// Call this under lock.
std::vector<Path> GameInfo::GetSaveDataDirectories() {
	_dbg_assert_(hasFlags & GameInfoFlags::PARAM_SFO);  // so we know we have the ID.
	Path memc = GetSysDirectory(DIRECTORY_SAVEDATA);

	std::vector<File::FileInfo> dirs;
	File::GetFilesInDir(memc, &dirs);

	std::vector<Path> directories;
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

u64 GameInfo::GetGameSavedataSizeInBytes() {
	if (fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY || fileType == IdentifiedFileType::PPSSPP_SAVESTATE) {
		return 0;
	}
	std::vector<Path> saveDataDir = GetSaveDataDirectories();

	u64 totalSize = 0;
	u64 filesSizeInDir = 0;
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<File::FileInfo> fileInfo;
		File::GetFilesInDir(saveDataDir[j], &fileInfo);
		for (auto const &file : fileInfo) {
			if (!file.isDirectory)
				filesSizeInDir += file.size;
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
	if (fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY || fileType == IdentifiedFileType::PPSSPP_SAVESTATE) {
		return 0;
	}
	std::vector<Path> saveDataDir = GetSaveDataDirectories();

	u64 totalSize = 0;
	u64 filesSizeInDir = 0;
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<File::FileInfo> fileInfo;
		File::GetFilesInDir(saveDataDir[j], &fileInfo);
		for (auto const &file : fileInfo) {
			// TODO: Might want to recurse here? Don't know games that use directories
			// for install-data though.
			if (!file.isDirectory)
				filesSizeInDir += file.size;
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

bool GameInfo::CreateLoader() {
	if (!fileLoader) {
		std::lock_guard<std::mutex> guard(loaderLock);
		fileLoader.reset(ConstructFileLoader(filePath_));
		if (!fileLoader)
			return false;
	}
	return true;
}

std::shared_ptr<FileLoader> GameInfo::GetFileLoader() {
	if (filePath_.empty()) {
		// Happens when workqueue tries to figure out priorities,
		// because Priority() calls GetFileLoader()... gnarly.
		return fileLoader;
	}

	std::lock_guard<std::mutex> guard(loaderLock);
	if (!fileLoader) {
		FileLoader *loader = ConstructFileLoader(filePath_);
		fileLoader.reset(loader);
		return fileLoader;
	}
	return fileLoader;
}

void GameInfo::DisposeFileLoader() {
	std::lock_guard<std::mutex> guard(loaderLock);
	fileLoader.reset();
}

bool GameInfo::DeleteAllSaveData() {
	std::vector<Path> saveDataDir = GetSaveDataDirectories();
	for (size_t j = 0; j < saveDataDir.size(); j++) {
		std::vector<File::FileInfo> fileInfo;
		File::GetFilesInDir(saveDataDir[j], &fileInfo);

		for (size_t i = 0; i < fileInfo.size(); i++) {
			File::Delete(fileInfo[i].fullName);
		}

		File::DeleteDir(saveDataDir[j]);
	}
	return true;
}

void GameInfo::ParseParamSFO() {
	title = paramSFO.GetValueString("TITLE");
	id = paramSFO.GetValueString("DISC_ID");
	id_version = id + "_" + paramSFO.GetValueString("DISC_VERSION");
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
		case 'K': region = GAMEREGION_KOREA; break;
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
}

std::string GameInfo::GetTitle() {
	std::lock_guard<std::mutex> guard(lock);
	if ((hasFlags & GameInfoFlags::PARAM_SFO) && !title.empty()) {
		return title;
	} else {
		return filePath_.GetFilename();
	}
}

void GameInfo::SetTitle(const std::string &newTitle) {
	std::lock_guard<std::mutex> guard(lock);
	title = newTitle;
}

void GameInfo::FinishPendingTextureLoads(Draw::DrawContext *draw) {
	if (draw && icon.dataLoaded && !icon.texture) {
		SetupTexture(draw, icon);
	}
	if (draw && pic0.dataLoaded && !pic0.texture) {
		SetupTexture(draw, pic0);
	}
	if (draw && pic1.dataLoaded && !pic1.texture) {
		SetupTexture(draw, pic1);
	}
}

void GameInfo::SetupTexture(Draw::DrawContext *thin3d, GameInfoTex &tex) {
	if (tex.timeLoaded) {
		// Failed before, skip.
		return;
	}
	if (tex.data.empty()) {
		tex.timeLoaded = time_now_d();
		return;
	}
	using namespace Draw;
	// TODO: Use TempImage to semi-load the image in the worker task, then here we
	// could just call CreateTextureFromTempImage.
	tex.texture = CreateTextureFromFileData(thin3d, (const uint8_t *)tex.data.data(), tex.data.size(), ImageFileType::DETECT, false, GetTitle().c_str());
	tex.timeLoaded = time_now_d();
	if (!tex.texture) {
		ERROR_LOG(Log::G3D, "Failed creating texture (%s) from %d-byte file", GetTitle().c_str(), (int)tex.data.size());
	}
}

static bool ReadFileToString(IFileSystem *fs, const char *filename, std::string *contents, std::mutex *mtx) {
	PSPFileInfo info = fs->GetFileInfo(filename);
	if (!info.exists) {
		return false;
	}

	int handle = fs->OpenFile(filename, FILEACCESS_READ);
	if (handle < 0) {
		return false;
	}
	if (mtx) {
		std::string data;
		data.resize(info.size);
		size_t readSize = fs->ReadFile(handle, (u8 *)data.data(), info.size);
		fs->CloseFile(handle);
		if (readSize != info.size) {
			return false;
		}
		std::lock_guard<std::mutex> lock(*mtx);
		*contents = std::move(data);
	} else {
		contents->resize(info.size);
		size_t readSize = fs->ReadFile(handle, (u8 *)contents->data(), info.size);
		fs->CloseFile(handle);
		if (readSize != info.size) {
			return false;
		}
	}
	return true;
}

static bool ReadLocalFileToString(const Path &path, std::string *contents, std::mutex *mtx) {
	std::string data;
	if (!File::ReadBinaryFileToString(path, &data)) {
		return false;
	}
	if (mtx) {
		std::lock_guard<std::mutex> lock(*mtx);
		*contents = std::move(data);
	} else {
		*contents = std::move(data);
	}
	return true;
}

static bool ReadVFSToString(const char *filename, std::string *contents, std::mutex *mtx) {
	size_t sz;
	uint8_t *data = g_VFS.ReadFile(filename, &sz);
	if (data) {
		if (mtx) {
			std::lock_guard<std::mutex> lock(*mtx);
			*contents = std::string((const char *)data, sz);
		} else {
			*contents = std::string((const char *)data, sz);
		}
	} else {
		return false;
	}
	delete [] data;
	return true;
}

class GameInfoWorkItem : public Task {
public:
	GameInfoWorkItem(const Path &gamePath, std::shared_ptr<GameInfo> &info, GameInfoFlags flags)
		: gamePath_(gamePath), info_(info), flags_(flags) {}

	~GameInfoWorkItem() {
		info_->DisposeFileLoader();
	}

	TaskType Type() const override {
		return TaskType::IO_BLOCKING;
	}

	TaskPriority Priority() const override {
		switch (gamePath_.Type()) {
		case PathType::NATIVE:
		case PathType::CONTENT_URI:
			return TaskPriority::NORMAL;

		default:
			// Remote/network access.
			return TaskPriority::LOW;
		}
	}

	void Run() override {
		// An early-return will result in the destructor running, where we can set
		// flags like working and pending.
		if (!info_->CreateLoader() || !info_->GetFileLoader() || !info_->GetFileLoader()->Exists()) {
			// Mark everything requested as done, so 
			std::unique_lock<std::mutex> lock(info_->lock);
			info_->MarkReadyNoLock(flags_);
			ERROR_LOG(Log::Loader, "Failed getting game info for %s", info_->GetFilePath().ToVisualString().c_str());
			return;
		}

		std::string errorString;

		if (flags_ & GameInfoFlags::FILE_TYPE) {
			info_->fileType = Identify_File(info_->GetFileLoader().get(), &errorString);
		}

		if (!info_->Ready(GameInfoFlags::FILE_TYPE) && !(flags_ & GameInfoFlags::FILE_TYPE)) {
			_dbg_assert_(false);
		}

		switch (info_->fileType) {
		case IdentifiedFileType::PSP_PBP:
		case IdentifiedFileType::PSP_PBP_DIRECTORY:
			{
				auto pbpLoader = info_->GetFileLoader();
				if (info_->fileType == IdentifiedFileType::PSP_PBP_DIRECTORY) {
					Path ebootPath = ResolvePBPFile(gamePath_);
					if (ebootPath != gamePath_) {
						pbpLoader.reset(ConstructFileLoader(ebootPath));
					}
				}

				PBPReader pbp(pbpLoader.get());
				if (!pbp.IsValid()) {
					if (pbp.IsELF()) {
						goto handleELF;
					}
					ERROR_LOG(Log::Loader, "invalid pbp '%s'\n", pbpLoader->GetPath().c_str());
					// We can't win here - just mark everything pending as fetched, and let the caller
					// handle the missing data.
					std::unique_lock<std::mutex> lock(info_->lock);
					info_->MarkReadyNoLock(flags_);
					return;
				}

				// First, PARAM.SFO.
				if (flags_ & GameInfoFlags::PARAM_SFO) {
					std::vector<u8> sfoData;
					if (pbp.GetSubFile(PBP_PARAM_SFO, &sfoData)) {
						std::lock_guard<std::mutex> lock(info_->lock);
						info_->paramSFO.ReadSFO(sfoData);
						info_->ParseParamSFO();

						// Assuming PSP_PBP_DIRECTORY without ID or with disc_total < 1 in GAME dir must be homebrew
						if ((info_->id.empty() || !info_->disc_total)
							&& gamePath_.FilePathContainsNoCase("PSP/GAME/")
							&& info_->fileType == IdentifiedFileType::PSP_PBP_DIRECTORY) {
							info_->id = g_paramSFO.GenerateFakeID(gamePath_);
							info_->id_version = info_->id + "_1.00";
							info_->region = GAMEREGION_MAX + 1; // Homebrew
						}
						info_->MarkReadyNoLock(GameInfoFlags::PARAM_SFO);
					}
				}

				// Then, ICON0.PNG.
				if (flags_ & GameInfoFlags::ICON) {
					if (pbp.GetSubFileSize(PBP_ICON0_PNG) > 0) {
						std::lock_guard<std::mutex> lock(info_->lock);
						pbp.GetSubFileAsString(PBP_ICON0_PNG, &info_->icon.data);
					} else {
						Path screenshot_jpg = GetSysDirectory(DIRECTORY_SCREENSHOT) / (info_->id + "_00000.jpg");
						Path screenshot_png = GetSysDirectory(DIRECTORY_SCREENSHOT) / (info_->id + "_00000.png");
						// Try using png/jpg screenshots first
						if (File::Exists(screenshot_png))
							ReadLocalFileToString(screenshot_png, &info_->icon.data, &info_->lock);
						else if (File::Exists(screenshot_jpg))
							ReadLocalFileToString(screenshot_jpg, &info_->icon.data, &info_->lock);
						else
							// Read standard icon
							ReadVFSToString("unknown.png", &info_->icon.data, &info_->lock);
					}
					info_->icon.dataLoaded = true;
				}

				if (flags_ & GameInfoFlags::BG) {
					if (pbp.GetSubFileSize(PBP_PIC0_PNG) > 0) {
						std::string data;
						pbp.GetSubFileAsString(PBP_PIC0_PNG, &data);
						std::lock_guard<std::mutex> lock(info_->lock);
						info_->pic0.data = std::move(data);
						info_->pic0.dataLoaded = true;
					}
					if (pbp.GetSubFileSize(PBP_PIC1_PNG) > 0) {
						std::string data;
						pbp.GetSubFileAsString(PBP_PIC1_PNG, &data);
						std::lock_guard<std::mutex> lock(info_->lock);
						info_->pic1.data = std::move(data);
						info_->pic1.dataLoaded = true;
					}
				}
				if (flags_ & GameInfoFlags::SND) {
					if (pbp.GetSubFileSize(PBP_SND0_AT3) > 0) {
						std::string data;
						pbp.GetSubFileAsString(PBP_SND0_AT3, &data);
						std::lock_guard<std::mutex> lock(info_->lock);
						info_->sndFileData = std::move(data);
						info_->sndDataLoaded = true;
					}
				}
			}
			break;

		case IdentifiedFileType::PSP_ELF:
handleELF:
			// An elf on its own has no usable information, no icons, no nothing.
			if (flags_ & GameInfoFlags::PARAM_SFO) {
				info_->id = g_paramSFO.GenerateFakeID(gamePath_);
				info_->id_version = info_->id + "_1.00";
				info_->region = GAMEREGION_MAX + 1; // Homebrew
			}

			if (flags_ & GameInfoFlags::ICON) {
				std::string id = g_paramSFO.GenerateFakeID(gamePath_);
				// Due to the dependency of the BASIC info, we fetch it already here.
				Path screenshot_jpg = GetSysDirectory(DIRECTORY_SCREENSHOT) / (id + "_00000.jpg");
				Path screenshot_png = GetSysDirectory(DIRECTORY_SCREENSHOT) / (id + "_00000.png");
				// Try using png/jpg screenshots first
				if (File::Exists(screenshot_png)) {
					ReadLocalFileToString(screenshot_png, &info_->icon.data, &info_->lock);
				} else if (File::Exists(screenshot_jpg)) {
					ReadLocalFileToString(screenshot_jpg, &info_->icon.data, &info_->lock);
				} else {
					// Read standard icon
					VERBOSE_LOG(Log::Loader, "Loading unknown.png because there was an ELF");
					ReadVFSToString("unknown.png", &info_->icon.data, &info_->lock);
				}
				info_->icon.dataLoaded = true;
			}
			break;

		case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		{
			SequentialHandleAllocator handles;
			VirtualDiscFileSystem umd(&handles, gamePath_);

			if (flags_ & GameInfoFlags::PARAM_SFO) {
				// Alright, let's fetch the PARAM.SFO.
				std::string paramSFOcontents;
				if (ReadFileToString(&umd, "/PARAM.SFO", &paramSFOcontents, 0)) {
					std::lock_guard<std::mutex> lock(info_->lock);
					info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
					info_->ParseParamSFO();
					info_->MarkReadyNoLock(GameInfoFlags::PARAM_SFO);
				}
			}
			if (flags_ & GameInfoFlags::ICON) {
				ReadFileToString(&umd, "/ICON0.PNG", &info_->icon.data, &info_->lock);
				info_->icon.dataLoaded = true;
			}
			if (flags_ & GameInfoFlags::BG) {
				ReadFileToString(&umd, "/PIC1.PNG", &info_->pic1.data, &info_->lock);
				info_->pic1.dataLoaded = true;
			}
			break;
		}

		case IdentifiedFileType::PPSSPP_SAVESTATE:
		{
			if (flags_ & GameInfoFlags::PARAM_SFO) {
				info_->SetTitle(SaveState::GetTitle(gamePath_));
				std::lock_guard<std::mutex> lock(info_->lock);
				info_->MarkReadyNoLock(GameInfoFlags::PARAM_SFO);
			}

			// Let's use the screenshot as an icon, too.
			if (flags_ & GameInfoFlags::ICON) {
				Path screenshotPath = gamePath_.WithReplacedExtension(".ppst", ".jpg");
				if (ReadLocalFileToString(screenshotPath, &info_->icon.data, &info_->lock)) {
					info_->icon.dataLoaded = true;
				} else {
					ERROR_LOG(Log::G3D, "Error loading screenshot data: '%s'", screenshotPath.c_str());
				}
			}
			break;
		}

		case IdentifiedFileType::PPSSPP_GE_DUMP:
		{
			if (flags_ & GameInfoFlags::ICON) {
				Path screenshotPath = gamePath_.WithReplacedExtension(".ppdmp", ".png");
				// Let's use the comparison screenshot as an icon, if it exists.
				if (ReadLocalFileToString(screenshotPath, &info_->icon.data, &info_->lock)) {
					info_->icon.dataLoaded = true;
				}
			}
			break;
		}

		case IdentifiedFileType::PSP_DISC_DIRECTORY:
			{
				SequentialHandleAllocator handles;
				VirtualDiscFileSystem umd(&handles, gamePath_);

				// Alright, let's fetch the PARAM.SFO.
				if (flags_ & GameInfoFlags::PARAM_SFO) {
					std::string paramSFOcontents;
					if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents, nullptr)) {
						std::lock_guard<std::mutex> lock(info_->lock);
						info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
						info_->ParseParamSFO();
					}
				}

				if (flags_ & GameInfoFlags::ICON) {
					ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info_->icon.data, &info_->lock);
					info_->icon.dataLoaded = true;
				}
				if (flags_ & GameInfoFlags::BG) {
					ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info_->pic0.data, &info_->lock);
					info_->pic0.dataLoaded = true;
					ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info_->pic1.data, &info_->lock);
					info_->pic1.dataLoaded = true;
				}
				if (flags_ & GameInfoFlags::SND) {
					ReadFileToString(&umd, "/PSP_GAME/SND0.AT3", &info_->sndFileData, &info_->lock);
					info_->pic1.dataLoaded = true;
				}
				break;
			}

		case IdentifiedFileType::PSP_ISO:
		case IdentifiedFileType::PSP_ISO_NP:
			{
				SequentialHandleAllocator handles;
				// Let's assume it's an ISO.
				// TODO: This will currently read in the whole directory tree. Not really necessary for just a
				// few files.
				auto fl = info_->GetFileLoader();
				if (!fl) {
					// BAD! Can't win here.
					ERROR_LOG(Log::Loader, "Failed getting game info for ISO %s", info_->GetFilePath().ToVisualString().c_str());
					std::unique_lock<std::mutex> lock(info_->lock);
					info_->MarkReadyNoLock(flags_);
					return;
				}
				BlockDevice *bd = constructBlockDevice(info_->GetFileLoader().get());
				if (!bd) {
					ERROR_LOG(Log::Loader, "Failed constructing block device for ISO %s", info_->GetFilePath().ToVisualString().c_str());
					std::unique_lock<std::mutex> lock(info_->lock);
					info_->MarkReadyNoLock(flags_);
					return;
				}
				ISOFileSystem umd(&handles, bd);

				// Alright, let's fetch the PARAM.SFO.
				if (flags_ & GameInfoFlags::PARAM_SFO) {
					std::string paramSFOcontents;
					if (ReadFileToString(&umd, "/PSP_GAME/PARAM.SFO", &paramSFOcontents, nullptr)) {
						{
							std::lock_guard<std::mutex> lock(info_->lock);
							info_->paramSFO.ReadSFO((const u8 *)paramSFOcontents.data(), paramSFOcontents.size());
							info_->ParseParamSFO();

							// quick-update the info while we have the lock, so we don't need to wait for the image load to display the title.
							info_->MarkReadyNoLock(GameInfoFlags::PARAM_SFO);
						}
					}
				}

				if (flags_ & GameInfoFlags::BG) {
					info_->pic0.dataLoaded = ReadFileToString(&umd, "/PSP_GAME/PIC0.PNG", &info_->pic0.data, &info_->lock);
					info_->pic1.dataLoaded = ReadFileToString(&umd, "/PSP_GAME/PIC1.PNG", &info_->pic1.data, &info_->lock);
				}

				if (flags_ & GameInfoFlags::SND) {
					info_->sndDataLoaded = ReadFileToString(&umd, "/PSP_GAME/SND0.AT3", &info_->sndFileData, &info_->lock);
				}

				// Fall back to unknown icon if ISO is broken/is a homebrew ISO, override is allowed though
				if (flags_ & GameInfoFlags::ICON) {
					if (!ReadFileToString(&umd, "/PSP_GAME/ICON0.PNG", &info_->icon.data, &info_->lock)) {
						Path screenshot_jpg = GetSysDirectory(DIRECTORY_SCREENSHOT) / (info_->id + "_00000.jpg");
						Path screenshot_png = GetSysDirectory(DIRECTORY_SCREENSHOT) / (info_->id + "_00000.png");
						// Try using png/jpg screenshots first
						if (File::Exists(screenshot_png))
							info_->icon.dataLoaded = ReadLocalFileToString(screenshot_png, &info_->icon.data, &info_->lock);
						else if (File::Exists(screenshot_jpg))
							info_->icon.dataLoaded = ReadLocalFileToString(screenshot_jpg, &info_->icon.data, &info_->lock);
						else {
							DEBUG_LOG(Log::Loader, "Loading unknown.png because no icon was found");
							info_->icon.dataLoaded = ReadVFSToString("unknown.png", &info_->icon.data, &info_->lock);
						}
					} else {
						info_->icon.dataLoaded = true;
					}
				}
				break;
			}

			case IdentifiedFileType::ARCHIVE_ZIP:
				if (flags_ & GameInfoFlags::ICON) {
					ReadVFSToString("zip.png", &info_->icon.data, &info_->lock);
					info_->icon.dataLoaded = true;
				}
				break;

			case IdentifiedFileType::ARCHIVE_RAR:
				if (flags_ & GameInfoFlags::ICON) {
					ReadVFSToString("rargray.png", &info_->icon.data, &info_->lock);
					info_->icon.dataLoaded = true;
				}
				break;

			case IdentifiedFileType::ARCHIVE_7Z:
				if (flags_ & GameInfoFlags::ICON) {
					ReadVFSToString("7z.png", &info_->icon.data, &info_->lock);
					info_->icon.dataLoaded = true;
				}
				break;

			case IdentifiedFileType::NORMAL_DIRECTORY:
			default:
				break;
		}

		if (flags_ & GameInfoFlags::PARAM_SFO) {
			// We fetch the hasConfig together with the params, since that's what fills out the id.
			info_->hasConfig = g_Config.hasGameConfig(info_->id);
		}

		if (flags_ & GameInfoFlags::SIZE) {
			std::lock_guard<std::mutex> lock(info_->lock);
			info_->gameSizeOnDisk = info_->GetSizeOnDiskInBytes();
			switch (info_->fileType) {
			case IdentifiedFileType::PSP_ISO:
			case IdentifiedFileType::PSP_ISO_NP:
			case IdentifiedFileType::PSP_DISC_DIRECTORY:
			case IdentifiedFileType::PSP_PBP:
			case IdentifiedFileType::PSP_PBP_DIRECTORY:
				info_->saveDataSize = info_->GetGameSavedataSizeInBytes();
				info_->installDataSize = info_->GetInstallDataSizeInBytes();
				break;
			default:
				info_->saveDataSize = 0;
				info_->installDataSize = 0;
				break;
			}
		}
		if (flags_ & GameInfoFlags::UNCOMPRESSED_SIZE) {
			info_->gameSizeUncompressed = info_->GetSizeUncompressedInBytes();
		}

		// Time to update the flags.
		std::unique_lock<std::mutex> lock(info_->lock);
		info_->MarkReadyNoLock(flags_);
		// INFO_LOG(Log::System, "Completed writing info for %s", info_->GetTitle().c_str());
	}

private:
	Path gamePath_;
	std::shared_ptr<GameInfo> info_;
	GameInfoFlags flags_{};

	DISALLOW_COPY_AND_ASSIGN(GameInfoWorkItem);
};

GameInfoCache::GameInfoCache() {
	Init();
}

GameInfoCache::~GameInfoCache() {
	Clear();
	Shutdown();
}

void GameInfoCache::Init() {}

void GameInfoCache::Shutdown() {
	CancelAll();
}

void GameInfoCache::Clear() {
	CancelAll();

	std::lock_guard<std::mutex> lock(mapLock_);
	info_.clear();
}

void GameInfoCache::CancelAll() {
	std::lock_guard<std::mutex> lock(mapLock_);
	for (auto info : info_) {
		// GetFileLoader will create one if there isn't one already.
		// Avoid that by checking.
		if (info.second->HasFileLoader()) {
			auto fl = info.second->GetFileLoader();
			if (fl) {
				fl->Cancel();
			}
		}
	}
}

void GameInfoCache::FlushBGs() {
	std::lock_guard<std::mutex> lock(mapLock_);
	for (auto iter = info_.begin(); iter != info_.end(); iter++) {
		std::lock_guard<std::mutex> lock(iter->second->lock);
		iter->second->pic0.Clear();
		iter->second->pic1.Clear();
		if (!iter->second->sndFileData.empty()) {
			iter->second->sndFileData.clear();
			iter->second->sndDataLoaded = false;
		}
		iter->second->hasFlags &= ~(GameInfoFlags::BG | GameInfoFlags::SND);
	}
}

void GameInfoCache::PurgeType(IdentifiedFileType fileType) {
	bool retry = false;
	int retryCount = 10;
	// Trickery to avoid sleeping with the lock held.
	do {
		if (retry) {
			retryCount--;
			if (retryCount == 0) {
				break;
			}
		}
		retry = false;
		{
			std::lock_guard<std::mutex> lock(mapLock_);
			for (auto iter = info_.begin(); iter != info_.end();) {
				auto &info = iter->second;
				if (!(info->hasFlags & GameInfoFlags::FILE_TYPE)) {
					iter++;
					continue;
				}
				if (info->fileType != fileType) {
					iter++;
					continue;
				}
				// TODO: Find a better way to wait here.
				if (info->pendingFlags != (GameInfoFlags)0) {
					INFO_LOG(Log::Loader, "%s: pending flags %08x, retrying", info->GetTitle().c_str(), (int)info->pendingFlags);
					retry = true;
					break;
				}
				iter = info_.erase(iter);
			}
		}

		sleep_ms(10);
	} while (retry);
}

// Call on the main thread ONLY - that is from stuff called from NativeFrame.
// Can also be called from the audio thread for menu background music, but that cannot request images!
std::shared_ptr<GameInfo> GameInfoCache::GetInfo(Draw::DrawContext *draw, const Path &gamePath, GameInfoFlags wantFlags) {
	const std::string &pathStr = gamePath.ToString();

	// _dbg_assert_(gamePath != GetSysDirectory(DIRECTORY_SAVEDATA));

	// This is always needed to determine the method to get the other info, so make sure it's computed first.
	wantFlags |= GameInfoFlags::FILE_TYPE;

	mapLock_.lock();

	auto iter = info_.find(pathStr);
	if (iter != info_.end()) {
		// There's already a structure about this game. Let's check.
		std::shared_ptr<GameInfo> info = iter->second;
		mapLock_.unlock();

		info->FinishPendingTextureLoads(draw);
		info->lastAccessedTime = time_now_d();
		GameInfoFlags wanted = (GameInfoFlags)0;
		{
			// Careful now!
			std::unique_lock<std::mutex> lock(info->lock);
			GameInfoFlags hasFlags = info->hasFlags | info->pendingFlags;  // We don't want to re-fetch data that we have, so or in pendingFlags.
			wanted = (GameInfoFlags)((int)wantFlags & ~(int)hasFlags);  // & is reserved for testing. ugh.
			info->pendingFlags |= wanted;
		}
		if (wanted != (GameInfoFlags)0) {
			// We're missing info that we want. Go get it!
			GameInfoWorkItem *item = new GameInfoWorkItem(gamePath, info, wanted);
			g_threadManager.EnqueueTask(item);
		}
		return info;
	}

	std::shared_ptr<GameInfo> info = std::make_shared<GameInfo>(gamePath);
	info->pendingFlags = wantFlags;
	info->lastAccessedTime = time_now_d();
	info_.insert(std::make_pair(pathStr, info));
	mapLock_.unlock();

	// Just get all the stuff we wanted.
	GameInfoWorkItem *item = new GameInfoWorkItem(gamePath, info, wantFlags);
	g_threadManager.EnqueueTask(item);
	return info;
}
