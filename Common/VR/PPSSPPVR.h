#pragma once

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"

enum VRCompatFlag {
	//compatibility tweaks
	VR_COMPAT_SKYPLANE,

	//render state
	VR_COMPAT_GEOMETRY, VR_USE_CLIP,

	//uniforms
	VR_COMPAT_FOG_COLOR,

	//end
	VR_COMPAT_MAX
};

#ifdef OPENXR

// VR app flow integration
bool IsVRBuild();
void InitVROnAndroid(void* vm, void* activity, int version, char* name);
void EnterVR(bool firstStart);
void GetVRResolutionPerEye(int* width, int* height);
void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool(*NativeTouch)(const TouchInput &touch), bool haptics, float dp_xscale, float dp_yscale);
void UpdateVRScreenKey(const KeyInput &key);

// VR games compatibility
void PreGLRenderPass(const void* step);
void PreGLCommand(const void* data);
void PostGLCommand(const void* data);
void SetVRCompat(VRCompatFlag flag, long value);

// VR rendering integration
void BindVRFramebuffer();
bool PreVRRender();
void PostVRRender();
void PreVRFrameRender(int fboIndex);
void PostVRFrameRender();
int GetVRFBOIndex();
bool IsMultiviewSupported();
bool IsFlatVRScene();
bool Is2DVRObject(float* projMatrix, bool ortho);
void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye);
void UpdateVRView(float* projMatrix, float* leftEye, float* rightEye);

#else //dummy integration

// VR app flow integration
inline bool IsVRBuild() { return false; }
inline void InitVROnAndroid(void* vm, void* activity, int version, char* name) {}
inline void EnterVR(bool firstTime) {}
inline void GetVRResolutionPerEye(int* width, int* height) {}
inline void UpdateVRInput(bool(*NativeKey)(const KeyInput &key), bool(*NativeTouch)(const TouchInput &touch), bool haptics, float dp_xscale, float dp_yscale) {}
inline void UpdateVRScreenKey(const KeyInput &key) {}

// VR games compatibility
inline void PreGLRenderPass(const void* step) {}
inline void PreGLCommand(const void* data) {}
inline void PostGLCommand(const void* data) {}
inline void SetVRCompat(VRCompatFlag flag, long value) {}

// VR rendering integration
inline void BindVRFramebuffer() {}
inline bool PreVRRender() { return false; }
inline void PostVRRender() {}
inline void PreVRFrameRender(int fboIndex) {}
inline void PostVRFrameRender() {}
inline int GetVRFBOIndex() { return 0; }
inline bool IsMultiviewSupported() { return false; }
inline bool IsFlatVRScene() { return true; }
inline bool Is2DVRObject(float* projMatrix, bool ortho) { return false; }
inline void UpdateVRProjection(float* projMatrix, float* leftEye, float* rightEye) {}
inline void UpdateVRView(float* projMatrix, float* leftEye, float* rightEye) {}

#endif
