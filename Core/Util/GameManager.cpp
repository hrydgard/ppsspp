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
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "i18n/i18n.h"

GameManager g_GameManager;

GameManager::GameManager()
	: installInProgress_(false), installProgress_(0.0f) {
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
	if (curDownload_.get() != 0) {
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
	INFO_LOG(HLE, "Deleting %s", gameDir.c_str());
	if (!File::Exists(gameDir)) {
		ERROR_LOG(HLE, "Game %s not installed, cannot uninstall", name.c_str());
		return false;
	}

	bool success = File::DeleteDirRecursively(gameDir);
	if (success) {
		INFO_LOG(HLE, "Successfully deleted game %s", name.c_str());
		g_Config.CleanRecent();
		return true;
	} else {
		ERROR_LOG(HLE, "Failed to delete game %s", name.c_str());
		return false;
	}
}

void GameManager::Update() {
	if (curDownload_.get() && curDownload_->Done()) {
		INFO_LOG(HLE, "Download completed! Status = %i", curDownload_->ResultCode());
		std::string fileName = curDownload_->outfile();
		if (curDownload_->ResultCode() == 200) {
			if (!File::Exists(fileName)) {
				ERROR_LOG(HLE, "Downloaded file %s does not exist :(", fileName.c_str());
				curDownload_.reset();
				return;
			}
			// Game downloaded to temporary file - install it!
			InstallGameOnThread(fileName, true);
		} else {
			ERROR_LOG(HLE, "Expected HTTP status code 200, got status code %i. Install cancelled, deleting partial file.", curDownload_->ResultCode());
			File::Delete(fileName.c_str());
		}
		curDownload_.reset();
	}
}

void countSlashes(std::string fileName, int *slashLocation, int *slashCount) {
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

ZipFileContents DetectZipFileContents(struct zip *z, ZipFileInfo *info) {
	int numFiles = zip_get_num_files(z);

	// Verify that this is a PSP zip file with the correct layout. We also try
	// to detect simple zipped ISO files, those we'll just "install" to the current
	// directory of the Games tab (where else?).
	bool isPSPMemstickGame = false;
	bool isZippedISO = false;
	int stripChars = 0;
	int isoFileIndex = -1;

	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		if (zippedName.find("EBOOT.PBP") != std::string::npos) {
			int slashCount = 0;
			int slashLocation = -1;
			countSlashes(zippedName, &slashLocation, &slashCount);
			// TODO: Rewrite this...
			if (slashCount >= 1 && (!isPSPMemstickGame || slashLocation < stripChars + 1)) {
				stripChars = slashLocation + 1;
				isPSPMemstickGame = true;
			} else {
				INFO_LOG(HLE, "Wrong number of slashes (%i) in %s", slashCount, zippedName.c_str());
			}
		} else if (endsWithNoCase(zippedName, ".iso") || endsWithNoCase(zippedName, ".cso")) {
			int slashCount = 0;
			int slashLocation = -1;
			countSlashes(zippedName, &slashLocation, &slashCount);
			if (slashCount <= 1) {
				// We only do this if the ISO file is in the root or one level down.
				isZippedISO = true;
				isoFileIndex = i;
			}
		}
	}

	info->stripChars = stripChars;
	info->numFiles = numFiles;
	info->isoFileIndex = isoFileIndex;
	// If a ZIP is detected as both, let's let the memstick game interpretation prevail.
	if (isPSPMemstickGame) {
		return ZipFileContents::PSP_GAME_DIR;
	} else if (isZippedISO) {
		return ZipFileContents::ISO_FILE;
	} else {
		return ZipFileContents::UNKNOWN;
	}
}

bool GameManager::InstallGame(std::string fileName, bool deleteAfter) {
	if (installInProgress_) {
		ERROR_LOG(HLE, "Cannot have two installs in progress at the same time");
		return false;
	}

	if (!File::Exists(fileName)) {
		ERROR_LOG(HLE, "Game file %s doesn't exist", fileName.c_str());
		return false;
	}

	bool isRawISO = false;  // TODO: Make it possible to pass in this information.
	if (isRawISO) {
		return InstallRawISO(fileName, fileName);
	}

	I18NCategory *sy = GetI18NCategory("System");
	installInProgress_ = true;

	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	INFO_LOG(HLE, "Installing '%s' into '%s'", fileName.c_str(), pspGame.c_str());
	int error = 0;
#ifdef _WIN32
	struct zip *z = zip_open(ConvertUTF8ToWString(fileName).c_str(), 0, &error);
#else
	struct zip *z = zip_open(fileName.c_str(), 0, &error);
#endif
	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file %s, error code=%i", fileName.c_str(), error);
		return false;
	}

	ZipFileInfo info;
	ZipFileContents contents = DetectZipFileContents(z, &info);
	switch (contents) {
	case ZipFileContents::PSP_GAME_DIR:
		// InstallMemstickGame contains code to close z.
		return InstallMemstickGame(z, fileName, pspGame, info.numFiles, info.stripChars, deleteAfter);
	case ZipFileContents::ISO_FILE:
		return InstallZippedISO(z, info.isoFileIndex, fileName, deleteAfter);
	default:
		ERROR_LOG(HLE, "File not a PSP game, no EBOOT.PBP found.");
		installProgress_ = 0.0f;
		installInProgress_ = false;
		installError_ = sy->T("Not a PSP game");
		InstallDone();
		if (deleteAfter)
			File::Delete(fileName);
		return false;
	}
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
	FILE *f = File::OpenCFile(outFilename, "wb");
	if (f) {
		size_t pos = 0;
		const size_t blockSize = 1024 * 128;
		u8 *buffer = new u8[blockSize];
		while (pos < size) {
			size_t bs = std::min(blockSize, size - pos);
			zip_fread(zf, buffer, bs);
			size_t written = fwrite(buffer, 1, bs, f);
			if (written != bs) {
				ERROR_LOG(HLE, "Wrote %d bytes out of %d - Disk full?", (int)written, (int)bs);
				delete[] buffer;
				buffer = 0;
				fclose(f);
				zip_fclose(zf);
				File::Delete(outFilename.c_str());
				return false;
			}
			pos += bs;

			*bytesCopied += bs;
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

bool GameManager::InstallMemstickGame(struct zip *z, std::string zipfile, std::string pspGame, int numFiles, int stripChars, bool deleteAfter) {
	size_t allBytes = 0;
	size_t bytesCopied = 0;

	I18NCategory *sy = GetI18NCategory("System");

	// Create all the directories first in one pass
	std::set<std::string> createdDirs;
	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		std::string outFilename = pspGame + zippedName.substr(stripChars);
		bool isDir = *outFilename.rbegin() == '/';
		if (!isDir && outFilename.find("/") != std::string::npos) {
			outFilename = outFilename.substr(0, outFilename.rfind('/'));
		}
		if (createdDirs.find(outFilename) == createdDirs.end()) {
			File::CreateFullPath(outFilename.c_str());
			createdDirs.insert(outFilename);
		}
		if (!isDir && strchr(fn, '/') != 0) {
			struct zip_stat zstat;
			if (zip_stat_index(z, i, 0, &zstat) >= 0) {
				allBytes += zstat.size;
			}
		}
	}

	// Now, loop through again in a second pass, writing files.
	std::vector<std::string> createdFiles;
	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		// Note that we do NOT write files that are not in a directory, to avoid random
		// README files etc.
		if (strchr(fn, '/') != 0) {
			fn += stripChars;
			std::string outFilename = pspGame + fn;
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
	INFO_LOG(HLE, "Extracted %i files (%i bytes / %i).", numFiles, (int)bytesCopied, (int)allBytes);

	zip_close(z);
	z = 0;
	installProgress_ = 1.0f;
	installInProgress_ = false;
	installError_ = "";
	if (deleteAfter) {
		File::Delete(zipfile.c_str());
	}
	InstallDone();
	return true;

bail:
	zip_close(z);
	// We end up here if disk is full or couldn't write to storage for some other reason.
	installProgress_ = 0.0f;
	installInProgress_ = false;
	installError_ = sy->T("Storage full");
	// Should we really delete in this case???
	if (deleteAfter) {
		File::Delete(zipfile.c_str());
	}
	for (size_t i = 0; i < createdFiles.size(); i++) {
		File::Delete(createdFiles[i].c_str());
	}
	for (auto iter = createdDirs.begin(); iter != createdDirs.end(); ++iter) {
		File::DeleteDir(iter->c_str());
	}
	InstallDone();
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

bool GameManager::InstallGameOnThread(std::string zipFile, bool deleteAfter) {
	if (installInProgress_) {
		return false;
	}
	installThread_.reset(new std::thread(std::bind(&GameManager::InstallGame, this, zipFile, deleteAfter)));
	installThread_->detach();
	return true;
}

bool GameManager::InstallRawISO(std::string file, std::string originalName) {
	std::string destPath = g_Config.currentDirectory + "/" + originalName;
	File::Copy(file, destPath);
	installProgress_ = 1.0f;
	installInProgress_ = false;
	installError_ = "";
	InstallDone();
	return true;
}

void GameManager::InstallDone() {
	if (installThread_.get() != 0) {
		installThread_.reset();
	}
}
