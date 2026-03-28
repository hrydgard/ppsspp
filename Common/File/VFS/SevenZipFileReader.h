#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonWindows.h"

#include "ext/lzma-sdk/7z.h"
#include "ext/lzma-sdk/7zFile.h"

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class SevenZipFileReader : public VFSBackend {
public:
	static SevenZipFileReader *Create(const Path &archivePath, std::string_view inArchivePath, bool logErrors = true);
	~SevenZipFileReader();

	bool IsValid() const { return valid_; }

	// Use delete[] on the returned value.
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
	std::string toString() const override {
		std::string retval = archivePath_.ToVisualString();
		if (!inArchivePath_.empty()) {
			retval += ": ";
			retval += inArchivePath_;
		}
		return retval;
	}

private:
	struct SevenZipEntry {
		std::string path;
		bool isDirectory = false;
		uint64_t size = 0;
	};

	SevenZipFileReader(const Path &archivePath, const std::string &inArchivePath);
	bool OpenArchive(bool logErrors);
	void CloseArchive();
	bool BuildEntryCache();
	bool FindEntry(std::string_view path, UInt32 *index, bool *isDirectory = nullptr) const;
	std::string ResolvePath(std::string_view path) const;
	std::string ReadEntryPath(UInt32 index) const;
	uint8_t *ExtractFile(UInt32 fileIndex, size_t *size);

	static void *Alloc(ISzAllocPtr p, size_t size);
	static void Free(ISzAllocPtr p, void *address);

	Path archivePath_;
	std::string inArchivePath_;

	mutable std::mutex lock_;
	CFileInStream archiveStream_;
	CLookToRead2 lookStream_;
	ISzAlloc allocImp_;
	ISzAlloc allocTempImp_;
	CSzArEx db_;
	Byte *lookStreamBuf_ = nullptr;
	Byte *cachedBlock_ = nullptr;
	size_t cachedBlockSize_ = 0;
	UInt32 blockIndex_ = 0xFFFFFFFF;
	bool valid_ = false;

	std::vector<SevenZipEntry> entries_;
};
