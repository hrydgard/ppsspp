#pragma once

#include "VRBase.h"

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer);
void* ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer);

void ovrRenderer_Create(XrSession session, ovrRenderer* renderer, int width, int height, void* vulkanContext);
void ovrRenderer_Destroy(ovrRenderer* renderer);
void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int sx, int sy);
