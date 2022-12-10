#pragma once

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

enum VRCompatFlag {
	//compatibility tweaks
	VR_COMPAT_SKYPLANE,

	//render state
	VR_COMPAT_FBO_CLEAR,

	//uniforms
	VR_COMPAT_FOG_COLOR,

	//end
	VR_COMPAT_MAX
};

enum VRAppMode {
	VR_CONTROLLER_MAPPING_MODE,
	VR_DIALOG_MODE,
	VR_GAME_MODE,
	VR_MENU_MODE,
};

// VR app flow integration
bool IsVREnabled();
void InitVROnAndroid(void* vm, void* activity, const char* system, int version, const char* name);
void EnterVR(bool firstStart, void* vulkanContext);
void GetVRResolutionPerEye(int* width, int* height);
void SetVRCallbacks(bool(*axis)(const AxisInput &axis), bool(*key)(const KeyInput &key), bool(*touch)(const TouchInput &touch));

// VR input integration
void SetVRAppMode(VRAppMode mode);
void UpdateVRInput(bool haptics, float dp_xscale, float dp_yscale);
bool UpdateVRAxis(const AxisInput &axis);
bool UpdateVRKeys(const KeyInput &key);

// VR games compatibility
void PreprocessStepVR(void* step);
void SetVRCompat(VRCompatFlag flag, long value);

// VR rendering integration
void* BindVRFramebuffer();
bool StartVRRender();
void FinishVRRender();
void PreVRFrameRender(int fboIndex);
void PostVRFrameRender();
int GetVRFBOIndex();
int GetVRPassesCount();
bool IsMultiviewSupported();
bool IsFlatVRGame();
bool IsFlatVRScene();
bool IsGameVRScene();
bool Is2DVRObject(float* projMatrix, bool ortho);
void UpdateVRParams(float* projMatrix, float* viewMatrix);
void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye);
void UpdateVRView(float* leftEye, float* rightEye);
