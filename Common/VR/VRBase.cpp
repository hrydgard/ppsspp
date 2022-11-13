#include "VRBase.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

enum ConfigsSetEXT {
    UNREAL_VERSION = 0,
    TRACKING_ORIGIN,
    OPENGL_NOERROR,
    ENABLE_SIX_DOF,
    PRESENTATION_FLAG,
    ENABLE_CPT,
    PLATFORM,
    FOVEATION_LEVEL,
    SET_DISPLAY_RATE = 8,
    MRC_TEXTURE_ID = 9,
};

enum PxrTrackingDof {
    PXR_TRACKING_3DOF = 0,
    PXR_TRACKING_6DOF = 1
};

typedef XrResult (XRAPI_PTR *PFN_xrSetEngineVersionPico)(XrInstance instance,const char* version);
typedef XrResult (XRAPI_PTR *PFN_xrStartCVControllerThreadPico)(XrInstance instance,int headSensorState, int handSensorState);
typedef XrResult (XRAPI_PTR *PFN_xrStopCVControllerThreadPico)(XrInstance instance,int headSensorState, int handSensorState);
typedef XrResult (XRAPI_PTR *PFN_xrSetConfigPICO) (XrSession instance, enum ConfigsSetEXT configIndex, const char *configData);

PFN_xrSetConfigPICO pfnXrSetConfigPICO = nullptr;
PFN_xrSetEngineVersionPico pfnXrSetEngineVersionPico = nullptr;
PFN_xrStartCVControllerThreadPico pfnXrStartCVControllerThreadPico = nullptr;
PFN_xrStopCVControllerThreadPico pfnXrStopCVControllerThreadPico = nullptr;

static bool vr_platform[VR_PLATFORM_MAX];
static engine_t vr_engine;
int vr_initialized = 0;

void VR_Init( void* system, const char* name, int version ) {
	if (vr_initialized)
		return;

	if (!XRLoad()) {
		return;
	}

	ovrApp_Clear(&vr_engine.appState);

#ifdef ANDROID
	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
	xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
	if (xrInitializeLoaderKHR != NULL) {
		ovrJava* java = (ovrJava*)system;
		XrLoaderInitInfoAndroidKHR loaderInitializeInfo;
		memset(&loaderInitializeInfo, 0, sizeof(loaderInitializeInfo));
		loaderInitializeInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
		loaderInitializeInfo.next = NULL;
		loaderInitializeInfo.applicationVM = java->Vm;
		loaderInitializeInfo.applicationContext = java->ActivityObject;
		xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfo);
	}
#endif

	std::vector<const char *> extensions;
	if (VR_GetPlatformFlag(VR_PLATFORM_RENDERER_VULKAN)) {
		extensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	} else {
#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
		extensions.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
#endif
	}
	extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
