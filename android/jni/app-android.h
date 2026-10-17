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
#include <vector>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"

jclass findClass(const char* name);
JNIEnv* getEnv();

// GetStringUTFChars returns Java's modified UTF-8, where characters outside the BMP (emoji) become two 3-byte
// surrogates, so we go through UTF-16.
inline std::string GetJavaString(JNIEnv *env, jstring jstr) {
	if (!jstr) {
		return "";
	}
	const jsize len = env->GetStringLength(jstr);
	const jchar *chars = env->GetStringChars(jstr, nullptr);
	if (!chars) {
		return "";
	}
	std::string cpp_string;
	cpp_string.reserve(len);
	char buf[4];
	for (jsize i = 0; i < len; i++) {
		uint32_t c = chars[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && chars[i + 1] >= 0xDC00 && chars[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (chars[i + 1] - 0xDC00);
			i++;
		} else if (c >= 0xD800 && c < 0xE000) {
			// Unpaired surrogate.
			c = 0xFFFD;
		}
		cpp_string.append(buf, UTF8::encode(buf, c));
	}
	env->ReleaseStringChars(jstr, chars);
	return cpp_string;
}

// Returns true (and clears it) if the last Java call threw. A pending exception makes the next JNI call abort,
// and the call's return value is garbage.
inline bool CheckJavaException(JNIEnv *env, const char *func) {
	if (!env->ExceptionCheck()) {
		return false;
	}
	ERROR_LOG(Log::System, "%s: Java exception", func);
	env->ExceptionDescribe();
	env->ExceptionClear();
	return true;
}

// Reads a java.util.List of strings. Null entries are skipped.
inline std::vector<std::string> GetJavaStringList(JNIEnv *env, jobject jlist) {
	std::vector<std::string> result;
	if (!jlist) {
		return result;
	}
	jclass listClass = env->GetObjectClass(jlist);
	jmethodID sizeMethod = env->GetMethodID(listClass, "size", "()I");
	jmethodID getMethod = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
	env->DeleteLocalRef(listClass);
	jint size = env->CallIntMethod(jlist, sizeMethod);
	if (CheckJavaException(env, __FUNCTION__)) {
		return result;
	}
	for (jint i = 0; i < size; i++) {
		jstring jstr = (jstring)env->CallObjectMethod(jlist, getMethod, i);
		if (CheckJavaException(env, __FUNCTION__)) {
			break;
		}
		if (jstr) {
			result.push_back(GetJavaString(env, jstr));
			env->DeleteLocalRef(jstr);
		}
	}
	return result;
}

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

