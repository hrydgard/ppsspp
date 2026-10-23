// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgard (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include <map>
#include <sstream>
#include <queue>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>

#ifndef _MSC_VER

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#include "Common/Render/Text/draw_text_android.h"

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
#include "Common/Net/URL.h"
#include "android/jni/AndroidAudio.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/AndroidStorage.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "android/jni/AndroidEGLGraphicsContext.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "Core/CmdLine.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Loaders.h"
#include "Core/KeyMap.h"
#include "Core/Util/RecentFiles.h"
#include "Core/System.h"
#include "Core/EmuThread.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Common/CPUDetect.h"
#include "UI/GameInfoCache.h"

#include "app-android.h"

AndroidAudioState *g_audioState;

struct FrameCommand {
	FrameCommand() = default;
	FrameCommand(std::string_view cmd, std::string_view param, int reqId) : command(cmd), params(param), requestId(reqId) {}
	std::string command;
	std::string params;
	// The request waiting for an answer to this command, or -1. Failed if the command can't be delivered.
	int requestId = -1;
};

static std::mutex frameCommandLock;
static std::vector<FrameCommand> g_frameCommands;

static std::string systemName;
static std::string langRegion;
static std::string boardName;
// The name the user gave the device (like "Galaxy S26"). Set from the UI thread.
static std::mutex g_deviceNameLock;
static std::string g_deviceName;

std::string g_externalDir;  // Original external dir (root of Android storage).
std::string g_extFilesDir;  // App private external dir.

static std::vector<std::string> g_additionalStorageDirs;

static int optimalFramesPerBuffer = 0;
static int optimalSampleRate = 0;
static int sampleRate = 0;
static int framesPerBuffer = 0;
static int androidVersion;
static int deviceType;

// This is the ACTUAL display size, not the hardware scaled display size.
static int display_xres;
static int display_yres;
static int display_dpi;
static float display_scale_x;  // Scale factor due to backbuffer scaling
static float display_scale_y;

static int desiredBackbufferSizeX;
static int desiredBackbufferSizeY;

// Cache the class loader so we can use it from native threads. Required for TextAndroid.
extern JavaVM *gJvm;

static jobject gClassLoader;
static jmethodID gFindClassMethod;

// PpssppActivity methods, looked up in JNI_OnLoad.
static jmethodID postCommand;
static jmethodID getDebugString;
static jmethodID getNativeCrashHistory;

static float g_safeInsetLeft = 0.0;
static float g_safeInsetRight = 0.0;
static float g_safeInsetTop = 0.0;
static float g_safeInsetBottom = 0.0;
static bool g_hasCameraCutout = false;
static bool g_hasAccelerometer = false;
static bool g_hasGyro = false;

// Global ref. Written on the UI thread under g_activityLock. Other threads go through Android_GetActivity.
static jobject ppssppActivity;
static std::mutex g_activityLock;

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;

// Set on the render thread (GL) or the emu thread (Vulkan), read by the input functions on the UI thread.
static std::atomic<bool> renderer_inited{false};

static bool sustainedPerfSupported = false;
static std::string g_installerName;

// Written on the Java UI thread, read on the render thread.
static std::mutex permissionsLock;
static std::map<SystemPermission, PermissionStatus> permissions;

// The name is the one System_AskForPermission sends.
static void SetPermissionStatusFromJava(std::string_view name, PermissionStatus status) {
	SystemPermission permission;
	if (name == "storage") {
		permission = SYSTEM_PERMISSION_STORAGE;
	} else if (name == "local_network") {
		permission = SYSTEM_PERMISSION_LOCAL_NETWORK;
	} else {
		WARN_LOG(Log::System, "Status %d for unknown permission '%.*s'", (int)status, STR_VIEW(name));
		return;
	}
	INFO_LOG(Log::System, "Permission '%.*s': status %d", STR_VIEW(name), (int)status);
	std::lock_guard<std::mutex> guard(permissionsLock);
	permissions[permission] = status;
}

static GraphicsContext *graphicsContext;

// The latest surface size from backbufferResize, width in the high half, for Vulkan to check the surface's extent
// against. Atomic rather than under g_pendingResizeLock (below), since a resize applied directly can recreate the swapchain.
static std::atomic<uint64_t> g_surfaceSize;

#if PPSSPP_ARCH(ARMV7)
// Old Android workaround
extern "C" {
int utimensat(int fd, const char *path, const struct timespec times[2]) {
	return -1;
}
}
#endif

static void ProcessFrameCommands();

JNIEnv* getEnv() {
	JNIEnv *env;
	int status = gJvm->GetEnv((void**)&env, JNI_VERSION_1_6);
	_assert_msg_(status >= 0, "'%s': Can only call getEnv if you've attached the thread already!", GetCurrentThreadName());
	return env;
}

