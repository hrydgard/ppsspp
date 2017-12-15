#pragma once

#include "Common/CommonWindows.h"
#include "Windows/GPU/WindowsGraphicsContext.h"

namespace Draw {
	class DrawContext;
}

class WindowsGLContext : public WindowsGraphicsContext {
public:
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;

	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;

	// Used during window resize. Must be called from the window thread,
	// not the rendering thread or CPU thread.
	void Pause() override;
	void Resume() override;
	void Resize() override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	Draw::DrawContext *draw_;
	HDC hDC;     // Private GDI Device Context
	HGLRC hRC;   // Permanent Rendering Context
	HWND hWnd;   // Holds Our Window Handle
	volatile bool pauseRequested;
	volatile bool resumeRequested;
	HANDLE pauseEvent;
	HANDLE resumeEvent;

	int xres;
	int yres;
};
