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
	static ZipFileReader *Create(const Path &zipFile, const char *inZipPath, bool logErrors = true);
	~ZipFileReader();

	bool IsValid() const { return zip_file_ != nullptr; }

	// use delete[] on the returned value.
	uint8_t *ReadFile(const char *path, size_t *size) override;

	VFSFileReference *GetFile(const char *path) override;
	bool GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) override;
	void ReleaseFile(VFSFileReference *vfsReference) override;

	VFSOpenFile *OpenFileForRead(VFSFileReference *vfsReference, size_t *size) override;
	void Rewind(VFSOpenFile *vfsOpenFile) override;
	size_t Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) override;
	void CloseFile(VFSOpenFile *vfsOpenFile) override;

	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter) override;
	bool GetFileInfo(const char *path, File::FileInfo *info) override;
	std::string toString() const override {
		std::string retval = zipPath_.ToVisualString();
		if (!inZipPath_.empty()) {
			retval += ": ";
			retval += inZipPath_;
		}
		return retval;
	}

private:
	ZipFileReader(zip *zip_file, const Path &zipPath, const std::string &inZipPath) : zip_file_(zip_file), zipPath_(zipPath), inZipPath_(inZipPath) {}
	// Path has to be either an empty string, or a string ending with a /.
	bool GetZipListings(const std::string &path, std::set<std::string> &files, std::set<std::string> &directories);

	zip *zip_file_ = nullptr;
	std::mutex lock_;
	std::string inZipPath_;
	Path zipPath_;
};

// When you just want a single file from a ZIP, and don't care about accurate error reporting, use this.
// The buffer should be free'd with free. Mutex will be locked while updating data, if non-null.
bool ReadSingleFileFromZip(Path zipFile, const char *path, std::string *data, std::mutex *mutex);
