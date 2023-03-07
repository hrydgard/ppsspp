#pragma once

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include <mutex>
#include <set>
#include <string>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class ZipFileReader : public VFSBackend {
public:
	ZipFileReader(const Path &zipFile, const char *inZipPath);
	~ZipFileReader();
	// use delete[] on the returned value.
	uint8_t *ReadFile(const char *path, size_t *size) override;
	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(const char *path, File::FileInfo *info) override;
	std::string toString() const override {
		return inZipPath_;
	}

private:
	void GetZipListings(const char *path, std::set<std::string> &files, std::set<std::string> &directories);

	zip *zip_file_;
	std::mutex lock_;
	char inZipPath_[256];
};
