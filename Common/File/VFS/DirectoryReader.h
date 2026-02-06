#pragma once

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class DirectoryReader : public VFSBackend {
public:
	explicit DirectoryReader(const Path &path);
	// use delete[] on the returned value.
	uint8_t *ReadFile(std::string_view path, size_t *size) override;

	VFSFileReference *GetFile(std::string_view path) override;
	bool GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) override;
	void ReleaseFile(VFSFileReference *vfsReference) override;

	VFSOpenFile *OpenFileForRead(VFSFileReference *vfsReference, size_t *size) override;
	void Rewind(VFSOpenFile *vfsOpenFile) override;
	size_t Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) override;
	void CloseFile(VFSOpenFile *vfsOpenFile) override;

	bool GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(std::string_view path, File::FileInfo *info) override;
	bool Exists(std::string_view path) override;
	std::string toString() const override {
		return path_.ToString();
	}

private:
	Path path_;
};
