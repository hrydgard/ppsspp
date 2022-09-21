// See comment in header for the purpose of the code in this file.

#include <algorithm>
#include <cmath>

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Software/BinManager.h"
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

static inline Vec4IntResult SOFTRAST_CALL ModulateRGBA(Vec4IntArg prim_in, Vec4IntArg texcolor_in, const SamplerID &samplerID) {
	Vec4<int> out;
	Vec4<int> prim_color = prim_in;
	Vec4<int> texcolor = texcolor_in;

#if defined(_M_SSE)
	// Modulate weights slightly on the tex color, by adding one to prim and dividing by 256.
	const __m128i p = _mm_slli_epi16(_mm_packs_epi32(prim_color.ivec, prim_color.ivec), 4);
	const __m128i pboost = _mm_add_epi16(p, _mm_set1_epi16(1 << 4));
	__m128i t = _mm_slli_epi16(_mm_packs_epi32(texcolor.ivec, texcolor.ivec), 4);
	if (samplerID.useColorDoubling) {
		const __m128i amask = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
		const __m128i a = _mm_and_si128(t, amask);
		const __m128i rgb = _mm_andnot_si128(amask, t);
		t = _mm_or_si128(_mm_slli_epi16(rgb, 1), a);
	}
	const __m128i b = _mm_mulhi_epi16(pboost, t);
	out.ivec = _mm_unpacklo_epi16(b, _mm_setzero_si128());
#else
	if (samplerID.useColorDoubling) {
		Vec4<int> tex = texcolor * Vec4<int>(2, 2, 2, 1);
		out = ((prim_color + Vec4<int>::AssignToAll(1)) * tex) / 256;
	} else {
		out = (prim_color + Vec4<int>::AssignToAll(1)) * texcolor / 256;
	}
#endif

	return ToVec4IntResult(out);
}

// Check if we can safely ignore the alpha test.
static inline bool AlphaTestIsNeedless(const PixelFuncID &pixelID) {
	switch (pixelID.AlphaTestFunc()) {
	case GE_COMP_NEVER:
	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
	case GE_COMP_LEQUAL:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_NOTEQUAL:
	case GE_COMP_GREATER:
	case GE_COMP_GEQUAL:
		if (pixelID.alphaTestRef != 0 || pixelID.hasAlphaTestMask)
			return false;
		// DrawSinglePixel5551 assumes it can take the src color directly if full alpha.
		return pixelID.alphaBlend && pixelID.AlphaBlendSrc() == PixelBlendFactor::SRCALPHA && pixelID.AlphaBlendDst() == PixelBlendFactor::INVSRCALPHA;
	}

	return false;
}

