#include "ppsspp_config.h"
#include "Common/GPU/D3D11/D3D11Loader.h"

#if PPSSPP_PLATFORM(UWP)
#error This file should not be compiled for UWP.
#endif

static HMODULE g_DXGIModule;
static HMODULE g_D3D11Module;
static HMODULE g_D3DCompileModule;

LPCREATEDXGIFACTORY ptr_CreateDXGIFactory;
LPD3D11CREATEDEVICE ptr_D3D11CreateDevice;
pD3DCompile ptr_D3DCompile;

LoadD3D11Error LoadD3D11() {
	if (g_D3D11Module) {
		// Already done
		return LoadD3D11Error::SUCCESS;
	}
	g_D3D11Module = LoadLibrary(L"d3d11.dll");
	if (g_D3D11Module) {
		ptr_D3D11CreateDevice = (LPD3D11CREATEDEVICE)GetProcAddress(g_D3D11Module, "D3D11CreateDevice");
	} else {
		return LoadD3D11Error::FAIL_NO_D3D11;
	}

	g_DXGIModule = LoadLibrary(L"dxgi.dll");
	if (g_DXGIModule) {
		ptr_CreateDXGIFactory = (LPCREATEDXGIFACTORY)GetProcAddress(g_DXGIModule, "CreateDXGIFactory1");
	} else {
		FreeLibrary(g_D3D11Module);
		g_D3D11Module = nullptr;
		return LoadD3D11Error::FAIL_NO_D3D11;
	}

	g_D3DCompileModule = LoadLibrary(D3DCOMPILER_DLL);
#if PPSSPP_ARCH(X86)
	// Workaround for distributing both 32-bit and 64-bit versions of the DLL.
	if (!g_D3DCompileModule)
		g_D3DCompileModule = LoadLibrary(L"D3dcompiler_47.x86.dll");
#endif
	if (!g_D3DCompileModule)
		g_D3DCompileModule = LoadLibrary(L"D3dcompiler_42.dll");

	if (!g_D3DCompileModule) {
		FreeLibrary(g_D3D11Module);
		g_D3D11Module = nullptr;
		FreeLibrary(g_DXGIModule);
		g_DXGIModule = nullptr;
		return LoadD3D11Error::FAIL_NO_COMPILER;
	}
	ptr_D3DCompile = (pD3DCompile)GetProcAddress(g_D3DCompileModule, "D3DCompile");

	return LoadD3D11Error::SUCCESS;
}

bool UnloadD3D11() {
	if (!g_D3D11Module)
		return false;

	if (g_DXGIModule) {
		FreeLibrary(g_DXGIModule);
		g_DXGIModule = nullptr;
	}
	if (g_D3D11Module) {
		FreeLibrary(g_D3D11Module);
		g_D3D11Module = nullptr;
	}
	if (g_D3DCompileModule) {
		FreeLibrary(g_D3DCompileModule);
		g_D3DCompileModule = nullptr;
	}

	return true;
}
