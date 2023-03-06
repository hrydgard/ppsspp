#pragma once

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class DirectoryReader : public VFSBackend {
public:
	explicit DirectoryReader(const Path &path);
	// use delete[] on the returned value.
	uint8_t *ReadFile(const char *path, size_t *size) override;

	virtual VFSFileReference *GetFile(const char *path) override;
	virtual void ReleaseFile(VFSFileReference *reference) override;

	virtual VFSOpenFile *OpenFileForRead(VFSFileReference *reference) override;
	virtual void Rewind(VFSOpenFile *openFile) override;
	virtual size_t Read(VFSOpenFile *openFile, uint8_t *buffer, size_t length) override;
	virtual void CloseFile(VFSOpenFile *openFile) override;

	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(const char *path, File::FileInfo *info) override;
	std::string toString() const override {
		return path_.ToString();
	}

private:
	Path path_;
};
