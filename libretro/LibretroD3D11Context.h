#pragma once

#define HAVE_D3D11
#include "libretro/libretro_d3d.h"
#include "libretro/LibretroGraphicsContext.h"

class LibretroD3D11Context : public LibretroHWRenderContext {
public:
   LibretroD3D11Context() : LibretroHWRenderContext(RETRO_HW_CONTEXT_DIRECT3D, 11) {}
   bool Init() override;

   void SwapBuffers() override;
   void GotBackbuffer() override;
   void LostBackbuffer() override;
   void CreateDrawContext() override;
   void DestroyDrawContext() override;

   GPUCore GetGPUCore() override { return GPUCORE_DIRECTX11; }
   const char *Ident() override { return "DirectX 11"; }

private:
   retro_hw_render_interface_d3d11 hwInterface_ = {};
   ID3D11Device1 *device1_ = nullptr;
   ID3D11DeviceContext1 *context1_ = nullptr;
   ID3D11Texture2D *texture_ = nullptr;
   ID3D11RenderTargetView *texture_rt_view_ = nullptr;
   ID3D11ShaderResourceView *texture_sr_view_ = nullptr;
   DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
};
