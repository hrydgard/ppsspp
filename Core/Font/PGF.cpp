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

// ============== NOTE!!!!

// Thanks to the JPCSP project! This sceFont implementation is basically a C++ take on JPCSP's font code.
// Some parts, especially in this file, were simply copied, so I guess this really makes this file GPL3.

#include "Common/ChunkFile.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/Font/PGF.h"
#include "Core/HLE/HLE.h"

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

// These fonts, created by ttf2pgf, don't have complete glyph info and need to be identified.
static bool isJPCSPFont(const char *fontName) {
	return !strcmp(fontName, "Liberation Sans") || !strcmp(fontName, "Liberation Serif") || !strcmp(fontName, "Sazanami") || !strcmp(fontName, "UnDotum") || !strcmp(fontName, "Microsoft YaHei");
}

// Gets a number of bits from an offset.
static int getBits(int numBits, const u8 *buf, size_t pos) {
	_dbg_assert_msg_(SCEFONT, numBits <= 32, "Unable to return more than 32 bits, %d requested", numBits);

	const size_t wordpos = pos >> 5;
	const u32 *wordbuf = (const u32 *)buf;
	const u8 bitoff = pos & 31;

	// Might just be in one, has to be within two.
	if (bitoff + numBits < 32) {
		const u32 mask = (1 << numBits) - 1;
		return (wordbuf[wordpos] >> bitoff) & mask;
	} else {
		int v = wordbuf[wordpos] >> bitoff;

		const u8 done = 32 - bitoff;
		const u8 remaining = numBits - done;
		const u32 mask = (1 << remaining) - 1;
		v |= (wordbuf[wordpos + 1] & mask) << done;
		return v;
	}
}

static inline int consumeBits(int numBits, const u8 *buf, size_t &pos) {
	int v = getBits(numBits, buf, pos);
	pos += numBits;
	return v;
}

static std::vector<int> getTable(const u8 *buf, int bpe, size_t length) {
	std::vector<int> vec;
	vec.resize(length);
	for (size_t i = 0; i < length; i++) {
		vec[i] = getBits(bpe, buf, bpe * i);
	}
	return vec;
}

PGF::PGF()
	: fontData(0) {

}

PGF::~PGF() {
	if (fontData) {
		delete [] fontData;
	}
}

struct GlyphFromPGF1State {
	int x;
	int y;
	int w;
	int h;
	int left;
	int top;
	int flags;
	int shadowID;
	int advanceH;
	int advanceV;
	int dimensionWidth, dimensionHeight;
	int xAdjustH, xAdjustV;
	int yAdjustH, yAdjustV;
	u32 ptr;

	operator Glyph() {
		Glyph ret;
		ret.w = w;
		ret.h = h;
		ret.left = left;
		ret.top = top;
		ret.flags = flags;
		// Wasn't read before.
		ret.shadowFlags = 0;
		ret.shadowID = shadowID;
		ret.advanceH = advanceH;
		ret.advanceV = advanceV;
		ret.dimensionWidth = dimensionWidth;
		ret.dimensionHeight = dimensionHeight;
		ret.xAdjustH = xAdjustH;
		ret.xAdjustV = xAdjustV;
		ret.yAdjustH = yAdjustH;
		ret.yAdjustV = yAdjustV;
		ret.ptr = ptr;
		return ret;
	}
};

