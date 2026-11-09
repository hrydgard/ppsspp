#pragma once

#include "VRBase.h"

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer);

bool ovrRenderer_Create(XrSession session, ovrRenderer* renderer, int width, int height);
void *ovrFramebuffer_GetVulkanImage(ovrFramebuffer* frameBuffer, uint32_t index);
void ovrRenderer_Destroy(ovrRenderer* renderer);
