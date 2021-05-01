// See comment in header for the purpose of the code in this file.

#include <algorithm>
#include <cmath>

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

extern DSStretch g_DarkStalkerStretch;
// For Darkstalkers hack. Ugh.
extern bool currentDialogActive;

namespace Rasterizer {

// Through mode, with the specific Darkstalker settings.
inline void DrawSinglePixel5551(u16 *pixel, const u32 color_in) {
	u32 new_color;
	if ((color_in >> 24) == 255) {
		new_color = color_in & 0xFFFFFF;
	} else {
		const u32 old_color = RGBA5551ToRGBA8888(*pixel);
		const Vec4<int> dst = Vec4<int>::FromRGBA(old_color);
		Vec3<int> blended = AlphaBlendingResult(Vec4<int>::FromRGBA(color_in), dst);
		// ToRGB() always automatically clamps.
		new_color = blended.ToRGB();
	}
	new_color |= (*pixel & 0x8000) ? 0xff000000 : 0x00000000;
	*pixel = RGBA8888ToRGBA5551(new_color);
}

static inline Vec4<int> ModulateRGBA(const Vec4<int>& prim_color, const Vec4<int>& texcolor) {
	Vec3<int> out_rgb;
	int out_a;

#if defined(_M_SSE)
	// We can be accurate up to 24 bit integers, should be enough.
	const __m128 p = _mm_cvtepi32_ps(prim_color.ivec);
	const __m128 t = _mm_cvtepi32_ps(texcolor.ivec);
	const __m128 b = _mm_mul_ps(p, t);
	if (gstate.isColorDoublingEnabled()) {
		// We double right here, only for modulate.  Other tex funcs do not color double.
		const __m128 doubleColor = _mm_setr_ps(2.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f, 1.0f / 255.0f);
		out_rgb.ivec = _mm_cvtps_epi32(_mm_mul_ps(b, doubleColor));
	} else {
		out_rgb.ivec = _mm_cvtps_epi32(_mm_mul_ps(b, _mm_set_ps1(1.0f / 255.0f)));
	}
	return Vec4<int>(out_rgb.ivec);
#else
	if (gstate.isColorDoublingEnabled()) {
		out_rgb = (prim_color.rgb() * texcolor.rgb() * 2) / 255;
	} else {
		out_rgb = prim_color.rgb() * texcolor.rgb() / 255;
	}
	out_a = (prim_color.a() * texcolor.a() / 255);
#endif

	return Vec4<int>(out_rgb.r(), out_rgb.g(), out_rgb.b(), out_a);

}

void DrawSprite(const VertexData& v0, const VertexData& v1) {
	const u8 *texptr = nullptr;

	GETextureFormat texfmt = gstate.getTextureFormat();
	u32 texaddr = gstate.getTextureAddress(0);
	int texbufw = GetTextureBufw(0, texaddr, texfmt);
	if (Memory::IsValidAddress(texaddr))
		texptr = Memory::GetPointerUnchecked(texaddr);

	ScreenCoords pprime(v0.screenpos.x, v0.screenpos.y, 0);
	Sampler::NearestFunc nearestFunc = Sampler::GetNearestFunc();  // Looks at gstate.

	DrawingCoords pos0 = TransformUnit::ScreenToDrawing(v0.screenpos);
	DrawingCoords pos1 = TransformUnit::ScreenToDrawing(v1.screenpos);

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1(), 0);
	DrawingCoords scissorBR(gstate.getScissorX2(), gstate.getScissorY2(), 0);

	int z = pos0.z;
	float fog = 1.0f;

	bool isWhite = v1.color0 == Vec4<int>(255, 255, 255, 255);

