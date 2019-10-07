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

#include <algorithm>
#include <cctype>
#include <string>
#include <set>
#include <cstring>
#include <thread>

#include "file/file_util.h"
#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif
#include "util/text/utf8.h"

#include "Common/Log.h"
#include "Common/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/ELF/PBPReader.h"
#include "Core/System.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/Util/GameManager.h"
#include "i18n/i18n.h"

GameManager g_GameManager;

GameManager::GameManager() {
}

std::string GameManager::GetTempFilename() const {
#ifdef _WIN32
	wchar_t tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	wchar_t buffer[MAX_PATH];
	GetTempFileName(tempPath, L"PSP", 1, buffer);
	return ConvertWStringToUTF8(buffer);
#else
	return g_Config.memStickDirectory + "/ppsspp.dl";
#endif
}

bool GameManager::IsGameInstalled(std::string name) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	return File::Exists(pspGame + name);
}

bool GameManager::DownloadAndInstall(std::string storeFileUrl) {
	if (curDownload_.get() != nullptr) {
		ERROR_LOG(HLE, "Can only process one download at a time");
		return false;
	}
	if (installInProgress_) {
		ERROR_LOG(HLE, "Can't download when an install is in progress (yet)");
		return false;
	}

	std::string filename = GetTempFilename();
	curDownload_ = g_DownloadManager.StartDownload(storeFileUrl, filename);
	return true;
}

bool GameManager::CancelDownload() {
	if (!curDownload_)
		return false;

	curDownload_->Cancel();
	curDownload_.reset();
	return true;
}

bool GameManager::Uninstall(std::string name) {
	if (name.empty()) {
		ERROR_LOG(HLE, "Cannot remove an empty-named game");
		return false;
	}
	std::string gameDir = GetSysDirectory(DIRECTORY_GAME) + name;
	INFO_LOG(HLE, "Deleting '%s'", gameDir.c_str());
	if (!File::Exists(gameDir)) {
		ERROR_LOG(HLE, "Game '%s' not installed, cannot uninstall", name.c_str());
		return false;
	}

	bool success = File::DeleteDirRecursively(gameDir);
	if (success) {
		INFO_LOG(HLE, "Successfully deleted game '%s'", name.c_str());
		g_Config.CleanRecent();
		return true;
	} else {
		ERROR_LOG(HLE, "Failed to delete game '%s'", name.c_str());
		return false;
	}
}

void GameManager::Update() {
	if (curDownload_.get() && curDownload_->Done()) {
		INFO_LOG(HLE, "Download completed! Status = %d", curDownload_->ResultCode());
		std::string fileName = curDownload_->outfile();
		if (curDownload_->ResultCode() == 200) {
			if (!File::Exists(fileName)) {
				ERROR_LOG(HLE, "Downloaded file '%s' does not exist :(", fileName.c_str());
				curDownload_.reset();
				return;
			}
			// Game downloaded to temporary file - install it!
			InstallGameOnThread(curDownload_->url(), fileName, true);
		} else {
			ERROR_LOG(HLE, "Expected HTTP status code 200, got status code %d. Install cancelled, deleting partial file '%s'",
				curDownload_->ResultCode(), fileName.c_str());
			File::Delete(fileName.c_str());
		}
		curDownload_.reset();
	}

	if (installDonePending_) {
		if (installThread_.get() != nullptr) {
			if (installThread_->joinable())
				installThread_->join();
			installThread_.reset();
		}
		installDonePending_ = false;
	}
}

static void countSlashes(const std::string &fileName, int *slashLocation, int *slashCount) {
	*slashCount = 0;
	int lastSlashLocation = -1;
	*slashLocation = -1;
	for (size_t i = 0; i < fileName.size(); i++) {
		if (fileName[i] == '/') {
			(*slashCount)++;
			*slashLocation = lastSlashLocation;
			lastSlashLocation = (int)i;
		}
	}
}

ZipFileContents DetectZipFileContents(std::string fileName, ZipFileInfo *info) {
	int error = 0;
#ifdef _WIN32
	struct zip *z = zip_open(ConvertUTF8ToWString(fileName).c_str(), 0, &error);
#else
	struct zip *z = zip_open(fileName.c_str(), 0, &error);
#endif
	if (!z) {
		return ZipFileContents::UNKNOWN;
	}
	ZipFileContents retVal = DetectZipFileContents(z, info);
	zip_close(z);
	return retVal;
}

