// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool GL_Init(HWND window, std::string *error_message);

void GL_Shutdown();
void GL_Resized();
void GL_SwapBuffers();