void DrawSprite(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state) {
	const u8 *texptr = state.texptr[0];

	GETextureFormat texfmt = state.samplerID.TexFmt();
	uint16_t texbufw = state.texbufw[0];

	Sampler::FetchFunc fetchFunc = Sampler::GetFetchFunc(state.samplerID);
	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	DrawingCoords pos0 = TransformUnit::ScreenToDrawing(v0.screenpos);
	// Include the ending pixel based on its center, not start.
	DrawingCoords pos1 = TransformUnit::ScreenToDrawing(v1.screenpos + ScreenCoords(7, 7, 0));

	DrawingCoords scissorTL = TransformUnit::ScreenToDrawing(range.x1, range.y1);
	DrawingCoords scissorBR = TransformUnit::ScreenToDrawing(range.x2, range.y2);

	const int z = v1.screenpos.z;
	constexpr int fog = 255;

	// Since it's flat, we can check depth range early.  Matters for earlyZChecks.
	if (pixelID.applyDepthRange && (z < pixelID.cached.minz || z > pixelID.cached.maxz))
		return;

	bool isWhite = v1.color0 == 0xFFFFFFFF;

	if (state.enableTextures) {
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
			pixelID.alphaBlend &&
			AlphaTestIsNeedless(pixelID) &&
			samplerID.useTextureAlpha &&
			samplerID.TexFunc() == GE_TEXFUNC_MODULATE &&
			!pixelID.applyColorWriteMask &&
			pixelID.FBFormat() == GE_FORMAT_5551) {
			if (isWhite) {
				int t = t_start;
				for (int y = pos0.y; y < pos1.y; y++) {
					int s = s_start;
					u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
					for (int x = pos0.x; x < pos1.x; x++) {
						u32 tex_color = Vec4<int>(fetchFunc(s, t, texptr, texbufw, 0, state.samplerID)).ToRGBA();
						if (tex_color & 0xFF000000) {
							DrawSinglePixel5551(pixel, tex_color, pixelID);
						}
						s += ds;
						pixel++;
					}
					t += dt;
				}
			} else {
				int t = t_start;
				const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
				for (int y = pos0.y; y < pos1.y; y++) {
					int s = s_start;
					u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = c0;
						Vec4<int> tex_color = fetchFunc(s, t, texptr, texbufw, 0, state.samplerID);
						prim_color = Vec4<int>(ModulateRGBA(ToVec4IntArg(prim_color), ToVec4IntArg(tex_color), state.samplerID));
						if (prim_color.a() > 0) {
							DrawSinglePixel5551(pixel, prim_color.ToRGBA(), pixelID);
						}
						s += ds;
						pixel++;
					}
					t += dt;
				}
			}
		} else {
			int xoff = ((v0.screenpos.x & 15) + 1) / 2;
			int yoff = ((v0.screenpos.y & 15) + 1) / 2;

			float dsf = ds * (1.0f / (float)(1 << state.samplerID.width0Shift));
			float dtf = dt * (1.0f / (float)(1 << state.samplerID.height0Shift));
			float sf_start = s_start * (1.0f / (float)(1 << state.samplerID.width0Shift));
			float tf_start = t_start * (1.0f / (float)(1 << state.samplerID.height0Shift));

			float t = tf_start;
			const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
			if (pixelID.earlyZChecks) {
				for (int y = pos0.y; y < pos1.y; y++) {
					float s = sf_start;
					// Not really that fast but faster than triangle.
					for (int x = pos0.x; x < pos1.x; x++) {
						if (CheckDepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z)) {
							Vec4<int> prim_color = state.nearest(s, t, xoff, yoff, ToVec4IntArg(c0), &texptr, &texbufw, 0, 0, state.samplerID);
							state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
						}

						s += dsf;
					}
					t += dtf;
				}
			} else {
				for (int y = pos0.y; y < pos1.y; y++) {
					float s = sf_start;
					// Not really that fast but faster than triangle.
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = state.nearest(s, t, xoff, yoff, ToVec4IntArg(c0), &texptr, &texbufw, 0, 0, state.samplerID);
						state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
						s += dsf;
					}
					t += dtf;
				}
			}
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
			pixelID.alphaBlend &&
			AlphaTestIsNeedless(pixelID) &&
			!pixelID.applyColorWriteMask &&
			pixelID.FBFormat() == GE_FORMAT_5551) {
			if (Vec4<int>::FromRGBA(v1.color0).a() == 0)
				return;

			for (int y = pos0.y; y < pos1.y; y++) {
				u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
				for (int x = pos0.x; x < pos1.x; x++) {
					DrawSinglePixel5551(pixel, v1.color0, pixelID);
					pixel++;
				}
			}
		} else if (pixelID.earlyZChecks) {
			const Vec4<int> prim_color = Vec4<int>::FromRGBA(v1.color0);
			for (int y = pos0.y; y < pos1.y; y++) {
				for (int x = pos0.x; x < pos1.x; x++) {
					if (!CheckDepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z))
						continue;

					state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
				}
			}
		} else {
			const Vec4<int> prim_color = Vec4<int>::FromRGBA(v1.color0);
			for (int y = pos0.y; y < pos1.y; y++) {
				for (int x = pos0.x; x < pos1.x; x++) {
					state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
				}
			}
		}
	}

#if defined(SOFTGPU_MEMORY_TAGGING_BASIC) || defined(SOFTGPU_MEMORY_TAGGING_DETAILED)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListR_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListRZ_%08x", state.listPC);

	for (int y = pos0.y; y < pos1.y; y++) {
		uint32_t row = gstate.getFrameBufAddress() + y * pixelID.cached.framebufStride * bpp;
		NotifyMemInfo(MemBlockFlags::WRITE, row + pos0.x * bpp, (pos1.x - pos0.x) * bpp, tag.c_str(), tag.size());
	}
#endif
}

bool g_needsClearAfterDialog = false;