void PGF::DoState(PointerWrap &p) {
	auto s = p.Section("PGF", 1, 2);
	if (!s)
		return;

	p.Do(header);
	p.Do(rev3extra);

	// Don't savestate size_t directly, 32-bit and 64-bit are different.
	u32 fontDataSizeTemp = (u32)fontDataSize;
	p.Do(fontDataSizeTemp);
	fontDataSize = (size_t)fontDataSizeTemp;
	if (p.mode == p.MODE_READ) {
		if (fontData) {
			delete [] fontData;
		}
		if (fontDataSize) {
			fontData = new u8[fontDataSize];
			p.DoArray(fontData, (int)fontDataSize);
		}
	} else if (fontDataSize) {
		p.DoArray(fontData, (int)fontDataSize);
	}
	p.Do(fileName);

	p.DoArray(dimensionTable, ARRAY_SIZE(dimensionTable));
	p.DoArray(xAdjustTable, ARRAY_SIZE(xAdjustTable));
	p.DoArray(yAdjustTable, ARRAY_SIZE(yAdjustTable));
	p.DoArray(advanceTable, ARRAY_SIZE(advanceTable));
	p.DoArray(charmapCompressionTable1, ARRAY_SIZE(charmapCompressionTable1));
	p.DoArray(charmapCompressionTable2, ARRAY_SIZE(charmapCompressionTable2));

	p.Do(charmap_compr);
	p.Do(charmap);
	if (s == 1) {
		std::vector<GlyphFromPGF1State> oldGlyphs;
		p.Do(oldGlyphs);
		glyphs.resize(oldGlyphs.size());
		for (size_t i = 0; i < oldGlyphs.size(); ++i) {
			glyphs[i] = oldGlyphs[i];
		}
		p.Do(oldGlyphs);
		shadowGlyphs.resize(oldGlyphs.size());
		for (size_t i = 0; i < oldGlyphs.size(); ++i) {
			shadowGlyphs[i] = oldGlyphs[i];
		}
	} else {
		p.Do(glyphs);
		p.Do(shadowGlyphs);
	}
	p.Do(firstGlyph);
}

