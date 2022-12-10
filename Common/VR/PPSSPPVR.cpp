#include "Common/VR/PPSSPPVR.h"

#include "Common/VR/VRBase.h"
#if XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
#include "Common/GPU/OpenGL/GLRenderManager.h"
#endif

#include "Common/VR/VRInput.h"
#include "Common/VR/VRMath.h"
#include "Common/VR/VRRenderer.h"

#include "Common/GPU/Vulkan/VulkanContext.h"

#include "Common/Math/lin/matrix4x4.h"

#include "Core/HLE/sceDisplay.h"
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
	VR_MIRRORING_UPDATED,
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
static std::map<int, bool> pspKeys;

static int vr3DGeometryCount = 0;
static long vrCompat[VR_COMPAT_MAX];
static bool vrFlatGame = false;
static float vrMatrix[VR_MATRIX_COUNT][16];
static bool vrMirroring[VR_MIRRORING_COUNT];

static bool(*NativeAxis)(const AxisInput &axis);
static bool(*NativeKey)(const KeyInput &key);
static bool(*NativeTouch)(const TouchInput &touch);

/*
================================================================================

VR button mapping

================================================================================
*/

struct ButtonMapping {
	ovrButton ovr;
	int keycode;
	bool pressed;
	int repeat;

	ButtonMapping(int keycode, ovrButton ovr) {
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

static const int controllerIds[] = {DEVICE_ID_XR_CONTROLLER_LEFT, DEVICE_ID_XR_CONTROLLER_RIGHT};
static std::vector<ButtonMapping> controllerMapping[2] = {
		leftControllerMapping,
		rightControllerMapping
};
static bool controllerMotion[2][5] = {};
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
		VR_SetPlatformFLag(VR_PLATFORM_INSTANCE_EXT, true);
	} else if ((strcmp(vendor, "META") == 0) || (strcmp(vendor, "OCULUS") == 0)) {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_PERFORMANCE_EXT, true);
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

void SetVRCallbacks(bool(*axis)(const AxisInput &axis), bool(*key)(const KeyInput &key), bool(*touch)(const TouchInput &touch)) {
	NativeAxis = axis;
	NativeKey = key;
	NativeTouch = touch;
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
	if (pspKeys[VIRTKEY_VR_CAMERA_ADJUST]) {
		AxisInput axis = {};
		for (int j = 0; j < 2; j++) {
			XrVector2f joystick = IN_VRGetJoystickState(j);
			axis.deviceId = DEVICE_ID_DEFAULT;

			//horizontal
			axis.axisId = j == 0 ? JOYSTICK_AXIS_X : JOYSTICK_AXIS_Z;
			axis.value = joystick.x;
			NativeAxis(axis);

			//vertical
			axis.axisId = j == 0 ? JOYSTICK_AXIS_Y : JOYSTICK_AXIS_RZ;
			axis.value = -joystick.y;
			NativeAxis(axis);
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
				NativeKey(keyInput);
				m.pressed = pressed;
				m.repeat = 0;
			} else if (pressed && (m.repeat > 30)) {
				keyInput.flags |= KEY_IS_REPEAT;
				NativeKey(keyInput);
				m.repeat = 0;
			} else {
				m.repeat++;
			}
		}
	}

	//motion control
	if (g_Config.bEnableMotions) {
		for (int j = 0; j < 2; j++) {
			bool activate;
			float limit = g_Config.fMotionLength; //length of needed movement in meters
			XrVector3f axis = {0, 1, 0};
			float center = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
			XrQuaternionf orientation = XrQuaternionf_CreateFromVectorAngle(axis, center);
			XrVector3f position = XrQuaternionf_Rotate(orientation, IN_VRGetPose(j).position);

			//up
			activate = position.y > limit;
			keyInput.flags = activate ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOTION_UP;
			keyInput.deviceId = controllerIds[j];
			if (controllerMotion[j][0] != activate) NativeKey(keyInput);
			controllerMotion[j][0] = activate;

			//down
			activate = position.y < -limit * 1.5f;
			keyInput.flags = activate ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOTION_DOWN;
			keyInput.deviceId = controllerIds[j];
			if (controllerMotion[j][1] != activate) NativeKey(keyInput);
			controllerMotion[j][1] = activate;

			//left
			activate = position.x < -limit * (j == 0 ? 1.0f : 0.25f);
			keyInput.flags = activate ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOTION_LEFT;
			keyInput.deviceId = controllerIds[j];
			if (controllerMotion[j][2] != activate) NativeKey(keyInput);
			controllerMotion[j][2] = activate;

			//right
			activate = position.x > limit * (j == 1 ? 1.0f : 0.25f);
			keyInput.flags = activate ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOTION_RIGHT;
			keyInput.deviceId = controllerIds[j];
			if (controllerMotion[j][3] != activate) NativeKey(keyInput);
			controllerMotion[j][3] = activate;

			//forward
			activate = position.z < -limit;
			keyInput.flags = activate ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOTION_FORWARD;
			keyInput.deviceId = controllerIds[j];
			if (controllerMotion[j][4] != activate) NativeKey(keyInput);
			controllerMotion[j][4] = activate;
		}
	}

