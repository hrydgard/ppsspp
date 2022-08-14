#pragma once

#include "VRFramebuffer.h"
#include "VRMath.h"

enum VRConfig {
	VR_CONFIG_MODE,
	VR_CONFIG_6DOF_ENABLED,
	VR_CONFIG_6DOF_SCALE,
	VR_CONFIG_MIRROR_AXIS_X,
	VR_CONFIG_MIRROR_AXIS_Y,
	VR_CONFIG_MIRROR_AXIS_Z,
	VR_CONFIG_MIRROR_PITCH,
	VR_CONFIG_MIRROR_YAW,
	VR_CONFIG_MIRROR_ROLL,
	VR_CONFIG_3D_GEOMETRY_COUNT,
	VR_CONFIG_FOV_SCALE,
	VR_CONFIG_MAX
};

enum VRMatrix {
	VR_PROJECTION_MATRIX_HUD = 0,
	VR_PROJECTION_MATRIX_LEFT_EYE = 1,
	VR_PROJECTION_MATRIX_RIGHT_EYE = 2,
	VR_VIEW_MATRIX_LEFT_EYE = 3,
	VR_VIEW_MATRIX_RIGHT_EYE = 4
};

enum VRMode {
	VR_MODE_FLAT_SCREEN = 0,
	VR_MODE_MONO_6DOF = 1,
	VR_MODE_STEREO_6DOF = 2
};

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine );
void VR_DestroyRenderer( engine_t* engine );

void VR_BeginFrame( engine_t* engine );
void VR_EndFrame( engine_t* engine );

int VR_GetConfig( VRConfig config );
void VR_SetConfig( VRConfig config, int value);

void VR_BindFramebuffer(engine_t *engine);
ovrMatrix4f VR_GetMatrix( VRMatrix matrix );
