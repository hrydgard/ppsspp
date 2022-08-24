#pragma once

#include "VRRenderer.h"

bool VR_TweakIsMatrixBigScale(float* matrix);
bool VR_TweakIsMatrixIdentity(float* matrix);
bool VR_TweakIsMatrixOneOrtho(float* matrix);
bool VR_TweakIsMatrixOneScale(float* matrix);
bool VR_TweakIsMatrixOneTransform(float* matrix);
void VR_TweakMirroring(float* projMatrix);
void VR_TweakProjection(float* src, float* dst, VRMatrix matrix);
void VR_TweakView(float* view, float* projMatrix, VRMatrix matrix);
