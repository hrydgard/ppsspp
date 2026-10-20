#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class DirectoryReader : public VFSBackend {
public:
	// With ignoreCase, GetFile and ReadFile also find files whose names only differ in case, like the
	// zip reader does. Meant for directories that people fill by hand on Windows, like texture packs,
	// which then break on case sensitive file systems.
	explicit DirectoryReader(const Path &path, bool ignoreCase = false);
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
	// Returns the path (relative to path_) of a file whose name matches path except for case.
	bool FindIgnoringCase(std::string_view path, std::string *actualPath);
	void IndexDirectory(const Path &dir, const std::string &prefix);

	Path path_;
	bool ignoreCase_ = false;

	// Lowercase relative path -> actual relative path of every file, built on the first miss.
	std::mutex indexLock_;
	bool indexBuilt_ = false;
	std::unordered_map<std::string, std::string> lowerIndex_;
	int caseFixCount_ = 0;
};