	if (gstate.isTextureMapEnabled()) {
		// 1:1 (but with mirror support) texture mapping!
		int s_start = v0.texturecoords.x;
		int t_start = v0.texturecoords.y;
		int ds = v1.texturecoords.x > v0.texturecoords.x ? 1 : -1;
		int dt = v1.texturecoords.y > v0.texturecoords.y ? 1 : -1;

		if (ds < 0) {
			s_start += ds;
		}
		if (dt < 0) {
			t_start += dt;
		}

		// First clip the right and bottom sides, since we don't need to adjust the deltas.
		if (pos1.x > scissorBR.x) pos1.x = scissorBR.x + 1;
		if (pos1.y > scissorBR.y) pos1.y = scissorBR.y + 1;
		// Now clip the other sides.
		if (pos0.x < scissorTL.x) {
			s_start += (scissorTL.x - pos0.x) * ds;
			pos0.x = scissorTL.x;
		}
		if (pos0.y < scissorTL.y) {
			t_start += (scissorTL.y - pos0.y) * dt;
			pos0.y = scissorTL.y;
		}

		if (!gstate.isStencilTestEnabled() &&
			!gstate.isDepthTestEnabled() &&
			!gstate.isLogicOpEnabled() &&
			!gstate.isColorTestEnabled() &&
			!gstate.isDitherEnabled() &&
			gstate.isAlphaTestEnabled() &&
			gstate.getAlphaTestRef() == 0 &&
			gstate.getAlphaTestMask() == 0xFF &&
			gstate.isAlphaBlendEnabled() &&
			gstate.isTextureAlphaUsed() &&
			gstate.getTextureFunction() == GE_TEXFUNC_MODULATE &&
			gstate.getColorMask() == 0x000000 &&
			gstate.FrameBufFormat() == GE_FORMAT_5551) {
			int t = t_start;
			for (int y = pos0.y; y < pos1.y; y++) {
				int s = s_start;
				u16 *pixel = fb.Get16Ptr(pos0.x, y, gstate.FrameBufStride());
				if (isWhite) {
					for (int x = pos0.x; x < pos1.x; x++) {
						u32 tex_color = nearestFunc(s, t, texptr, texbufw, 0);
						if (tex_color & 0xFF000000) {
							DrawSinglePixel5551(pixel, tex_color);
						}
						s += ds;
						pixel++;
					}
				} else {
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = v1.color0;
						Vec4<int> tex_color = Vec4<int>::FromRGBA(nearestFunc(s, t, texptr, texbufw, 0));
						prim_color = ModulateRGBA(prim_color, tex_color);
						if (prim_color.a() > 0) {
							DrawSinglePixel5551(pixel, prim_color.ToRGBA());
						}
						s += ds;
						pixel++;
					}
				}
				t += dt;
			}
		} else {
			int t = t_start;
			for (int y = pos0.y; y < pos1.y; y++) {
				int s = s_start;
				// Not really that fast but faster than triangle.
				for (int x = pos0.x; x < pos1.x; x++) {
					Vec4<int> prim_color = v1.color0;
					Vec4<int> tex_color = Vec4<int>::FromRGBA(nearestFunc(s, t, texptr, texbufw, 0));
					prim_color = GetTextureFunctionOutput(prim_color, tex_color);
					DrawingCoords pos(x, y, z);
					DrawSinglePixelNonClear(pos, (u16)z, 1.0f, prim_color);
					s += ds;
				}
				t += dt;
			}
		}
	} else {
		if (pos1.x > scissorBR.x) pos1.x = scissorBR.x + 1;
		if (pos1.y > scissorBR.y) pos1.y = scissorBR.y + 1;
		if (pos0.x < scissorTL.x) pos0.x = scissorTL.x;
		if (pos0.y < scissorTL.y) pos0.y = scissorTL.y;
		if (!gstate.isStencilTestEnabled() &&
			!gstate.isDepthTestEnabled() &&
			!gstate.isLogicOpEnabled() &&
			!gstate.isColorTestEnabled() &&
			!gstate.isDitherEnabled() &&
			gstate.isAlphaTestEnabled() &&
			gstate.getAlphaTestRef() == 0 &&
			gstate.getAlphaTestMask() == 0xFF &&
			gstate.isAlphaBlendEnabled() &&
			gstate.isTextureAlphaUsed() &&
			gstate.getTextureFunction() == GE_TEXFUNC_MODULATE &&
			gstate.getColorMask() == 0x000000 &&
			gstate.FrameBufFormat() == GE_FORMAT_5551) {
			if (v1.color0.a() == 0)
				return;

			for (int y = pos0.y; y < pos1.y; y++) {
				u16 *pixel = fb.Get16Ptr(pos0.x, y, gstate.FrameBufStride());
				for (int x = pos0.x; x < pos1.x; x++) {
					Vec4<int> prim_color = v1.color0;
					DrawSinglePixel5551(pixel, prim_color.ToRGBA());
					pixel++;
				}
			}
		} else {
			for (int y = pos0.y; y < pos1.y; y++) {
				for (int x = pos0.x; x < pos1.x; x++) {
					Vec4<int> prim_color = v1.color0;
					DrawingCoords pos(x, y, z);
					DrawSinglePixelNonClear(pos, (u16)z, fog, prim_color);
				}
			}
		}
	}
}

bool g_needsClearAfterDialog = false;

