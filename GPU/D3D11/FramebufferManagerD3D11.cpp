// Copyright (c) 2017- PPSSPP Project.

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
#include <d3d11.h>
#include <D3Dcompiler.h>

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/thin3d.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

FramebufferManagerD3D11::FramebufferManagerD3D11(Draw::DrawContext *draw)
	: FramebufferManagerCommon(draw) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	featureLevel_ = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);

	presentation_->SetLanguage(HLSL_D3D11);
	preferredPixelsFormat_ = Draw::DataFormat::B8G8R8A8_UNORM;
}

static void CopyPixelDepthOnly(u32 *dstp, const u32 *srcp, size_t c) {
	size_t x = 0;

#ifdef _M_SSE
	size_t sseSize = (c / 4) * 4;
	const __m128i srcMask = _mm_set1_epi32(0x00FFFFFF);
	const __m128i dstMask = _mm_set1_epi32(0xFF000000);
	__m128i *dst = (__m128i *)dstp;
	const __m128i *src = (const __m128i *)srcp;

	for (; x < sseSize; x += 4) {
		const __m128i bits24 = _mm_and_si128(_mm_load_si128(src), srcMask);
		const __m128i bits8 = _mm_and_si128(_mm_load_si128(dst), dstMask);
		_mm_store_si128(dst, _mm_or_si128(bits24, bits8));
		dst++;
		src++;
	}
#endif

	// Copy the remaining pixels that didn't fit in SSE.
	for (; x < c; ++x) {
		memcpy(dstp + x, srcp + x, 3);
	}
}

// Nobody calls this yet.
void FramebufferManagerD3D11::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	const u32 z_address = vfb->z_address;
	// TODO
}

void FramebufferManagerD3D11::EndFrame() {
}
