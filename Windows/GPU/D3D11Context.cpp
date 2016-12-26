#include "Common/CommonWindows.h"
#include <d3d11.h>

#include "base/logging.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Windows/GPU/D3D11Context.h"
#include "Windows/W32Util/Misc.h"
#include "thin3d/thin3d.h"

void D3D11Context::SwapBuffers() {
	swapChain_->Present(0, 0);
}

Draw::DrawContext *D3D11Context::CreateThin3DContext() {
	return Draw::T3DCreateD3D11Context(device_, context_);
}

static void GetRes(HWND hWnd, int &xres, int &yres) {
	RECT rc;
	GetClientRect(hWnd, &rc);
	xres = rc.right - rc.left;
	yres = rc.bottom - rc.top;
}

void D3D11Context::SwapInterval(int interval) {
	// Dummy
}

bool D3D11Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	bool windowed = true;
	hWnd_ = wnd;
	HRESULT hr = S_OK;

	RECT rc;
	GetClientRect(hWnd_, &rc);
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	const D3D_FEATURE_LEVEL featureLevels[] = {
		// D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {
		driverType_ = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);

		if (hr == E_INVALIDARG) {
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
				D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
		}

		if (SUCCEEDED(hr))
			break;
	}
	if (FAILED(hr))
		return false;

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	{
		IDXGIDevice* dxgiDevice = nullptr;
		hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (SUCCEEDED(hr)) {
			IDXGIAdapter* adapter = nullptr;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr)) {
				hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}
	if (FAILED(hr))
		return false;

	// Create swap chain
	/*
	IDXGIFactory2* dxgiFactory2 = nullptr;
	hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
	if (dxgiFactory2)
	{
		// DirectX 11.1 or later
		hr = device_->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device_1));
		if (SUCCEEDED(hr))
		{
			(void)context_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&context_1));
		}

		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;

		hr = dxgiFactory2->CreateSwapChainForHwnd(device_, g_hWnd, &sd, nullptr, nullptr, &g_pSwapChain1);
		if (SUCCEEDED(hr))
		{
			hr = g_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g_pSwapChain));
		}
		dxgiFactory2->Release();
	} else {
	*/
	// DirectX 11.0 systems
		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 1;
		sd.BufferDesc.Width = width;
		sd.BufferDesc.Height = height;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hWnd_;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;

		hr = dxgiFactory->CreateSwapChain(device_, &sd, &swapChain_);
	// }

	// Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
	dxgiFactory->MakeWindowAssociation(hWnd_, DXGI_MWA_NO_ALT_ENTER);

	dxgiFactory->Release();

	if (FAILED(hr))
		return false;

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hr))
		return false;

	hr = device_->CreateRenderTargetView(pBackBuffer, nullptr, &renderTargetView_);
	pBackBuffer->Release();
	if (FAILED(hr))
		return false;

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = device_->CreateTexture2D(&descDepth, nullptr, &depthStencilTex_);
	if (FAILED(hr))
		return false;

	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	ZeroMemory(&descDSV, sizeof(descDSV));
	descDSV.Format = descDepth.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;
	hr = device_->CreateDepthStencilView(depthStencilTex_, &descDSV, &depthStencilView_);
	if (FAILED(hr))
		return false;

	context_->OMSetRenderTargets(1, &renderTargetView_, depthStencilView_);

	int xres, yres;
	GetRes(hWnd_, xres, yres);

	return true;
}

void D3D11Context::Resize() {
	// This should only be called from the emu thread.
	/*
	int xres, yres;
	GetRes(hWnd, xres, yres);
	bool w_changed = pp.BackBufferWidth != xres;
	bool h_changed = pp.BackBufferHeight != yres;

	if (device && (w_changed || h_changed)) {
		// DX9::fbo_shutdown();

		pp.BackBufferWidth = xres;
		pp.BackBufferHeight = yres;
		HRESULT hr = device_->Reset(&pp);
		if (FAILED(hr)) {
      // Had to remove DXGetErrorStringA calls here because dxerr.lib is deprecated and will not link with VS 2015.
			ERROR_LOG_REPORT(G3D, "Unable to reset D3D device");
			PanicAlert("Unable to reset D3D11 device");
		}
		// DX9::fbo_init(d3d);
	}
	*/
}

void D3D11Context::Shutdown() {
	context_->Release();
	context_ = nullptr;
	device_->Release();
	device_ = nullptr;
	/*
	DX9::DestroyShaders();
	DX9::fbo_shutdown();
	device->EndScene();
	device->Release();
	d3d->Release();
	UnloadD3DXDynamic();
	DX9::pD3Ddevice = nullptr;
	DX9::pD3DdeviceEx = nullptr;
	DX9::pD3D = nullptr;
	*/
	hWnd_ = nullptr;
	// FreeLibrary(hD3D11);
	// hD3D11 = nullptr;
}
