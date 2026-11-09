#include <cstddef>
#include <cstring>

#include "Common/Common.h"
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

LibretroD3D11Context::~LibretroD3D11Context() {
   // The base class destructor can't reach our DestroyDrawContext.
   ReleasePresentTextures();
}

bool LibretroD3D11Context::InitAPI(void *wnd, std::string *deviceName, std::string *error_message) {
   if (!LibretroHWRenderContext::InitHW(true)) {
      return false;
   }

   // Ask for interface version 2, which shares the immediate context by taking turns. Without it, a frontend
   // that presents from another thread (RetroArch's threaded video) can only give us a deferred context,
   // which can't read anything back. A frontend that doesn't know about this hands out version 1.
   retro_hw_render_context_negotiation_interface probe{};
   probe.interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11;
   probe.interface_version = 0;
   if (Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT, &probe) && probe.interface_version >= 1) {
      static const retro_hw_render_context_negotiation_interface_d3d11 negotiation = {
         RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11,
         RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11_VERSION,
         RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2,
      };
      Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&negotiation);
   }

   g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
   return true;
}

bool LibretroD3D11Context::FetchInterface() {
   retro_hw_render_interface_d3d11 *d3d11Interface = nullptr;
   if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&d3d11Interface)) {
      ERROR_LOG(Log::G3D, "Failed to get HW rendering interface!\n");
      return false;
   }

   if (!d3d11Interface) {
      ERROR_LOG(Log::G3D, "D3D11 interface was null!\n");
      return false;
   }

   const unsigned version = d3d11Interface->interface_version;
   if (version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION && version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2) {
      ERROR_LOG(Log::G3D, "HW render interface mismatch, expected %u or %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_D3D11_VERSION, RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2, version);
      return false;
   }

   // Reject lower feature levels. We have D3D9 for these ancient GPUs.
   if (d3d11Interface->featureLevel < D3D_FEATURE_LEVEL_10_0) {
      ERROR_LOG(Log::G3D, "D3D11 featureLevel not high enough - rejecting!\n");
      return false;
   }

   // Workaround: RetroArch doesn't correctly persist interface pointers across context_reset calls even
   // with cache_context set. Pointers within the structure are persisted. Make a hard copy instead to
   // avoid crashes. A version 1 frontend's struct ends where the version 2 members start.
   hwInterface_ = {};
   if (version == RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2) {
      hwInterface_ = *d3d11Interface;
   } else {
      memcpy(&hwInterface_, d3d11Interface, offsetof(retro_hw_render_interface_d3d11, lock_context));
   }
   INFO_LOG(Log::G3D, "D3D11 HW render interface version %u", version);
   return true;
}

void LibretroD3D11Context::ContextReset() {
   if (!FetchInterface()) {
      return;
   }
   LockContext();
   LibretroHWRenderContext::ContextReset();
   UnlockContext();
}

void LibretroD3D11Context::ContextDestroy() {
   // DestroyDrawContext may clear hwInterface_, so hold on to a copy to unlock with.
   const retro_hw_render_interface_d3d11 iface = hwInterface_;
   LockContext();
   if (iface.set_texture) {
      iface.set_texture(iface.handle, nullptr);
   }
   LibretroHWRenderContext::ContextDestroy();
   if (iface.unlock_context) {
      iface.unlock_context(iface.handle);
   }
   // The frontend's handle may not outlive this. The next context reset gets a fresh interface.
   hwInterface_.lock_context = nullptr;
   hwInterface_.unlock_context = nullptr;
   hwInterface_.set_texture = nullptr;
   frameLocked_ = false;
}

void LibretroD3D11Context::LockContext() {
   if (!IsVersion2()) {
      return;
   }
   if (hwInterface_.lock_context(hwInterface_.handle) && draw_) {
      // The frontend has drawn with the context since we last held it, so nothing we bound is still bound.
      draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
   }
}

