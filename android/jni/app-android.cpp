// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgard (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.


#include <assert.h>
#include <sstream>
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <stdlib.h>
#include <stdint.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <queue>
#include <mutex>
#include <thread>

#include "base/basictypes.h"
#include "base/stringutil.h"
#include "base/display.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "thread/prioritizedworkqueue.h"
#include "thread/threadutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "profiler/profiler.h"
#include "math/math_util.h"
#include "net/resolve.h"
#include "util/text/parsers.h"
#include "android/jni/native_audio.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"

#include "thin3d/thin3d.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Common/CPUDetect.h"
#include "Common/GraphicsContext.h"
#include "Common/GL/GLInterfaceBase.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"
#include "UI/GameInfoCache.h"

#include "app-android.h"

static JNIEnv *jniEnvUI;

enum {
	ANDROID_VERSION_GINGERBREAD = 9,
	ANDROID_VERSION_ICS = 14,
	ANDROID_VERSION_JELLYBEAN = 16,
	ANDROID_VERSION_KITKAT = 19,
	ANDROID_VERSION_LOLLIPOP = 21,
	ANDROID_VERSION_MARSHMALLOW = 23,
	ANDROID_VERSION_NOUGAT = 24,
	ANDROID_VERSION_NOUGAT_1 = 25,
};

struct FrameCommand {
	FrameCommand() {}
	FrameCommand(std::string cmd, std::string prm) : command(cmd), params(prm) {}

	std::string command;
	std::string params;
};

class AndroidGraphicsContext : public GraphicsContext {
public:
	virtual bool Init(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) = 0;
};

