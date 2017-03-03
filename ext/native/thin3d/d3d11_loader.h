#pragma once

#include "ppsspp_config.h"

// Standard Windows includes
#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d11.h>
#include <D3Dcompiler.h>

#if PPSSPP_PLATFORM(UWP)
#error This file should not be compiled for UWP.
#endif

typedef HRESULT (WINAPI *LPCREATEDXGIFACTORY)(REFIID, void **);
typedef HRESULT (WINAPI *LPD3D11CREATEDEVICEANDSWAPCHAIN)(__in_opt IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, __in_ecount_opt(FeatureLevels) CONST D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, __in_opt CONST DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, __out_opt IDXGISwapChain **ppSwapChain, __out_opt ID3D11Device **ppDevice, __out_opt D3D_FEATURE_LEVEL *pFeatureLevel, __out_opt ID3D11DeviceContext **ppImmediateContext);
typedef HRESULT (WINAPI *LPD3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT32, D3D_FEATURE_LEVEL *, UINT, UINT32, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

extern LPCREATEDXGIFACTORY ptr_CreateDXGIFactory;
extern LPD3D11CREATEDEVICE ptr_D3D11CreateDevice;
extern LPD3D11CREATEDEVICEANDSWAPCHAIN ptr_D3D11CreateDeviceAndSwapChain;
extern pD3DCompile ptr_D3DCompile;

enum class LoadD3D11Error {
	SUCCESS,
	FAIL_NO_D3D11,
	FAIL_NO_COMPILER,
};

LoadD3D11Error LoadD3D11();
bool UnloadD3D11();
