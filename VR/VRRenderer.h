#pragma once

#include "VRFramebuffer.h"

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
void VR_SetMode( VRMode mode );
VRMode VR_GetMode();

int VR_GeView3DCount();
void VR_SetView3DCount( int value );

void VR_BindFramebuffer( engine_t* engine, int eye );
ovrMatrix4f VR_GetMatrix( VRMatrix matrix );
void VR_SetInvertedProjection( bool inverted );
