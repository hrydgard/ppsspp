#include "thin3d/d3d11_loader.h"

static HMODULE g_DXGIModule;
static HMODULE g_D3D11Module;
static HMODULE g_D3DCompileModule;

LPCREATEDXGIFACTORY ptr_CreateDXGIFactory;
LPD3D11CREATEDEVICE ptr_D3D11CreateDevice;
LPD3D11CREATEDEVICEANDSWAPCHAIN ptr_D3D11CreateDeviceAndSwapChain;
pD3DCompile ptr_D3DCompile;

bool LoadD3D11() {
	if (g_D3D11Module) {
		return true;
	}
	g_D3D11Module = LoadLibrary(L"d3d11.dll");
	if (g_D3D11Module) {
		ptr_D3D11CreateDevice = (LPD3D11CREATEDEVICE)GetProcAddress(g_D3D11Module, "D3D11CreateDevice");
		ptr_D3D11CreateDeviceAndSwapChain = (LPD3D11CREATEDEVICEANDSWAPCHAIN)GetProcAddress(g_D3D11Module, "D3D11CreateDeviceAndSwapChain");
	}
	if (!ptr_CreateDXGIFactory) {
		g_DXGIModule = LoadLibrary(L"dxgi.dll");
		if (g_DXGIModule) {
			ptr_CreateDXGIFactory = (LPCREATEDXGIFACTORY)GetProcAddress(g_DXGIModule, "CreateDXGIFactory1");
		}
	}
	g_D3DCompileModule = LoadLibrary(L"D3dcompiler_47.dll");
	ptr_D3DCompile = (pD3DCompile)GetProcAddress(g_D3DCompileModule, "D3DCompile");

	return g_DXGIModule != nullptr && g_D3D11Module != nullptr && g_D3DCompileModule != nullptr;
}

bool UnloadD3D11() {
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