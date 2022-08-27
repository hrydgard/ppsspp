#pragma once

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

#ifdef OPENXR

// VR app flow integration
bool IsVRBuild();
void InitVROnAndroid(void* vm, void* activity, int version, char* name);
void EnterVR(bool firstStart);
void GetVRResolutionPerEye(int* width, int* height);
void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool haptics);
void UpdateVRScreenKey(const KeyInput &key);

// VR rendering integration
void BindVRFramebuffer();
bool PreVRRender();
void PostVRRender();
bool IsFlatVRScene();
bool Is2DVRObject(float* projMatrix, bool ortho);
void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye);
void UpdateVRView(float* projMatrix, float* leftEye, float* rightEye);

#else //dummy integration

// VR app flow integration
bool IsVRBuild() { return false; }
void InitVROnAndroid(void* vm, void* activity, int version, char* name) {}
void EnterVR(bool firstTime) {}
void GetVRResolutionPerEye(int* width, int* height) {}
void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool haptics) {}
void UpdateVRScreenKey(const KeyInput &key) {}

// VR rendering integration
void BindVRFramebuffer() {}
bool PreVRRender() { return false; }
void PostVRRender() {}
bool IsFlatVRScene() { return true; }
bool Is2DVRObject(float* projMatrix, bool ortho) { return false; }
void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye) {}
void UpdateVRView(float* projMatrix, float* leftEye, float* rightEye) {}

#endif
