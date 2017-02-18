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
#include "thin3d/d3d11_loader.h"

D3D11Context::D3D11Context() : draw_(nullptr), adapterId(-1), hDC(nullptr), hWnd_(nullptr), hD3D11(nullptr) {
	LoadD3D11();
}

D3D11Context::~D3D11Context() {
	UnloadD3D11();
}

void D3D11Context::SwapBuffers() {
	draw_->HandleEvent(Draw::Event::PRESENT_REQUESTED);
	// Might be a good idea.
	// context_->ClearState();
	//
}

void D3D11Context::SwapInterval(int interval) {
	// Dummy
}

bool D3D11Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	bool windowed = true;
	hWnd_ = wnd;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	static const D3D_DRIVER_TYPE driverTypes[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	static const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	HRESULT hr = S_OK;
	// Temporarily commenting out until we can dynamically load D3D11CreateDevice.
	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {
		driverType_ = driverTypes[driverTypeIndex];
		hr = ptr_D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, (D3D_FEATURE_LEVEL *)featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);

		if (hr == E_INVALIDARG) {
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = ptr_D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, (D3D_FEATURE_LEVEL *)&featureLevels[1], numFeatureLevels - 1,
				D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
		}
		if (SUCCEEDED(hr))
			break;
	}

	if (FAILED(hr)) {
		const char *defaultError = "Your GPU does not appear to support Direct3D 11.\n\nWould you like to try again using Direct3D 9 instead?";
		I18NCategory *err = GetI18NCategory("Error");

		std::wstring error = ConvertUTF8ToWString(err->T("D3D11NotSupported", defaultError));
		std::wstring title = ConvertUTF8ToWString(err->T("D3D11InitializationError", "Direct3D 11 initialization error"));
		bool yes = IDYES == MessageBox(hWnd_, error.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);
		if (yes) {
			// Change the config to D3D and restart.
			g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D9;
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

	draw_ = Draw::T3DCreateD3D11Context(device_, context_, device1_, context1_, hWnd_);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER);
	return true;
}

void D3D11Context::Resize() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER);
	draw_->HandleEvent(Draw::Event::RESIZED);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER);
}

void D3D11Context::Shutdown() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER);
	delete draw_;
	draw_ = nullptr;
	context_->ClearState();
	context_->Flush();

#ifdef _DEBUG
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, false);
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, false);
	d3dInfoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, false);
	d3dDebug_->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL);
	d3dDebug_->Release();
	d3dInfoQueue_->Release();
#endif

	if (context1_)
		context1_->Release();
	context_->Release();
	context_ = nullptr;
	if (device1_)
		device1_->Release();
	device1_ = nullptr;
	device_->Release();
	device_ = nullptr;
	hWnd_ = nullptr;
}