#ifdef ANDROID
	if (VR_GetPlatformFlag(VR_PLATFORM_PERFORMANCE_EXT)) {
		extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
		extensions.push_back(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
	}
	if (VR_GetPlatformFlag(VR_PLATFORM_PICO_INIT)) {
		extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
		extensions.push_back("XR_PICO_android_controller_function_ext_enable");
		extensions.push_back("XR_PICO_view_state_ext_enable");
		extensions.push_back("XR_PICO_frame_end_info_ext");
		extensions.push_back("XR_PICO_configs_ext");
		extensions.push_back("XR_PICO_reset_sensor");
	}
#endif

	// Create the OpenXR instance.
	XrApplicationInfo appInfo;
	memset(&appInfo, 0, sizeof(appInfo));
	strcpy(appInfo.applicationName, name);
	strcpy(appInfo.engineName, name);
	appInfo.applicationVersion = version;
	appInfo.engineVersion = version;
	appInfo.apiVersion = XR_CURRENT_API_VERSION;

	XrInstanceCreateInfo instanceCreateInfo;
	memset(&instanceCreateInfo, 0, sizeof(instanceCreateInfo));
	instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.next = NULL;
	instanceCreateInfo.createFlags = 0;
	instanceCreateInfo.applicationInfo = appInfo;
	instanceCreateInfo.enabledApiLayerCount = 0;
	instanceCreateInfo.enabledApiLayerNames = NULL;
	instanceCreateInfo.enabledExtensionCount = (uint32_t)extensions.size();
	instanceCreateInfo.enabledExtensionNames = extensions.data();

#ifdef ANDROID
	XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	if (VR_GetPlatformFlag(VR_PLATFORM_PICO_INIT)) {
		ovrJava* java = (ovrJava*)system;
		instanceCreateInfoAndroid.applicationVM = java->Vm;
		instanceCreateInfoAndroid.applicationActivity = java->ActivityObject;
		instanceCreateInfo.next = (XrBaseInStructure*)&instanceCreateInfoAndroid;
	}
#endif

	XrResult initResult;
	OXR(initResult = xrCreateInstance(&instanceCreateInfo, &vr_engine.appState.Instance));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to create XR instance: %d.", initResult);
		exit(1);
	}

	XRLoadInstanceFunctions(vr_engine.appState.Instance);

	if (VR_GetPlatformFlag(VR_PLATFORM_PICO_INIT)) {
		xrGetInstanceProcAddr(vr_engine.appState.Instance, "xrSetEngineVersionPico", (PFN_xrVoidFunction*)(&pfnXrSetEngineVersionPico));
		xrGetInstanceProcAddr(vr_engine.appState.Instance, "xrStartCVControllerThreadPico", (PFN_xrVoidFunction*)(&pfnXrStartCVControllerThreadPico));
		xrGetInstanceProcAddr(vr_engine.appState.Instance, "xrStopCVControllerThreadPico", (PFN_xrVoidFunction*)(&pfnXrStopCVControllerThreadPico));
		xrGetInstanceProcAddr(vr_engine.appState.Instance,"xrSetConfigPICO", (PFN_xrVoidFunction*)(&pfnXrSetConfigPICO));
		if (pfnXrSetEngineVersionPico != nullptr) pfnXrSetEngineVersionPico(vr_engine.appState.Instance, "2.8.0.1");
		if (pfnXrStartCVControllerThreadPico != nullptr) pfnXrStartCVControllerThreadPico(vr_engine.appState.Instance, PXR_TRACKING_6DOF, PXR_TRACKING_6DOF);
	}

	XrInstanceProperties instanceInfo;
	instanceInfo.type = XR_TYPE_INSTANCE_PROPERTIES;
	instanceInfo.next = NULL;
	OXR(xrGetInstanceProperties(vr_engine.appState.Instance, &instanceInfo));
	ALOGV(
			"Runtime %s: Version : %u.%u.%u",
			instanceInfo.runtimeName,
			XR_VERSION_MAJOR(instanceInfo.runtimeVersion),
			XR_VERSION_MINOR(instanceInfo.runtimeVersion),
			XR_VERSION_PATCH(instanceInfo.runtimeVersion));

	XrSystemGetInfo systemGetInfo;
	memset(&systemGetInfo, 0, sizeof(systemGetInfo));
	systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrSystemId systemId;
	OXR(initResult = xrGetSystem(vr_engine.appState.Instance, &systemGetInfo, &systemId));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to get system.");
		exit(1);
	}

	// Get the graphics requirements.
	if (VR_GetPlatformFlag(VR_PLATFORM_RENDERER_VULKAN)) {
		PFN_xrGetVulkanGraphicsRequirementsKHR pfnGetVulkanGraphicsRequirementsKHR = NULL;
		OXR(xrGetInstanceProcAddr(
				vr_engine.appState.Instance,
				"xrGetVulkanGraphicsRequirementsKHR",
				(PFN_xrVoidFunction*)(&pfnGetVulkanGraphicsRequirementsKHR)));

		XrGraphicsRequirementsVulkanKHR graphicsRequirements = {};
		graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
		OXR(pfnGetVulkanGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));
	} else {
#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
		PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = NULL;
		OXR(xrGetInstanceProcAddr(
				vr_engine.appState.Instance,
				"xrGetOpenGLESGraphicsRequirementsKHR",
				(PFN_xrVoidFunction*)(&pfnGetOpenGLESGraphicsRequirementsKHR)));

		XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {};
		graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
		OXR(pfnGetOpenGLESGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));
