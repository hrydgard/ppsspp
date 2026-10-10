#include <stddef.h>
#include <string.h>

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "GPU/GPUCommon.h"
#include "libretro/LibretroD3D11Context.h"
#include "Common/GPU/D3D11/D3D11Loader.h"

#include <d3d11_1.h>

#ifdef __MINGW32__
#undef __uuidof
#define __uuidof(type) IID_##type
#endif

/* Alive until retro_deinit, as the frontend keeps the pointer. */
static const struct retro_hw_render_context_negotiation_interface_d3d11 d3d11_negotiation = {
   RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11,
   RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11_VERSION,
   RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2,
};

/* The interface is fetched again at every context reset: a frontend that
 * rebuilt its driver may hand out another handle, context or version. */
static bool FetchD3D11Interface(retro_hw_render_interface_d3d11 *out) {
   retro_hw_render_interface_d3d11 *iface = nullptr;
   if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&iface) || !iface) {
      ERROR_LOG(Log::G3D, "Failed to get HW rendering interface!\n");
      return false;
   }
   if (iface->interface_type != RETRO_HW_RENDER_INTERFACE_D3D11) {
      ERROR_LOG(Log::G3D, "HW render interface is not D3D11!\n");
      return false;
   }
   memset(out, 0, sizeof(*out));
   if (iface->interface_version == RETRO_HW_RENDER_INTERFACE_D3D11_VERSION) {
      /* A version 1 frontend's struct ends where version 2 begins. */
      memcpy(out, iface, offsetof(retro_hw_render_interface_d3d11, lock_context));
   } else if (iface->interface_version == RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2) {
      *out = *iface;
   } else {
      ERROR_LOG(Log::G3D, "HW render interface version %u not supported!\n", iface->interface_version);
      return false;
   }
   // Reject lower feature levels. We have D3D9 for these ancient GPUs.
   if (out->featureLevel < D3D_FEATURE_LEVEL_10_0) {
      ERROR_LOG(Log::G3D, "D3D11 featureLevel not high enough - rejecting!\n");
      return false;
   }
   return true;
}

bool LibretroD3D11Context::InitAPI(void *wnd, std::string *deviceName, std::string *error_message) {
   retro_hw_render_context_negotiation_interface probe;

   if (!LibretroHWRenderContext::InitHW(true)) {
      return false;
   }

   /* Ask for version 2; a frontend that does not know the type answers 0
    * and hands out version 1. */
   probe.interface_type    = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_D3D11;
   probe.interface_version = 0;
   if (Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT, &probe)
         && probe.interface_version >= 1) {
      Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&d3d11_negotiation);
   }

   g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
   return true;
}

void LibretroD3D11Context::CreateDrawContext() {
   std::vector<std::string> adapterNames;

   ptr_D3DCompile = hwInterface_.D3DCompile;
   hwInterface_.device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1_);
   hwInterface_.context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&context1_);

   draw_ = Draw::T3DCreateD3D11Context(hwInterface_.device, hwInterface_.context, device1_, context1_, nullptr, hwInterface_.featureLevel, nullptr, adapterNames);
}

void LibretroD3D11Context::DestroyDrawContext() {
   LostBackbuffer();
   FreeSlots();
   LibretroHWRenderContext::DestroyDrawContext();
   if (device1_) {
      device1_->Release();
      device1_ = nullptr;
   }
   if (context1_) {
      context1_->Release();
      context1_ = nullptr;
   }
}

static void ReleaseSlot(LibretroD3D11Context::Slot *s) {
   if (s->rtv) {
      s->rtv->Release();
   }
   if (s->tex) {
      s->tex->Release();
   }
   s->rtv    = nullptr;
   s->tex    = nullptr;
   s->width  = 0;
   s->height = 0;
}

void LibretroD3D11Context::FreeSlots() {
   int i;
   for (i = 0; i < LIBRETRO_D3D11_MAX_SLOTS; i++) {
      ReleaseSlot(&slots_[i]);
   }
}

void LibretroD3D11Context::LockGPU() {
   if (!live_ || !v2_) {
      return;
   }
   // Outside a frame nothing binds the cached state again.
   if (hwInterface_.lock_context(hwInterface_.handle) && draw_ && frameSlot_ < 0) {
      hwInterface_.context->ClearState();
      draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
   }
}

void LibretroD3D11Context::UnlockGPU() {
   if (live_ && v2_) {
      hwInterface_.unlock_context(hwInterface_.handle);
   }
}

