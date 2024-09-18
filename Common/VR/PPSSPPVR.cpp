#include "Common/VR/PPSSPPVR.h"

#include "Common/VR/VRBase.h"
#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
#include "Common/GPU/OpenGL/GLRenderManager.h"
#endif

#include "Common/VR/VRInput.h"
#include "Common/VR/VRMath.h"
#include "Common/VR/VRRenderer.h"

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

#include "Common/GPU/Vulkan/VulkanContext.h"

#include "Common/Math/lin/matrix4x4.h"

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceCtrl.h"

#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "Core/System.h"

enum VRMatrix {
	VR_PROJECTION_MATRIX,
	VR_VIEW_MATRIX_LEFT_EYE,
	VR_VIEW_MATRIX_RIGHT_EYE,
	VR_MATRIX_COUNT
};

enum VRMirroring {
	VR_MIRRORING_AXIS_X,
	VR_MIRRORING_AXIS_Y,
	VR_MIRRORING_AXIS_Z,
	VR_MIRRORING_PITCH,
	VR_MIRRORING_YAW,
	VR_MIRRORING_ROLL,
	VR_MIRRORING_COUNT
};

static VRAppMode appMode = VR_MENU_MODE;
static std::map<int, std::map<int, float> > pspAxis;
static std::map<int, bool> pspKeys;  // key can be virtual, so not using the enum.

static int vr3DGeometryCount = 0;
static long vrCompat[VR_COMPAT_MAX];
static bool vrFlatForced = false;
static bool vrFlatGame = false;
static float vrMatrix[VR_MATRIX_COUNT][16];
static bool vrMirroring[VR_MIRRORING_COUNT];
static int vrMirroringVariant = 0;
static XrView vrView[2];

static void (*cbNativeAxis)(const AxisInput *axis, size_t count);
static bool (*cbNativeKey)(const KeyInput &key);
static void (*cbNativeTouch)(const TouchInput &touch);

/*
================================================================================

VR button mapping

================================================================================
*/

struct ButtonMapping {
	ovrButton ovr;
	InputKeyCode keycode;
	bool pressed;
	int repeat;

	ButtonMapping(InputKeyCode keycode, ovrButton ovr) {
		this->keycode = keycode;
		this->ovr = ovr;
		pressed = false;
		repeat = 0;
	}
};

static std::vector<ButtonMapping> leftControllerMapping = {
		ButtonMapping(NKCODE_BUTTON_X, ovrButton_X),
		ButtonMapping(NKCODE_BUTTON_Y, ovrButton_Y),
		ButtonMapping(NKCODE_ALT_LEFT, ovrButton_GripTrigger),
		ButtonMapping(NKCODE_DPAD_UP, ovrButton_Up),
		ButtonMapping(NKCODE_DPAD_DOWN, ovrButton_Down),
		ButtonMapping(NKCODE_DPAD_LEFT, ovrButton_Left),
		ButtonMapping(NKCODE_DPAD_RIGHT, ovrButton_Right),
		ButtonMapping(NKCODE_BUTTON_THUMBL, ovrButton_LThumb),
		ButtonMapping(NKCODE_ENTER, ovrButton_Trigger),
		ButtonMapping(NKCODE_BACK, ovrButton_Enter),
};

static std::vector<ButtonMapping> rightControllerMapping = {
		ButtonMapping(NKCODE_BUTTON_A, ovrButton_A),
		ButtonMapping(NKCODE_BUTTON_B, ovrButton_B),
		ButtonMapping(NKCODE_ALT_RIGHT, ovrButton_GripTrigger),
		ButtonMapping(NKCODE_DPAD_UP, ovrButton_Up),
		ButtonMapping(NKCODE_DPAD_DOWN, ovrButton_Down),
		ButtonMapping(NKCODE_DPAD_LEFT, ovrButton_Left),
		ButtonMapping(NKCODE_DPAD_RIGHT, ovrButton_Right),
		ButtonMapping(NKCODE_BUTTON_THUMBR, ovrButton_RThumb),
		ButtonMapping(NKCODE_ENTER, ovrButton_Trigger),
};

static const InputDeviceID controllerIds[] = {DEVICE_ID_XR_CONTROLLER_LEFT, DEVICE_ID_XR_CONTROLLER_RIGHT};
static std::vector<ButtonMapping> controllerMapping[2] = {
		leftControllerMapping,
		rightControllerMapping
};
static int mouseController = 1;
static bool mousePressed = false;

