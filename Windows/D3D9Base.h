// Modelled on OpenD3DBase. Might make a cleaner interface later.

#pragma once

#include "Common/CommonWindows.h"

class Thin3DContext;

bool D3D9_Init(HWND window, bool windowed, std::string *error_message);
void D3D9_Shutdown();
void D3D9_Resize(HWND window);
void D3D9_SwapBuffers();
Thin3DContext *D3D9_CreateThin3DContext();