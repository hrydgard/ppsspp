#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <jni.h>

extern JNIEnv *jniEnvMain;
extern JNIEnv *jniEnvGraphics;
extern JavaVM *javaVM;

#endif
