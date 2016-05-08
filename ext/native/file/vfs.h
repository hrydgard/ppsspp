#pragma once

#include "base/basictypes.h"
#include "file/file_util.h"
// Basic virtual file system. Used to manage assets on Android, where we have to
// read them manually out of the APK zipfile, while being able to run on other
// platforms as well with the appropriate directory set-up.

class AssetReader;

void VFSRegister(const char *prefix, AssetReader *reader);
void VFSShutdown();

// Use delete [] to release the returned memory.
// Always allocates an extra zero byte at the end, so that it
// can be used for text like shader sources.
uint8_t *VFSReadFile(const char *filename, size_t *size);
bool VFSGetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter = 0);
bool VFSGetFileInfo(const char *filename, FileInfo *fileInfo);
