#include <inttypes.h>

#include "Common/File/AndroidStorage.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/System/System.h"

#include "android/jni/app-android.h"
#include "Common/Thread/ThreadUtil.h"

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

static jmethodID openContentUri;
static jmethodID listContentUriDir;
static jmethodID contentUriCreateFile;
static jmethodID contentUriCreateDirectory;
static jmethodID contentUriCopyFile;
static jmethodID contentUriMoveFile;
static jmethodID contentUriRemoveFile;
static jmethodID contentUriRenameFileTo;
static jmethodID contentUriGetFileInfo;
static jmethodID contentUriFileExists;
static jmethodID contentUriGetFreeStorageSpace;
static jmethodID filePathGetFreeStorageSpace;
static jmethodID isExternalStoragePreservedLegacy;
static jmethodID computeRecursiveDirectorySize;

static jobject g_nativeActivity;
static jclass g_classContentUri;

void Android_StorageSetActivity(jobject nativeActivity) {
	g_nativeActivity = nativeActivity;
}

void Android_RegisterStorageCallbacks(JNIEnv * env, jobject obj) {
	jclass localClass = env->FindClass("org/ppsspp/ppsspp/ContentUri");
	_dbg_assert_(localClass);

	openContentUri = env->GetStaticMethodID(localClass, "openContentUri", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(openContentUri);
	listContentUriDir = env->GetStaticMethodID(localClass, "listContentUriDir", "(Landroid/app/Activity;Ljava/lang/String;)[Ljava/lang/String;");
	_dbg_assert_(listContentUriDir);
	contentUriCreateDirectory = env->GetStaticMethodID(localClass, "contentUriCreateDirectory", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(contentUriCreateDirectory);
	contentUriCreateFile = env->GetStaticMethodID(localClass, "contentUriCreateFile", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(contentUriCreateFile);
	contentUriCopyFile = env->GetStaticMethodID(localClass, "contentUriCopyFile", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(contentUriCopyFile);
	contentUriRemoveFile = env->GetStaticMethodID(localClass, "contentUriRemoveFile", "(Landroid/app/Activity;Ljava/lang/String;)I");
	_dbg_assert_(contentUriRemoveFile);
	contentUriMoveFile = env->GetStaticMethodID(localClass, "contentUriMoveFile", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(contentUriMoveFile);
	contentUriRenameFileTo = env->GetStaticMethodID(localClass, "contentUriRenameFileTo", "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(contentUriRenameFileTo);
	contentUriGetFileInfo = env->GetStaticMethodID(localClass, "contentUriGetFileInfo", "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
	_dbg_assert_(contentUriGetFileInfo);
	contentUriFileExists = env->GetStaticMethodID(localClass, "contentUriFileExists", "(Landroid/app/Activity;Ljava/lang/String;)Z");
	_dbg_assert_(contentUriFileExists);
	contentUriGetFreeStorageSpace = env->GetStaticMethodID(localClass, "contentUriGetFreeStorageSpace", "(Landroid/app/Activity;Ljava/lang/String;)J");
	_dbg_assert_(contentUriGetFreeStorageSpace);
	filePathGetFreeStorageSpace = env->GetStaticMethodID(localClass, "filePathGetFreeStorageSpace", "(Landroid/app/Activity;Ljava/lang/String;)J");
	_dbg_assert_(filePathGetFreeStorageSpace);
	isExternalStoragePreservedLegacy = env->GetStaticMethodID(localClass, "isExternalStoragePreservedLegacy", "()Z");  // doesn't need an activity
	_dbg_assert_(isExternalStoragePreservedLegacy);
	computeRecursiveDirectorySize = env->GetStaticMethodID(localClass, "computeRecursiveDirectorySize", "(Landroid/app/Activity;Ljava/lang/String;)J");
	_dbg_assert_(computeRecursiveDirectorySize);

	g_classContentUri = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
	env->DeleteLocalRef(localClass); // cleanup local ref
}

void Android_UnregisterStorageCallbacks(JNIEnv * env) {
	if (g_classContentUri) {
		env->DeleteGlobalRef(g_classContentUri);
		g_classContentUri = nullptr;
	}
	g_nativeActivity = nullptr;
	openContentUri = nullptr;
	listContentUriDir = nullptr;
	contentUriCreateFile = nullptr;
	contentUriCreateDirectory = nullptr;
	contentUriCopyFile = nullptr;
	contentUriMoveFile = nullptr;
	contentUriRemoveFile = nullptr;
	contentUriRenameFileTo = nullptr;
	contentUriGetFileInfo = nullptr;
	contentUriFileExists = nullptr;
	contentUriGetFreeStorageSpace = nullptr;
	filePathGetFreeStorageSpace = nullptr;
	isExternalStoragePreservedLegacy = nullptr;
	computeRecursiveDirectorySize = nullptr;
}

bool Android_IsContentUri(std::string_view filename) {
	return startsWith(filename, "content://");
}

int Android_OpenContentUriFd(std::string_view filename, Android_OpenContentUriMode mode) {
	if (!g_nativeActivity) {
		// Hit this in shortcut creation.
		ERROR_LOG(Log::IO, "Android_OpenContentUriFd: No native activity");
		return -1;
	}

	/*
	// Should breakpoint here to try to find and move as many of these off the EmuThread as possible
	if (!strcmp(GetCurrentThreadName(), "EmuThread")) {
		WARN_LOG(Log::IO, "Content URI opened on EmuThread: %.*s", (int)filename.size(), filename.data());
	}
	*/

	std::string fname(filename);
	// PPSSPP adds an ending slash to directories before looking them up.
	// TODO: Fix that in the caller (or don't call this for directories).
	if (fname.back() == '/')
		fname.pop_back();

	auto env = getEnv();
	const char *modeStr = "";
	switch (mode) {
	case Android_OpenContentUriMode::READ: modeStr = "r"; break;
	case Android_OpenContentUriMode::READ_WRITE: modeStr = "rw"; break;
	case Android_OpenContentUriMode::READ_WRITE_TRUNCATE: modeStr = "rwt"; break;
	}
	jstring j_filename = env->NewStringUTF(fname.c_str());
	jstring j_mode = env->NewStringUTF(modeStr);
	int fd = env->CallStaticIntMethod(g_classContentUri, openContentUri, g_nativeActivity, j_filename, j_mode);
	return fd;
}

StorageError Android_CreateDirectory(const std::string &rootTreeUri, const std::string &dirName) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramRoot = env->NewStringUTF(rootTreeUri.c_str());
	jstring paramDirName = env->NewStringUTF(dirName.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriCreateDirectory, g_nativeActivity, paramRoot, paramDirName));
}

StorageError Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramRoot = env->NewStringUTF(parentTreeUri.c_str());
	jstring paramFileName = env->NewStringUTF(fileName.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriCreateFile, g_nativeActivity, paramRoot, paramFileName));
}

StorageError Android_CopyFile(const std::string &fileUri, const std::string &destParentUri) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramFileName = env->NewStringUTF(fileUri.c_str());
	jstring paramDestParentUri = env->NewStringUTF(destParentUri.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriCopyFile, g_nativeActivity, paramFileName, paramDestParentUri));
}

StorageError Android_MoveFile(const std::string &fileUri, const std::string &srcParentUri, const std::string &destParentUri) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramFileName = env->NewStringUTF(fileUri.c_str());
	jstring paramSrcParentUri = env->NewStringUTF(srcParentUri.c_str());
	jstring paramDestParentUri = env->NewStringUTF(destParentUri.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriMoveFile, g_nativeActivity, paramFileName, paramSrcParentUri, paramDestParentUri));
}