inline char asciitolower(char in) {
	if (in <= 'Z' && in >= 'A')
		return in - ('Z' - 'z');
	return in;
}

ZipFileContents DetectZipFileContents(struct zip *z, ZipFileInfo *info) {
	int numFiles = zip_get_num_files(z);

	// Verify that this is a PSP zip file with the correct layout. We also try
	// to detect simple zipped ISO files, those we'll just "install" to the current
	// directory of the Games tab (where else?).
	bool isPSPMemstickGame = false;
	bool isZippedISO = false;
	bool isTexturePack = false;
	int stripChars = 0;
	int isoFileIndex = -1;
	int stripCharsTexturePack = -1;
	int textureIniIndex = -1;

	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		std::transform(zippedName.begin(), zippedName.end(), zippedName.begin(),
			[](unsigned char c) { return asciitolower(c); });  // Not using std::tolower to avoid Turkish I->ı conversion.
		if (zippedName.find("eboot.pbp") != std::string::npos) {
			int slashCount = 0;
			int slashLocation = -1;
			countSlashes(zippedName, &slashLocation, &slashCount);
			if (slashCount >= 1 && (!isPSPMemstickGame || slashLocation < stripChars + 1)) {
				stripChars = slashLocation + 1;
				isPSPMemstickGame = true;
			} else {
				INFO_LOG(HLE, "Wrong number of slashes (%i) in '%s'", slashCount, fn);
			}
		} else if (endsWith(zippedName, ".iso") || endsWith(zippedName, ".cso")) {
			int slashCount = 0;
			int slashLocation = -1;
			countSlashes(zippedName, &slashLocation, &slashCount);
			if (slashCount <= 1) {
				// We only do this if the ISO file is in the root or one level down.
				isZippedISO = true;
				isoFileIndex = i;
			}
		} else if (zippedName.find("textures.ini") != std::string::npos) {
			int slashLocation = (int)zippedName.find_last_of('/');
			if (stripCharsTexturePack == -1 || slashLocation < stripCharsTexturePack + 1) {
				stripCharsTexturePack = slashLocation + 1;
				isTexturePack = true;
				textureIniIndex = i;
			}
		}
	}

	info->stripChars = stripChars;
	info->numFiles = numFiles;
	info->isoFileIndex = isoFileIndex;
	info->textureIniIndex = textureIniIndex;
	info->ignoreMetaFiles = false;

	// If a ZIP is detected as both, let's let the memstick game interpretation prevail.
	if (isPSPMemstickGame) {
		return ZipFileContents::PSP_GAME_DIR;
	} else if (isZippedISO) {
		return ZipFileContents::ISO_FILE;
	} else if (isTexturePack) {
		info->stripChars = stripCharsTexturePack;
		info->ignoreMetaFiles = true;
		return ZipFileContents::TEXTURE_PACK;
	} else {
		return ZipFileContents::UNKNOWN;
	}
}

