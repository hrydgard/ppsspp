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

class DirectoryReaderFileReference : public VFSFileReference {
public:
	Path path;
};

class DirectoryReaderOpenFile : public VFSOpenFile {
public:
	FILE *file;
};

VFSFileReference *DirectoryReader::GetFile(const char *path) {
	return nullptr;
}

void DirectoryReader::ReleaseFile(VFSFileReference *reference) {
	DirectoryReaderFileReference *file = (DirectoryReaderFileReference *)reference;
}

VFSOpenFile *DirectoryReader::OpenFileForRead(VFSFileReference *reference) {
	DirectoryReaderFileReference *file = (DirectoryReaderFileReference *)reference;
	return nullptr;
}

void DirectoryReader::Rewind(VFSOpenFile *openFile) {
	DirectoryReaderOpenFile *file = (DirectoryReaderOpenFile *)openFile;
}

size_t DirectoryReader::Read(VFSOpenFile *openFile, uint8_t *buffer, size_t length) {
	DirectoryReaderOpenFile *file = (DirectoryReaderOpenFile *)openFile;
	return 0;
}

void DirectoryReader::CloseFile(VFSOpenFile *openFile) {
	DirectoryReaderOpenFile *file = (DirectoryReaderOpenFile *)openFile;
}
