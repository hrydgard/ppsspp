// See comment in header for the purpose of the code in this file.

#include <algorithm>
#include <cmath>

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ParallelLoop.h"

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

extern DSStretch g_DarkStalkerStretch;
// For Darkstalkers hack. Ugh.
extern bool currentDialogActive;

namespace Rasterizer {

// Through mode, with the specific Darkstalker settings.
inline void DrawSinglePixel5551(u16 *pixel, const u32 color_in, const PixelFuncID &pixelID) {
	u32 new_color;
	if ((color_in >> 24) == 255) {
		new_color = color_in & 0xFFFFFF;
	} else {
		const u32 old_color = RGBA5551ToRGBA8888(*pixel);
		const Vec4<int> dst = Vec4<int>::FromRGBA(old_color);
		Vec3<int> blended = AlphaBlendingResult(pixelID, Vec4<int>::FromRGBA(color_in), dst);
		// ToRGB() always automatically clamps.
		new_color = blended.ToRGB();
	}
	new_color |= (*pixel & 0x8000) ? 0xff000000 : 0x00000000;
	*pixel = RGBA8888ToRGBA5551(new_color);
}

static inline Vec4IntResult SOFTRAST_CALL ModulateRGBA(Vec4IntArg prim_in, Vec4IntArg texcolor_in) {
	Vec4<int> out;
	Vec4<int> prim_color = prim_in;
	Vec4<int> texcolor = texcolor_in;

#if defined(_M_SSE)
	// Modulate weights slightly on the tex color, by adding one to prim and dividing by 256.
	const __m128i p = _mm_slli_epi16(_mm_packs_epi32(prim_color.ivec, prim_color.ivec), 4);
	const __m128i pboost = _mm_add_epi16(p, _mm_set1_epi16(1 << 4));
	__m128i t = _mm_slli_epi16(_mm_packs_epi32(texcolor.ivec, texcolor.ivec), 4);
	if (gstate.isColorDoublingEnabled()) {
		const __m128i amask = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
		const __m128i a = _mm_and_si128(t, amask);
		const __m128i rgb = _mm_andnot_si128(amask, t);
		t = _mm_or_si128(_mm_slli_epi16(rgb, 1), a);
	}
	const __m128i b = _mm_mulhi_epi16(pboost, t);
	out.ivec = _mm_unpacklo_epi16(b, _mm_setzero_si128());
#else
	if (gstate.isColorDoublingEnabled()) {
		Vec4<int> tex = texcolor * Vec4<int>(2, 2, 2, 1);
		out = ((prim_color + Vec4<int>::AssignToAll(1)) * tex) / 256;
	} else {
		out = (prim_color + Vec4<int>::AssignToAll(1)) * texcolor / 256;
	}
#endif

	return ToVec4IntResult(out);
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
	// Include the ending pixel based on its center, not start.
	DrawingCoords pos1 = TransformUnit::ScreenToDrawing(v1.screenpos + ScreenCoords(7, 7, 0));

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1(), 0);
	DrawingCoords scissorBR(gstate.getScissorX2(), gstate.getScissorY2(), 0);

	int z = pos0.z;
	int fog = 255;

	bool isWhite = v1.color0 == Vec4<int>(255, 255, 255, 255);

	PixelFuncID pixelID;
	ComputePixelFuncID(&pixelID);
	Rasterizer::SingleFunc drawPixel = Rasterizer::GetSingleFunc(pixelID);

	constexpr int MIN_LINES_PER_THREAD = 32;

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

