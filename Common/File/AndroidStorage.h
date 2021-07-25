#pragma once

#include <vector>
#include <string>

#include "Common/File/DirListing.h"

// To emphasize that Android storage mode strings are different, let's just use
// an enum.
enum class Android_OpenContentUriMode {
	READ = 0,  // "r"
	READ_WRITE = 1,  // "rw"
	READ_WRITE_TRUNCATE = 2,  // "rwt"
};

// Matches the constants in PpssppActivity.java.
enum class ContentError {
	SUCCESS = 0,
	OTHER = -1,
	NOT_FOUND = -2,
	DISK_FULL = -3,
};

inline ContentError ContentErrorFromInt(int ival) {
	if (ival >= 0) {
		return ContentError::SUCCESS;
	} else {
		return (ContentError)ival;
	}
}

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

#include <jni.h>

extern std::string g_extFilesDir;

void Android_StorageSetNativeActivity(jobject nativeActivity);

bool Android_IsContentUri(const std::string &uri);
int Android_OpenContentUriFd(const std::string &uri, const Android_OpenContentUriMode mode);
bool Android_CreateDirectory(const std::string &parentTreeUri, const std::string &dirName);
bool Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName);
bool Android_MoveFile(const std::string &fileUri, const std::string &srcParentUri, const std::string &destParentUri);
bool Android_CopyFile(const std::string &fileUri, const std::string &destParentUri);
bool Android_RemoveFile(const std::string &fileUri);
bool Android_RenameFileTo(const std::string &fileUri, const std::string &newName);
bool Android_GetFileInfo(const std::string &fileUri, File::FileInfo *info);
bool Android_FileExists(const std::string &fileUri);
int64_t Android_GetFreeSpaceByContentUri(const std::string &uri);
int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath);
bool Android_IsExternalStoragePreservedLegacy();
const char *Android_ErrorToString(ContentError error);

std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri);

void Android_RegisterStorageCallbacks(JNIEnv * env, jobject obj);

#else

extern std::string g_extFilesDir;

// Stub out the Android Storage wrappers, so that we can avoid ifdefs everywhere.

inline bool Android_IsContentUri(const std::string &uri) { return false; }
inline int Android_OpenContentUriFd(const std::string &uri, const Android_OpenContentUriMode mode) { return -1; }
inline bool Android_CreateDirectory(const std::string &parentTreeUri, const std::string &dirName) { return false; }
inline bool Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName) { return false; }
inline bool Android_MoveFile(const std::string &fileUri, const std::string &srcParentUri, const std::string &destParentUri) { return false; }
inline bool Android_CopyFile(const std::string &fileUri, const std::string &destParentUri) { return false; }
inline bool Android_RemoveFile(const std::string &fileUri) { return false; }
inline bool Android_RenameFileTo(const std::string &fileUri, const std::string &newName) { return false; }
inline bool Android_GetFileInfo(const std::string &fileUri, File::FileInfo *info) { return false; }
inline bool Android_FileExists(const std::string &fileUri) { return false; }
inline int64_t Android_GetFreeSpaceByContentUri(const std::string &uri) { return -1; }
inline int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath) { return -1; }
inline bool Android_IsExternalStoragePreservedLegacy() { return false; }
inline const char *Android_ErrorToString(ContentError error) { return ""; }
inline std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri) {
	return std::vector<File::FileInfo>();
}

#endif
