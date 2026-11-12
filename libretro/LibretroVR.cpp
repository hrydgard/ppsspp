// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cmath>

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/fast/fast_matrix.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/VR/VRBase.h"
#include "Common/VR/VRMath.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

#include "libretro/LibretroGraphicsContext.h"
#include "libretro/LibretroVR.h"

// RetroArch owns OpenXR and supplies the poses. The core renders both eyes with Vulkan multiview,
// and PresentationCommon::CopyToLibretroVR puts them side by side in the output image.
namespace Libretro {
static VRMode vrMode = VRMode::Off;
static bool vrActive = false;
static bool vrImmersive = false;
static bool vrReferenceValid = false;
static bool vrCylinder = false;
static float vrCanvasAspect = 2.0f;
static unsigned vrWidth = 0, vrHeight = 0;
static retro_vr_frame_state vrFrame{};
static XrPosef vrReference = XrPosef_Identity();

void DestroyVR() {
	vrActive = vrImmersive = vrReferenceValid = false;
	vrWidth = vrHeight = 0;
	vrCylinder = false;
}

void SetVRMode(VRMode mode) {
	if (vrMode != mode) {
		vrReferenceValid = false;
	}
	vrMode = mode;
}

bool VRActive() {
	return vrActive;
}

static bool ValidEye(const retro_vr_eye_state &eye) {
	float length = 0.0f;
	for (float component : eye.orientation) {
		if (!std::isfinite(component)) {
			return false;
		}
		length += component * component;
	}
	if (fabsf(length - 1.0f) > 0.01f) {
		return false;
	}
	for (float component : eye.position) {
		if (!std::isfinite(component)) {
			return false;
		}
	}
	for (float tangent : eye.fov_tan) {
		if (!std::isfinite(tangent) || tangent <= 0.0f) {
			return false;
		}
	}
	return true;
}

static bool PublishVRViews(bool tracked) {
	retro_video_view eyes[2]{};
	for (int eye = 0; eye < 2; ++eye) {
		eyes[eye] = {eye * vrWidth, 0, vrWidth, vrHeight, 0,
			(unsigned)(eye == 0 ? RETRO_VIDEO_VIEW_EYE_LEFT : RETRO_VIDEO_VIEW_EYE_RIGHT),
			vrHeight ? (float)vrWidth / vrHeight : 0.0f};
	}
	retro_video_views views{};
	views.views = eyes;
	views.num_views = vrActive ? 2 : 0;
	views.flags = tracked ? RETRO_VIDEO_VIEWS_FLAG_TRACKED : 0;
	views.reference_space = RETRO_VR_REFERENCE_SPACE_LOCAL;
	bool accepted = environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views);
	if (views.recommended_view_width && views.recommended_view_height) {
		vrCanvasAspect = 2.0f * views.recommended_view_width / views.recommended_view_height;
	}
	return accepted;
}

void UpdateVR() {
	const bool wasActive = vrActive;
	vrActive = vrImmersive = false;
	vrCylinder = false;
	unsigned status = 0;
	const bool supported = ctx && ctx->GetGPUCore() == GPUCORE_VULKAN && !g_Config.bSoftwareRendering &&
		ctx->GetDrawContext() && ctx->GetDrawContext()->GetDeviceCaps().multiViewSupported;
	if (supported && vrMode != VRMode::Off &&
		environ_cb(RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS, &status) &&
		(status & RETRO_VIDEO_VIEWS_STATUS_STEREO) && (status & RETRO_VIDEO_VIEWS_STATUS_PRESENTS)) {
		vrWidth = PSP_CoreParameter().pixelWidth;
		vrHeight = PSP_CoreParameter().pixelHeight;
		const unsigned maxSize = ctx->GetDrawContext()->GetDeviceCaps().maxTextureSize;
		vrActive = vrWidth && vrHeight && vrWidth <= maxSize / 2 && vrHeight <= maxSize;
		vrFrame = {};
		bool tracked = (status & RETRO_VIDEO_VIEWS_STATUS_HMD) &&
			environ_cb(RETRO_ENVIRONMENT_GET_VR_FRAME_STATE, &vrFrame) &&
			ValidEye(vrFrame.eyes[0]) && ValidEye(vrFrame.eyes[1]);
		vrImmersive = vrActive && tracked && vrMode == VRMode::Immersive;
		if (tracked && (!vrReferenceValid || (vrFrame.flags & RETRO_VR_FRAME_RECENTERED))) {
			const auto &left = vrFrame.eyes[0];
			const auto &right = vrFrame.eyes[1];
			vrReference.orientation = {left.orientation[0], left.orientation[1], left.orientation[2], left.orientation[3]};
			vrReference.position = {(left.position[0] + right.position[0]) * 0.5f,
				(left.position[1] + right.position[1]) * 0.5f, (left.position[2] + right.position[2]) * 0.5f};
			vrReferenceValid = true;
		}
		if (!tracked) {
			vrReferenceValid = false;
		}
	} else {
		vrReferenceValid = false;
	}

	g_Config.bEnableStereo = true;
	// IdentityViewHack uses standalone's projection-layer path, not its immersive cylinder.
	// Keep those games on a stereo screen until that presentation path is adapted too.
	g_Config.bEnableVR = vrImmersive && !PSP_CoreParameter().compat.vrCompat().IdentityViewHack;
	g_Config.bEnableImmersiveVR = g_Config.bEnableVR;
	SetVRAppMode(VR_GAME_MODE);
	if (vrActive) {
		StartVRRender();
		vrCylinder = vrImmersive && !IsFlatVRScene();
	}
	if (!PublishVRViews(vrCylinder)) {
		vrActive = vrImmersive = false;
		PublishVRViews(false);
	}
	if (!vrActive) {
		vrCylinder = false;
		g_Config.bEnableVR = g_Config.bEnableImmersiveVR = false;
	}
	if (wasActive != vrActive) {
		if (gpu) {
			gpu->NotifyConfigChanged();
		}
		unsigned width = PSP_CoreParameter().pixelWidth;
		unsigned height = PSP_CoreParameter().pixelHeight;
		retro_game_geometry geometry{width * (vrActive ? 2u : 1u), height,
			width * 2, height, (float)width * (vrActive ? 2.0f : 1.0f) / height};
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
		INFO_LOG(Log::G3D, "Libretro VR: %s", vrActive ? "stereo views active (immersive cylinder available)" : "flat output");
	}
}