inline float clampFloat(float x, float minValue, float maxValue) {
	if (x < minValue) return minValue;
	if (x > maxValue) return maxValue;
	return x;
}

/*
================================================================================

VR app flow integration

================================================================================
*/

bool IsVREnabled() {
	// For now, let the OPENXR build flag control enablement.
	// This will change.
#ifdef OPENXR
	return true;
#else
	return false;
#endif
}

#if PPSSPP_PLATFORM(ANDROID)
void InitVROnAndroid(void* vm, void* activity, const char* system, int version, const char* name) {

	//Get device vendor (uppercase)
	char vendor[64];
	sscanf(system, "%[^:]", vendor);
	for (unsigned int i = 0; i < strlen(vendor); i++) {
		if ((vendor[i] >= 'a') && (vendor[i] <= 'z')) {
			vendor[i] = vendor[i] - 'a' + 'A';
		}
	}

	//Set platform flags
	if (strcmp(vendor, "PICO") == 0) {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_PICO, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_INSTANCE, true);
		VR_SetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING, 1.0f);
	} else {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PASSTHROUGH, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
		VR_SetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING, 1.3f);
	}
	VR_SetPlatformFLag(VR_PLATFORM_RENDERER_VULKAN, (GPUBackend)g_Config.iGPUBackend == GPUBackend::VULKAN);

	//Init VR
	ovrJava java;
	java.Vm = (JavaVM*)vm;
	java.ActivityObject = (jobject)activity;
	VR_Init(&java, name, version);
}
#endif

void EnterVR(bool firstStart, void* vulkanContext) {
	if (firstStart) {
		engine_t* engine = VR_GetEngine();
		bool useVulkan = (GPUBackend)g_Config.iGPUBackend == GPUBackend::VULKAN;
		if (useVulkan) {
			auto* context = (VulkanContext*)vulkanContext;
			engine->graphicsBindingVulkan = {};
			engine->graphicsBindingVulkan.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
			engine->graphicsBindingVulkan.next = NULL;
			engine->graphicsBindingVulkan.device = context->GetDevice();
			engine->graphicsBindingVulkan.instance = context->GetInstance();
			engine->graphicsBindingVulkan.physicalDevice = context->GetCurrentPhysicalDevice();
			engine->graphicsBindingVulkan.queueFamilyIndex = context->GetGraphicsQueueFamilyIndex();
			engine->graphicsBindingVulkan.queueIndex = 0;
			VR_EnterVR(engine, &engine->graphicsBindingVulkan);
		} else {
			VR_EnterVR(engine, nullptr);
		}
		IN_VRInit(engine);
	}
	VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, false);
}

void GetVRResolutionPerEye(int* width, int* height) {
	if (VR_GetEngine()->appState.Instance) {
		VR_GetResolution(VR_GetEngine(), width, height);
	}
}

void SetVRCallbacks(void (*axis)(const AxisInput *axis, size_t count), bool(*key)(const KeyInput &key), void (*touch)(const TouchInput &touch)) {
	cbNativeAxis = axis;
	cbNativeKey = key;
	cbNativeTouch = touch;
}

/*
================================================================================

VR input integration

================================================================================
*/

void SetVRAppMode(VRAppMode mode) {
	appMode = mode;
}

