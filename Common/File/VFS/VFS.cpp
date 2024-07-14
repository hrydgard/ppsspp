#include <cstring>

#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/AndroidStorage.h"
#include "Common/StringUtils.h"

VFS g_VFS;

void VFS::Register(const char *prefix, VFSBackend *reader) {
	if (reader) {
		entries_.push_back(VFSEntry{ prefix, reader });
		DEBUG_LOG(Log::IO, "Registered VFS for prefix %s: %s", prefix, reader->toString().c_str());
	} else {
		ERROR_LOG(Log::IO, "Trying to register null VFS backend for prefix %s", prefix);
	}
}

void VFS::Clear() {
	for (auto &entry : entries_) {
		delete entry.reader;
	}
	entries_.clear();
}

// TODO: Use Path more.
static bool IsLocalAbsolutePath(std::string_view path) {
	bool isUnixLocal = path[0] == '/';
#ifdef _WIN32
	bool isWindowsLocal = (isalpha(path[0]) && path[1] == ':') || startsWith(path, "\\\\") || startsWith(path, "//");
#else
	bool isWindowsLocal = false;
#endif
	bool isContentURI = Android_IsContentUri(path);
	return isUnixLocal || isWindowsLocal || isContentURI;
}

// The returned data should be free'd with delete[].
uint8_t *VFS::ReadFile(const char *filename, size_t *size) {
	if (IsLocalAbsolutePath(filename)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Reading local file.", filename);
		return File::ReadLocalFile(Path(filename), size);
	}

	int fn_len = (int)strlen(filename);
	bool fileSystemFound = false;
	for (const auto &entry : entries_) {
		int prefix_len = (int)strlen(entry.prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(filename, entry.prefix, prefix_len)) {
			fileSystemFound = true;
			// INFO_LOG(Log::IO, "Prefix match: %s (%s) -> %s", entries[i].prefix, filename, filename + prefix_len);
			uint8_t *data = entry.reader->ReadFile(filename + prefix_len, size);
			if (data)
				return data;
			else
				continue;
			// Else try the other registered file systems.
		}
	}
	if (!fileSystemFound) {
		ERROR_LOG(Log::IO, "Missing filesystem for '%s'", filename);
	}  // Otherwise, the file was just missing. No need to log.
	return nullptr;
}

bool VFS::GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Reading local directory.", path);
		File::GetFilesInDir(Path(std::string(path)), listing, filter);
		return true;
	}

	int fn_len = (int)strlen(path);
	bool fileSystemFound = false;
	for (const auto &entry : entries_) {
		int prefix_len = (int)strlen(entry.prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entry.prefix, prefix_len)) {
			fileSystemFound = true;
			if (entry.reader->GetFileListing(path + prefix_len, listing, filter)) {
				return true;
			}
		}
	}

	if (!fileSystemFound) {
		ERROR_LOG(Log::IO, "Missing filesystem for %s", path);
	}  // Otherwise, the file was just missing. No need to log.
	return false;
}

bool VFS::GetFileInfo(const char *path, File::FileInfo *info) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::GetFileInfo(Path(std::string(path)), info);
	}

	bool fileSystemFound = false;
	int fn_len = (int)strlen(path);
	for (const auto &entry : entries_) {
		int prefix_len = (int)strlen(entry.prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entry.prefix, prefix_len)) {
			fileSystemFound = true;
			if (entry.reader->GetFileInfo(path + prefix_len, info))
				return true;
			else
				continue;
		}
	}
	if (!fileSystemFound) {
		ERROR_LOG(Log::IO, "Missing filesystem for '%s'", path);
	}  // Otherwise, the file was just missing. No need to log.
	return false;
}