bool PGF::ReadPtr(const u8 *ptr, size_t dataSize) {
	const u8 *const startPtr = ptr;

	if (dataSize < sizeof(header)) {
		return false;
	}

	DEBUG_LOG(SCEFONT, "Reading %d bytes of PGF header", (int)sizeof(header));
	memcpy(&header, ptr, sizeof(header));
	ptr += sizeof(header);

	fileName = header.fontName;

	if (header.revision == 3) {
		memcpy(&rev3extra, ptr, sizeof(rev3extra));
		rev3extra.compCharMapLength1 &= 0xFFFF;
		rev3extra.compCharMapLength2 &= 0xFFFF;
		ptr += sizeof(rev3extra);
	}

	const u32_le *wptr = (const u32_le *)ptr;
	dimensionTable[0].resize(header.dimTableLength);
	dimensionTable[1].resize(header.dimTableLength);
	for (int i = 0; i < header.dimTableLength; i++) {
		dimensionTable[0][i] = *wptr++;
		dimensionTable[1][i] = *wptr++;
	}

	xAdjustTable[0].resize(header.xAdjustTableLength);
	xAdjustTable[1].resize(header.xAdjustTableLength);
	for (int i = 0; i < header.xAdjustTableLength; i++) {
		xAdjustTable[0][i] = *wptr++;
		xAdjustTable[1][i] = *wptr++;
	}

	yAdjustTable[0].resize(header.yAdjustTableLength);
	yAdjustTable[1].resize(header.yAdjustTableLength);
	for (int i = 0; i < header.yAdjustTableLength; i++) {
		yAdjustTable[0][i] = *wptr++;
		yAdjustTable[1][i] = *wptr++;
	}

	advanceTable[0].resize(header.advanceTableLength);
	advanceTable[1].resize(header.advanceTableLength);
	for (int i = 0; i < header.advanceTableLength; i++) {
		advanceTable[0][i] = *wptr++;
		advanceTable[1][i] = *wptr++;
	}

	const u8 *uptr = (const u8 *)wptr;

	if (uptr >= startPtr + dataSize) {
		return false;
	}

	int shadowCharMapSize = ((header.shadowMapLength * header.shadowMapBpe + 31) & ~31) / 8;
	const u8 *shadowCharMap = uptr;
	uptr += shadowCharMapSize;

	const u16_le *sptr = (const u16_le *)uptr;
	if (header.revision == 3) {
		charmapCompressionTable1[0].resize(rev3extra.compCharMapLength1);
		charmapCompressionTable1[1].resize(rev3extra.compCharMapLength1);
		for (int i = 0; i < rev3extra.compCharMapLength1; i++) {
			charmapCompressionTable1[0][i] = *sptr++;
			charmapCompressionTable1[1][i] = *sptr++;
		}

		charmapCompressionTable2[0].resize(rev3extra.compCharMapLength2);
		charmapCompressionTable2[1].resize(rev3extra.compCharMapLength2);
		for (int i = 0; i < rev3extra.compCharMapLength2; i++) {
			charmapCompressionTable2[0][i] = *sptr++;
			charmapCompressionTable2[1][i] = *sptr++;
		}
	}

	uptr = (const u8 *)sptr;

	if (uptr >= startPtr + dataSize) {
		return false;
	}

	int charMapSize = ((header.charMapLength * header.charMapBpe + 31) & ~31) / 8;
	const u8 *charMap = uptr;
	uptr += charMapSize;

	int charPointerSize = (((header.charPointerLength * header.charPointerBpe + 31) & ~31) / 8);
	const u8 *charPointerTable = uptr;
	uptr += charPointerSize;

	// PGF Fontdata.
	u32 fontDataOffset = (u32)(uptr - startPtr);

	fontDataSize = dataSize - fontDataOffset;
	fontData = new u8[fontDataSize];
	memcpy(fontData, uptr, fontDataSize);

	// charmap.resize();
	charmap.resize(header.charMapLength);
	int charmap_compr_len = header.revision == 3 ? 7 : 1;
	charmap_compr.resize(charmap_compr_len * 4);
	glyphs.resize(header.charPointerLength);
	shadowGlyphs.resize(header.charPointerLength);
	firstGlyph = header.firstGlyph;

	// Parse out the char map (array where each entry is an irregular number of bits)
	// BPE = bits per entry, I think.
	for (int i = 0; i < header.charMapLength; i++) {
		charmap[i] = getBits(header.charMapBpe, charMap, i * header.charMapBpe);
		// This check seems a little odd.
		if ((size_t)charmap[i] >= glyphs.size())
			charmap[i] = 65535;
	}

	std::vector<int> charPointers = getTable(charPointerTable, header.charPointerBpe, glyphs.size());
	std::vector<int> shadowMap = getTable(shadowCharMap, header.shadowMapBpe, (s32)header.shadowMapLength);

	// Pregenerate glyphs.
	for (size_t i = 0; i < glyphs.size(); i++) {
		ReadCharGlyph(fontData, charPointers[i] * 4 * 8  /* ??? */, glyphs[i]);
	}

	// And shadow glyphs.
	for (size_t i = 0; i < glyphs.size(); i++) {
		size_t shadowId = glyphs[i].shadowID;
		if (shadowId < shadowMap.size()) {
			size_t charId = shadowMap[shadowId];
			if (charId < shadowGlyphs.size()) {
				// TODO: check for pre existing shadow glyph
				ReadShadowGlyph(fontData, charPointers[charId] * 4 * 8  /* ??? */, shadowGlyphs[charId]);
			}
		}
	}

	return true;
}

int PGF::GetCharIndex(int charCode, const std::vector<int> &charmapCompressed) {
	int charIndex = 0;
	for (size_t i = 0; i < charmapCompressed.size(); i += 2) {
		if (charCode >= charmapCompressed[i] && charCode < charmapCompressed[i] + charmapCompressed[i + 1]) {
			charIndex += charCode - charmapCompressed[i];
			return charIndex;
		}
		charIndex += charmapCompressed[i + 1];
	}
	return -1;
}

