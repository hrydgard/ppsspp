// Copyright (c) 2012- PPSSPP Project.
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2.0 or later.

#pragma once

struct XrView;

namespace Libretro {
enum class VRMode { Off, Stereoscopic, Immersive };

// Frontend-thread entry points.
void SetVRMode(VRMode mode);
void UpdateVR();
void DestroyVR();
bool VRActive();
bool VRRequested();
void GetVRView(int eye, XrView *view);
bool VRCanvasActive();
float VRCanvasAspect();
void GetVRCanvasMatrix(int eye, float *mvp);
}
