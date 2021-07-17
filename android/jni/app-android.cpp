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

#include <android/log.h>

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

struct JNIEnv {};

#define JNIEXPORT
#define JNICALL
// Just a random value to make MSVC highlighting happy.
#define JNI_VERSION_1_6 16
#endif

#include "Common/Net/Resolve.h"
#include "android/jni/AndroidAudio.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/AssetReader.h"
#include "Common/File/AndroidStorage.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/Log.h"
#include "Common/GraphicsContext.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "AndroidGraphicsContext.h"
#include "AndroidVulkanContext.h"
#include "AndroidEGLContext.h"
#include "AndroidJavaGLContext.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Loaders.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/System.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/Host.h"
#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "UI/GameInfoCache.h"

#include "app-android.h"

bool useCPUThread = true;

// We turn this on now that when we target Android 11+.
// Along with adding:
//   android:preserveLegacyExternalStorage="true"
// To the already requested:
//   android:requestLegacyExternalStorage="true"
//
// This will cause Android 11+ to still behave like Android 10 until the app
// is manually uninstalled. We can detect this state with
// Android_IsExternalStoragePreservedLegacy(), but most of the app will just see
// that scoped storage enforcement is disabled in this case.

static const bool useScopedStorageIfRequired = true;

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

static std::thread emuThread;
static std::atomic<int> emuThreadState((int)EmuThreadState::DISABLED);

void UpdateRunLoopAndroid(JNIEnv *env);

AndroidAudioState *g_audioState;

struct FrameCommand {
	FrameCommand() {}
	FrameCommand(std::string cmd, std::string prm) : command(cmd), params(prm) {}

	std::string command;
	std::string params;
};

static std::mutex frameCommandLock;
static std::queue<FrameCommand> frameCommands;

std::string systemName;
std::string langRegion;
std::string mogaVersion;
std::string boardName;

std::string g_extFilesDir;

std::vector<std::string> g_additionalStorageDirs;

static float left_joystick_x_async;
static float left_joystick_y_async;
static float right_joystick_x_async;
static float right_joystick_y_async;
static float hat_joystick_x_async;
static float hat_joystick_y_async;

static int optimalFramesPerBuffer = 0;
static int optimalSampleRate = 0;
static int sampleRate = 0;
static int framesPerBuffer = 0;
static int androidVersion;
static int deviceType;

// Should only be used for display detection during startup (for config defaults etc)
// This is the ACTUAL display size, not the hardware scaled display size.
// Exposed so it can be displayed on the touchscreen test.
int display_xres;
int display_yres;
static int display_dpi_x;
static int display_dpi_y;
static int backbuffer_format;	// Android PixelFormat enum

static int desiredBackbufferSizeX;
static int desiredBackbufferSizeY;

// Cache the class loader so we can use it from native threads. Required for TextAndroid.
JavaVM* gJvm = nullptr;
static jobject gClassLoader;
static jmethodID gFindClassMethod;

static float g_safeInsetLeft = 0.0;
static float g_safeInsetRight = 0.0;
static float g_safeInsetTop = 0.0;
static float g_safeInsetBottom = 0.0;

static jmethodID postCommand;

static jobject nativeActivity;
static volatile bool exitRenderLoop;
static bool renderLoopRunning;
static int inputBoxSequence = 1;
std::map<int, std::function<void(bool, const std::string &)>> inputBoxCallbacks;

static float dp_xscale = 1.0f;
static float dp_yscale = 1.0f;

static bool renderer_inited = false;
static bool sustainedPerfSupported = false;
static std::mutex renderLock;

// See NativeQueryConfig("androidJavaGL") to change this value.
static bool javaGL = true;

static std::string library_path;
static std::map<SystemPermission, PermissionStatus> permissions;

AndroidGraphicsContext *graphicsContext;

#ifndef LOG_APP_NAME
#define LOG_APP_NAME "PPSSPP"
#endif

#ifdef _DEBUG
#define DLOG(...)    __android_log_print(ANDROID_LOG_INFO, LOG_APP_NAME, __VA_ARGS__);
#else
#define DLOG(...)
#endif

#define ILOG(...)    __android_log_print(ANDROID_LOG_INFO, LOG_APP_NAME, __VA_ARGS__);
#define WLOG(...)    __android_log_print(ANDROID_LOG_WARN, LOG_APP_NAME, __VA_ARGS__);
#define ELOG(...)    __android_log_print(ANDROID_LOG_ERROR, LOG_APP_NAME, __VA_ARGS__);
#define FLOG(...)    __android_log_print(ANDROID_LOG_FATAL, LOG_APP_NAME, __VA_ARGS__);

