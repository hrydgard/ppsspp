#pragma once

#include <math.h>
#include <openxr.h>

#ifndef EPSILON
#define EPSILON 0.001f
#endif

typedef struct {
	float M[4][4];
} ovrMatrix4f;

float ToDegrees(float rad);
float ToRadians(float deg);

// ovrMatrix4f
float ovrMatrix4f_Minor(const ovrMatrix4f* m, int r0, int r1, int r2, int c0, int c1, int c2);
ovrMatrix4f ovrMatrix4f_CreateFromQuaternion(const XrQuaternionf* q);
ovrMatrix4f ovrMatrix4f_CreateProjectionFov(const float fovDegreesX, const float fovDegreesY, const float offsetX, const float offsetY, const float nearZ, const float farZ);
ovrMatrix4f ovrMatrix4f_CreateRotation(const float radiansX, const float radiansY, const float radiansZ);
ovrMatrix4f ovrMatrix4f_Inverse(const ovrMatrix4f* m);
ovrMatrix4f ovrMatrix4f_Multiply(const ovrMatrix4f* a, const ovrMatrix4f* b);
XrVector3f ovrMatrix4f_ToEulerAngles(const ovrMatrix4f* m);

// XrPosef
XrPosef XrPosef_Identity();
XrPosef XrPosef_Inverse(const XrPosef a);
XrPosef XrPosef_Multiply(const XrPosef a, const XrPosef b);
XrVector3f XrPosef_Transform(const XrPosef a, const XrVector3f v);

// XrQuaternionf
XrQuaternionf XrQuaternionf_CreateFromVectorAngle(const XrVector3f axis, const float angle);
XrQuaternionf XrQuaternionf_Inverse(const XrQuaternionf q);
XrQuaternionf XrQuaternionf_Multiply(const XrQuaternionf a, const XrQuaternionf b);
XrVector3f XrQuaternionf_Rotate(const XrQuaternionf a, const XrVector3f v);
XrVector3f XrQuaternionf_ToEulerAngles(const XrQuaternionf q);

// XrVector3f, XrVector4f
float XrVector3f_Length(const XrVector3f v);
float XrVector3f_LengthSquared(const XrVector3f v);
XrVector3f XrVector3f_Add(const XrVector3f u, const XrVector3f v);
XrVector3f XrVector3f_GetAnglesFromVectors(const XrVector3f forward, const XrVector3f right, const XrVector3f up);
XrVector3f XrVector3f_Normalized(const XrVector3f v);
XrVector3f XrVector3f_ScalarMultiply(const XrVector3f v, float scale);
XrVector4f XrVector4f_MultiplyMatrix4f(const ovrMatrix4f* a, const XrVector4f* v);
