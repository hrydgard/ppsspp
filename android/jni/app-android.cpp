// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgard (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.

#include <cstdlib>
#include <cstdint>

#include <sstream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

#ifndef _MSC_VER

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#elif !defined(JNIEXPORT)
// Just for better highlighting in MSVC if opening this file.
// Not having types makes it get confused and say everything is wrong.
struct JavaVM;
typedef void *jmethodID;
typedef void *jfieldID;

typedef uint8_t jboolean;
typedef int8_t jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;
typedef jint jsize;
typedef float jfloat;
typedef double jdouble;

class _jobject {};
class _jclass : public _jobject {};
typedef _jobject *jobject;
typedef _jclass *jclass;
typedef jobject jstring;
typedef jobject jbyteArray;
typedef jobject jintArray;
typedef jobject jfloatArray;

struct JNIEnv {};

#define JNIEXPORT
#define JNICALL
// Just a random value to make MSVC highlighting happy.
#define JNI_VERSION_1_6 16
#endif

#include "Common/Log.h"
#include "Common/LogReporting.h"

#include "Common/Net/Resolve.h"
#include "android/jni/AndroidAudio.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/AndroidStorage.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"

#include "Common/GraphicsContext.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "AndroidGraphicsContext.h"
#include "AndroidVulkanContext.h"
#include "AndroidJavaGLContext.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Loaders.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/KeyMap.h"
#include "Core/System.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "UI/GameInfoCache.h"

#include "app-android.h"

bool useCPUThread = true;

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

// OpenGL emu thread
static std::thread emuThread;
static std::atomic<int> emuThreadState((int)EmuThreadState::DISABLED);

AndroidAudioState *g_audioState;

struct FrameCommand {
	FrameCommand() {}
	FrameCommand(std::string cmd, std::string prm) : command(cmd), params(prm) {}

	std::string command;
	std::string params;
};

static std::mutex frameCommandLock;
static std::queue<FrameCommand> frameCommands;

static std::string systemName;
static std::string langRegion;
static std::string mogaVersion;
static std::string boardName;

std::string g_externalDir;  // Original external dir (root of Android storage).
std::string g_extFilesDir;  // App private external dir.
std::string g_nativeLibDir;  // App native library dir

static std::vector<std::string> g_additionalStorageDirs;

static int optimalFramesPerBuffer = 0;
static int optimalSampleRate = 0;
static int sampleRate = 0;
static int framesPerBuffer = 0;
static int androidVersion;
static int deviceType;

// Should only be used for display detection during startup (for config defaults etc)
// This is the ACTUAL display size, not the hardware scaled display size.
// Exposed so it can be displayed on the touchscreen test.
static int display_xres;
static int display_yres;
static int display_dpi_x;
static int display_dpi_y;
static int backbuffer_format;	// Android PixelFormat enum

static int desiredBackbufferSizeX;
static int desiredBackbufferSizeY;

// Cache the class loader so we can use it from native threads. Required for TextAndroid.
extern JavaVM *gJvm;

static jobject gClassLoader;
static jmethodID gFindClassMethod;

static float g_safeInsetLeft = 0.0;
static float g_safeInsetRight = 0.0;
static float g_safeInsetTop = 0.0;
static float g_safeInsetBottom = 0.0;

static jmethodID postCommand;
static jmethodID getDebugString;

static jobject nativeActivity;

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static bool renderer_inited = false;
static std::mutex renderLock;

static bool sustainedPerfSupported = false;

static std::map<SystemPermission, PermissionStatus> permissions;

static AndroidGraphicsContext *graphicsContext;

#define MessageBox(a, b, c, d) __android_log_print(ANDROID_LOG_INFO, APP_NAME, "%s %s", (b), (c));

#if PPSSPP_ARCH(ARMV7)
// Old Android workaround
extern "C" {
int utimensat(int fd, const char *path, const struct timespec times[2]) {
	return -1;
}
}
#endif

static void ProcessFrameCommands(JNIEnv *env);

JNIEnv* getEnv() {
	JNIEnv *env;
	int status = gJvm->GetEnv((void**)&env, JNI_VERSION_1_6);
	_assert_msg_(status >= 0, "'%s': Can only call getEnv if you've attached the thread already!", GetCurrentThreadName());
	return env;
}

jclass findClass(const char* name) {
	return static_cast<jclass>(getEnv()->CallObjectMethod(gClassLoader, gFindClassMethod, getEnv()->NewStringUTF(name)));
}

void Android_AttachThreadToJNI() {
	JNIEnv *env;
	int status = gJvm->GetEnv((void **)&env, JNI_VERSION_1_6);
	if (status < 0) {
		DEBUG_LOG(Log::System, "Attaching thread '%s' (not already attached) to JNI.", GetCurrentThreadName());
		JavaVMAttachArgs args{};
		args.version = JNI_VERSION_1_6;
		args.name = GetCurrentThreadName();
		status = gJvm->AttachCurrentThread(&env, &args);

		if (status < 0) {
			// bad, but what can we do other than report..
			ERROR_LOG_REPORT_ONCE(threadAttachFail, Log::System, "Failed to attach thread %s to JNI.", GetCurrentThreadName());
		}
	} else {
		WARN_LOG(Log::System, "Thread %s was already attached to JNI.", GetCurrentThreadName());
	}
}

void Android_DetachThreadFromJNI() {
	if (gJvm->DetachCurrentThread() == JNI_OK) {
		DEBUG_LOG(Log::System, "Detached thread from JNI: '%s'", GetCurrentThreadName());
	} else {
		WARN_LOG(Log::System, "Failed to detach thread '%s' from JNI - never attached?", GetCurrentThreadName());
	}
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *pjvm, void *reserved) {
	INFO_LOG(Log::System, "JNI_OnLoad");
	gJvm = pjvm;  // cache the JavaVM pointer
	auto env = getEnv();
	//replace with one of your classes in the line below
	auto randomClass = env->FindClass("org/ppsspp/ppsspp/NativeActivity");
	jclass classClass = env->GetObjectClass(randomClass);
	auto classLoaderClass = env->FindClass("java/lang/ClassLoader");
	auto getClassLoaderMethod = env->GetMethodID(classClass, "getClassLoader",
												 "()Ljava/lang/ClassLoader;");
	gClassLoader = env->NewGlobalRef(env->CallObjectMethod(randomClass, getClassLoaderMethod));
	gFindClassMethod = env->GetMethodID(classLoaderClass, "findClass",
										"(Ljava/lang/String;)Ljava/lang/Class;");

	RegisterAttachDetach(&Android_AttachThreadToJNI, &Android_DetachThreadFromJNI);

	TimeInit();
	return JNI_VERSION_1_6;
}

