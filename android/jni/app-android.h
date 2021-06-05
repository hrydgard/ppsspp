#pragma once

#include "ppsspp_config.h"

#include <string>
#include <vector>
#include <cstdint>

#include "Common/LogManager.h"
#include "Common/File/DirListing.h"
#include "Common/File/Path.h"
#include "Common/File/AndroidStorage.h"

#if PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

class AndroidLogger : public LogListener {
public:
	void Log(const LogMessage &message) override;
};

#endif
