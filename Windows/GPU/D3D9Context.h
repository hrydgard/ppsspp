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
#include <wrl/client.h>

namespace Draw {
	class DrawContext;
}

class D3D9Context : public WindowsGraphicsContext {
public:
	D3D9Context() : draw_(nullptr), has9Ex_(false), d3d_(nullptr), d3dEx_(nullptr), adapterId_(-1), device_(nullptr), deviceEx_(nullptr), hDC_(nullptr), hWnd_(nullptr), hD3D9_(nullptr), presentParams_{} {
	}

	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;

	void Resize() override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	Draw::DrawContext *draw_;
	bool has9Ex_;
	Microsoft::WRL::ComPtr<IDirect3D9> d3d_;
	Microsoft::WRL::ComPtr<IDirect3D9Ex> d3dEx_;
	int adapterId_;
	Microsoft::WRL::ComPtr<IDirect3DDevice9> device_;
	Microsoft::WRL::ComPtr<IDirect3DDevice9Ex> deviceEx_;
	HDC hDC_;     // Private GDI Device Context
	HWND hWnd_;   // Holds Our Window Handle
	HMODULE hD3D9_;
	D3DPRESENT_PARAMETERS presentParams_;
	int swapInterval_ = 0;
};