#define MessageBox(a, b, c, d) __android_log_print(ANDROID_LOG_INFO, APP_NAME, "%s %s", (b), (c));

void AndroidLogger::Log(const LogMessage &message) {
	// Log with simplified headers as Android already provides timestamp etc.
	switch (message.level) {
	case LogTypes::LVERBOSE:
	case LogTypes::LDEBUG:
	case LogTypes::LINFO:
		ILOG("[%s] %s", message.log, message.msg.c_str());
		break;
	case LogTypes::LERROR:
		ELOG("[%s] %s", message.log, message.msg.c_str());
		break;
	case LogTypes::LWARNING:
		WLOG("[%s] %s", message.log, message.msg.c_str());
		break;
	case LogTypes::LNOTICE:
	default:
		ILOG("[%s] !!! %s", message.log, message.msg.c_str());
		break;
	}
}

JNIEnv* getEnv() {
	JNIEnv *env;
	int status = gJvm->GetEnv((void**)&env, JNI_VERSION_1_6);
	if(status < 0) {
		status = gJvm->AttachCurrentThread(&env, NULL);
		if(status < 0) {
			return nullptr;
		}
	}
	return env;
}

jclass findClass(const char* name) {
	return static_cast<jclass>(getEnv()->CallObjectMethod(gClassLoader, gFindClassMethod, getEnv()->NewStringUTF(name)));
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *pjvm, void *reserved) {
	INFO_LOG(SYSTEM, "JNI_OnLoad");
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
	return JNI_VERSION_1_6;
}

static void EmuThreadFunc() {
	JNIEnv *env;
	gJvm->AttachCurrentThread(&env, nullptr);

	SetCurrentThreadName("Emu");
	INFO_LOG(SYSTEM, "Entering emu thread");

	// Wait for render loop to get started.
	if (!graphicsContext || !graphicsContext->Initialized()) {
		INFO_LOG(SYSTEM, "Runloop: Waiting for displayInit...");
		while (!graphicsContext || !graphicsContext->Initialized()) {
			sleep_ms(20);
		}
	} else {
		INFO_LOG(SYSTEM, "Runloop: Graphics context available! %p", graphicsContext);
	}
	NativeInitGraphics(graphicsContext);

	INFO_LOG(SYSTEM, "Graphics initialized. Entering loop.");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;
	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		UpdateRunLoopAndroid(env);
	}
	INFO_LOG(SYSTEM, "QUIT_REQUESTED found, left loop. Setting state to STOPPED.");
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();

	// Also ask the main thread to stop, so it doesn't hang waiting for a new frame.
	graphicsContext->StopThread();

	gJvm->DetachCurrentThread();
	INFO_LOG(SYSTEM, "Leaving emu thread");
}

static void EmuThreadStart() {
	INFO_LOG(SYSTEM, "EmuThreadStart");
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc);
}

// Call EmuThreadStop first, then keep running the GPU (or eat commands)
// as long as emuThreadState isn't STOPPED and/or there are still things queued up.
// Only after that, call EmuThreadJoin.
static void EmuThreadStop(const char *caller) {
	INFO_LOG(SYSTEM, "EmuThreadStop - stopping (%s)...", caller);
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
	INFO_LOG(SYSTEM, "EmuThreadJoin - joined");
}

static void ProcessFrameCommands(JNIEnv *env);

void PushCommand(std::string cmd, std::string param) {
	std::lock_guard<std::mutex> guard(frameCommandLock);
	frameCommands.push(FrameCommand(cmd, param));
}

// Android implementation of callbacks to the Java part of the app
void SystemToast(const char *text) {
	PushCommand("toast", text);
}

void ShowKeyboard() {
	PushCommand("showKeyboard", "");
}

void Vibrate(int length_ms) {
	char temp[32];
	sprintf(temp, "%i", length_ms);
	PushCommand("vibrate", temp);
}

void OpenDirectory(const char *path) {
	// Unsupported
}

void LaunchBrowser(const char *url) {
	PushCommand("launchBrowser", url);
}

void LaunchMarket(const char *url) {
	PushCommand("launchMarket", url);
}

void LaunchEmail(const char *email_address) {
	PushCommand("launchEmail", email_address);
}

void System_SendMessage(const char *command, const char *parameter) {
	PushCommand(command, parameter);
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
		return std::vector<std::string>();
	}
}

