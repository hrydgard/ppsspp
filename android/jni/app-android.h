#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

#endif
