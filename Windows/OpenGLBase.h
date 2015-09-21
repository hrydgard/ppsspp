#pragma once

#include "Common/CommonWindows.h"

bool GL_Init(HWND window, std::string *error_message);
void GL_Shutdown();
void GL_SwapInterval(int interval);
void GL_SwapBuffers();

// Used during window resize. Must be called from the window thread,
// not the rendering thread or CPU thread.
void GL_Pause();
void GL_Resume();