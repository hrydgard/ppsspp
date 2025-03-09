#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#ifndef __LIBRETRO__  // their build server uses an old SDK
#include <dxgi1_5.h>
#endif
#include <d3d11.h>
#include <WinError.h>
#include <wrl/client.h>

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Windows/GPU/D3D11Context.h"
#include "Windows/W32Util/Misc.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/D3D11/D3D11Loader.h"

#ifndef DXGI_ERROR_NOT_FOUND
#define _FACDXGI    0x87a
#define MAKE_DXGI_HRESULT(code) MAKE_HRESULT(1, _FACDXGI, code)
#define DXGI_ERROR_NOT_FOUND MAKE_DXGI_HRESULT(2)
#endif

#if PPSSPP_PLATFORM(UWP)
#error This file should not be compiled for UWP.
#endif

using Microsoft::WRL::ComPtr;

HRESULT D3D11Context::CreateTheDevice(IDXGIAdapter *adapter) {
	bool windowed = true;
	// D3D11 has no need for display rotation.
	g_display.rotation = DisplayRotation::ROTATE_0;
	g_display.rot_matrix.setIdentity();
#if defined(_DEBUG) && !PPSSPP_ARCH(ARM) && !PPSSPP_ARCH(ARM64)
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
	UINT createDeviceFlags = 0;
#endif

	static const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
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

bool D3D11Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	hWnd_ = wnd;
	LoadD3D11Error result = LoadD3D11();

	HRESULT hr = E_FAIL;
	std::vector<std::string> adapterNames;
	std::string chosenAdapterName;
	if (result == LoadD3D11Error::SUCCESS) {
		std::vector<ComPtr<IDXGIAdapter>> adapters;
		int chosenAdapter = 0;
		ComPtr<IDXGIFactory> pFactory;

		hr = ptr_CreateDXGIFactory(IID_PPV_ARGS(&pFactory));
		if (SUCCEEDED(hr)) {
			ComPtr<IDXGIAdapter> pAdapter;
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
			if (!adapters.empty()) {
				chosenAdapterName = adapterNames[chosenAdapter];
				hr = CreateTheDevice(adapters[chosenAdapter].Get());
				adapters.clear();
			} else {
				// No adapters found. Trip the error path below.
				hr = E_FAIL;
			}
		}
	}

	if (FAILED(hr)) {
		const char *defaultError = "Your GPU does not appear to support Direct3D 11.\n\nWould you like to try again using OpenGL instead?";
		auto err = GetI18NCategory(I18NCat::ERRORS);

		std::wstring error;

		if (result == LoadD3D11Error::FAIL_NO_COMPILER) {
			error = ConvertUTF8ToWString(err->T("D3D11CompilerMissing", "D3DCompiler_47.dll not found. Please install. Or press Yes to try again using OpenGL instead."));
		} else if (result == LoadD3D11Error::FAIL_NO_D3D11) {
			error = ConvertUTF8ToWString(err->T("D3D11Missing", "Your operating system version does not include D3D11. Please run Windows Update.\n\nPress Yes to try again using Direct3D9 instead."));
		}

		error = ConvertUTF8ToWString(err->T("D3D11NotSupported", defaultError));
		std::wstring title = ConvertUTF8ToWString(err->T("D3D11InitializationError", "Direct3D 11 initialization error"));
		bool yes = IDYES == MessageBox(hWnd_, error.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);
		if (yes) {
			// Change the config to OpenGL and restart.
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			g_Config.sFailedGPUBackends.clear();
			g_Config.Save("save_d3d9_fallback");

			W32Util::ExitAndRestart();
		}
		return false;
	}

	if (FAILED(device_.As(&device1_))) {
		device1_ = nullptr;
	}

	if (FAILED(context_.As(&context1_))) {
		context1_ = nullptr;
	}

#ifdef _DEBUG
	if (SUCCEEDED(device_.As(&d3dDebug_))) {
		if (SUCCEEDED(d3dDebug_.As(&d3dInfoQueue_))) {
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
			d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
		}
	}
#endif


	int width;
	int height;
	W32Util::GetWindowRes(hWnd_, &width, &height);

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	ComPtr<IDXGIFactory1> dxgiFactory;
	ComPtr<IDXGIDevice> dxgiDevice;
	hr = device_.As(&dxgiDevice);
	if (SUCCEEDED(hr)) {
		ComPtr<IDXGIAdapter> adapter;
		hr = dxgiDevice->GetAdapter(&adapter);
		if (SUCCEEDED(hr)) {
			hr = adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
		}
	}

	// Create the swap chain. Modern features (flip model, tearing) require Windows 10 (DXGI 1.5) or newer.
	swapChainDesc_.BufferCount = 1;
	swapChainDesc_.BufferDesc.Width = width;
	swapChainDesc_.BufferDesc.Height = height;
	swapChainDesc_.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc_.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc_.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc_.OutputWindow = hWnd_;
	swapChainDesc_.SampleDesc.Count = 1;
	swapChainDesc_.SampleDesc.Quality = 0;
	swapChainDesc_.Windowed = TRUE;
	swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

#ifndef __LIBRETRO__  // their build server uses an old SDK
	ComPtr<IDXGIFactory5> dxgiFactory5;
	hr = dxgiFactory.As(&dxgiFactory5);
	if (SUCCEEDED(hr)) {
		swapChainDesc_.BufferCount = 2;
		swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		BOOL allowTearing = FALSE;
		hr = dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
		if (SUCCEEDED(hr) && allowTearing) {
			swapChainDesc_.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		}
	}
#endif

	hr = dxgiFactory->CreateSwapChain(device_.Get(), &swapChainDesc_, &swapChain_);
	dxgiFactory->MakeWindowAssociation(hWnd_, DXGI_MWA_NO_ALT_ENTER);

	draw_ = Draw::T3DCreateD3D11Context(device_.Get(), context_.Get(), device1_.Get(), context1_.Get(), swapChain_.Get(), featureLevel_, hWnd_, adapterNames, g_Config.iInflightFrames);
	SetGPUBackend(GPUBackend::DIRECT3D11, chosenAdapterName);
	bool success = draw_->CreatePresets();  // If we can run D3D11, there's a compiler installed. I think.
	_assert_msg_(success, "Failed to compile preset shaders");

	GotBackbuffer();
	return true;
}

