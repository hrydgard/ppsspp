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

#include "file/file_util.h"
#include "ext/libzip/zip.h"
#include "thread/thread.h"
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
	return g_Config.externalDirectory + "/ppsspp.dl";
#endif
}

bool GameManager::IsGameInstalled(std::string name) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	return File::Exists(pspGame + name);
}

bool GameManager::DownloadAndInstall(std::string storeZipUrl) {
	if (curDownload_.get() != 0) {
		ERROR_LOG(HLE, "Can only process one download at a time");
		return false;
	}
	if (installInProgress_) {
		ERROR_LOG(HLE, "Can't download when an install is in progress (yet)");
		return false;
	}

	std::string filename = GetTempFilename();
	curDownload_ = g_DownloadManager.StartDownload(storeZipUrl, filename);
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
		std::string zipName = curDownload_->outfile();
		if (curDownload_->ResultCode() == 200) {
			if (!File::Exists(zipName)) {
				ERROR_LOG(HLE, "Downloaded file %s does not exist :(", zipName.c_str());
				curDownload_.reset();
				return;
			}
			// Install the game!
			InstallGame(zipName);
			// Doesn't matter if the install succeeds or not, we delete the temp file to not squander space.
			// TODO: Handle disk full?
			deleteFile(zipName.c_str());
		} else {
			ERROR_LOG(HLE, "Expected HTTP status code 200, got status code %i. Install cancelled.", curDownload_->ResultCode());
			deleteFile(zipName.c_str());
		}
		curDownload_.reset();
	}
}

bool GameManager::InstallGame(std::string zipfile, bool deleteAfter) {
	if (installInProgress_) {
		ERROR_LOG(HLE, "Cannot have two installs in progress at the same time");
		return false;
	}

	I18NCategory *s = GetI18NCategory("System");
	installInProgress_ = true;

	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	INFO_LOG(HLE, "Installing %s into %s", zipfile.c_str(), pspGame.c_str());

	if (!File::Exists(zipfile)) {
		ERROR_LOG(HLE, "ZIP file %s doesn't exist", zipfile.c_str());
		return false;
	}

	int error;
#ifdef _WIN32
	struct zip *z = zip_open(ConvertUTF8ToWString(zipfile).c_str(), 0, &error);
#elif defined(__SYMBIAN32__)
	// If zipfile is non-ascii, this may not function correctly. Other options?
	struct zip *z = zip_open(std::wstring(zipfile.begin(), zipfile.end()).c_str(), 0, &error);
#else
	struct zip *z = zip_open(zipfile.c_str(), 0, &error);
#endif
	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file %s, error code=%i", zipfile.c_str(), error);
		return false;
	}

	int numFiles = zip_get_num_files(z);

	// First, find all the directories, and precreate them before we fill in with files.
	// Also, verify that this is a PSP zip file with the correct layout.
	bool isPSP = false;
	int stripChars = 0;

	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		if (zippedName.find("EBOOT.PBP") != std::string::npos) {
			int slashCount = 0;
			int lastSlashLocation = -1;
			int slashLocation = -1;
			for (size_t i = 0; i < zippedName.size(); i++) {
				if (zippedName[i] == '/') {
					slashCount++;
					slashLocation = lastSlashLocation;
					lastSlashLocation = (int)i;
				}
			}
			if (slashCount >= 1 && (!isPSP || slashLocation < stripChars + 1)) {
				stripChars = slashLocation + 1;
				isPSP = true;
			} else {
				INFO_LOG(HLE, "Wrong number of slashes (%i) in %s", slashCount, zippedName.c_str());
			}
		}
	}

	if (!isPSP) {
		ERROR_LOG(HLE, "File not a PSP game, no EBOOT.PBP found.");
		installProgress_ = 0.0f;
		installInProgress_ = false;
		installError_ = s->T("Not a PSP game");
		InstallDone();
		return false;
	}

	size_t allBytes = 0, bytesCopied = 0;

	// Create all the directories in one pass
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
			struct zip_stat zstat;
			zip_stat_index(z, i, 0, &zstat);
			size_t size = zstat.size;

			fn += stripChars;

			std::string outFilename = pspGame + fn;
			bool isDir = *outFilename.rbegin() == '/';
			if (isDir)
				continue;

			if (i < 10) {
				INFO_LOG(HLE, "Writing %i bytes to %s", (int)size, outFilename.c_str());
			}

			zip_file *zf = zip_fopen_index(z, i, 0);
			FILE *f = fopen(outFilename.c_str(), "wb");
			if (f) {
				size_t pos = 0;
				const size_t blockSize = 1024 * 128;
				u8 *buffer = new u8[blockSize];
				while (pos < size) {
					size_t bs = std::min(blockSize, pos - size);
					zip_fread(zf, buffer, bs);
					size_t written = fwrite(buffer, 1, bs, f);
					if (written != bs) {
						ERROR_LOG(HLE, "Wrote %i bytes out of %i - Disk full?", (int)written, (int)bs);
						delete [] buffer;
						buffer = 0;
						fclose(f);
						zip_fclose(zf);
						deleteFile(outFilename.c_str());
						// Yes it's a goto. Sue me. I think it's appropriate here.
						goto bail;
					}
					pos += bs;

					bytesCopied += bs;
					installProgress_ = (float)bytesCopied / (float)allBytes;
					// printf("Progress: %f\n", installProgress_);
				}
				zip_fclose(zf);
				fclose(f);
				createdFiles.push_back(outFilename);
				delete [] buffer;
			} else {
				ERROR_LOG(HLE, "Failed to open file for writing");
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
		deleteFile(zipfile.c_str());
	}
	InstallDone();
	return true;

bail:
	zip_close(z);
	// We end up here if disk is full or couldn't write to storage for some other reason.
	installProgress_ = 0.0f;
	installInProgress_ = false;
	installError_ = s->T("Storage full");
	if (deleteAfter) {
		deleteFile(zipfile.c_str());
	}
	for (size_t i = 0; i < createdFiles.size(); i++) {
		deleteFile(createdFiles[i].c_str());
	}
	for (auto iter = createdDirs.begin(); iter != createdDirs.end(); ++iter) {
		deleteDir(iter->c_str());
	}
	InstallDone();
	return false;
}

bool GameManager::InstallGameOnThread(std::string zipFile, bool deleteAfter) {
	if (installInProgress_) {
		return false;
	}

	installThread_.reset(new std::thread(std::bind(&GameManager::InstallGame, this, zipFile, deleteAfter)));
	installThread_->detach();
	return true;
}

void GameManager::InstallDone() {
	if (installThread_.get() != 0) {
		installThread_.reset();
	}
}
