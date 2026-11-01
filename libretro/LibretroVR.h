// Copyright (c) 2012- PPSSPP Project.
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2.0 or later.

#pragma once

struct XrView;

namespace Libretro {
enum class VRMode { Off, Stereoscopic, Immersive };

// Frontend/GL-thread entry points.
void SetVRMode(VRMode mode);
void UpdateVR();
void DestroyVR();
bool VRActive();
bool VRRequested();
int VREye();
void GetVRView(int eye, XrView *view);
unsigned VRGeneration();
void BeginVREye(int eye);
void EndVREyes();
bool RetainVRCommands();
void PresentVR(unsigned framebuffer);
}
