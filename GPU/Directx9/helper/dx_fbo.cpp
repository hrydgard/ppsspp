// Copyright (c) 2012- PPSSPP Project.

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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "base/logging.h"
#include "dx_fbo.h"
#include "gfx/d3d9_state.h"

namespace DX9 {
	
struct FBO_DX9 {
	uint32_t id;
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 depthstencil;
	LPDIRECT3DTEXTURE9 tex;
	LPDIRECT3DTEXTURE9 depthstenciltex;

	int width;
	int height;
	DX9::FBOColorDepth colorDepth;
};

static LPDIRECT3DSURFACE9 deviceRTsurf;
static LPDIRECT3DSURFACE9 deviceDSsurf;
static bool supportsINTZ = false;
static LPDIRECT3DDEVICE9 g_device;
#define FB_DIV 1
#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z')))

void fbo_init(LPDIRECT3D9 d3d, LPDIRECT3DDEVICE9 device) {
	g_device = device;
	g_device->GetRenderTarget(0, &deviceRTsurf);
	g_device->GetDepthStencilSurface(&deviceDSsurf);

	if (d3d) {
		D3DDISPLAYMODE displayMode;
		d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode);

		// To be safe, make sure both the display format and the FBO format support INTZ.
		HRESULT displayINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		HRESULT fboINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		supportsINTZ = SUCCEEDED(displayINTZ) && SUCCEEDED(fboINTZ);
	}
}

void fbo_shutdown() {
	deviceRTsurf->Release();
	deviceDSsurf->Release();
}

FBO_DX9 *fbo_create(const FramebufferDesc &desc) {
	static uint32_t id = 0;

	FBO_DX9 *fbo = new FBO_DX9();
	fbo->width = desc.width;
	fbo->height = desc.height;
	fbo->colorDepth = desc.colorDepth;
	fbo->depthstenciltex = nullptr;

	HRESULT rtResult = g_device->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &fbo->tex, NULL);
	if (FAILED(rtResult)) {
		ELOG("Failed to create render target");
		delete fbo;
		return NULL;
	}
	fbo->tex->GetSurfaceLevel(0, &fbo->surf);

	HRESULT dsResult;
	if (supportsINTZ) {
		dsResult = g_device->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_DEPTHSTENCIL, FOURCC_INTZ, D3DPOOL_DEFAULT, &fbo->depthstenciltex, NULL);
		if (SUCCEEDED(dsResult)) {
			dsResult = fbo->depthstenciltex->GetSurfaceLevel(0, &fbo->depthstencil);
		}
	} else {
		dsResult = g_device->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	}
	if (FAILED(dsResult)) {
		ELOG("Failed to create depth buffer");
		fbo->surf->Release();
		fbo->tex->Release();
		if (fbo->depthstenciltex) {
			fbo->depthstenciltex->Release();
		}
		delete fbo;
		return NULL;
	}
	fbo->id = id++;
	return fbo;
}

void fbo_destroy(FBO_DX9 *fbo) {
	fbo->tex->Release();
	fbo->surf->Release();
	fbo->depthstencil->Release();
	if (fbo->depthstenciltex) {
		fbo->depthstenciltex->Release();
	}
	delete fbo;
}

void fbo_bind_backbuffer_as_render_target() {
	g_device->SetRenderTarget(0, deviceRTsurf);
	g_device->SetDepthStencilSurface(deviceDSsurf);
	dxstate.scissorRect.restore();
	dxstate.viewport.restore();
}

void fbo_resolve(FBO_DX9 *fbo) {
}

void fbo_bind_as_render_target(FBO_DX9 *fbo) {
	g_device->SetRenderTarget(0, fbo->surf);
	g_device->SetDepthStencilSurface(fbo->depthstencil);
	dxstate.scissorRect.restore();
	dxstate.viewport.restore();
}

uintptr_t fbo_get_api_texture(FBO_DX9 *fbo, int channelBits, int attachment) {
	if (channelBits & FB_SURFACE_BIT) {
		switch (channelBits & 7) {
		case FB_DEPTH_BIT:
			return (uintptr_t)fbo->depthstencil;
		case FB_STENCIL_BIT:
			return (uintptr_t)fbo->depthstencil;
		case FB_COLOR_BIT:
		default:
			return (uintptr_t)fbo->surf;
		}
	} else {
		switch (channelBits & 7) {
		case FB_DEPTH_BIT:
			return (uintptr_t)fbo->depthstenciltex;
		case FB_STENCIL_BIT:
			return 0;  // Can't texture from stencil
		case FB_COLOR_BIT:
		default:
			return (uintptr_t)fbo->tex;
		}
	}
}

LPDIRECT3DSURFACE9 fbo_get_color_for_read(FBO_DX9 *fbo) {
	return fbo->surf;
}

void fbo_bind_as_texture(FBO_DX9 *fbo, int binding, FBOChannel channelBit, int color) {
	switch (channelBit) {
	case FB_DEPTH_BIT:
		if (fbo->depthstenciltex) {
			g_device->SetTexture(binding, fbo->depthstenciltex);
		}
		break;
	case FB_COLOR_BIT:
	default:
		if (fbo->tex) {
			g_device->SetTexture(binding, fbo->tex);
		}
		break;
	}
}

void fbo_get_dimensions(FBO_DX9 *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

bool fbo_blit(FBO_DX9 *src, int srcX1, int srcY1, int srcX2, int srcY2, FBO_DX9 *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) {
	if (channelBits != FB_COLOR_BIT)
		return false;
	RECT srcRect{ (LONG)srcX1, (LONG)srcY1, (LONG)srcX2, (LONG)srcY2 };
	RECT dstRect{ (LONG)dstX1, (LONG)dstY1, (LONG)dstX2, (LONG)dstY2 };
	LPDIRECT3DSURFACE9 srcSurf = src ? src->surf : deviceRTsurf;
	LPDIRECT3DSURFACE9 dstSurf = dst ? dst->surf : deviceRTsurf;
	return SUCCEEDED(g_device->StretchRect(srcSurf, &srcRect, dstSurf, &dstRect, filter == FB_BLIT_LINEAR ? D3DTEXF_LINEAR : D3DTEXF_POINT));
}

}  // namespace
