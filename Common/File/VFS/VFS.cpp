#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/AssetReader.h"
#include "Common/File/AndroidStorage.h"

VFS g_VFS;

void VFS::Register(const char *prefix, AssetReader *reader) {
	entries_[numEntries_].prefix = prefix;
	entries_[numEntries_].reader = reader;
	DEBUG_LOG(IO, "Registered VFS for prefix %s: %s", prefix, reader->toString().c_str());
	numEntries_++;
}

void VFS::Clear() {
	for (int i = 0; i < numEntries_; i++) {
		delete entries_[i].reader;
	}
	numEntries_ = 0;
}

// TODO: Use Path more.
static bool IsLocalAbsolutePath(const char *path) {
	bool isUnixLocal = path[0] == '/';
#ifdef _WIN32
	bool isWindowsLocal = isalpha(path[0]) && path[1] == ':';
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
		// INFO_LOG(IO, "Not a VFS path: %s . Reading local file.", filename);
		return File::ReadLocalFile(Path(filename), size);
	}

	int fn_len = (int)strlen(filename);
	bool fileSystemFound = false;
	for (int i = 0; i < numEntries_; i++) {
		int prefix_len = (int)strlen(entries_[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(filename, entries_[i].prefix, prefix_len)) {
			fileSystemFound = true;
			// INFO_LOG(IO, "Prefix match: %s (%s) -> %s", entries[i].prefix, filename, filename + prefix_len);
			uint8_t *data = entries_[i].reader->ReadAsset(filename + prefix_len, size);
			if (data)
				return data;
			else
				continue;
			// Else try the other registered file systems.
		}
	}
	if (!fileSystemFound) {
		ERROR_LOG(IO, "Missing filesystem for '%s'", filename);
	}  // Otherwise, the file was just missing. No need to log.
	return 0;
}

bool VFS::GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(IO, "Not a VFS path: %s . Reading local directory.", path);
		File::GetFilesInDir(Path(std::string(path)), listing, filter);
		return true;
	}

	int fn_len = (int)strlen(path);
	bool fileSystemFound = false;
	for (int i = 0; i < numEntries_; i++) {
		int prefix_len = (int)strlen(entries_[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entries_[i].prefix, prefix_len)) {
			fileSystemFound = true;
			if (entries_[i].reader->GetFileListing(path + prefix_len, listing, filter)) {
				return true;
			}
		}
	}

	if (!fileSystemFound) {
		ERROR_LOG(IO, "Missing filesystem for %s", path);
	}  // Otherwise, the file was just missing. No need to log.
	return false;
}

bool VFS::GetFileInfo(const char *path, File::FileInfo *info) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::GetFileInfo(Path(std::string(path)), info);
	}

	bool fileSystemFound = false;
	int fn_len = (int)strlen(path);
	for (int i = 0; i < numEntries_; i++) {
		int prefix_len = (int)strlen(entries_[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entries_[i].prefix, prefix_len)) {
			fileSystemFound = true;
			if (entries_[i].reader->GetFileInfo(path + prefix_len, info))
				return true;
			else
				continue;
		}
	}
	if (!fileSystemFound) {
		ERROR_LOG(IO, "Missing filesystem for '%s'", path);
	}  // Otherwise, the file was just missing. No need to log.
	return false;
}
