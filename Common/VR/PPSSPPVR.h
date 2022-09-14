#pragma once

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

#ifdef OPENXR

// VR app flow integration
bool IsVRBuild();
void InitVROnAndroid(void* vm, void* activity, int version, const char* name);
void EnterVR(bool firstStart);
void GetVRResolutionPerEye(int* width, int* height);
void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool(*NativeTouch)(const TouchInput &touch), bool haptics, float dp_xscale, float dp_yscale);
void UpdateVRScreenKey(const KeyInput &key);

// VR rendering integration
void BindVRFramebuffer();
bool StartVRRender();
void FinishVRRender();
void PreVRFrameRender(int fboIndex);
void PostVRFrameRender();
int GetVRFBOIndex();
bool IsMultiviewSupported();
bool IsFlatVRScene();
bool Is2DVRObject(float* projMatrix, bool ortho);
void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye);
void UpdateVRView(float* leftEye, float* rightEye);

#else //dummy integration

// VR app flow integration
inline bool IsVRBuild() { return false; }
inline void InitVROnAndroid(void* vm, void* activity, int version, const char* name) {}
inline void EnterVR(bool firstTime) {}
inline void GetVRResolutionPerEye(int* width, int* height) {}
inline void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool(*NativeTouch)(const TouchInput &touch), bool haptics, float dp_xscale, float dp_yscale) {}
inline void UpdateVRScreenKey(const KeyInput &key) {}

// VR rendering integration
inline void BindVRFramebuffer() {}
inline bool StartVRRender() { return false; }
inline void FinishVRRender() {}
inline void PreVRFrameRender(int fboIndex) {}
inline void PostVRFrameRender() {}
inline int GetVRFBOIndex() { return 0; }
inline bool IsMultiviewSupported() { return false; }
inline bool IsFlatVRScene() { return true; }
inline bool Is2DVRObject(float* projMatrix, bool ortho) { return false; }
inline void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye) {}
inline void UpdateVRView(float* leftEye, float* rightEye) {}

#endif
