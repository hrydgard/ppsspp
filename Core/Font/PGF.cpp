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

#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "Core/Font/PGF.h"
#include "Core/HLE/HLE.h"

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

// These fonts, created by ttf2pgf, don't have complete glyph info and need to be identified.
static bool isJPCSPFont(const char *fontName) {
	return !strcmp(fontName, "Liberation") || !strcmp(fontName, "Sazanami") || !strcmp(fontName, "UnDotum");
}

// Gets a number of bits from an offset.
// TODO: Make more efficient.
static int getBits(int numBits, const u8 *buf, size_t pos) {
	int v = 0;
	for (int i = 0; i < numBits; i++) {
		v = v | (((buf[pos >> 3] >> (pos & 7)) & 1) << i);
		pos++;
	}
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

void PGF::DoState(PointerWrap &p) {
	p.Do(header);
	p.Do(rev3extra);

	p.Do(fontDataSize);
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
	p.Do(glyphs);
	p.Do(shadowGlyphs);
	p.Do(firstGlyph);

	p.DoMarker("PGF");
}

void PGF::ReadPtr(const u8 *ptr, size_t dataSize) {
	const u8 *const startPtr = ptr;

	INFO_LOG(HLE, "Reading %d bytes of PGF header", (int)sizeof(header));
	memcpy(&header, ptr, sizeof(header));
	ptr += sizeof(header);

	if (header.revision == 3) {
		memcpy(&rev3extra, ptr, sizeof(rev3extra));
		rev3extra.compCharMapLength1 &= 0xFFFF;
		rev3extra.compCharMapLength2 &= 0xFFFF;
		ptr += sizeof(rev3extra);
	}

	const u32 *wptr = (const u32 *)ptr;
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

	int shadowCharMapSize = ((header.shadowMapLength * header.shadowMapBpe + 31) & ~31) / 8;
	u8 *shadowCharMap = new u8[shadowCharMapSize];
	for (int i = 0; i < shadowCharMapSize; i++) {
		shadowCharMap[i] = *uptr++;
	}

	const u16 *sptr = (const u16 *)uptr;
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

	int charMapSize = ((header.charMapLength * header.charMapBpe + 31) & ~31) / 8;

	u8 *charMap = new u8[charMapSize];
	for (int i = 0; i < charMapSize; i++) {
		charMap[i] = *uptr++;
	}

	int charPointerSize = (((header.charPointerLength * header.charPointerBpe + 31) & ~31) / 8);
	u8 *charPointerTable = new u8[charPointerSize];
	for (int i = 0; i < charPointerSize; i++) {
		charPointerTable[i] = *uptr++;
	}

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
	shadowGlyphs.resize(header.shadowMapLength);
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
	std::vector<int> shadowMap = getTable(shadowCharMap, header.shadowMapBpe, shadowGlyphs.size());

	delete [] charMap;
	delete [] shadowCharMap;
	delete [] charPointerTable;

	// Pregenerate glyphs.
	for (size_t i = 0; i < glyphs.size(); i++) {
		GetGlyph(fontData, charPointers[i] * 4 * 8  /* ??? */, FONT_PGF_CHARGLYPH, glyphs[i]);
	}

	// And shadow glyphs.
	for (size_t i = 0; i < shadowGlyphs.size(); i++) {
		size_t shadowId = glyphs[i].shadowID;
		if (shadowId < shadowMap.size()) {
			size_t charId = shadowMap[shadowId];
			if (charId < glyphs.size()) {
				// TODO: check for pre existing shadow glyph
				GetGlyph(fontData, charPointers[charId] * 4 * 8  /* ??? */, FONT_PGF_SHADOWGLYPH, shadowGlyphs[i]);
			}
		}
	}
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

bool PGF::GetCharInfo(int charCode, PGFCharInfo *charInfo) {
	Glyph glyph;
	memset(charInfo, 0, sizeof(*charInfo));

	if (!GetCharGlyph(charCode, FONT_PGF_CHARGLYPH, glyph)) {
		// Character not in font, return zeroed charInfo as on real PSP.
		return false;
	}

	charInfo->bitmapWidth = glyph.w;
	charInfo->bitmapHeight = glyph.h;
	charInfo->bitmapLeft = glyph.left;
	charInfo->bitmapTop = glyph.top;
	charInfo->sfp26Width = glyph.dimensionWidth;
	charInfo->sfp26Height = glyph.dimensionHeight;
	charInfo->sfp26Ascender = glyph.top << 6;
	charInfo->sfp26Descender = (glyph.h - glyph.top) << 6;
	charInfo->sfp26BearingHX = glyph.xAdjustH;
	charInfo->sfp26BearingHY = glyph.yAdjustH;
	charInfo->sfp26BearingVX = glyph.xAdjustV;
	charInfo->sfp26BearingVY = glyph.yAdjustV;
	charInfo->sfp26AdvanceH = glyph.advanceH;
	charInfo->sfp26AdvanceV = glyph.advanceV;
	return true;
}

void PGF::GetFontInfo(PGFFontInfo *fi) {
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
	fi->maxGlyphWidthF = header.maxSize[0] / 64.0f;
	fi->maxGlyphHeightF = header.maxSize[1] / 64.0f;
	fi->maxGlyphAscenderF = header.maxAscender / 64.0f;
	fi->maxGlyphDescenderF = header.maxDescender / 64.0f;
	fi->maxGlyphLeftXF = header.maxLeftXAdjust / 64.0f;
	fi->maxGlyphBaseYF = header.maxBaseYAdjust / 64.0f;
	fi->minGlyphCenterXF = header.minCenterXAdjust / 64.0f;
	fi->maxGlyphTopYF = header.maxTopYAdjust / 64.0f;
	fi->maxGlyphAdvanceXF = header.maxAdvance[0] / 64.0f;
	fi->maxGlyphAdvanceYF = header.maxAdvance[1] / 64.0f;

	fi->maxGlyphWidth = header.maxGlyphWidth;
	fi->maxGlyphHeight = header.maxGlyphHeight;
	fi->charMapLength = header.charMapLength;
	fi->shadowMapLength = 0;  // header.shadowMapLength; TODO

	fi->BPP = header.bpp;
}

bool PGF::GetGlyph(const u8 *fontdata, size_t charPtr, int glyphType, Glyph &glyph) {
	if (glyphType == FONT_PGF_SHADOWGLYPH) {
		if (charPtr + 96 > fontDataSize * 8)
			return false;
		charPtr += getBits(14, fontdata, charPtr) * 8;
		if (charPtr + 96 > fontDataSize * 8)
			return false;
	}
	charPtr += 14;

	glyph.w = getBits(7, fontdata, charPtr);
	charPtr += 7;

	glyph.h = getBits(7, fontdata, charPtr);
	charPtr += 7;

	glyph.left = getBits(7, fontdata, charPtr);
	charPtr += 7;
	if (glyph.left >= 64) {
		glyph.left -= 128;
	}

	glyph.top = getBits(7, fontdata, charPtr);
	charPtr += 7;
	if (glyph.top >= 64) {
		glyph.top -= 128;
	}

	glyph.flags = getBits(6, fontdata, charPtr);
	charPtr += 6;

	if (glyph.flags & FONT_PGF_CHARGLYPH) {
		// Skip magic number
		charPtr += 7;

		glyph.shadowID = getBits(9, fontdata, charPtr);
		charPtr += 9;

		int dimensionIndex = getBits(8, fontdata, charPtr);
		charPtr += 8;

		int xAdjustIndex = getBits(8, fontdata, charPtr);
		charPtr += 8;

		int yAdjustIndex = getBits(8, fontdata, charPtr);
		charPtr += 8;

		charPtr += 
			((glyph.flags & FONT_PGF_METRIC_FLAG1) ? 0 : 56) +
			((glyph.flags & FONT_PGF_METRIC_FLAG2) ? 0 : 56) +
			((glyph.flags & FONT_PGF_METRIC_FLAG3) ? 0 : 56);

		int advanceIndex = getBits(8, fontdata, charPtr);
		charPtr += 8;

		if (dimensionIndex < header.dimTableLength) {
			glyph.dimensionWidth = dimensionTable[0][dimensionIndex];
			glyph.dimensionHeight = dimensionTable[1][dimensionIndex];
		}

		if (xAdjustIndex < header.xAdjustTableLength) {
			glyph.xAdjustH = xAdjustTable[0][xAdjustIndex];
			glyph.xAdjustV = xAdjustTable[1][xAdjustIndex];
		}

		if (yAdjustIndex < header.xAdjustTableLength) {
			glyph.yAdjustH = yAdjustTable[0][yAdjustIndex];
			glyph.yAdjustV = yAdjustTable[1][yAdjustIndex];
		}

		if (dimensionIndex == 0 && xAdjustIndex == 0 && yAdjustIndex == 0 && isJPCSPFont(fileName.c_str())) {
			// Fonts created by ttf2pgf do not contain complete Glyph information.
			// Provide default values.
			glyph.dimensionWidth = glyph.w << 6;
			glyph.dimensionHeight = glyph.h << 6;
			// This stuff doesn't exactly look right.
			glyph.xAdjustH = glyph.left << 6;
			glyph.xAdjustV = glyph.left << 6;
			glyph.yAdjustH = glyph.top << 6;
			glyph.yAdjustV = glyph.top << 6;
		}

		if (advanceIndex < header.advanceTableLength) {
			glyph.advanceH = advanceTable[0][advanceIndex];
			glyph.advanceV = advanceTable[1][advanceIndex];
		}
	} else {
		glyph.shadowID = 65535;
		glyph.advanceH = 0;
	}

	glyph.ptr = (u32)(charPtr / 8);
	return true;
}

bool PGF::GetCharGlyph(int charCode, int glyphType, Glyph &glyph) {
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

void PGF::DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int altCharCode, int glyphType) {
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

	u32 bitPtr = glyph.ptr * 8;
	int numberPixels = glyph.w * glyph.h;
	int pixelIndex = 0;

	int x = image->xPos64 >> 6;
	int y = image->yPos64 >> 6;

	while (pixelIndex < numberPixels && bitPtr + 8 < fontDataSize * 8) {
		// This is some kind of nibble based RLE compression.
		int nibble = getBits(4, fontData, bitPtr);
		bitPtr += 4;

		int count;
		int value = 0;
		if (nibble < 8) {
			value = getBits(4, fontData, bitPtr);
			bitPtr += 4;
			count = nibble + 1;
		} else {
			count = 16 - nibble;
		}

		for (int i = 0; i < count && pixelIndex < numberPixels; i++) {
			if (nibble >= 8) {
				value = getBits(4, fontData, bitPtr);
				bitPtr += 4;
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
				switch (image->pixelFormat) {
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
				}

				SetFontPixel(image->bufferPtr, image->bytesPerLine, image->bufWidth, image->bufHeight, pixelX, pixelY, pixelColor, image->pixelFormat);
			}

			pixelIndex++;
		}
	}

	gpu->InvalidateCache(image->bufferPtr, image->bytesPerLine * image->bufHeight, GPU_INVALIDATE_SAFE);
}

void PGF::SetFontPixel(u32 base, int bpl, int bufWidth, int bufHeight, int x, int y, int pixelColor, int pixelformat) {
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