bool PGF::GetCharInfo(int charCode, PGFCharInfo *charInfo, int altCharCode, int glyphType) const {
	Glyph glyph;
	memset(charInfo, 0, sizeof(*charInfo));

	if (!GetCharGlyph(charCode, glyphType, glyph)) {
		if (charCode < firstGlyph) {
			// Character not in font, return zeroed charInfo as on real PSP.
			return false;
		}
		if (!GetCharGlyph(altCharCode, glyphType, glyph)) {
			return false;
		}
	}

	charInfo->bitmapWidth = glyph.w;
	charInfo->bitmapHeight = glyph.h;
	charInfo->bitmapLeft = glyph.left;
	charInfo->bitmapTop = glyph.top;
	charInfo->sfp26Width = glyph.dimensionWidth;
	charInfo->sfp26Height = glyph.dimensionHeight;
	charInfo->sfp26Ascender = glyph.yAdjustH;
	// Font y goes upwards.  If top is 10 and height is 11, the descender is approx. -1 (below 0.)
	charInfo->sfp26Descender = charInfo->sfp26Ascender - (s32_le)charInfo->sfp26Height;
	charInfo->sfp26BearingHX = glyph.xAdjustH;
	charInfo->sfp26BearingHY = glyph.yAdjustH;
	charInfo->sfp26BearingVX = glyph.xAdjustV;
	charInfo->sfp26BearingVY = glyph.yAdjustV;
	charInfo->sfp26AdvanceH = glyph.advanceH;
	charInfo->sfp26AdvanceV = glyph.advanceV;
	charInfo->shadowFlags = glyph.shadowFlags;
	charInfo->shadowId = glyph.shadowID;
	return true;
}

void PGF::GetFontInfo(PGFFontInfo *fi) const {
	fi->maxGlyphWidthI = header.maxSize[0];
	fi->maxGlyphHeightI = header.maxSize[1];
	fi->maxGlyphAscenderI = header.maxAscender;
	fi->maxGlyphDescenderI = header.maxDescender;
	fi->maxGlyphLeftXI = header.maxLeftXAdjust;
	fi->maxGlyphBaseYI = header.maxBaseYAdjust;
	fi->minGlyphCenterXI = header.minCenterXAdjust;
	fi->maxGlyphTopYI = header.maxTopYAdjust;
	fi->maxGlyphAdvanceXI = header.maxAdvance[0];
	fi->maxGlyphAdvanceYI = header.maxAdvance[1];
	fi->maxGlyphWidthF = (float)header.maxSize[0] / 64.0f;
	fi->maxGlyphHeightF = (float)header.maxSize[1] / 64.0f;
	fi->maxGlyphAscenderF = (float)header.maxAscender / 64.0f;
	fi->maxGlyphDescenderF = (float)header.maxDescender / 64.0f;
	fi->maxGlyphLeftXF = (float)header.maxLeftXAdjust / 64.0f;
	fi->maxGlyphBaseYF = (float)header.maxBaseYAdjust / 64.0f;
	fi->minGlyphCenterXF = (float)header.minCenterXAdjust / 64.0f;
	fi->maxGlyphTopYF = (float)header.maxTopYAdjust / 64.0f;
	fi->maxGlyphAdvanceXF = (float)header.maxAdvance[0] / 64.0f;
	fi->maxGlyphAdvanceYF = (float)header.maxAdvance[1] / 64.0f;

	fi->maxGlyphWidth = header.maxGlyphWidth;
	fi->maxGlyphHeight = header.maxGlyphHeight;
	fi->numGlyphs = header.charPointerLength;
	fi->shadowMapLength = 0;  // header.shadowMapLength; TODO

	fi->BPP = header.bpp;
}

bool PGF::ReadShadowGlyph(const u8 *fontdata, size_t charPtr, Glyph &glyph) {
	// Most of the glyph info is from the char data.
	if (!ReadCharGlyph(fontdata, charPtr, glyph))
		return false;

	// Skip over the char data.
	if (charPtr + 96 > fontDataSize * 8)
		return false;
	charPtr += getBits(14, fontdata, charPtr) * 8;
	if (charPtr + 96 > fontDataSize * 8)
		return false;

	// Skip size.
	charPtr += 14;

	glyph.w = consumeBits(7, fontdata, charPtr);
	glyph.h = consumeBits(7, fontdata, charPtr);

	glyph.left = consumeBits(7, fontdata, charPtr);
	if (glyph.left >= 64) {
		glyph.left -= 128;
	}

	glyph.top = consumeBits(7, fontdata, charPtr);
	if (glyph.top >= 64) {
		glyph.top -= 128;
	}

	glyph.ptr = (u32)(charPtr / 8);
	return true;
}

