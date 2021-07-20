#include "Common/File/AndroidStorage.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"

#include "android/jni/app-android.h"

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

static jmethodID openContentUri;
static jmethodID listContentUriDir;
static jmethodID contentUriCreateFile;
static jmethodID contentUriCreateDirectory;
static jmethodID contentUriCopyFile;
static jmethodID contentUriRemoveFile;
static jmethodID contentUriRenameFileTo;
static jmethodID contentUriGetFileInfo;
static jmethodID contentUriFileExists;
static jmethodID contentUriGetFreeStorageSpace;
static jmethodID filePathGetFreeStorageSpace;
static jmethodID isExternalStoragePreservedLegacy;

static jobject g_nativeActivity;

void Android_StorageSetNativeActivity(jobject nativeActivity) {
	g_nativeActivity = nativeActivity;
}

void Android_RegisterStorageCallbacks(JNIEnv * env, jobject obj) {
	openContentUri = env->GetMethodID(env->GetObjectClass(obj), "openContentUri", "(Ljava/lang/String;Ljava/lang/String;)I");
	_dbg_assert_(openContentUri);
	listContentUriDir = env->GetMethodID(env->GetObjectClass(obj), "listContentUriDir", "(Ljava/lang/String;)[Ljava/lang/String;");
	_dbg_assert_(listContentUriDir);
	contentUriCreateDirectory = env->GetMethodID(env->GetObjectClass(obj), "contentUriCreateDirectory", "(Ljava/lang/String;Ljava/lang/String;)Z");
	_dbg_assert_(contentUriCreateDirectory);
	contentUriCreateFile = env->GetMethodID(env->GetObjectClass(obj), "contentUriCreateFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
	_dbg_assert_(contentUriCreateFile);
	contentUriCopyFile = env->GetMethodID(env->GetObjectClass(obj), "contentUriCopyFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
	_dbg_assert_(contentUriCopyFile);
	contentUriRemoveFile = env->GetMethodID(env->GetObjectClass(obj), "contentUriRemoveFile", "(Ljava/lang/String;)Z");
	_dbg_assert_(contentUriRemoveFile);
	contentUriRenameFileTo = env->GetMethodID(env->GetObjectClass(obj), "contentUriRenameFileTo", "(Ljava/lang/String;Ljava/lang/String;)Z");
	_dbg_assert_(contentUriRenameFileTo);
	contentUriGetFileInfo = env->GetMethodID(env->GetObjectClass(obj), "contentUriGetFileInfo", "(Ljava/lang/String;)Ljava/lang/String;");
	_dbg_assert_(contentUriGetFileInfo);
	contentUriFileExists = env->GetMethodID(env->GetObjectClass(obj), "contentUriFileExists", "(Ljava/lang/String;)Z");
	_dbg_assert_(contentUriFileExists);
	contentUriGetFreeStorageSpace = env->GetMethodID(env->GetObjectClass(obj), "contentUriGetFreeStorageSpace", "(Ljava/lang/String;)J");
	_dbg_assert_(contentUriGetFreeStorageSpace);
	filePathGetFreeStorageSpace = env->GetMethodID(env->GetObjectClass(obj), "filePathGetFreeStorageSpace", "(Ljava/lang/String;)J");
	_dbg_assert_(filePathGetFreeStorageSpace);
	isExternalStoragePreservedLegacy = env->GetMethodID(env->GetObjectClass(obj), "isExternalStoragePreservedLegacy", "()Z");
	_dbg_assert_(isExternalStoragePreservedLegacy);
}

bool Android_IsContentUri(const std::string &filename) {
	return startsWith(filename, "content://");
}

int Android_OpenContentUriFd(const std::string &filename, Android_OpenContentUriMode mode) {
	if (!g_nativeActivity) {
		return -1;
	}

	std::string fname = filename;
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
	int fd = env->CallIntMethod(g_nativeActivity, openContentUri, j_filename, j_mode);
	return fd;
}

bool Android_CreateDirectory(const std::string &rootTreeUri, const std::string &dirName) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramRoot = env->NewStringUTF(rootTreeUri.c_str());
	jstring paramDirName = env->NewStringUTF(dirName.c_str());
	return env->CallBooleanMethod(g_nativeActivity, contentUriCreateDirectory, paramRoot, paramDirName);
}

bool Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramRoot = env->NewStringUTF(parentTreeUri.c_str());
	jstring paramFileName = env->NewStringUTF(fileName.c_str());
	return env->CallBooleanMethod(g_nativeActivity, contentUriCreateFile, paramRoot, paramFileName);
}

