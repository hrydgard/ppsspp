#include <cstdio>

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/VFS/DirectoryReader.h"

DirectoryReader::DirectoryReader(const Path &path) {
	path_ = path;
}

uint8_t *DirectoryReader::ReadFile(std::string_view path, size_t *size) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::ReadLocalFile(new_path, size);
}

bool DirectoryReader::GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) {
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

bool DirectoryReader::GetFileInfo(std::string_view path, File::FileInfo *info) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::GetFileInfo(new_path, info);
}

bool DirectoryReader::Exists(std::string_view path) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::Exists(new_path);
}

class DirectoryReaderFileReference : public VFSFileReference {
public:
	Path path;
};

class DirectoryReaderOpenFile : public VFSOpenFile {
public:
	~DirectoryReaderOpenFile() {
		_dbg_assert_(file == nullptr);
	}
	FILE *file = nullptr;
};

VFSFileReference *DirectoryReader::GetFile(std::string_view path) {
	Path filePath = path_ / path;
	if (!File::Exists(filePath)) {
		return nullptr;
	}

	DirectoryReaderFileReference *reference = new DirectoryReaderFileReference();
	reference->path = filePath;
	return reference;
}

bool DirectoryReader::GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	return File::GetFileInfo(reference->path, fileInfo);
}

void DirectoryReader::ReleaseFile(VFSFileReference *vfsReference) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	delete reference;
}

VFSOpenFile *DirectoryReader::OpenFileForRead(VFSFileReference *vfsReference, size_t *size) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	FILE *file = File::OpenCFile(reference->path, "rb");
	if (!file) {
		return nullptr;
	}
	File::Fseek(file, 0, SEEK_END);
	*size = File::Ftell(file);
	File::Fseek(file, 0, SEEK_SET);
	DirectoryReaderOpenFile *openFile = new DirectoryReaderOpenFile();
	openFile->file = file;
	return openFile;
}

void DirectoryReader::Rewind(VFSOpenFile *vfsOpenFile) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	File::Fseek(openFile->file, 0, SEEK_SET);
}

size_t DirectoryReader::Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	return fread(buffer, 1, length, openFile->file);
}

void DirectoryReader::CloseFile(VFSOpenFile *vfsOpenFile) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	_dbg_assert_(openFile);
	if (!openFile) {
		return;
	}
	_dbg_assert_(openFile->file != nullptr);
	if (openFile->file) {
		fclose(openFile->file);
		openFile->file = nullptr;
	}
	delete openFile;
}