void UpdateVRInput(bool haptics, float dp_xscale, float dp_yscale) {
	//axis
	if (pspKeys[(int)VIRTKEY_VR_CAMERA_ADJUST]) {
		AxisInput axis[2] = {};
		axis[0].deviceId = DEVICE_ID_DEFAULT;
		axis[1].deviceId = DEVICE_ID_DEFAULT;
		for (int j = 0; j < 2; j++) {
			XrVector2f joystick = IN_VRGetJoystickState(j);

			//horizontal
			axis[0].axisId = j == 0 ? JOYSTICK_AXIS_X : JOYSTICK_AXIS_Z;
			axis[0].value = joystick.x;

			//vertical
			axis[1].axisId = j == 0 ? JOYSTICK_AXIS_Y : JOYSTICK_AXIS_RZ;
			axis[1].value = -joystick.y;
			cbNativeAxis(axis, 2);
		}
	}

	//buttons
	KeyInput keyInput = {};
	for (int j = 0; j < 2; j++) {
		int status = IN_VRGetButtonState(j);
		for (ButtonMapping& m : controllerMapping[j]) {

			//fill KeyInput structure
			bool pressed = status & m.ovr;
			keyInput.flags = pressed ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = m.keycode;
			keyInput.deviceId = controllerIds[j];

			//process the key action

			if (m.pressed != pressed) {
				if (pressed && haptics) {
					INVR_Vibrate(100, j, 1000);
				}
				cbNativeKey(keyInput);
				m.pressed = pressed;
				m.repeat = 0;
			} else if (pressed && (m.repeat > 30)) {
				keyInput.flags |= KEY_IS_REPEAT;
				cbNativeKey(keyInput);
				m.repeat = 0;
			} else {
				m.repeat++;
			}
		}
	}

	// Camera adjust
	if (pspKeys[VIRTKEY_VR_CAMERA_ADJUST]) {
		for (auto& device : pspAxis) {
			for (auto& axis : device.second) {
				switch(axis.first) {
					case JOYSTICK_AXIS_X:
						if (axis.second < -0.75f) g_Config.fCameraSide -= 0.1f;
						if (axis.second > 0.75f) g_Config.fCameraSide += 0.1f;
						g_Config.fCameraSide = clampFloat(g_Config.fCameraSide, -150.0f, 150.0f);
						break;
					case JOYSTICK_AXIS_Y:
						if (axis.second > 0.75f) g_Config.fCameraHeight -= 0.1f;
						if (axis.second < -0.75f) g_Config.fCameraHeight += 0.1f;
						g_Config.fCameraHeight = clampFloat(g_Config.fCameraHeight, -150.0f, 150.0f);
						break;
					case JOYSTICK_AXIS_Z:
						if (axis.second < -0.75f) g_Config.fCameraPitch -= 0.5f;
						if (axis.second > 0.75f) g_Config.fCameraPitch += 0.5f;
						g_Config.fCameraPitch = clampFloat(g_Config.fCameraPitch, -90.0f, 90.0f);
						break;
					case JOYSTICK_AXIS_RZ:
						if (axis.second > 0.75f) g_Config.fCameraDistance -= 0.1f;
						if (axis.second < -0.75f) g_Config.fCameraDistance += 0.1f;
						g_Config.fCameraDistance = clampFloat(g_Config.fCameraDistance, -150.0f, 150.0f);
						break;
				}
			}
		}
	}

	//enable or disable mouse
	for (int j = 0; j < 2; j++) {
		bool pressed = IN_VRGetButtonState(j) & ovrButton_Trigger;
		if (pressed) {
			int lastController = mouseController;
			mouseController = j;

			//prevent misclicks when changing the left/right controller
			if (lastController != mouseController) {
				mousePressed = true;
			}
		}
	}

	//mouse cursor
	if ((mouseController >= 0) && ((appMode == VR_DIALOG_MODE) || (appMode == VR_MENU_MODE))) {
		//get position on screen
		XrPosef pose = IN_VRGetPose(mouseController);
		XrVector3f angles = XrQuaternionf_ToEulerAngles(pose.orientation);
		float width = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_WIDTH);
		float height = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_HEIGHT);
		float cx = width / 2;
		float cy = height / 2;
		float speed = (cx + cy) / 2;
		float x = cx - tan(ToRadians(angles.y - VR_GetConfigFloat(VR_CONFIG_MENU_YAW))) * speed;
		float y = cy - tan(ToRadians(angles.x)) * speed * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
		x *= VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);
		y *= VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);

		//set renderer
		VR_SetConfig(VR_CONFIG_MOUSE_X, (int)x);
		VR_SetConfig(VR_CONFIG_MOUSE_Y, (int)y);
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 6 * (int)pow(VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE), 0.25f));
		VR_SetConfig(VR_CONFIG_CANVAS_6DOF, g_Config.bEnable6DoF);

		//inform engine about the status
		TouchInput touch;
		touch.id = mouseController;
		touch.x = x * dp_xscale;
		touch.y = (height - y - 1) * dp_yscale / VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
		bool pressed = IN_VRGetButtonState(mouseController) & ovrButton_Trigger;
		if (mousePressed != pressed) {
			if (pressed) {
				touch.flags = TOUCH_DOWN;
				cbNativeTouch(touch);
				touch.flags = TOUCH_UP;
				cbNativeTouch(touch);
			}
			mousePressed = pressed;
		}

		// mouse wheel emulation
		// TODO: Spams key-up events if nothing changed!
		for (int j = 0; j < 2; j++) {
			keyInput.deviceId = controllerIds[j];
			float scroll = -IN_VRGetJoystickState(j).y;
			keyInput.flags = scroll < -0.5f ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
			cbNativeKey(keyInput);
			keyInput.flags = scroll > 0.5f ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
			cbNativeKey(keyInput);
		}
	} else {
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 0);
	}
}

