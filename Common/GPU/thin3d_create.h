#pragma once

#include <string>
#include <vector>
#include "Common/GPU/thin3d.h"

// Separated this stuff into its own file so we don't get Windows.h included if all we want is the thin3d declarations.

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <D3Dcommon.h>
struct IDirect3DDevice9;
struct IDirect3D9;
struct IDirect3DDevice9Ex;
struct IDirect3D9Ex;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Device1;
struct ID3D11DeviceContext1;
struct IDXGISwapChain;
#endif

class VulkanContext;

namespace Draw {

DrawContext *T3DCreateGLContext(bool canChangeSwapInterval);

#ifdef _WIN32
DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Device1 *device1, ID3D11DeviceContext1 *context1, IDXGISwapChain *swapChain, D3D_FEATURE_LEVEL featureLevel, HWND hWnd, const std::vector<std::string> &adapterNames, int maxInflightFrames);
#endif

DrawContext *T3DCreateVulkanContext(VulkanContext *context, bool useRenderThread);

}  // namespace Draw