bool GameManager::InstallGame(const std::string &url, const std::string &fileName, bool deleteAfter) {
	if (installInProgress_) {
		ERROR_LOG(HLE, "Cannot have two installs in progress at the same time");
		return false;
	}

	if (!File::Exists(fileName)) {
		ERROR_LOG(HLE, "Game file '%s' doesn't exist", fileName.c_str());
		return false;
	}

	// Examine the URL to guess out what we're installing.
	if (endsWithNoCase(url, ".cso") || endsWithNoCase(url, ".iso")) {
		// It's a raw ISO or CSO file. We just copy it to the destination.
		std::string shortFilename = GetFilenameFromPath(url);
		return InstallRawISO(fileName, shortFilename, deleteAfter);
	}

	I18NCategory *sy = GetI18NCategory("System");
	installInProgress_ = true;

	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	std::string dest = pspGame;
	int error = 0;
#ifdef _WIN32
	struct zip *z = zip_open(ConvertUTF8ToWString(fileName).c_str(), 0, &error);
#else
	struct zip *z = zip_open(fileName.c_str(), 0, &error);
#endif
	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file '%s', error code=%i", fileName.c_str(), error);
		return false;
	}

	ZipFileInfo info;
	ZipFileContents contents = DetectZipFileContents(z, &info);
	switch (contents) {
	case ZipFileContents::PSP_GAME_DIR:
		INFO_LOG(HLE, "Installing '%s' into '%s'", fileName.c_str(), pspGame.c_str());
		// InstallMemstickGame contains code to close z.
		return InstallMemstickGame(z, fileName, pspGame, info, false, deleteAfter);
	case ZipFileContents::ISO_FILE:
		INFO_LOG(HLE, "Installing '%s' into its containing directory", fileName.c_str());
		// InstallZippedISO contains code to close z.
		return InstallZippedISO(z, info.isoFileIndex, fileName, deleteAfter);
	case ZipFileContents::TEXTURE_PACK:
		// InstallMemstickGame contains code to close z, and works for textures too.
		if (DetectTexturePackDest(z, info.textureIniIndex, &dest)) {
			INFO_LOG(HLE, "Installing '%s' into '%s'", fileName.c_str(), dest.c_str());
			File::CreateFullPath(dest);
			File::CreateEmptyFile(dest + "/.nomedia");
			return InstallMemstickGame(z, fileName, dest, info, true, deleteAfter);
		} else {
			zip_close(z);
			z = nullptr;
		}
		return false;
	default:
		ERROR_LOG(HLE, "File not a PSP game, no EBOOT.PBP found.");
		SetInstallError(sy->T("Not a PSP game"));
		zip_close(z);
		z = nullptr;
		if (deleteAfter)
			File::Delete(fileName);
		return false;
	}
}

bool GameManager::DetectTexturePackDest(struct zip *z, int iniIndex, std::string *dest) {
	I18NCategory *iz = GetI18NCategory("InstallZip");

	struct zip_stat zstat;
	zip_stat_index(z, iniIndex, 0, &zstat);

	if (zstat.size >= 32 * 1024 * 1024) {
		SetInstallError(iz->T("Texture pack doesn't support install"));
		return false;
	}

	std::string buffer;
	buffer.resize(zstat.size);
	zip_file *zf = zip_fopen_index(z, iniIndex, 0);
	if (zip_fread(zf, &buffer[0], buffer.size()) != (ssize_t)zstat.size) {
		SetInstallError(iz->T("Zip archive corrupt"));
		return false;
	}

	IniFile ini;
	std::stringstream sstream(buffer);
	ini.Load(sstream);

	auto games = ini.GetOrCreateSection("games")->ToMap();
	if (games.empty()) {
		SetInstallError(iz->T("Texture pack doesn't support install"));
		return false;
	}

	std::string gameID = games.begin()->first;
	if (games.size() > 1) {
		// Check for any supported game on their recent list and use that instead.
		for (const std::string &path : g_Config.recentIsos) {
			std::string recentID = GetGameID(path);
			if (games.find(recentID) != games.end()) {
				gameID = recentID;
				break;
			}
		}
	}

	std::string pspTextures = GetSysDirectory(DIRECTORY_TEXTURES);
	*dest = pspTextures + gameID + "/";
	return true;
}

void GameManager::SetInstallError(const std::string &err) {
	installProgress_ = 0.0f;
	installInProgress_ = false;
	installError_ = err;
	InstallDone();
}

std::string GameManager::GetGameID(const std::string &path) const {
	auto loader = ConstructFileLoader(path);
	std::string id;

	switch (Identify_File(loader)) {
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
		delete loader;
		loader = ConstructFileLoader(ResolvePBPFile(path));
		id = GetPBPGameID(loader);
		break;

	case IdentifiedFileType::PSP_PBP:
		id = GetPBPGameID(loader);
		break;

	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
		id = GetISOGameID(loader);
		break;

	default:
		id.clear();
		break;
	}

	delete loader;
	return id;
}

std::string GameManager::GetPBPGameID(FileLoader *loader) const {
	PBPReader pbp(loader);
	std::vector<u8> sfoData;
	if (pbp.GetSubFile(PBP_PARAM_SFO, &sfoData)) {
		ParamSFOData sfo;
		sfo.ReadSFO(sfoData);
		return sfo.GetValueString("DISC_ID");
	}
	return "";
}

