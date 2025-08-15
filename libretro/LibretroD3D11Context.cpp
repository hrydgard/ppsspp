#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "libretro/LibretroD3D11Context.h"
#include "Common/GPU/D3D11/D3D11Loader.h"

#include <d3d11_1.h>

#ifdef __MINGW32__
#undef __uuidof
#define __uuidof(type) IID_##type
#endif

bool LibretroD3D11Context::Init() {
   if (!LibretroHWRenderContext::Init(true)) {
      return false;
   }

   g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
   return true;
}

void LibretroD3D11Context::CreateDrawContext() {
   retro_hw_render_interface_d3d11 *d3d11Interface = nullptr;
   if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&d3d11Interface)) {
      ERROR_LOG(Log::G3D, "Failed to get HW rendering interface!\n");
      return;
   }

   if (!d3d11Interface) {
      ERROR_LOG(Log::G3D, "D3D11 interface was null!\n");
      return;
   }

   if (d3d11Interface->interface_version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION) {
      ERROR_LOG(Log::G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_D3D11_VERSION, d3d11Interface->interface_version);
      return;
   }

   // Reject lower feature levels. We have D3D9 for these ancient GPUs.
   if (d3d11Interface->featureLevel < D3D_FEATURE_LEVEL_10_0) {
      ERROR_LOG(Log::G3D, "D3D11 featureLevel not high enough - rejecting!\n");
      return;
   }

   // Workaround: RetroArch doesn't correctly persist interface pointers across context_reset calls even
   // with cache_context set. Pointers within the structure are persisted. Make a hard copy instead to
   // avoid crashes.
   hwInterface_ = *d3d11Interface;

   ptr_D3DCompile = hwInterface_.D3DCompile;
   hwInterface_.device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1_);
   hwInterface_.context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&context1_);

   std::vector<std::string> adapterNames;
   draw_ = Draw::T3DCreateD3D11Context(hwInterface_.device, hwInterface_.context, device1_, context1_, nullptr, hwInterface_.featureLevel, nullptr, adapterNames, g_Config.iInflightFrames);
}

void LibretroD3D11Context::DestroyDrawContext() {
   LibretroHWRenderContext::DestroyDrawContext();
   if (device1_) {
      device1_->Release();
      device1_ = nullptr;
   }
   if (context1_) {
      context1_->Release();
      context1_ = nullptr;
   }
   hwInterface_ = {};
}

void LibretroD3D11Context::GotBackbuffer() {
   D3D11_TEXTURE2D_DESC desc{};
   desc.Width = PSP_CoreParameter().pixelWidth;
   desc.Height = PSP_CoreParameter().pixelHeight;
   desc.MipLevels = 1;
   desc.ArraySize = 1;
   desc.Format = format_;
   desc.SampleDesc.Count = 1;
   desc.SampleDesc.Quality = 0;
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
   desc.CPUAccessFlags = 0;
   desc.MiscFlags = 0;

   if (SUCCEEDED(hwInterface_.device->CreateTexture2D(&desc, nullptr, &texture_))) {
      if (SUCCEEDED(hwInterface_.device->CreateRenderTargetView(texture_, nullptr, &texture_rt_view_))) {
         if (SUCCEEDED(hwInterface_.device->CreateShaderResourceView(texture_, nullptr, &texture_sr_view_))) {
            draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, desc.Width, desc.Height, texture_rt_view_, texture_);
            return;
         }
         texture_rt_view_->Release();
         texture_rt_view_ = nullptr;
      }
      texture_->Release();
      texture_ = nullptr;
   }
}

void LibretroD3D11Context::LostBackbuffer() {
   if (draw_ && texture_) {
      D3D11_TEXTURE2D_DESC desc;
      texture_->GetDesc(&desc);

      draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, desc.Width, desc.Height);
   }
   if (texture_sr_view_) {
      texture_sr_view_->Release();
      texture_sr_view_ = nullptr;
   }
   if (texture_rt_view_) {
      texture_rt_view_->Release();
      texture_rt_view_ = nullptr;
   }
   if (texture_) {
      texture_->Release();
      texture_ = nullptr;
   }
}

void LibretroD3D11Context::SwapBuffers() {
   ID3D11RenderTargetView *nullView = nullptr;
   hwInterface_.context->OMSetRenderTargets(1, &nullView, nullptr);

   // libretro doesn't specify how to pass our D3D11 frame to the frontend. RetroArch expects it to be
   // bound to the first shader resource slot.
   hwInterface_.context->PSSetShaderResources(0, 1, &texture_sr_view_);
   LibretroHWRenderContext::SwapBuffers();

   ID3D11ShaderResourceView *nullSRV = nullptr;
   hwInterface_.context->PSSetShaderResources(0, 1, &nullSRV);

   draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
}
