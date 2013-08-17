#include "global.h"
#include <stdint.h>
#include <string.h>
#include "fbo.h"

static LPDIRECT3DSURFACE9 deviceRTsurf;
static LPDIRECT3DSURFACE9 deviceDSsurf;


struct FBO {
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 depthstencil;
	LPDIRECT3DTEXTURE9 tex;
	uint32_t color_texture;
	uint32_t z_stencil_buffer;  // Either this is set, or the two below.
	uint32_t z_buffer;
	uint32_t stencil_buffer;

	int width;
	int height;
	FBOColorDepth colorDepth;
};

void fbo_init() {
	pD3Ddevice->GetRenderTarget(0, &deviceRTsurf);
	pD3Ddevice->GetDepthStencilSurface(&deviceDSsurf);
}

FBO * current_fbo = NULL;


FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth) {
	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;
	fbo->colorDepth = colorDepth;

	// only support 32bit surfaces
	//pD3Ddevice->CreateRenderTarget(fbo->width, fbo->height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->surf, NULL);
	
	/*
	// Create depth + stencil target | forced to 24-bit Z, 8-bit stencil
	pD3Ddevice->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	*/
	// Only needed on xbox :s
	pD3Ddevice->CreateTexture(fbo->width, fbo->height, 1, 0, D3DFMT_A8R8G8B8, 0, &fbo->tex, NULL);

	fbo->stencil_buffer = 8;
	fbo->z_buffer = 24;
	return fbo;
}

void * fbo_get_rtt(FBO *fbo) {
	return fbo->tex;
}

void fbo_unbind() {
	if (current_fbo != NULL) {
		//pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0, NULL, current_fbo->tex, NULL, 0, 0, NULL, 0.0f, 0, NULL );
	}
	current_fbo = NULL;

	//pD3Ddevice->SetRenderTarget(0, deviceRTsurf);
	//pD3Ddevice->SetDepthStencilSurface(deviceDSsurf);
}

void fbo_resolve(FBO *fbo) {
	pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS|D3DRESOLVE_CLEARRENDERTARGET|D3DRESOLVE_CLEARDEPTHSTENCIL, NULL, fbo->tex, NULL, 0, 0, NULL, 0.0f, 0, NULL );
}

void fbo_bind_as_render_target(FBO *fbo) {
	current_fbo = fbo;

	//pD3Ddevice->SetRenderTarget(0, fbo->surf);
	//pD3Ddevice->SetDepthStencilSurface(fbo->depthstencil);
}

void fbo_bind_for_read(FBO *fbo) {
	OutputDebugStringA("fbo_bind_for_read: Fix me\r\n");
}

void fbo_bind_color_as_texture(FBO *fbo, int color) {
	//OutputDebugStringA("fbo_bind_color_as_texture: Fix me\r\n");
	//pD3Ddevice->SetTexture(0, fbo->tex);
	pD3Ddevice->SetTexture(0, NULL);
}

void fbo_destroy(FBO *fbo) {
	/*
	fbo->depthstencil->Release();
	*/
	//fbo->surf->Release();
	fbo->tex->Release();
	delete fbo;
}

void fbo_get_dimensions(FBO *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

void SwapBuffer() {
	pD3Ddevice->Present(0, 0, 0, 0);

	// :s
	pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 0, 0);
}