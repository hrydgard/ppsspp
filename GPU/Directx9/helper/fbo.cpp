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

#define FB_DIV 4

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
}

FBO * current_fbo = NULL;


FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth) {
	static uint32_t id = 0;

	FBO *fbo = new FBO();
	fbo->width = width;
	fbo->height = height;
	fbo->colorDepth = colorDepth;

#if 1
	D3DSURFACE_PARAMETERS SurfaceParams;
    memset( &SurfaceParams, 0, sizeof( D3DSURFACE_PARAMETERS ) );
    SurfaceParams.Base = 0;

	pD3Ddevice->CreateTexture(fbo->width, fbo->height, 0, 0, ( D3DFORMAT )( D3DFMT_LE_A8R8G8B8 ), 0, &fbo->tex, NULL);        
	pD3Ddevice->CreateRenderTarget(fbo->width, fbo->height, ( D3DFORMAT )( D3DFMT_LE_A8R8G8B8 ), D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->surf, &SurfaceParams);
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
	pD3Ddevice->SetRenderTarget(0, deviceRTsurf);
	current_fbo = NULL;
}

void fbo_resolve(FBO *fbo) {
	if (fbo && fbo->tex) {
#ifdef _XBOX
		D3DVECTOR4 White = {0.0f, 0.0f, 0.0f, 0.0f};
		pD3Ddevice->Resolve( D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS, NULL, 
			fbo->tex, NULL, 0, 0, &White, 0.0f, 0, NULL );
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
	pD3Ddevice->SetRenderTarget(0, fbo->surf);
}

void fbo_bind_for_read(FBO *fbo) {
	//fbo_resolve(fbo);
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
	pD3Ddevice->SetTexture(0, fbo->tex);
}

void fbo_destroy(FBO *fbo) {
	fbo->tex->Release();
	fbo->surf->Release();

	delete fbo;
}

void fbo_get_dimensions(FBO *fbo, int *w, int *h) {
	*w = fbo->width;
	*h = fbo->height;
}

};