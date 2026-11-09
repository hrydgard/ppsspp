#pragma once

#define HAVE_D3D11
#include <libretro_d3d.h>
#include "libretro/LibretroGraphicsContext.h"

class LibretroD3D11Context : public LibretroHWRenderContext {
public:
   LibretroD3D11Context() : LibretroHWRenderContext(RETRO_HW_CONTEXT_D3D11, 11) {}
   ~LibretroD3D11Context() override;
   bool InitAPI(void *wnd, std::string *deviceName, std::string *error_message) override;

   void SwapBuffers() override;
   void GotBackbuffer() override;
   void LostBackbuffer() override;
   void CreateDrawContext() override;
   void DestroyDrawContext() override;
   void ContextReset() override;
   void ContextDestroy() override;

   void LockContext() override;
   void UnlockContext() override;
   void BeginFrameAccess() override;
   void EndFrameAccess() override;

   GPUCore GetGPUCore() override { return GPUCORE_DIRECTX11; }
   const char *Ident() override { return "DirectX 11"; }

private:
   bool FetchInterface();
   // Version 2: the frontend reads the frame from a texture of ours, one per sync index.
   bool IsVersion2() const { return hwInterface_.lock_context != nullptr; }
   ID3D11Texture2D *GetPresentTexture(unsigned index);
   void ReleasePresentTextures();

   retro_hw_render_interface_d3d11 hwInterface_ = {};
   ID3D11Texture2D *presentTextures_[32] = {};
   bool frameLocked_ = false;
   ID3D11Device1 *device1_ = nullptr;
   ID3D11DeviceContext1 *context1_ = nullptr;
   ID3D11Texture2D *texture_ = nullptr;
   ID3D11RenderTargetView *texture_rt_view_ = nullptr;
   ID3D11ShaderResourceView *texture_sr_view_ = nullptr;
   DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
};