bool UpdateVRAxis(const AxisInput *axes, size_t count) {
	for (size_t i = 0; i < count; i++) {
		const AxisInput &axis = axes[i];
		if (pspAxis.find(axis.deviceId) == pspAxis.end()) {
			pspAxis[axis.deviceId] = std::map<int, float>();
		}
		pspAxis[axis.deviceId][axis.axisId] = axis.value;
	}
	return !pspKeys[VIRTKEY_VR_CAMERA_ADJUST];
}

bool UpdateVRKeys(const KeyInput &key) {
	//store key value
	std::vector<int> nativeKeys;
	bool wasScreenKeyOn = pspKeys[CTRL_SCREEN];
	bool wasCameraAdjustOn = pspKeys[VIRTKEY_VR_CAMERA_ADJUST];
	if (KeyMap::InputMappingToPspButton(InputMapping(key.deviceId, key.keyCode), &nativeKeys)) {
		for (int& nativeKey : nativeKeys) {
			pspKeys[nativeKey] = key.flags & KEY_DOWN;
		}
	}

	//block VR controller keys in the UI mode
	if ((key.deviceId == controllerIds[0]) || (key.deviceId == controllerIds[1])) {
		if ((appMode == VR_DIALOG_MODE) || (appMode == VR_MENU_MODE)) {
			switch (key.keyCode) {
				case NKCODE_BACK:
				case NKCODE_EXT_MOUSEWHEEL_UP:
				case NKCODE_EXT_MOUSEWHEEL_DOWN:
					return true;
				default:
					return false;
			}
		}
	}

	// Update force flat 2D mode
	if (g_Config.bManualForceVR) {
		if (!wasScreenKeyOn && pspKeys[CTRL_SCREEN]) {
			vrFlatForced = !vrFlatForced;
		}
	} else {
		vrFlatForced = pspKeys[CTRL_SCREEN];
	}

	// Release keys on enabling camera adjust
	if (!wasCameraAdjustOn && pspKeys[VIRTKEY_VR_CAMERA_ADJUST]) {
		KeyInput keyUp;
		keyUp.deviceId = key.deviceId;
		keyUp.flags = KEY_UP;

		pspKeys[VIRTKEY_VR_CAMERA_ADJUST] = false;
		for (auto& pspKey : pspKeys) {
			if (pspKey.second) {
				keyUp.keyCode = (InputKeyCode)pspKey.first;
				cbNativeKey(keyUp);
			}
		}
		pspKeys[VIRTKEY_VR_CAMERA_ADJUST] = true;
	}

	// Reset camera adjust
	if (pspKeys[VIRTKEY_VR_CAMERA_ADJUST] && pspKeys[VIRTKEY_VR_CAMERA_RESET]) {
		g_Config.fCameraHeight = 0;
		g_Config.fCameraSide = 0;
		g_Config.fCameraDistance = 0;
		g_Config.fCameraPitch = 0;
	}

	//block keys by camera adjust
	return !pspKeys[VIRTKEY_VR_CAMERA_ADJUST];
}

/*
================================================================================

// VR games compatibility

================================================================================
*/

#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES

void PreprocessSkyplane(GLRStep* step) {

	// Do not do anything if the scene is not in VR.
	if (IsFlatVRScene()) {
		return;
	}

	// Check if it is the step we need to modify.
	for (auto& cmd : step->commands) {
		if (cmd.cmd == GLRRenderCommand::BIND_FB_TEXTURE) {
			return;
		}
	}

	// Clear sky with the fog color.
	if (!vrCompat[VR_COMPAT_FBO_CLEAR]) {
		GLRRenderData &skyClear = step->commands.insert(step->commands.begin());
		skyClear.cmd = GLRRenderCommand::CLEAR;
		skyClear.clear.colorMask = 0xF;
		skyClear.clear.clearMask = GL_COLOR_BUFFER_BIT;  // don't need to initialize clearZ, clearStencil
		skyClear.clear.clearColor = vrCompat[VR_COMPAT_FOG_COLOR];
		skyClear.clear.scissorX = 0;
		skyClear.clear.scissorY = 0;
		skyClear.clear.scissorW = 0;  // signal no scissor
		skyClear.clear.scissorH = 0;
		vrCompat[VR_COMPAT_FBO_CLEAR] = true;
	}

	// Remove original sky plane.
	bool depthEnabled = false;
	for (auto& command : step->commands) {
		if (command.cmd == GLRRenderCommand::DEPTH) {
			depthEnabled = command.depth.enabled;
		} else if ((command.cmd == GLRRenderCommand::DRAW && command.draw.indexBuffer != nullptr) && !depthEnabled) {
			command.draw.count = 0;
		}
	}
}