std::string GameManager::GetISOGameID(FileLoader *loader) const {
	SequentialHandleAllocator handles;
	BlockDevice *bd = constructBlockDevice(loader);
	if (!bd) {
		return "";
	}
	ISOFileSystem umd(&handles, bd);

	PSPFileInfo info = umd.GetFileInfo("/PSP_GAME/PARAM.SFO");
	int handle = 0;
	if (info.exists) {
		handle = umd.OpenFile("/PSP_GAME/PARAM.SFO", FILEACCESS_READ);
	}

	std::string sfoData;
	sfoData.resize(info.size);
	umd.ReadFile(handle, (u8 *)&sfoData[0], info.size);
	umd.CloseFile(handle);

	ParamSFOData sfo;
	sfo.ReadSFO((const u8 *)sfoData.data(), sfoData.size());
	return sfo.GetValueString("DISC_ID");
}

bool GameManager::ExtractFile(struct zip *z, int file_index, std::string outFilename, size_t *bytesCopied, size_t allBytes) {
	struct zip_stat zstat;
	zip_stat_index(z, file_index, 0, &zstat);
	size_t size = zstat.size;

	// Don't spam the log.
	if (file_index < 10) {
		INFO_LOG(HLE, "Writing %d bytes to '%s'", (int)size, outFilename.c_str());
	}

	zip_file *zf = zip_fopen_index(z, file_index, 0);
	if (!zf) {
		ERROR_LOG(HLE, "Failed to open file by index (%d) (%s)", file_index, outFilename.c_str());
		return false;
	}

	FILE *f = File::OpenCFile(outFilename, "wb");
	if (f) {
		size_t pos = 0;
		const size_t blockSize = 1024 * 128;
		u8 *buffer = new u8[blockSize];
		while (pos < size) {
			size_t readSize = std::min(blockSize, size - pos);
			ssize_t retval = zip_fread(zf, buffer, readSize);
			if (retval < 0 || (size_t)retval < readSize) {
				ERROR_LOG(HLE, "Failed to read %d bytes from zip (%d) - archive corrupt?", (int)readSize, (int)retval);
				delete[] buffer;
				fclose(f);
				zip_fclose(zf);
				File::Delete(outFilename.c_str());
				return false;
			}
			size_t written = fwrite(buffer, 1, readSize, f);
			if (written != readSize) {
				ERROR_LOG(HLE, "Wrote %d bytes out of %d - Disk full?", (int)written, (int)readSize);
				delete[] buffer;
				fclose(f);
				zip_fclose(zf);
				File::Delete(outFilename.c_str());
				return false;
			}
			pos += readSize;

			*bytesCopied += readSize;
			installProgress_ = (float)*bytesCopied / (float)allBytes;
		}
		zip_fclose(zf);
		fclose(f);
		delete[] buffer;
		return true;
	} else {
		ERROR_LOG(HLE, "Failed to open file for writing");
		return false;
	}
}

