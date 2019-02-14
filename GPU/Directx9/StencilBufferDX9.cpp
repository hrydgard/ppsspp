// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <d3d9.h>

#include "base/logging.h"

#include "gfx/d3d9_state.h"
#include "ext/native/thin3d/thin3d.h"
#include "Core/Reporting.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"

namespace DX9 {

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static const char *stencil_ps = R"(
sampler tex: register(s0);
// TODO: Don't use fixed registers?  Or don't overlap?
float4 u_stencilValue : register(c)" STR(CONST_PS_STENCILVALUE) R"();
struct PS_IN {
  float2 v_texcoord0 : TEXCOORD0;
};
float roundAndScaleTo255f(in float x) { return floor(x * 255.99); }
float4 main(PS_IN In) : COLOR {
  float4 index = tex2D(tex, In.v_texcoord0);
  float shifted = roundAndScaleTo255f(index.a) / roundAndScaleTo255f(u_stencilValue.x);
  clip(fmod(floor(shifted), 2.0) - 0.99);
  return index.aaaa;
}
)";

static const char *stencil_vs = R"(
struct VS_IN {
  float4 a_position : POSITION;
  float2 a_texcoord0 : TEXCOORD0;
};
struct VS_OUT {
  float4 position : POSITION;
  float2 v_texcoord0 : TEXCOORD0;
};
VS_OUT main(VS_IN In) {
  VS_OUT Out;
  Out.position = In.a_position;
  Out.v_texcoord0 = In.a_texcoord0;
  return Out;
}
)";

bool FramebufferManagerDX9::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
	addr &= 0x3FFFFFFF;
	if (!MayIntersectFramebuffer(addr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (vfb->fb_address == addr) {
			dstBuffer = vfb;
		}
	}
	if (!dstBuffer) {
		return false;
	}

	int values = 0;
	u8 usedBits = 0;

	const u8 *src = Memory::GetPointer(addr);
	if (!src) {
		return false;
	}

	switch (dstBuffer->format) {
	case GE_FORMAT_565:
		// Well, this doesn't make much sense.
		return false;
	case GE_FORMAT_5551:
		usedBits = StencilBits5551(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 2;
		break;
	case GE_FORMAT_4444:
		usedBits = StencilBits4444(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 16;
		break;
	case GE_FORMAT_8888:
		usedBits = StencilBits8888(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 256;
		break;
	case GE_FORMAT_INVALID:
		// Impossible.
		break;
	}

	if (usedBits == 0) {
		if (skipZero) {
			// Common when creating buffers, it's already 0.  We're done.
			return false;
		}

		// Let's not bother with the shader if it's just zero.
		dxstate.scissorTest.disable();
		dxstate.colorMask.set(false, false, false, true);
		// TODO: Verify this clears only stencil/alpha.
		device_->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_STENCIL, D3DCOLOR_RGBA(0, 0, 0, 0), 0.0f, 0);

		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
		return true;
	}

	if (stencilUploadFailed_) {
		return false;
	}

	// TODO: Helper with logging?
	if (!stencilUploadPS_) {
		std::string errorMessage;
		bool success = CompilePixelShader(device_, stencil_ps, &stencilUploadPS_, NULL, errorMessage);
		if (!errorMessage.empty()) {
			if (success) {
				ERROR_LOG(G3D, "Warnings in shader compilation!");
			} else {
				ERROR_LOG(G3D, "Error in shader compilation!");
			}
			ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
			ERROR_LOG(G3D, "Shader source:\n%s", stencil_ps);
			OutputDebugStringUTF8("Messages:\n");
			OutputDebugStringUTF8(errorMessage.c_str());
			Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), stencil_ps);
		}
		if (!success) {
			if (stencilUploadPS_) {
				stencilUploadPS_->Release();
			}
			stencilUploadPS_ = nullptr;
		}
	}
	if (!stencilUploadVS_) {
		std::string errorMessage;
		bool success = CompileVertexShader(device_, stencil_vs, &stencilUploadVS_, NULL, errorMessage);
		if (!errorMessage.empty()) {
			if (success) {
				ERROR_LOG(G3D, "Warnings in shader compilation!");
			} else {
				ERROR_LOG(G3D, "Error in shader compilation!");
			}
			ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
			ERROR_LOG(G3D, "Shader source:\n%s", stencil_vs);
			OutputDebugStringUTF8("Messages:\n");
			OutputDebugStringUTF8(errorMessage.c_str());
			Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), stencil_vs);
		}
		if (!success) {
			if (stencilUploadVS_) {
				stencilUploadVS_->Release();
			}
			stencilUploadVS_ = nullptr;
		}
	}
	if (!stencilUploadPS_ || !stencilUploadVS_) {
		stencilUploadFailed_ = true;
		return false;
	}

	shaderManagerDX9_->DirtyLastShader();

	dxstate.colorMask.set(false, false, false, true);
	dxstate.stencilTest.enable();
	dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);

	u16 w = dstBuffer->renderWidth;
	u16 h = dstBuffer->renderHeight;

	if (dstBuffer->fbo) {
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR });
	}
	D3DVIEWPORT9 vp{ 0, 0, w, h, 0.0f, 1.0f };
	device_->SetViewport(&vp);
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);

	float u1 = 1.0f;
	float v1 = 1.0f;
	MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight, u1, v1);

	device_->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_STENCIL, D3DCOLOR_RGBA(0, 0, 0, 0), 0.0f, 0);

	dxstate.stencilFunc.set(D3DCMP_ALWAYS, 0xFF, 0xFF);

	float coord[20] = {
		-1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
		 1.0f,  1.0f, 0.0f, u1,   0.0f,
		-1.0f, -1.0f, 0.0f, 0.0f, v1,
		 1.0f, -1.0f, 0.0f, u1,   v1,
	};

	device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	device_->SetVertexDeclaration(pFramebufferVertexDecl);
	device_->SetPixelShader(stencilUploadPS_);
	device_->SetVertexShader(stencilUploadVS_);

	device_->SetTexture(0, drawPixelsTex_);

	shaderManagerDX9_->DirtyLastShader();
	textureCacheDX9_->ForgetLastTexture();

	for (int i = 1; i < values; i += i) {
		if (!(usedBits & i)) {
			// It's already zero, let's skip it.
			continue;
		}
		if (dstBuffer->format == GE_FORMAT_4444) {
			dxstate.stencilMask.set(i | (i << 4));
			const float f[4] = {i * (16.0f / 255.0f)};
			device_->SetPixelShaderConstantF(CONST_PS_STENCILVALUE, f, 1);
		} else if (dstBuffer->format == GE_FORMAT_5551) {
			dxstate.stencilMask.set(0xFF);
			const float f[4] = {i * (128.0f / 255.0f)};
			device_->SetPixelShaderConstantF(CONST_PS_STENCILVALUE, f, 1);
		} else {
			dxstate.stencilMask.set(i);
			const float f[4] = {i * (1.0f / 255.0f)};
			device_->SetPixelShaderConstantF(CONST_PS_STENCILVALUE, f, 1);
		}
		HRESULT hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, coord, 5 * sizeof(float));
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "Failed to draw stencil bit %x: %08x", i, hr);
		}
	}
	dxstate.stencilMask.set(0xFF);
	dxstate.viewport.restore();
	RebindFramebuffer();
	return true;
}

} // namespace DX9