void LibretroD3D11Context::ContextReset() {
   retro_hw_render_interface_d3d11 iface;
   bool changed;

   INFO_LOG(Log::G3D, "Context reset");
   if (!FetchD3D11Interface(&iface)) {
      return;
   }

   if (iface.interface_version >= RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2
         && iface.lock_context(iface.handle)) {
      iface.context->ClearState();
   }

   if (gpu && lostGpu_ != gpu) {
      gpu->DeviceLost();
      lostGpu_ = gpu;
   }

   /* Another device or context, or another version: nothing made for
    * the old one is kept. */
   changed = !draw_
      || iface.device            != hwInterface_.device
      || iface.context           != hwInterface_.context
      || iface.interface_version != hwInterface_.interface_version;
   if (changed && draw_) {
      DestroyDrawContext();
   }

   hwInterface_ = iface;
   v2_          = iface.interface_version >= RETRO_HW_RENDER_INTERFACE_D3D11_VERSION_2;
   live_        = true;

   if (!draw_) {
      bool success;
      CreateDrawContext();
      success = draw_->CreatePresets();
      _assert_(success);
   }

   GotBackbuffer();

   if (gpu && lostGpu_ == gpu) {
      gpu->DeviceRestore(draw_);
   }
   lostGpu_ = nullptr;

   if (v2_) {
      hwInterface_.unlock_context(hwInterface_.handle);
   }
}

void LibretroD3D11Context::ContextDestroy() {
   INFO_LOG(Log::G3D, "Context destroy");
   if (!live_) {
      return;
   }

   LockGPU();
   if (gpu && lostGpu_ != gpu) {
      gpu->DeviceLost();
      lostGpu_ = gpu;
   }
   /* The frontend is done with the context: everything made on it goes
    * now, while the context can still be taken. */
   DestroyDrawContext();
   UnlockGPU();
   live_ = false;
}

void LibretroD3D11Context::GotBackbuffer() {
   D3D11_TEXTURE2D_DESC desc{};

   if (v2_) {
      /* Bound per frame in FrameBegin, once the frontend is done with
       * the sync index's texture. */
      LostBackbuffer();
      return;
   }

   LostBackbuffer();

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
   if (draw_ && (texture_ || boundSlot_ >= 0)) {
      draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, -1, -1);
   }
   boundSlot_ = -1;
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

/* Version 2: waits for the frontend to be done with this sync index's
 * texture, takes the context and makes the texture the backbuffer. The
 * frame draws straight into it and the frontend reads it from there. */
void LibretroD3D11Context::FrameBegin() {
   D3D11_TEXTURE2D_DESC desc;
   UINT w, h;
   unsigned idx;
   Slot *s;

   if (!live_ || !draw_ || frameSlot_ >= 0) {
      return;
   }
   /* The frontend draws on this context between frames, and leaves bound
    * stages nothing here ever sets (a geometry shader, say). BeginFrame
    * binds the cached state again and BeginHostFrame dirties all of the
    * PSP's. */
   if (!v2_) {
      hwInterface_.context->ClearState();
      return;
   }

   idx = hwInterface_.get_sync_index(hwInterface_.handle);
   if (idx >= LIBRETRO_D3D11_MAX_SLOTS) {
      return;
   }
   hwInterface_.wait_sync_index(hwInterface_.handle);
   if (hwInterface_.lock_context(hwInterface_.handle)) {
      hwInterface_.context->ClearState();
   }
   frameSlot_ = (int)idx;

   w = (UINT)PSP_CoreParameter().pixelWidth;
   h = (UINT)PSP_CoreParameter().pixelHeight;
   s = &slots_[idx];
   if (!s->tex || s->width != w || s->height != h) {
      if ((int)idx == boundSlot_) {
         LostBackbuffer();
      }
      ReleaseSlot(s);

      memset(&desc, 0, sizeof(desc));
      desc.Width            = w;
      desc.Height           = h;
      desc.MipLevels        = 1;
      desc.ArraySize        = 1;
      desc.Format           = format_;
      desc.SampleDesc.Count = 1;
      desc.Usage            = D3D11_USAGE_DEFAULT;
      /* Shader resource for the frontend's own view of it. */
      desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      if (FAILED(hwInterface_.device->CreateTexture2D(&desc, nullptr, &s->tex))) {
         s->tex = nullptr;
      } else if (FAILED(hwInterface_.device->CreateRenderTargetView(s->tex, nullptr, &s->rtv))) {
         ReleaseSlot(s);
      }
      if (!s->tex) {
         ERROR_LOG(Log::G3D, "Failed to create the present texture for sync index %u\n", idx);
         return;
      }
      s->width  = w;
      s->height = h;
   }

   if (boundSlot_ != (int)idx) {
      draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, (int)w, (int)h, s->rtv, s->tex);
      boundSlot_ = (int)idx;
   }
}

void LibretroD3D11Context::SwapBuffers() {
   ID3D11RenderTargetView *nullView = nullptr;

   if (v2_) {
      int idx = frameSlot_;
      if (idx < 0) {
         /* No frame was drawn: the frontend shows the last one again. */
         video_cb(nullptr, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
         return;
      }
      frameSlot_ = -1;
      if (boundSlot_ != idx) {
         /* The texture could not be made; nothing is handed over. */
         hwInterface_.unlock_context(hwInterface_.handle);
         video_cb(nullptr, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
         return;
      }
      hwInterface_.context->OMSetRenderTargets(1, &nullView, nullptr);
      hwInterface_.set_texture(hwInterface_.handle, slots_[idx].tex);
      draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
      hwInterface_.unlock_context(hwInterface_.handle);
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
