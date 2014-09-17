#include "global.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "base/logging.h"
#include "fbo.h"
#include "dx_state.h"

struct FBO {
	uint32_t id;
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 depthstencil;
	LPDIRECT3DTEXTURE9 tex;
	LPDIRECT3DTEXTURE9 depthstenciltex;

	int width;
	int height;
	DX9::FBOColorDepth colorDepth;
};

namespace DX9 {

static LPDIRECT3DSURFACE9 deviceRTsurf;
static LPDIRECT3DSURFACE9 deviceDSsurf;
static bool supportsINTZ = false;

#define FB_DIV 1
#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z')))

void fbo_init(LPDIRECT3D9 d3d) {
	pD3Ddevice->GetRenderTarget(0, &deviceRTsurf);
	pD3Ddevice->GetDepthStencilSurface(&deviceDSsurf);

	if (d3d) {
		D3DDISPLAYMODE displayMode;
		d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode);
		HRESULT intzFormat = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		supportsINTZ = SUCCEEDED(intzFormat);
	}
}

void fbo_shutdown() {
	deviceRTsurf->Release();
	deviceDSsurf->Release();
}

FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth) {
	static uint32_t id = 0;

	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;
	fbo->colorDepth = colorDepth;
	fbo->depthstenciltex = nullptr;

	HRESULT rtResult = pD3Ddevice->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &fbo->tex, NULL);
	if (FAILED(rtResult)) {
		ELOG("Failed to create render target");
		delete fbo;
		return NULL;
	}
	fbo->tex->GetSurfaceLevel(0, &fbo->surf);

	HRESULT dsResult;
	if (supportsINTZ) {
		dsResult = pD3Ddevice->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_DEPTHSTENCIL, FOURCC_INTZ, D3DPOOL_DEFAULT, &fbo->depthstenciltex, NULL);
		if (SUCCEEDED(dsResult)) {
			dsResult = fbo->depthstenciltex->GetSurfaceLevel(0, &fbo->depthstencil);
		}
	} else {
		dsResult = pD3Ddevice->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
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

void fbo_destroy(FBO *fbo) {
	fbo->tex->Release();
	fbo->surf->Release();
	fbo->depthstencil->Release();
	if (fbo->depthstenciltex) {
		fbo->depthstenciltex->Release();
	}
	delete fbo;
}

void fbo_unbind() {
	pD3Ddevice->SetRenderTarget(0, deviceRTsurf);
	pD3Ddevice->SetDepthStencilSurface(deviceDSsurf);
}

void fbo_resolve(FBO *fbo) {
}

void fbo_bind_as_render_target(FBO *fbo) {
	pD3Ddevice->SetRenderTarget(0, fbo->surf);
	pD3Ddevice->SetDepthStencilSurface(fbo->depthstencil);
	dxstate.scissorRect.restore();
	dxstate.viewport.restore();
}


LPDIRECT3DTEXTURE9 fbo_get_color_texture(FBO *fbo) {
	return fbo->tex;
}

LPDIRECT3DTEXTURE9 fbo_get_depth_texture(FBO *fbo) {
	return fbo->depthstenciltex;
}

LPDIRECT3DSURFACE9 fbo_get_color_for_read(FBO *fbo) {
	return fbo->surf;
}

LPDIRECT3DSURFACE9 fbo_get_color_for_write(FBO *fbo) {
	return fbo->surf;
}

void fbo_bind_color_as_texture(FBO *fbo, int color) {
	pD3Ddevice->SetTexture(0, fbo->tex);
}

void fbo_bind_depth_as_texture(FBO *fbo) {
	if (fbo->depthstenciltex) {
		pD3Ddevice->SetTexture(0, fbo->depthstenciltex);
	}
}

void fbo_get_dimensions(FBO *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

HRESULT fbo_blit_color(FBO *src, const RECT *srcRect, FBO *dst, const RECT *dstRect, D3DTEXTUREFILTERTYPE filter) {
	LPDIRECT3DSURFACE9 srcSurf = src ? src->surf : deviceRTsurf;
	LPDIRECT3DSURFACE9 dstSurf = dst ? dst->surf : deviceRTsurf;
	return pD3Ddevice->StretchRect(srcSurf, srcRect, dstSurf, dstRect, filter);
}

}