StorageError Android_RemoveFile(const std::string &fileUri) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramFileName = env->NewStringUTF(fileUri.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriRemoveFile, g_nativeActivity, paramFileName));
}

StorageError Android_RenameFileTo(const std::string &fileUri, const std::string &newName) {
	if (!g_nativeActivity) {
		return StorageError::UNKNOWN;
	}
	auto env = getEnv();
	jstring paramFileUri = env->NewStringUTF(fileUri.c_str());
	jstring paramNewName = env->NewStringUTF(newName.c_str());
	return StorageErrorFromInt(env->CallStaticIntMethod(g_classContentUri, contentUriRenameFileTo, g_nativeActivity, paramFileUri, paramNewName));
}

// NOTE: Does not set fullName - you're supposed to already know it.
static bool ParseFileInfo(std::string_view line, File::FileInfo *fileInfo) {
	std::vector<std::string> parts;
	SplitString(line, '|', parts);
	if (parts.size() != 4) {
		ERROR_LOG(Log::FileSystem, "Bad format (1): %.*s", (int)line.size(), line.data());
		return false;
	}
	fileInfo->name = parts[2];
	fileInfo->isDirectory = parts[0][0] == 'D';
	fileInfo->exists = true;
	if (1 != sscanf(parts[1].c_str(), "%" PRIu64, &fileInfo->size)) {
		ERROR_LOG(Log::FileSystem, "Bad format (2): %.*s", (int)line.size(), line.data());
		return false;
	}
	fileInfo->isWritable = true;  // TODO: Should be passed as part of the string.
	// TODO: For read-only mappings, reflect that here, similarly as with isWritable.
	// Directories are normally executable (0111) which means they're traversable.
	fileInfo->access = fileInfo->isDirectory ? 0777 : 0666;

	uint64_t lastModifiedMs = 0;
	if (1 != sscanf(parts[3].c_str(), "%" PRIu64, &lastModifiedMs)) {
		ERROR_LOG(Log::FileSystem, "Bad format (3): %.*s", (int)line.size(), line.data());
		return false;
	}

	// Convert from milliseconds
	uint32_t lastModified = lastModifiedMs / 1000;

	// We don't have better information, so let's just spam lastModified into all the date/time fields.
	fileInfo->mtime = lastModified;
	fileInfo->ctime = lastModified;
	fileInfo->atime = lastModified;
	return true;
}

