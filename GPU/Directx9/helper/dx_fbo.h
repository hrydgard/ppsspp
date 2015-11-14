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

#pragma once

// Simple wrapper around FBO functionality.
// Very C-ish API because that's what I felt like, and it's cool to completely
// hide the data from callers...

namespace DX9 {

struct FBO_DX9;

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
FBO_DX9 *fbo_create(int width, int height, int num_color_textures, bool z_stencil, FBOColorDepth colorDepth = FBO_8888);

// These functions should be self explanatory.
void fbo_bind_as_render_target(FBO_DX9 *fbo);
// color must be 0, for now.
void fbo_bind_color_as_texture(FBO_DX9 *fbo, int color);
void fbo_bind_depth_as_texture(FBO_DX9 *fbo);
LPDIRECT3DSURFACE9 fbo_get_color_for_read(FBO_DX9 *fbo);
LPDIRECT3DSURFACE9 fbo_get_color_for_write(FBO_DX9 *fbo);
void fbo_unbind();
void fbo_destroy(FBO_DX9 *fbo);
void fbo_get_dimensions(FBO_DX9 *fbo, int *w, int *h);
void fbo_resolve(FBO_DX9 *fbo);
HRESULT fbo_blit_color(FBO_DX9 *src, const RECT *srcRect, FBO_DX9 *dst, const RECT *dstRect, D3DTEXTUREFILTERTYPE filter);

LPDIRECT3DTEXTURE9 fbo_get_color_texture(FBO_DX9 *fbo);
LPDIRECT3DTEXTURE9 fbo_get_depth_texture(FBO_DX9 *fbo);

// To get default depth and rt surface
void fbo_init(LPDIRECT3D9 d3d);
void fbo_shutdown();

};