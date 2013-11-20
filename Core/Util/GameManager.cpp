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

#include "file/file_util.h"
#include "native/ext/libzip/zip.h"

#include "Common/Log.h"
#include "Common/FileUtil.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"

GameManager g_GameManager;

bool GameManager::IsGameInstalled(std::string name) {
	return false;
}

bool GameManager::DownloadAndInstall(std::string storeZipUrl) {
	if (curDownload_.get() != 0) {
		ERROR_LOG(HLE, "Can only process one download at a time");
		return false;
	}

	// TODO: Android-compatible temp file names
	curDownload_ = downloader_.StartDownload(storeZipUrl, "/tmp/ppsspp.dl");
	return true;
}

void GameManager::Uninstall(std::string name) {

}

void GameManager::Update() {
	if (curDownload_.get() && curDownload_->Done()) {
		// Install the game!
		std::string zipName = curDownload_->outfile();
		InstallGame(zipName);
		// Doesn't matter if the install succeeds or not, we delete the temp file to not squander space.
		// TODO: Handle disk full?
		deleteFile(zipName.c_str());
		curDownload_.reset();
	}
}

void GameManager::InstallGame(std::string zipfile) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	INFO_LOG(HLE, "Installing %s into %s", zipfile.c_str(), pspGame.c_str());

	int error;
	struct zip *z = zip_open(zipfile.c_str(), 0, &error);
	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file %s, error code=%i", zipfile.c_str(), error);
		return;
	}

	int numFiles = zip_get_num_files(z);

	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		if (strstr(fn, "/") != 0) {
			INFO_LOG(HLE, "File: %i: %s", i, fn);
			struct zip_stat zstat;
			int x = zip_stat_index(z, i, 0, &zstat);
			size_t size = zstat.size;
			u8 *buffer = new u8[size];
			zip_file *zf = zip_fopen_index(z, i, 0);
			zip_fread(zf, buffer, size);
			zip_fclose(zf);

			std::string outFilename = pspGame + fn;
			bool isDir = outFilename.back() == '/';
			if (isDir) {
				File::CreateFullPath(outFilename.c_str());
			} else {
				INFO_LOG(HLE, "Writing %i bytes to %s", (int)size, outFilename.c_str());
				FILE *f = fopen(outFilename.c_str(), "wb");
				if (f) {
					fwrite(buffer, 1, size, f);
					fclose(f);
				} else {
					ERROR_LOG(HLE, "Failed to open file for writing");
				}
				delete [] buffer;
			}
		}
	}

	zip_close(z);
}
