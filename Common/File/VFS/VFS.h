#pragma once

#include <vector>

#include "Common/File/DirListing.h"

// Basic virtual file system. Used to manage assets on Android, where we have to
// read them manually out of the APK zipfile, while being able to run on other
// platforms as well with the appropriate directory set-up.

class AssetReader;

class VFS {
public:
	void Register(const char *prefix, AssetReader *reader);
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
		AssetReader *reader;
	};

	VFSEntry entries_[16];
	int numEntries_ = 0;
};

extern VFS g_VFS;
