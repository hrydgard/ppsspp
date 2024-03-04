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

#include "ppsspp_config.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <set>
#include <sstream>
#include <thread>

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif
#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Log.h"
#include "Common/System/OSD.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/ELF/PBPReader.h"
#include "Core/System.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/Util/GameManager.h"
#include "Common/Data/Text/I18n.h"

GameManager g_GameManager;

static struct zip *ZipOpenPath(Path fileName) {
	int error = 0;
	// Need to special case for content URI here, similar to OpenCFile.
	struct zip *z;
#if PPSSPP_PLATFORM(ANDROID)
	if (fileName.Type() == PathType::CONTENT_URI) {
		int fd = File::OpenFD(fileName, File::OPEN_READ);
		z = zip_fdopen(fd, 0, &error);
	} else
#endif
	{  // continuation of above else in the ifdef
		z = zip_open(fileName.c_str(), 0, &error);
	}

	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file '%s', error code=%i", fileName.c_str(), error);
	}
	return z;
}

GameManager::GameManager() {
}

Path GameManager::GetTempFilename() const {
#ifdef _WIN32
	wchar_t tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	wchar_t buffer[MAX_PATH];
	GetTempFileName(tempPath, L"PSP", 1, buffer);
	return Path(buffer);
#else
	return g_Config.memStickDirectory / "ppsspp.dl";
#endif
}

bool GameManager::IsGameInstalled(const std::string &name) {
	Path pspGame = GetSysDirectory(DIRECTORY_GAME);
	return File::Exists(pspGame / name);
}

bool GameManager::DownloadAndInstall(const std::string &storeFileUrl) {
	if (curDownload_.get() != nullptr) {
		ERROR_LOG(HLE, "Can only process one download at a time");
		return false;
	}
	if (InstallInProgress()) {
		ERROR_LOG(HLE, "Can't download when an install is in progress (yet)");
		return false;
	}

	Path filename = GetTempFilename();
	const char *acceptMime = "application/zip, application/x-cso, application/x-iso9660-image, application/octet-stream; q=0.9, */*; q=0.8";
	curDownload_ = g_DownloadManager.StartDownload(storeFileUrl, filename, http::ProgressBarMode::VISIBLE, acceptMime);
	return true;
}

bool GameManager::IsDownloading(const std::string &storeZipUrl) {
	if (curDownload_)
		return curDownload_->url() == storeZipUrl;
	return false;
}

bool GameManager::CancelDownload() {
	if (!curDownload_)
		return false;

	curDownload_->Cancel();
	curDownload_.reset();
	return true;
}

float GameManager::DownloadSpeedKBps() {
	if (curDownload_)
		return curDownload_->SpeedKBps();
	return 0.0f;
}

void GameManager::UninstallGame(const std::string &name) {
	SetCurrentThreadName("UninstallGame");

	AndroidJNIThreadContext context;  // Destructor detaches.

	Path gameDir = GetSysDirectory(DIRECTORY_GAME) / name;
	auto st = GetI18NCategory(I18NCat::STORE);

	INFO_LOG(HLE, "Uninstalling '%s'", gameDir.c_str());
	if (!File::Exists(gameDir)) {
		ERROR_LOG(HLE, "Game '%s' not installed, cannot uninstall", name.c_str());
		return;
	}
	g_OSD.SetProgressBar("install", st->T("Uninstall"), 0.0f, 0.0f, 0.0f, 0.1f);
	bool success = File::DeleteDirRecursively(gameDir);
	g_OSD.RemoveProgressBar("install", success, 0.5f);
	if (success) {
		INFO_LOG(HLE, "Successfully uninstalled game '%s'", name.c_str());
		InstallDone();
		cleanRecentsAfter_ = true;
		return;
	} else {
		ERROR_LOG(HLE, "Failed to uninstalled game '%s'", name.c_str());
		InstallDone();
		return;
	}
}

