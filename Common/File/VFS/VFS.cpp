#include <cstring>
#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/AndroidStorage.h"
#include "Common/StringUtils.h"

VFS g_VFS;

void VFS::Register(std::string_view prefix, VFSBackend *reader) {
	if (reader) {
		entries_.push_back(VFSEntry{ prefix, reader });
		DEBUG_LOG(Log::IO, "Registered VFS for prefix %.*s: %s", STR_VIEW(prefix), reader->toString().c_str());
	} else {
		ERROR_LOG(Log::IO, "Trying to register null VFS backend for prefix %.*s", STR_VIEW(prefix));
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

template<class RetType, class Func>
inline RetType RouteVFSOperation(const std::vector<VFSEntry> &entries, std::string_view filename, Func func) {
	const int fn_len = (int)filename.length();
	bool fileSystemFound = false;
	for (const auto &entry : entries) {
		int prefix_len = (int)entry.prefix.length();
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(filename.data(), entry.prefix.data(), prefix_len)) {
			fileSystemFound = true;
			RetType result = func(entry.reader, filename.substr(prefix_len));
			if (result)
				return result;
			else
				continue;
			// INFO_LOG(Log::IO, "Prefix match: %s (%s) -> %s", entries[i].prefix, filename, filename + prefix_len);
			// Else try the other registered file systems.
		}
	}
	if (!fileSystemFound) {
		ERROR_LOG(Log::IO, "Missing filesystem for '%.*s'", STR_VIEW(filename));
	}  // Otherwise, the file was just missing. No need to log.
	return RetType();
}

// The returned data should be free'd with delete[].
uint8_t *VFS::ReadFile(std::string_view filename, size_t *size) {
	if (IsLocalAbsolutePath(filename)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %.*s . Reading local file.", STR_VIEW(filename));
		return File::ReadLocalFile(Path(filename), size);
	}
	return RouteVFSOperation<uint8_t *>(entries_, filename, [size](VFSBackend *reader, std::string_view path) {
		return reader->ReadFile(path, size);
	});
}

bool VFS::GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Reading local directory.", path);
		File::GetFilesInDir(Path(path), listing, filter);
		return true;
	}
	return RouteVFSOperation<bool>(entries_, path, [listing, filter](VFSBackend *reader, std::string_view path) {
		return reader->GetFileListing(path, listing, filter);
	});
}

bool VFS::GetFileInfo(std::string_view path, File::FileInfo *info) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::GetFileInfo(Path(path), info);
	}
	return RouteVFSOperation<bool>(entries_, path, [info](VFSBackend *reader, std::string_view path) {
		return reader->GetFileInfo(path, info);
	});
}

bool VFS::Exists(std::string_view path) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::Exists(Path(path));
	}
	return RouteVFSOperation<bool>(entries_, path, [](VFSBackend *reader, std::string_view path) {
		return reader->Exists(path);
	});
}
