#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include <d3d11.h>
#include <WinError.h>
#include <cassert>

#include "base/logging.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Windows/GPU/D3D11Context.h"
#include "Windows/W32Util/Misc.h"
#include "thin3d/thin3d.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/d3d11_loader.h"

#ifdef __MINGW32__
#undef __uuidof
#define __uuidof(type) IID_##type
#endif

#ifndef DXGI_ERROR_NOT_FOUND
#define _FACDXGI    0x87a
#define MAKE_DXGI_HRESULT(code) MAKE_HRESULT(1, _FACDXGI, code)
#define DXGI_ERROR_NOT_FOUND MAKE_DXGI_HRESULT(2)
#endif

#if PPSSPP_PLATFORM(UWP)
#error This file should not be compiled for UWP.
#endif

D3D11Context::D3D11Context() : draw_(nullptr), adapterId(-1), hDC(nullptr), hWnd_(nullptr), hD3D11(nullptr) {
}

D3D11Context::~D3D11Context() {
}

void D3D11Context::SwapBuffers() {
	swapChain_->Present(0, 0);
	draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
}

void D3D11Context::SwapInterval(int interval) {
	// Dummy
}

HRESULT D3D11Context::CreateTheDevice(IDXGIAdapter *adapter) {
	bool windowed = true;
#ifdef _DEBUG
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
	UINT createDeviceFlags = 0;
#endif

	static const D3D_DRIVER_TYPE driverTypes[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	const UINT numDriverTypes = ARRAYSIZE(driverTypes);

	static const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1,
	};
	const UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	HRESULT hr = S_OK;
	D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;
	hr = ptr_D3D11CreateDevice(adapter, driverType, nullptr, createDeviceFlags, (D3D_FEATURE_LEVEL *)featureLevels, numFeatureLevels,
		D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
	if (hr == E_INVALIDARG) {
		// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
		hr = ptr_D3D11CreateDevice(adapter, driverType, nullptr, createDeviceFlags, (D3D_FEATURE_LEVEL *)&featureLevels[3], numFeatureLevels - 3,
			D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
	}
	return hr;
}

static void GetRes(HWND hWnd, int &xres, int &yres) {
	RECT rc;
	GetClientRect(hWnd, &rc);
	xres = rc.right - rc.left;
	yres = rc.bottom - rc.top;
}

bool D3D11Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	hWnd_ = wnd;
	LoadD3D11Error result = LoadD3D11();

	HRESULT hr = E_FAIL;
	std::vector<std::string> adapterNames;
	if (result == LoadD3D11Error::SUCCESS) {
		std::vector<IDXGIAdapter *> adapters;
		int chosenAdapter = 0;

		IDXGIFactory * pFactory = nullptr;
		ptr_CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);

		IDXGIAdapter *pAdapter;
		for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
			adapters.push_back(pAdapter);
			DXGI_ADAPTER_DESC desc;
			pAdapter->GetDesc(&desc);
			std::string str = ConvertWStringToUTF8(desc.Description);
			adapterNames.push_back(str);
			if (str == g_Config.sD3D11Device) {
				chosenAdapter = i;
			}
		}

		hr = CreateTheDevice(adapters[chosenAdapter]);
		for (int i = 0; i < (int)adapters.size(); i++) {
			adapters[i]->Release();
		}
	}

	if (FAILED(hr)) {
		const char *defaultError = "Your GPU does not appear to support Direct3D 11.\n\nWould you like to try again using Direct3D 9 instead?";
		I18NCategory *err = GetI18NCategory("Error");

		std::wstring error;

		if (result == LoadD3D11Error::FAIL_NO_COMPILER) {
			error = ConvertUTF8ToWString(err->T("D3D11CompilerMissing", "D3DCompiler_47.dll not found. Please install. Or press Yes to try again using Direct3D9 instead."));
		} else if (result == LoadD3D11Error::FAIL_NO_D3D11) {
			error = ConvertUTF8ToWString(err->T("D3D11Missing", "Your operating system version does not include D3D11. Please run Windows Update.\n\nPress Yes to try again using Direct3D9 instead."));
		}

		error = ConvertUTF8ToWString(err->T("D3D11NotSupported", defaultError));
		std::wstring title = ConvertUTF8ToWString(err->T("D3D11InitializationError", "Direct3D 11 initialization error"));
		bool yes = IDYES == MessageBox(hWnd_, error.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);
		if (yes) {
			// Change the config to D3D and restart.
			g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D9;
			g_Config.Save();

			W32Util::ExitAndRestart();
		}
		return false;
	}

	if (FAILED(device_->QueryInterface(__uuidof (ID3D11Device1), (void **)&device1_))) {
		device1_ = nullptr;
	}

	if (FAILED(context_->QueryInterface(__uuidof (ID3D11DeviceContext1), (void **)&context1_))) {
		context1_ = nullptr;
	}

#ifdef _DEBUG
	if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug_))) {
		if (SUCCEEDED(d3dDebug_->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue_))) {
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
		}
	}
#endif

	draw_ = Draw::T3DCreateD3D11Context(device_, context_, device1_, context1_, featureLevel_, hWnd_, adapterNames);
	SetGPUBackend(GPUBackend::DIRECT3D11);
	bool success = draw_->CreatePresets();  // If we can run D3D11, there's a compiler installed. I think.
	assert(success);

	int width;
	int height;
	GetRes(hWnd_, width, height);

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
	if (SUCCEEDED(hr)) {
		hr = dxgiDevice->GetAdapter(&adapter);
		if (SUCCEEDED(hr)) {
			hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);
			adapter->Release();
		}
		dxgiDevice->Release();
	}

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
	dxgiFactory->MakeWindowAssociation(hWnd_, DXGI_MWA_NO_ALT_ENTER);
	dxgiFactory->Release();

	GotBackbuffer();
	return true;
}

void D3D11Context::LostBackbuffer() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, width, height, nullptr);
	bbRenderTargetTex_->Release();
	bbRenderTargetTex_ = nullptr;
	bbRenderTargetView_->Release();
	bbRenderTargetView_ = nullptr;
}

void D3D11Context::GotBackbuffer() {
	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bbRenderTargetTex_));
	if (FAILED(hr))
		return;

	D3D11_TEXTURE2D_DESC bbDesc{};
	bbRenderTargetTex_->GetDesc(&bbDesc);
	width = bbDesc.Width;
	height = bbDesc.Height;

	hr = device_->CreateRenderTargetView(bbRenderTargetTex_, nullptr, &bbRenderTargetView_);
	if (FAILED(hr))
		return;
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, bbRenderTargetView_, bbRenderTargetTex_);
}

void D3D11Context::Resize() {
	LostBackbuffer();
	int width;
	int height;
	GetRes(hWnd_, width, height);
	swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	GotBackbuffer();
}

void D3D11Context::Shutdown() {
	LostBackbuffer();

	delete draw_;
	draw_ = nullptr;

	swapChain_->Release();
	swapChain_ = nullptr;
	if (context1_)
		context1_->Release();
	if (device1_)
		device1_->Release();
	device1_ = nullptr;
	device_->Release();
	device_ = nullptr;

	context_->ClearState();
	context_->Flush();
	context_->Release();
	context_ = nullptr;

#ifdef _DEBUG
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, false);
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, false);
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, false);
	d3dDebug_->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL);
	d3dDebug_->Release();
	d3dInfoQueue_->Release();
#endif

	hWnd_ = nullptr;
	UnloadD3D11();
}
