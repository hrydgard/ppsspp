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
	if (!LibretroHWRenderContext::Init(true))
		return false;

	g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
	return true;
}

void LibretroD3D11Context::CreateDrawContext() {
	std::vector<std::string> adapterNames;

	if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&d3d11_) || !d3d11_) {
		ERROR_LOG(Log::G3D, "Failed to get HW rendering interface!\n");
		return;
	}

	if (d3d11_->interface_version != RETRO_HW_RENDER_INTERFACE_D3D11_VERSION) {
		ERROR_LOG(Log::G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_D3D11_VERSION, d3d11_->interface_version);
		return;
	}

   // Reject lower feature levels. We have D3D9 for these ancient GPUs.
   if (d3d11_->featureLevel < D3D_FEATURE_LEVEL_10_0) {
      ERROR_LOG(Log::G3D, "D3D11 featureLevel not high enough - rejecting!\n");
      return;
   }

	ptr_D3DCompile = d3d11_->D3DCompile;

	ID3D11Device1 *device1 = nullptr;
	d3d11_->device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1);

	ID3D11DeviceContext1 *context1 = nullptr;
	d3d11_->context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&context1);

	draw_ = Draw::T3DCreateD3D11Context(d3d11_->device, d3d11_->context, device1, context1, nullptr, d3d11_->featureLevel, NULL, adapterNames, 3);
}

void LibretroD3D11Context::DestroyDrawContext() {
	LibretroHWRenderContext::DestroyDrawContext();
	d3d11_ = nullptr;
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

	if (SUCCEEDED(d3d11_->device->CreateTexture2D(&desc, nullptr, &texture_))) {
		if (SUCCEEDED(d3d11_->device->CreateRenderTargetView(texture_, nullptr, &RTView_))) {
			if (SUCCEEDED(d3d11_->device->CreateShaderResourceView(texture_, nullptr, &SRView_))) {
				draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, RTView_, texture_);
				return;
			}
			RTView_->Release();
			RTView_ = nullptr;
		}
		texture_->Release();
		texture_ = nullptr;
	}
}

void LibretroD3D11Context::LostBackbuffer() {
	LibretroGraphicsContext::LostBackbuffer();
	SRView_->Release();
	SRView_ = nullptr;
	RTView_->Release();
	RTView_ = nullptr;
	texture_->Release();
	texture_ = nullptr;
}

void LibretroD3D11Context::SwapBuffers() {
	ID3D11RenderTargetView *nullView = nullptr;
	d3d11_->context->OMSetRenderTargets(1, &nullView, nullptr);

	d3d11_->context->PSSetShaderResources(0, 1, &SRView_);
	LibretroHWRenderContext::SwapBuffers();

	ID3D11ShaderResourceView *nullSRV = nullptr;
	d3d11_->context->PSSetShaderResources(0, 1, &nullSRV);

	draw_->HandleEvent(Draw::Event::PRESENTED, 0, 0, nullptr, nullptr);
}
