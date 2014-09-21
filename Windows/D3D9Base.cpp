#include "Common/CommonWindows.h"
#include <d3d9.h>

#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/helper/fbo.h"

#include "base/logging.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Core/Config.h"
#include "Windows/D3D9Base.h"
#include "thin3d/thin3d.h"
#include "thin3d/d3dx9_loader.h"

static bool has9Ex = false;
static LPDIRECT3D9 d3d;
static LPDIRECT3D9EX d3dEx;
static int adapterId;
static LPDIRECT3DDEVICE9 device;
static LPDIRECT3DDEVICE9EX deviceEx;
static HDC hDC;     // Private GDI Device Context
static HGLRC hRC;   // Permanent Rendering Context
static HWND hWnd;   // Holds Our Window Handle

static int xres, yres;

// TODO: Make config?
static bool enableGLDebug = true;

void D3D9_SwapBuffers() {
	if (has9Ex) {
		deviceEx->EndScene();
		deviceEx->PresentEx(NULL, NULL, NULL, NULL, 0);
		deviceEx->BeginScene();
	} else {
		device->EndScene();
		device->Present(NULL, NULL, NULL, NULL);
		device->BeginScene();
	}
}

Thin3DContext *D3D9_CreateThin3DContext() {
	return T3DCreateDX9Context(d3d, d3dEx, adapterId, device, deviceEx);
}

typedef HRESULT (*DIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);

bool IsWin7OrLater() {
	DWORD version = GetVersion();
	DWORD major = (DWORD)(LOBYTE(LOWORD(version)));
	DWORD minor = (DWORD)(HIBYTE(LOWORD(version)));

	return (major > 6) || ((major == 6) && (minor >= 1));
}

bool D3D9_Init(HWND hWnd, bool windowed, std::string *error_message) {
	DIRECT3DCREATE9EX g_pfnCreate9ex;

	HMODULE hD3D9 = LoadLibrary(TEXT("d3d9.dll"));

	if (!hD3D9) {
		ELOG("Missing d3d9.dll");
		*error_message = "D3D9.dll missing";
		return false;
	}

	g_pfnCreate9ex = (DIRECT3DCREATE9EX)GetProcAddress(hD3D9, "Direct3DCreate9Ex");
	has9Ex = (g_pfnCreate9ex != NULL);

	if (has9Ex) {
		HRESULT result = g_pfnCreate9ex(D3D_SDK_VERSION, &d3dEx);
		d3d = d3dEx;
		if (FAILED(result)) {
			*error_message = "D3D9Ex available but context creation failed";
			return false;
		}
	} else {
		d3d = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d) {
			*error_message = "Failed to create D3D9 context";
			return false;
		}
	}
	FreeLibrary(hD3D9);

	D3DCAPS9 d3dCaps;

	D3DDISPLAYMODE d3ddm;
	if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) {
		*error_message = "GetAdapterDisplayMode failed";
		d3d->Release();
		return false;
	}

	adapterId = D3DADAPTER_DEFAULT;
	if (FAILED(d3d->GetDeviceCaps(adapterId, D3DDEVTYPE_HAL, &d3dCaps))) {
		*error_message = "GetDeviceCaps failed (???)";
		d3d->Release();
		return false;
	}

	HRESULT hr;
	if (FAILED(hr = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL,
		d3ddm.Format,
		D3DUSAGE_DEPTHSTENCIL,
		D3DRTYPE_SURFACE,
		D3DFMT_D24S8))) {
		if (hr == D3DERR_NOTAVAILABLE) {
			*error_message = "D24S8 depth/stencil not available";
			d3d->Release();
			return false;
		}
	}

	DWORD dwBehaviorFlags = D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE;
	if (d3dCaps.VertexProcessingCaps != 0)
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	RECT rc;
	GetClientRect(hWnd, &rc);
	int xres = rc.right - rc.left;
	int yres = rc.bottom - rc.top;

	D3DPRESENT_PARAMETERS pp;
	memset(&pp, 0, sizeof(pp));
	pp.BackBufferWidth = xres;
	pp.BackBufferHeight = yres;
	pp.BackBufferFormat = d3ddm.Format;
	pp.MultiSampleType = D3DMULTISAMPLE_NONE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.Windowed = windowed;
	pp.hDeviceWindow = hWnd;
	pp.EnableAutoDepthStencil = true;
	pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	pp.PresentationInterval = (g_Config.bVSync) ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

	if (has9Ex) {
		if (windowed && IsWin7OrLater()) {
			// This new flip mode gives higher performance.
			// TODO: This makes it slower?
			//pp.BackBufferCount = 2;
			//pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
		}
		hr = d3dEx->CreateDeviceEx(adapterId, D3DDEVTYPE_HAL, hWnd, dwBehaviorFlags, &pp, NULL, &deviceEx);
		device = deviceEx;
	} else {
		hr = d3d->CreateDevice(adapterId, D3DDEVTYPE_HAL, hWnd, dwBehaviorFlags, &pp, &device);
	}

	if (FAILED(hr)) {
		*error_message = "Failed to create D3D device";
		if (has9Ex) {
			d3dEx->Release();
		} else {
			d3d->Release();
		}
		return false;
	}

	device->BeginScene();
	DX9::pD3Ddevice = device;
	DX9::pD3DdeviceEx = deviceEx;

	LoadD3DX9Dynamic();

	DX9::CompileShaders();
	DX9::fbo_init(d3d);

	if (deviceEx && IsWin7OrLater()) {
		// TODO: This makes it slower?
		//deviceEx->SetMaximumFrameLatency(1);
	}

	return true;
}

void D3D9_Resize(HWND window) {
	// TODO!
}

void D3D9_Shutdown() { 
	DX9::DestroyShaders();
	DX9::fbo_shutdown();
	device->EndScene();
	device->Release();
	d3d->Release();
	DX9::pD3Ddevice = nullptr;
	DX9::pD3DdeviceEx = nullptr;
	device = nullptr;
	hWnd = nullptr;
}
