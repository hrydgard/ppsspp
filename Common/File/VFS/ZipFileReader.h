#pragma once

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

class ZipContainer {
public:
	ZipContainer() noexcept;
	ZipContainer(const Path &path);
	~ZipContainer();
	ZipContainer(const ZipContainer &) = delete;
	ZipContainer(ZipContainer &&) noexcept;
	ZipContainer &operator=(const ZipContainer &) = delete;
	ZipContainer &operator=(ZipContainer &&) noexcept;
	void close() noexcept;
	operator zip_t *() const noexcept;

private:
	struct SourceData {
		Path path;
		FILE *file;
	} *sourceData_;
	zip_t *zip_;

	static zip_int64_t SourceCallback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd);
};

class ZipFileReader : public VFSBackend {
public:
	static ZipFileReader *Create(const Path &zipFile, std::string_view inZipPath, bool logErrors = true);
	~ZipFileReader();

	bool IsValid() const { return zip_file_ != nullptr; }

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
	std::string toString() const override {
		std::string retval = zipPath_.ToVisualString();
		if (!inZipPath_.empty()) {
			retval += ": ";
			retval += inZipPath_;
		}
		return retval;
	}

private:
	ZipFileReader(ZipContainer &&zip, const Path &zipPath, const std::string &inZipPath) : zip_file_(std::move(zip)), zipPath_(zipPath), inZipPath_(inZipPath) {}
	// Path has to be either an empty string, or a string ending with a /.
	bool GetZipListings(const std::string &path, std::set<std::string> &files, std::set<std::string> &directories);

	ZipContainer zip_file_;
	std::mutex lock_;
	std::string inZipPath_;
	Path zipPath_;
};

// When you just want a single file from a ZIP, and don't care about accurate error reporting, use this.
// The buffer should be free'd with free. Mutex will be locked while updating data, if non-null.
bool ReadSingleFileFromZip(Path zipFile, const char *path, std::string *data, std::mutex *mutex);
