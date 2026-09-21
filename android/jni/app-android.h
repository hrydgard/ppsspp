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

// Returns a new local reference to the current activity, or null if there is none. The activity gets
// replaced whenever Android recreates it (rotation, resize), so don't hang on to anything longer-lived.
jobject Android_GetActivity(JNIEnv *env);

// Frees the JNI local references created in its scope. Java only does that for us when a native
// method returns to it, which never happens on the threads we attach ourselves (EmuThread, IO and
// worker threads), so there every local ref leaks until the thread detaches. Before Android 8 there's
// only room for 512 of them, and running out aborts the process.
class JNILocalFrame {
public:
	explicit JNILocalFrame(JNIEnv *env, int capacity = 16) : env_(env) {
		pushed_ = env_->PushLocalFrame(capacity) == 0;
	}
	~JNILocalFrame() {
		if (pushed_) {
			env_->PopLocalFrame(nullptr);
		}
	}
	JNILocalFrame(const JNILocalFrame &) = delete;
	JNILocalFrame &operator=(const JNILocalFrame &) = delete;

private:
	JNIEnv *env_;
	bool pushed_;
};

#endif

#else

inline std::string Android_GetInputDeviceDebugString() { return ""; }
inline std::vector<std::string> Android_GetNativeCrashHistory(int maxEntries) { return {}; }

#endif

