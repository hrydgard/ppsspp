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
#include <wrl/client.h>

class DrawContext;

class D3D11Context : public WindowsGraphicsContext {
public:
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;

	void Resize() override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	HRESULT CreateTheDevice(IDXGIAdapter *adapter);

	void LostBackbuffer();
	void GotBackbuffer();

	Draw::DrawContext *draw_ = nullptr;
	Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
	DXGI_SWAP_CHAIN_DESC swapChainDesc_ = {};
	Microsoft::WRL::ComPtr<ID3D11Device> device_;
	Microsoft::WRL::ComPtr<ID3D11Device1> device1_;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1_;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> bbRenderTargetTex_;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bbRenderTargetView_;

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D11Debug> d3dDebug_;
	Microsoft::WRL::ComPtr<ID3D11InfoQueue> d3dInfoQueue_;
#endif
	D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
	int adapterId = -1;
	HDC hDC = nullptr;     // Private GDI Device Context
	HWND hWnd_ = nullptr;   // Holds Our Window Handle
	HMODULE hD3D11 = nullptr;
	int width = 0;
	int height = 0;
	int swapInterval_ = 0;
};
