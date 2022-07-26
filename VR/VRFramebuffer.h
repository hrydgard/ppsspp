#pragma once

//OpenXR
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#include <math.h>
#include <openxr.h>
#include <openxr_platform.h>

#define MATH_PI 3.14159265358979323846f

#define ALOGE(...) printf(__VA_ARGS__)
#define ALOGV(...) printf(__VA_ARGS__)

typedef union {
	XrCompositionLayerProjection Projection;
	XrCompositionLayerCylinderKHR Cylinder;
} ovrCompositorLayer_Union;

enum { ovrMaxLayerCount = 1 };
enum { ovrMaxNumEyes = 2 };

#define GL(func) func;
#define OXR(func) func;

typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
	char AppName[64];
	int AppVersion;
} ovrJava;

typedef struct {
	XrSwapchain Handle;
	uint32_t Width;
	uint32_t Height;
} ovrSwapChain;

typedef struct {
	int Width;
	int Height;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
	unsigned int* DepthBuffers;
	unsigned int* FrameBuffers;
} ovrFramebuffer;

typedef struct {
	ovrFramebuffer FrameBuffer[ovrMaxNumEyes];
} ovrRenderer;

typedef struct {
	int Active;
	XrPosef Pose;
} ovrTrackedController;

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

	int TouchPadDownLastFrame;
	ovrRenderer Renderer;
	ovrTrackedController TrackedController[2];
} ovrApp;


typedef struct {
	float M[4][4];
} ovrMatrix4f;

