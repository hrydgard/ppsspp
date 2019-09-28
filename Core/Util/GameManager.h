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


// Manages the PSP/GAME directory contents.
//
// Not concerned with full ISOs.

#pragma once

#include <thread>
#include "net/http_client.h"

enum class GameManagerState {
	IDLE,
	DOWNLOADING,
	INSTALLING,
};

struct zip;
class FileLoader;
struct ZipFileInfo;

class GameManager {
public:
	GameManager();

	bool IsGameInstalled(std::string name);

	// This starts off a background process.
	bool DownloadAndInstall(std::string storeZipUrl);
	bool Uninstall(std::string name);

	// Cancels the download in progress, if any.
	bool CancelDownload();

	// Call from time to time to check on completed downloads from the
	// main UI thread.
	void Update();

	GameManagerState GetState() {
		if (installInProgress_)
			return GameManagerState::INSTALLING;
		if (curDownload_)
			return GameManagerState::DOWNLOADING;
		return GameManagerState::IDLE;
	}

	float GetCurrentInstallProgressPercentage() const {
		return installProgress_;
	}
	std::string GetInstallError() const {
		return installError_;
	}

	// Only returns false if there's already an installation in progress.
	bool InstallGameOnThread(std::string url, std::string tempFileName, bool deleteAfter);

private:
	bool InstallGame(const std::string &url, const std::string &tempFileName, bool deleteAfter);
	bool InstallMemstickGame(struct zip *z, const std::string &zipFile, const std::string &pspGame, const ZipFileInfo &info, bool allowRoot, bool deleteAfter);
	bool InstallZippedISO(struct zip *z, int isoFileIndex, std::string zipfile, bool deleteAfter);
	bool InstallRawISO(const std::string &zipFile, const std::string &originalName, bool deleteAfter);
	void InstallDone();
	bool ExtractFile(struct zip *z, int file_index, std::string outFilename, size_t *bytesCopied, size_t allBytes);
	bool DetectTexturePackDest(struct zip *z, int iniIndex, std::string *dest);
	void SetInstallError(const std::string &err);

	std::string GetTempFilename() const;
	std::string GetGameID(const std::string &path) const;
	std::string GetPBPGameID(FileLoader *loader) const;
	std::string GetISOGameID(FileLoader *loader) const;
	std::shared_ptr<http::Download> curDownload_;
	std::shared_ptr<std::thread> installThread_;
	bool installInProgress_ = false;
	bool installDonePending_ = false;
	float installProgress_ = 0.0f;
	std::string installError_;
};

extern GameManager g_GameManager;

enum class ZipFileContents {
	UNKNOWN,
	PSP_GAME_DIR,
	ISO_FILE,
	TEXTURE_PACK,
};

struct ZipFileInfo {
	int numFiles;
	int stripChars;  // for PSP game
	int isoFileIndex;  // for ISO
	int textureIniIndex;  // for textures
	bool ignoreMetaFiles;
};

ZipFileContents DetectZipFileContents(struct zip *z, ZipFileInfo *info);
ZipFileContents DetectZipFileContents(std::string fileName, ZipFileInfo *info);