int System_GetPropertyInt(SystemProperty prop) {
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
		return display_hz;
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
	case SYSPROP_HAS_ADDITIONAL_STORAGE:
		return !g_additionalStorageDirs.empty();
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return true;
	case SYSPROP_HAS_FILE_BROWSER:
		// It's only really needed with scoped storage, but why not make it available
		// as far back as possible - works just fine.
		return androidVersion >= 19;  // when ACTION_OPEN_DOCUMENT was added
	case SYSPROP_HAS_FOLDER_BROWSER:
		// Uses OPEN_DOCUMENT_TREE to let you select a folder.
		return androidVersion >= 21;  // when ACTION_OPEN_DOCUMENT_TREE was added
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_ANDROID_SCOPED_STORAGE:
		if (useScopedStorageIfRequired && androidVersion >= 28) {
			// Here we do a check to see if we ended up in the preserveLegacyExternalStorage path.
			// That won't last if the user uninstalls/reinstalls though, but would preserve the user
			// experience for simple upgrades so maybe let's support it.
			return !Android_IsExternalStoragePreservedLegacy();
		} else {
			return false;
		}
	default:
		return false;
	}
}

std::string GetJavaString(JNIEnv *env, jstring jstr) {
	if (!jstr)
		return "";
	const char *str = env->GetStringUTFChars(jstr, 0);
	std::string cpp_string = std::string(str);
	env->ReleaseStringUTFChars(jstr, str);
	return cpp_string;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeActivity_registerCallbacks(JNIEnv *env, jobject obj) {
	nativeActivity = env->NewGlobalRef(obj);
	postCommand = env->GetMethodID(env->GetObjectClass(obj), "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
	_dbg_assert_(postCommand);

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

extern "C" jstring Java_org_ppsspp_ppsspp_NativeApp_queryConfig
	(JNIEnv *env, jclass, jstring jquery) {
	std::string query = GetJavaString(env, jquery);
	std::string result = NativeQueryConfig(query);
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
	(JNIEnv *env, jclass, jstring jmodel, jint jdeviceType, jstring jlangRegion, jstring japkpath,
		jstring jdataDir, jstring jexternalStorageDir, jstring jexternalFilesDir, jstring jadditionalStorageDirs, jstring jlibraryDir, jstring jcacheDir, jstring jshortcutParam,
		jint jAndroidVersion, jstring jboard) {
	SetCurrentThreadName("androidInit");

	// Makes sure we get early permission grants.
	ProcessFrameCommands(env);

	EARLY_LOG("NativeApp.init() -- begin");
	PROFILE_INIT();

	std::lock_guard<std::mutex> guard(renderLock);
	renderer_inited = false;
	androidVersion = jAndroidVersion;
	deviceType = jdeviceType;

	left_joystick_x_async = 0;
	left_joystick_y_async = 0;
	right_joystick_x_async = 0;
	right_joystick_y_async = 0;
	hat_joystick_x_async = 0;
	hat_joystick_y_async = 0;

	std::string apkPath = GetJavaString(env, japkpath);
	VFSRegister("", new ZipAssetReader(apkPath.c_str(), "assets/"));

	systemName = GetJavaString(env, jmodel);
	langRegion = GetJavaString(env, jlangRegion);

	EARLY_LOG("NativeApp.init(): device name: '%s'", systemName.c_str());

	std::string externalStorageDir = GetJavaString(env, jexternalStorageDir);
	std::string additionalStorageDirsString = GetJavaString(env, jadditionalStorageDirs);
	std::string externalFilesDir = GetJavaString(env, jexternalFilesDir);

	g_extFilesDir = externalFilesDir;

	if (!additionalStorageDirsString.empty()) {
		SplitString(additionalStorageDirsString, ':', g_additionalStorageDirs);
		for (auto &str : g_additionalStorageDirs) {
			EARLY_LOG("Additional storage: %s", str.c_str());
		}
	}

	std::string user_data_path = GetJavaString(env, jdataDir);
	if (user_data_path.size() > 0)
		user_data_path += "/";
	library_path = GetJavaString(env, jlibraryDir) + "/";
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
		parse_args(temp, shortcut_param);
		for (const auto &arg : temp) {
			args.push_back(arg.c_str());
		}
	}

	NativeInit((int)args.size(), &args[0], user_data_path.c_str(), externalStorageDir.c_str(), cacheDir.c_str());

	// No need to use EARLY_LOG anymore.

retry:
	// Now that we've loaded config, set javaGL.
	javaGL = NativeQueryConfig("androidJavaGL") == "true";

	switch (g_Config.iGPUBackend) {
	case (int)GPUBackend::OPENGL:
		useCPUThread = true;
		if (javaGL) {
			INFO_LOG(SYSTEM, "NativeApp.init() -- creating OpenGL context (JavaGL)");
			graphicsContext = new AndroidJavaEGLGraphicsContext();
		} else {
			graphicsContext = new AndroidEGLGraphicsContext();
		}
		break;
	case (int)GPUBackend::VULKAN:
	{
		INFO_LOG(SYSTEM, "NativeApp.init() -- creating Vulkan context");
		useCPUThread = false;  // The Vulkan render manager manages its own thread.
		// We create and destroy the Vulkan graphics context in the "EGL" thread.
		AndroidVulkanContext *ctx = new AndroidVulkanContext();
		if (!ctx->InitAPI()) {
			INFO_LOG(SYSTEM, "Failed to initialize Vulkan, switching to OpenGL");
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend(GPUBackend::OPENGL);
			goto retry;
		} else {
			graphicsContext = ctx;
		}
		break;
	}
	default:
		ERROR_LOG(SYSTEM, "NativeApp.init(): iGPUBackend %d not supported. Switching to OpenGL.", (int)g_Config.iGPUBackend);
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
		goto retry;
	}

	if (useCPUThread) {
		INFO_LOG(SYSTEM, "NativeApp.init() - launching emu thread");
		EmuThreadStart();
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

	INFO_LOG(AUDIO, "NativeApp.audioInit() -- Using OpenSL audio! frames/buffer: %i	 optimal sr: %i	 actual sr: %i", optimalFramesPerBuffer, optimalSampleRate, sampleRate);
	if (!g_audioState) {
		g_audioState = AndroidAudio_Init(&NativeMix, framesPerBuffer, sampleRate);
	} else {
		ERROR_LOG(AUDIO, "Audio state already initialized");
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioShutdown(JNIEnv *, jclass) {
	if (g_audioState) {
		AndroidAudio_Shutdown(g_audioState);
		g_audioState = nullptr;
	} else {
		ERROR_LOG(AUDIO, "Audio state already shutdown!");
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

bool audioRecording_Available() {
	return true;
}

bool audioRecording_State() {
	return AndroidAudio_Recording_State(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_resume(JNIEnv *, jclass) {
	INFO_LOG(SYSTEM, "NativeApp.resume() - resuming audio");
	AndroidAudio_Resume(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_pause(JNIEnv *, jclass) {
	INFO_LOG(SYSTEM, "NativeApp.pause() - pausing audio");
	AndroidAudio_Pause(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_shutdown(JNIEnv *, jclass) {
	if (renderer_inited && useCPUThread && graphicsContext) {
		// Only used in Java EGL path.
		EmuThreadStop("shutdown");
		INFO_LOG(SYSTEM, "BeginAndroidShutdown");
		graphicsContext->BeginAndroidShutdown();
		// Skipping GL calls, the old context is gone.
		while (graphicsContext->ThreadFrame()) {
			INFO_LOG(SYSTEM, "graphicsContext->ThreadFrame executed to clear buffers");
		}
		INFO_LOG(SYSTEM, "Joining emuthread");
		EmuThreadJoin();
		INFO_LOG(SYSTEM, "Joined emuthread");

		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();
		INFO_LOG(SYSTEM, "Graphics context now shut down from NativeApp_shutdown");
	}

	INFO_LOG(SYSTEM, "NativeApp.shutdown() -- begin");
	if (renderer_inited) {
		INFO_LOG(G3D, "Shutting down renderer");
		// This will be from the wrong thread? :/
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = nullptr;
		renderer_inited = false;
	} else {
		INFO_LOG(G3D, "Not shutting down renderer - not initialized");
	}

	{
		std::lock_guard<std::mutex> guard(renderLock);
		inputBoxCallbacks.clear();
		NativeShutdown();
		VFSShutdown();
	}

	std::lock_guard<std::mutex> guard(frameCommandLock);
	while (frameCommands.size())
		frameCommands.pop();
	INFO_LOG(SYSTEM, "NativeApp.shutdown() -- end");
}

// JavaEGL
extern "C" bool Java_org_ppsspp_ppsspp_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
	// We should be running on the render thread here.
	std::string errorMessage;
	if (renderer_inited) {
		// Would be really nice if we could get something on the GL thread immediately when shutting down.
		INFO_LOG(G3D, "NativeApp.displayInit() restoring");
		if (useCPUThread) {
			EmuThreadStop("displayInit");
			graphicsContext->BeginAndroidShutdown();
			INFO_LOG(G3D, "BeginAndroidShutdown. Looping until emu thread done...");
			// Skipping GL calls here because the old context is lost.
			while (graphicsContext->ThreadFrame()) {
				continue;
			}
			INFO_LOG(G3D, "Joining emu thread");
			EmuThreadJoin();
		} else {
			NativeShutdownGraphics();
		}
		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();

		INFO_LOG(G3D, "Shut down both threads. Now let's bring it up again!");

		if (!graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0)) {
			SystemToast("Graphics initialization failed. Quitting.");
			return false;
		}

		graphicsContext->GetDrawContext()->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
			host->NotifyUserMessage(details, 5.0, 0xFFFFFFFF, "error_callback");
		}, nullptr);

		if (useCPUThread) {
			EmuThreadStart();
		} else {
			if (!NativeInitGraphics(graphicsContext)) {
				// Gonna be in a weird state here, not good.
				SystemToast("Failed to initialize graphics.");
				return false;
			}
		}

		graphicsContext->ThreadStart();
		INFO_LOG(G3D, "Restored.");
	} else {
		INFO_LOG(G3D, "NativeApp.displayInit() first time");
		if (!graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0)) {
			SystemToast("Graphics initialization failed. Quitting.");
			return false;
		}

		graphicsContext->GetDrawContext()->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
			host->NotifyUserMessage(details, 5.0, 0xFFFFFFFF, "error_callback");
		}, nullptr);

		graphicsContext->ThreadStart();
		renderer_inited = true;
	}
	NativeMessageReceived("recreateviews", "");
	return true;
}

static void recalculateDpi() {
	g_dpi = display_dpi_x;
	g_dpi_scale_x = 240.0f / display_dpi_x;
	g_dpi_scale_y = 240.0f / display_dpi_y;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;

	dp_xres = display_xres * g_dpi_scale_x;
	dp_yres = display_yres * g_dpi_scale_y;

	// Touch scaling is from display pixels to dp pixels.
	// Wait, doesn't even make sense... this is equal to g_dpi_scale_x. TODO: Figure out what's going on!
	dp_xscale = (float)dp_xres / (float)display_xres;
	dp_yscale = (float)dp_yres / (float)display_yres;

	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;

	INFO_LOG(G3D, "RecalcDPI: display_xres=%d display_yres=%d", display_xres, display_yres);
	INFO_LOG(G3D, "RecalcDPI: g_dpi=%f g_dpi_scale_x=%f g_dpi_scale_y=%f", g_dpi, g_dpi_scale_x, g_dpi_scale_y);
	INFO_LOG(G3D, "RecalcDPI: dp_xscale=%f dp_yscale=%f", dp_xscale, dp_yscale);
	INFO_LOG(G3D, "RecalcDPI: dp_xres=%d dp_yres=%d", dp_xres, dp_yres);
	INFO_LOG(G3D, "RecalcDPI: pixel_xres=%d pixel_yres=%d", pixel_xres, pixel_yres);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_backbufferResize(JNIEnv *, jclass, jint bufw, jint bufh, jint format) {
	INFO_LOG(SYSTEM, "NativeApp.backbufferResize(%d x %d)", bufw, bufh);

	bool new_size = pixel_xres != bufw || pixel_yres != bufh;
	int old_w = pixel_xres;
	int old_h = pixel_yres;
	// pixel_*res is the backbuffer resolution.
	pixel_xres = bufw;
	pixel_yres = bufh;
	backbuffer_format = format;

	recalculateDpi();

	if (new_size) {
		INFO_LOG(G3D, "Size change detected (previously %d,%d) - calling NativeResized()", old_w, old_h);
		NativeResized();
	} else {
		INFO_LOG(G3D, "NativeApp::backbufferResize: Size didn't change.");
	}
}

void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb) {
	int seq = inputBoxSequence++;
	inputBoxCallbacks[seq] = cb;

	std::string serialized = StringFromFormat("%d:@:%s:@:%s", seq, title.c_str(), defaultValue.c_str());
	System_SendMessage("inputbox", serialized.c_str());
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_sendInputBox(JNIEnv *env, jclass, jstring jseqID, jboolean result, jstring jvalue) {
	std::string seqID = GetJavaString(env, jseqID);
	std::string value = GetJavaString(env, jvalue);

	static std::string lastSeqID = "";
	if (lastSeqID == seqID) {
		// We send this on dismiss, so twice in many cases.
		DEBUG_LOG(SYSTEM, "Ignoring duplicate sendInputBox");
		return;
	}
	lastSeqID = seqID;

	int seq = 0;
	if (!TryParse(seqID, &seq)) {
		ERROR_LOG(SYSTEM, "Invalid inputbox seqID value: %s", seqID.c_str());
		return;
	}

	auto entry = inputBoxCallbacks.find(seq);
	if (entry == inputBoxCallbacks.end()) {
		ERROR_LOG(SYSTEM, "Did not find inputbox callback for %s, shutdown?", seqID.c_str());
		return;
	}

	NativeInputBoxReceived(entry->second, result, value);
}

void LockedNativeUpdateRender() {
	std::lock_guard<std::mutex> renderGuard(renderLock);
	NativeUpdate();
	NativeRender(graphicsContext);
}

void UpdateRunLoopAndroid(JNIEnv *env) {
	LockedNativeUpdateRender();

	std::lock_guard<std::mutex> guard(frameCommandLock);
	if (!nativeActivity) {
		while (!frameCommands.empty())
			frameCommands.pop();
		return;
	}
	// Still under lock here.
	ProcessFrameCommands(env);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayRender(JNIEnv *env, jobject obj) {
	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		hasSetThreadName = true;
		SetCurrentThreadName("AndroidRender");
	}

	if (useCPUThread) {
		// This is the "GPU thread".
		if (graphicsContext)
			graphicsContext->ThreadFrame();
	} else {
		UpdateRunLoopAndroid(env);
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

extern "C" jboolean JNICALL Java_org_ppsspp_ppsspp_NativeApp_touch
	(JNIEnv *, jclass, float x, float y, int code, int pointerId) {

	float scaledX = x * dp_xscale;
	float scaledY = y * dp_yscale;

	TouchInput touch;
	touch.id = pointerId;
	touch.x = scaledX;
	touch.y = scaledY;
	touch.flags = code;

	bool retval = NativeTouch(touch);
	return retval;
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_keyDown(JNIEnv *, jclass, jint deviceId, jint key, jboolean isRepeat) {
	KeyInput keyInput;
	keyInput.deviceId = deviceId;
	keyInput.keyCode = key;
	keyInput.flags = KEY_DOWN;
	if (isRepeat) {
		keyInput.flags |= KEY_IS_REPEAT;
	}
	return NativeKey(keyInput);
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_keyUp(JNIEnv *, jclass, jint deviceId, jint key) {
	KeyInput keyInput;
	keyInput.deviceId = deviceId;
	keyInput.keyCode = key;
	keyInput.flags = KEY_UP;
	return NativeKey(keyInput);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_beginJoystickEvent(
	JNIEnv *env, jclass) {
	// mutex lock?
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_joystickAxis(
		JNIEnv *env, jclass, jint deviceId, jint axisId, jfloat value) {
	if (!renderer_inited)
		return false;
	switch (axisId) {
	case JOYSTICK_AXIS_X:
		left_joystick_x_async = value;
		break;
	case JOYSTICK_AXIS_Y:
		left_joystick_y_async = -value;
		break;
	case JOYSTICK_AXIS_Z:
		right_joystick_x_async = value;
		break;
	case JOYSTICK_AXIS_RZ:
		right_joystick_y_async = -value;
		break;
	case JOYSTICK_AXIS_HAT_X:
		hat_joystick_x_async = value;
		break;
	case JOYSTICK_AXIS_HAT_Y:
		hat_joystick_y_async = -value;
		break;
	}

	AxisInput axis;
	axis.axisId = axisId;
	axis.deviceId = deviceId;
	axis.value = value;

	return NativeAxis(axis);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_endJoystickEvent(
	JNIEnv *env, jclass) {
	// mutex unlock?
}


extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_mouseWheelEvent(
	JNIEnv *env, jclass, jint stick, jfloat x, jfloat y) {
	// TODO: Support mousewheel for android
	return true;
}

extern "C" jboolean JNICALL Java_org_ppsspp_ppsspp_NativeApp_accelerometer(JNIEnv *, jclass, float x, float y, float z) {
	if (!renderer_inited)
		return false;

	AxisInput axis;
	axis.deviceId = DEVICE_ID_ACCELEROMETER;
	axis.flags = 0;

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_X;
	axis.value = x;
	bool retvalX = NativeAxis(axis);

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Y;
	axis.value = y;
	bool retvalY = NativeAxis(axis);

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Z;
	axis.value = z;
	bool retvalZ = NativeAxis(axis);

	return retvalX || retvalY || retvalZ;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_sendMessage(JNIEnv *env, jclass, jstring message, jstring param) {
	std::string msg = GetJavaString(env, message);
	std::string prm = GetJavaString(env, param);

	// Some messages are caught by app-android.
	if (msg == "moga") {
		mogaVersion = prm;
	} else if (msg == "permission_pending") {
		INFO_LOG(SYSTEM, "STORAGE PERMISSION: PENDING");
		// TODO: Add support for other permissions
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_PENDING;
	} else if (msg == "permission_denied") {
		INFO_LOG(SYSTEM, "STORAGE PERMISSION: DENIED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_DENIED;
	} else if (msg == "permission_granted") {
		INFO_LOG(SYSTEM, "STORAGE PERMISSION: GRANTED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_GRANTED;
	} else if (msg == "sustained_perf_supported") {
		sustainedPerfSupported = true;
	} else if (msg == "safe_insets") {
		INFO_LOG(SYSTEM, "Got insets: %s", prm.c_str());
		// We don't bother with supporting exact rectangular regions. Safe insets are good enough.
		int left, right, top, bottom;
		if (4 == sscanf(prm.c_str(), "%d:%d:%d:%d", &left, &right, &top, &bottom)) {
			g_safeInsetLeft = (float)left * g_dpi_scale_x;
			g_safeInsetRight = (float)right * g_dpi_scale_x;
			g_safeInsetTop = (float)top * g_dpi_scale_y;
			g_safeInsetBottom = (float)bottom * g_dpi_scale_y;
		}
	}

	// Ensures that the receiver can handle it on a sensible thread.
	NativeMessageReceived(msg.c_str(), prm.c_str());
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeActivity_exitEGLRenderLoop(JNIEnv *env, jobject obj) {
	if (!renderLoopRunning) {
		ERROR_LOG(SYSTEM, "Render loop already exited");
		return;
	}
	exitRenderLoop = true;
	while (renderLoopRunning) {
		sleep_ms(10);
	}
}

void correctRatio(int &sz_x, int &sz_y, float scale) {
	float x = (float)sz_x;
	float y = (float)sz_y;
	float ratio = x / y;
	INFO_LOG(G3D, "CorrectRatio: Considering size: %0.2f/%0.2f=%0.2f for scale %f", x, y, ratio, scale);
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
	INFO_LOG(G3D, "Target ratio: %0.2f ratio: %0.2f correction: %0.2f", targetRatio, ratio, correction);
	if (ratio < targetRatio) {
		y *= correction;
	} else {
		x /= correction;
	}

	sz_x = x;
	sz_y = y;
	INFO_LOG(G3D, "Corrected ratio: %dx%d", sz_x, sz_y);
}

void getDesiredBackbufferSize(int &sz_x, int &sz_y) {
	sz_x = display_xres;
	sz_y = display_yres;
	std::string config = NativeQueryConfig("hwScale");
	int scale;
	if (1 == sscanf(config.c_str(), "%d", &scale) && scale > 0) {
		correctRatio(sz_x, sz_y, scale);
	} else {
		sz_x = 0;
		sz_y = 0;
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_setDisplayParameters(JNIEnv *, jclass, jint xres, jint yres, jint dpi, jfloat refreshRate) {
	INFO_LOG(G3D, "NativeApp.setDisplayParameters(%d x %d, dpi=%d, refresh=%0.2f)", xres, yres, dpi, refreshRate);
	bool changed = false;
	changed = changed || display_xres != xres || display_yres != yres;
	changed = changed || display_dpi_x != dpi || display_dpi_y != dpi;
	changed = changed || display_hz != refreshRate;

	if (changed) {
		display_xres = xres;
		display_yres = yres;
		display_dpi_x = dpi;
		display_dpi_y = dpi;
		display_hz = refreshRate;

		recalculateDpi();
		NativeResized();
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_computeDesiredBackbufferDimensions() {
	getDesiredBackbufferSize(desiredBackbufferSizeX, desiredBackbufferSizeY);
}

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_getDesiredBackbufferWidth(JNIEnv *, jclass) {
	return desiredBackbufferSizeX;
}

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_getDesiredBackbufferHeight(JNIEnv *, jclass) {
	return desiredBackbufferSizeY;
}

std::vector<std::string> __cameraGetDeviceList() {
	jclass cameraClass = findClass("org/ppsspp/ppsspp/CameraHelper");
	jmethodID deviceListMethod = getEnv()->GetStaticMethodID(cameraClass, "getDeviceList", "()Ljava/util/ArrayList;");
	jobject deviceListObject = getEnv()->CallStaticObjectMethod(cameraClass, deviceListMethod);
	jclass arrayListClass = getEnv()->FindClass("java/util/ArrayList");
	jmethodID arrayListSize = getEnv()->GetMethodID(arrayListClass, "size", "()I");
	jmethodID arrayListGet = getEnv()->GetMethodID(arrayListClass, "get", "(I)Ljava/lang/Object;");

	jint arrayListObjectLen = getEnv()->CallIntMethod(deviceListObject, arrayListSize);
	std::vector<std::string> deviceListVector;

	for (int i=0; i < arrayListObjectLen; i++) {
		jstring dev = static_cast<jstring>(getEnv()->CallObjectMethod(deviceListObject, arrayListGet, i));
		const char* cdev = getEnv()->GetStringUTFChars(dev, nullptr);
		deviceListVector.push_back(cdev);
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

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_pushCameraImageAndroid(JNIEnv *env, jclass,
		jbyteArray image) {

	if (image != NULL) {
		jlong size = env->GetArrayLength(image);
		jbyte* buffer = env->GetByteArrayElements(image, NULL);
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

		INFO_LOG(SYSTEM, "frameCommand '%s' '%s'", frameCmd.command.c_str(), frameCmd.params.c_str());

		jstring cmd = env->NewStringUTF(frameCmd.command.c_str());
		jstring param = env->NewStringUTF(frameCmd.params.c_str());
		env->CallVoidMethod(nativeActivity, postCommand, cmd, param);
		env->DeleteLocalRef(cmd);
		env->DeleteLocalRef(param);
	}
}

extern "C" bool JNICALL Java_org_ppsspp_ppsspp_NativeActivity_runEGLRenderLoop(JNIEnv *env, jobject obj, jobject _surf) {
	if (!graphicsContext) {
		ERROR_LOG(G3D, "runEGLRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	// Needed for Vulkan, even if we're not using the old EGL path.

	exitRenderLoop = false;
	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	ANativeWindow *wnd = _surf ? ANativeWindow_fromSurface(env, _surf) : nullptr;

	WARN_LOG(G3D, "runEGLRenderLoop. display_xres=%d display_yres=%d", display_xres, display_yres);

	if (wnd == nullptr) {
		ERROR_LOG(G3D, "Error: Surface is null.");
		renderLoopRunning = false;
		return false;
	}

	auto tryInit = [&]() {
		if (graphicsContext->InitFromRenderThread(wnd, desiredBackbufferSizeX, desiredBackbufferSizeY, backbuffer_format, androidVersion)) {
			return true;
		} else {
			ERROR_LOG(G3D, "Failed to initialize graphics context.");
			SystemToast("Failed to initialize graphics context.");
			return false;
		}
	};

	bool initSuccess = tryInit();
	if (!initSuccess) {
		if (!exitRenderLoop && g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
			INFO_LOG(G3D, "Trying again, this time with OpenGL.");
			SetGPUBackend(GPUBackend::OPENGL);
			g_Config.iGPUBackend = (int)GetGPUBackend();

			// If we were still supporting EGL for GL, we'd retry here:
			//initSuccess = tryInit();
		}

		if (!initSuccess) {
			delete graphicsContext;
			graphicsContext = nullptr;
			renderLoopRunning = false;
			return false;
		}
	}

	if (!exitRenderLoop) {
		if (!useCPUThread) {
			if (!NativeInitGraphics(graphicsContext)) {
				ERROR_LOG(G3D, "Failed to initialize graphics.");
				// Gonna be in a weird state here..
			}
		}
		graphicsContext->ThreadStart();
		renderer_inited = true;
	}

	if (!exitRenderLoop) {
		static bool hasSetThreadName = false;
		if (!hasSetThreadName) {
			hasSetThreadName = true;
			SetCurrentThreadName("AndroidRender");
		}
	}

	if (useCPUThread) {
		ERROR_LOG(SYSTEM, "Running graphics loop");
		while (!exitRenderLoop) {
			// This is the "GPU thread".
			graphicsContext->ThreadFrame();
			graphicsContext->SwapBuffers();
		}
	} else {
		while (!exitRenderLoop) {
			LockedNativeUpdateRender();
			graphicsContext->SwapBuffers();

			ProcessFrameCommands(env);
		}
	}

	INFO_LOG(G3D, "Leaving EGL/Vulkan render loop.");

	if (useCPUThread) {
		EmuThreadStop("exitrenderloop");
		while (graphicsContext->ThreadFrame()) {
			continue;
		}
		EmuThreadJoin();
	} else {
		NativeShutdownGraphics();
	}
	renderer_inited = false;
	graphicsContext->ThreadEnd();

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(G3D, "Shutting down graphics context from render thread...");
	graphicsContext->ShutdownFromRenderThread();
	renderLoopRunning = false;
	WARN_LOG(G3D, "Render loop function exited.");
	return true;
}

extern "C" jstring Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameName(JNIEnv *env, jclass, jstring jpath) {
	Path path = Path(GetJavaString(env, jpath));
	std::string result = "";

	GameInfoCache *cache = new GameInfoCache();
	std::shared_ptr<GameInfo> info = cache->GetInfo(nullptr, path, 0);
	// Wait until it's done: this is synchronous, unfortunately.
	if (info) {
		cache->WaitUntilDone(info);
		if (info->fileType != IdentifiedFileType::UNKNOWN) {
			result = info->GetTitle();

			// Pretty arbitrary, but the home screen will often truncate titles.
			// Let's remove "The " from names since it's common in English titles.
			if (result.length() > strlen("The ") && startsWithNoCase(result, "The ")) {
				result = result.substr(strlen("The "));
			}
		}
	}
	delete cache;

	return env->NewStringUTF(result.c_str());
}
