#pragma once

#ifdef ANDROID
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenXR", __VA_ARGS__);
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "OpenXR", __VA_ARGS__);
#else
#define ALOGE(...) printf(__VA_ARGS__)
#define ALOGV(...) printf(__VA_ARGS__)
#endif

//OpenXR
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#include <math.h>
#include <openxr.h>
#include <openxr_platform.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#ifdef _DEBUG
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

#define OPENXR_HAS_PERFORMANCE_EXTENSION

enum { ovrMaxLayerCount = 1 };
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
	bool Multiview;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
	unsigned int* DepthBuffers;
	unsigned int* FrameBuffers;
	bool Acquired;
} ovrFramebuffer;

typedef struct {
	ovrFramebuffer FrameBuffer;
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

typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
	char AppName[64];
	int AppVersion;
} ovrJava;

typedef struct {
	uint64_t frameIndex;
	ovrApp appState;
	ovrJava java;
	float predictedDisplayTime;
} engine_t;

void VR_Init( ovrJava java );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);