// Returns a local reference.
jclass findClass(const char* name) {
	JNIEnv *env = getEnv();
	jstring jname = env->NewStringUTF(name);
	jclass cls = static_cast<jclass>(env->CallObjectMethod(gClassLoader, gFindClassMethod, jname));
	env->DeleteLocalRef(jname);
	return cls;
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
			ERROR_LOG(Log::System, "Failed to attach thread %s to JNI.", GetCurrentThreadName());
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
	// Method IDs stay valid for as long as the class is loaded, which is the life of the process.
	jclass activityClass = env->FindClass("org/ppsspp/ppsspp/PpssppActivity");
	jclass classClass = env->GetObjectClass(activityClass);
	jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
	jmethodID getClassLoaderMethod = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	gClassLoader = env->NewGlobalRef(env->CallObjectMethod(activityClass, getClassLoaderMethod));
	gFindClassMethod = env->GetMethodID(classLoaderClass, "findClass", "(Ljava/lang/String;)Ljava/lang/Class;");

	postCommand = env->GetMethodID(activityClass, "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
	getDebugString = env->GetMethodID(activityClass, "getDebugString", "(Ljava/lang/String;)Ljava/lang/String;");
	getNativeCrashHistory = env->GetMethodID(activityClass, "getNativeCrashHistory", "(I)Ljava/util/ArrayList;");
	_dbg_assert_(postCommand && getDebugString && getNativeCrashHistory);

	Android_RegisterStorageCallbacks(env);

	RegisterAttachDetach(&Android_AttachThreadToJNI, &Android_DetachThreadFromJNI);

	TimeInit();
	return JNI_VERSION_1_6;
}

jobject Android_GetActivity(JNIEnv *env) {
	std::lock_guard<std::mutex> guard(g_activityLock);
	return ppssppActivity ? env->NewLocalRef(ppssppActivity) : nullptr;
}

static void PushCommand(std::string_view cmd, std::string_view param, int requestId = -1) {
	std::lock_guard<std::mutex> guard(frameCommandLock);
	g_frameCommands.emplace_back(cmd, param, requestId);
}

// Android implementation of callbacks to the Java part of the app
void System_Toast(std::string_view text) {
	PushCommand("toast", text);
}

void System_ShowKeyboard() {
	PushCommand("showKeyboard", "");
}

void System_Vibrate(int length_ms) {
	char temp[32];
	snprintf(temp, sizeof(temp), "%d", length_ms);
	PushCommand("vibrate", temp);
}

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	switch (urlType) {
	case LaunchUrlType::BROWSER_URL: PushCommand("launchBrowser", url); break;
	case LaunchUrlType::EMAIL_ADDRESS: PushCommand("launchEmail", url); break;
	// Can't really support the below well on Android...
	case LaunchUrlType::LOCAL_FOLDER: break;
	case LaunchUrlType::LOCAL_FILE: break;
	}
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return systemName;
	case SYSPROP_LANGREGION:	// "en_US"
		return langRegion;
	case SYSPROP_BOARDNAME:
		return boardName;
	case SYSPROP_DEVICE_NAME:
	{
		std::lock_guard<std::mutex> guard(g_deviceNameLock);
		return g_deviceName;
	}
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	case SYSPROP_INSTALLER_NAME:
		return g_installerName;
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
	case SYSPROP_DISPLAY_DPI:
		return display_dpi;
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
		return g_safeInsetLeft * display_scale_x * g_display.dpi_scale_x;
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
		return g_safeInsetRight * display_scale_x * g_display.dpi_scale_x;
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
		return g_safeInsetTop * display_scale_y * g_display.dpi_scale_y;
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return g_safeInsetBottom * display_scale_y * g_display.dpi_scale_y;
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
	case SYSPROP_MICROPHONE_NEEDS_POLLING:
		return true;
	case SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE:
		return sustainedPerfSupported;  // 7.0 introduced sustained performance mode as an optional feature.
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
		return androidVersion >= 11;  // honeycomb
	case SYSPROP_HAS_TEXT_CLIPBOARD:
		return true;
	case SYSPROP_HAS_DEEP_LINKS:
		return true;  // The game's content URI, launchable through an intent.
	case SYSPROP_CAN_LAUNCH_URL:
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
	case SYSPROP_SUPPORTS_SHARE_TEXT:
	case SYSPROP_SUPPORTS_SHARE_FILE:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_USE_IAP:
		// Not yet implemented, we use a separate gold app on Android
		return false;
	case SYSPROP_USE_APP_STORE:
		return true;
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
		// A headset has the sensors, but tilting it isn't tilt control.
		return g_hasAccelerometer && deviceType != DEVICE_TYPE_VR;
	case SYSPROP_HAS_GYRO:
		return g_hasGyro && deviceType != DEVICE_TYPE_VR;
	case SYSPROP_CAN_CREATE_SHORTCUT:
		return false;  // We can't create shortcuts directly from game code, but we can from the Android UI.
	case SYSPROP_DISPLAY_HAS_CAMERA_CUTOUT:
		return g_hasCameraCutout;
	case SYSPROP_CAN_RESTRICT_ORIENTATION:
		// Android 17+ ignores orientation requests on large screens, but not from games (appCategory="game").
		return true;
#ifndef HTTPS_NOT_AVAILABLE
	case SYSPROP_SUPPORTS_HTTPS:
		return !g_Config.bDisableHTTPS;
#endif
	case SYSPROP_CAN_GET_FREE_SPACE_FAST:
		return false;
	default:
		return false;
	}
}

std::string Android_GetInputDeviceDebugString() {
	auto env = getEnv();
	JNILocalFrame frame(env);
	jobject activity = Android_GetActivity(env);
	if (!activity) {
		return "(N/A)";
	}

	jstring jparam = env->NewStringUTF("InputDevice");
	jstring jstr = (jstring)env->CallObjectMethod(activity, getDebugString, jparam);
	if (CheckJavaException(env, __FUNCTION__) || !jstr) {
		return "(N/A)";
	}
	return GetJavaString(env, jstr);
}

std::vector<std::string> Android_GetNativeCrashHistory(int maxEntries) {
	if (!getNativeCrashHistory) {
		return {};
	}
	auto env = getEnv();
	JNILocalFrame frame(env);
	jobject activity = Android_GetActivity(env);
	if (!activity) {
		return {};
	}
	jobject jlist = env->CallObjectMethod(activity, getNativeCrashHistory, maxEntries);
	if (CheckJavaException(env, __FUNCTION__)) {
		return {};
	}
	return GetJavaStringList(env, jlist);
}