	// Camera adjust
	if (pspKeys[VIRTKEY_VR_CAMERA_ADJUST]) {
		for (auto& device : pspAxis) {
			for (auto& axis : device.second) {
				switch(axis.first) {
					case JOYSTICK_AXIS_X:
						if (axis.second < -0.75f) g_Config.fCameraSide -= 0.05f;
						if (axis.second > 0.75f) g_Config.fCameraSide += 0.05f;
						g_Config.fCameraSide = clampFloat(g_Config.fCameraSide, -50.0f, 50.0f);
						break;
					case JOYSTICK_AXIS_Y:
						if (axis.second > 0.75f) g_Config.fCameraHeight -= 0.05f;
						if (axis.second < -0.75f) g_Config.fCameraHeight += 0.05f;
						g_Config.fCameraHeight = clampFloat(g_Config.fCameraHeight, -50.0f, 50.0f);
						break;
					case JOYSTICK_AXIS_Z:
						if (axis.second < -0.75f) g_Config.fHeadUpDisplayScale -= 0.01f;
						if (axis.second > 0.75f) g_Config.fHeadUpDisplayScale += 0.01f;
						g_Config.fHeadUpDisplayScale = clampFloat(g_Config.fHeadUpDisplayScale, 0.2f, 1.5f);
						break;
					case JOYSTICK_AXIS_RZ:
						if (axis.second > 0.75f) g_Config.fCameraDistance -= 0.1f;
						if (axis.second < -0.75f) g_Config.fCameraDistance += 0.1f;
						g_Config.fCameraDistance = clampFloat(g_Config.fCameraDistance, -50.0f, 50.0f);
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

		//set renderer
		VR_SetConfig(VR_CONFIG_MOUSE_X, (int)x);
		VR_SetConfig(VR_CONFIG_MOUSE_Y, (int)y);
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 6 * (int)pow(VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE), 0.25f));

		//inform engine about the status
		TouchInput touch;
		touch.id = mouseController;
		touch.x = x * dp_xscale;
		touch.y = (height - y - 1) * dp_yscale / VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
		bool pressed = IN_VRGetButtonState(mouseController) & ovrButton_Trigger;
		if (mousePressed != pressed) {
			if (pressed) {
				touch.flags = TOUCH_DOWN;
				NativeTouch(touch);
				touch.flags = TOUCH_UP;
				NativeTouch(touch);
			}
			mousePressed = pressed;
		}

		//mouse wheel emulation
		for (int j = 0; j < 2; j++) {
			keyInput.deviceId = controllerIds[j];
			float scroll = -IN_VRGetJoystickState(j).y;
			keyInput.flags = scroll < -0.5f ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
			NativeKey(keyInput);
			keyInput.flags = scroll > 0.5f ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
			NativeKey(keyInput);
		}
	} else {
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 0);
	}
}

bool UpdateVRAxis(const AxisInput &axis) {
	if (pspAxis.find(axis.deviceId) == pspAxis.end()) {
		pspAxis[axis.deviceId] = std::map<int, float>();
	}
	pspAxis[axis.deviceId][axis.axisId] = axis.value;
	return !pspKeys[VIRTKEY_VR_CAMERA_ADJUST];
}

