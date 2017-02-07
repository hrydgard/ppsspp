#pragma once

// Standard Windows includes
#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d11.h>
#include <D3Dcompiler.h>

typedef HRESULT(WINAPI *LPCREATEDXGIFACTORY)(REFIID, void **);
typedef HRESULT(WINAPI *LPD3D11CREATEDEVICEANDSWAPCHAIN)(__in_opt IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, __in_ecount_opt(FeatureLevels) CONST D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, __in_opt CONST DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, __out_opt IDXGISwapChain **ppSwapChain, __out_opt ID3D11Device **ppDevice, __out_opt D3D_FEATURE_LEVEL *pFeatureLevel, __out_opt ID3D11DeviceContext **ppImmediateContext);
typedef HRESULT(WINAPI *LPD3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT32, D3D_FEATURE_LEVEL *, UINT, UINT32, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
// typedef HRESULT(WINAPI *LPD3DCOMPILE)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName, const D3D_SHADER_MACRO pDefines, ID3DInclude *pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob *ppCode, ID3DBlob *ppErrorMsgs);

extern LPCREATEDXGIFACTORY ptr_CreateDXGIFactory;
extern LPD3D11CREATEDEVICE ptr_D3D11CreateDevice;
extern LPD3D11CREATEDEVICEANDSWAPCHAIN ptr_D3D11CreateDeviceAndSwapChain;
extern pD3DCompile ptr_D3DCompile;

bool LoadD3D11();
bool UnloadD3D11();