static inline bool NoClampOrWrap(const RasterizerState &state, const Vec2f &tc) {
	if (tc.x < 0 || tc.y < 0)
		return false;
	if (state.samplerID.cached.sizes[0].w > 512 || state.samplerID.cached.sizes[0].h > 512)
		return false;
	return tc.x <= state.samplerID.cached.sizes[0].w && tc.y <= state.samplerID.cached.sizes[0].h;
}

// Returns true if the normal path should be skipped.
bool RectangleFastPath(const VertexData &v0, const VertexData &v1, BinManager &binner) {
	const RasterizerState &state = binner.State();

	g_DarkStalkerStretch = DSStretch::Off;
	// Check for 1:1 texture mapping. In that case we can call DrawSprite.
	int xdiff = v1.screenpos.x - v0.screenpos.x;
	int ydiff = v1.screenpos.y - v0.screenpos.y;
	int udiff = (v1.texturecoords.x - v0.texturecoords.x) * (float)SCREEN_SCALE_FACTOR;
	int vdiff = (v1.texturecoords.y - v0.texturecoords.y) * (float)SCREEN_SCALE_FACTOR;
	bool coord_check =
		(xdiff == udiff || xdiff == -udiff) &&
		(ydiff == vdiff || ydiff == -vdiff);
	// Currently only works for TL/BR, which is the most common but not required.
	bool orient_check = xdiff >= 0 && ydiff >= 0;
	// We already have a fast path for clear in ClearRectangle.
	bool state_check = state.throughMode && !state.pixelID.clearMode && !state.samplerID.hasAnyMips && NoClampOrWrap(state, v0.texturecoords) && NoClampOrWrap(state, v1.texturecoords);
	// This doesn't work well with offset drawing, see #15876.  Through never has a subpixel offset.
	bool subpixel_check = ((v0.screenpos.x | v0.screenpos.y | v1.screenpos.x | v1.screenpos.y) & 0xF) == 0;
	if ((coord_check || !state.enableTextures) && orient_check && state_check && subpixel_check) {
		binner.AddSprite(v0, v1);
		return true;
	}

	// Eliminate the stretch blit in DarkStalkers.
	// We compensate for that when blitting the framebuffer in SoftGpu.cpp.
	if (PSP_CoreParameter().compat.flags().DarkStalkersPresentHack && v0.texturecoords.x == 64.0f && v0.texturecoords.y == 16.0f && v1.texturecoords.x == 448.0f && v1.texturecoords.y == 240.0f) {
		// check for save/load dialog.
		if (!currentDialogActive) {
			if (v0.screenpos.x + gstate.getOffsetX16() == 0x7100 && v0.screenpos.y + gstate.getOffsetY16() == 0x7780 && v1.screenpos.x + gstate.getOffsetX16() == 0x8f00 && v1.screenpos.y + gstate.getOffsetY16() == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Wide;
			} else if (v0.screenpos.x + gstate.getOffsetX16() == 0x7400 && v0.screenpos.y + gstate.getOffsetY16() == 0x7780 && v1.screenpos.x + gstate.getOffsetX16() == 0x8C00 && v1.screenpos.y + gstate.getOffsetY16() == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Normal;
			} else {
				return false;
			}
			if (g_needsClearAfterDialog) {
				g_needsClearAfterDialog = false;
				// Afterwards, we also need to clear the actual destination. Can do a fast rectfill.
				gstate.textureMapEnable &= ~1;
				VertexData newV1 = v1;
				newV1.color0 = 0xFF000000;
				binner.AddSprite(v0, newV1);
				gstate.textureMapEnable |= 1;
			}
			return true;
		} else {
			g_needsClearAfterDialog = true;
		}
	}
	return false;
}

static bool AreCoordsRectangleCompatible(const RasterizerState &state, const VertexData &data0, const VertexData &data1) {
	if (data1.color0 != data0.color0)
		return false;
	if (data1.screenpos.z != data0.screenpos.z) {
		// Sometimes, we don't actually care about z.
		if (state.pixelID.depthWrite || state.pixelID.DepthTestFunc() != GE_COMP_ALWAYS)
			return false;
	}
	if (!state.throughMode) {
		if (data1.color1 != data0.color1)
			return false;
		// This means it should be culled, outside range.
		if (data1.OutsideRange() || data0.OutsideRange())
			return false;
		// Do we have to think about perspective correction or slope mip level?
		if (state.enableTextures && data1.clippos.w != data0.clippos.w) {
			// If the w is off by less than a factor of 1/512, it should be safe to treat as a rectangle.
			static constexpr float halftexel = 0.5f / 512.0f;
			if (data1.clippos.w - halftexel > data0.clippos.w || data1.clippos.w + halftexel < data0.clippos.w)
				return false;
		}
		if (state.pixelID.applyFog && data1.fogdepth != data0.fogdepth) {
			// Similar to w, this only matters if they're farther apart than 1/255.
			static constexpr float foghalfstep = 0.5f / 255.0f;
			if (data1.fogdepth - foghalfstep > data0.fogdepth || data1.fogdepth + foghalfstep < data0.fogdepth)
				return false;
		}
	}
	return true;
}

