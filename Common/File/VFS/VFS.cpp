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
		delete entry.backend;
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

bool VFS::MapPath(std::string_view path, VFSBackend **backend, std::string_view *relativePath) {
	int fn_len = (int)path.length();
	for (const auto &entry : entries_) {
		int prefix_len = (int)entry.prefix.length();
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path.data(), entry.prefix.data(), prefix_len)) {
			*backend = entry.backend;
			*relativePath = path.substr(prefix_len);
			return true;
		}
	}
	ERROR_LOG(Log::IO, "VFS: '%.*s' has an unknown filesystem prefix.", STR_VIEW(path));
	return false;
}

// The returned data should be free'd with delete[].
uint8_t *VFS::ReadFile(std::string_view filename, size_t *size) {
	if (IsLocalAbsolutePath(filename)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %.*s . Reading local file.", STR_VIEW(filename));
		return File::ReadLocalFile(Path(filename), size);
	}

	VFSBackend *backend = nullptr;
	std::string_view relativePath;
	if (!MapPath(filename, &backend, &relativePath)) {
		return nullptr;
	}

	return backend->ReadFile(relativePath, size);
}

bool VFS::GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Reading local directory.", path);
		File::GetFilesInDir(Path(path), listing, filter);
		return true;
	}

	VFSBackend *backend = nullptr;
	std::string_view relativePath;
	if (!MapPath(path, &backend, &relativePath)) {
		return false;
	}

	return backend->GetFileListing(relativePath, listing, filter);
}

bool VFS::GetFileInfo(std::string_view path, File::FileInfo *info) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::GetFileInfo(Path(path), info);
	}

	VFSBackend *backend = nullptr;
	std::string_view relativePath;
	if (!MapPath(path, &backend, &relativePath)) {
		return false;
	}

	return backend->GetFileInfo(relativePath, info);
}

bool VFS::Exists(std::string_view path) {
	if (IsLocalAbsolutePath(path)) {
		// Local path, not VFS.
		// INFO_LOG(Log::IO, "Not a VFS path: %s . Getting local file info.", path);
		return File::Exists(Path(path));
	}

	VFSBackend *backend = nullptr;
	std::string_view relativePath;
	if (!MapPath(path, &backend, &relativePath)) {
		return false;
	}

	return backend->Exists(relativePath);
}
