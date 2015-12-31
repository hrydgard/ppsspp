// Modelled on OpenD3DBase. Might make a cleaner interface later.

#pragma once

#include "Common/CommonWindows.h"
#include "Windows/GPU/WindowsGraphicsContext.h"

class Thin3DContext;

class D3D9Context : public WindowsGraphicsContext {
public:
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;

	void Resize() override;

	Thin3DContext *CreateThin3DContext() override;
};