bool Android_CopyFile(const std::string &fileUri, const std::string &destParentUri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramFileName = env->NewStringUTF(fileUri.c_str());
	jstring paramDestParentUri = env->NewStringUTF(destParentUri.c_str());
	return env->CallBooleanMethod(g_nativeActivity, contentUriCopyFile, paramFileName, paramDestParentUri);
}

bool Android_RemoveFile(const std::string &fileUri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramFileName = env->NewStringUTF(fileUri.c_str());
	return env->CallBooleanMethod(g_nativeActivity, contentUriRemoveFile, paramFileName);
}

bool Android_RenameFileTo(const std::string &fileUri, const std::string &newName) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	jstring paramFileUri = env->NewStringUTF(fileUri.c_str());
	jstring paramNewName = env->NewStringUTF(newName.c_str());
	return env->CallBooleanMethod(g_nativeActivity, contentUriRenameFileTo, paramFileUri, paramNewName);
}

// NOTE: Does not set fullName - you're supposed to already know it.
static bool ParseFileInfo(const std::string &line, File::FileInfo *fileInfo) {
	std::vector<std::string> parts;
	SplitString(line, '|', parts);
	if (parts.size() != 4) {
		ERROR_LOG(FILESYS, "Bad format: %s", line.c_str());
		return false;
	}
	fileInfo->name = std::string(parts[2]);
	fileInfo->isDirectory = parts[0][0] == 'D';
	fileInfo->exists = true;
	sscanf(parts[1].c_str(), "%" PRIu64, &fileInfo->size);
	fileInfo->isWritable = true;  // TODO: Should be passed as part of the string.
	fileInfo->access = fileInfo->isDirectory ? 0666 : 0777;  // TODO: For read-only mappings, reflect that here, similarly as with isWritable.

	uint64_t lastModifiedMs = 0;
	sscanf(parts[3].c_str(), "%" PRIu64, &lastModifiedMs);

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

	jstring str = (jstring)env->CallObjectMethod(g_nativeActivity, contentUriGetFileInfo, paramFileUri);
	if (!str) {
		return false;
	}
	const char *charArray = env->GetStringUTFChars(str, 0);
	bool retval = ParseFileInfo(std::string(charArray), fileInfo);
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
	bool exists = env->CallBooleanMethod(g_nativeActivity, contentUriFileExists, paramFileUri);
	return exists;
}

std::vector<File::FileInfo> Android_ListContentUri(const std::string &path) {
	if (!g_nativeActivity) {
		return std::vector<File::FileInfo>();
	}
	auto env = getEnv();

	double start = time_now_d();

	jstring param = env->NewStringUTF(path.c_str());
	jobject retval = env->CallObjectMethod(g_nativeActivity, listContentUriDir, param);

	jobjectArray fileList = (jobjectArray)retval;
	std::vector<File::FileInfo> items;
	int size = env->GetArrayLength(fileList);
	for (int i = 0; i < size; i++) {
		jstring str = (jstring)env->GetObjectArrayElement(fileList, i);
		const char *charArray = env->GetStringUTFChars(str, 0);
		if (charArray) {  // paranoia
			File::FileInfo info;
			if (ParseFileInfo(std::string(charArray), &info)) {
				// We can just reconstruct the URI.
				info.fullName = Path(path) / info.name;
				items.push_back(info);
			}
		}
		env->ReleaseStringUTFChars(str, charArray);
		env->DeleteLocalRef(str);
	}
	env->DeleteLocalRef(fileList);

	double elapsed = time_now_d() - start;
	if (elapsed > 0.1) {
		INFO_LOG(FILESYS, "Listing directory on content URI took %0.3f s (%d files)", elapsed, (int)size);
	}
	return items;
}

int64_t Android_GetFreeSpaceByContentUri(const std::string &uri) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();

	jstring param = env->NewStringUTF(uri.c_str());
	return env->CallLongMethod(g_nativeActivity, contentUriGetFreeStorageSpace, param);
}

int64_t Android_GetFreeSpaceByFilePath(const std::string &filePath) {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();

	jstring param = env->NewStringUTF(filePath.c_str());
	return env->CallLongMethod(g_nativeActivity, filePathGetFreeStorageSpace, param);
}

bool Android_IsExternalStoragePreservedLegacy() {
	if (!g_nativeActivity) {
		return false;
	}
	auto env = getEnv();
	return env->CallBooleanMethod(g_nativeActivity, isExternalStoragePreservedLegacy);
}

const char *Android_ErrorToString(ContentError error) {
	switch (error) {
	case ContentError::SUCCESS: return "SUCCESS";
	case ContentError::OTHER: return "OTHER";
	case ContentError::NOT_FOUND: return "NOT_FOUND";
	case ContentError::DISK_FULL: return "DISK_FULL";
	default: return "(UNKNOWN)";
	}
}

#endif