#endif
	}

#ifdef ANDROID
	vr_engine.appState.MainThreadTid = gettid();
#endif
	vr_engine.appState.SystemId = systemId;
	vr_initialized = 1;
}

void VR_Destroy( engine_t* engine ) {
	if (engine == &vr_engine) {
		if (VR_GetPlatformFlag(VR_PLATFORM_PICO_INIT)) {
			if (pfnXrStopCVControllerThreadPico != nullptr) {
				pfnXrStopCVControllerThreadPico(engine->appState.Instance, PXR_TRACKING_6DOF, PXR_TRACKING_6DOF);
			}
		}

		xrDestroyInstance(engine->appState.Instance);
		ovrApp_Destroy(&engine->appState);
	}
}

void VR_EnterVR( engine_t* engine, XrGraphicsBindingVulkanKHR* graphicsBindingVulkan ) {

	if (engine->appState.Session) {
		ALOGE("VR_EnterVR called with existing session");
		return;
	}

	// Create the OpenXR Session.
	XrSessionCreateInfo sessionCreateInfo = {};
#ifdef ANDROID
	XrGraphicsBindingOpenGLESAndroidKHR graphicsBindingGL = {};
#elif XR_USE_GRAPHICS_API_OPENGL
	XrGraphicsBindingOpenGLWin32KHR graphicsBindingGL = {};
#endif
	memset(&sessionCreateInfo, 0, sizeof(sessionCreateInfo));
	if (VR_GetPlatformFlag(VR_PLATFORM_RENDERER_VULKAN)) {
		sessionCreateInfo.next = graphicsBindingVulkan;
	} else {
#ifdef ANDROID
		graphicsBindingGL.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
		graphicsBindingGL.next = NULL;
		graphicsBindingGL.display = eglGetCurrentDisplay();
		graphicsBindingGL.config = eglGetCurrentSurface(EGL_DRAW);
		graphicsBindingGL.context = eglGetCurrentContext();
		sessionCreateInfo.next = &graphicsBindingGL;
#else
		//TODO:PCVR definition
#endif
	}
	sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessionCreateInfo.createFlags = 0;
	sessionCreateInfo.systemId = engine->appState.SystemId;

	XrResult initResult;
	OXR(initResult = xrCreateSession(engine->appState.Instance, &sessionCreateInfo, &engine->appState.Session));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to create XR session: %d.", initResult);
		exit(1);
	}
	if (VR_GetPlatformFlag(VR_PLATFORM_PICO_INIT)) {
		pfnXrSetConfigPICO(engine->appState.Session, TRACKING_ORIGIN, "1");
	}

	// Create a space to the first path
	XrReferenceSpaceCreateInfo spaceCreateInfo = {};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.HeadSpace));
}

void VR_LeaveVR( engine_t* engine ) {
	if (engine->appState.Session) {
		OXR(xrDestroySpace(engine->appState.HeadSpace));
		// StageSpace is optional.
		if (engine->appState.StageSpace != XR_NULL_HANDLE) {
			OXR(xrDestroySpace(engine->appState.StageSpace));
		}
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
		engine->appState.CurrentSpace = XR_NULL_HANDLE;
		OXR(xrDestroySession(engine->appState.Session));
		engine->appState.Session = NULL;
	}
}

engine_t* VR_GetEngine( void ) {
	return &vr_engine;
}

bool VR_GetPlatformFlag(VRPlatformFlag flag) {
	return vr_platform[flag];
}

void VR_SetPlatformFLag(VRPlatformFlag flag, bool value) {
	vr_platform[flag] = value;
}