void PreprocessStepVR(void* step) {
	auto* glrStep = (GLRStep*)step;
	if (vrCompat[VR_COMPAT_SKYPLANE]) PreprocessSkyplane(glrStep);
}

#else

void PreprocessStepVR(void* step) {}

#endif

void SetVRCompat(VRCompatFlag flag, long value) {
	vrCompat[flag] = value;
}

/*
================================================================================

VR rendering integration

================================================================================
*/

void* BindVRFramebuffer() {
	return VR_BindFramebuffer(VR_GetEngine());
}

bool StartVRRender() {
	if (!VR_GetConfig(VR_CONFIG_VIEWPORT_VALID)) {
		VR_InitRenderer(VR_GetEngine());
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}

	if (VR_InitFrame(VR_GetEngine())) {

		// VR flags
		bool vrIncompatibleGame = PSP_CoreParameter().compat.vrCompat().ForceFlatScreen;
		bool vrMode = (g_Config.bEnableVR || IsImmersiveVRMode()) && !vrIncompatibleGame;
		bool vrScene = !vrFlatForced && (g_Config.bManualForceVR || (vr3DGeometryCount > 15));
		bool vrStereo = !PSP_CoreParameter().compat.vrCompat().ForceMono && g_Config.bEnableStereo;

		// Get VR status
		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			vrView[eye] = VR_GetView(eye);
		}
		UpdateVRViewMatrices();

		// Update projection matrix
		XrFovf fov = {};
		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			fov.angleLeft += vrView[eye].fov.angleLeft / 2.0f;
			fov.angleRight += vrView[eye].fov.angleRight / 2.0f;
			fov.angleUp += vrView[eye].fov.angleUp / 2.0f;
			fov.angleDown += vrView[eye].fov.angleDown / 2.0f;
		}
		float nearZ = 0.01f;
		float tanAngleLeft = tanf(fov.angleLeft);
		float tanAngleRight = tanf(fov.angleRight);
		float tanAngleDown = tanf(fov.angleDown);
		float tanAngleUp = tanf(fov.angleUp);
		float M[16] = {};
		M[0] = 2 / (tanAngleRight - tanAngleLeft);
		M[5] = 2 / (tanAngleUp - tanAngleDown);
		M[8] = (tanAngleRight + tanAngleLeft) / (tanAngleRight - tanAngleLeft);
		M[9] = (tanAngleUp + tanAngleDown) / (tanAngleUp - tanAngleDown);
		M[10] = -1;
		M[11] = -1;
		M[14] = -(nearZ + nearZ);
		if (IsImmersiveVRMode()) {
			M[0] /= 2.0f;
		}
		memcpy(vrMatrix[VR_PROJECTION_MATRIX], M, sizeof(float) * 16);

		// Decide if the scene is 3D or not
		VR_SetConfigFloat(VR_CONFIG_CANVAS_ASPECT, 480.0f / 272.0f);
		if (vrMode && vrScene && (appMode == VR_GAME_MODE)) {
			VR_SetConfig(VR_CONFIG_MODE, vrStereo ? VR_MODE_STEREO_6DOF : VR_MODE_MONO_6DOF);
			VR_SetConfig(VR_CONFIG_REPROJECTION, IsImmersiveVRMode() ? 0 : 1);
			vrFlatGame = false;
		} else if (appMode == VR_GAME_MODE) {
			VR_SetConfig(VR_CONFIG_MODE, vrStereo ? VR_MODE_STEREO_SCREEN : VR_MODE_MONO_SCREEN);
			if (IsGameVRScene()) {
				vrFlatGame = true;
			}
		} else {
			VR_SetConfig(VR_CONFIG_MODE, VR_MODE_MONO_SCREEN);
		}
		vr3DGeometryCount /= 2;

		// Set compatibility
		vrCompat[VR_COMPAT_SKYPLANE] = PSP_CoreParameter().compat.vrCompat().Skyplane;

		// Set customizations
		VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, vrScene && (appMode == VR_GAME_MODE) ? g_Config.fCanvas3DDistance : g_Config.fCanvasDistance);
		VR_SetConfig(VR_CONFIG_PASSTHROUGH, g_Config.bPassthrough && IsPassthroughSupported());
		return true;
	}
	return false;
}

