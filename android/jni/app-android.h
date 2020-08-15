#pragma once

#include "ppsspp_config.h"

#include "Common/LogManager.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <jni.h>

jclass findClass(const char* name);
JNIEnv* getEnv();

#endif

class AndroidLogger : public LogListener {
public:
	void Log(const LogMessage &message) override;
};
