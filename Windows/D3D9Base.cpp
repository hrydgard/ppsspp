#include "Common/CommonWindows.h"
#include <d3d9.h>

#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/helper/fbo.h"

#include "base/logging.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Windows/D3D9Base.h"
#include "thin3d/thin3d.h"
#include "thin3d/d3dx9_loader.h"

static LPDIRECT3D9 d3d;
static LPDIRECT3DDEVICE9 device;
static HDC hDC;     // Private GDI Device Context
static HGLRC hRC;   // Permanent Rendering Context
static HWND hWnd;   // Holds Our Window Handle

static int xres, yres;

// TODO: Make config?
static bool enableGLDebug = true;

void D3D9_SwapBuffers() {
	device->EndScene();
	device->Present(NULL, NULL, NULL, NULL);
	device->BeginScene();
}

Thin3DContext *D3D9_CreateThin3DContext() {
	return T3DCreateDX9Context(device);
}

bool D3D9_Init(HWND hWnd, bool windowed, std::string *error_message) {
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		ELOG("Failed to create D3D context");
		return false;
	}

	RECT rc;
	GetClientRect(hWnd, &rc);
	int xres = rc.right - rc.left;
	int yres = rc.bottom - rc.top;
	
	D3DCAPS9 d3dCaps;

	D3DDISPLAYMODE d3ddm;
	if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) {
		d3d->Release();
		return false;
	}

	int adapter = D3DADAPTER_DEFAULT;
	if (FAILED(d3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &d3dCaps))) {
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
			d3d->Release();
			return false;
		}
	}

	DWORD dwBehaviorFlags = D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE;
	if (d3dCaps.VertexProcessingCaps != 0)
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

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
	pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	hr = d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hWnd, dwBehaviorFlags, &pp, &device);
	if (FAILED(hr)) {
		ELOG("Failed to create D3D device");
		d3d->Release();
		return false;
	}

	device->BeginScene();
	DX9::pD3Ddevice = device;

	LoadD3DX9Dynamic();

	DX9::CompileShaders();
	DX9::fbo_init();

	return true;
}

void D3D9_Resize(HWND window) {

}

void D3D9_Shutdown() { 
	device->EndScene();
	device->Release();
	d3d->Release();
	hWnd = NULL;
}