		if (!pixelID.stencilTest &&
			pixelID.DepthTestFunc() == GE_COMP_ALWAYS &&
			!pixelID.applyLogicOp &&
			!pixelID.colorTest &&
			!pixelID.dithering &&
			// TODO: Safe?
			pixelID.AlphaTestFunc() != GE_COMP_ALWAYS &&
			pixelID.alphaTestRef == 0 &&
			!pixelID.hasAlphaTestMask &&
			pixelID.alphaBlend &&
			gstate.isTextureAlphaUsed() &&
			gstate.getTextureFunction() == GE_TEXFUNC_MODULATE &&
			!pixelID.applyColorWriteMask &&
			pixelID.FBFormat() == GE_FORMAT_5551) {
			if (isWhite) {
				ParallelRangeLoop(&g_threadManager, [=](int y1, int y2) {
					int t = t_start + (y1 - pos0.y) * dt;
					for (int y = y1; y < y2; y++) {
						int s = s_start;
						u16 *pixel = fb.Get16Ptr(pos0.x, y, gstate.FrameBufStride());
						for (int x = pos0.x; x < pos1.x; x++) {
							u32 tex_color = Vec4<int>(nearestFunc(s, t, texptr, texbufw, 0)).ToRGBA();
							if (tex_color & 0xFF000000) {
								DrawSinglePixel5551(pixel, tex_color, pixelID);
							}
							s += ds;
							pixel++;
						}
						t += dt;
					}
				}, pos0.y, pos1.y, MIN_LINES_PER_THREAD);
			} else {
				ParallelRangeLoop(&g_threadManager, [=](int y1, int y2) {
					int t = t_start + (y1 - pos0.y) * dt;
					for (int y = y1; y < y2; y++) {
						int s = s_start;
						u16 *pixel = fb.Get16Ptr(pos0.x, y, gstate.FrameBufStride());
						for (int x = pos0.x; x < pos1.x; x++) {
							Vec4<int> prim_color = v1.color0;
							Vec4<int> tex_color = nearestFunc(s, t, texptr, texbufw, 0);
							prim_color = Vec4<int>(ModulateRGBA(ToVec4IntArg(prim_color), ToVec4IntArg(tex_color)));
							if (prim_color.a() > 0) {
								DrawSinglePixel5551(pixel, prim_color.ToRGBA(), pixelID);
							}
							s += ds;
							pixel++;
						}
						t += dt;
					}
				}, pos0.y, pos1.y, MIN_LINES_PER_THREAD);
			}
		} else {
			ParallelRangeLoop(&g_threadManager, [=](int y1, int y2) {
				int t = t_start + (y1 - pos0.y) * dt;
				for (int y = y1; y < y2; y++) {
					int s = s_start;
					// Not really that fast but faster than triangle.
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = v1.color0;
						Vec4<int> tex_color = nearestFunc(s, t, texptr, texbufw, 0);
						prim_color = GetTextureFunctionOutput(ToVec4IntArg(prim_color), ToVec4IntArg(tex_color));
						drawPixel(x, y, z, 255, ToVec4IntArg(prim_color), pixelID);
						s += ds;
					}
					t += dt;
				}
			}, pos0.y, pos1.y, MIN_LINES_PER_THREAD);
		}
	} else {
		if (pos1.x > scissorBR.x) pos1.x = scissorBR.x + 1;
		if (pos1.y > scissorBR.y) pos1.y = scissorBR.y + 1;
		if (pos0.x < scissorTL.x) pos0.x = scissorTL.x;
		if (pos0.y < scissorTL.y) pos0.y = scissorTL.y;
		if (!pixelID.stencilTest &&
			pixelID.DepthTestFunc() == GE_COMP_ALWAYS &&
			!pixelID.applyLogicOp &&
			!pixelID.colorTest &&
			!pixelID.dithering &&
			// TODO: Safe?
			pixelID.AlphaTestFunc() != GE_COMP_ALWAYS &&
			pixelID.alphaTestRef == 0 &&
			!pixelID.hasAlphaTestMask &&
			pixelID.alphaBlend &&
			gstate.isTextureAlphaUsed() &&
			gstate.getTextureFunction() == GE_TEXFUNC_MODULATE &&
			!pixelID.applyColorWriteMask &&
			pixelID.FBFormat() == GE_FORMAT_5551) {
			if (v1.color0.a() == 0)
				return;

			ParallelRangeLoop(&g_threadManager, [=](int y1, int y2) {
				for (int y = y1; y < y2; y++) {
					u16 *pixel = fb.Get16Ptr(pos0.x, y, gstate.FrameBufStride());
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = v1.color0;
						DrawSinglePixel5551(pixel, prim_color.ToRGBA(), pixelID);
						pixel++;
					}
				}
			}, pos0.y, pos1.y, MIN_LINES_PER_THREAD);
		} else {
			ParallelRangeLoop(&g_threadManager, [=](int y1, int y2) {
				for (int y = y1; y < y2; y++) {
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = v1.color0;
						drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
					}
				}
			}, pos0.y, pos1.y, MIN_LINES_PER_THREAD);
		}
	}
}

bool g_needsClearAfterDialog = false;

