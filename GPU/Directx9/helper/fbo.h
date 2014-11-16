#pragma once

// Simple wrapper around FBO functionality.
// Very C-ish API because that's what I felt like, and it's cool to completely
// hide the data from callers...

struct FBO;

namespace DX9 {

enum FBOColorDepth {
	FBO_8888,
	FBO_565,
	FBO_4444,
	FBO_5551,
};

// Creates a simple FBO with a RGBA32 color buffer stored in a texture, and
// optionally an accompanying Z/stencil buffer.
// No mipmap support.
// num_color_textures must be 1 for now.
// you lose bound texture state.

// On some hardware, you might get a 24-bit depth buffer even though you only wanted a 16-bit one.
FBO *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth = FBO_8888);

// These functions should be self explanatory.
void fbo_bind_as_render_target(FBO *fbo);
// color must be 0, for now.
void fbo_bind_color_as_texture(FBO *fbo, int color);
void fbo_bind_depth_as_texture(FBO *fbo);
LPDIRECT3DSURFACE9 fbo_get_color_for_read(FBO *fbo);
LPDIRECT3DSURFACE9 fbo_get_color_for_write(FBO *fbo);
void fbo_unbind();
void fbo_destroy(FBO *fbo);
void fbo_get_dimensions(FBO *fbo, int *w, int *h);
void fbo_resolve(FBO *fbo);
HRESULT fbo_blit_color(FBO *src, const RECT *srcRect, FBO *dst, const RECT *dstRect, D3DTEXTUREFILTERTYPE filter);

LPDIRECT3DTEXTURE9 fbo_get_color_texture(FBO *fbo);
LPDIRECT3DTEXTURE9 fbo_get_depth_texture(FBO *fbo);

// To get default depth and rt surface
void fbo_init(LPDIRECT3D9 d3d);
void fbo_shutdown();

};