// Only used in OpenGL mode.
static void EmuThreadFunc() {
	SetCurrentThreadName("EmuThread");

	// Name the thread in the JVM, because why not (might result in better debug output in Play Console).
	// TODO: Do something clever with getEnv() and stored names from SetCurrentThreadName?
	JNIEnv *env;
	JavaVMAttachArgs args{};
	args.version = JNI_VERSION_1_6;
	args.name = "EmuThread";
	gJvm->AttachCurrentThread(&env, &args);

	INFO_LOG(Log::System, "Entering emu thread");

	// Wait for render loop to get started.
	INFO_LOG(Log::System, "Runloop: Waiting for displayInit...");
	while (!graphicsContext || graphicsContext->GetState() == GraphicsContextState::PENDING) {
		sleep_ms(5, "graphics-poll");
	}

	// Check the state of the graphics context before we try to feed it into NativeInitGraphics.
	if (graphicsContext->GetState() != GraphicsContextState::INITIALIZED) {
		ERROR_LOG(Log::G3D, "Failed to initialize the graphics context! %d", (int)graphicsContext->GetState());
		emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
		gJvm->DetachCurrentThread();
		return;
	}

	if (!NativeInitGraphics(graphicsContext)) {
		_assert_msg_(false, "NativeInitGraphics failed, might as well bail");
		emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
		gJvm->DetachCurrentThread();
		return;
	}

	INFO_LOG(Log::System, "Graphics initialized. Entering loop.");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;
	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		{
			std::lock_guard<std::mutex> renderGuard(renderLock);
			NativeFrame(graphicsContext);
		}

		std::lock_guard<std::mutex> guard(frameCommandLock);
		if (!nativeActivity) {
			ERROR_LOG(Log::System, "No activity, clearing commands");
			while (!frameCommands.empty())
				frameCommands.pop();
			return;
		}
		// Still under lock here.
		ProcessFrameCommands(env);
	}

	INFO_LOG(Log::System, "QUIT_REQUESTED found, left EmuThreadFunc loop. Setting state to STOPPED.");
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();

	// Also ask the main thread to stop, so it doesn't hang waiting for a new frame.
	graphicsContext->StopThread();

	gJvm->DetachCurrentThread();
	INFO_LOG(Log::System, "Leaving emu thread");
}

static void EmuThreadStart() {
	INFO_LOG(Log::System, "EmuThreadStart");
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc);
}

// Call EmuThreadStop first, then keep running the GPU (or eat commands)
// as long as emuThreadState isn't STOPPED and/or there are still things queued up.
// Only after that, call EmuThreadJoin.
static void EmuThreadStop(const char *caller) {
	INFO_LOG(Log::System, "EmuThreadStop - stopping (%s)...", caller);
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
	INFO_LOG(Log::System, "EmuThreadJoin - joined");
}

static void PushCommand(std::string cmd, std::string param) {
	std::lock_guard<std::mutex> guard(frameCommandLock);
	frameCommands.push(FrameCommand(std::move(cmd), std::move(param)));
}

// Android implementation of callbacks to the Java part of the app
void System_Toast(std::string_view text) {
	PushCommand("toast", std::string(text));
}

void System_ShowKeyboard() {
	PushCommand("showKeyboard", "");
}

void System_Vibrate(int length_ms) {
	char temp[32];
	snprintf(temp, sizeof(temp), "%d", length_ms);
	PushCommand("vibrate", temp);
}

void System_LaunchUrl(LaunchUrlType urlType, const char *url) {
	switch (urlType) {
	case LaunchUrlType::BROWSER_URL: PushCommand("launchBrowser", url); break;
	case LaunchUrlType::MARKET_URL: PushCommand("launchMarket", url); break;
	case LaunchUrlType::EMAIL_ADDRESS: PushCommand("launchEmail", url); break;
	}
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return systemName;
	case SYSPROP_LANGREGION:	// "en_US"
		return langRegion;
	case SYSPROP_MOGA_VERSION:
		return mogaVersion;
	case SYSPROP_BOARDNAME:
		return boardName;
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_ADDITIONAL_STORAGE_DIRS:
		return g_additionalStorageDirs;

	case SYSPROP_TEMP_DIRS:
	default:
		return {};
	}
}

int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_SYSTEMVERSION:
		return androidVersion;
	case SYSPROP_DEVICE_TYPE:
		return deviceType;
	case SYSPROP_DISPLAY_XRES:
		return display_xres;
	case SYSPROP_DISPLAY_YRES:
		return display_yres;
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return sampleRate;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
		return framesPerBuffer;
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE:
		return optimalSampleRate;
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER:
		return optimalFramesPerBuffer;
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return g_display.display_hz;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
		return g_safeInsetLeft;
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
		return g_safeInsetRight;
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
		return g_safeInsetTop;
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return g_safeInsetBottom;
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_SUPPORTS_PERMISSIONS:
		if (androidVersion < 23) {
			// 6.0 Marshmallow introduced run time permissions.
			return false;
		} else {
			// It gets a bit complicated here. If scoped storage enforcement is on,
			// we also don't need to request permissions. We'll have the access we request
			// on a per-folder basis.
			return !System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE);
		}
	case SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE:
		return sustainedPerfSupported;  // 7.0 introduced sustained performance mode as an optional feature.
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
		return androidVersion >= 11;  // honeycomb
	case SYSPROP_HAS_TEXT_CLIPBOARD:
		return true;
	case SYSPROP_HAS_OPEN_DIRECTORY:
		return false;  // We have this implemented but it may or may not work depending on if a file explorer is installed.
	case SYSPROP_HAS_ADDITIONAL_STORAGE:
		return !g_additionalStorageDirs.empty();
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return deviceType != DEVICE_TYPE_VR;
	case SYSPROP_HAS_FILE_BROWSER:
		// It's only really needed with scoped storage, but why not make it available
		// as far back as possible - works just fine.
		return (androidVersion >= 19) && (deviceType != DEVICE_TYPE_VR);  // when ACTION_OPEN_DOCUMENT was added
	case SYSPROP_HAS_FOLDER_BROWSER:
		// Uses OPEN_DOCUMENT_TREE to let you select a folder.
		// Doesn't actually mean it's usable though, in many early versions of Android
		// this dialog is complete garbage and only lets you select subfolders of the Downloads folder.
		return (androidVersion >= 21) && (deviceType != DEVICE_TYPE_VR);  // when ACTION_OPEN_DOCUMENT_TREE was added
	case SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR:
		return false;  // Update if we add support in FileUtil.cpp: OpenFileInEditor
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_ANDROID_SCOPED_STORAGE:
		// We turn this on for Android 30+ (11) now that when we target Android 11+.
		// Along with adding:
		//   android:preserveLegacyExternalStorage="true"
		// To the already requested:
		//   android:requestLegacyExternalStorage="true"
		//
		// This will cause Android 11+ to still behave like Android 10 until the app
		// is manually uninstalled. We can detect this state with
		// Android_IsExternalStoragePreservedLegacy(), but most of the app will just see
		// that scoped storage enforcement is disabled in this case.
		if (androidVersion >= 30) {
			// Here we do a check to see if we ended up in the preserveLegacyExternalStorage path.
			// That won't last if the user uninstalls/reinstalls though, but would preserve the user
			// experience for simple upgrades so maybe let's support it.
			return !Android_IsExternalStoragePreservedLegacy();
		} else {
			return false;
		}
	case SYSPROP_HAS_KEYBOARD:
		return deviceType != DEVICE_TYPE_VR;
	case SYSPROP_HAS_ACCELEROMETER:
		return deviceType == DEVICE_TYPE_MOBILE;
	case SYSPROP_CAN_CREATE_SHORTCUT:
		return false;  // We can't create shortcuts directly from game code, but we can from the Android UI.
#ifndef HTTPS_NOT_AVAILABLE
	case SYSPROP_SUPPORTS_HTTPS:
		return !g_Config.bDisableHTTPS;
#endif
	default:
		return false;
	}
}