bool PGF::ReadCharGlyph(const u8 *fontdata, size_t charPtr, Glyph &glyph) {
	// Skip size.
	charPtr += 14;

	glyph.w = consumeBits(7, fontdata, charPtr);
	glyph.h = consumeBits(7, fontdata, charPtr);

	glyph.left = consumeBits(7, fontdata, charPtr);
	if (glyph.left >= 64) {
		glyph.left -= 128;
	}

	glyph.top = consumeBits(7, fontdata, charPtr);
	if (glyph.top >= 64) {
		glyph.top -= 128;
	}

	glyph.flags = consumeBits(6, fontdata, charPtr);

	glyph.shadowFlags = consumeBits(2, fontdata, charPtr) << (2 + 3);
	glyph.shadowFlags |= consumeBits(2, fontdata, charPtr) << 3;
	glyph.shadowFlags |= consumeBits(3, fontdata, charPtr);

	glyph.shadowID = consumeBits(9, fontdata, charPtr);

	if ((glyph.flags & FONT_PGF_METRIC_DIMENSION_INDEX) == FONT_PGF_METRIC_DIMENSION_INDEX)
	{
		int dimensionIndex = consumeBits(8, fontdata, charPtr);

		if (dimensionIndex < header.dimTableLength) {
			glyph.dimensionWidth = dimensionTable[0][dimensionIndex];
			glyph.dimensionHeight = dimensionTable[1][dimensionIndex];
		}

		if (dimensionIndex == 0 && isJPCSPFont(fileName.c_str())) {
			// Fonts created by ttf2pgf do not contain complete Glyph information.
			// Provide default values.
			glyph.dimensionWidth = glyph.w << 6;
			glyph.dimensionHeight = glyph.h << 6;
		}
	}
	else
	{
		glyph.dimensionWidth = consumeBits(32, fontdata, charPtr);
		glyph.dimensionHeight = consumeBits(32, fontdata, charPtr);
	}

	if ((glyph.flags & FONT_PGF_METRIC_BEARING_X_INDEX) == FONT_PGF_METRIC_BEARING_X_INDEX)
	{
		int xAdjustIndex = consumeBits(8, fontdata, charPtr);

		if (xAdjustIndex < header.xAdjustTableLength) {
			glyph.xAdjustH = xAdjustTable[0][xAdjustIndex];
			glyph.xAdjustV = xAdjustTable[1][xAdjustIndex];
		}

		if (xAdjustIndex == 0 && isJPCSPFont(fileName.c_str()))
		{
			// Fonts created by ttf2pgf do not contain complete Glyph information.
			// Provide default values.
			glyph.xAdjustH = glyph.left << 6;
			glyph.xAdjustV = glyph.left << 6;
		}
	}
	else
	{
		glyph.xAdjustH = consumeBits(32, fontdata, charPtr);
		glyph.xAdjustV = consumeBits(32, fontdata, charPtr);
	}

	if ((glyph.flags & FONT_PGF_METRIC_BEARING_Y_INDEX) == FONT_PGF_METRIC_BEARING_Y_INDEX)
	{
		int yAdjustIndex = consumeBits(8, fontdata, charPtr);

		if (yAdjustIndex < header.xAdjustTableLength) {
			glyph.yAdjustH = yAdjustTable[0][yAdjustIndex];
			glyph.yAdjustV = yAdjustTable[1][yAdjustIndex];
		}

		if (yAdjustIndex == 0 && isJPCSPFont(fileName.c_str()))
		{
			// Fonts created by ttf2pgf do not contain complete Glyph information.
			// Provide default values.
			glyph.yAdjustH = glyph.top << 6;
			glyph.yAdjustV = glyph.top << 6;
		}
	}
	else
	{
		glyph.yAdjustH = consumeBits(32, fontdata, charPtr);
		glyph.yAdjustV = consumeBits(32, fontdata, charPtr);
	}

	if ((glyph.flags & FONT_PGF_METRIC_ADVANCE_INDEX) == FONT_PGF_METRIC_ADVANCE_INDEX)
	{
		int advanceIndex = consumeBits(8, fontdata, charPtr);

		if (advanceIndex < header.advanceTableLength) {
			glyph.advanceH = advanceTable[0][advanceIndex];
			glyph.advanceV = advanceTable[1][advanceIndex];
		}
	}
	else
	{
		glyph.advanceH = consumeBits(32, fontdata, charPtr);
		glyph.advanceV = consumeBits(32, fontdata, charPtr);
	}

	glyph.ptr = (u32)(charPtr / 8);
	return true;
}

