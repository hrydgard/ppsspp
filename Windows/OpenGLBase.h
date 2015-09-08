#pragma once

#include "Common/CommonWindows.h"

bool GL_Init(HWND window, std::string *error_message);
void GL_Shutdown();
void GL_SwapInterval(int interval);
void GL_SwapBuffers();
