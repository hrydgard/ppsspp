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

#pragma once

#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include "Windows/GPU/WindowsGraphicsContext.h"
#include <d3d11.h>
#include <d3d11_1.h>

class DrawContext;

class D3D11Context : public WindowsGraphicsContext {
public:
	D3D11Context();
	~D3D11Context();
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;

	void Resize() override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	HRESULT CreateTheDevice(IDXGIAdapter *adapter);

	void LostBackbuffer();
	void GotBackbuffer();

	Draw::DrawContext *draw_ = nullptr;
	IDXGISwapChain *swapChain_ = nullptr;
	ID3D11Device *device_ = nullptr;
	ID3D11Device1 *device1_ = nullptr;
	ID3D11DeviceContext *context_ = nullptr;
	ID3D11DeviceContext1 *context1_ = nullptr;

	ID3D11Texture2D *bbRenderTargetTex_ = nullptr;
	ID3D11RenderTargetView *bbRenderTargetView_ = nullptr;

#ifdef _DEBUG
	ID3D11Debug *d3dDebug_ = nullptr;
	ID3D11InfoQueue *d3dInfoQueue_ = nullptr;
#endif

	D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
	int adapterId;
	HDC hDC;     // Private GDI Device Context
	HWND hWnd_;   // Holds Our Window Handle
	HMODULE hD3D11;
	int width;
	int height;
};