extern "C" void Java_org_ppsspp_ppsspp_PpssppActivity_registerCallbacks(JNIEnv *env, jobject obj) {
	jobject newActivity = env->NewGlobalRef(obj);
	jobject oldActivity;
	{
		std::lock_guard<std::mutex> guard(g_activityLock);
		oldActivity = ppssppActivity;
		ppssppActivity = newActivity;
	}
	Android_StorageSetActivity(newActivity);

	// An activity that gets recreated (rotation, resize) never unregisters, so this is where we let go
	// of it. Used to leak the whole activity, once per rotation.
	if (oldActivity) {
		env->DeleteGlobalRef(oldActivity);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_PpssppActivity_unregisterCallbacks(JNIEnv *env, jobject obj) {
	Android_StorageSetActivity(nullptr);
	jobject oldActivity;
	{
		std::lock_guard<std::mutex> guard(g_activityLock);
		oldActivity = ppssppActivity;
		ppssppActivity = nullptr;
	}
	if (oldActivity) {
		env->DeleteGlobalRef(oldActivity);
	}
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
		return g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation()).bImmersiveMode ? "1" : "0";
	} else if (query == "sustainedPerformanceMode") {
		return g_Config.bSustainedPerformanceMode ? "1" : "0";
	} else if (query == "audioMixWithOthers") {
		return g_Config.bAudioMixWithOthers ? "1" : "0";
	} else if (query == "externalDisplayOutput") {
		// Whether the game goes to a second display, see ExternalDisplayPresentation.java. The running backend: the
		// setting is already the new one while the restart prompt for a change is up.
		return GetGPUBackend() == GPUBackend::VULKAN && g_Config.iExternalDisplay == (int)ExternalDisplayMode::SHOW_GAME ? "1" : "0";
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

// False if the config in memory may not be the user's: the ini couldn't be read (say the memory stick is on an
// SD card that isn't mounted), or NativeInit couldn't reach the memory stick in memstick_dir.txt and fell
// back to the default one, where an old ppsspp.ini may be lying around.
static bool HaveUsersConfig() {
	if (!g_Config.LoadedFromFile() || g_Config.memStickDirectory.empty()) {
		return false;
	}
	const Path memstickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
	if (!File::Exists(memstickDirFile)) {
		return true;
	}
	std::string memstickDir;
	return File::ReadTextFileToString(memstickDirFile, &memstickDir) && Path(memstickDir) == g_Config.memStickDirectory;
}

// For ContentUri.manageUriGrants. Anything that stores the URI of a picked file or folder should be listed
// here, or the grant for it may eventually be released. A grant is kept if it's one of these, or a folder
// one of these is in. Returns null if we don't have the user's config, since then we don't know what's in use.
extern "C" jobjectArray Java_org_ppsspp_ppsspp_NativeApp_getReferencedUris(JNIEnv *env, jclass) {
	if (!HaveUsersConfig()) {
		return nullptr;
	}
	std::vector<std::string> uris = g_recentFiles.GetRecentFiles();
	uris.push_back(g_Config.memStickDirectory.ToString());
	uris.push_back(g_Config.currentDirectory.ToString());
	uris.push_back(g_Config.sRemoteISOSharedDir);
	uris.insert(uris.end(), g_Config.vPinnedPaths.begin(), g_Config.vPinnedPaths.end());
	uris.insert(uris.end(), g_Config.vShortcutUris.begin(), g_Config.vShortcutUris.end());
	uris.push_back(g_Config.sAchievementsUnlockAudioFile);
	uris.push_back(g_Config.sAchievementsLeaderboardSubmitAudioFile);
	uris.erase(std::remove_if(uris.begin(), uris.end(), [](const std::string &uri) {
		return !Android_IsContentUri(uri);
	}), uris.end());

	jclass stringClass = env->FindClass("java/lang/String");
	jobjectArray result = env->NewObjectArray((jsize)uris.size(), stringClass, nullptr);
	for (size_t i = 0; i < uris.size(); i++) {
		jstring juri = env->NewStringUTF(uris[i].c_str());
		env->SetObjectArrayElement(result, (jsize)i, juri);
		env->DeleteLocalRef(juri);
	}
	env->DeleteLocalRef(stringClass);
	return result;
}

// Only called from PpssppActivity.Initialize, before the render thread runs, so g_Config is ours to change.
// Returns false if they couldn't be saved, so the Java side keeps them for next time.
extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_addShortcutUris(JNIEnv *env, jclass, jobjectArray juris) {
	if (!HaveUsersConfig()) {
		return false;
	}
	const std::vector<std::string> before = g_Config.vShortcutUris;
	const jsize count = env->GetArrayLength(juris);
	// Oldest first, so the newest ends up at the front.
	for (jsize i = 0; i < count; i++) {
		jstring juri = (jstring)env->GetObjectArrayElement(juris, i);
		std::string uri = GetJavaString(env, juri);
		env->DeleteLocalRef(juri);
		if (Android_IsContentUri(uri) && g_Config.AddShortcutUri(uri)) {
			INFO_LOG(Log::System, "New home screen shortcut target: %s", uri.c_str());
		}
	}
	// Also when only the order changed, so the ones launched lately aren't the first to go.
	if (g_Config.vShortcutUris != before) {
		return g_Config.Save("ShortcutUrisAdded");
	}
	return true;
}

// Need to use raw Android logging before NativeInit.
#define EARLY_LOG(...)  __android_log_print(ANDROID_LOG_INFO, "PPSSPP", __VA_ARGS__)

static bool bFirstResume = false;

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_init
(JNIEnv * env, jclass, jstring jmodel, jint jdeviceType, jstring jlangRegion, jstring japkpath,
	jstring jdataDir, jstring jexternalStorageDir, jstring jexternalFilesDir, jstring jNativeLibDir,
	jstring jadditionalStorageDirs, jstring jcacheDir, jstring jshortcutParam, jstring jInstallerName,
	jint jAndroidVersion, jstring jboard) {
	SetCurrentThreadName("androidInit");

	// Makes sure we get early permission grants.
	ProcessFrameCommands();

	EARLY_LOG("NativeApp.init() -- begin");
	PROFILE_INIT();

	renderer_inited = false;
	exitRenderLoop = false;
	androidVersion = jAndroidVersion;
	deviceType = jdeviceType;

	Path apkPath(GetJavaString(env, japkpath));
	g_VFS.Register("", ZipFileReader::Create(apkPath, "assets/"));

	systemName = GetJavaString(env, jmodel);
	langRegion = GetJavaString(env, jlangRegion);

	g_installerName = GetJavaString(env, jInstallerName);

	EARLY_LOG("NativeApp.init(): device name: '%s'", systemName.c_str());

	std::string externalStorageDir = GetJavaString(env, jexternalStorageDir);
	std::string additionalStorageDirsString = GetJavaString(env, jadditionalStorageDirs);
	std::string externalFilesDir = GetJavaString(env, jexternalFilesDir);
	std::string nativeLibDir = GetJavaString(env, jNativeLibDir);

	g_externalDir = externalStorageDir;
	g_extFilesDir = externalFilesDir;
	VulkanSetNativeLibDir(nativeLibDir);

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
		temp = SplitCommandLine(shortcut_param);
		for (const auto &arg : temp) {
			args.push_back(arg.c_str());
		}
	}

	CommandLineOptions cmdLineOptions;
	CommandLineParseResult parseResult = cmdLineOptions.Parse((int)args.size(), args.data());
	switch (parseResult) {
	case CommandLineParseResult::Exit:
		EARLY_LOG("Command line parse said to exit - mobile, so ignoring.");
		break;
	case CommandLineParseResult::Error:
		EARLY_LOG("Command line parse reported error - mobile, so ignoring.");
		break;
	default:
		// Continue with launch.
		break;
	}

	EARLY_LOG("Calling NativeInit with user_data_path %s, externalStorageDir %s, cacheDir %s", user_data_path.c_str(), externalStorageDir.c_str(), cacheDir.c_str());

	// TODO: We should be able to do the Vulkan init in parallel with NativeInit.
	NativeInit((int)args.size(), &args[0], cmdLineOptions, user_data_path.c_str(), externalStorageDir.c_str(), cacheDir.c_str());

	bFirstResume = true;

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
		INFO_LOG(Log::System, "NativeApp.init() -- creating OpenGL context");
		graphicsContext = new AndroidEGLGraphicsContext();
		// Like Vulkan, we wait for a surface before doing anything - see runRenderLoop.
		break;
	case (int)GPUBackend::VULKAN:
	{
		INFO_LOG(Log::System, "NativeApp.init() -- creating Vulkan context");
		// The Vulkan render manager manages its own thread.
		// We create and destroy the Vulkan graphics context in the app main thread though.
		GraphicsContext *ctx = new VulkanGraphicsContext();
		std::string errorMessage;
		if (!ctx->InitAPI(nullptr, &g_Config.sVulkanDevice, &errorMessage)) {
			ERROR_LOG(Log::System, "Failed to initialize Vulkan, switching to OpenGL: %s", errorMessage.c_str());
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend(GPUBackend::OPENGL);
			delete ctx;
			goto retry;
		}
		((VulkanContext *)ctx->GetAPIContext())->SetCbGetDrawSize([]() {
			const uint64_t size = g_surfaceSize;
			return VkExtent2D{ (uint32_t)(size >> 32), (uint32_t)size };
		});
		graphicsContext = ctx;
		// Now, we wait until we get a surface to continue initializing Vulkan.
		break;
	}
	default:
		ERROR_LOG(Log::System, "NativeApp.init(): iGPUBackend %d not supported. Switching to OpenGL.", (int)g_Config.iGPUBackend);
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
		goto retry;
	}

	if (IsVREnabled()) {
		Version gitVer(PPSSPP_GIT_VERSION);
		InitVROnAndroid(gJvm, ppssppActivity, systemName.c_str(), gitVer.ToInteger(), "PPSSPP");
		SetVRCallbacks(NativeAxis, NativeKey, NativeTouch);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_setAchievementsHostOverride(JNIEnv *env, jclass, jstring jhost) {
	std::string host = GetJavaString(env, jhost);
	NativeSetAchievementsHostOverride(host);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_clearAchievementsHostOverride(JNIEnv *, jclass) {
	NativeClearAchievementsHostOverride();
}

AudioBackend *System_CreateAudioBackend() {
	// Use legacy mechanisms.
	return nullptr;
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

	INFO_LOG(Log::Audio, "NativeApp.audioInit() -- Using Oboe audio! frames/buffer: %i	 optimal sr: %i	 actual sr: %i", optimalFramesPerBuffer, optimalSampleRate, sampleRate);
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

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_resume(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.resume() - begin");
	AndroidAudio_Resume(g_audioState);

	System_PostUIMessage(UIMessage::APP_RESUMED, bFirstResume ? "first" : "");

	bFirstResume = false;
	INFO_LOG(Log::System, "NativeApp.resume() - end");
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_pause(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.pause() - begin");
	AndroidAudio_Pause(g_audioState);
	INFO_LOG(Log::System, "NativeApp.pause() - end");
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_shutdown(JNIEnv *, jclass) {
	INFO_LOG(Log::System, "NativeApp.shutdown() -- begin");

	// Nothing to join here: the render loop thread owns both itself and the emu thread, and
	// requestExitRenderLoop has already brought them down along with the surface.
	{
		if (graphicsContext) {
			INFO_LOG(Log::G3D, "Shutting down renderer");
			graphicsContext->ShutdownAPI();
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
		g_frameCommands.clear();
	}
	INFO_LOG(Log::System, "NativeApp.shutdown() -- end");
}

// A size change from the UI thread, for the render thread to apply between frames. Changing g_display while a
// frame is being drawn gives a frame laid out for one size and drawn at another.
struct PendingResize {
	int pixelXres;
	int pixelYres;
	float dpiX;
	float dpiY;
	bool rotated;
};
static std::mutex g_pendingResizeLock;
static std::optional<PendingResize> g_pendingResize;

static void ApplyResize(const PendingResize &resize) {
	const int old_w = g_display.pixel_xres;
	const int old_h = g_display.pixel_yres;
	const int old_dp_w = g_display.dp_xres;
	const int old_dp_h = g_display.dp_yres;

	bool new_size = g_display.Recalculate(resize.pixelXres, resize.pixelYres, resize.dpiX, resize.dpiY, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor));
	INFO_LOG(Log::G3D, "ApplyResize: pixel_xres=%d pixel_yres=%d g_dpi_scale_x=%f g_dpi_scale_y=%f dp_xres=%d dp_yres=%d",
		g_display.pixel_xres, g_display.pixel_yres, g_display.dpi_scale_x, g_display.dpi_scale_y, g_display.dp_xres, g_display.dp_yres);

	if (new_size || resize.rotated) {
		// Note that a density change moves the dp size while the pixel size stays put.
		INFO_LOG(Log::G3D, "%s detected (previously %dx%d px, %dx%d dp) - calling NativeResized()",
			new_size ? "Size change" : "Rotation change", old_w, old_h, old_dp_w, old_dp_h);
		NativeResized();
	} else {
		INFO_LOG(Log::G3D, "ApplyResize: Size didn't change.");
	}
}

// On the render thread, before each frame.
static void ApplyPendingResize() {
	std::optional<PendingResize> resize;
	{
		std::lock_guard<std::mutex> guard(g_pendingResizeLock);
		resize.swap(g_pendingResize);
	}
	if (resize) {
		ApplyResize(*resize);
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_setDeviceName(JNIEnv *env, jclass, jstring jname) {
	std::string name = jname ? GetJavaString(env, jname) : "";
	std::lock_guard<std::mutex> guard(g_deviceNameLock);
	g_deviceName = name;
}

// Secondary outputs (a TV on HDMI, for one). The surface's window is kept until the output is removed.
static std::mutex g_outputWindowsLock;
static std::map<int, ANativeWindow *> g_outputWindows;

extern "C" jint JNICALL Java_org_ppsspp_ppsspp_NativeApp_addOutput(JNIEnv *env, jclass, jobject surface, jint width, jint height) {
	ANativeWindow *wnd = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
	if (!wnd) {
		ERROR_LOG(Log::G3D, "addOutput: no window for the surface");
		return 0;
	}
	Draw::SwapchainDesc desc{};
	desc.winsys = WINDOWSYSTEM_ANDROID;
	desc.data1 = wnd;
	desc.width = width;
	desc.height = height;
	desc.tag = "ExternalDisplay";
	const int id = Native_AddOutput(OutputRole::Game, desc);
	INFO_LOG(Log::G3D, "addOutput: %dx%d, output %d", width, height, id);
	std::lock_guard<std::mutex> guard(g_outputWindowsLock);
	g_outputWindows[id] = wnd;
	return id;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_resizeOutput(JNIEnv *, jclass, jint id, jint width, jint height) {
	INFO_LOG(Log::G3D, "resizeOutput: output %d, %dx%d", id, width, height);
	Native_ResizeOutput(id, width, height);
}

// Returns once the render thread is done with the surface, as surfaceDestroyed requires.
extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_removeOutput(JNIEnv *, jclass, jint id) {
	INFO_LOG(Log::G3D, "removeOutput: output %d", id);
	Native_RemoveOutputAndWait(id);
	ANativeWindow *wnd = nullptr;
	{
		std::lock_guard<std::mutex> guard(g_outputWindowsLock);
		auto iter = g_outputWindows.find(id);
		if (iter != g_outputWindows.end()) {
			wnd = iter->second;
			g_outputWindows.erase(iter);
		}
	}
	if (wnd) {
		ANativeWindow_release(wnd);
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_backbufferResize(JNIEnv *, jclass, jint pixel_xres, jint pixel_yres, jint rotation) {
	INFO_LOG(Log::System, "NativeApp.backbufferResize(%d x %d, rotation %d)", pixel_xres, pixel_yres, rotation);

	// The display's Surface.ROTATION_*. A 180 degree flip changes this but not the size, and the swapchain
	// still needs recreating, for the pre-rotation.
	static int lastRotation = -1;
	const bool rotated = lastRotation != -1 && rotation != lastRotation;
	lastRotation = rotation;

	g_surfaceSize = ((uint64_t)(uint32_t)pixel_xres << 32) | (uint32_t)pixel_yres;

	// pixel_*res is the backbuffer resolution.
	if (IsVREnabled()) {
		GetVRResolutionPerEye(&pixel_xres, &pixel_yres);
	}

	// Compute display scale factor. Always < 1.0f (well, as long as we use buffers sized smaller than the screen...)
	// These two convert touch coordinates, here on the UI thread, so they're set right away.
	display_scale_x = (float)pixel_xres / (float)display_xres;
	display_scale_y = (float)pixel_yres / (float)display_yres;

	PendingResize resize{};
	resize.pixelXres = pixel_xres;
	resize.pixelYres = pixel_yres;
	resize.dpiX = (1.0f / display_scale_x) * (240.0f / (float)display_dpi);
	resize.dpiY = (1.0f / display_scale_y) * (240.0f / (float)display_dpi);
	resize.rotated = rotated;
	INFO_LOG(Log::G3D, "RecalcDPI: display_xres=%d display_yres=%d g_dpi=%d scaled_dpi_x=%f scaled_dpi_y=%f display_scale_x=%f display_scale_y=%f",
		display_xres, display_yres, display_dpi, resize.dpiX, resize.dpiY, display_scale_x, display_scale_y);

	std::lock_guard<std::mutex> guard(g_pendingResizeLock);
	if (g_pendingResize) {
		// One the render thread hasn't got to yet. Its size is superseded, but a rotation in it still counts.
		resize.rotated |= g_pendingResize->rotated;
	}
	if (renderLoopRunning) {
		// Applied by the render thread before its next frame (or the next render loop's first one, if it's exiting).
		g_pendingResize = resize;
	} else {
		// Nothing is drawing, so nothing reads g_display. runRenderLoop is on this thread, so that can't change under us.
		g_pendingResize.reset();
		ApplyResize(resize);
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
	case SystemNotification::EXTERNAL_DISPLAY_CHANGED:
		PushCommand("externalDisplay", "");
		break;
	case SystemNotification::TEST_JAVA_EXCEPTION:
		PushCommand("testException", "This is a test exception");
		break;
	case SystemNotification::AUDIO_MODE_CHANGED:
		PushCommand("audio_mode_changed", "");
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
		// The flags go before the strings, so the free text comes last.
		std::string serialized = StringFromFormat("%d:@:%d:@:%s:@:%s", requestId, param3 ? 1 : 0, param1.c_str(), param2.c_str());
		PushCommand("inputbox", serialized, requestId);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
		PushCommand("browse_image", StringFromFormat("%d", requestId), requestId);
		return true;
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		BrowseFileType fileType = (BrowseFileType)param3;
		std::string params = StringFromFormat("%d", requestId);
		switch (fileType) {
		case BrowseFileType::SOUND_EFFECT:
			PushCommand("browse_file_audio", params, requestId);
			break;
		case BrowseFileType::ZIP:
			PushCommand("browse_file_zip", params, requestId);
			break;
		default:
			PushCommand("browse_file", params, requestId);
			break;
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
		PushCommand("browse_folder", StringFromFormat("%d", requestId), requestId);
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
		// From the CPU thread. Starting and stopping go through Java, which asks for permission.
		if (param1 == "pollRecording") {
			AndroidAudio_Recording_Poll();
			return true;
		} else if (param1 == "deviceChanged") {
			// There's no device choice on Android.
			return true;
		}
		if (startsWith(param1, "startRecording:")) {
			// Whatever is left over is from before, maybe at another rate.
			AndroidAudio_Recording_Clear();
		}
		PushCommand("microphone_command", param1);
		return true;
	case SystemRequestType::SHARE_TEXT:
		PushCommand("share_text", param1);
		return true;
	case SystemRequestType::SHARE_FILE:
		// The MIME type first, as a path could contain the separator.
		PushCommand("share_file", param2 + ":@:" + param1);
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
	// Don't log the value, it can be a password.
	INFO_LOG(Log::System, "Received result of request %d from Java: %d: %d (%d chars)", jrequestID, (int)result, jintValue, (int)value.size());
	if (result) {
		g_requestManager.PostSystemSuccess(jrequestID, value, jintValue);
	} else {
		g_requestManager.PostSystemFailure(jrequestID, jintValue);
	}
}

void System_AskForPermission(SystemPermission permission) {
	switch (permission) {
	case SYSTEM_PERMISSION_STORAGE:
		PushCommand("ask_permission", "storage");
		break;
	case SYSTEM_PERMISSION_LOCAL_NETWORK:
		PushCommand("ask_permission", "local_network");
		break;
	}
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	if (androidVersion < 23) {
		return PERMISSION_STATUS_GRANTED;
	} else {
		std::lock_guard<std::mutex> guard(permissionsLock);
		auto iter = permissions.find(permission);
		return iter != permissions.end() ? iter->second : PERMISSION_STATUS_UNKNOWN;
	}
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_touch
	(JNIEnv *, jclass, float x, float y, int code, int pointerId) {
	if (!renderer_inited)
		return;
	TouchInput touch{};
	touch.id = pointerId;
	touch.x = x * display_scale_x * g_display.dpi_scale_x;
	touch.y = y * display_scale_y * g_display.dpi_scale_y;
	touch.flags = (TouchInputFlags)code;
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
	keyInput.flags = KeyInputFlags::DOWN;
	if (isRepeat) {
		keyInput.flags |= KeyInputFlags::IS_REPEAT;
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
	keyInput.flags = KeyInputFlags::UP;
	return NativeKey(keyInput);
}

extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_keyChar(JNIEnv *, jclass, jint deviceId, jint unicodeChar) {
	if (!renderer_inited) {
		return false; // could probably return true here too..
	}

	KeyInput keyInput;
	keyInput.deviceId = (InputDeviceID)deviceId;
	keyInput.unicodeChar = unicodeChar;
	keyInput.flags = KeyInputFlags::CHAR;
	return NativeKey(keyInput);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_joystickAxis(
		JNIEnv *env, jclass, jint deviceId, jintArray axisIds, jfloatArray values, jint count) {
	if (!renderer_inited)
		return;

	count = std::min({ count, env->GetArrayLength(axisIds), env->GetArrayLength(values) });

	// These are dirty-filtered on the Java side, so there are rarely more than a few.
	constexpr int BATCH_SIZE = 16;
	jint ids[BATCH_SIZE];
	jfloat vals[BATCH_SIZE];
	AxisInput axis[BATCH_SIZE];
	for (int start = 0; start < count; start += BATCH_SIZE) {
		const int n = std::min(count - start, BATCH_SIZE);
		env->GetIntArrayRegion(axisIds, start, n, ids);
		env->GetFloatArrayRegion(values, start, n, vals);
		for (int i = 0; i < n; i++) {
			axis[i].deviceId = (InputDeviceID)(int)deviceId;
			axis[i].axisId = (InputAxis)(int)ids[i];
			axis[i].value = vals[i];
		}
		NativeAxis(axis, n);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_mouse(
	JNIEnv *env, jclass, jfloat x, jfloat y, int button, int action) {
	if (!renderer_inited)
		return;
	TouchInput input{};

	static float last_x = 0.0f;
	static float last_y = 0.0f;

	if (x == -1.0f) {
		x = last_x;
	} else {
		last_x = x;
	}
	if (y == -1.0f) {
		y = last_y;
	} else {
		last_y = y;
	}

	x *= g_display.dpi_scale_x;
	y *= g_display.dpi_scale_y;

	if (button == 0) {
		// It's a pure mouse move.
		input.flags = TouchInputFlags::MOUSE | TouchInputFlags::MOVE;
		input.x = x;
		input.y = y;
		input.id = 0;
	} else {
		input.buttons = button;
		input.x = x;
		input.y = y;
		switch (action) {
		case 1:
			input.flags = TouchInputFlags::MOUSE | TouchInputFlags::DOWN;
			break;
		case 2:
			input.flags = TouchInputFlags::MOUSE | TouchInputFlags::UP;
			break;
		}
		input.id = 0;
	}
	VERBOSE_LOG(Log::System, "New-style mouse event: %f %f %d %d -> x: %f y: %f buttons: %d flags: %04x", x, y, button, action, input.x, input.y, input.buttons, input.flags);
	NativeTouch(input);

	// Also send mouse button key events, for binding.
	if (button) {
		KeyInput keyInput{};
		keyInput.deviceId = DEVICE_ID_MOUSE;
		switch (button) {
		case 1: keyInput.keyCode = NKCODE_EXT_MOUSEBUTTON_1; break;
		case 2: keyInput.keyCode = NKCODE_EXT_MOUSEBUTTON_2; break;
		case 3: keyInput.keyCode = NKCODE_EXT_MOUSEBUTTON_3; break;
		default: WARN_LOG(Log::System, "Unexpected mouse button %d", button);
		}
		keyInput.flags = action == 1 ? KeyInputFlags::DOWN : KeyInputFlags::UP;
		if (keyInput.keyCode != 0) {
			NativeKey(keyInput);
		}
	}
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
	key.flags = (KeyInputFlags)((u32)KeyInputFlags::DOWN | (u32)KeyInputFlags::HAS_WHEEL_DELTA | (wheelDelta << 16));
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

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_gyroscope(JNIEnv *, jclass, float x, float y, float z, jlong timestampNs) {
	if (!renderer_inited)
		return;
	NativeGyroscope(x, y, z, (double)timestampNs * 1e-9);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_sendMessageFromJava(JNIEnv *env, jclass, jstring jmessage, jstring jparam) {
	std::string msg = GetJavaString(env, jmessage);
	std::string prm = GetJavaString(env, jparam);

	// A bit ugly, see InputDeviceState.java.
	static InputDeviceID nextInputDeviceID = DEVICE_ID_ANY;

	// Some messages are caught by app-android. TODO: Should be all.
	if (msg == "permission_pending") {
		SetPermissionStatusFromJava(prm, PERMISSION_STATUS_PENDING);
	} else if (msg == "permission_denied") {
		SetPermissionStatusFromJava(prm, PERMISSION_STATUS_DENIED);
	} else if (msg == "permission_granted") {
		SetPermissionStatusFromJava(prm, PERMISSION_STATUS_GRANTED);
		System_PostUIMessage(UIMessage::PERMISSION_GRANTED, prm);
	} else if (msg == "sustained_perf_supported") {
		sustainedPerfSupported = true;
	} else if (msg == "safe_insets") {
		// INFO_LOG(Log::System, "Got insets: %s", prm.c_str());
		// We don't bother with supporting exact rectangular regions. Safe insets are good enough.
		int left, right, top, bottom, cutout;
		if (5 == sscanf(prm.c_str(), "%d:%d:%d:%d:%d", &left, &right, &top, &bottom, &cutout)) {
			g_safeInsetLeft = (float)left;
			g_safeInsetRight = (float)right;
			g_safeInsetTop = (float)top;
			g_safeInsetBottom = (float)bottom;
			g_hasCameraCutout = cutout != 0;
		}
	} else if (msg == "sensors") {
		int accelerometer, gyro;
		if (2 == sscanf(prm.c_str(), "%d:%d", &accelerometer, &gyro)) {
			g_hasAccelerometer = accelerometer != 0;
			g_hasGyro = gyro != 0;
		}
	} else if (msg == "inputDeviceConnectedID") {
		nextInputDeviceID = (InputDeviceID)parseLong(prm);
	} else if (msg == "inputDeviceConnected") {
		KeyMap::NotifyPadConnected(nextInputDeviceID, prm);
	} else if (msg == "inputDeviceDisconnectedID") {
		InputDeviceID disconnectedID = (InputDeviceID)parseLong(prm);
		KeyMap::NotifyPadDisconnected(disconnectedID);
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

		// This is a command line, the same kind NativeApp.init gets: a quoted path from a shortcut or a
		// file association, or raw arguments from the Args extra. We're already up and running, so any
		// options in it are not applied. They still have to be parsed, to tell them (and their
		// arguments) from the game to boot.
		std::vector<std::string> parts = SplitCommandLine(prm);
		std::vector<const char *> args;
		args.push_back("ppsspp");
		for (const auto &part : parts) {
			args.push_back(part.c_str());
		}
		CommandLineOptions options;
		if (options.Parse((int)args.size(), args.data()) == CommandLineParseResult::Error) {
			WARN_LOG(Log::System, "shortcutParam: Failed to parse '%s'", prm.c_str());
		}
		if (options.bootFilenames.empty()) {
			INFO_LOG(Log::System, "shortcutParam: Nothing to boot in there, ignoring (options only apply at startup)");
			return;
		}
		std::string bootFilename = options.bootFilenames[0];
		// NOTE: The parameter can be a file:// URL, which we need to take care of here. Similar to in NativeApp.cpp, search for file://
		if (startsWith(bootFilename, "file:///")) {
			std::string param = bootFilename;
			bootFilename = UriDecode(bootFilename.substr(7));
			INFO_LOG(Log::IO, "Decoding '%s' to '%s'", param.c_str(), bootFilename.c_str());
		}
		System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, bootFilename);
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

	// Hard parameters for the display. Actual DPI recalculation happens in BackbufferResize.
	display_xres = xres;
	display_yres = yres;
	display_dpi = dpi;
	g_display.display_hz = refreshRate;
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
	JNIEnv *env = getEnv();
	JNILocalFrame frame(env);
	jclass cameraClass = findClass("org/ppsspp/ppsspp/CameraHelper");
	if (CheckJavaException(env, __FUNCTION__) || !cameraClass) {
		return {};
	}
	jmethodID deviceListMethod = env->GetStaticMethodID(cameraClass, "getDeviceList", "()Ljava/util/ArrayList;");
	jobject deviceList = env->CallStaticObjectMethod(cameraClass, deviceListMethod);
	if (CheckJavaException(env, __FUNCTION__)) {
		return {};
	}
	return GetJavaStringList(env, deviceList);
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

static void ProcessFrameCommands() {
	JNIEnv *env = getEnv();
	JNILocalFrame frame(env);
	jobject activity = Android_GetActivity(env);
	std::vector<FrameCommand> frameCommands;
	{
		std::lock_guard<std::mutex> guard(frameCommandLock);
		frameCommands = std::move(g_frameCommands);
		g_frameCommands.clear();
	}
	if (frameCommands.empty()) {
		return;
	}

	if (!activity) {
		ERROR_LOG(Log::System, "No activity, dropping %zu commands", frameCommands.size());
		for (const FrameCommand &frameCmd : frameCommands) {
			// Whoever made the request is waiting for an answer.
			if (frameCmd.requestId >= 0) {
				g_requestManager.PostSystemFailure(frameCmd.requestId);
			}
		}
		return;
	}

	INFO_LOG(Log::System, "Processing %zu frame commands", frameCommands.size());
	for (const FrameCommand &frameCmd : frameCommands) {
		// Not logging the params, an inputbox one can contain a password.
		DEBUG_LOG(Log::System, "frameCommand '%s'", frameCmd.command.c_str());

		// The params can contain any text, like a title the game gave its OSK.
		std::string params;
		ConvertUTF8ToJavaModifiedUTF8(&params, frameCmd.params);
		jstring cmd = env->NewStringUTF(frameCmd.command.c_str());
		jstring param = env->NewStringUTF(params.c_str());
		env->CallVoidMethod(activity, postCommand, cmd, param);
		if (CheckJavaException(env, __FUNCTION__) && frameCmd.requestId >= 0) {
			g_requestManager.PostSystemFailure(frameCmd.requestId);
		}
		env->DeleteLocalRef(cmd);
		env->DeleteLocalRef(param);
	}
}

std::thread g_renderLoopThread;

static void RenderLoopThread(ANativeWindow *wnd, GraphicsContext *graphicsContext);

// Runs on both the Vulkan and the OpenGL path.
// This handles the entire lifecycle of the graphics context's surface, init and exit.
extern "C" jboolean JNICALL Java_org_ppsspp_ppsspp_PpssppActivity_runRenderLoop(JNIEnv * env, jobject obj, jobject _surf) {
	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		if (renderLoopRunning) {
			ERROR_LOG(Log::G3D, "runRenderLoop: Already running");
			return false;
		}
		// The previous thread gave up by itself (failed surface init). Reap it so we can try again.
		WARN_LOG(Log::G3D, "runRenderLoop: Joining a render thread that had already exited");
		g_renderLoopThread.join();
		g_renderLoopThread = std::thread();
	}

	_assert_(!exitRenderLoop);

	ANativeWindow *wnd = _surf ? ANativeWindow_fromSurface(env, _surf) : nullptr;

	if (!wnd) {
		// This shouldn't ever happen.
		ERROR_LOG(Log::G3D, "Error: Surface is null.");
		return false;
	}

	// Set before the thread exists, so an exit request can't slip in ahead of it.
	renderLoopRunning = true;
	g_renderLoopThread = std::thread(RenderLoopThread, wnd, graphicsContext);
	return true;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_PpssppActivity_requestExitRenderLoop(JNIEnv * env, jobject obj) {
	// Like runRenderLoop, this is only called from the UI thread, so the thread object is ours.
	// Don't go by renderLoopRunning - the thread might have bailed by itself, and still needs joining.
	if (!g_renderLoopThread.joinable()) {
		INFO_LOG(Log::System, "Render loop not running, nothing to join");
		return;
	}
	exitRenderLoop = true;
	g_renderLoopThread.join();
	g_renderLoopThread = std::thread();
	exitRenderLoop = false;
}

// This function must release the wnd reference.
static void RenderLoopThread(ANativeWindow *wnd, GraphicsContext *graphicsContext) {
	// Only a provisional name, so the surface init below has something to log under. RunGraphicsLoop
	// renames this thread once it knows which of the two roles it's going to take.
	SetCurrentThreadName("RenderLoop");

	AndroidJNIThreadContext ctx;
	_assert_(graphicsContext);

	if (exitRenderLoop) {
		WARN_LOG(Log::G3D, "RenderLoopThread: ExitRenderLoop requested at start, skipping the whole thing.");
		renderLoopRunning = false;
		ANativeWindow_release(wnd);
		return;
	}

	WARN_LOG(Log::G3D, "RenderLoopThread. display_xres=%d display_yres=%d desiredBackbufferSizeX=%d desiredBackbufferSizeY=%d",
		display_xres, display_yres, desiredBackbufferSizeX, desiredBackbufferSizeY);

	std::string errorMessage;
	if (!graphicsContext->InitSurface(WINDOWSYSTEM_ANDROID, wnd, nullptr, &errorMessage)) {
		// On Android, if we get here, really no point in continuing.
		// The UI is supposed to render on any device both on OpenGL and Vulkan. If either of those don't work
		// on a device, we blacklist it. Hopefully we should have already failed in InitAPI anyway and reverted to GL back then.
		ERROR_LOG(Log::G3D, "Failed to initialize graphics context for surface: %s", errorMessage.c_str());
		System_Toast("Failed to initialize graphics context for surface.");
		renderLoopRunning = false;
		ANativeWindow_release(wnd);
		return;
	}

	renderer_inited = true;
	const auto frame = [](GraphicsContext *graphicsContext) {
		ApplyPendingResize();
		NativeFrame(graphicsContext);
		ProcessFrameCommands();
		return !exitRenderLoop;
	};
	// Both backends share one lifecycle now, which is the point of all this: RunGraphicsLoop picks
	// the thread arrangement the context needs and doesn't return until emulation is done. Windows
	// and headless go through the same function, via MainThreadFunc.
	// We have to hand it exitRenderLoop as well as the frame callback - in VR the loop can be
	// parked waiting for the session to come back, where the callback is never reached.
	RunGraphicsLoop(graphicsContext, new NativeApplication(), frame, []() { return exitRenderLoop.load(); });

	renderer_inited = false;

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(Log::G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownSurface();

	// But we don't shut down the API. On some platforms like Android, we keep that around for later.

	// exitRenderLoop is reset by whoever set it, after joining us.
	renderLoopRunning = false;
	ANativeWindow_release(wnd);

	WARN_LOG(Log::G3D, "Render loop function exited.");
}

// The cache and everything it loaded are gone when this returns, before queryGameInfo tears down the
// thread manager they ran on.
static void QueryGameInfo(const Path &path, std::string *gameName, std::string *iconData) {
	GameInfoCache cache;
	std::shared_ptr<GameInfo> info = cache.GetInfo(nullptr, path, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
	if (!info) {
		INFO_LOG(Log::System, "No info from cache");
		return;
	}

	// Wait for both name and icon for a second.
	int attempts = 100;
	while ((!info->Ready(GameInfoFlags::PARAM_SFO) || !info->Ready(GameInfoFlags::ICON)) && attempts > 0) {
		sleep_ms(10, "info-icon-poll");
		attempts--;
	}
	if (attempts == 0 || info->fileType == IdentifiedFileType::UNKNOWN) {
		INFO_LOG(Log::System, "Failed to query game info");
		return;
	}

	*gameName = info->GetTitle();
	if (gameName->length() > strlen("The ") && startsWithNoCase(*gameName, "The ")) {
		*gameName = gameName->substr(strlen("The "));
	}
	INFO_LOG(Log::System, "Got name: '%s'", gameName->c_str());

	if (info->Ready(GameInfoFlags::ICON)) {
		*iconData = info->icon.data;
	}
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameInfo(JNIEnv * env, jclass, jobject activity, jstring jpath) {

	jobject activityRef = nullptr;

	bool teardownThreadManager = false;
	// Maybe we should just check ppssppActivity instead.
	if (!g_threadManager.IsInitialized()) {
		teardownThreadManager = true;
		g_threadManager.Init(1, 1);
		g_logManager.SetOutputsEnabled(LogOutput::Stdio);
		g_logManager.SetAllLogLevels(LogLevel::LDEBUG);
		activityRef = env->NewGlobalRef(activity);
		Android_StorageSetActivity(activityRef);
		INFO_LOG(Log::System, "No thread manager - initializing one");
	}

	Path path = Path(GetJavaString(env, jpath));
	INFO_LOG(Log::System, "queryGameInfo(%s)", path.c_str());

	std::string gameName;
	jbyteArray gameIcon = nullptr;

	std::string iconData;
	QueryGameInfo(path, &gameName, &iconData);
	if (!iconData.empty()) {
		gameIcon = env->NewByteArray((jsize)iconData.size());
		env->SetByteArrayRegion(gameIcon, 0, (jsize)iconData.size(), (const jbyte *)iconData.data());
	}

	if (teardownThreadManager) {
		g_logManager.SetOutputsEnabled((LogOutput)0);
		Android_StorageSetActivity(nullptr);
		g_threadManager.Teardown();
	}

	// Construct a Java Object[] with two entries: name (String), icon (byte[])
	jobjectArray result = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);

	jstring jname = env->NewStringUTF(gameName.c_str());
	env->SetObjectArrayElement(result, 0, jname);
	env->DeleteLocalRef(jname);

	if (gameIcon != nullptr) {
		env->SetObjectArrayElement(result, 1, gameIcon);
		env->DeleteLocalRef(gameIcon);
	}
	if (activityRef) {
		env->DeleteGlobalRef(activityRef);
	}
	return result;
}

#endif
