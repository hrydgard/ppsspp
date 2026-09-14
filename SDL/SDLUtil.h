#pragma once

#include <SDL3/SDL.h>
#include "Common/GPU/MiscTypes.h"
#include <string>

struct WindowDesc;

bool DetermineVulkanWindowSystem(SDL_Window *window, WindowDesc *desc, std::string *errorMessage);
