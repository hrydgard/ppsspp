// Modelled on OpenD3DBase. Might make a cleaner interface later.

#pragma once

#include "Common/CommonWindows.h"
#include "Windows/GPU/WindowsGraphicsContext.h"
#include <d3d9.h>

class Thin3DContext;

class D3D9Context : public WindowsGraphicsContext {
public:
	D3D9Context() : has9Ex(false), d3d(nullptr), d3dEx(nullptr), adapterId(-1), device(nullptr), deviceEx(nullptr), hDC(nullptr), hRC(nullptr), hWnd(nullptr), hD3D9(nullptr) {
		memset(&pp, 0, sizeof(pp));
	}

	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;

	void Resize() override;

	Thin3DContext *CreateThin3DContext() override;

private:
	bool has9Ex;
	LPDIRECT3D9 d3d;
	LPDIRECT3D9EX d3dEx;
	int adapterId;
	LPDIRECT3DDEVICE9 device;
	LPDIRECT3DDEVICE9EX deviceEx;
	HDC hDC;     // Private GDI Device Context
	HGLRC hRC;   // Permanent Rendering Context
	HWND hWnd;   // Holds Our Window Handle
	HMODULE hD3D9;
	D3DPRESENT_PARAMETERS pp;
};

