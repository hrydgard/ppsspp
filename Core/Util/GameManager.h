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
#include <atomic>

#include "Common/Net/HTTPClient.h"
#include "Common/File/Path.h"

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

	bool IsGameInstalled(const std::string &name);

	// This starts off a background process.
	bool DownloadAndInstall(const std::string &storeZipUrl);
	bool IsDownloading(const std::string &storeZipUrl);

	// Cancels the download in progress, if any.
	bool CancelDownload();

	float DownloadSpeedKBps();

	// Call from time to time to check on completed downloads from the
	// main UI thread.
	void Update();

	GameManagerState GetState() {
		if (InstallInProgress() || installDonePending_)
			return GameManagerState::INSTALLING;
		if (curDownload_)
			return GameManagerState::DOWNLOADING;
		return GameManagerState::IDLE;
	}

	float GetCurrentInstallProgressPercentage() const {
		return installProgress_;
	}
	void ResetInstallError();
	std::string GetInstallError() const {
		return installError_;
	}

	// Only returns false if there's already an installation in progress.
	bool InstallGameOnThread(const Path &url, const Path &tempFileName, bool deleteAfter);
	bool UninstallGameOnThread(const std::string &name);

private:
	// TODO: The return value on this is a bit pointless, we can't get at it.
	bool InstallGame(const Path &url, const Path &tempFileName, bool deleteAfter);
	bool InstallMemstickGame(struct zip *z, const Path &zipFile, const Path &dest, const ZipFileInfo &info, bool allowRoot, bool deleteAfter);
	bool InstallMemstickZip(struct zip *z, const Path &zipFile, const Path &dest, const ZipFileInfo &info, bool deleteAfter);
	bool InstallZippedISO(struct zip *z, int isoFileIndex, const Path &zipfile, bool deleteAfter);
	bool InstallRawISO(const Path &zipFile, const std::string &originalName, bool deleteAfter);
	void UninstallGame(const std::string &name);

	void InstallDone();

	bool ExtractFile(struct zip *z, int file_index, const Path &outFilename, size_t *bytesCopied, size_t allBytes);
	bool DetectTexturePackDest(struct zip *z, int iniIndex, Path &dest);
	void SetInstallError(std::string_view err);

	bool InstallInProgress() const { return installThread_.joinable(); }

	Path GetTempFilename() const;
	std::string GetGameID(const Path &path) const;
	std::string GetPBPGameID(FileLoader *loader) const;
	std::string GetISOGameID(FileLoader *loader) const;
	std::shared_ptr<http::Request> curDownload_;
	std::thread installThread_;
	std::atomic<bool> installDonePending_{};
	std::atomic<bool> cleanRecentsAfter_{};

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
ZipFileContents DetectZipFileContents(const Path &fileName, ZipFileInfo *info);
