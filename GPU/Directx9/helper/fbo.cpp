#include "global.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "fbo.h"

namespace DX9 {

static LPDIRECT3DSURFACE9 currentRtt;
static LPDIRECT3DSURFACE9 workingRtt;
static LPDIRECT3DSURFACE9 deviceRTsurf;
static LPDIRECT3DSURFACE9 deviceDSsurf;

#define FB_DIV 1

struct FBO {
	uint32_t id;
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

	//pD3Ddevice->CreateRenderTarget(1280/FB_DIV, 720/FB_DIV, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &workingRtt, NULL);
}

FBO * current_fbo = NULL;


FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth) {
	static uint32_t id = 0;

	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;
	fbo->colorDepth = colorDepth;

	// only support 32bit surfaces
	//pD3Ddevice->CreateRenderTarget(fbo->width/4, fbo->height/4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->surf, NULL);
	
	/*
	// Create depth + stencil target | forced to 24-bit Z, 8-bit stencil
	pD3Ddevice->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	*/
	// Only needed on xbox
#ifdef _XBOX
	pD3Ddevice->CreateTexture(fbo->width/FB_DIV, fbo->height/FB_DIV, 1, 0, D3DFMT_A8R8G8B8, 0, &fbo->tex, NULL);
	if (workingRtt == NULL) {
		pD3Ddevice->CreateRenderTarget(fbo->width/FB_DIV, fbo->height/FB_DIV, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &workingRtt, NULL);
	}
#else
	pD3Ddevice->CreateRenderTarget(fbo->width/4, fbo->height/4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->surf, NULL);
	pD3Ddevice->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
#endif

	fbo->stencil_buffer = 8;
	fbo->z_buffer = 24;
	fbo->id = id++;
	return fbo;
}

void * fbo_get_rtt(FBO *fbo) {
	return fbo->tex;
}

void fbo_unbind() {
	if (current_fbo != NULL) {
		
#ifdef _XBOX
		D3DVECTOR4 White = {0.0f, 0.0f, 0.0f, 0.0f};
		pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS|D3DRESOLVE_CLEARRENDERTARGET|D3DRESOLVE_CLEARDEPTHSTENCIL, NULL, 
			current_fbo->tex, NULL, 0, 0, &White, 0.0f, 0, NULL );
#else
		// TODO?
#endif
		/*
		pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, 
			current_fbo->tex, NULL, 0, 0, 0, 0.0f, 0, NULL );
		*/
		//pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 0, 0);
	}
	
	//pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 0, 0);
	current_fbo = NULL;

	pD3Ddevice->SetRenderTarget(0, deviceRTsurf);
	//pD3Ddevice->SetDepthStencilSurface(deviceDSsurf);

	currentRtt = deviceRTsurf;
}

void fbo_resolve(FBO *fbo) {
	if (fbo && fbo->tex) {
#ifdef _XBOX
		pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, fbo->tex, NULL, 0, 0, NULL, 0.0f, 0, NULL );
#else
		// TODO?
#endif
	}
#if 0
		// Hack save to disk ...
		char fname[256];
		static int f = 0;
		sprintf(fname, "game:\\rtt.%08x.%d.png", fbo->id, f++);
		D3DXSaveTextureToFile(fname, D3DXIFF_PNG, fbo->tex, NULL);
		//strcat(fname, "\n");
		OutputDebugString(fname);
#endif
}

void fbo_bind_as_render_target(FBO *fbo) {
	current_fbo = fbo;
	pD3Ddevice->SetRenderTarget(0, workingRtt);
	currentRtt = workingRtt;
	//pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 0, 0);
	//pD3Ddevice->SetDepthStencilSurface(fbo->depthstencil);
}

void fbo_bind_for_read(FBO *fbo) {
	OutputDebugStringA("fbo_bind_for_read: Fix me\r\n");
}

void fbo_bind_color_as_texture(FBO *fbo, int color) {

#if 0
		// Hack save to disk ...
		char fname[256];
		static int f = 0;
		sprintf(fname, "game:\\rtt.%08x.%d.png", fbo->id, f++);
		D3DXSaveTextureToFile(fname, D3DXIFF_PNG, fbo->tex, NULL);
		//strcat(fname, "\n");
		OutputDebugString(fname);
#endif
	//pD3Ddevice->SetRenderTarget(0, workingRtt);
	//pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, fbo->tex, NULL, 0, 0, NULL, 0.0f, 0, NULL );
	//pD3Ddevice->SetRenderTarget(0, currentRtt);

	//OutputDebugStringA("fbo_bind_color_as_texture: Fix me\r\n");
	pD3Ddevice->SetTexture(0, fbo->tex);
	//pD3Ddevice->SetTexture(0, NULL);
}

void fbo_destroy(FBO *fbo) {
	/*
	fbo->depthstencil->Release();
	*/
	//fbo->surf->Release();
#ifdef _XBOX
	fbo->tex->Release();
#else
	fbo->depthstencil->Release();
	fbo->surf->Release();
#endif
	delete fbo;
}

void fbo_get_dimensions(FBO *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

void SwapBuffer() {
	pD3Ddevice->Present(0, 0, 0, 0);

	// :s
	//pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0,0 ,0), 0, 0);
}

};