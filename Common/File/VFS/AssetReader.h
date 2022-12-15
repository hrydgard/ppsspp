// TODO: Move much of this code to vfs.cpp
#pragma once

#ifdef __ANDROID__
#include <zip.h>
#endif

#include <string.h>
#include <string>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class AssetReader {
public:
	virtual ~AssetReader() {}
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size) = 0;
	// Filter support is optional but nice to have
	virtual bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter = 0) = 0;
	virtual bool GetFileInfo(const char *path, File::FileInfo *info) = 0;
	virtual std::string toString() const = 0;
};

#ifdef __ANDROID__
uint8_t *ReadFromZip(zip *archive, const char* filename, size_t *size);
class ZipAssetReader : public AssetReader {
public:
	ZipAssetReader(const char *zip_file, const char *in_zip_path);
	~ZipAssetReader();
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size);
	virtual bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter);
	virtual bool GetFileInfo(const char *path, File::FileInfo *info);
	virtual std::string toString() const {
		return in_zip_path_;
	}

private:
	zip *zip_file_;
	char in_zip_path_[256];
};
#endif

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

