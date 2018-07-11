#include "Common/CommonWindows.h"
#include <d3d9.h>

#include "gfx/d3d9_state.h"

#include "base/logging.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Common/OSVersion.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/W32Util/Misc.h"
#include "thin3d/thin3d.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/d3dx9_loader.h"

void D3D9Context::SwapBuffers() {
	if (has9Ex_) {
		deviceEx_->EndScene();
		deviceEx_->PresentEx(NULL, NULL, NULL, NULL, 0);
		deviceEx_->BeginScene();
	} else {
		device_->EndScene();
		device_->Present(NULL, NULL, NULL, NULL);
		device_->BeginScene();
	}
}

typedef HRESULT (__stdcall *DIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);

bool IsWin7OrLater() {
	DWORD version = GetVersion();
	DWORD major = (DWORD)(LOBYTE(LOWORD(version)));
	DWORD minor = (DWORD)(HIBYTE(LOWORD(version)));

	return (major > 6) || ((major == 6) && (minor >= 1));
}

static void GetRes(HWND hWnd, int &xres, int &yres) {
	RECT rc;
	GetClientRect(hWnd, &rc);
	xres = rc.right - rc.left;
	yres = rc.bottom - rc.top;
}

void D3D9Context::SwapInterval(int interval) {
	// Dummy
}

bool D3D9Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	bool windowed = true;
	hWnd_ = wnd;

	DIRECT3DCREATE9EX g_pfnCreate9ex;

	hD3D9_ = LoadLibrary(TEXT("d3d9.dll"));
	if (!hD3D9_) {
		ELOG("Missing d3d9.dll");
		*error_message = "D3D9.dll missing - try reinstalling DirectX.";
		return false;
	}

	int d3dx_version = LoadD3DX9Dynamic();
	if (!d3dx_version) {
		*error_message = "D3DX DLL not found! Try reinstalling DirectX.";
		return false;
	}

	g_pfnCreate9ex = (DIRECT3DCREATE9EX)GetProcAddress(hD3D9_, "Direct3DCreate9Ex");
	has9Ex_ = (g_pfnCreate9ex != NULL) && IsVistaOrHigher();

	if (has9Ex_) {
		HRESULT result = g_pfnCreate9ex(D3D_SDK_VERSION, &d3dEx_);
		d3d_ = d3dEx_;
		if (FAILED(result)) {
			FreeLibrary(hD3D9_);
			*error_message = "D3D9Ex available but context creation failed. Try reinstalling DirectX.";
			return false;
		}
	} else {
		d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d_) {
			FreeLibrary(hD3D9_);
			*error_message = "Failed to create D3D9 context. Try reinstalling DirectX.";
			return false;
		}
	}
	adapterId_ = D3DADAPTER_DEFAULT;

	D3DCAPS9 d3dCaps;

	D3DDISPLAYMODE d3ddm;
	if (FAILED(d3d_->GetAdapterDisplayMode(adapterId_, &d3ddm))) {
		*error_message = "GetAdapterDisplayMode failed";
		d3d_->Release();
		return false;
	}

	if (FAILED(d3d_->GetDeviceCaps(adapterId_, D3DDEVTYPE_HAL, &d3dCaps))) {
		*error_message = "GetDeviceCaps failed (???)";
		d3d_->Release();
		return false;
	}

	HRESULT hr;
	if (FAILED(hr = d3d_->CheckDeviceFormat(adapterId_,
		D3DDEVTYPE_HAL,
		d3ddm.Format,
		D3DUSAGE_DEPTHSTENCIL,
		D3DRTYPE_SURFACE,
		D3DFMT_D24S8))) {
		if (hr == D3DERR_NOTAVAILABLE) {
			*error_message = "D24S8 depth/stencil not available";
			d3d_->Release();
			return false;
		}
	}

	DWORD dwBehaviorFlags = D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE;
	if (d3dCaps.VertexProcessingCaps != 0)
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	int xres, yres;
	GetRes(hWnd_, xres, yres);

	presentParams_ = {};
	presentParams_.BackBufferWidth = xres;
	presentParams_.BackBufferHeight = yres;
	presentParams_.BackBufferFormat = d3ddm.Format;
	presentParams_.MultiSampleType = D3DMULTISAMPLE_NONE;
	presentParams_.SwapEffect = D3DSWAPEFFECT_DISCARD;
	presentParams_.Windowed = windowed;
	presentParams_.hDeviceWindow = wnd;
	presentParams_.EnableAutoDepthStencil = true;
	presentParams_.AutoDepthStencilFormat = D3DFMT_D24S8;
	presentParams_.PresentationInterval = (g_Config.bVSync) ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	if (has9Ex_) {
		if (windowed && IsWin7OrLater()) {
			// This "new" flip mode should give higher performance but doesn't.
			//pp.BackBufferCount = 2;
			//pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
		}
		hr = d3dEx_->CreateDeviceEx(adapterId_, D3DDEVTYPE_HAL, wnd, dwBehaviorFlags, &presentParams_, NULL, &deviceEx_);
		device_ = deviceEx_;
	} else {
		hr = d3d_->CreateDevice(adapterId_, D3DDEVTYPE_HAL, wnd, dwBehaviorFlags, &presentParams_, &device_);
	}

	if (FAILED(hr)) {
		*error_message = "Failed to create D3D device";
		d3d_->Release();
		return false;
	}

	device_->BeginScene();
	DX9::pD3Ddevice = device_;
	DX9::pD3DdeviceEx = deviceEx_;

	if (deviceEx_ && IsWin7OrLater()) {
		// TODO: This makes it slower?
		//deviceEx->SetMaximumFrameLatency(1);
	}
	draw_ = Draw::T3DCreateDX9Context(d3d_, d3dEx_, adapterId_, device_, deviceEx_);
	SetGPUBackend(GPUBackend::DIRECT3D9);
	if (!draw_->CreatePresets()) {
		// Shader compiler not installed? Return an error so we can fall back to GL.
		device_->Release();
		d3d_->Release();
		*error_message = "DirectX9 runtime not correctly installed. Please install.";
		return false;
	}
	if (draw_)
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, 0, 0, nullptr);
	return true;
}

void D3D9Context::Resize() {
	// This should only be called from the emu thread.
	int xres, yres;
	GetRes(hWnd_, xres, yres);
	bool w_changed = presentParams_.BackBufferWidth != xres;
	bool h_changed = presentParams_.BackBufferHeight != yres;

	if (device_ && (w_changed || h_changed)) {
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
		presentParams_.BackBufferWidth = xres;
		presentParams_.BackBufferHeight = yres;
		HRESULT hr = device_->Reset(&presentParams_);
		if (FAILED(hr)) {
      // Had to remove DXGetErrorStringA calls here because dxerr.lib is deprecated and will not link with VS 2015.
			PanicAlert("Unable to reset D3D9 device");
		}
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, 0, 0, nullptr);
	}
}

void D3D9Context::Shutdown() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
	delete draw_;
	draw_ = nullptr;
	device_->EndScene();
	device_->Release();
	d3d_->Release();
	UnloadD3DXDynamic();
	DX9::pD3Ddevice = nullptr;
	DX9::pD3DdeviceEx = nullptr;
	device_ = nullptr;
	hWnd_ = nullptr;
	FreeLibrary(hD3D9_);
	hD3D9_ = nullptr;
}