static inline bool NoClampOrWrap(const Vec2f &tc) {
	if (tc.x < 0 || tc.y < 0)
		return false;
	return tc.x <= gstate.getTextureWidth(0) && tc.y <= gstate.getTextureHeight(0);
}

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
	// We already have a fast path for clear in ClearRectangle.
	bool state_check = !gstate.isModeClear() && NoClampOrWrap(v0.texturecoords) && NoClampOrWrap(v1.texturecoords);
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
	// We'll only do this when the color is flat.
	if (!(data[0].color0 == data[1].color0))
		return false;
	if (!(data[1].color0 == data[2].color0))
		return false;
	if (!(data[2].color0 == data[3].color0))
		return false;

	// And the depth must also be flat.
	if (!(data[0].screenpos.z == data[1].screenpos.z))
		return false;
	if (!(data[1].screenpos.z == data[2].screenpos.z))
		return false;
	if (!(data[2].screenpos.z == data[3].screenpos.z))
		return false;

	// OK, now let's look at data to detect rectangles. There are a few possibilities
	// but we focus on Darkstalkers for now.
	if (data[0].screenpos.x == data[1].screenpos.x &&
		data[0].screenpos.y == data[2].screenpos.y &&
		data[2].screenpos.x == data[3].screenpos.x &&
		data[1].screenpos.y == data[3].screenpos.y &&
		data[1].screenpos.y > data[0].screenpos.y &&
		data[2].screenpos.x > data[0].screenpos.x) {
		// Okay, this is in the shape of a triangle, but what about rotation/texture?
		if (!gstate.isTextureMapEnabled())
			return true;

		if (data[0].texturecoords.x == data[1].texturecoords.x &&
			data[0].texturecoords.y == data[2].texturecoords.y &&
			data[2].texturecoords.x == data[3].texturecoords.x &&
			data[1].texturecoords.y == data[3].texturecoords.y &&
			data[1].texturecoords.y > data[0].texturecoords.y &&
			data[2].texturecoords.x > data[0].texturecoords.x) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	// There's the other vertex order too...
	if (data[0].screenpos.x == data[2].screenpos.x &&
		data[0].screenpos.y == data[1].screenpos.y &&
		data[1].screenpos.x == data[3].screenpos.x &&
		data[2].screenpos.y == data[3].screenpos.y &&
		data[2].screenpos.y > data[0].screenpos.y &&
		data[1].screenpos.x > data[0].screenpos.x) {
		// Okay, this is in the shape of a triangle, but what about rotation/texture?
		if (!gstate.isTextureMapEnabled())
			return true;

		if (data[0].texturecoords.x == data[2].texturecoords.x &&
			data[0].texturecoords.y == data[1].texturecoords.y &&
			data[1].texturecoords.x == data[3].texturecoords.x &&
			data[2].texturecoords.y == data[3].texturecoords.y &&
			data[2].texturecoords.y > data[0].texturecoords.y &&
			data[1].texturecoords.x > data[0].texturecoords.x) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	return false;
}

bool DetectRectangleFromThroughModeFan(const VertexData *data, int c, int *tlIndex, int *brIndex) {
	// Color and Z must be flat.
	for (int i = 1; i < c; ++i) {
		if (!(data[i].color0 == data[0].color0))
			return false;
		if (!(data[i].screenpos.z == data[0].screenpos.z))
			return false;
	}

	// Check for the common case: a single TL-TR-BR-BL.
	if (c == 4) {
		const auto &tl = data[0].screenpos, &tr = data[1].screenpos;
		const auto &bl = data[3].screenpos, &br = data[2].screenpos;
		if (tl.x == bl.x && tr.x == br.x && tl.y == tr.y && bl.y == br.y) {
			// Looking like yes.  Set TL/BR based on y order first...
			*tlIndex = tl.y > bl.y ? 2 : 0;
			*brIndex = tl.y > bl.y ? 0 : 2;
			// And if it's horizontally flipped, trade to the actual TL/BR.
			if (tl.x > tr.x) {
				*tlIndex ^= 1;
				*brIndex ^= 1;
			}

			// Do we need to think about rotation?
			if (!gstate.isTextureMapEnabled())
				return true;

			const auto &textl = data[*tlIndex].texturecoords, &textr = data[*tlIndex ^ 1].texturecoords;
			const auto &texbl = data[*brIndex ^ 1].texturecoords, &texbr = data[*brIndex].texturecoords;

			if (textl.x == texbl.x && textr.x == texbr.x && textl.y == textr.y && texbl.y == texbr.y) {
				// Okay, the texture is also good, but let's avoid rotation issues.
				return textl.y < texbr.y && textl.x < texbr.x;
			}
		}
	}

	return false;
}

bool DetectRectangleSlices(const VertexData data[4]) {
	// Color and Z must be flat.
	for (int i = 1; i < 4; ++i) {
		if (!(data[i].color0 == data[0].color0))
			return false;
		if (!(data[i].screenpos.z == data[0].screenpos.z))
			return false;
	}

	// Games very commonly use vertical strips of rectangles.  Detect and combine.
	const auto &tl1 = data[0].screenpos, &br1 = data[1].screenpos;
	const auto &tl2 = data[2].screenpos, &br2 = data[3].screenpos;
	if (tl1.y == tl2.y && br1.y == br2.y && br1.y > tl1.y) {
		if (br1.x == tl2.x && tl1.x < br1.x && tl2.x < br2.x) {
			if (!gstate.isTextureMapEnabled() || gstate.isModeClear())
				return true;

			const auto &textl1 = data[0].texturecoords, &texbr1 = data[1].texturecoords;
			const auto &textl2 = data[2].texturecoords, &texbr2 = data[3].texturecoords;
			if (textl1.y != textl2.y || texbr1.y != texbr2.y || textl1.y > texbr1.y)
				return false;
			if (texbr1.x != textl2.x || textl1.x > texbr1.x || textl2.x > texbr2.x)
				return false;

			// We might be able to compare ratios, but let's expect 1:1.
			int texdiff1 = (texbr1.x - textl1.x) * 16.0f;
			int texdiff2 = (texbr2.x - textl2.x) * 16.0f;
			int posdiff1 = br1.x - tl1.x;
			int posdiff2 = br2.x - tl2.x;
			return texdiff1 == posdiff1 && texdiff2 == posdiff2;
		}
	}

	return false;
}

}  // namespace Rasterizer