bool PGF::GetCharGlyph(int charCode, int glyphType, Glyph &glyph) const {
	if (charCode < firstGlyph)
		return false;
	charCode -= firstGlyph;
	if (charCode < (int)charmap.size()) {
		charCode = charmap[charCode];
	}
	if (glyphType == FONT_PGF_CHARGLYPH) {
		if (charCode >= (int)glyphs.size())
			return false;
		glyph = glyphs[charCode];
	} else {
		if (charCode >= (int)shadowGlyphs.size())
			return false;
		glyph = shadowGlyphs[charCode];
	}
	return true;
}

void PGF::DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int altCharCode, int glyphType) const {
	Glyph glyph;
	if (!GetCharGlyph(charCode, glyphType, glyph)) {
		// No Glyph available for this charCode, try to use the alternate char.
		charCode = altCharCode;
		if (!GetCharGlyph(charCode, glyphType, glyph)) {
			return;
		}
	}

	if (glyph.w <= 0 || glyph.h <= 0) {
		return;
	}

	if (((glyph.flags & FONT_PGF_BMP_OVERLAY) != FONT_PGF_BMP_H_ROWS) &&
		((glyph.flags & FONT_PGF_BMP_OVERLAY) != FONT_PGF_BMP_V_ROWS)) {
			return;
	}

	size_t bitPtr = glyph.ptr * 8;
	int numberPixels = glyph.w * glyph.h;
	int pixelIndex = 0;

	int x = image->xPos64 >> 6;
	int y = image->yPos64 >> 6;

	// Negative means don't clip on that side.
	if (clipX < 0)
		clipX = 0;
	if (clipY < 0)
		clipY = 0;
	if (clipWidth < 0)
		clipWidth = 8192;
	if (clipHeight < 0)
		clipHeight = 8192;

	while (pixelIndex < numberPixels && bitPtr + 8 < fontDataSize * 8) {
		// This is some kind of nibble based RLE compression.
		int nibble = consumeBits(4, fontData, bitPtr);

		int count;
		int value = 0;
		if (nibble < 8) {
			value = consumeBits(4, fontData, bitPtr);
			count = nibble + 1;
		} else {
			count = 16 - nibble;
		}

		for (int i = 0; i < count && pixelIndex < numberPixels; i++) {
			if (nibble >= 8) {
				value = consumeBits(4, fontData, bitPtr);
			}

			int xx, yy;
			if ((glyph.flags & FONT_PGF_BMP_OVERLAY) == FONT_PGF_BMP_H_ROWS) {
				xx = pixelIndex % glyph.w;
				yy = pixelIndex / glyph.w;
			} else {
				xx = pixelIndex / glyph.h;
				yy = pixelIndex % glyph.h;
			}

			int pixelX = x + xx;
			int pixelY = y + yy;

			if (pixelX >= clipX && pixelX < clipX + clipWidth && pixelY >= clipY && pixelY < clipY + clipHeight) {
				// 4-bit color value
				int pixelColor = value;
				switch ((FontPixelFormat)(u32)image->pixelFormat) {
				case PSP_FONT_PIXELFORMAT_8:
					// 8-bit color value
					pixelColor |= pixelColor << 4;
					break;
				case PSP_FONT_PIXELFORMAT_24:
					// 24-bit color value
					pixelColor |= pixelColor << 4;
					pixelColor |= pixelColor << 8;
					pixelColor |= pixelColor << 8;
					break;
				case PSP_FONT_PIXELFORMAT_32:
					// 32-bit color value
					pixelColor |= pixelColor << 4;
					pixelColor |= pixelColor << 8;
					pixelColor |= pixelColor << 16;
					break;
				case PSP_FONT_PIXELFORMAT_4:
				case PSP_FONT_PIXELFORMAT_4_REV:
					break;
				default:
					ERROR_LOG_REPORT(SCEFONT, "Unhandled font pixel format: %d", (u32)image->pixelFormat);
					break;
				}

				SetFontPixel(image->bufferPtr, image->bytesPerLine, image->bufWidth, image->bufHeight, pixelX, pixelY, pixelColor, image->pixelFormat);
			}

			pixelIndex++;
		}
	}

	gpu->InvalidateCache(image->bufferPtr, image->bytesPerLine * image->bufHeight, GPU_INVALIDATE_SAFE);
}