bool GameManager::InstallMemstickGame(struct zip *z, const std::string &zipfile, const std::string &dest, const ZipFileInfo &info, bool allowRoot, bool deleteAfter) {
	size_t allBytes = 0;
	size_t bytesCopied = 0;

	I18NCategory *sy = GetI18NCategory("System");

	auto fileAllowed = [&](const char *fn) {
		if (!allowRoot && strchr(fn, '/') == 0)
			return false;

		const char *basefn = strrchr(fn, '/');
		basefn = basefn ? basefn + 1 : fn;

		if (info.ignoreMetaFiles) {
			if (basefn[0] == '.' || !strcmp(basefn, "Thumbs.db") || !strcmp(basefn, "desktop.ini"))
				return false;
		}

		return true;
	};

	// Create all the directories first in one pass
	std::set<std::string> createdDirs;
	for (int i = 0; i < info.numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		if (zippedName.length() < (size_t)info.stripChars) {
			continue;
		}
		std::string outFilename = dest + zippedName.substr(info.stripChars);
		bool isDir = *outFilename.rbegin() == '/';
		if (!isDir && outFilename.find("/") != std::string::npos) {
			outFilename = outFilename.substr(0, outFilename.rfind('/'));
		}
		if (createdDirs.find(outFilename) == createdDirs.end()) {
			File::CreateFullPath(outFilename.c_str());
			createdDirs.insert(outFilename);
		}
		if (!isDir && fileAllowed(fn)) {
			struct zip_stat zstat;
			if (zip_stat_index(z, i, 0, &zstat) >= 0) {
				allBytes += zstat.size;
			}
		}
	}

	// Now, loop through again in a second pass, writing files.
	std::vector<std::string> createdFiles;
	for (int i = 0; i < info.numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		// Note that we do NOT write files that are not in a directory, to avoid random
		// README files etc. (unless allowRoot is true.)
		if (fileAllowed(fn) && strlen(fn) > (size_t)info.stripChars) {
			fn += info.stripChars;
			std::string outFilename = dest + fn;
			bool isDir = *outFilename.rbegin() == '/';
			if (isDir)
				continue;

			if (!ExtractFile(z, i, outFilename, &bytesCopied, allBytes)) {
				goto bail;
			} else {
				createdFiles.push_back(outFilename);
			}
		}
	}
	INFO_LOG(HLE, "Extracted %i files (%i bytes / %i).", info.numFiles, (int)bytesCopied, (int)allBytes);

	zip_close(z);
	z = nullptr;
	installProgress_ = 1.0f;
	installInProgress_ = false;
	installError_ = "";
	if (deleteAfter) {
		File::Delete(zipfile.c_str());
	}
	InstallDone();
	return true;

bail:
	// We end up here if disk is full or couldn't write to storage for some other reason.
	zip_close(z);
	// We don't delete the original in this case. Try to delete the files we created so far.
	for (size_t i = 0; i < createdFiles.size(); i++) {
		File::Delete(createdFiles[i].c_str());
	}
	for (auto iter = createdDirs.begin(); iter != createdDirs.end(); ++iter) {
		File::DeleteDir(iter->c_str());
	}
	SetInstallError(sy->T("Storage full"));
	return false;
}

bool GameManager::InstallZippedISO(struct zip *z, int isoFileIndex, std::string zipfile, bool deleteAfter) {
	// Let's place the output file in the currently selected Games directory.

	std::string fn = zip_get_name(z, isoFileIndex, 0);
	size_t nameOffset = fn.rfind('/');
	if (nameOffset == std::string::npos) {
		nameOffset = 0;
	} else {
		nameOffset++;
	}
	size_t allBytes = 1;
	struct zip_stat zstat;
	if (zip_stat_index(z, isoFileIndex, 0, &zstat) >= 0) {
		allBytes += zstat.size;
	}

	std::string outputISOFilename = g_Config.currentDirectory + "/" + fn.substr(nameOffset);
	size_t bytesCopied = 0;
	if (ExtractFile(z, isoFileIndex, outputISOFilename, &bytesCopied, allBytes)) {
		ILOG("Successfully extracted ISO file to '%s'", outputISOFilename.c_str());
	}
	zip_close(z);
	if (deleteAfter) {
		File::Delete(zipfile.c_str());
	}

	z = 0;
	installProgress_ = 1.0f;
	installInProgress_ = false;
	installError_ = "";
	InstallDone();
	return true;
}

bool GameManager::InstallGameOnThread(std::string url, std::string fileName, bool deleteAfter) {
	if (installInProgress_) {
		return false;
	}
	installThread_.reset(new std::thread(std::bind(&GameManager::InstallGame, this, url, fileName, deleteAfter)));
	return true;
}

bool GameManager::InstallRawISO(const std::string &file, const std::string &originalName, bool deleteAfter) {
	std::string destPath = g_Config.currentDirectory + "/" + originalName;
	// TODO: To save disk space, we should probably attempt a move first.
	if (File::Copy(file, destPath)) {
		if (deleteAfter) {
			File::Delete(file.c_str());
		}
	}
	installProgress_ = 1.0f;
	installInProgress_ = false;
	installError_ = "";
	InstallDone();
	return true;
}

void GameManager::InstallDone() {
	installDonePending_ = true;
}
