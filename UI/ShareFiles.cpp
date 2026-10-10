// Copyright (c) 2012- PPSSPP Project.

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

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include <cstdlib>

#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "UI/ShareFiles.h"

// How long a shared zip is kept for the receiving app to read.
static constexpr int64_t SHARE_KEEP_SECONDS = 60 * 60;

static Path ShareDirectory() {
	return g_Config.appCacheDirectory / "share";
}

// Deletes the folders of earlier shares (named by their time) once they're old enough.
static void CleanUpOldShares(int64_t now) {
	std::vector<File::FileInfo> folders;
	File::GetFilesInDir(ShareDirectory(), &folders);
	for (const File::FileInfo &folder : folders) {
		if (!folder.isDirectory) {
			continue;
		}
		const int64_t created = strtoll(folder.name.c_str(), nullptr, 10);
		if (created <= 0 || now - created > SHARE_KEEP_SECONDS) {
			INFO_LOG(Log::System, "Deleting old shared files in %s", folder.fullName.c_str());
			File::DeleteDirRecursively(folder.fullName);
		}
	}
}

bool CanShareFiles() {
	return System_GetPropertyBool(SYSPROP_SUPPORTS_SHARE_FILE) && !g_Config.appCacheDirectory.empty();
}

bool ShareFilesAsZip(std::string_view zipName, const std::vector<ShareZipEntry> &files) {
	if (!CanShareFiles() || files.empty()) {
		return false;
	}

	const int64_t now = (int64_t)time_now_unix_utc();
	CleanUpOldShares(now);

	std::string name = SanitizeString(zipName, StringRestriction::FileName);
	if (!endsWithNoCase(name, ".zip")) {
		name += ".zip";
	}
	// A folder per share, so a share can't overwrite a zip that an app is still reading.
	Path folder = ShareDirectory() / StringFromFormat("%lld", (long long)now);
	for (int i = 1; File::Exists(folder); i++) {
		folder = ShareDirectory() / StringFromFormat("%lld_%d", (long long)now, i);
	}
	if (!File::CreateFullPath(folder)) {
		ERROR_LOG(Log::System, "ShareFilesAsZip: Couldn't create %s", folder.c_str());
		return false;
	}
	const Path zipPath = folder / name;

	// libzip reads the buffers when the archive is closed, so they have to live until then.
	std::vector<std::string> contents(files.size());
	int error = 0;
	zip_t *zip = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
	if (!zip) {
		ERROR_LOG(Log::System, "ShareFilesAsZip: Couldn't create %s (libzip error %d)", zipPath.c_str(), error);
		File::DeleteDirRecursively(folder);
		return false;
	}
	bool success = true;
	for (size_t i = 0; i < files.size(); i++) {
		const ShareZipEntry &entry = files[i];
		const std::string entryName = entry.nameInZip.empty() ? entry.source.GetFilename() : entry.nameInZip;
		if (!File::ReadBinaryFileToString(entry.source, &contents[i])) {
			ERROR_LOG(Log::System, "ShareFilesAsZip: Couldn't read %s", entry.source.c_str());
			success = false;
			break;
		}
		zip_source_t *source = zip_source_buffer(zip, contents[i].data(), contents[i].size(), 0);
		// Fails on a name that's already in the zip, too.
		if (!source || zip_file_add(zip, entryName.c_str(), source, ZIP_FL_ENC_UTF_8) < 0) {
			ERROR_LOG(Log::System, "ShareFilesAsZip: Couldn't add %s as '%s': %s", entry.source.c_str(), entryName.c_str(), zip_strerror(zip));
			if (source) {
				zip_source_free(source);
			}
			success = false;
			break;
		}
	}
	if (!success) {
		zip_discard(zip);
		File::DeleteDirRecursively(folder);
		return false;
	}
	if (zip_close(zip) != 0) {
		ERROR_LOG(Log::System, "ShareFilesAsZip: Couldn't write %s: %s", zipPath.c_str(), zip_strerror(zip));
		zip_discard(zip);
		File::DeleteDirRecursively(folder);
		return false;
	}

	INFO_LOG(Log::System, "ShareFilesAsZip: Sharing %s, %d files", zipPath.c_str(), (int)files.size());
	System_ShareFile(zipPath, "application/zip");
	return true;
}
