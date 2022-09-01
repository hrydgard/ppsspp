#include "Common/VR/PPSSPPVR.h"
#include "Common/VR/VRBase.h"
#include "Common/VR/VRInput.h"
#include "Common/VR/VRMath.h"
#include "Common/VR/VRRenderer.h"
#include "Common/VR/VRTweaks.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/Config.h"
#include "Core/KeyMap.h"


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

struct MouseActivator {
	bool activate;
	ovrButton ovr;

	MouseActivator(bool activate, ovrButton ovr) {
		this->activate = activate;
		this->ovr = ovr;
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
static int mouseController = -1;
static bool mousePressed[] = {false, false};

static std::vector<MouseActivator> mouseActivators = {
		MouseActivator(true, ovrButton_Trigger),
		MouseActivator(false, ovrButton_Up),
		MouseActivator(false, ovrButton_Down),
		MouseActivator(false, ovrButton_Left),
		MouseActivator(false, ovrButton_Right),
};

/*
================================================================================

VR app flow integration

================================================================================
*/

bool IsVRBuild() {
	return true;
}

void InitVROnAndroid(void* vm, void* activity, int version, char* name) {
	ovrJava java;
	java.Vm = (JavaVM*)vm;
	java.ActivityObject = (jobject)activity;
	java.AppVersion = version;
	strcpy(java.AppName, name);
	VR_Init(java);

	__DisplaySetFramerate(72);
}

void EnterVR(bool firstStart) {
	if (firstStart) {
		VR_EnterVR(VR_GetEngine());
		IN_VRInit(VR_GetEngine());
	}
	VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, false);
}

void GetVRResolutionPerEye(int* width, int* height) {
	if (VR_GetEngine()->appState.Instance) {
		VR_GetResolution(VR_GetEngine(), width, height);
	}
}

void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool(*NativeTouch)(const TouchInput &touch), bool haptics, float dp_xscale, float dp_yscale) {
	//buttons
	KeyInput keyInput = {};
	for (int j = 0; j < 2; j++) {
		int status = IN_VRGetButtonState(j);
		for (ButtonMapping& m : controllerMapping[j]) {
			bool pressed = status & m.ovr;
			keyInput.flags = pressed ? KEY_DOWN : KEY_UP;
			keyInput.keyCode = m.keycode;
			keyInput.deviceId = controllerIds[j];

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

	//enable or disable mouse
	for (int j = 0; j < 2; j++) {
		int status = IN_VRGetButtonState(j);
		for (MouseActivator& m : mouseActivators) {
			if (status & m.ovr) {
				mouseController = m.activate ? j : -1;
			}
		}
	}

	//mouse cursor
	if (mouseController >= 0) {
		//get position on screen
		XrPosef pose = IN_VRGetPose(mouseController);
		XrVector3f angles = XrQuaternionf_ToEulerAngles(pose.orientation);
		float width = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_WIDTH);
		float height = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_HEIGHT);
		float cx = width / 2;
		float cy = height / 2;
		float speed = (cx + cy) / 2;
		float x = cx - tan(ToRadians(angles.y - (float)VR_GetConfig(VR_CONFIG_MENU_YAW))) * speed;
		float y = cy - tan(ToRadians(angles.x)) * speed;

		//set renderer
		VR_SetConfig(VR_CONFIG_MOUSE_X, (int)x);
		VR_SetConfig(VR_CONFIG_MOUSE_Y, (int)y);
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 6 * (int)pow(VR_GetConfig(VR_CONFIG_CANVAS_DISTANCE), 0.25f));

		//inform engine about the status
		TouchInput touch;
		touch.id = mouseController;
		touch.x = x * dp_xscale;
		touch.y = (height - y - 1) * dp_yscale;
		bool pressed = IN_VRGetButtonState(mouseController) & ovrButton_Trigger;
		if (mousePressed[mouseController] != pressed) {
			if (!pressed) {
				touch.flags = TOUCH_DOWN;
				NativeTouch(touch);
				touch.flags = TOUCH_UP;
				NativeTouch(touch);
			}
			mousePressed[mouseController] = pressed;
		}
	} else {
		VR_SetConfig(VR_CONFIG_MOUSE_SIZE, 0);
	}
}