// Returns true if the normal path should be skipped.
bool RectangleFastPath(const VertexData &v0, const VertexData &v1) {
	g_DarkStalkerStretch = DSStretch::Off;
	// Check for 1:1 texture mapping. In that case we can call DrawSprite.
	int xdiff = v1.screenpos.x - v0.screenpos.x;
	int ydiff = v1.screenpos.y - v0.screenpos.y;
	int udiff = (v1.texturecoords.x - v0.texturecoords.x) * 16.0f;
	int vdiff = (v1.texturecoords.y - v0.texturecoords.y) * 16.0f;
	bool coord_check =
		(xdiff == udiff || xdiff == -udiff) &&
		(ydiff == vdiff || ydiff == -vdiff);
	// Currently only works for TL/BR, which is the most common but not required.
	bool orient_check = xdiff >= 0 && ydiff >= 0;
	bool state_check = !gstate.isModeClear();  // TODO: Add support for clear modes in Rasterizer::DrawSprite.
	if ((coord_check || !gstate.isTextureMapEnabled()) && orient_check && state_check) {
		Rasterizer::DrawSprite(v0, v1);
		return true;
	}

	// Eliminate the stretch blit in DarkStalkers.
	// We compensate for that when blitting the framebuffer in SoftGpu.cpp.
	if (PSP_CoreParameter().compat.flags().DarkStalkersPresentHack && v0.texturecoords.x == 64.0f && v0.texturecoords.y == 16.0f && v1.texturecoords.x == 448.0f && v1.texturecoords.y == 240.0f) {
		// check for save/load dialog.
		if (!currentDialogActive) {
			if (v0.screenpos.x == 0x7100 && v0.screenpos.y == 0x7780 && v1.screenpos.x == 0x8f00 && v1.screenpos.y == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Wide;
			} else if (v0.screenpos.x == 0x7400 && v0.screenpos.y == 0x7780 && v1.screenpos.x == 0x8C00 && v1.screenpos.y == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Normal;
			} else {
				return false;
			}
			if (g_needsClearAfterDialog) {
				g_needsClearAfterDialog = false;
				// Afterwards, we also need to clear the actual destination. Can do a fast rectfill.
				gstate.textureMapEnable &= ~1;
				VertexData newV1 = v1;
				newV1.color0 = Vec4<int>(0, 0, 0, 255);
				Rasterizer::DrawSprite(v0, newV1);
				gstate.textureMapEnable |= 1;
			}
			return true;
		} else {
			g_needsClearAfterDialog = true;
		}
	}
	return false;
}

bool DetectRectangleFromThroughModeStrip(const VertexData data[4]) {
	// OK, now let's look at data to detect rectangles. There are a few possibilities
	// but we focus on Darkstalkers for now.
	if (data[0].screenpos.x == data[1].screenpos.x &&
		data[0].screenpos.y == data[2].screenpos.y &&
		data[2].screenpos.x == data[3].screenpos.x &&
		data[1].screenpos.y == data[3].screenpos.y &&
		data[1].screenpos.y > data[0].screenpos.y &&  // Avoid rotation handling
		data[2].screenpos.x > data[0].screenpos.x &&
		data[0].texturecoords.x == data[1].texturecoords.x &&
		data[0].texturecoords.y == data[2].texturecoords.y &&
		data[2].texturecoords.x == data[3].texturecoords.x &&
		data[1].texturecoords.y == data[3].texturecoords.y &&
		data[1].texturecoords.y > data[0].texturecoords.y &&
		data[2].texturecoords.x > data[0].texturecoords.x &&
		data[0].color0 == data[1].color0 &&
		data[1].color0 == data[2].color0 &&
		data[2].color0 == data[3].color0) {
		// It's a rectangle!
		return true;
	}
	// There's the other vertex order too...
	if (data[0].screenpos.x == data[2].screenpos.x &&
		data[0].screenpos.y == data[1].screenpos.y &&
		data[1].screenpos.x == data[3].screenpos.x &&
		data[2].screenpos.y == data[3].screenpos.y &&
		data[2].screenpos.y > data[0].screenpos.y &&  // Avoid rotation handling
		data[1].screenpos.x > data[0].screenpos.x &&
		data[0].texturecoords.x == data[2].texturecoords.x &&
		data[0].texturecoords.y == data[1].texturecoords.y &&
		data[1].texturecoords.x == data[3].texturecoords.x &&
		data[2].texturecoords.y == data[3].texturecoords.y &&
		data[2].texturecoords.y > data[0].texturecoords.y &&
		data[1].texturecoords.x > data[0].texturecoords.x &&
		data[0].color0 == data[1].color0 &&
		data[1].color0 == data[2].color0 &&
		data[2].color0 == data[3].color0) {
		// It's a rectangle!
		return true;
	}
	return false;
}


}  // namespace Rasterizer

