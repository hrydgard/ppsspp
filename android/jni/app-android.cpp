// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgard (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.

#include <cassert>
#include <cstdlib>
#include <cstdint>

#include <sstream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include "base/basictypes.h"
#include "base/stringutil.h"
#include "base/display.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "thread/threadutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "profiler/profiler.h"
#include "math/math_util.h"
#include "net/resolve.h"
#include "util/text/parsers.h"
#include "android/jni/native_audio.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"

#include "Common/GraphicsContext.h"
#include "AndroidGraphicsContext.h"
#include "AndroidVulkanContext.h"
#include "AndroidEGLContext.h"
#include "AndroidJavaGLContext.h"

#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Core/System.h"
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

static std::thread emuThread;
static std::atomic<int> emuThreadState((int)EmuThreadState::DISABLED);

void UpdateRunLoopAndroid(JNIEnv *env);

static AndroidAudioState *g_audioState;

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
static int display_xres;
static int display_yres;
static int display_dpi_x;
static int display_dpi_y;
static int backbuffer_format;	// Android PixelFormat enum

static int desiredBackbufferSizeX;
static int desiredBackbufferSizeY;

// Cache the class loader so we can use it from native threads. Required for TextAndroid.
JavaVM* gJvm = nullptr;
static jobject gClassLoader;
static jmethodID gFindClassMethod;


static jmethodID postCommand;
static jobject nativeActivity;
static volatile bool exitRenderLoop;
static bool renderLoopRunning;

static float dp_xscale = 1.0f;
static float dp_yscale = 1.0f;

static bool renderer_inited = false;
static bool sustainedPerfSupported = false;

// See NativeQueryConfig("androidJavaGL") to change this value.
static bool javaGL = true;

static std::string library_path;
static std::map<SystemPermission, PermissionStatus> permissions;

AndroidGraphicsContext *graphicsContext;

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
	ILOG("JNI_OnLoad");
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

	setCurrentThreadName("Emu");
	ILOG("Entering emu thread");

	// Wait for render loop to get started.
	if (!graphicsContext || !graphicsContext->Initialized()) {
		ILOG("Runloop: Waiting for displayInit...");
		while (!graphicsContext || !graphicsContext->Initialized()) {
			sleep_ms(20);
		}
	} else {
		ILOG("Runloop: Graphics context available! %p", graphicsContext);
	}
	NativeInitGraphics(graphicsContext);

	ILOG("Graphics initialized. Entering loop.");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;
	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		UpdateRunLoopAndroid(env);
	}
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();

	// Also ask the main thread to stop, so it doesn't hang waiting for a new frame.
	graphicsContext->StopThread();

	gJvm->DetachCurrentThread();
	ILOG("Leaving emu thread");
}

static void EmuThreadStart() {
	ILOG("EmuThreadStart");
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc);
}

// Call EmuThreadStop first, then keep running the GPU (or eat commands)
// as long as emuThreadState isn't STOPPED and/or there are still things queued up.
// Only after that, call EmuThreadJoin.
static void EmuThreadStop() {
	ILOG("EmuThreadStop - stopping...");
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
	ILOG("EmuThreadJoin - joined");
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
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return (int)(display_hz * 1000.0);
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_SUPPORTS_PERMISSIONS:
		return androidVersion >= 23;	// 6.0 Marshmallow introduced run time permissions.
	case SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE:
		return sustainedPerfSupported;  // 7.0 introduced sustained performance mode as an optional feature.
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
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
}

