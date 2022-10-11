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

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "Common/GPU/D3D9/D3D9StateCache.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"

#include "Common/GPU/thin3d.h"

#include <algorithm>

#ifdef _M_SSE
#include <emmintrin.h>
#endif

FramebufferManagerDX9::FramebufferManagerDX9(Draw::DrawContext *draw)
	: FramebufferManagerCommon(draw) {
	presentation_->SetLanguage(HLSL_D3D9);
	preferredPixelsFormat_ = Draw::DataFormat::B8G8R8A8_UNORM;
}

FramebufferManagerDX9::~FramebufferManagerDX9() {
}

bool FramebufferManagerDX9::ReadbackDepthbufferSync(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride) {
	// We always read the depth buffer in 24_8 format.
	LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(fbo, Draw::FB_DEPTH_BIT, 0);
	if (!tex)
		return false;

	// This is separate from the thin3d way because it applies DepthScaleFactors.
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT locked;
	tex->GetLevelDesc(0, &desc);

	RECT rect = {(LONG)x, (LONG)y, (LONG)w, (LONG)h};
	HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);
	if (!SUCCEEDED(hr))
		return false;

	const u32 *packed = (const u32 *)locked.pBits;
	u16 *depth = (u16 *)pixels;

	DepthScaleFactors depthScale = GetDepthScaleFactors();
	// TODO: Optimize.
	for (int yp = 0; yp < h; ++yp) {
		for (int xp = 0; xp < w; ++xp) {
			const int offset = (yp + y) * pixelsStride + x + xp;

			float scaled = depthScale.Apply((packed[offset] & 0x00FFFFFF) * (1.0f / 16777215.0f));
			if (scaled <= 0.0f) {
				depth[offset] = 0;
			} else if (scaled >= 65535.0f) {
				depth[offset] = 65535;
			} else {
				depth[offset] = (int)scaled;
			}
		}
	}

	tex->UnlockRect(0);
	return true;
}
