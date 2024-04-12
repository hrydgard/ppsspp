#pragma once

#include <vector>
#include <string>
#include <string_view>

#include "Common/File/DirListing.h"

// To emphasize that Android storage mode strings are different, let's just use
// an enum.
enum class Android_OpenContentUriMode {
	READ = 0,  // "r"
	READ_WRITE = 1,  // "rw"
	READ_WRITE_TRUNCATE = 2,  // "rwt"
};

// Matches the constants in PpssppActivity.java.
enum class StorageError {
	SUCCESS = 0,
	UNKNOWN = -1,
	NOT_FOUND = -2,
	DISK_FULL = -3,
	ALREADY_EXISTS = -4,
};

inline StorageError StorageErrorFromInt(int ival) {
	if (ival >= 0) {
		return StorageError::SUCCESS;
	} else {
		return (StorageError)ival;
	}
}

extern std::string g_extFilesDir;
extern std::string g_externalDir;
extern std::string g_nativeLibDir;

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

#include <jni.h>

void Android_StorageSetNativeActivity(jobject nativeActivity);

bool Android_IsContentUri(std::string_view uri);
int Android_OpenContentUriFd(std::string_view uri, const Android_OpenContentUriMode mode);
StorageError Android_CreateDirectory(const std::string &parentTreeUri, const std::string &dirName);
StorageError Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName);
StorageError Android_MoveFile(const std::string &fileUri, const std::string &srcParentUri, const std::string &destParentUri);
StorageError Android_CopyFile(const std::string &fileUri, const std::string &destParentUri);

// WARNING: This is very powerful, it will delete directories recursively!
StorageError Android_RemoveFile(const std::string &fileUri);

StorageError Android_RenameFileTo(const std::string &fileUri, const std::string &newName);
bool Android_GetFileInfo(const std::string &fileUri, File::FileInfo *info);
bool Android_FileExists(const std::string &fileUri);
int64_t Android_ComputeRecursiveDirectorySize(const std::string &fileUri);
int64_t Android_GetFreeSpaceByContentUri(const std::string &uri);
int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath);
bool Android_IsExternalStoragePreservedLegacy();
const char *Android_ErrorToString(StorageError error);

std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri, bool *exists);

void Android_RegisterStorageCallbacks(JNIEnv * env, jobject obj);

#else

// Stub out the Android Storage wrappers, so that we can avoid ifdefs everywhere.

// See comments for the corresponding functions above.

inline bool Android_IsContentUri(std::string_view uri) { return false; }
inline int Android_OpenContentUriFd(std::string_view uri, const Android_OpenContentUriMode mode) { return -1; }
inline StorageError Android_CreateDirectory(const std::string &parentTreeUri, const std::string &dirName) { return StorageError::UNKNOWN; }
inline StorageError Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName) { return StorageError::UNKNOWN; }
inline StorageError Android_MoveFile(const std::string &fileUri, const std::string &srcParentUri, const std::string &destParentUri) { return StorageError::UNKNOWN; }
inline StorageError Android_CopyFile(const std::string &fileUri, const std::string &destParentUri) { return StorageError::UNKNOWN; }
inline StorageError Android_RemoveFile(const std::string &fileUri) { return StorageError::UNKNOWN; }
inline StorageError Android_RenameFileTo(const std::string &fileUri, const std::string &newName) { return StorageError::UNKNOWN; }
inline bool Android_GetFileInfo(const std::string &fileUri, File::FileInfo *info) { return false; }
inline bool Android_FileExists(const std::string &fileUri) { return false; }
inline int64_t Android_ComputeRecursiveDirectorySize(const std::string &fileUri) { return -1; }
inline int64_t Android_GetFreeSpaceByContentUri(const std::string &uri) { return -1; }
inline int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath) { return -1; }
inline bool Android_IsExternalStoragePreservedLegacy() { return false; }
inline const char *Android_ErrorToString(StorageError error) { return ""; }
inline std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri, bool *exists) {
	*exists = false;
	return std::vector<File::FileInfo>();
}

#endif