void FinishVRRender() {
	VR_FinishFrame(VR_GetEngine());
}

void PreVRFrameRender(int fboIndex) {
	VR_BeginFrame(VR_GetEngine(), fboIndex);
	vrCompat[VR_COMPAT_FBO_CLEAR] = false;
}

void PostVRFrameRender() {
	VR_EndFrame(VR_GetEngine());
}

int GetVRFBOIndex() {
	return VR_GetConfig(VR_CONFIG_CURRENT_FBO);
}

int GetVRPassesCount() {
	bool vrStereo = !PSP_CoreParameter().compat.vrCompat().ForceMono && g_Config.bEnableStereo;
	return vrStereo ? 2 : 1;
}

bool IsPassthroughSupported() {
	return VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH);
}

bool IsFlatVRGame() {
	return vrFlatGame;
}

bool IsFlatVRScene() {
	if (g_Config.bForceVR && (!vrFlatForced || !g_Config.bManualForceVR)) {
		return false;
	}
	int vrMode = VR_GetConfig(VR_CONFIG_MODE);
	return (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
}

bool IsGameVRScene() {
	return (appMode == VR_GAME_MODE) || (appMode == VR_DIALOG_MODE);
}

bool IsImmersiveVRMode() {
	return g_Config.bEnableImmersiveVR && !PSP_CoreParameter().compat.vrCompat().IdentityViewHack;
}

bool Is2DVRObject(float* projMatrix, bool ortho) {

	// Quick analyze if the object is in 2D
	if ((fabs(fabs(projMatrix[12]) - 1.0f) < EPSILON) && (fabs(fabs(projMatrix[13]) - 1.0f) < EPSILON) && (fabs(fabs(projMatrix[14]) - 1.0f) < EPSILON)) {
		return true;
	} else if ((fabs(projMatrix[0]) > 10.0f) && (fabs(projMatrix[5]) > 10.0f)) {
		return true;
	} else if (fabs(projMatrix[15] - 1) < EPSILON) {
		return true;
	}

	// Update 3D geometry count
	bool identity = IsMatrixIdentity(projMatrix);
	if (!identity && !ortho) {
		vr3DGeometryCount++;
	}
	return identity || ortho;
}

void UpdateVRParams(float* projMatrix) {

	// Set mirroring of axes
	vrMirroring[VR_MIRRORING_AXIS_X] = projMatrix[0] < 0;
	vrMirroring[VR_MIRRORING_AXIS_Y] = projMatrix[5] < 0;
	vrMirroring[VR_MIRRORING_AXIS_Z] =  projMatrix[10] > 0;

	int variant = 1;
	variant += projMatrix[0] < 0;
	variant += (projMatrix[5] < 0) << 1;
	variant += (projMatrix[10] < 0) << 2;
	if (PSP_CoreParameter().compat.vrCompat().MirroringVariant > 0) {
		variant = PSP_CoreParameter().compat.vrCompat().MirroringVariant;
	}

	switch (variant) {
		case 1: //e.g. ATV
			vrMirroring[VR_MIRRORING_PITCH] = false;
			vrMirroring[VR_MIRRORING_YAW] = true;
			vrMirroring[VR_MIRRORING_ROLL] = true;
			break;
		case 2: //e.g. Tales of the World
			vrMirroring[VR_MIRRORING_PITCH] = false;
			vrMirroring[VR_MIRRORING_YAW] = false;
			vrMirroring[VR_MIRRORING_ROLL] = false;
			break;
		case 3: //e.g.PES 2014
		case 4: //untested
		case 6: //e.g Dante's Inferno
		case 8: //untested
			vrMirroring[VR_MIRRORING_PITCH] = true;
			vrMirroring[VR_MIRRORING_YAW] = true;
			vrMirroring[VR_MIRRORING_ROLL] = false;
			break;
		case 5: //e.g. Assassins Creed
		case 7: //e.g. Ghost in the shell
			vrMirroring[VR_MIRRORING_PITCH] = true;
			vrMirroring[VR_MIRRORING_YAW] = false;
			vrMirroring[VR_MIRRORING_ROLL] = true;
			break;
		default:
			assert(false);
			std::exit(1);
	}

	if (vrMirroringVariant != variant) {
		vrMirroringVariant = variant;
		UpdateVRViewMatrices();
	}
}

void UpdateVRProjection(float* projMatrix, float* output) {
	for (int i = 0; i < 16; i++) {
		if (!IsVREnabled()) {
			output[i] = projMatrix[i];
		} else if (PSP_CoreParameter().compat.vrCompat().ProjectionHack && ((i == 8) || (i == 9))) {
			output[i] = 0;
		} else if (fabs(projMatrix[i]) > 0) {
			output[i] = vrMatrix[VR_PROJECTION_MATRIX][i];
			if ((output[i] > 0) != (projMatrix[i] > 0)) {
				output[i] *= -1.0f;
			}
		} else {
			output[i] = 0;
		}
	}
	output[11] *= g_Config.fFieldOfViewPercentage / 100.0f;
}

void UpdateVRView(float* leftEye, float* rightEye) {
	float* dst[] = {leftEye, rightEye};
	float* matrix[] = {vrMatrix[VR_VIEW_MATRIX_LEFT_EYE], vrMatrix[VR_VIEW_MATRIX_RIGHT_EYE]};
	for (int index = 0; index < 2; index++) {

		// Validate the view matrix
		if (PSP_CoreParameter().compat.vrCompat().IdentityViewHack && IsMatrixIdentity(dst[index])) {
			return;
		}

		// Get view matrix from the game
		Lin::Matrix4x4 gameView = {};
		memcpy(gameView.m, dst[index], 16 * sizeof(float));

		// Get view matrix from the headset
		Lin::Matrix4x4 hmdView = {};
		memcpy(hmdView.m, matrix[index], 16 * sizeof(float));

		// Combine the matrices
		Lin::Matrix4x4 renderView = hmdView * gameView;
		memcpy(dst[index], renderView.m, 16 * sizeof(float));
	}
}

void UpdateVRViewMatrices() {

	// Get 6DoF scale
	float scale = 1.0f;
	if (PSP_CoreParameter().compat.vrCompat().UnitsPerMeter > 0) {
		scale = PSP_CoreParameter().compat.vrCompat().UnitsPerMeter;
	}

	// Get input
	XrPosef invView = XrPosef_Identity();
	if (IsVREnabled()) {
		invView = vrView[0].pose;
	}
	bool flatScreen = false;
	int vrMode = VR_GetConfig(VR_CONFIG_MODE);
	if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN)) {
		invView = XrPosef_Identity();
		flatScreen = true;
	}

	// get axis mirroring configuration
	float mx = vrMirroring[VR_MIRRORING_PITCH] ? -1.0f : 1.0f;
	float my = vrMirroring[VR_MIRRORING_YAW] ? -1.0f : 1.0f;
	float mz = vrMirroring[VR_MIRRORING_ROLL] ? -1.0f : 1.0f;

	// ensure there is maximally one axis to mirror rotation
	if (mx + my + mz < 0) {
		mx *= -1.0f;
		my *= -1.0f;
		mz *= -1.0f;
	} else {
		invView = XrPosef_Inverse(invView);
	}

	// apply camera pitch
	float s = sin(ToRadians(g_Config.fCameraPitch));
	float c = cos(ToRadians(g_Config.fCameraPitch));
	XrVector3f positionOffset = {g_Config.fCameraSide, g_Config.fCameraHeight, g_Config.fCameraDistance};
	positionOffset = {positionOffset.x, s * positionOffset.z + c * positionOffset.y, c * positionOffset.z - s * positionOffset.y};
	XrQuaternionf rotationOffset = XrQuaternionf_CreateFromVectorAngle({1, 0, 0}, ToRadians(g_Config.fCameraPitch));
	invView.orientation = XrQuaternionf_Multiply(rotationOffset, invView.orientation);

	// decompose rotation
	XrVector3f rotation = XrQuaternionf_ToEulerAngles(invView.orientation);
	float mPitch = mx * ToRadians(rotation.x);
	float mYaw = my * ToRadians(rotation.y);
	float mRoll = mz * ToRadians(rotation.z);

	// create updated quaternion
	XrQuaternionf pitch = XrQuaternionf_CreateFromVectorAngle({1, 0, 0}, mPitch);
	XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle({0, 1, 0}, mYaw);
	XrQuaternionf roll = XrQuaternionf_CreateFromVectorAngle({0, 0, 1}, mRoll);
	invView.orientation = XrQuaternionf_Multiply(roll, XrQuaternionf_Multiply(pitch, yaw));
	if (!VR_GetConfig(VR_CONFIG_REPROJECTION)) {
		float axis = vrMirroring[VR_MIRRORING_PITCH] ? -1.0f : 1.0f;
		invView.orientation = XrQuaternionf_CreateFromVectorAngle({axis, 0, 0}, ToRadians(g_Config.fCameraPitch));
	}

	float M[16];
	XrQuaternionf_ToMatrix4f(&invView.orientation, M);

	// Apply 6Dof head movement
	if (g_Config.bEnable6DoF && IsVREnabled()) {
		M[3] -= vrView[0].pose.position.x * (vrMirroring[VR_MIRRORING_AXIS_X] ? -1.0f : 1.0f) * scale;
		M[7] -= vrView[0].pose.position.y * (vrMirroring[VR_MIRRORING_AXIS_Y] ? -1.0f : 1.0f) * scale;
		M[11] -= vrView[0].pose.position.z * (vrMirroring[VR_MIRRORING_AXIS_Z] ? -1.0f : 1.0f) * scale;
	}
	// Camera adjust - distance
	if (fabsf(positionOffset.z) > 0.0f) {
		XrVector3f forward = {0.0f, 0.0f, positionOffset.z * scale};
		forward = XrQuaternionf_Rotate(invView.orientation, forward);
		forward = XrVector3f_ScalarMultiply(forward, vrMirroring[VR_MIRRORING_AXIS_Z] ? -1.0f : 1.0f);
		M[3] += forward.x;
		M[7] += forward.y;
		M[11] += forward.z;
	}
	// Camera adjust - height
	if (fabsf(positionOffset.y) > 0.0f) {
		XrVector3f up = {0.0f, -positionOffset.y * scale, 0.0f};
		up = XrQuaternionf_Rotate(invView.orientation, up);
		up = XrVector3f_ScalarMultiply(up, vrMirroring[VR_MIRRORING_AXIS_Y] ? -1.0f : 1.0f);
		M[3] += up.x;
		M[7] += up.y;
		M[11] += up.z;
	}
	// Camera adjust - side
	if (fabsf(positionOffset.x) > 0.0f) {
		XrVector3f side = {-positionOffset.x * scale, 0.0f,  0.0f};
		side = XrQuaternionf_Rotate(invView.orientation, side);
		side = XrVector3f_ScalarMultiply(side, vrMirroring[VR_MIRRORING_AXIS_X] ? -1.0f : 1.0f);
		M[3] += side.x;
		M[7] += side.y;
		M[11] += side.z;
	}

	for (int matrix = VR_VIEW_MATRIX_LEFT_EYE; matrix <= VR_VIEW_MATRIX_RIGHT_EYE; matrix++) {

		// Stereoscopy
		bool vrStereo = !PSP_CoreParameter().compat.vrCompat().ForceMono && g_Config.bEnableStereo;
		if (vrStereo && IsVREnabled()) {
			bool mirrored = vrMirroring[VR_MIRRORING_AXIS_Z] ^ (matrix == VR_VIEW_MATRIX_RIGHT_EYE);
			float dx = fabs(vrView[1].pose.position.x - vrView[0].pose.position.x);
			float dy = fabs(vrView[1].pose.position.y - vrView[0].pose.position.y);
			float dz = fabs(vrView[1].pose.position.z - vrView[0].pose.position.z);
			float ipd = sqrt(dx * dx + dy * dy + dz * dz);
			XrVector3f separation = {ipd * scale * 0.5f, 0.0f, 0.0f};
			separation = XrQuaternionf_Rotate(invView.orientation, separation);
			separation = XrVector3f_ScalarMultiply(separation, mirrored ? -1.0f : 2.0f);
			M[3] += separation.x;
			M[7] += separation.y;
			M[11] += separation.z;
		}

		memcpy(vrMatrix[matrix], M, sizeof(float) * 16);
	}
}