void D3D11Context::LostBackbuffer() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, width, height, nullptr);
	bbRenderTargetTex_.Reset();
	bbRenderTargetView_.Reset();
}

void D3D11Context::GotBackbuffer() {
	// Create a render target view
	HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&bbRenderTargetTex_));
	if (FAILED(hr))
		return;

	D3D11_TEXTURE2D_DESC bbDesc{};
	bbRenderTargetTex_->GetDesc(&bbDesc);
	width = bbDesc.Width;
	height = bbDesc.Height;

	hr = device_->CreateRenderTargetView(bbRenderTargetTex_.Get(), nullptr, &bbRenderTargetView_);
	if (FAILED(hr))
		return;
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height, bbRenderTargetView_.Get(), bbRenderTargetTex_.Get());
}

void D3D11Context::Resize() {
	LostBackbuffer();
	int width;
	int height;
	W32Util::GetWindowRes(hWnd_, &width, &height);
	swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapChainDesc_.Flags);
	GotBackbuffer();
}

void D3D11Context::Shutdown() {
	LostBackbuffer();

	delete draw_;
	draw_ = nullptr;

	context_->ClearState();
	context_->Flush();
#ifdef _DEBUG
	if (d3dInfoQueue_) {
		d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, false);
		d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, false);
		d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, false);
	}
	if (d3dDebug_) {
		d3dDebug_->ReportLiveDeviceObjects(D3D11_RLDO_FLAGS(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL));
	}
#endif

	hWnd_ = nullptr;
	UnloadD3D11();
}
