#pragma once

#include "VRRenderer.h"

bool VR_TweakIs2D(float* projMatrix);
bool VR_TweakIsHUD(bool is2D, bool isOrtho, bool isProjection);
void VR_TweakMirroring(float* projMatrix);
void VR_TweakProjection(float* src, float* dst, VRMatrix matrix);
void VR_TweakView(float* view, float* projMatrix, VRMatrix matrix);
