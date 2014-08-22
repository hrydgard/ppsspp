#include "global.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "base/logging.h"
#include "fbo.h"

namespace DX9 {

static LPDIRECT3DSURFACE9 deviceRTsurf;
static LPDIRECT3DSURFACE9 deviceDSsurf;

#define FB_DIV 1

struct FBO {
	uint32_t id;
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 depthstencil;
	LPDIRECT3DTEXTURE9 tex;

	int width;
	int height;
	FBOColorDepth colorDepth;
};

void fbo_init() {
	pD3Ddevice->GetRenderTarget(0, &deviceRTsurf);
	pD3Ddevice->GetDepthStencilSurface(&deviceDSsurf);
}

void fbo_shutdown() {
	deviceRTsurf->Release();
	deviceDSsurf->Release();
}

FBO * current_fbo = NULL;


FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth) {
	static uint32_t id = 0;

	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;
	fbo->colorDepth = colorDepth;

	HRESULT rtResult = pD3Ddevice->CreateTexture(fbo->width, fbo->height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &fbo->tex, NULL);
	if (FAILED(rtResult)) {
		ELOG("Failed to create render target");
		delete fbo;
		return NULL;
	}
	fbo->tex->GetSurfaceLevel(0, &fbo->surf);

	HRESULT dsResult = pD3Ddevice->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	if (FAILED(dsResult)) {
		ELOG("Failed to create depth buffer");
		fbo->surf->Release();
		fbo->tex->Release();
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
	delete fbo;
}

void * fbo_get_rtt(FBO *fbo) {
	return fbo->tex;
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
}

void fbo_bind_for_read(FBO *fbo) {
	OutputDebugStringA("fbo_bind_for_read: Fix me\r\n");
}

void fbo_bind_color_as_texture(FBO *fbo, int color) {
	pD3Ddevice->SetTexture(0, fbo->tex);
}

void fbo_get_dimensions(FBO *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

}