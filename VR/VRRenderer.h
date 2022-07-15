#ifndef __VR_RENDERER
#define __VR_RENDERER

#include "VRFramebuffer.h"

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine );
void VR_DestroyRenderer( engine_t* engine );
void VR_BeginFrame( engine_t* engine );
void VR_DrawFrame( engine_t* engine );
void VR_ReInitRenderer();
unsigned int VR_Framebuffer( engine_t* engine, int eye );

#endif