std::string Android_GetInputDeviceDebugString() {
	if (!nativeActivity) {
		return "(N/A)";
	}
	auto env = getEnv();

	jstring jparam = env->NewStringUTF("InputDevice");
	jstring jstr = (jstring)env->CallObjectMethod(nativeActivity, getDebugString, jparam);
	if (!jstr) {
		env->DeleteLocalRef(jparam);
		return "(N/A)";
	}

	const char *charArray = env->GetStringUTFChars(jstr, nullptr);
	std::string retVal = charArray;
	env->ReleaseStringUTFChars(jstr, charArray);
	env->DeleteLocalRef(jstr);
	env->DeleteLocalRef(jparam);
	return retVal;
}

std::string GetJavaString(JNIEnv *env, jstring jstr) {
	if (!jstr)
		return "";
	const char *str = env->GetStringUTFChars(jstr, nullptr);
	std::string cpp_string = std::string(str);
	env->ReleaseStringUTFChars(jstr, str);
	return cpp_string;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeActivity_registerCallbacks(JNIEnv *env, jobject obj) {
	nativeActivity = env->NewGlobalRef(obj);
	postCommand = env->GetMethodID(env->GetObjectClass(obj), "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
	getDebugString = env->GetMethodID(env->GetObjectClass(obj), "getDebugString", "(Ljava/lang/String;)Ljava/lang/String;");
	_dbg_assert_(postCommand);
	_dbg_assert_(getDebugString);

	Android_RegisterStorageCallbacks(env, obj);
	Android_StorageSetNativeActivity(nativeActivity);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeActivity_unregisterCallbacks(JNIEnv *env, jobject obj) {
	Android_StorageSetNativeActivity(nullptr);
	env->DeleteGlobalRef(nativeActivity);
	nativeActivity = nullptr;
}

// This is now only used as a trigger for GetAppInfo as a function to all before Init.
// On Android we don't use any of the values it returns.
extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_isLandscape(JNIEnv *env, jclass) {
	std::string app_name, app_nice_name, version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_nice_name, &landscape, &version);
	return landscape;
}

// Allow the app to intercept the back button.
extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_isAtTopLevel(JNIEnv *env, jclass) {
	return NativeIsAtTopLevel();
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioConfig
	(JNIEnv *env, jclass, jint optimalFPB, jint optimalSR) {
	optimalFramesPerBuffer = optimalFPB;
	optimalSampleRate = optimalSR;
}

// Easy way for the Java side to ask the C++ side for configuration options, such as
// the rotation lock which must be controlled from Java on Android.
static std::string QueryConfig(std::string_view query) {
	char temp[128];
	if (query == "screenRotation") {
		INFO_LOG(Log::G3D, "g_Config.screenRotation = %d", g_Config.iScreenRotation);
		snprintf(temp, sizeof(temp), "%d", g_Config.iScreenRotation);
		return temp;
	} else if (query == "immersiveMode") {
		return g_Config.bImmersiveMode ? "1" : "0";
	} else if (query == "sustainedPerformanceMode") {
		return g_Config.bSustainedPerformanceMode ? "1" : "0";
	} else if (query == "androidJavaGL") {
		// If we're using Vulkan, we say no... need C++ to use Vulkan.
		if (GetGPUBackend() == GPUBackend::VULKAN) {
			return "false";
		}
		// Otherwise, some devices prefer the Java init so play it safe.
		return "true";
	} else {
		return "";
	}
}

extern "C" jstring Java_org_ppsspp_ppsspp_NativeApp_queryConfig
	(JNIEnv *env, jclass, jstring jquery) {
	std::string query = GetJavaString(env, jquery);
	std::string result = QueryConfig(query);
	jstring jresult = env->NewStringUTF(result.c_str());
	return jresult;
}

static void parse_args(std::vector<std::string> &args, const std::string value) {
	// Simple argument parser so we can take args from extra params.
	const char *p = value.c_str();

	while (*p != '\0') {
		while (isspace(*p)) {
			p++;
		}
		if (*p == '\0') {
			break;
		}

		bool done = false;
		bool quote = false;
		std::string arg;

		while (!done) {
			size_t sz = strcspn(p, "\"\\ \r\n\t");
			arg += std::string(p, sz);
			p += sz;

			switch (*p) {
			case '"':
				quote = !quote;
				p++;
				break;

			case '\\':
				p++;
				arg += std::string(p, 1);
				p++;
				break;

			case '\0':
				done = true;
				break;

			default:
				// If it's not the above, it's whitespace.
				if (!quote) {
					done = true;
				} else {
					sz = strspn(p, " \r\n\t");
					arg += std::string(p, sz);
					p += sz;
				}
				break;
			}
		}

		args.push_back(arg);

		while (isspace(*p)) {
			p++;
		}
	}
}

// Need to use raw Android logging before NativeInit.
#define EARLY_LOG(...)  __android_log_print(ANDROID_LOG_INFO, "PPSSPP", __VA_ARGS__)

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_init
(JNIEnv * env, jclass, jstring jmodel, jint jdeviceType, jstring jlangRegion, jstring japkpath,
	jstring jdataDir, jstring jexternalStorageDir, jstring jexternalFilesDir, jstring jNativeLibDir, jstring jadditionalStorageDirs, jstring jcacheDir, jstring jshortcutParam,
	jint jAndroidVersion, jstring jboard) {
	// SetCurrentThreadName("androidInit");

	// Makes sure we get early permission grants.
	ProcessFrameCommands(env);

	EARLY_LOG("NativeApp.init() -- begin");
	PROFILE_INIT();

	std::lock_guard<std::mutex> guard(renderLock);
	renderer_inited = false;
	exitRenderLoop = false;
	androidVersion = jAndroidVersion;
	deviceType = jdeviceType;

	Path apkPath(GetJavaString(env, japkpath));
	g_VFS.Register("", ZipFileReader::Create(apkPath, "assets/"));

	systemName = GetJavaString(env, jmodel);
	langRegion = GetJavaString(env, jlangRegion);

	EARLY_LOG("NativeApp.init(): device name: '%s'", systemName.c_str());

	std::string externalStorageDir = GetJavaString(env, jexternalStorageDir);
	std::string additionalStorageDirsString = GetJavaString(env, jadditionalStorageDirs);
	std::string externalFilesDir = GetJavaString(env, jexternalFilesDir);
	std::string nativeLibDir = GetJavaString(env, jNativeLibDir);

	g_externalDir = externalStorageDir;
	g_extFilesDir = externalFilesDir;
	g_nativeLibDir = nativeLibDir;

	if (!additionalStorageDirsString.empty()) {
		SplitString(additionalStorageDirsString, ':', g_additionalStorageDirs);
		for (auto &str : g_additionalStorageDirs) {
			EARLY_LOG("Additional storage: %s", str.c_str());
		}
	}

	std::string user_data_path = GetJavaString(env, jdataDir);
	if (!user_data_path.empty())
		user_data_path += "/";
	std::string shortcut_param = GetJavaString(env, jshortcutParam);
	std::string cacheDir = GetJavaString(env, jcacheDir);
	std::string buildBoard = GetJavaString(env, jboard);
	boardName = buildBoard;
	EARLY_LOG("NativeApp.init(): External storage path: %s", externalStorageDir.c_str());
	EARLY_LOG("NativeApp.init(): Launch shortcut parameter: %s", shortcut_param.c_str());

	std::string app_name;
	std::string app_nice_name;
	std::string version;
	bool landscape;

	// Unfortunately, on the Samsung Galaxy S7, this isn't in /proc/cpuinfo.
	// We also can't read it from __system_property_get.
	if (buildBoard == "universal8890") {
		cpu_info.sQuirks.bExynos8890DifferingCachelineSizes = true;
	}

	NativeGetAppInfo(&app_name, &app_nice_name, &landscape, &version);

	// If shortcut_param is not empty, pass it as additional arguments to the NativeInit() method.
	// NativeInit() is expected to treat extra argument as boot_filename, which in turn will start game immediately.
	// NOTE: Will only work if ppsspp started from Activity.onCreate(). Won't work if ppsspp app start from onResume().

	std::vector<const char *> args;
	std::vector<std::string> temp;
	args.push_back(app_name.c_str());
	if (!shortcut_param.empty()) {
		EARLY_LOG("NativeInit shortcut param %s", shortcut_param.c_str());
		parse_args(temp, shortcut_param);
		for (const auto &arg : temp) {
			args.push_back(arg.c_str());
		}
	}

	NativeInit((int)args.size(), &args[0], user_data_path.c_str(), externalStorageDir.c_str(), cacheDir.c_str());

	// In debug mode, don't allow creating software Vulkan devices (reject by VulkanMaybeAvailable).
	// Needed for #16931.
#ifdef NDEBUG
	if (!VulkanMayBeAvailable()) {
		// If VulkanLoader decided on no viable backend, let's force Vulkan off in release builds at least.
		g_Config.iGPUBackend = 0;
	}
#endif

	// No need to use EARLY_LOG anymore.

retry:
	switch (g_Config.iGPUBackend) {
	case (int)GPUBackend::OPENGL:
		useCPUThread = true;
		INFO_LOG(Log::System, "NativeApp.init() -- creating OpenGL context (JavaGL)");
		graphicsContext = new AndroidJavaEGLGraphicsContext();
		INFO_LOG(Log::System, "NativeApp.init() - launching emu thread");
		EmuThreadStart();
		break;
	case (int)GPUBackend::VULKAN:
	{
		INFO_LOG(Log::System, "NativeApp.init() -- creating Vulkan context");
		useCPUThread = false;
		// The Vulkan render manager manages its own thread.
		// We create and destroy the Vulkan graphics context in the app main thread though.
		AndroidVulkanContext *ctx = new AndroidVulkanContext();
		if (!ctx->InitAPI()) {
			INFO_LOG(Log::System, "Failed to initialize Vulkan, switching to OpenGL");
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend(GPUBackend::OPENGL);
			goto retry;
		} else {
			graphicsContext = ctx;
		}
		break;
	}
	default:
		ERROR_LOG(Log::System, "NativeApp.init(): iGPUBackend %d not supported. Switching to OpenGL.", (int)g_Config.iGPUBackend);
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
		goto retry;
	}

	if (IsVREnabled()) {
		Version gitVer(PPSSPP_GIT_VERSION);
		InitVROnAndroid(gJvm, nativeActivity, systemName.c_str(), gitVer.ToInteger(), "PPSSPP");
		SetVRCallbacks(NativeAxis, NativeKey, NativeTouch);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioInit(JNIEnv *, jclass) {
	sampleRate = optimalSampleRate;
	if (optimalSampleRate == 0) {
		sampleRate = 44100;
	}
	if (optimalFramesPerBuffer > 0) {
		framesPerBuffer = optimalFramesPerBuffer;
	} else {
		framesPerBuffer = 512;
	}

	// Some devices have totally bonkers buffer sizes like 8192. They will have terrible latency anyway, so to avoid having to
	// create extra smart buffering code, we'll just let their regular mixer deal with it, missing the fast path (as if they had one...)
	if (framesPerBuffer > 512) {
		framesPerBuffer = 512;
		sampleRate = 44100;
	}

	INFO_LOG(Log::Audio, "NativeApp.audioInit() -- Using OpenSL audio! frames/buffer: %i	 optimal sr: %i	 actual sr: %i", optimalFramesPerBuffer, optimalSampleRate, sampleRate);
	if (!g_audioState) {
		g_audioState = AndroidAudio_Init(&NativeMix, framesPerBuffer, sampleRate);
	} else {
		ERROR_LOG(Log::Audio, "Audio state already initialized");
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioShutdown(JNIEnv *, jclass) {
	if (g_audioState) {
		AndroidAudio_Shutdown(g_audioState);
		g_audioState = nullptr;
	} else {
		ERROR_LOG(Log::Audio, "Audio state already shutdown!");
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioRecording_1SetSampleRate(JNIEnv *, jclass, jint sampleRate) {
	AndroidAudio_Recording_SetSampleRate(g_audioState, sampleRate);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioRecording_1Start(JNIEnv *, jclass) {
	AndroidAudio_Recording_Start(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioRecording_1Stop(JNIEnv *, jclass) {
	AndroidAudio_Recording_Stop(g_audioState);
}

bool System_AudioRecordingIsAvailable() {
	return true;
}

bool System_AudioRecordingState() {
	return AndroidAudio_Recording_State(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_resume(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.resume() - resuming audio");
	AndroidAudio_Resume(g_audioState);

	System_PostUIMessage(UIMessage::APP_RESUMED);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_pause(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.pause() - pausing audio");
	AndroidAudio_Pause(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_shutdown(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.shutdown() -- begin");

	if (renderer_inited && useCPUThread && graphicsContext) {
		// Only used in Java EGL path.

		// We can't lock renderLock here because the emu thread will be in NativeFrame
		// which locks renderLock already, and only gets out once we call ThreadFrame()
		// in a loop before, to empty the queue.
		EmuThreadStop("shutdown");
		INFO_LOG(Log::System, "BeginAndroidShutdown");
		graphicsContext->BeginAndroidShutdown();
		// Now, it could be that we had some frames queued up. Get through them.
		// We're on the render thread, so this is synchronous.
		do {
			INFO_LOG(Log::System, "Executing graphicsContext->ThreadFrame to clear buffers");
		} while (graphicsContext->ThreadFrame());
		graphicsContext->ThreadEnd();
		INFO_LOG(Log::System, "ThreadEnd called.");
		graphicsContext->ShutdownFromRenderThread();
		INFO_LOG(Log::System, "Graphics context now shut down from NativeApp_shutdown");

		INFO_LOG(Log::System, "Joining emuthread");
		EmuThreadJoin();
	}

	{
		std::lock_guard<std::mutex> guard(renderLock);

		if (graphicsContext) {
			INFO_LOG(Log::G3D, "Shutting down renderer");
			graphicsContext->Shutdown();
			delete graphicsContext;
			graphicsContext = nullptr;
			renderer_inited = false;
		} else {
			INFO_LOG(Log::G3D, "Not shutting down renderer - not initialized");
		}

		NativeShutdown();
		g_VFS.Clear();
	}

	{
		std::lock_guard<std::mutex> guard(frameCommandLock);
		while (frameCommands.size())
			frameCommands.pop();
	}
	INFO_LOG(Log::System, "NativeApp.shutdown() -- end");
}

// JavaEGL. This doesn't get called on the Vulkan path.
// This gets called from onSurfaceCreated.
extern "C" jboolean Java_org_ppsspp_ppsspp_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
	_assert_(useCPUThread);

	INFO_LOG(Log::G3D, "NativeApp.displayInit()");
	bool firstStart = !renderer_inited;

	// We should be running on the render thread here.
	std::string errorMessage;
	if (renderer_inited) {
		// Would be really nice if we could get something on the GL thread immediately when shutting down,
		// but the only mechanism for handling lost devices seems to be that onSurfaceCreated is called again,
		// which ends up calling displayInit.

		INFO_LOG(Log::G3D, "NativeApp.displayInit() restoring");
		EmuThreadStop("displayInit");
		graphicsContext->BeginAndroidShutdown();
		INFO_LOG(Log::G3D, "BeginAndroidShutdown. Looping until emu thread done...");
		// Skipping GL calls here because the old context is lost.
		while (graphicsContext->ThreadFrame()) {
		}
		INFO_LOG(Log::G3D, "Joining emu thread");
		EmuThreadJoin();

		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();

		INFO_LOG(Log::G3D, "Shut down both threads. Now let's bring it up again!");

		if (!graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0)) {
			System_Toast("Graphics initialization failed. Quitting.");
			return false;
		}

		graphicsContext->GetDrawContext()->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, details, 5.0);
		}, nullptr);

		EmuThreadStart();

		graphicsContext->ThreadStart();

		INFO_LOG(Log::G3D, "Restored.");
	} else {
		INFO_LOG(Log::G3D, "NativeApp.displayInit() first time");
		if (!graphicsContext || !graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0)) {
			System_Toast("Graphics initialization failed. Quitting.");
			return false;
		}

		graphicsContext->GetDrawContext()->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, details, 5.0);
		}, nullptr);

		graphicsContext->ThreadStart();
		renderer_inited = true;
	}

	System_PostUIMessage(UIMessage::RECREATE_VIEWS);

	if (IsVREnabled()) {
		EnterVR(firstStart);
	}
	return true;
}

static void recalculateDpi() {
	g_display.dpi = (float)display_dpi_x;
	g_display.dpi_scale_x = 240.0f / (float)display_dpi_x;
	g_display.dpi_scale_y = 240.0f / (float)display_dpi_y;
	g_display.dpi_scale_real_x = g_display.dpi_scale_x;
	g_display.dpi_scale_real_y = g_display.dpi_scale_y;

	g_display.dp_xres = display_xres * g_display.dpi_scale_x;
	g_display.dp_yres = display_yres * g_display.dpi_scale_y;

	g_display.pixel_in_dps_x = (float)g_display.pixel_xres / g_display.dp_xres;
	g_display.pixel_in_dps_y = (float)g_display.pixel_yres / g_display.dp_yres;

	INFO_LOG(Log::G3D, "RecalcDPI: display_xres=%d display_yres=%d pixel_xres=%d pixel_yres=%d", display_xres, display_yres, g_display.pixel_xres, g_display.pixel_yres);
	INFO_LOG(Log::G3D, "RecalcDPI: g_dpi=%f g_dpi_scale_x=%f g_dpi_scale_y=%f dp_xres=%d dp_yres=%d", g_display.dpi, g_display.dpi_scale_x, g_display.dpi_scale_y, g_display.dp_xres, g_display.dp_yres);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_backbufferResize(JNIEnv *, jclass, jint bufw, jint bufh, jint format) {
	INFO_LOG(Log::System, "NativeApp.backbufferResize(%d x %d)", bufw, bufh);

	bool new_size = g_display.pixel_xres != bufw || g_display.pixel_yres != bufh;
	int old_w = g_display.pixel_xres;
	int old_h = g_display.pixel_yres;
	// pixel_*res is the backbuffer resolution.
	g_display.pixel_xres = bufw;
	g_display.pixel_yres = bufh;
	backbuffer_format = format;

	if (IsVREnabled()) {
		GetVRResolutionPerEye(&g_display.pixel_xres, &g_display.pixel_yres);
	}

	recalculateDpi();

	if (new_size) {
		INFO_LOG(Log::G3D, "Size change detected (previously %d,%d) - calling NativeResized()", old_w, old_h);
		NativeResized();
	} else {
		INFO_LOG(Log::G3D, "NativeApp::backbufferResize: Size didn't change.");
	}
}

void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::ROTATE_UPDATED:
		PushCommand("rotate", "");
		break;
	case SystemNotification::FORCE_RECREATE_ACTIVITY:
		PushCommand("recreate", "");
		break;
	case SystemNotification::IMMERSIVE_MODE_CHANGE:
		PushCommand("immersive", "");
		break;
	case SystemNotification::SUSTAINED_PERF_CHANGE:
		PushCommand("sustainedPerfMode", "");
		break;
	case SystemNotification::TEST_JAVA_EXCEPTION:
		PushCommand("testException", "This is a test exception");
		break;
	default:
		break;
	}
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::EXIT_APP:
		PushCommand("finish", "");
		return true;
	case SystemRequestType::RESTART_APP:
		PushCommand("graphics_restart", param1);
		return true;
	case SystemRequestType::RECREATE_ACTIVITY:
		PushCommand("recreate", param1);
		return true;
	case SystemRequestType::COPY_TO_CLIPBOARD:
		PushCommand("copy_to_clipboard", param1);
		return true;
	case SystemRequestType::INPUT_TEXT_MODAL:
	{
		std::string serialized = StringFromFormat("%d:@:%s:@:%s", requestId, param1.c_str(), param2.c_str());
		PushCommand("inputbox", serialized);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
		PushCommand("browse_image", StringFromFormat("%d", requestId));
		return true;
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		BrowseFileType fileType = (BrowseFileType)param3;
		std::string params = StringFromFormat("%d", requestId);
		switch (fileType) {
		case BrowseFileType::SOUND_EFFECT:
			PushCommand("browse_file_audio", params);
			break;
		case BrowseFileType::ZIP:
			PushCommand("browse_file_zip", params);
			break;
		default:
			PushCommand("browse_file", params);
			break;
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
		PushCommand("browse_folder", StringFromFormat("%d", requestId));
		return true;

	case SystemRequestType::CAMERA_COMMAND:
		PushCommand("camera_command", param1);
		return true;
	case SystemRequestType::GPS_COMMAND:
		PushCommand("gps_command", param1);
		return true;
	case SystemRequestType::INFRARED_COMMAND:
		PushCommand("infrared_command", param1);
		return true;
	case SystemRequestType::MICROPHONE_COMMAND:
		PushCommand("microphone_command", param1);
		return true;
	case SystemRequestType::SHARE_TEXT:
		PushCommand("share_text", param1);
		return true;
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT:
		PushCommand("set_keep_screen_bright", param3 ? "on" : "off");
		return true;
	case SystemRequestType::SHOW_FILE_IN_FOLDER:
		PushCommand("show_folder", param1);
		return true;
	default:
		return false;
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_sendRequestResult(JNIEnv *env, jclass, jint jrequestID, jboolean result, jstring jvalue, jint jintValue) {
	std::string value = jvalue ? GetJavaString(env, jvalue) : "(no value)";
	INFO_LOG(Log::System, "Received result of request %d from Java: %d: %d '%s'", jrequestID, (int)result, jintValue, value.c_str());
	if (result) {
		g_requestManager.PostSystemSuccess(jrequestID, value.c_str());
	} else {
		g_requestManager.PostSystemFailure(jrequestID);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayRender(JNIEnv *env, jobject obj) {
	// This doesn't get called on the Vulkan path.
	_assert_(useCPUThread);

	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		hasSetThreadName = true;
		SetCurrentThreadName("AndroidRender");
	}

	if (IsVREnabled() && !StartVRRender())
		return;

	// This is the "GPU thread". Call ThreadFrame.
	if (!graphicsContext || !graphicsContext->ThreadFrame()) {
		return;
	}

	if (IsVREnabled()) {
		UpdateVRInput(g_Config.bHapticFeedback, g_display.dpi_scale_x, g_display.dpi_scale_y);
		FinishVRRender();
	}
}

void System_AskForPermission(SystemPermission permission) {
	switch (permission) {
	case SYSTEM_PERMISSION_STORAGE:
		PushCommand("ask_permission", "storage");
		break;
	}
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	if (androidVersion < 23) {
		return PERMISSION_STATUS_GRANTED;
	} else {
		return permissions[permission];
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_touch
	(JNIEnv *, jclass, float x, float y, int code, int pointerId) {
	if (!renderer_inited)
		return;
	TouchInput touch{};
	touch.id = pointerId;
	touch.x = x * g_display.dpi_scale_x;
	touch.y = y * g_display.dpi_scale_y;
	touch.flags = code;
	NativeTouch(touch);
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_keyDown(JNIEnv *, jclass, jint deviceId, jint key, jboolean isRepeat) {
	if (!renderer_inited) {
		return false; // could probably return true here too..
	}
	if (key == 0 && deviceId >= DEVICE_ID_PAD_0 && deviceId <= DEVICE_ID_PAD_9) {
		// Ignore keycode 0 from pads. Stadia controllers seem to produce them when pressing L2/R2 for some reason, confusing things.
		return true;  // need to eat the key so it doesn't go through legacy path
	}

	KeyInput keyInput;
	keyInput.deviceId = (InputDeviceID)deviceId;
	keyInput.keyCode = (InputKeyCode)key;
	keyInput.flags = KEY_DOWN;
	if (isRepeat) {
		keyInput.flags |= KEY_IS_REPEAT;
	}
	return NativeKey(keyInput);
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_keyUp(JNIEnv *, jclass, jint deviceId, jint key) {
	if (!renderer_inited) {
		return false; // could probably return true here too..
	}
	if (key == 0 && deviceId >= DEVICE_ID_PAD_0 && deviceId <= DEVICE_ID_PAD_9) {
		// Ignore keycode 0 from pads. Stadia controllers seem to produce them when pressing L2/R2 for some reason, confusing things.
		return true;  // need to eat the key so it doesn't go through legacy path
	}

	KeyInput keyInput;
	keyInput.deviceId = (InputDeviceID)deviceId;
	keyInput.keyCode = (InputKeyCode)key;
	keyInput.flags = KEY_UP;
	return NativeKey(keyInput);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_joystickAxis(
		JNIEnv *env, jclass, jint deviceId, jintArray axisIds, jfloatArray values, jint count) {
	if (!renderer_inited)
		return;

	AxisInput *axis = new AxisInput[count];
	_dbg_assert_(count <= env->GetArrayLength(axisIds));
	_dbg_assert_(count <= env->GetArrayLength(values));
	jint *axisIdBuffer = env->GetIntArrayElements(axisIds, nullptr);
	jfloat *valueBuffer = env->GetFloatArrayElements(values, nullptr);

	// These are dirty-filtered on the Java side.
	for (int i = 0; i < count; i++) {
		axis[i].deviceId = (InputDeviceID)(int)deviceId;
		axis[i].axisId = (InputAxis)(int)axisIdBuffer[i];
		axis[i].value = valueBuffer[i];
	}
	NativeAxis(axis, count);
	delete[] axis;
	env->ReleaseIntArrayElements(axisIds, axisIdBuffer, JNI_ABORT);  // ABORT just means we don't want changes copied back!
	env->ReleaseFloatArrayElements(values, valueBuffer, JNI_ABORT);  // ABORT just means we don't want changes copied back!
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_mouseWheelEvent(
	JNIEnv *env, jclass, jfloat x, jfloat y) {
	if (!renderer_inited)
		return false;
	// TODO: Mousewheel should probably be an axis instead.
	int wheelDelta = y * 30.0f;
	if (wheelDelta > 500) wheelDelta = 500;
	if (wheelDelta < -500) wheelDelta = -500;

	KeyInput key;
	key.deviceId = DEVICE_ID_MOUSE;
	if (wheelDelta < 0) {
		key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
		wheelDelta = -wheelDelta;
	} else {
		key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
	}
	// There's no separate keyup event for mousewheel events,
	// so we release it with a slight delay.
	key.flags = KEY_DOWN | KEY_HASWHEELDELTA | (wheelDelta << 16);
	NativeKey(key);
	return true;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_mouseDelta(
	JNIEnv * env, jclass, jfloat x, jfloat y) {
	if (!renderer_inited)
		return;
	NativeMouseDelta(x, y);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_accelerometer(JNIEnv *, jclass, float x, float y, float z) {
	if (!renderer_inited)
		return;
	NativeAccelerometer(x, y, z);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_sendMessageFromJava(JNIEnv *env, jclass, jstring jmessage, jstring jparam) {
	std::string msg = GetJavaString(env, jmessage);
	std::string prm = GetJavaString(env, jparam);

	// A bit ugly, see InputDeviceState.java.
	static InputDeviceID nextInputDeviceID = DEVICE_ID_ANY;

	// Some messages are caught by app-android. TODO: Should be all.
	if (msg == "moga") {
		mogaVersion = prm;
	} else if (msg == "permission_pending") {
		INFO_LOG(Log::System, "STORAGE PERMISSION: PENDING");
		// TODO: Add support for other permissions
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_PENDING;
		// Don't need to send along, nothing else is listening.
	} else if (msg == "permission_denied") {
		INFO_LOG(Log::System, "STORAGE PERMISSION: DENIED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_DENIED;
		// Don't need to send along, nothing else is listening.
	} else if (msg == "permission_granted") {
		INFO_LOG(Log::System, "STORAGE PERMISSION: GRANTED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_GRANTED;
		// Send along.
		System_PostUIMessage(UIMessage::PERMISSION_GRANTED, prm);
	} else if (msg == "sustained_perf_supported") {
		sustainedPerfSupported = true;
	} else if (msg == "safe_insets") {
		// INFO_LOG(Log::System, "Got insets: %s", prm.c_str());
		// We don't bother with supporting exact rectangular regions. Safe insets are good enough.
		int left, right, top, bottom;
		if (4 == sscanf(prm.c_str(), "%d:%d:%d:%d", &left, &right, &top, &bottom)) {
			g_safeInsetLeft = (float)left * g_display.dpi_scale_x;
			g_safeInsetRight = (float)right * g_display.dpi_scale_x;
			g_safeInsetTop = (float)top * g_display.dpi_scale_y;
			g_safeInsetBottom = (float)bottom * g_display.dpi_scale_y;
		}
	} else if (msg == "inputDeviceConnectedID") {
		nextInputDeviceID = (InputDeviceID)parseLong(prm);
	} else if (msg == "inputDeviceConnected") {
		KeyMap::NotifyPadConnected(nextInputDeviceID, prm);
	} else if (msg == "core_powerSaving") {
		// Forward.
		System_PostUIMessage(UIMessage::POWER_SAVING, prm);
	} else if (msg == "exception") {
		g_OSD.Show(OSDType::MESSAGE_ERROR, std::string("Java Exception"), prm, 10.0f);
	} else if (msg == "shortcutParam") {
		if (prm.empty()) {
			WARN_LOG(Log::System, "shortcutParam empty");
			return;
		}
		INFO_LOG(Log::System, "shortcutParam received: %s", prm.c_str());
		System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, StripQuotes(prm));
	} else {
		ERROR_LOG(Log::System, "Got unexpected message from Java, ignoring: %s / %s", msg.c_str(), prm.c_str());
	}
}

void correctRatio(int &sz_x, int &sz_y, float scale) {
	float x = (float)sz_x;
	float y = (float)sz_y;
	float ratio = x / y;
	INFO_LOG(Log::G3D, "CorrectRatio: Considering size: %0.2f/%0.2f=%0.2f for scale %f", x, y, ratio, scale);
	float targetRatio;

	// Try to get the longest dimension to match scale*PSP resolution.
	if (x >= y) {
		targetRatio = 480.0f / 272.0f;
		x = 480.f * scale;
		y = 272.f * scale;
	} else {
		targetRatio = 272.0f / 480.0f;
		x = 272.0f * scale;
		y = 480.0f * scale;
	}

	float correction = targetRatio / ratio;
	INFO_LOG(Log::G3D, "Target ratio: %0.2f ratio: %0.2f correction: %0.2f", targetRatio, ratio, correction);
	if (ratio < targetRatio) {
		y *= correction;
	} else {
		x /= correction;
	}

	sz_x = x;
	sz_y = y;
	INFO_LOG(Log::G3D, "Corrected ratio: %dx%d", sz_x, sz_y);
}

void getDesiredBackbufferSize(int &sz_x, int &sz_y) {
	sz_x = display_xres;
	sz_y = display_yres;

	int scale = g_Config.iAndroidHwScale;
	// Override hw scale for TV type devices.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV)
		scale = 0;

	if (scale == 1) {
		// If g_Config.iInternalResolution is also set to Auto (1), we fall back to "Device resolution" (0). It works out.
		scale = g_Config.iInternalResolution;
	} else if (scale >= 2) {
		scale -= 1;
	}

	int max_res = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 1;

	scale = std::min(scale, max_res);

	if (scale > 0) {
		correctRatio(sz_x, sz_y, scale);
	} else {
		sz_x = 0;
		sz_y = 0;
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_setDisplayParameters(JNIEnv *, jclass, jint xres, jint yres, jint dpi, jfloat refreshRate) {
	INFO_LOG(Log::G3D, "NativeApp.setDisplayParameters(%d x %d, dpi=%d, refresh=%0.2f)", xres, yres, dpi, refreshRate);

	if (IsVREnabled()) {
		int width, height;
		GetVRResolutionPerEye(&width, &height);
		xres = width;
		yres = height * 272 / 480;
		dpi = 320;
	}

	bool changed = false;
	changed = changed || display_xres != xres || display_yres != yres;
	changed = changed || display_dpi_x != dpi || display_dpi_y != dpi;
	changed = changed || g_display.display_hz != refreshRate;

	if (changed) {
		display_xres = xres;
		display_yres = yres;
		display_dpi_x = dpi;
		display_dpi_y = dpi;
		g_display.display_hz = refreshRate;

		recalculateDpi();
		NativeResized();
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_computeDesiredBackbufferDimensions(JNIEnv *, jclass) {
	getDesiredBackbufferSize(desiredBackbufferSizeX, desiredBackbufferSizeY);
}

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_getDesiredBackbufferWidth(JNIEnv *, jclass) {
	return desiredBackbufferSizeX;
}

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_getDesiredBackbufferHeight(JNIEnv *, jclass) {
	return desiredBackbufferSizeY;
}

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_getDisplayFramerateMode(JNIEnv *, jclass) {
	return g_Config.iDisplayFramerateMode;
}

std::vector<std::string> System_GetCameraDeviceList() {
	jclass cameraClass = findClass("org/ppsspp/ppsspp/CameraHelper");
	jmethodID deviceListMethod = getEnv()->GetStaticMethodID(cameraClass, "getDeviceList", "()Ljava/util/ArrayList;");
	jobject deviceListObject = getEnv()->CallStaticObjectMethod(cameraClass, deviceListMethod);
	jclass arrayListClass = getEnv()->FindClass("java/util/ArrayList");
	jmethodID arrayListSize = getEnv()->GetMethodID(arrayListClass, "size", "()I");
	jmethodID arrayListGet = getEnv()->GetMethodID(arrayListClass, "get", "(I)Ljava/lang/Object;");

	jint arrayListObjectLen = getEnv()->CallIntMethod(deviceListObject, arrayListSize);
	std::vector<std::string> deviceListVector;

	for (int i = 0; i < arrayListObjectLen; i++) {
		jstring dev = static_cast<jstring>(getEnv()->CallObjectMethod(deviceListObject, arrayListGet, i));
		const char *cdev = getEnv()->GetStringUTFChars(dev, nullptr);
		if (!cdev) {
			getEnv()->DeleteLocalRef(dev);
			continue;
		}
		deviceListVector.emplace_back(cdev);
		getEnv()->ReleaseStringUTFChars(dev, cdev);
		getEnv()->DeleteLocalRef(dev);
	}
	return deviceListVector;
}

extern "C" jint Java_org_ppsspp_ppsspp_NativeApp_getSelectedCamera(JNIEnv *, jclass) {
	int cameraId = 0;
	sscanf(g_Config.sCameraDevice.c_str(), "%d:", &cameraId);
	return cameraId;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_setGpsDataAndroid(JNIEnv *, jclass,
       jlong time, jfloat hdop, jfloat latitude, jfloat longitude, jfloat altitude, jfloat speed, jfloat bearing) {
	GPS::setGpsData(time, hdop, latitude, longitude, altitude, speed, bearing);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_setSatInfoAndroid(JNIEnv *, jclass,
	   jshort index, jshort id, jshort elevation, jshort azimuth, jshort snr, jshort good) {
	GPS::setSatInfo(index, id, elevation, azimuth, snr, good);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_pushCameraImageAndroid(JNIEnv *env, jclass, jbyteArray image) {
	if (image) {
		jlong size = env->GetArrayLength(image);
		jbyte* buffer = env->GetByteArrayElements(image, nullptr);
		Camera::pushCameraImage(size, (unsigned char *)buffer);
		env->ReleaseByteArrayElements(image, buffer, JNI_ABORT);
	}
}

// Call this under frameCommandLock.
static void ProcessFrameCommands(JNIEnv *env) {
	while (!frameCommands.empty()) {
		FrameCommand frameCmd;
		frameCmd = frameCommands.front();
		frameCommands.pop();

		INFO_LOG(Log::System, "frameCommand '%s' '%s'", frameCmd.command.c_str(), frameCmd.params.c_str());

		jstring cmd = env->NewStringUTF(frameCmd.command.c_str());
		jstring param = env->NewStringUTF(frameCmd.params.c_str());
		env->CallVoidMethod(nativeActivity, postCommand, cmd, param);
		env->DeleteLocalRef(cmd);
		env->DeleteLocalRef(param);
	}
}

std::thread g_renderLoopThread;

static void VulkanEmuThread(ANativeWindow *wnd);

// This runs in Vulkan mode only.
// This handles the entire lifecycle of the Vulkan context, init and exit.
extern "C" jboolean JNICALL Java_org_ppsspp_ppsspp_NativeActivity_runVulkanRenderLoop(JNIEnv * env, jobject obj, jobject _surf) {
	_assert_(!useCPUThread);

	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Already running");
		return false;
	}

	ANativeWindow *wnd = _surf ? ANativeWindow_fromSurface(env, _surf) : nullptr;

	if (!wnd) {
		// This shouldn't ever happen.
		ERROR_LOG(Log::G3D, "Error: Surface is null.");
		renderLoopRunning = false;
		return false;
	}

	g_renderLoopThread = std::thread(VulkanEmuThread, wnd);
	return true;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeActivity_requestExitVulkanRenderLoop(JNIEnv * env, jobject obj) {
	if (!renderLoopRunning) {
		ERROR_LOG(Log::System, "Render loop already exited");
		return;
	}
	_assert_(g_renderLoopThread.joinable());
	exitRenderLoop = true;
	g_renderLoopThread.join();
	_assert_(!g_renderLoopThread.joinable());
	g_renderLoopThread = std::thread();
}

// TODO: Merge with the Win32 EmuThread and so on, and the Java EmuThread?
static void VulkanEmuThread(ANativeWindow *wnd) {
	SetCurrentThreadName("EmuThread");

	AndroidJNIThreadContext ctx;
	JNIEnv *env = getEnv();

	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	if (exitRenderLoop) {
		WARN_LOG(Log::G3D, "runVulkanRenderLoop: ExitRenderLoop requested at start, skipping the whole thing.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	WARN_LOG(Log::G3D, "runVulkanRenderLoop. display_xres=%d display_yres=%d desiredBackbufferSizeX=%d desiredBackbufferSizeY=%d",
		display_xres, display_yres, desiredBackbufferSizeX, desiredBackbufferSizeY);

	if (!graphicsContext->InitFromRenderThread(wnd, desiredBackbufferSizeX, desiredBackbufferSizeY, backbuffer_format, androidVersion)) {
		// On Android, if we get here, really no point in continuing.
		// The UI is supposed to render on any device both on OpenGL and Vulkan. If either of those don't work
		// on a device, we blacklist it. Hopefully we should have already failed in InitAPI anyway and reverted to GL back then.
		ERROR_LOG(Log::G3D, "Failed to initialize graphics context.");
		System_Toast("Failed to initialize graphics context.");

		delete graphicsContext;
		graphicsContext = nullptr;
		renderLoopRunning = false;
		return;
	}

	if (!exitRenderLoop) {
		if (!NativeInitGraphics(graphicsContext)) {
			ERROR_LOG(Log::G3D, "Failed to initialize graphics.");
			// Gonna be in a weird state here..
		}
		graphicsContext->ThreadStart();
		renderer_inited = true;

		while (!exitRenderLoop) {
			{
				std::lock_guard<std::mutex> renderGuard(renderLock);
				NativeFrame(graphicsContext);
			}
			{
				std::lock_guard<std::mutex> guard(frameCommandLock);
				ProcessFrameCommands(env);
			}
		}
		INFO_LOG(Log::G3D, "Leaving Vulkan main loop.");
	} else {
		INFO_LOG(Log::G3D, "Not entering main loop.");
	}

	NativeShutdownGraphics();

	renderer_inited = false;
	graphicsContext->ThreadEnd();

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(Log::G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownFromRenderThread();
	renderLoopRunning = false;
	exitRenderLoop = false;

	WARN_LOG(Log::G3D, "Render loop function exited.");
}

// NOTE: This is defunct and not working, due to how the Android storage functions currently require
// a PpssppActivity specifically and we don't have one here.
extern "C" jstring Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameName(JNIEnv * env, jclass, jstring jpath) {
	bool teardownThreadManager = false;
	if (!g_threadManager.IsInitialized()) {
		INFO_LOG(Log::System, "No thread manager - initializing one");
		// Need a thread manager.
		teardownThreadManager = true;
		g_threadManager.Init(1, 1);
	}

	Path path = Path(GetJavaString(env, jpath));

	INFO_LOG(Log::System, "queryGameName(%s)", path.c_str());

	std::string result;

	GameInfoCache *cache = new GameInfoCache();
	std::shared_ptr<GameInfo> info = cache->GetInfo(nullptr, path, GameInfoFlags::PARAM_SFO);
	// Wait until it's done: this is synchronous, unfortunately.
	if (info) {
		INFO_LOG(Log::System, "GetInfo successful, waiting");
		while (!info->Ready(GameInfoFlags::PARAM_SFO)) {
			sleep_ms(1, "info-poll");
		}
		INFO_LOG(Log::System, "Done waiting");
		if (info->fileType != IdentifiedFileType::UNKNOWN) {
			result = info->GetTitle();

			// Pretty arbitrary, but the home screen will often truncate titles.
			// Let's remove "The " from names since it's common in English titles.
			if (result.length() > strlen("The ") && startsWithNoCase(result, "The ")) {
				result = result.substr(strlen("The "));
			}

			INFO_LOG(Log::System, "queryGameName: Got '%s'", result.c_str());
		} else {
			INFO_LOG(Log::System, "queryGameName: Filetype unknown");
		}
	} else {
		INFO_LOG(Log::System, "No info from cache");
	}
	delete cache;

	if (teardownThreadManager) {
		g_threadManager.Teardown();
	}

	return env->NewStringUTF(result.c_str());
}


extern "C"
JNIEXPORT jbyteArray JNICALL
Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameIcon(JNIEnv * env, jclass clazz, jstring jpath) {
	bool teardownThreadManager = false;
	if (!g_threadManager.IsInitialized()) {
		INFO_LOG(Log::System, "No thread manager - initializing one");
		// Need a thread manager.
		teardownThreadManager = true;
		g_threadManager.Init(1, 1);
	}
	// TODO: implement requestIcon()

	Path path = Path(GetJavaString(env, jpath));

	INFO_LOG(Log::System, "queryGameIcon(%s)", path.c_str());

	jbyteArray result = nullptr;

	GameInfoCache *cache = new GameInfoCache();
	std::shared_ptr<GameInfo> info = cache->GetInfo(nullptr, path, GameInfoFlags::ICON);
	// Wait until it's done: this is synchronous, unfortunately.
	if (info) {
		INFO_LOG(Log::System, "GetInfo successful, waiting");
        int attempts = 1000;
        while (!info->Ready(GameInfoFlags::ICON)) {
            sleep_ms(1, "icon-poll");
            attempts--;
            if (!attempts) {
                break;
            }
        }
        INFO_LOG(Log::System, "Done waiting");
        if (info->Ready(GameInfoFlags::ICON)) {
            if (!info->icon.data.empty()) {
                INFO_LOG(Log::System, "requestIcon: Got icon");
                result = env->NewByteArray((jsize)info->icon.data.size());
                env->SetByteArrayRegion(result, 0, (jsize)info->icon.data.size(), (const jbyte *)info->icon.data.data());
            }
        } else {
            INFO_LOG(Log::System, "requestIcon: Filetype unknown");
        }
    } else {
        INFO_LOG(Log::System, "No info from cache");
    }

    delete cache;

    if (teardownThreadManager) {
        g_threadManager.Teardown();
    }

    return result;
}
