#pragma once

#include <vector>
#include <cstdint>

#include "Common/File/DirListing.h"

// Basic read-only virtual file system. Used to manage assets on Android, where we have to
// read them manually out of the APK zipfile, while being able to run on other
// platforms as well with the appropriate directory set-up.

// Note that this is kinda similar in concept to Core/MetaFileSystem.h, but that one
// is specifically for operations done by the emulated PSP, while this is for operations
// on the system level, like loading assets, and maybe texture packs. Also, as mentioned,
// this one is read-only, so a bit smaller and simpler.

class VFSBackend {
public:
	virtual ~VFSBackend() {}
	// use delete[] to release the returned memory.
	virtual uint8_t *ReadFile(const char *path, size_t *size) = 0;

	// Filter support is optional but nice to have
	virtual bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter = 0) = 0;
	virtual bool GetFileInfo(const char *path, File::FileInfo *info) = 0;
	virtual std::string toString() const = 0;
};

class VFS {
public:
	~VFS() { Clear(); }
	void Register(const char *prefix, VFSBackend *reader);
	void Clear();

	// Use delete [] to release the returned memory.
	// Always allocates an extra zero byte at the end, so that it
	// can be used for text like shader sources.
	uint8_t *ReadFile(const char *filename, size_t *size);
	bool GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter = 0);
	bool GetFileInfo(const char *filename, File::FileInfo *fileInfo);

private:
	struct VFSEntry {
		const char *prefix;
		VFSBackend *reader;
	};
	std::vector<VFSEntry> entries_;
};

extern VFS g_VFS;