void UpdateVRScreenKey(const KeyInput &key) {
	std::vector<int> nativeKeys;
	if (KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &nativeKeys)) {
		for (int& nativeKey : nativeKeys) {
			if (nativeKey == CTRL_SCREEN) {
				VR_SetConfig(VR_CONFIG_FORCE_2D, key.flags & KEY_DOWN);
			}
		}
	}
}

/*
================================================================================

VR rendering integration

================================================================================
*/

void BindVRFramebuffer() {
	VR_BindFramebuffer(VR_GetEngine());
}

bool PreVRRender() {
	if (!VR_GetConfig(VR_CONFIG_VIEWPORT_VALID)) {
		VR_InitRenderer(VR_GetEngine(), IsMultiviewSupported());
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}

	if (VR_BeginFrame(VR_GetEngine())) {

		// Decide if the scene is 3D or not
		if (g_Config.bEnableVR && !VR_GetConfig(VR_CONFIG_FORCE_2D) && (VR_GetConfig(VR_CONFIG_3D_GEOMETRY_COUNT) > 15)) {
			VR_SetConfig(VR_CONFIG_MODE, g_Config.bEnableStereo ? VR_MODE_STEREO_6DOF : VR_MODE_MONO_6DOF);
		} else {
			VR_SetConfig(VR_CONFIG_MODE, VR_MODE_FLAT_SCREEN);
		}
		VR_SetConfig(VR_CONFIG_3D_GEOMETRY_COUNT, VR_GetConfig(VR_CONFIG_3D_GEOMETRY_COUNT) / 2);

		// Set customizations
		VR_SetConfig(VR_CONFIG_6DOF_ENABLED, g_Config.bEnable6DoF);
		VR_SetConfig(VR_CONFIG_CANVAS_DISTANCE, g_Config.iCanvasDistance);
		VR_SetConfig(VR_CONFIG_FOV_SCALE, g_Config.iFieldOfViewPercentage);
		return true;
	}
	return false;
}

void PostVRRender() {
	VR_EndFrame(VR_GetEngine());
}

bool IsMultiviewSupported() {
	return false;
}

bool IsFlatVRScene() {
	return VR_GetConfig(VR_CONFIG_MODE) == VR_MODE_FLAT_SCREEN;
}

bool Is2DVRObject(float* projMatrix, bool ortho) {
	bool is2D = VR_TweakIsMatrixBigScale(projMatrix) ||
	            VR_TweakIsMatrixIdentity(projMatrix) ||
	            VR_TweakIsMatrixOneOrtho(projMatrix) ||
	            VR_TweakIsMatrixOneScale(projMatrix) ||
	            VR_TweakIsMatrixOneTransform(projMatrix);
	if (!is2D && !ortho) {
		VR_SetConfig(VR_CONFIG_3D_GEOMETRY_COUNT, VR_GetConfig(VR_CONFIG_3D_GEOMETRY_COUNT) + 1);
	}
	return is2D;
}

void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye) {
	VR_TweakProjection(projMatrix, leftEye, VR_PROJECTION_MATRIX_LEFT_EYE);
	VR_TweakProjection(projMatrix, rightEye, VR_PROJECTION_MATRIX_RIGHT_EYE);
	VR_TweakMirroring(projMatrix);
}

void UpdateVRView(float* projMatrix, float* leftEye, float* rightEye) {
	VR_TweakView(leftEye, projMatrix, VR_VIEW_MATRIX_LEFT_EYE);
	VR_TweakView(rightEye, projMatrix, VR_VIEW_MATRIX_RIGHT_EYE);
}