void GameManager::Update() {
	if (curDownload_.get() && curDownload_->Done()) {
		INFO_LOG(HLE, "Download completed! Status = %d", curDownload_->ResultCode());
		Path fileName = curDownload_->outfile();
		if (curDownload_->ResultCode() == 200) {
			if (!File::Exists(fileName)) {
				ERROR_LOG(HLE, "Downloaded file '%s' does not exist :(", fileName.c_str());
				curDownload_.reset();
				return;
			}
			// Game downloaded to temporary file - install it!
			InstallGameOnThread(Path(curDownload_->url()), fileName, true);
		} else {
			ERROR_LOG(HLE, "Expected HTTP status code 200, got status code %d. Install cancelled, deleting partial file '%s'",
				curDownload_->ResultCode(), fileName.c_str());
			File::Delete(fileName);
		}
		curDownload_.reset();
	}

	if (installDonePending_.exchange(false)) {
		if (installThread_.joinable()) {
			installThread_.join();
		}
		if (cleanRecentsAfter_.exchange(false)) {
			g_Config.CleanRecent();
		}
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

ZipFileContents DetectZipFileContents(const Path &fileName, ZipFileInfo *info) {
	struct zip *z = ZipOpenPath(fileName);
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

// Parameters need to be by value, since this is a thread func.
bool GameManager::InstallGame(const Path &url, const Path &fileName, bool deleteAfter) {
	SetCurrentThreadName("InstallGame");

	if (installDonePending_) {
		ERROR_LOG(HLE, "Cannot have two installs in progress at the same time");
		return false;
	}

	AndroidJNIThreadContext context;  // Destructor detaches.
	if (!File::Exists(fileName)) {
		ERROR_LOG(HLE, "Game file '%s' doesn't exist", fileName.c_str());
		return false;
	}

	auto st = GetI18NCategory(I18NCat::STORE);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	std::string extension = url.GetFileExtension();
	// Examine the URL to guess out what we're installing.
	if (extension == ".cso" || extension == ".iso") {
		// It's a raw ISO or CSO file. We just copy it to the destination.
		std::string shortFilename = url.GetFilename();
		bool success = InstallRawISO(fileName, shortFilename, deleteAfter);
		return success;
	}

	Path pspGame = GetSysDirectory(DIRECTORY_GAME);
	Path dest = pspGame;
	int error = 0;

	struct zip *z = ZipOpenPath(fileName);
	if (!z) {
		g_OSD.RemoveProgressBar("install", false, 0.5f);
		SetInstallError(sy->T("Unable to open zip file"));
		return false;
	}

	ZipFileInfo info;
	ZipFileContents contents = DetectZipFileContents(z, &info);
	bool success = false;
	switch (contents) {
	case ZipFileContents::PSP_GAME_DIR:
		INFO_LOG(HLE, "Installing '%s' into '%s'", fileName.c_str(), pspGame.c_str());
		// InstallMemstickGame contains code to close (and delete) z.
		success = InstallMemstickGame(z, fileName, pspGame, info, false, deleteAfter);
		break;
	case ZipFileContents::ISO_FILE:
		INFO_LOG(HLE, "Installing '%s' into its containing directory", fileName.c_str());
		// InstallZippedISO contains code to close z.
		success = InstallZippedISO(z, info.isoFileIndex, fileName, deleteAfter);
		break;
	case ZipFileContents::TEXTURE_PACK:
		// InstallMemstickGame contains code to close z, and works for textures too.
		if (DetectTexturePackDest(z, info.textureIniIndex, dest)) {
			INFO_LOG(HLE, "Installing texture pack '%s' into '%s'", fileName.c_str(), dest.c_str());
			File::CreateFullPath(dest);
			// Install as a zip file if textures.ini is in the root. Performs better on Android.
			if (info.stripChars == 0) {
				success = InstallMemstickZip(z, fileName, dest / "textures.zip", info, deleteAfter);
			} else {
				// TODO: Can probably remove this, as we now put .nomedia in /TEXTURES directly.
				File::CreateEmptyFile(dest / ".nomedia");
				success = InstallMemstickGame(z, fileName, dest, info, true, deleteAfter);
			}
		} else {
			zip_close(z);
			z = nullptr;
		}
		break;
	default:
		ERROR_LOG(HLE, "File not a PSP game, no EBOOT.PBP found.");
		SetInstallError(sy->T("Not a PSP game"));
		zip_close(z);
		z = nullptr;
		if (deleteAfter)
			File::Delete(fileName);
		break;
	}
	g_OSD.RemoveProgressBar("install", success, 0.5f);
	return success;
}

bool GameManager::DetectTexturePackDest(struct zip *z, int iniIndex, Path &dest) {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	struct zip_stat zstat;
	zip_stat_index(z, iniIndex, 0, &zstat);

	if (zstat.size >= 32 * 1024 * 1024) {
		SetInstallError(iz->T("Texture pack doesn't support install"));
		return false;
	}

	std::string buffer;
	buffer.resize(zstat.size);
	zip_file *zf = zip_fopen_index(z, iniIndex, 0);
	if (zip_fread(zf, &buffer[0], buffer.size()) != (zip_int64_t)zstat.size) {
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
		for (const std::string &path : g_Config.RecentIsos()) {
			std::string recentID = GetGameID(Path(path));
			if (games.find(recentID) != games.end()) {
				gameID = recentID;
				break;
			}
		}
	}

	Path pspTextures = GetSysDirectory(DIRECTORY_TEXTURES);
	dest = pspTextures / gameID;
	return true;
}

void GameManager::SetInstallError(std::string_view err) {
	installProgress_ = 0.0f;
	installError_ = err;
	InstallDone();
}

std::string GameManager::GetGameID(const Path &path) const {
	auto loader = ConstructFileLoader(path);
	std::string id;

	std::string errorString;
	switch (Identify_File(loader, &errorString)) {
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
	int handle = -1;
	if (info.exists) {
		handle = umd.OpenFile("/PSP_GAME/PARAM.SFO", FILEACCESS_READ);
	}
	if (handle < 0) {
		return "";
	}

	std::string sfoData;
	sfoData.resize(info.size);
	umd.ReadFile(handle, (u8 *)&sfoData[0], info.size);
	umd.CloseFile(handle);

	ParamSFOData sfo;
	sfo.ReadSFO((const u8 *)sfoData.data(), sfoData.size());
	return sfo.GetValueString("DISC_ID");
}

bool GameManager::ExtractFile(struct zip *z, int file_index, const Path &outFilename, size_t *bytesCopied, size_t allBytes) {
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
			zip_int64_t retval = zip_fread(zf, buffer, readSize);
			if (retval < 0 || (size_t)retval < readSize) {
				ERROR_LOG(HLE, "Failed to read %d bytes from zip (%d) - archive corrupt?", (int)readSize, (int)retval);
				delete[] buffer;
				fclose(f);
				zip_fclose(zf);
				File::Delete(outFilename);
				return false;
			}
			size_t written = fwrite(buffer, 1, readSize, f);
			if (written != readSize) {
				ERROR_LOG(HLE, "Wrote %d bytes out of %d - Disk full?", (int)written, (int)readSize);
				delete[] buffer;
				fclose(f);
				zip_fclose(zf);
				File::Delete(outFilename);
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
		auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
		g_OSD.Show(OSDType::MESSAGE_ERROR, iz->T("Installation failed"));
		ERROR_LOG(HLE, "Failed to open file for writing: %s", outFilename.c_str());
		return false;
	}
}

bool GameManager::InstallMemstickGame(struct zip *z, const Path &zipfile, const Path &dest, const ZipFileInfo &info, bool allowRoot, bool deleteAfter) {
	size_t allBytes = 0;
	size_t bytesCopied = 0;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

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

	auto di = GetI18NCategory(I18NCat::DIALOG);

	// Create all the directories first in one pass
	std::set<Path> createdDirs;
	for (int i = 0; i < info.numFiles; i++) {
		// Let's count the directories as the first 10%.
		const char *fn = zip_get_name(z, i, 0);
		std::string zippedName = fn;
		if (zippedName.length() < (size_t)info.stripChars) {
			continue;
		}
		Path outFilename = dest / zippedName.substr(info.stripChars);

		bool isDir = zippedName.empty() || zippedName.back() == '/';
		if (!isDir && zippedName.find("/") != std::string::npos) {
			outFilename = dest / zippedName.substr(0, zippedName.rfind('/'));
		} else if (!isDir) {
			outFilename = dest;
		}

		Path outPath(outFilename);
		if (createdDirs.find(outPath) == createdDirs.end()) {
			File::CreateFullPath(outPath);
			createdDirs.insert(outPath);
		}
		if (!isDir && fileAllowed(fn)) {
			struct zip_stat zstat;
			if (zip_stat_index(z, i, 0, &zstat) >= 0) {
				allBytes += zstat.size;
			}
		}
		g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, info.numFiles, (i + 1) * 0.1f, 0.1f);
	}

	// Now, loop through again in a second pass, writing files.
	std::vector<Path> createdFiles;
	for (int i = 0; i < info.numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		// Note that we do NOT write files that are not in a directory, to avoid random
		// README files etc. (unless allowRoot is true.)
		if (fileAllowed(fn) && strlen(fn) > (size_t)info.stripChars) {
			std::string zippedName = fn;
			fn += info.stripChars;
			Path outFilename = dest / fn;
			bool isDir = zippedName.empty() || zippedName.back() == '/';
			if (isDir)
				continue;

			if (!ExtractFile(z, i, outFilename, &bytesCopied, allBytes)) {
				ERROR_LOG(HLE, "Bailing: Failed to extract file: %s -> %s", zippedName.c_str(), outFilename.c_str());
				goto bail;
			} else {
				createdFiles.push_back(outFilename);
			}
		}
		g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, 1.0f, 0.1f + (i + 1) / (float)info.numFiles * 0.9f, 0.1f);
	}

	INFO_LOG(HLE, "Unzipped %d files (%d bytes / %d).", info.numFiles, (int)bytesCopied, (int)allBytes);
	zip_close(z);
	z = nullptr;
	installProgress_ = 1.0f;
	if (deleteAfter) {
		INFO_LOG(HLE, "Deleting '%s' after extraction", zipfile.c_str());
		File::Delete(zipfile);
	}
	InstallDone();
	ResetInstallError();
	g_OSD.RemoveProgressBar("install", true, 0.5f);
	return true;

bail:
	// We end up here if disk is full or couldn't write to storage for some other reason.
	zip_close(z);
	// We don't delete the original in this case. Try to delete the files we created so far.
	for (size_t i = 0; i < createdFiles.size(); i++) {
		File::Delete(createdFiles[i]);
	}
	for (auto const &iter : createdDirs) {
		File::DeleteDir(iter);
	}
	SetInstallError(sy->T("Storage full"));
	g_OSD.RemoveProgressBar("install", false, 0.5f);
	return false;
}

bool GameManager::InstallMemstickZip(struct zip *z, const Path &zipfile, const Path &dest, const ZipFileInfo &info, bool deleteAfter) {
	size_t allBytes = 0;
	size_t bytesCopied = 0;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	// We don't need the zip anymore, as we're going to copy it as-is.
	zip_close(z);
	z = nullptr;

	// Not using File::Copy() so we can report progress.
	FILE *inf = File::OpenCFile(zipfile, "rb");
	if (!inf)
		return false;

	allBytes = (size_t)File::GetFileSize(inf);
	FILE *outf = File::OpenCFile(dest, "wb");
	if (!outf) {
		SetInstallError(sy->T("Storage full"));
		fclose(inf);
		return false;
	}

	auto di = GetI18NCategory(I18NCat::DIALOG);

	const size_t blockSize = 1024 * 128;
	u8 *buffer = new u8[blockSize];
	while (bytesCopied < allBytes) {
		size_t readSize = std::min(blockSize, allBytes - bytesCopied);
		if (fread(buffer, readSize, 1, inf) != 1)
			break;
		if (fwrite(buffer, readSize, 1, outf) != 1)
			break;
		bytesCopied += readSize;
		installProgress_ = (float)bytesCopied / (float)allBytes;
		g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, 1.0f, installProgress_, 0.1f);
	}

	delete[] buffer;
	fclose(inf);
	fclose(outf);

	if (bytesCopied < allBytes) {
		File::Delete(dest);
		g_OSD.RemoveProgressBar("install", false, 0.5f);
		SetInstallError(sy->T("Storage full"));
		return false;
	}

	installProgress_ = 1.0f;
	if (deleteAfter) {
		File::Delete(zipfile);
	}
	InstallDone();
	ResetInstallError();
	g_OSD.RemoveProgressBar("install", true, 0.5f);
	return true;
}

bool GameManager::InstallZippedISO(struct zip *z, int isoFileIndex, const Path &zipfile, bool deleteAfter) {
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

	Path outputISOFilename = Path(g_Config.currentDirectory) / fn.substr(nameOffset);
	size_t bytesCopied = 0;
	bool success = false;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, 0.0f, 0.0f, 0.1f);
	if (ExtractFile(z, isoFileIndex, outputISOFilename, &bytesCopied, allBytes)) {
		INFO_LOG(IO, "Successfully unzipped ISO file to '%s'", outputISOFilename.c_str());
		success = true;
	}
	zip_close(z);
	if (success && deleteAfter) {
		File::Delete(zipfile);
		g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, 0.0f, 0.0f, 0.1f);
	}
	g_OSD.RemoveProgressBar("install", success, 0.5f);

	z = 0;
	installProgress_ = 1.0f;
	InstallDone();
	ResetInstallError();
	return true;
}