bool VRRequested() {
	return vrMode != VRMode::Off;
}

void GetVRView(int eye, XrView *view) {
	*view = {};
	view->type = XR_TYPE_VIEW;
	view->pose = XrPosef_Identity();
	const auto &source = vrFrame.eyes[eye];
	view->pose.position.x = eye == 0 ? -0.032f : 0.032f;
	if (vrReferenceValid) {
		view->pose.position = {source.position[0], source.position[1], source.position[2]};
		view->pose.orientation = {source.orientation[0], source.orientation[1], source.orientation[2], source.orientation[3]};
	}
	view->fov = {-atanf(source.fov_tan[0] > 0 ? source.fov_tan[0] : 1.0f),
		atanf(source.fov_tan[1] > 0 ? source.fov_tan[1] : 1.0f),
		atanf(source.fov_tan[2] > 0 ? source.fov_tan[2] : 1.0f),
		-atanf(source.fov_tan[3] > 0 ? source.fov_tan[3] : 1.0f)};
}

bool VRCanvasActive() {
	return vrCylinder;
}

float VRCanvasAspect() {
	return vrCanvasAspect;
}

// The camera for the immersive cylinder, in OpenGL clip coordinates.
void GetVRCanvasMatrix(int eye, float *mvp) {
	const auto &pose = vrFrame.eyes[eye];
	XrQuaternionf referenceInverse = XrQuaternionf_Inverse(vrReference.orientation);
	XrPosef camera = XrPosef_Identity();

	camera.orientation = XrQuaternionf_Multiply(referenceInverse,
		{pose.orientation[0], pose.orientation[1], pose.orientation[2], pose.orientation[3]});

	// Match standalone's default: the cylinder follows translation, never rotation.
	// Keep the eye separation so HUD elements lie on the cylinder, not at infinity.
	XrVector3f origin = vrReference.position;
	if (!g_Config.bEnable6DoF) {
		origin = {(vrFrame.eyes[0].position[0] + vrFrame.eyes[1].position[0]) * 0.5f,
			(vrFrame.eyes[0].position[1] + vrFrame.eyes[1].position[1]) * 0.5f,
			(vrFrame.eyes[0].position[2] + vrFrame.eyes[1].position[2]) * 0.5f};
	}

	camera.position = XrQuaternionf_Rotate(referenceInverse, {
		pose.position[0] - origin.x, pose.position[1] - origin.y, pose.position[2] - origin.z});
	XrPosef view = XrPosef_Inverse(camera);
	float rowMajor[16], matrix[16];
	XrQuaternionf_ToMatrix4f(&view.orientation, rowMajor);

	for (int col = 0; col < 4; ++col) {
		for (int row = 0; row < 4; ++row) {
			matrix[col * 4 + row] = rowMajor[row * 4 + col];
		}
	}

	matrix[12] = view.position.x;
	matrix[13] = view.position.y;
	matrix[14] = view.position.z;
	const float *fov = pose.fov_tan;
	float lens[16]{};

	lens[0] = 2.0f / (fov[0] + fov[1]);
	lens[5] = 2.0f / (fov[2] + fov[3]);
	lens[8] = (fov[1] - fov[0]) / (fov[0] + fov[1]);
	lens[9] = (fov[2] - fov[3]) / (fov[2] + fov[3]);
	lens[10] = -1.0f;
	lens[11] = -1.0f;
	lens[14] = -0.02f;

	fast_matrix_mul_4x4(mvp, lens, matrix);
}

}  // namespace Libretro
