#pragma once

#include "VRFramebuffer.h"

void VR_Init( ovrJava java );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );
