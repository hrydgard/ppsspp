#pragma once

#ifdef ANDROID
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenXR", __VA_ARGS__);
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "OpenXR", __VA_ARGS__);
#else
#include <cstdio>
#define ALOGE(...) printf(__VA_ARGS__)
#define ALOGV(...) printf(__VA_ARGS__)
#endif

//Vulkan
#ifdef VK_USE_NATIVE_LIB
#include <vulkan/vulkan.h>
#else
#include "Common/GPU/Vulkan/VulkanLoader.h"
using namespace PPSSPP_VK;
#endif

#ifdef ANDROID
//OpenXR
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#elif defined(_WIN32)
#include "Common/CommonWindows.h"
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#else
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#endif

#include <math.h>
#include <openxr.h>
#include <openxr_platform.h>

#ifdef ANDROID

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#endif

#if defined(_DEBUG) && defined(ANDROID)
static const char* GlErrorString(GLenum error) {
	switch (error) {
		case GL_NO_ERROR:
			return "GL_NO_ERROR";
		case GL_INVALID_ENUM:
			return "GL_INVALID_ENUM";
		case GL_INVALID_VALUE:
			return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION:
			return "GL_INVALID_OPERATION";
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			return "GL_INVALID_FRAMEBUFFER_OPERATION";
		case GL_OUT_OF_MEMORY:
			return "GL_OUT_OF_MEMORY";
		default:
			return "unknown";
	}
}

static void GLCheckErrors(char* file, int line) {
	for (int i = 0; i < 10; i++) {
		const GLenum error = glGetError();
		if (error == GL_NO_ERROR) {
			break;
		}
		ALOGE("GL error on line %s:%d %s", file, line, GlErrorString(error));
	}
}

#define GL(func) func; GLCheckErrors(__FILE__ , __LINE__);
#else
#define GL(func) func;
#endif

#if defined(_DEBUG)
static void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError) {
	if (XR_FAILED(result)) {
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, errorBuffer);
		if (failOnError) {
			ALOGE("OpenXR error: %s: %s\n", function, errorBuffer);
		} else {
			ALOGV("OpenXR error: %s: %s\n", function, errorBuffer);
		}
	}
}
#define OXR(func) OXR_CheckErrors(VR_GetEngine()->appState.Instance, func, #func, true);
#else
#define OXR(func) func;
#endif

enum { ovrMaxLayerCount = 2 };
enum { ovrMaxNumEyes = 2 };

typedef union {
	XrCompositionLayerProjection Projection;
	XrCompositionLayerCylinderKHR Cylinder;
} ovrCompositorLayer_Union;

typedef struct {
	XrSwapchain Handle;
	uint32_t Width;
	uint32_t Height;
} ovrSwapChain;

typedef struct {
	int Width;
	int Height;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	ovrSwapChain DepthSwapChain;
	void* ColorSwapChainImage;
	void* DepthSwapChainImage;
	unsigned int* GLFrameBuffers;
	VkFramebuffer* VKFrameBuffers;
	VkImageView* VKColorImages;
	VkImageView* VKDepthImages;

	bool Acquired;
	XrGraphicsBindingVulkanKHR* VKContext;
} ovrFramebuffer;

typedef struct {
	bool Multiview;
	ovrFramebuffer FrameBuffer[ovrMaxNumEyes];
} ovrRenderer;

typedef struct {
	int Focused;

	XrInstance Instance;
	XrSession Session;
	XrViewConfigurationProperties ViewportConfig;
	XrViewConfigurationView ViewConfigurationView[ovrMaxNumEyes];
	XrSystemId SystemId;
	XrSpace HeadSpace;
	XrSpace StageSpace;
	XrSpace FakeStageSpace;
	XrSpace CurrentSpace;
	int SessionActive;

	int SwapInterval;
	// These threads will be marked as performance threads.
	int MainThreadTid;
	int RenderThreadTid;
	ovrCompositorLayer_Union Layers[ovrMaxLayerCount];
	int LayerCount;

	ovrRenderer Renderer;
} ovrApp;

#ifdef ANDROID
typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
} ovrJava;
#endif

typedef struct {
	uint64_t frameIndex;
	ovrApp appState;
	float predictedDisplayTime;
	XrGraphicsBindingVulkanKHR graphicsBindingVulkan;
} engine_t;

enum VRPlatformFlag {
	VR_PLATFORM_CONTROLLER_PICO,
	VR_PLATFORM_CONTROLLER_QUEST,
	VR_PLATFORM_PERFORMANCE_EXT,
	VR_PLATFORM_PICO_INIT,
	VR_PLATFORM_RENDERER_VULKAN,
	VR_PLATFORM_TRACKING_FLOOR,
	VR_PLATFORM_MAX
};

void VR_Init( void* system, const char* name, int version );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine, XrGraphicsBindingVulkanKHR* graphicsBindingVulkan );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );
bool VR_GetPlatformFLag(VRPlatformFlag flag);
void VR_SetPlatformFLag(VRPlatformFlag flag, bool value);

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);