bool UpdateVRKeys(const KeyInput &key) {
	//store key value
	std::vector<int> nativeKeys;
	bool wasCameraAdjustOn = pspKeys[VIRTKEY_VR_CAMERA_ADJUST];
	if (KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &nativeKeys)) {
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

	// Release keys on enabling camera adjust
	if (!wasCameraAdjustOn && pspKeys[VIRTKEY_VR_CAMERA_ADJUST]) {
		KeyInput keyUp;
		keyUp.deviceId = key.deviceId;
		keyUp.flags = KEY_UP;

		pspKeys[VIRTKEY_VR_CAMERA_ADJUST] = false;
		for (auto& pspKey : pspKeys) {
			if (pspKey.second) {
				keyUp.keyCode = pspKey.first;
				NativeKey(keyUp);
			}
		}
		pspKeys[VIRTKEY_VR_CAMERA_ADJUST] = true;
	}

	// Reset camera adjust
	if (pspKeys[VIRTKEY_VR_CAMERA_ADJUST] && pspKeys[VIRTKEY_VR_CAMERA_RESET]) {
		g_Config.fCameraHeight = 0;
		g_Config.fCameraSide = 0;
		g_Config.fCameraDistance = 0;
		g_Config.fHeadUpDisplayScale = 0.3f;
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
		GLRRenderData skyClear {};
		skyClear.cmd = GLRRenderCommand::CLEAR;
		skyClear.clear.colorMask = 0xF;
		skyClear.clear.clearMask = GL_COLOR_BUFFER_BIT;
		skyClear.clear.clearColor = vrCompat[VR_COMPAT_FOG_COLOR];
		step->commands.insert(step->commands.begin(), skyClear);
		vrCompat[VR_COMPAT_FBO_CLEAR] = true;
	}

	// Remove original sky plane.
	bool depthEnabled = false;
	for (auto& command : step->commands) {
		if (command.cmd == GLRRenderCommand::DEPTH) {
			depthEnabled = command.depth.enabled;
		} else if ((command.cmd == GLRRenderCommand::DRAW_INDEXED) && !depthEnabled) {
			command.drawIndexed.count = 0;
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
		VR_InitRenderer(VR_GetEngine(), IsMultiviewSupported());
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}

	if (VR_InitFrame(VR_GetEngine())) {

		// Get OpenXR view and fov
		XrFovf fov = {};
		XrPosef invViewTransform[2];
		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			XrView view = VR_GetView(eye);
			fov.angleLeft += view.fov.angleLeft / 2.0f;
			fov.angleRight += view.fov.angleRight / 2.0f;
			fov.angleUp += view.fov.angleUp / 2.0f;
			fov.angleDown += view.fov.angleDown / 2.0f;
			invViewTransform[eye] = view.pose;
		}

		// Get 6DoF scale
		float scale = 1.0f;
		bool hasUnitScale = false;
		if (PSP_CoreParameter().compat.vrCompat().UnitsPerMeter > 0) {
			scale = PSP_CoreParameter().compat.vrCompat().UnitsPerMeter;
			hasUnitScale = true;
		}

		// Update matrices
		for (int matrix = 0; matrix < VR_MATRIX_COUNT; matrix++) {
			if (matrix == VR_PROJECTION_MATRIX) {
				float nearZ = g_Config.fFieldOfViewPercentage / 200.0f;
				float tanAngleLeft = tanf(fov.angleLeft);
				float tanAngleRight = tanf(fov.angleRight);
				float tanAngleDown = tanf(fov.angleDown);
				float tanAngleUp = tanf(fov.angleUp);

				float M[16] = {};
				M[0] = 2 / (tanAngleRight - tanAngleLeft);
				M[2] = (tanAngleRight + tanAngleLeft) / (tanAngleRight - tanAngleLeft);
				M[5] = 2 / (tanAngleUp - tanAngleDown);
				M[6] = (tanAngleUp + tanAngleDown) / (tanAngleUp - tanAngleDown);
				M[10] = -1;
				M[11] = -(nearZ + nearZ);
				M[14] = -1;

				memcpy(vrMatrix[matrix], M, sizeof(float) * 16);
			} else if ((matrix == VR_VIEW_MATRIX_LEFT_EYE) || (matrix == VR_VIEW_MATRIX_RIGHT_EYE)) {
				bool flatScreen = false;
				XrPosef invView = invViewTransform[0];
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

				// create updated quaternion
				XrVector3f rotation = XrQuaternionf_ToEulerAngles(invView.orientation);
				XrQuaternionf pitch = XrQuaternionf_CreateFromVectorAngle({1, 0, 0}, mx * ToRadians(rotation.x));
				XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle({0, 1, 0}, my * ToRadians(rotation.y));
				XrQuaternionf roll = XrQuaternionf_CreateFromVectorAngle({0, 0, 1}, mz * ToRadians(rotation.z));
				invView.orientation = XrQuaternionf_Multiply(roll, XrQuaternionf_Multiply(pitch, yaw));

				float M[16];
				XrQuaternionf_ToMatrix4f(&invView.orientation, M);
				memcpy(&M, M, sizeof(float) * 16);

				// Apply 6Dof head movement
				if (!flatScreen && g_Config.bEnable6DoF) {
					M[3] -= invViewTransform[0].position.x * (vrMirroring[VR_MIRRORING_AXIS_X] ? -1.0f : 1.0f) * scale;
					M[7] -= invViewTransform[0].position.y * (vrMirroring[VR_MIRRORING_AXIS_Y] ? -1.0f : 1.0f) * scale;
					M[11] -= invViewTransform[0].position.z * (vrMirroring[VR_MIRRORING_AXIS_Z] ? -1.0f : 1.0f) * scale;
				}
				// Camera adjust - distance
				if (fabsf(g_Config.fCameraDistance) > 0.0f) {
					XrVector3f forward = {0.0f, 0.0f, g_Config.fCameraDistance * scale};
					forward = XrQuaternionf_Rotate(invView.orientation, forward);
					forward = XrVector3f_ScalarMultiply(forward, vrMirroring[VR_MIRRORING_AXIS_Z] ? -1.0f : 1.0f);
					M[3] += forward.x;
					M[7] += forward.y;
					M[11] += forward.z;
				}
				// Camera adjust - height
				if (fabsf(g_Config.fCameraHeight) > 0.0f) {
					XrVector3f up = {0.0f, -g_Config.fCameraHeight * scale, 0.0f};
					up = XrQuaternionf_Rotate(invView.orientation, up);
					up = XrVector3f_ScalarMultiply(up, vrMirroring[VR_MIRRORING_AXIS_Y] ? -1.0f : 1.0f);
					M[3] += up.x;
					M[7] += up.y;
					M[11] += up.z;
				}
				// Camera adjust - side
				if (fabsf(g_Config.fCameraSide) > 0.0f) {
					XrVector3f side = {-g_Config.fCameraSide * scale, 0.0f,  0.0f};
					side = XrQuaternionf_Rotate(invView.orientation, side);
					side = XrVector3f_ScalarMultiply(side, vrMirroring[VR_MIRRORING_AXIS_X] ? -1.0f : 1.0f);
					M[3] += side.x;
					M[7] += side.y;
					M[11] += side.z;
				}
				// Stereoscopy
				if (hasUnitScale && (matrix == VR_VIEW_MATRIX_RIGHT_EYE)) {
					float dx = fabs(invViewTransform[1].position.x - invViewTransform[0].position.x);
					float dy = fabs(invViewTransform[1].position.y - invViewTransform[0].position.y);
					float dz = fabs(invViewTransform[1].position.z - invViewTransform[0].position.z);
					float ipd = sqrt(dx * dx + dy * dy + dz * dz);
					XrVector3f separation = {ipd * scale, 0.0f, 0.0f};
					separation = XrQuaternionf_Rotate(invView.orientation, separation);
					separation = XrVector3f_ScalarMultiply(separation, vrMirroring[VR_MIRRORING_AXIS_Z] ? -1.0f : 1.0f);
					M[3] -= separation.x;
					M[7] -= separation.y;
					M[11] -= separation.z;
				}
				memcpy(vrMatrix[matrix], M, sizeof(float) * 16);
			} else {
				assert(false);
			}
		}

		// Decide if the scene is 3D or not
		bool stereo = hasUnitScale && g_Config.bEnableStereo;
		VR_SetConfigFloat(VR_CONFIG_CANVAS_ASPECT, 480.0f / 272.0f);
		if (g_Config.bEnableVR && !pspKeys[CTRL_SCREEN] && (appMode == VR_GAME_MODE) && (vr3DGeometryCount > 15)) {
			VR_SetConfig(VR_CONFIG_MODE, stereo ? VR_MODE_STEREO_6DOF : VR_MODE_MONO_6DOF);
			vrFlatGame = false;
		} else {
			VR_SetConfig(VR_CONFIG_MODE, stereo ? VR_MODE_STEREO_SCREEN : VR_MODE_MONO_SCREEN);
			if (IsGameVRScene()) {
				vrFlatGame = true;
			}
		}
		vr3DGeometryCount /= 2;

		// Set compatibility
		vrCompat[VR_COMPAT_SKYPLANE] = PSP_CoreParameter().compat.vrCompat().Skyplane;

		// Set customizations
		__DisplaySetFramerate(g_Config.bForce72Hz ? 72 : 60);
		VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, g_Config.fCanvasDistance);
		vrMirroring[VR_MIRRORING_UPDATED] = false;
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
	if (!IsMultiviewSupported() && g_Config.bEnableStereo) {
		return 2;
	} else {
		return 1;
	}
}

