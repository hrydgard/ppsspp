// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

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

