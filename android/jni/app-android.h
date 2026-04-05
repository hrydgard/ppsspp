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
std::vector<std::string> Android_GetNativeCrashHistory(int maxEntries);

#if !defined(__LIBRETRO__)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

#endif

#else

inline std::string Android_GetInputDeviceDebugString() { return ""; }
inline std::vector<std::string> Android_GetNativeCrashHistory(int maxEntries) { return {}; }

#endif