bool IsMultiviewSupported() {
	return false;
}

bool IsFlatVRGame() {
	return vrFlatGame;
}

bool IsFlatVRScene() {
	int vrMode = VR_GetConfig(VR_CONFIG_MODE);
	return (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
}

bool IsGameVRScene() {
	return (appMode == VR_GAME_MODE) || (appMode == VR_DIALOG_MODE);
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

void UpdateVRParams(float* projMatrix, float* viewMatrix) {

	// Set mirroring of axes
	bool identityView = PSP_CoreParameter().compat.vrCompat().IdentityViewHack && IsMatrixIdentity(viewMatrix);
	if (!vrMirroring[VR_MIRRORING_UPDATED] && !IsMatrixIdentity(projMatrix) && !identityView) {
		vrMirroring[VR_MIRRORING_UPDATED] = true;
		vrMirroring[VR_MIRRORING_AXIS_X] = projMatrix[0] < 0;
		vrMirroring[VR_MIRRORING_AXIS_Y] = projMatrix[5] < 0;
		vrMirroring[VR_MIRRORING_AXIS_Z] =  projMatrix[10] > 0;

		float up = 0;
		for (int i = 4; i < 7;  i++) {
			up += viewMatrix[i];
		}

		int variant = projMatrix[0] < 0;
		variant += (projMatrix[5] < 0) << 1;
		variant += (projMatrix[10] < 0) << 2;
		variant += (up < 0) << 3;

		switch (variant) {
			case 0: //e.g. ATV
			case 1: //untested
				vrMirroring[VR_MIRRORING_PITCH] = false;
				vrMirroring[VR_MIRRORING_YAW] = true;
				vrMirroring[VR_MIRRORING_ROLL] = true;
				break;
			case 2: //e.g.PES 2014
			case 3: //untested
			case 5: //e.g Dante's Inferno
			case 7: //untested
			case 8: //untested
			case 9: //untested
			case 10: //untested
			case 11: //untested
			case 13: //untested
			case 15: //untested
				vrMirroring[VR_MIRRORING_PITCH] = true;
				vrMirroring[VR_MIRRORING_YAW] = true;
				vrMirroring[VR_MIRRORING_ROLL] = false;
				break;
			case 4: //e.g. Assassins Creed
			case 6: //e.g. Ghost in the shell
			case 12: //e.g. GTA Vice City
			case 14: //untested
				vrMirroring[VR_MIRRORING_PITCH] = true;
				vrMirroring[VR_MIRRORING_YAW] = false;
				vrMirroring[VR_MIRRORING_ROLL] = true;
				break;
			default:
				assert(false);
				std::exit(1);
		}
	}
}

void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye) {
	float* hmdProjection = vrMatrix[VR_PROJECTION_MATRIX];
	for (int i = 0; i < 16; i++) {
		if ((hmdProjection[i] > 0) != (projMatrix[i] > 0)) {
			hmdProjection[i] *= -1.0f;
		}
	}
	memcpy(leftEye, hmdProjection, 16 * sizeof(float));
	memcpy(rightEye, hmdProjection, 16 * sizeof(float));
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