extern "C" void Java_org_ppsspp_ppsspp_NativeActivity_unregisterCallbacks(JNIEnv *env, jobject obj) {
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

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_init
	(JNIEnv *env, jclass, jstring jmodel, jint jdeviceType, jstring jlangRegion, jstring japkpath,
		jstring jdataDir, jstring jexternalDir, jstring jlibraryDir, jstring jcacheDir, jstring jshortcutParam,
		jint jAndroidVersion, jstring jboard) {
	setCurrentThreadName("androidInit");

	// Makes sure we get early permission grants.
	ProcessFrameCommands(env);

	ILOG("NativeApp.init() -- begin");
	PROFILE_INIT();

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

	std::string externalDir = GetJavaString(env, jexternalDir);
	std::string user_data_path = GetJavaString(env, jdataDir);
	if (user_data_path.size() > 0)
		user_data_path += "/";
	library_path = GetJavaString(env, jlibraryDir) + "/";
	std::string shortcut_param = GetJavaString(env, jshortcutParam);
	std::string cacheDir = GetJavaString(env, jcacheDir);
	std::string buildBoard = GetJavaString(env, jboard);
	boardName = buildBoard;
	ILOG("NativeApp.init(): External storage path: %s", externalDir.c_str());
	ILOG("NativeApp.init(): Launch shortcut parameter: %s", shortcut_param.c_str());

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

	// If shortcut_param is not empty, pass it as additional varargs argument to NativeInit() method.
	// NativeInit() is expected to treat extra argument as boot_filename, which in turn will start game immediately.
	// NOTE: Will only work if ppsspp started from Activity.onCreate(). Won't work if ppsspp app start from onResume().

	if (shortcut_param.empty()) {
		const char *argv[2] = {app_name.c_str(), 0};
		NativeInit(1, argv, user_data_path.c_str(), externalDir.c_str(), cacheDir.c_str());
	} else {
		const char *argv[3] = {app_name.c_str(), shortcut_param.c_str(), 0};
		NativeInit(2, argv, user_data_path.c_str(), externalDir.c_str(), cacheDir.c_str());
	}

retry:
	// Now that we've loaded config, set javaGL.
	javaGL = NativeQueryConfig("androidJavaGL") == "true";

	switch (g_Config.iGPUBackend) {
	case (int)GPUBackend::OPENGL:
		useCPUThread = true;
		_assert_(javaGL);
		ILOG("NativeApp.init() -- creating OpenGL context (JavaGL)");
		graphicsContext = new AndroidJavaEGLGraphicsContext();
		break;
	case (int)GPUBackend::VULKAN:
	{
		ILOG("NativeApp.init() -- creating Vulkan context");
		useCPUThread = false;  // The Vulkan render manager manages its own thread.
		// We create and destroy the Vulkan graphics context in the "EGL" thread.
		AndroidVulkanContext *ctx = new AndroidVulkanContext();
		if (!ctx->InitAPI()) {
			ILOG("Failed to initialize Vulkan, switching to OpenGL");
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend(GPUBackend::OPENGL);
			goto retry;
		} else {
			graphicsContext = ctx;
		}
		break;
	}
	default:
		ELOG("NativeApp.init(): iGPUBackend %d not supported. Switching to OpenGL.", (int)g_Config.iGPUBackend);
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
		goto retry;
		// Crash();
	}

	if (useCPUThread) {
		ILOG("NativeApp.init() - launching emu thread");
		EmuThreadStart();
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioInit(JNIEnv *, jclass) {
	sampleRate = optimalSampleRate;
	if (NativeQueryConfig("force44khz") != "0" || optimalSampleRate == 0) {
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

	ILOG("NativeApp.audioInit() -- Using OpenSL audio! frames/buffer: %i	 optimal sr: %i	 actual sr: %i", optimalFramesPerBuffer, optimalSampleRate, sampleRate);
	if (!g_audioState) {
		g_audioState = AndroidAudio_Init(&NativeMix, library_path, framesPerBuffer, sampleRate);
	} else {
		ELOG("Audio state already initialized");
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioShutdown(JNIEnv *, jclass) {
	if (g_audioState) {
		AndroidAudio_Shutdown(g_audioState);
		g_audioState = nullptr;
	} else {
		ELOG("Audio state already shutdown!");
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_resume(JNIEnv *, jclass) {
	ILOG("NativeApp.resume() - resuming audio");
	AndroidAudio_Resume(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_pause(JNIEnv *, jclass) {
	ILOG("NativeApp.pause() - pausing audio");
	AndroidAudio_Pause(g_audioState);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_shutdown(JNIEnv *, jclass) {
	if (useCPUThread && graphicsContext) {
		EmuThreadStop();
		while (graphicsContext->ThreadFrame()) {
			continue;
		}
		EmuThreadJoin();

		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();
	}

	ILOG("NativeApp.shutdown() -- begin");
	if (renderer_inited) {
		ILOG("Shutting down renderer");
		// This will be from the wrong thread? :/
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = nullptr;
		renderer_inited = false;
	} else {
		ILOG("Not shutting down renderer - not initialized");
	}

	NativeShutdown();
	VFSShutdown();
	while (frameCommands.size())
		frameCommands.pop();
	ILOG("NativeApp.shutdown() -- end");
}

// JavaEGL
extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
	// We should be running on the render thread here.
	std::string errorMessage;
	if (renderer_inited) {
		// Would be really nice if we could get something on the GL thread immediately when shutting down...
		ILOG("NativeApp.displayInit() restoring");
		if (useCPUThread) {
			EmuThreadStop();
			while (graphicsContext->ThreadFrame()) {
				continue;
			}
			EmuThreadJoin();
		} else {
			NativeShutdownGraphics();
		}
		graphicsContext->ThreadEnd();
		graphicsContext->ShutdownFromRenderThread();

		ILOG("Shut down both threads. Now let's bring it up again!");

		graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0);
		if (useCPUThread) {
			EmuThreadStart();
		} else {
			NativeInitGraphics(graphicsContext);
		}
		graphicsContext->ThreadStart();
		ILOG("Restored.");
	} else {
		ILOG("NativeApp.displayInit() first time");
		graphicsContext->InitFromRenderThread(nullptr, 0, 0, 0, 0);
		graphicsContext->ThreadStart();
		renderer_inited = true;
	}
	NativeMessageReceived("recreateviews", "");
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_backbufferResize(JNIEnv *, jclass, jint bufw, jint bufh, jint format) {
	ILOG("NativeApp.backbufferResize(%d x %d)", bufw, bufh);

	bool new_size = pixel_xres != bufw || pixel_yres != bufh;
	int old_w = pixel_xres;
	int old_h = pixel_yres;
	// pixel_*res is the backbuffer resolution.
	pixel_xres = bufw;
	pixel_yres = bufh;
	backbuffer_format = format;

	g_dpi = display_dpi_x;
	g_dpi_scale_x = 240.0f / g_dpi;
	g_dpi_scale_y = 240.0f / g_dpi;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;

	dp_xres = display_xres * g_dpi_scale_x;
	dp_yres = display_yres * g_dpi_scale_y;

	// Touch scaling is from display pixels to dp pixels.
	dp_xscale = (float)dp_xres / (float)display_xres;
	dp_yscale = (float)dp_yres / (float)display_yres;

	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;

	ILOG("g_dpi=%f g_dpi_scale_x=%f g_dpi_scale_y=%f", g_dpi, g_dpi_scale_x, g_dpi_scale_y);
	ILOG("dp_xscale=%f dp_yscale=%f", dp_xscale, dp_yscale);
	ILOG("dp_xres=%d dp_yres=%d", dp_xres, dp_yres);
	ILOG("pixel_xres=%d pixel_yres=%d", pixel_xres, pixel_yres);

	if (new_size) {
		ILOG("Size change detected (previously %d,%d) - calling NativeResized()", old_w, old_h);
		NativeResized();
	} else {
		ILOG("Size didn't change.");
	}
}

void UpdateRunLoopAndroid(JNIEnv *env) {
	NativeUpdate();

	NativeRender(graphicsContext);
	time_update();

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
		setCurrentThreadName("AndroidRender");
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

	float sensitivity = g_Config.fXInputAnalogSensitivity;
	axis.value *= sensitivity;

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
		ILOG("STORAGE PERMISSION: PENDING");
		// TODO: Add support for other permissions
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_PENDING;
	} else if (msg == "permission_denied") {
		ILOG("STORAGE PERMISSION: DENIED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_DENIED;
	} else if (msg == "permission_granted") {
		ILOG("STORAGE PERMISSION: GRANTED");
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_GRANTED;
	} else if (msg == "sustained_perf_supported") {
		sustainedPerfSupported = true;
	}

	// Ensures that the receiver can handle it on a sensible thread.
	NativeMessageReceived(msg.c_str(), prm.c_str());
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeActivity_exitEGLRenderLoop(JNIEnv *env, jobject obj) {
	if (!renderLoopRunning) {
		ELOG("Render loop already exited");
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
	ILOG("CorrectRatio: Considering size: %0.2f/%0.2f=%0.2f for scale %f", x, y, ratio, scale);
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
	ILOG("Target ratio: %0.2f ratio: %0.2f correction: %0.2f", targetRatio, ratio, correction);
	if (ratio < targetRatio) {
		y *= correction;
	} else {
		x /= correction;
	}

	sz_x = x;
	sz_y = y;
	ILOG("Corrected ratio: %dx%d", sz_x, sz_y);
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
	ILOG("NativeApp.setDisplayParameters(%d x %d, dpi=%d, refresh=%0.2f)", xres, yres, dpi, refreshRate);
	display_xres = xres;
	display_yres = yres;
	display_dpi_x = dpi;
	display_dpi_y = dpi;
	display_hz = refreshRate;
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

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_pushNewGpsData(JNIEnv *, jclass,
		jfloat latitude, jfloat longitude, jfloat altitude, jfloat speed, jfloat bearing, jlong time) {
	PushNewGpsData(latitude, longitude, altitude, speed, bearing, time);
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_pushCameraImage(JNIEnv *env, jclass,
		jbyteArray image) {

	if (image != NULL) {
		jlong size = env->GetArrayLength(image);
		jbyte* buffer = env->GetByteArrayElements(image, NULL);
		PushCameraImage(size, (unsigned char *)buffer);
		env->ReleaseByteArrayElements(image, buffer, JNI_ABORT);
	}
}

// Call this under frameCommandLock.
static void ProcessFrameCommands(JNIEnv *env) {
	while (!frameCommands.empty()) {
		FrameCommand frameCmd;
		frameCmd = frameCommands.front();
		frameCommands.pop();

		ILOG("frameCommand '%s' '%s'", frameCmd.command.c_str(), frameCmd.params.c_str());

		jstring cmd = env->NewStringUTF(frameCmd.command.c_str());
		jstring param = env->NewStringUTF(frameCmd.params.c_str());
		env->CallVoidMethod(nativeActivity, postCommand, cmd, param);
		env->DeleteLocalRef(cmd);
		env->DeleteLocalRef(param);
	}
}

extern "C" bool JNICALL Java_org_ppsspp_ppsspp_NativeActivity_runEGLRenderLoop(JNIEnv *env, jobject obj, jobject _surf) {
	// Needed for Vulkan, even if we're not using the old EGL path.

	exitRenderLoop = false;
	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	ANativeWindow *wnd = ANativeWindow_fromSurface(env, _surf);

	WLOG("runEGLRenderLoop. display_xres=%d display_yres=%d", display_xres, display_yres);

	if (wnd == nullptr) {
		ELOG("Error: Surface is null.");
		renderLoopRunning = false;
		return false;
	}

retry:

	bool vulkan = g_Config.iGPUBackend == (int)GPUBackend::VULKAN;

	int tries = 0;

	if (!graphicsContext->InitFromRenderThread(wnd, desiredBackbufferSizeX, desiredBackbufferSizeY, backbuffer_format, androidVersion)) {
		ELOG("Failed to initialize graphics context.");

		if (!exitRenderLoop && (vulkan && tries < 2)) {
			ILOG("Trying again, this time with OpenGL.");
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
			tries++;
			goto retry;
		}

		delete graphicsContext;
		graphicsContext = nullptr;
		renderLoopRunning = false;
		return false;
	}

	if (!exitRenderLoop) {
		NativeInitGraphics(graphicsContext);
		renderer_inited = true;
	}

	while (!exitRenderLoop) {
		static bool hasSetThreadName = false;
		if (!hasSetThreadName) {
			hasSetThreadName = true;
			setCurrentThreadName("AndroidRender");
		}

		NativeUpdate();

		NativeRender(graphicsContext);
		time_update();

		graphicsContext->SwapBuffers();

		ProcessFrameCommands(env);
	}

	ILOG("Leaving EGL/Vulkan render loop.");

	NativeShutdownGraphics();
	renderer_inited = false;

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	ILOG("Shutting down graphics context from render thread...");
	graphicsContext->ShutdownFromRenderThread();
	renderLoopRunning = false;
	WLOG("Render loop function exited.");
	return true;
}

extern "C" jstring Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameName(JNIEnv *env, jclass, jstring jpath) {
	std::string path = GetJavaString(env, jpath);
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
