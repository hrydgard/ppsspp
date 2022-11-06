#pragma once

#include "VRFramebuffer.h"
#include "VRMath.h"

enum VRConfig {
	//switching between 2D and 3D
	VR_CONFIG_MODE, VR_CONFIG_3D_GEOMETRY_COUNT, VR_CONFIG_FORCE_2D,
	//camera setup
	VR_CONFIG_CAMERA_CONTROL,
	VR_CONFIG_HAS_UNIT_SCALE,
	//6DoF
	VR_CONFIG_6DOF_ENABLED, VR_CONFIG_MIRROR_UPDATED,
	VR_CONFIG_MIRROR_AXIS_X, VR_CONFIG_MIRROR_AXIS_Y, VR_CONFIG_MIRROR_AXIS_Z,
	VR_CONFIG_MIRROR_PITCH, VR_CONFIG_MIRROR_YAW, VR_CONFIG_MIRROR_ROLL,
	//mouse cursor
	VR_CONFIG_MOUSE_SIZE, VR_CONFIG_MOUSE_X, VR_CONFIG_MOUSE_Y,
	//viewport setup
	VR_CONFIG_VIEWPORT_WIDTH, VR_CONFIG_VIEWPORT_HEIGHT, VR_CONFIG_VIEWPORT_VALID,
	//render status
	VR_CONFIG_CURRENT_FBO,

	//end
	VR_CONFIG_MAX
};

enum VRConfigFloat {
	// 6dof
	VR_CONFIG_6DOF_SCALE,
	// camera setup
	VR_CONFIG_FOV_SCALE, VR_CONFIG_CAMERA_DISTANCE,
	VR_CONFIG_CAMERA_HEIGHT, VR_CONFIG_CAMERA_SIDE, VR_CONFIG_CANVAS_DISTANCE,
	// 2D canvas positioning
	VR_CONFIG_MENU_PITCH, VR_CONFIG_MENU_YAW, VR_CONFIG_RECENTER_YAW,

	VR_CONFIG_FLOAT_MAX
};

enum VRMode {
	VR_MODE_MONO_SCREEN,
	VR_MODE_STEREO_SCREEN,
	VR_MODE_MONO_6DOF,
	VR_MODE_STEREO_6DOF
};

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine, bool multiview );
void VR_DestroyRenderer( engine_t* engine );

bool VR_InitFrame( engine_t* engine );
void VR_BeginFrame( engine_t* engine, int fboIndex );
void VR_EndFrame( engine_t* engine );
void VR_FinishFrame( engine_t* engine );

int VR_GetConfig( VRConfig config );
void VR_SetConfig( VRConfig config, int value);
float VR_GetConfigFloat( VRConfigFloat config );
void VR_SetConfigFloat( VRConfigFloat config, float value );

void* VR_BindFramebuffer(engine_t *engine);
XrView VR_GetView(int eye);
