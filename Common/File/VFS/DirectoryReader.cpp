#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/VFS/DirectoryReader.h"

DirectoryReader::DirectoryReader(const Path &path) {
	path_ = path;
}

uint8_t *DirectoryReader::ReadFile(const char *path, size_t *size) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::ReadLocalFile(new_path, size);
}

bool DirectoryReader::GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;

	File::FileInfo info;
	if (!File::GetFileInfo(new_path, &info))
		return false;

	if (info.isDirectory) {
		File::GetFilesInDir(new_path, listing, filter);
		return true;
	}
	return false;
}

bool DirectoryReader::GetFileInfo(const char *path, File::FileInfo *info) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::GetFileInfo(new_path, info);
}
