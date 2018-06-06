#pragma once

#include <string>
#include <vector>
#include "thin3d/thin3d.h"

// Separated this stuff into its own file so we don't get Windows.h included if all we want is the thin3d declarations.

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3dcommon.h>
struct IDirect3DDevice9;
struct IDirect3D9;
struct IDirect3DDevice9Ex;
struct IDirect3D9Ex;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Device1;
struct ID3D11DeviceContext1;

#endif

class VulkanContext;

namespace Draw {

DrawContext *T3DCreateGLContext();

#ifdef _WIN32
DrawContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx);
DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Device1 *device1, ID3D11DeviceContext1 *context1, D3D_FEATURE_LEVEL featureLevel, HWND hWnd, std::vector<std::string> adapterNames);
#endif

DrawContext *T3DCreateVulkanContext(VulkanContext *context, bool splitSubmit);

}  // namespace Draw
