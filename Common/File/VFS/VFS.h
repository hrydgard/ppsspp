#pragma once

#include <vector>
#include <cstdint>
#include <string_view>

#include "Common/File/DirListing.h"

// Basic read-only virtual file system. Used to manage assets on Android, where we have to
// read them manually out of the APK zipfile, while being able to run on other
// platforms as well with the appropriate directory set-up.

// Note that this is kinda similar in concept to Core/MetaFileSystem.h, but that one
// is specifically for operations done by the emulated PSP, while this is for operations
// on the system level, like loading assets, and maybe texture packs. Also, as mentioned,
// this one is read-only, so a bit smaller and simpler.

// VFSBackend instances can be used on their own, without the VFS, to serve as an abstraction of
// a single directory or ZIP file.

// The VFSFileReference level of abstraction is there to hold things like zip file indices,
// for fast re-open etc.

class VFSFileReference {
public:
	virtual ~VFSFileReference() {}
};

class VFSOpenFile {
public:
	virtual ~VFSOpenFile() {}
};

// Common interface parts between VFSBackend and VFS.
// Sometimes you don't need the VFS multiplexing and only have a VFSBackend *, sometimes you do need it,
// and it would be cool to be able to use the same interface, like when loading INI files.
class VFSInterface {
public:
	virtual ~VFSInterface() {}
	virtual uint8_t *ReadFile(std::string_view path, size_t *size) = 0;
	// If listing already contains files, it'll be cleared.
	virtual bool GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) = 0;
};

class VFSBackend : public VFSInterface {
public:
	virtual VFSFileReference *GetFile(std::string_view path) = 0;
	virtual bool GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) = 0;
	virtual void ReleaseFile(VFSFileReference *vfsReference) = 0;

	// Must write the size of the file to *size. Both backends can do this efficiently here,
	// avoiding a call to GetFileInfo.
	virtual VFSOpenFile *OpenFileForRead(VFSFileReference *vfsReference, size_t *size) = 0;
	virtual void Rewind(VFSOpenFile *vfsOpenFile) = 0;
	virtual size_t Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) = 0;
	virtual void CloseFile(VFSOpenFile *vfsOpenFile) = 0;

	// Filter support is optional but nice to have
	virtual bool GetFileInfo(std::string_view path, File::FileInfo *info) = 0;
	virtual bool Exists(std::string_view path) {
		File::FileInfo info{};
		return GetFileInfo(path, &info) && info.exists;
	}

	virtual std::string toString() const = 0;
};

class VFS : public VFSInterface {
public:
	~VFS() { Clear(); }
	void Register(std::string_view prefix, VFSBackend *reader);
	void Clear();

	// Use delete [] to release the returned memory.
	// Always allocates an extra zero byte at the end, so that it
	// can be used for text like shader sources.
	uint8_t *ReadFile(std::string_view filename, size_t *size) override;
	bool GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) override;

	bool GetFileInfo(std::string_view filename, File::FileInfo *fileInfo);
	bool Exists(std::string_view path);

private:
	struct VFSEntry {
		std::string_view prefix;
		VFSBackend *reader;
	};
	std::vector<VFSEntry> entries_;
};

extern VFS g_VFS;