typedef struct {
	uint64_t frameIndex;
	ovrApp appState;
	ovrJava java;
	float predictedDisplayTime;
} engine_t;

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Resolve(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_SetNone();

void ovrRenderer_Create(
		XrSession session,
		ovrRenderer* renderer,
		int suggestedEyeTextureWidth,
		int suggestedEyeTextureHeight);
void ovrRenderer_Destroy(ovrRenderer* renderer);

void ovrTrackedController_Clear(ovrTrackedController* controller);

ovrMatrix4f ovrMatrix4f_Multiply(const ovrMatrix4f* a, const ovrMatrix4f* b);
ovrMatrix4f ovrMatrix4f_CreateRotation(const float radiansX, const float radiansY, const float radiansZ);
ovrMatrix4f ovrMatrix4f_CreateFromQuaternion(const XrQuaternionf* q);
ovrMatrix4f ovrMatrix4f_CreateProjectionFov(
		const float fovDegreesX,
		const float fovDegreesY,
		const float offsetX,
		const float offsetY,
		const float nearZ,
		const float farZ);

static inline float
ovrMatrix4f_Minor(const ovrMatrix4f* m, int r0, int r1, int r2, int c0, int c1, int c2) {
	return m->M[r0][c0] * (m->M[r1][c1] * m->M[r2][c2] - m->M[r2][c1] * m->M[r1][c2]) -
	       m->M[r0][c1] * (m->M[r1][c0] * m->M[r2][c2] - m->M[r2][c0] * m->M[r1][c2]) +
	       m->M[r0][c2] * (m->M[r1][c0] * m->M[r2][c1] - m->M[r2][c0] * m->M[r1][c1]);
}

static inline ovrMatrix4f ovrMatrix4f_Inverse(const ovrMatrix4f* m) {
	const float rcpDet = 1.0f /
	                     (m->M[0][0] * ovrMatrix4f_Minor(m, 1, 2, 3, 1, 2, 3) -
	                      m->M[0][1] * ovrMatrix4f_Minor(m, 1, 2, 3, 0, 2, 3) +
	                      m->M[0][2] * ovrMatrix4f_Minor(m, 1, 2, 3, 0, 1, 3) -
	                      m->M[0][3] * ovrMatrix4f_Minor(m, 1, 2, 3, 0, 1, 2));
	ovrMatrix4f out;
	out.M[0][0] = ovrMatrix4f_Minor(m, 1, 2, 3, 1, 2, 3) * rcpDet;
	out.M[0][1] = -ovrMatrix4f_Minor(m, 0, 2, 3, 1, 2, 3) * rcpDet;
	out.M[0][2] = ovrMatrix4f_Minor(m, 0, 1, 3, 1, 2, 3) * rcpDet;
	out.M[0][3] = -ovrMatrix4f_Minor(m, 0, 1, 2, 1, 2, 3) * rcpDet;
	out.M[1][0] = -ovrMatrix4f_Minor(m, 1, 2, 3, 0, 2, 3) * rcpDet;
	out.M[1][1] = ovrMatrix4f_Minor(m, 0, 2, 3, 0, 2, 3) * rcpDet;
	out.M[1][2] = -ovrMatrix4f_Minor(m, 0, 1, 3, 0, 2, 3) * rcpDet;
	out.M[1][3] = ovrMatrix4f_Minor(m, 0, 1, 2, 0, 2, 3) * rcpDet;
	out.M[2][0] = ovrMatrix4f_Minor(m, 1, 2, 3, 0, 1, 3) * rcpDet;
	out.M[2][1] = -ovrMatrix4f_Minor(m, 0, 2, 3, 0, 1, 3) * rcpDet;
	out.M[2][2] = ovrMatrix4f_Minor(m, 0, 1, 3, 0, 1, 3) * rcpDet;
	out.M[2][3] = -ovrMatrix4f_Minor(m, 0, 1, 2, 0, 1, 3) * rcpDet;
	out.M[3][0] = -ovrMatrix4f_Minor(m, 1, 2, 3, 0, 1, 2) * rcpDet;
	out.M[3][1] = ovrMatrix4f_Minor(m, 0, 2, 3, 0, 1, 2) * rcpDet;
	out.M[3][2] = -ovrMatrix4f_Minor(m, 0, 1, 3, 0, 1, 2) * rcpDet;
	out.M[3][3] = ovrMatrix4f_Minor(m, 0, 1, 2, 0, 1, 2) * rcpDet;
	return out;
}


XrVector4f XrVector4f_MultiplyMatrix4f(const ovrMatrix4f* a, const XrVector4f* v);


/// THESE METHODS HAVE ORIGIN IN openxr_oculus_helpers.h

static inline double FromXrTime(const XrTime time) {
	return (time * 1e-9);
}

static inline XrTime ToXrTime(const double timeInSeconds) {
	return (timeInSeconds * 1e9);
}

static inline XrPosef XrPosef_Identity() {
	XrPosef r;
	r.orientation.x = 0;
	r.orientation.y = 0;
	r.orientation.z = 0;
	r.orientation.w = 1;
	r.position.x = 0;
	r.position.y = 0;
	r.position.z = 0;
	return r;
}

static inline float XrVector3f_LengthSquared(const XrVector3f v) {
	return v.x * v.x + v.y * v.y + v.z * v.z;;
}

static inline float XrVector3f_Length(const XrVector3f v) {
	return sqrtf(XrVector3f_LengthSquared(v));
}

static inline XrVector3f XrVector3f_ScalarMultiply(const XrVector3f v, float scale) {
	XrVector3f u;
	u.x = v.x * scale;
	u.y = v.y * scale;
	u.z = v.z * scale;
	return u;
}

static inline XrVector3f XrVector3f_Normalized(const XrVector3f v) {
	float rcpLen = 1.0f / XrVector3f_Length(v);
	return XrVector3f_ScalarMultiply(v, rcpLen);
}

static inline XrQuaternionf XrQuaternionf_CreateFromVectorAngle(
		const XrVector3f axis,
		const float angle) {
	XrQuaternionf r;
	if (XrVector3f_LengthSquared(axis) == 0.0f) {
		r.x = 0;
		r.y = 0;
		r.z = 0;
		r.w = 1;
		return r;
	}

	XrVector3f unitAxis = XrVector3f_Normalized(axis);
	float sinHalfAngle = sinf(angle * 0.5f);

	r.w = cosf(angle * 0.5f);
	r.x = unitAxis.x * sinHalfAngle;
	r.y = unitAxis.y * sinHalfAngle;
	r.z = unitAxis.z * sinHalfAngle;
	return r;
}

static inline XrQuaternionf XrQuaternionf_Multiply(const XrQuaternionf a, const XrQuaternionf b) {
	XrQuaternionf c;
	c.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	c.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	c.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	c.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return c;
}

static inline XrQuaternionf XrQuaternionf_Inverse(const XrQuaternionf q) {
	XrQuaternionf r;
	r.x = -q.x;
	r.y = -q.y;
	r.z = -q.z;
	r.w = q.w;
	return r;
}

static inline XrVector3f XrQuaternionf_Rotate(const XrQuaternionf a, const XrVector3f v) {
	XrVector3f r;
	XrQuaternionf q = {v.x, v.y, v.z, 0.0f};
	XrQuaternionf aq = XrQuaternionf_Multiply(a, q);
	XrQuaternionf aInv = XrQuaternionf_Inverse(a);
	XrQuaternionf aqaInv = XrQuaternionf_Multiply(aq, aInv);
	r.x = aqaInv.x;
	r.y = aqaInv.y;
	r.z = aqaInv.z;
	return r;
}

static inline XrVector3f XrVector3f_Add(const XrVector3f u, const XrVector3f v) {
	XrVector3f w;
	w.x = u.x + v.x;
	w.y = u.y + v.y;
	w.z = u.z + v.z;
	return w;
}

static inline XrVector3f XrPosef_Transform(const XrPosef a, const XrVector3f v) {
	XrVector3f r0 = XrQuaternionf_Rotate(a.orientation, v);
	return XrVector3f_Add(r0, a.position);
}

static inline XrPosef XrPosef_Multiply(const XrPosef a, const XrPosef b) {
	XrPosef c;
	c.orientation = XrQuaternionf_Multiply(a.orientation, b.orientation);
	c.position = XrPosef_Transform(a, b.position);
	return c;
}

static inline XrPosef XrPosef_Inverse(const XrPosef a) {
	XrPosef b;
	b.orientation = XrQuaternionf_Inverse(a.orientation);
	b.position = XrQuaternionf_Rotate(b.orientation, XrVector3f_ScalarMultiply(a.position, -1.0f));
	return b;
}
