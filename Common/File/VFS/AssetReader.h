#pragma once

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include <mutex>
#include <set>
#include <string.h>
#include <string>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class ZipAssetReader : public AssetReader {
public:
	ZipAssetReader(const char *zip_file, const char *in_zip_path);
	~ZipAssetReader();
	// use delete[] on the returned value.
	uint8_t *ReadAsset(const char *path, size_t *size) override;
	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(const char *path, File::FileInfo *info) override;
	std::string toString() const override {
		return in_zip_path_;
	}

private:
	void GetZipListings(const char *path, std::set<std::string> &files, std::set<std::string> &directories);

	zip *zip_file_;
	std::mutex lock_;
	char in_zip_path_[256];
};

class DirectoryAssetReader : public AssetReader {
public:
	explicit DirectoryAssetReader(const Path &path);
	// use delete[]
	uint8_t *ReadAsset(const char *path, size_t *size) override;
	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(const char *path, File::FileInfo *info) override;
	std::string toString() const override {
		return path_.ToString();
	}

private:
	Path path_;
};

