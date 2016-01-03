#pragma once

#include "Common/CommonWindows.h"

bool GL_Init(Windows::UI::Xaml::Controls::SwapChainPanel^ window, std::string *error_message);
bool GL_Resize(Windows::UI::Xaml::Controls::SwapChainPanel^ window);
void GL_Shutdown();
void GL_SwapInterval(int interval);
void GL_SwapBuffers();

// Used during window resize. Must be called from the window thread,
// not the rendering thread or CPU thread.
void GL_Pause();
void GL_Resume();