void PGF::SetFontPixel(u32 base, int bpl, int bufWidth, int bufHeight, int x, int y, int pixelColor, int pixelformat) const {
	if (x < 0 || x >= bufWidth || y < 0 || y >= bufHeight) {
		return;
	}

	static const u8 fontPixelSizeInBytes[] = { 0, 0, 1, 3, 4 }; // 0 means 2 pixels per byte
	int pixelBytes = fontPixelSizeInBytes[pixelformat];
	int bufMaxWidth = (pixelBytes == 0 ? bpl * 2 : bpl / pixelBytes);
	if (x >= bufMaxWidth) {
		return;
	}

	int framebufferAddr = base + (y * bpl) + (pixelBytes == 0 ? x / 2 : x * pixelBytes);

	switch (pixelformat) {
	case PSP_FONT_PIXELFORMAT_4:
	case PSP_FONT_PIXELFORMAT_4_REV:
		{
			int oldColor = Memory::Read_U8(framebufferAddr);
			int newColor;
			if ((x & 1) != pixelformat) {
				newColor = (pixelColor << 4) | (oldColor & 0xF);
			} else {
				newColor = (oldColor & 0xF0) | pixelColor;
			}
			Memory::Write_U8(newColor, framebufferAddr);
			break;
		}
	case PSP_FONT_PIXELFORMAT_8:
		{
			Memory::Write_U8((u8)pixelColor, framebufferAddr);
			break;
		}
	case PSP_FONT_PIXELFORMAT_24:
		{
			Memory::Write_U8(pixelColor & 0xFF, framebufferAddr + 0);
			Memory::Write_U8(pixelColor >>  8, framebufferAddr + 1);
			Memory::Write_U8(pixelColor >> 16, framebufferAddr + 2);
			break;
		}
	case PSP_FONT_PIXELFORMAT_32:
		{
			Memory::Write_U32(pixelColor, framebufferAddr);
			break;
		}
	}
}

u32 GetFontPixelColor(int color, int pixelformat) {
	switch (pixelformat) {
	case PSP_FONT_PIXELFORMAT_4:
	case PSP_FONT_PIXELFORMAT_4_REV:
		// Use only 4-bit alpha
		color = (color >> 28) & 0xF;
		break;
	case PSP_FONT_PIXELFORMAT_8:
		// Use only 8-bit alpha
		color = (color >> 24) & 0xFF;
		break;
	case PSP_FONT_PIXELFORMAT_24:
		// Use RGB with 8-bit values
		color = color & 0x00FFFFFF;
		break;
	case PSP_FONT_PIXELFORMAT_32:
		// Use RGBA with 8-bit values
		break;
	}

	return color;
}
