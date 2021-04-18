#pragma once

#include "ppsspp_config.h"

#include <string>
#include <vector>

#include "Common/LogManager.h"
#include "Common/File/DirListing.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

class AndroidLogger : public LogListener {
public:
	void Log(const LogMessage &message) override;
};

extern std::string g_extFilesDir;

// Called from PathBrowser for example.

bool Android_IsContentUri(const std::string &uri);
int Android_OpenContentUriFd(const std::string &uri);
bool Android_CreateDirectory(const std::string &parentTreeUri, const std::string &dirName);
bool Android_CreateFile(const std::string &parentTreeUri, const std::string &fileName);
bool Android_RemoveFile(const std::string &fileUri);
bool Android_GetFileInfo(const std::string &fileUri, FileInfo *info);

std::vector<File::FileInfo> Android_ListContentUri(const std::string &uri);

#endif
