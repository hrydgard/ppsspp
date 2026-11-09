#pragma once

#include "VRFramebuffer.h"
#include "VRMath.h"

enum VRConfig {
	//switching between mode
	VR_CONFIG_MODE, VR_CONFIG_PASSTHROUGH, VR_CONFIG_CANVAS_6DOF, VR_CONFIG_REPROJECTION,
	//mouse cursor
	VR_CONFIG_MOUSE_SIZE, VR_CONFIG_MOUSE_X, VR_CONFIG_MOUSE_Y,
	//viewport setup
	VR_CONFIG_VIEWPORT_WIDTH, VR_CONFIG_VIEWPORT_HEIGHT, VR_CONFIG_VIEWPORT_VALID,

	//end
	VR_CONFIG_MAX
};

enum VRConfigFloat {
	// 2D canvas positioning
	VR_CONFIG_CANVAS_DISTANCE, VR_CONFIG_MENU_PITCH, VR_CONFIG_MENU_YAW, VR_CONFIG_RECENTER_YAW,
	VR_CONFIG_CANVAS_ASPECT,

	//viewport setup
	VR_CONFIG_VIEWPORT_SUPERSAMPLING,

	VR_CONFIG_FLOAT_MAX
};

enum VRMode {
	VR_MODE_MONO_SCREEN,
	VR_MODE_SBS_SCREEN,
	VR_MODE_STEREO_SCREEN,
	VR_MODE_MONO_6DOF,
	VR_MODE_SBS_6DOF,
	VR_MODE_STEREO_6DOF
};

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine );
void VR_DestroyRenderer( engine_t* engine );

bool VR_InitFrame( engine_t* engine );
void VR_FinishFrame( engine_t* engine );

int VR_GetConfig( VRConfig config );
void VR_SetConfig( VRConfig config, int value);
float VR_GetConfigFloat( VRConfigFloat config );
void VR_SetConfigFloat( VRConfigFloat config, float value );

// VR_InitFrame begins the frame on the emu thread, and the render thread ends it with
// VR_FinishFrame after submitting. Present() calls VR_NotifyPresentQueued so the next VR_InitFrame
// knows to wait for that, instead of ending the frame itself.
void VR_NotifyPresentQueued();
bool VR_AcquireVulkanImage(engine_t *engine, uint32_t *index);
void VR_ReleaseVulkanImage(engine_t *engine);
// Around every use of the VkQueue the session was created with.
void VR_LockQueue();
void VR_UnlockQueue();
XrView VR_GetView(int eye);
XrVector3f VR_GetHMDAngles();