bool GameManager::InstallGameOnThread(const Path &url, const Path &fileName, bool deleteAfter) {
	if (InstallInProgress() || installDonePending_) {
		return false;
	}
	installThread_ = std::thread(std::bind(&GameManager::InstallGame, this, url, fileName, deleteAfter));
	return true;
}

bool GameManager::UninstallGameOnThread(const std::string &name) {
	if (name.empty()) {
		ERROR_LOG(HLE, "Cannot uninstall an empty-named game");
		return false;
	}
	if (InstallInProgress() || installDonePending_ || curDownload_.get() != nullptr) {
		return false;
	}
	installThread_ = std::thread(std::bind(&GameManager::UninstallGame, this, name));
	return true;
}

bool GameManager::InstallRawISO(const Path &file, const std::string &originalName, bool deleteAfter) {
	Path destPath = Path(g_Config.currentDirectory) / originalName;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	g_OSD.SetProgressBar("install", di->T("Installing..."), 0.0f, 0.0f, 0.0f, 0.1f);
	// TODO: To save disk space, we should probably attempt a move first.
	if (File::Copy(file, destPath)) {
		if (deleteAfter) {
			File::Delete(file);
		}
		g_OSD.RemoveProgressBar("install", true, 0.5f);
	} else {
		g_OSD.RemoveProgressBar("install", false, 0.5f);
	}
	installProgress_ = 1.0f;
	InstallDone();
	ResetInstallError();
	return true;
}

void GameManager::ResetInstallError() {
	if (!InstallInProgress()) {
		installError_.clear();
	}
}

void GameManager::InstallDone() {
	installDonePending_ = true;
}
