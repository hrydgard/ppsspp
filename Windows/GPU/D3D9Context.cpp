#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include <d3d9.h>

#include "Common/GPU/D3D9/D3D9StateCache.h"

#include "Common/System/Display.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Log.h"
#include "Common/OSVersion.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/OSVersion.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/W32Util/Misc.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/D3D9/D3DCompilerLoader.h"

typedef HRESULT (__stdcall *DIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);

bool D3D9Context::Init(HINSTANCE hInst, HWND wnd, std::string *error_message) {
	bool windowed = true;
	hWnd_ = wnd;

	// D3D9 has no need for display rotation.
	g_display.rotation = DisplayRotation::ROTATE_0;
	g_display.rot_matrix.setIdentity();

	DIRECT3DCREATE9EX g_pfnCreate9ex;

	hD3D9_ = LoadLibrary(TEXT("d3d9.dll"));
	if (!hD3D9_) {
		ERROR_LOG(Log::G3D, "Missing d3d9.dll");
		*error_message = "D3D9.dll missing - try reinstalling DirectX.";
		return false;
	}

	bool result = LoadD3DCompilerDynamic();
	if (!result) {
		*error_message = "D3DCompiler not found! Try reinstalling DirectX.";
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
		d3d_ = nullptr;
		return false;
	}

	if (FAILED(d3d_->GetDeviceCaps(adapterId_, D3DDEVTYPE_HAL, &d3dCaps))) {
		*error_message = "GetDeviceCaps failed (?)";
		d3d_ = nullptr;
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
			d3d_ = nullptr;
			return false;
		}
	}

	DWORD dwBehaviorFlags = D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE;
	if (d3dCaps.VertexProcessingCaps != 0)
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	int xres, yres;
	W32Util::GetWindowRes(hWnd_, &xres, &yres);

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
	presentParams_.PresentationInterval = swapInterval_ == 1 ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	if (has9Ex_) {
		if (windowed && IsWin7OrHigher()) {
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
		d3d_ = nullptr;
		return false;
	}

	device_->BeginScene();
	pD3Ddevice9 = device_;
	pD3DdeviceEx9 = deviceEx_;

	if (deviceEx_ && IsWin7OrHigher()) {
		// TODO: This makes it slower?
		//deviceEx->SetMaximumFrameLatency(1);
	}
	draw_ = Draw::T3DCreateDX9Context(d3d_.Get(), d3dEx_.Get(), adapterId_, device_.Get(), deviceEx_.Get());
	SetGPUBackend(GPUBackend::DIRECT3D9);
	if (!draw_->CreatePresets()) {
		// Shader compiler not installed? Return an error so we can fall back to GL.
		device_ = nullptr;
		d3d_ = nullptr;
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
	W32Util::GetWindowRes(hWnd_, &xres, &yres);
	uint32_t newInterval = swapInterval_ == 1 ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
	bool w_changed = presentParams_.BackBufferWidth != xres;
	bool h_changed = presentParams_.BackBufferHeight != yres;
	bool i_changed = presentParams_.PresentationInterval != newInterval;

	if (device_ && (w_changed || h_changed || i_changed)) {
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
		presentParams_.BackBufferWidth = xres;
		presentParams_.BackBufferHeight = yres;
		presentParams_.PresentationInterval = newInterval;
		HRESULT hr = device_->Reset(&presentParams_);
		if (FAILED(hr)) {
			// Had to remove DXGetErrorStringA calls here because dxerr.lib is deprecated and will not link with VS 2015.
			_assert_msg_(false, "Unable to reset D3D9 device");
		}
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, 0, 0, nullptr);
	}
}

void D3D9Context::Shutdown() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0, nullptr);
	delete draw_;
	draw_ = nullptr;
	device_->EndScene();
	device_ = nullptr;
	d3d_ = nullptr;
	UnloadD3DCompiler();
	pD3Ddevice9 = nullptr;
	pD3DdeviceEx9 = nullptr;
	device_ = nullptr;
	hWnd_ = nullptr;
	FreeLibrary(hD3D9_);
	hD3D9_ = nullptr;
}