void LibretroD3D11Context::UnlockContext() {
   if (IsVersion2()) {
      hwInterface_.unlock_context(hwInterface_.handle);
   }
}

void LibretroD3D11Context::BeginFrameAccess() {
   if (!IsVersion2() || frameLocked_) {
      return;
   }
   // Not with the lock held: the frontend needs it to finish with the texture.
   hwInterface_.wait_sync_index(hwInterface_.handle);
   LockContext();
   frameLocked_ = true;
}

void LibretroD3D11Context::EndFrameAccess() {
   if (frameLocked_) {
      frameLocked_ = false;
      UnlockContext();
   }
}

ID3D11Texture2D *LibretroD3D11Context::GetPresentTexture(unsigned index) {
   if (!texture_ || index >= ARRAY_SIZE(presentTextures_)) {
      return nullptr;
   }
   D3D11_TEXTURE2D_DESC desc;
   texture_->GetDesc(&desc);
   ID3D11Texture2D *&tex = presentTextures_[index];
   if (tex) {
      D3D11_TEXTURE2D_DESC existing;
      tex->GetDesc(&existing);
      if (existing.Width == desc.Width && existing.Height == desc.Height && existing.Format == desc.Format) {
         return tex;
      }
      // The frontend took its own reference in set_texture, if it still needs it.
      tex->Release();
      tex = nullptr;
   }
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
   if (FAILED(hwInterface_.device->CreateTexture2D(&desc, nullptr, &tex))) {
      tex = nullptr;
   }
   return tex;
}

void LibretroD3D11Context::ReleasePresentTextures() {
   for (ID3D11Texture2D *&tex : presentTextures_) {
      if (tex) {
         tex->Release();
         tex = nullptr;
      }
   }
}

void LibretroD3D11Context::CreateDrawContext() {
   if (!hwInterface_.device) {
      return;
   }

   ptr_D3DCompile = hwInterface_.D3DCompile;
   hwInterface_.device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1_);
   hwInterface_.context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&context1_);

   std::vector<std::string> adapterNames;
   draw_ = Draw::T3DCreateD3D11Context(hwInterface_.device, hwInterface_.context, device1_, context1_, nullptr, hwInterface_.featureLevel, nullptr, adapterNames, g_Config.iInflightFrames);
}

void LibretroD3D11Context::DestroyDrawContext() {
   const retro_hw_render_interface_d3d11 iface = hwInterface_;
   LockContext();
   if (iface.set_texture) {
      iface.set_texture(iface.handle, nullptr);
   }
   ReleasePresentTextures();
   LibretroHWRenderContext::DestroyDrawContext();
   if (iface.unlock_context) {
      iface.unlock_context(iface.handle);
   }
   frameLocked_ = false;
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

   if (IsVersion2()) {
      // retro_run normally holds the context already.
      BeginFrameAccess();
      hwInterface_.context->OMSetRenderTargets(1, &nullView, nullptr);
      // We keep drawing into texture_, so the frontend gets a copy it can keep until it's done with it.
      ID3D11Texture2D *present = GetPresentTexture(hwInterface_.get_sync_index(hwInterface_.handle));
      if (present) {
         hwInterface_.context->CopyResource(present, texture_);
      }
      hwInterface_.set_texture(hwInterface_.handle, present);
      draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
      // video_cb may wait for the frontend's thread, which may be waiting for the lock.
      EndFrameAccess();
      LibretroHWRenderContext::SwapBuffers();
      return;
   }

   hwInterface_.context->OMSetRenderTargets(1, &nullView, nullptr);

   // libretro doesn't specify how to pass our D3D11 frame to the frontend. RetroArch expects it to be
   // bound to the first shader resource slot.
   hwInterface_.context->PSSetShaderResources(0, 1, &texture_sr_view_);
   LibretroHWRenderContext::SwapBuffers();

   ID3D11ShaderResourceView *nullSRV = nullptr;
   hwInterface_.context->PSSetShaderResources(0, 1, &nullSRV);

   draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
}