bool DetectRectangleFromStrip(const RasterizerState &state, const VertexData data[4], int *tlIndex, int *brIndex) {
	// Color and Z must be flat.  Also find the TL and BR meanwhile.
	int tl = 0, br = 0;
	for (int i = 1; i < 4; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;

		if (data[i].screenpos.x <= data[tl].screenpos.x && data[i].screenpos.y <= data[tl].screenpos.y)
			tl = i;
		if (data[i].screenpos.x >= data[br].screenpos.x && data[i].screenpos.y >= data[br].screenpos.y)
			br = i;
	}

	*tlIndex = tl;
	*brIndex = br;

	// OK, now let's look at data to detect rectangles. There are a few possibilities
	// but we focus on Darkstalkers for now.
	if (data[0].screenpos.x == data[1].screenpos.x &&
		data[0].screenpos.y == data[2].screenpos.y &&
		data[2].screenpos.x == data[3].screenpos.x &&
		data[1].screenpos.y == data[3].screenpos.y) {
		// Okay, this is in the shape of a rectangle, but what about texture?
		if (!state.enableTextures)
			return true;

		if (data[0].texturecoords.x == data[1].texturecoords.x &&
			data[0].texturecoords.y == data[2].texturecoords.y &&
			data[2].texturecoords.x == data[3].texturecoords.x &&
			data[1].texturecoords.y == data[3].texturecoords.y) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	// There's the other vertex order too...
	if (data[0].screenpos.x == data[2].screenpos.x &&
		data[0].screenpos.y == data[1].screenpos.y &&
		data[1].screenpos.x == data[3].screenpos.x &&
		data[2].screenpos.y == data[3].screenpos.y) {
		// Okay, this is in the shape of a rectangle, but what about texture?
		if (!state.enableTextures)
			return true;

		if (data[0].texturecoords.x == data[2].texturecoords.x &&
			data[0].texturecoords.y == data[1].texturecoords.y &&
			data[1].texturecoords.x == data[3].texturecoords.x &&
			data[2].texturecoords.y == data[3].texturecoords.y) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	return false;
}

bool DetectRectangleFromFan(const RasterizerState &state, const VertexData *data, int c, int *tlIndex, int *brIndex) {
	// Color and Z must be flat.
	for (int i = 1; i < c; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;
	}

	// Check for the common case: a single TL-TR-BR-BL.
	if (c == 4) {
		const auto &pos0 = data[0].screenpos, &pos1 = data[1].screenpos;
		const auto &pos2 = data[2].screenpos, &pos3 = data[3].screenpos;
		if (pos0.x == pos3.x && pos1.x == pos2.x && pos0.y == pos1.y && pos3.y == pos2.y) {
			// Looking like yes.  Set TL/BR based on y order first...
			*tlIndex = pos0.y > pos3.y ? 2 : 0;
			*brIndex = pos0.y > pos3.y ? 0 : 2;
			// And if it's horizontally flipped, trade to the actual TL/BR.
			if (pos0.x > pos1.x) {
				*tlIndex ^= 1;
				*brIndex ^= 1;
			}

			// Do we need to think about rotation?
			if (!state.enableTextures)
				return true;

			const auto &textl = data[*tlIndex].texturecoords, &textr = data[*tlIndex ^ 1].texturecoords;
			const auto &texbl = data[*brIndex ^ 1].texturecoords, &texbr = data[*brIndex].texturecoords;

			if (textl.x == texbl.x && textr.x == texbr.x && textl.y == textr.y && texbl.y == texbr.y) {
				// Okay, the texture is also good, but let's avoid rotation issues.
				const auto &postl = data[*tlIndex].screenpos;
				const auto &posbr = data[*brIndex].screenpos;
				return textl.y < texbr.y && postl.y < posbr.y && textl.x < texbr.x && postl.x < posbr.x;
			}
		}
	}

	return false;
}

bool DetectRectangleFromPair(const RasterizerState &state, const VertexData data[6], int *tlIndex, int *brIndex) {
	// Color and Z must be flat.  Also find the TL and BR meanwhile.
	int tl = 0, br = 0;
	for (int i = 1; i < 6; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;

		if (data[i].screenpos.x <= data[tl].screenpos.x && data[i].screenpos.y <= data[tl].screenpos.y)
			tl = i;
		if (data[i].screenpos.x >= data[br].screenpos.x && data[i].screenpos.y >= data[br].screenpos.y)
			br = i;
	}

	*tlIndex = tl;
	*brIndex = br;

	auto xat = [&](int i) { return data[i].screenpos.x; };
	auto yat = [&](int i) { return data[i].screenpos.y; };
	auto uat = [&](int i) { return data[i].texturecoords.x; };
	auto vat = [&](int i) { return data[i].texturecoords.y; };

	// A likely order would be: TL, TR, BR, TL, BR, BL.  We'd have the last index of each.
	// TODO: Make more generic.
	if (tl == 3 && br == 4) {
		bool x1_match = xat(0) == xat(3) && xat(0) == xat(5);
		bool x2_match = xat(1) == xat(2) && xat(1) == xat(4);
		bool y1_match = yat(0) == yat(1) && yat(0) == yat(3);
		bool y2_match = yat(2) == yat(4) && yat(2) == yat(5);
		if (x1_match && y1_match && x2_match && y2_match) {
			// Do we need to think about rotation or UVs?
			if (!state.enableTextures)
				return true;

			x1_match = uat(0) == uat(3) && uat(0) == uat(5);
			x2_match = uat(1) == uat(2) && uat(1) == uat(4);
			y1_match = vat(0) == vat(1) && vat(0) == vat(3);
			y2_match = vat(2) == vat(4) && vat(2) == vat(5);
			if (x1_match && y1_match && x2_match && y2_match) {
				// Double check rotation direction.
				return vat(tl) < vat(br) && yat(tl) < yat(br) && uat(tl) < uat(br) && xat(tl) < xat(br);
			}
		}
	}

	return false;
}

bool DetectRectangleThroughModeSlices(const RasterizerState &state, const VertexData data[4]) {
	// Color and Z must be flat.
	for (int i = 1; i < 4; ++i) {
		if (!(data[i].color0 == data[0].color0))
			return false;
		if (!(data[i].screenpos.z == data[0].screenpos.z)) {
			// Sometimes, we don't actually care about z.
			if (state.pixelID.depthWrite || state.pixelID.DepthTestFunc() != GE_COMP_ALWAYS)
				return false;
		}
	}

	// Games very commonly use vertical strips of rectangles.  Detect and combine.
	const auto &tl1 = data[0].screenpos, &br1 = data[1].screenpos;
	const auto &tl2 = data[2].screenpos, &br2 = data[3].screenpos;
	if (tl1.y == tl2.y && br1.y == br2.y && br1.y > tl1.y) {
		if (br1.x == tl2.x && tl1.x < br1.x && tl2.x < br2.x) {
			if (!state.enableTextures)
				return true;

			const auto &textl1 = data[0].texturecoords, &texbr1 = data[1].texturecoords;
			const auto &textl2 = data[2].texturecoords, &texbr2 = data[3].texturecoords;
			if (textl1.y != textl2.y || texbr1.y != texbr2.y || textl1.y > texbr1.y)
				return false;
			if (texbr1.x != textl2.x || textl1.x > texbr1.x || textl2.x > texbr2.x)
				return false;

			// We might be able to compare ratios, but let's expect 1:1.
			int texdiff1 = (texbr1.x - textl1.x) * (float)SCREEN_SCALE_FACTOR;
			int texdiff2 = (texbr2.x - textl2.x) * (float)SCREEN_SCALE_FACTOR;
			int posdiff1 = br1.x - tl1.x;
			int posdiff2 = br2.x - tl2.x;
			return texdiff1 == posdiff1 && texdiff2 == posdiff2;
		}
	}

	return false;
}

}  // namespace Rasterizer