bool Android_GetFileInfo(const std::string &fileUri, File::FileInfo *fileInfo) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramFileUri = env->NewStringUTF(fileUri.c_str());

	jstring str = (jstring)env->CallStaticObjectMethod(g_classContentUri, contentUriGetFileInfo, g_nativeActivity, paramFileUri);
	if (!str) {
		return false;
	}
	const char *charArray = env->GetStringUTFChars(str, 0);
	bool retval = ParseFileInfo(charArray, fileInfo);
	fileInfo->fullName = Path(fileUri);

	env->DeleteLocalRef(str);
	return retval && fileInfo->exists;
}

bool Android_FileExists(const std::string &fileUri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramFileUri = env->NewStringUTF(fileUri.c_str());
	bool exists = env->CallStaticBooleanMethod(g_classContentUri, contentUriFileExists, g_nativeActivity, paramFileUri);
	return exists;
}

std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri, const std::string &prefix, bool *exists) {
	if (!g_nativeActivity) {
		*exists = false;
		return {};
	}
	auto env = getEnv();
	*exists = true;

	double start = time_now_d();

	jstring param = env->NewStringUTF(uri.c_str());
	jobject retval = env->CallStaticObjectMethod(g_classContentUri, listContentUriDir, g_nativeActivity, param);

	jobjectArray fileList = (jobjectArray)retval;
	std::vector<File::FileInfo> items;
	int size = env->GetArrayLength(fileList);
	for (int i = 0; i < size; i++) {
		jstring str = (jstring)env->GetObjectArrayElement(fileList, i);
		const char *charArray = env->GetStringUTFChars(str, 0);
		if (charArray) {  // paranoia
			std::string line = charArray;
			File::FileInfo info{};
			if (line == "X") {
				// Indicates an exception thrown, uri doesn't exist.
				*exists = false;
			} else if (ParseFileInfo(line, &info)) {
				// We can just reconstruct the URI.
				info.fullName = Path(uri) / info.name;
				// INFO_LOG(Log::IO, "%s", info.name.c_str());
				items.push_back(info);
			}
		}
		env->ReleaseStringUTFChars(str, charArray);
		env->DeleteLocalRef(str);
	}
	env->DeleteLocalRef(fileList);

	double elapsed = time_now_d() - start;
	double threshold = 0.1;
	if (elapsed >= threshold) {
		INFO_LOG(Log::IO, "Listing directory on content URI '%s' took %0.3f s (%d files, log threshold = %0.3f)", uri.c_str(), elapsed, (int)items.size(), threshold);
	}
	return items;
}

int64_t Android_GetFreeSpaceByContentUri(const std::string &uri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();

	jstring param = env->NewStringUTF(uri.c_str());
	return env->CallStaticLongMethod(g_classContentUri, contentUriGetFreeStorageSpace, g_nativeActivity, param);
}

// Hm, this is never used? We use statvfs instead.
int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();

	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) < 26) {
		// This is available from Android O.
		return -1;
	}

	jstring param = env->NewStringUTF(filePath.c_str());
	return env->CallStaticLongMethod(g_classContentUri, filePathGetFreeStorageSpace, g_nativeActivity, param);
}

int64_t Android_ComputeRecursiveDirectorySize(const std::string &uri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();

	jstring param = env->NewStringUTF(uri.c_str());

	double start = time_now_d();
	int64_t size = env->CallStaticLongMethod(g_classContentUri, computeRecursiveDirectorySize, g_nativeActivity, param);
	double elapsed = time_now_d() - start;

	INFO_LOG(Log::IO, "ComputeRecursiveDirectorySize(%s) in %0.3f s", uri.c_str(), elapsed);
	return size;
}

bool Android_IsExternalStoragePreservedLegacy() {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	// Note: No activity param
	return env->CallStaticBooleanMethod(g_classContentUri, isExternalStoragePreservedLegacy);
}

const char *Android_ErrorToString(StorageError error) {
	switch (error) {
	case StorageError::SUCCESS: return "SUCCESS";
	case StorageError::UNKNOWN: return "UNKNOWN";
	case StorageError::NOT_FOUND: return "NOT_FOUND";
	case StorageError::DISK_FULL: return "DISK_FULL";
	case StorageError::ALREADY_EXISTS: return "ALREADY_EXISTS";
	default: return "(UNKNOWN)";
	}
}

#else

// These strings should never appear except on Android.
// Very hacky.
std::string g_extFilesDir = "(IF YOU SEE THIS THERE'S A BUG)";
std::string g_externalDir = "(IF YOU SEE THIS THERE'S A BUG (2))";
std::string g_nativeLibDir = "(IF YOU SEE THIS THERE'S A BUG (3))";

#endif
