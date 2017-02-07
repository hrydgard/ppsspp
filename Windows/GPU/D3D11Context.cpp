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
}

void D3D11Context::SwapInterval(int interval) {
	// Dummy
}

bool D3D11Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	bool windowed = true;
	hWnd_ = wnd;
	HRESULT hr = S_OK;

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
	if (FAILED(hr))
		return false;

	draw_ = Draw::T3DCreateD3D11Context(device_, context_, hWnd_);
	return true;
}

void D3D11Context::Resize() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER);
	// This should only be called from the emu thread.
	/*
	int xres, yres;
	GetRes(hWnd, xres, yres);
	bool w_changed = pp.BackBufferWidth != xres;
	bool h_changed = pp.BackBufferHeight != yres;

	if (device && (w_changed || h_changed)) {
		pp.BackBufferWidth = xres;
		pp.BackBufferHeight = yres;
		HRESULT hr = device_->Reset(&pp);
		if (FAILED(hr)) {
      // Had to remove DXGetErrorStringA calls here because dxerr.lib is deprecated and will not link with VS 2015.
			ERROR_LOG_REPORT(G3D, "Unable to reset D3D device");
			PanicAlert("Unable to reset D3D11 device");
		}
	}
	*/
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER);
}

void D3D11Context::Shutdown() {
	delete draw_;
	draw_ = nullptr;

	context_->Release();
	context_ = nullptr;
	device_->Release();
	device_ = nullptr;
	/*
	DX9::DestroyShaders();
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
