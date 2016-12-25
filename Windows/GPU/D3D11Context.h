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
#include <d3d11.h>

class DrawContext;

class D3D11Context : public WindowsGraphicsContext {
public:
	D3D11Context() : adapterId(-1), hDC(nullptr), hWnd_(nullptr), hD3D11(nullptr) {
	}

	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;

	void Resize() override;

	Draw::DrawContext *CreateThin3DContext() override;

private:
	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	IDXGISwapChain *swapChain_ = nullptr;
	ID3D11RenderTargetView *renderTargetView_ = nullptr;
	ID3D11Texture2D *depthStencilTex_ = nullptr;
	ID3D11DepthStencilView *depthStencilView_ = nullptr;

	D3D_DRIVER_TYPE driverType_;
	D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
	int adapterId;
	HDC hDC;     // Private GDI Device Context
	HWND hWnd_;   // Holds Our Window Handle
	HMODULE hD3D11;
	// D3DPRESENT_PARAMETERS pp;
};

