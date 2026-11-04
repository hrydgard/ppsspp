#pragma once

#include <cstddef>

struct AxisInput;
struct TouchInput;
struct KeyInput;

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
// GL: creates the session (once per instance, on the GL thread).
void EnterVR();
// Tears down what InitVROnAndroid and EnterVR set up, so a new activity can start over in this process.
void ShutdownVR();
void GetVRResolutionPerEye(int* width, int* height);
void SetVRCallbacks(void(*axis)(const AxisInput *axis, size_t count), bool(*key)(const KeyInput &key), void(*touch)(const TouchInput &touch));

// VR input integration
void SetVRAppMode(VRAppMode mode);
void UpdateVRInput(bool haptics, float dp_xscale, float dp_yscale);
bool UpdateVRAxis(const AxisInput *axes, size_t count);
bool UpdateVRKeys(const KeyInput &key);

// VR games compatibility
void PreprocessStepVR(void* step);
struct VKRStep;
void PreprocessVulkanStepVR(VKRStep *step);
void SetVRCompat(VRCompatFlag flag, long value);

// VR rendering integration
void* BindVRFramebuffer();
bool StartVRRender();
void FinishVRRender();
void PreVRFrameRender(int fboIndex);
void PostVRFrameRender();
// The pointer for the menus, in backbuffer pixels from the top left, if the app has to draw it
// (Vulkan; OpenGL draws it into the eye buffers itself).
bool GetVRMouseCursorRect(int *x, int *y, int *w, int *h);
int GetVRFBOIndex();
int GetVRPassesCount();
bool IsPassthroughSupported();
bool IsBigScreenVRMode();
bool IsFlatVRGame();
bool IsFlatVRScene();
bool IsGameVRScene();
bool IsImmersiveVRMode();
bool Is2DVRObject(float* projMatrix, bool ortho);
void UpdateVRParams(float* projMatrix);
void UpdateVRProjection(float* projMatrix, float* output);
void UpdateVRView(float* leftEye, float* rightEye);
void UpdateVRViewMatrices();