class AndroidEGLGraphicsContext : public AndroidGraphicsContext {
public:
	AndroidEGLGraphicsContext() : draw_(nullptr), wnd_(nullptr), gl(nullptr) {}
	bool Init(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;
	void Shutdown() override;
	void SwapBuffers() override;
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

private:
	Draw::DrawContext *draw_;
	ANativeWindow *wnd_;
	cInterfaceBase *gl;
};

bool AndroidEGLGraphicsContext::Init(ANativeWindow *wnd, int backbufferWidth, int backbufferHeight, int backbufferFormat, int androidVersion) {
	wnd_ = wnd;
	gl = HostGL_CreateGLInterface();
	if (!gl) {
		ELOG("ERROR: Failed to create GL interface");
		return false;
	}
	ILOG("EGL interface created. Desired backbuffer size: %dx%d", backbufferWidth, backbufferHeight);

	// Apparently we still have to set this through Java through setFixedSize on the bufferHolder for it to take effect...
	gl->SetBackBufferDimensions(backbufferWidth, backbufferHeight);
	gl->SetMode(MODE_DETECT_ES);

	bool use565 = false;

	// This workaround seems only be needed on some really old devices.
	if (androidVersion < ANDROID_VERSION_ICS) {
		switch (backbufferFormat) {
		case 4:	// PixelFormat.RGB_565
			use565 = true;
			break;
		default:
			break;
		}
	}

	if (!gl->Create(wnd, false, use565)) {
		ELOG("EGL creation failed! (use565=%d)", (int)use565);
		// TODO: What do we do now?
		delete gl;
		return false;
	}
	gl->MakeCurrent();
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	return true;
}

void AndroidEGLGraphicsContext::Shutdown() {
	delete draw_;
	draw_ = nullptr;
	NativeShutdownGraphics();
	gl->ClearCurrent();
	gl->Shutdown();
	delete gl;
	ANativeWindow_release(wnd_);
}

void AndroidEGLGraphicsContext::SwapBuffers() {
	gl->Swap();
}

// Doesn't do much. Just to fit in.
class AndroidJavaEGLGraphicsContext : public GraphicsContext {
public:
	AndroidJavaEGLGraphicsContext() {
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext();
	}
	~AndroidJavaEGLGraphicsContext() {
		delete draw_;
	}
	void Shutdown() override {}
	void SwapBuffers() override {}
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
private:
	Draw::DrawContext *draw_;
};


static const bool g_validate_ = true;
static VulkanContext *g_Vulkan;

class AndroidVulkanContext : public AndroidGraphicsContext {
public:
	AndroidVulkanContext() : draw_(nullptr) {}
	bool Init(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;
	void Resize() override;

	void *GetAPIContext() override { return g_Vulkan; }

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
private:
	Draw::DrawContext *draw_;
};

struct VulkanLogOptions {
	bool breakOnWarning;
	bool breakOnError;
	bool msgBoxOnError;
};
static VulkanLogOptions g_LogOptions;

const char *ObjTypeToString(VkDebugReportObjectTypeEXT type) {
	switch (type) {
	case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return "Instance";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return "PhysicalDevice";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return "Device";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return "Queue";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return "CommandBuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return "DeviceMemory";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return "Buffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return "BufferView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return "Image";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return "ImageView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return "ShaderModule";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return "Pipeline";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return "PipelineLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return "Sampler";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return "DescriptorSet";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return "DescriptorSetLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return "DescriptorPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return "Fence";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return "Semaphore";
	case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT: return "Event";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return "QueryPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return "Framebuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return "RenderPass";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return "PipelineCache";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return "SurfaceKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return "SwapChainKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return "CommandPool";
	default: return "Unknown";
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL Vulkan_Dbg(VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void *pUserData) {
	const VulkanLogOptions *options = (const VulkanLogOptions *)pUserData;
	int loglevel = ANDROID_LOG_INFO;
	if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		loglevel = ANDROID_LOG_ERROR;
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	}
	
	__android_log_print(loglevel, APP_NAME, "[%s] %s Code %d : %s", pLayerPrefix, ObjTypeToString(objType), msgCode, pMsg);

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}

bool AndroidVulkanContext::Init(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	if (g_Vulkan) {
		return false;
	}

	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	ILOG("Creating vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);
	g_Vulkan = new VulkanContext("PPSSPP", gitVer.ToInteger(), VULKAN_FLAG_PRESENT_MAILBOX | VULKAN_FLAG_PRESENT_FIFO_RELAXED);
	if (!g_Vulkan->GetInstance()) {
		ELOG("Failed to create vulkan context");
		return false;
	}

	ILOG("Creating vulkan device");
	if (g_Vulkan->CreateDevice(0) != VK_SUCCESS) {
		ILOG("Failed to create vulkan device: %s", g_Vulkan->InitError().c_str());
		return false;
	}
	int width = desiredBackbufferSizeX;
	int height = desiredBackbufferSizeY;
	if (!width || !height) {
		width = pixel_xres;
		height = pixel_yres;
	}
	ILOG("InitSurfaceAndroid: width=%d height=%d", width, height);
	g_Vulkan->InitSurfaceAndroid(wnd, width, height);
	if (g_validate_) {
		int bits = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		g_Vulkan->InitDebugMsgCallback(&Vulkan_Dbg, bits, &g_LogOptions);
	}
	g_Vulkan->InitObjects(true);
	draw_ = Draw::T3DCreateVulkanContext(g_Vulkan);
	return true;
}

void AndroidVulkanContext::Shutdown() {
	delete draw_;
	draw_ = nullptr;
	NativeShutdownGraphics();

	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->DestroyObjects();
	g_Vulkan->DestroyDebugMsgCallback();
	g_Vulkan->DestroyDevice();

	delete g_Vulkan;
	g_Vulkan = nullptr;

	finalize_glslang();
}

void AndroidVulkanContext::SwapBuffers() {
}

void AndroidVulkanContext::Resize() {
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->DestroyObjects();

	// backbufferResize updated these values.	TODO: Notify another way?
	g_Vulkan->ReinitSurfaceAndroid(pixel_xres, pixel_yres);
	g_Vulkan->InitObjects(g_Vulkan);
}

void AndroidVulkanContext::SwapInterval(int interval) {
}

static std::mutex frameCommandLock;
static std::queue<FrameCommand> frameCommands;

std::string systemName;
std::string langRegion;
std::string mogaVersion;

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
static int display_dpi;
static int display_xres;
static int display_yres;
static int backbuffer_format;	// Android PixelFormat enum

static int desiredBackbufferSizeX;
static int desiredBackbufferSizeY;

static jmethodID postCommand;
static jobject nativeActivity;
static volatile bool exitRenderLoop;
static bool renderLoopRunning;

static float dp_xscale = 1.0f;
static float dp_yscale = 1.0f;

static bool renderer_inited = false;
static bool renderer_ever_inited = false;
// See NativeQueryConfig("androidJavaGL") to change this value.
static bool javaGL = true;

static std::string library_path;
static std::map<SystemPermission, PermissionStatus> permissions;

GraphicsContext *graphicsContext;

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
	const char *str = env->GetStringUTFChars(jstr, 0);
	std::string cpp_string = std::string(str);
	env->ReleaseStringUTFChars(jstr, str);
	return cpp_string;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeActivity_registerCallbacks(JNIEnv *env, jobject obj) {
	nativeActivity = env->NewGlobalRef(obj);
	postCommand = env->GetMethodID(env->GetObjectClass(obj), "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
	ILOG("Got method ID to postCommand: %p", postCommand);
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
	jniEnvUI = env;
	setCurrentThreadName("androidInit");

	ILOG("NativeApp.init() -- begin");
	PROFILE_INIT();

	renderer_inited = false;
	renderer_ever_inited = false;
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
	std::string user_data_path = GetJavaString(env, jdataDir) + "/";
	library_path = GetJavaString(env, jlibraryDir) + "/";
	std::string shortcut_param = GetJavaString(env, jshortcutParam);
	std::string cacheDir = GetJavaString(env, jcacheDir);
	std::string buildBoard = GetJavaString(env, jboard);

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
	}
	else {
		const char *argv[3] = {app_name.c_str(), shortcut_param.c_str(), 0};
		NativeInit(2, argv, user_data_path.c_str(), externalDir.c_str(), cacheDir.c_str());
	}

	// Now that we've loaded config, set javaGL.
	javaGL = NativeQueryConfig("androidJavaGL") == "true";

	ILOG("NativeApp.init() -- end");
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
	AndroidAudio_Init(&NativeMix, library_path, framesPerBuffer, sampleRate);
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioShutdown(JNIEnv *, jclass) {
	AndroidAudio_Shutdown();
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_resume(JNIEnv *, jclass) {
	ILOG("NativeApp.resume() - resuming audio");
	AndroidAudio_Resume();
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_pause(JNIEnv *, jclass) {
	ILOG("NativeApp.pause() - pausing audio");
	AndroidAudio_Pause();
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_shutdown(JNIEnv *, jclass) {
	ILOG("NativeApp.shutdown() -- begin");
	if (renderer_inited) {
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = nullptr;
		renderer_inited = false;
	}

	NativeShutdown();
	VFSShutdown();
	ILOG("NativeApp.shutdown() -- end");
}

// JavaEGL
extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
	if (javaGL && !graphicsContext) {
		graphicsContext = new AndroidJavaEGLGraphicsContext();
	}

	if (renderer_inited) {
		ILOG("NativeApp.displayInit() restoring");
		NativeDeviceLost();
		NativeShutdownGraphics();
		NativeDeviceRestore();
		NativeInitGraphics(graphicsContext);

		ILOG("Restored.");
	} else {
		ILOG("NativeApp.displayInit() first time");
		NativeInitGraphics(graphicsContext);
		renderer_inited = true;
		renderer_ever_inited = true;
	}

	NativeMessageReceived("recreateviews", "");
}

// JavaEGL
extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayResize(JNIEnv *, jobject clazz, jint w, jint h, jint dpi, jfloat refreshRate) {
	ILOG("NativeApp.displayResize(%i x %i, dpi=%i, refresh=%0.2f)", w, h, dpi, refreshRate);

	/*
	g_dpi = dpi;
	g_dpi_scale = 240.0f / (float)g_dpi;
	g_dpi_scale_real = g_dpi_scale;

	pixel_xres = w;
	pixel_yres = h;
	dp_xres = pixel_xres * g_dpi_scale;
	dp_yres = pixel_yres * g_dpi_scale;
	dp_xscale = (float)dp_xres / pixel_xres;
	dp_yscale = (float)dp_yres / pixel_yres;
	*/
	// display_hz = refreshRate;

	pixel_xres = w;
	pixel_yres = h;
	// backbuffer_format = format;

	g_dpi = (int)display_dpi;
	g_dpi_scale = 240.0f / (float)g_dpi;
	g_dpi_scale_real = g_dpi_scale;

	dp_xres = display_xres * g_dpi_scale;
	dp_yres = display_yres * g_dpi_scale;

	// Touch scaling is from display pixels to dp pixels.
	dp_xscale = (float)dp_xres / (float)display_xres;
	dp_yscale = (float)dp_yres / (float)display_yres;

	pixel_in_dps = (float)pixel_xres / dp_xres;

	NativeResized();
}

// JavaEGL
extern "C" void Java_org_ppsspp_ppsspp_NativeRenderer_displayRender(JNIEnv *env, jobject obj) {
	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		hasSetThreadName = true;
		setCurrentThreadName("AndroidRender");
	}

	if (renderer_inited) {
		NativeUpdate();

		NativeRender(graphicsContext);
		time_update();
	} else {
		ELOG("BAD: Ended up in nativeRender even though app has quit.%s", "");
		// Shouldn't really get here. Let's draw magenta.
		// TODO: Should we have GL here?
		glDepthMask(GL_TRUE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(1.0, 0.0, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	std::lock_guard<std::mutex> guard(frameCommandLock);
	if (!nativeActivity) {
		while (!frameCommands.empty())
			frameCommands.pop();
		return;
	}
	// Still under lock here.
	ProcessFrameCommands(env);
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
		// TODO: Add support for other permissions
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_PENDING;
		NativePermissionStatus(SYSTEM_PERMISSION_STORAGE, PERMISSION_STATUS_PENDING);
	} else if (msg == "permission_denied") {
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_DENIED;
		NativePermissionStatus(SYSTEM_PERMISSION_STORAGE, PERMISSION_STATUS_PENDING);
	} else if (msg == "permission_granted") {
		permissions[SYSTEM_PERMISSION_STORAGE] = PERMISSION_STATUS_GRANTED;
		NativePermissionStatus(SYSTEM_PERMISSION_STORAGE, PERMISSION_STATUS_PENDING);
	}

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
	display_dpi = dpi;
	display_hz = refreshRate;
}

extern "C" void JNICALL Java_org_ppsspp_ppsspp_NativeApp_backbufferResize(JNIEnv *, jclass, jint bufw, jint bufh, jint format) {
	ILOG("NativeApp.backbufferResize(%d x %d)", bufw, bufh);

	// pixel_*res is the backbuffer resolution.
	pixel_xres = bufw;
	pixel_yres = bufh;
	backbuffer_format = format;

	g_dpi = (int)display_dpi;
	g_dpi_scale = 240.0f / (float)g_dpi;
	g_dpi_scale_real = g_dpi_scale;

	dp_xres = display_xres * g_dpi_scale;
	dp_yres = display_yres * g_dpi_scale;

	// Touch scaling is from display pixels to dp pixels.
	dp_xscale = (float)dp_xres / (float)display_xres;
	dp_yscale = (float)dp_yres / (float)display_yres;

	pixel_in_dps = (float)pixel_xres / dp_xres;

	ILOG("dp_xscale=%f dp_yscale=%f", dp_xscale, dp_yscale);
	ILOG("dp_xres=%d dp_yres=%d", dp_xres, dp_yres);
	ILOG("pixel_xres=%d pixel_yres=%d", pixel_xres, pixel_yres);
	ILOG("g_dpi=%d g_dpi_scale=%f", g_dpi, g_dpi_scale);

	NativeResized();
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

// Call this under frameCommandLock.
static void ProcessFrameCommands(JNIEnv *env) {
	while (!frameCommands.empty()) {
		FrameCommand frameCmd;
		frameCmd = frameCommands.front();
		frameCommands.pop();

		WLOG("frameCommand! '%s' '%s'", frameCmd.command.c_str(), frameCmd.params.c_str());

		jstring cmd = env->NewStringUTF(frameCmd.command.c_str());
		jstring param = env->NewStringUTF(frameCmd.params.c_str());
		env->CallVoidMethod(nativeActivity, postCommand, cmd, param);
		env->DeleteLocalRef(cmd);
		env->DeleteLocalRef(param);
	}
}

extern "C" bool JNICALL Java_org_ppsspp_ppsspp_NativeActivity_runEGLRenderLoop(JNIEnv *env, jobject obj, jobject _surf) {
	ANativeWindow *wnd = ANativeWindow_fromSurface(env, _surf);

	WLOG("runEGLRenderLoop. display_xres=%d display_yres=%d", display_xres, display_yres);

	if (wnd == nullptr) {
		ELOG("Error: Surface is null.");
		return false;
	}
	
	bool vulkan = g_Config.iGPUBackend == GPU_BACKEND_VULKAN;

	AndroidGraphicsContext *graphicsContext;
	if (vulkan) {
		graphicsContext = new AndroidVulkanContext();
	} else {
		graphicsContext = new AndroidEGLGraphicsContext();
	}

	if (!graphicsContext->Init(wnd, desiredBackbufferSizeX, desiredBackbufferSizeY, backbuffer_format, androidVersion)) {
		ELOG("Failed to initialize graphics context.");
		delete graphicsContext;
		return false;
	}

	if (!renderer_inited) {
		NativeInitGraphics(graphicsContext);
		if (renderer_ever_inited) {
			NativeDeviceRestore();
		}
		renderer_inited = true;
		renderer_ever_inited = true;
	}

	exitRenderLoop = false;
	renderLoopRunning = true;

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

	ILOG("After render loop.");
	g_gameInfoCache->WorkQueue()->Flush();

	NativeDeviceLost();
	renderer_inited = false;

	graphicsContext->Shutdown();
	delete graphicsContext;
	graphicsContext = nullptr;
	renderLoopRunning = false;
	WLOG("Render loop function exited.");
	return true;
}

extern "C" jstring Java_org_ppsspp_ppsspp_ShortcutActivity_queryGameName(JNIEnv *env, jclass, jstring jpath) {
	std::string path = GetJavaString(env, jpath);
	std::string result = "";

	GameInfoCache *cache = new GameInfoCache();
	GameInfo *info = cache->GetInfo(nullptr, path, 0);
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
