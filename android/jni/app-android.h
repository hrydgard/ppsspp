#pragma once

#include "ppsspp_config.h"

#include <string>
#include <cstdint>

#include "Common/Log/LogManager.h"
#include "Common/File/DirListing.h"
#include "Common/File/Path.h"
#include "Common/File/AndroidStorage.h"

#if PPSSPP_PLATFORM(ANDROID)

std::string Android_GetInputDeviceDebugString();

#if !defined(__LIBRETRO__)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

class AndroidLogger : public LogListener {
public:
	void Log(const LogMessage &message) override;
};
#endif

#endif

