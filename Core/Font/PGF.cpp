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

#include <algorithm>
#include <limits>
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/Font/PGF.h"

#include "GPU/GPUCommon.h"

// These fonts, created by ttf2pgf, don't have complete glyph info and need to be identified.
static bool isJPCSPFont(const char *fontName) {
	return !strcmp(fontName, "Liberation Sans") || !strcmp(fontName, "Liberation Serif") || !strcmp(fontName, "Sazanami") || !strcmp(fontName, "UnDotum") || !strcmp(fontName, "Microsoft YaHei");
}

static bool BitsToBytes(size_t count, int bpe, size_t *bytes) {
	if (bpe < 0 || bpe > 32) {
		return false;
	}
	const size_t bits_per_entry = (size_t)bpe;
	if (bits_per_entry != 0 && count > (std::numeric_limits<size_t>::max() - 31) / bits_per_entry) {
		return false;
	}
	const size_t bits = count * bits_per_entry;
	*bytes = ((bits + 31) / 32) * sizeof(u32);
	return true;
}

// Gets a number of bits from an offset.
static bool getBits(int numBits, const u8 *buf, size_t bufferBits, size_t pos, int *value) {
	if (numBits < 0 || numBits > 32 || pos > bufferBits || (size_t)numBits > bufferBits - pos) {
		return false;
	}

	u32 result = 0;
	for (int i = 0; i < numBits; ++i) {
		const size_t bit = pos + i;
		result |= ((buf[bit / 8] >> (bit & 7)) & 1) << i;
	}
	*value = (int)result;
	return true;
}

static bool consumeBits(int numBits, const u8 *buf, size_t bufferBits, size_t &pos, int *value) {
	if (!getBits(numBits, buf, bufferBits, pos, value)) {
		return false;
	}
	pos += numBits;
	return true;
}

static bool getTable(const u8 *buf, size_t bufferBits, int bpe, size_t length, std::vector<int> *vec) {
	vec->resize(length);
	for (size_t i = 0; i < length; i++) {
		if (!getBits(bpe, buf, bufferBits, bpe * i, &(*vec)[i])) {
			return false;
		}
	}
	return true;
}

PGF::PGF()
	: fontData(0) {

}

PGF::~PGF() {
	delete [] fontData;
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

	Do(p, header);
	Do(p, rev3extra);

	// Don't savestate size_t directly, 32-bit and 64-bit are different.
	u32 fontDataSizeTemp = (u32)fontDataSize;
	Do(p, fontDataSizeTemp);
	fontDataSize = (size_t)fontDataSizeTemp;
	if (p.mode == p.MODE_READ) {
		delete [] fontData;
		if (fontDataSize) {
			fontData = new u8[fontDataSize];
			DoArray(p, fontData, (int)fontDataSize);
		}
	} else if (fontDataSize) {
		DoArray(p, fontData, (int)fontDataSize);
	}
	Do(p, fileName);

	DoArray(p, dimensionTable, ARRAY_SIZE(dimensionTable));
	DoArray(p, xAdjustTable, ARRAY_SIZE(xAdjustTable));
	DoArray(p, yAdjustTable, ARRAY_SIZE(yAdjustTable));
	DoArray(p, advanceTable, ARRAY_SIZE(advanceTable));
	DoArray(p, charmapCompressionTable1, ARRAY_SIZE(charmapCompressionTable1));
	DoArray(p, charmapCompressionTable2, ARRAY_SIZE(charmapCompressionTable2));

	Do(p, charmap_compr);
	Do(p, charmap);
	if (s == 1) {
		std::vector<GlyphFromPGF1State> oldGlyphs;
		Do(p, oldGlyphs);
		glyphs.resize(oldGlyphs.size());
		for (size_t i = 0; i < oldGlyphs.size(); ++i) {
			glyphs[i] = oldGlyphs[i];
		}
		Do(p, oldGlyphs);
		shadowGlyphs.resize(oldGlyphs.size());
		for (size_t i = 0; i < oldGlyphs.size(); ++i) {
			shadowGlyphs[i] = oldGlyphs[i];
		}
	} else {
		Do(p, glyphs);
		Do(p, shadowGlyphs);
	}
	Do(p, firstGlyph);
}

bool PGF::ReadPtr(const u8 *ptr, size_t dataSize) {
	const u8 *const startPtr = ptr;

	if (dataSize < sizeof(header)) {
		return false;
	}
	size_t offset = sizeof(header);
	auto advance = [&](size_t size) {
		if (size > dataSize - offset) {
			return false;
		}
		offset += size;
		return true;
	};

	DEBUG_LOG(Log::sceFont, "Reading %d bytes of PGF header", (int)sizeof(header));
	memcpy(&header, ptr, sizeof(header));

	const char *nameEnd = (const char *)memchr(header.fontName, 0, sizeof(header.fontName));
	fileName.assign(header.fontName, nameEnd ? nameEnd - header.fontName : sizeof(header.fontName));

	if (header.charMapLength < 0 || header.charPointerLength < 0 || header.shadowMapLength < 0) {
		return false;
	}

	const size_t charMapLength = (size_t)(int)header.charMapLength;
	const size_t charPointerLength = (size_t)(int)header.charPointerLength;
	const size_t shadowMapLength = (size_t)(int)header.shadowMapLength;
	const int charMapBpe = header.charMapBpe;
	const int charPointerBpe = header.charPointerBpe;
	const int shadowMapBpe = header.shadowMapBpe;

	size_t charMapSize;
	size_t charPointerSize;
	size_t shadowCharMapSize;
	if (!BitsToBytes(charMapLength, charMapBpe, &charMapSize) ||
		!BitsToBytes(charPointerLength, charPointerBpe, &charPointerSize) ||
		!BitsToBytes(shadowMapLength, shadowMapBpe, &shadowCharMapSize)) {
		return false;
	}

	if (header.revision == 3) {
		if (!advance(sizeof(rev3extra))) {
			return false;
		}
		memcpy(&rev3extra, startPtr + offset - sizeof(rev3extra), sizeof(rev3extra));
		rev3extra.compCharMapLength1 &= 0xFFFF;
		rev3extra.compCharMapLength2 &= 0xFFFF;
	}

	const size_t metricTableEntries = header.dimTableLength + header.xAdjustTableLength +
		header.yAdjustTableLength + header.advanceTableLength;
	const size_t metricTableSize = metricTableEntries * 2 * sizeof(u32_le);
	if (!advance(metricTableSize)) {
		return false;
	}
	const u32_le *wptr = (const u32_le *)(startPtr + offset - metricTableSize);
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

	if (!advance(shadowCharMapSize)) {
		return false;
	}
	const u8 *shadowCharMap = startPtr + offset - shadowCharMapSize;

	if (header.revision == 3) {
		const size_t compressionTableSize = ((size_t)(int)rev3extra.compCharMapLength1 + (size_t)(int)rev3extra.compCharMapLength2) * 2 * sizeof(u16_le);
		if (!advance(compressionTableSize)) {
			return false;
		}
		const u16_le *sptr = (const u16_le *)(startPtr + offset - compressionTableSize);
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

	if (!advance(charMapSize)) {
		return false;
	}
	const u8 *charMap = startPtr + offset - charMapSize;
	if (!advance(charPointerSize)) {
		return false;
	}
	const u8 *charPointerTable = startPtr + offset - charPointerSize;

	// PGF Fontdata.
	fontDataSize = dataSize - offset;
	fontData = new u8[fontDataSize];
	memcpy(fontData, startPtr + offset, fontDataSize);

	// charmap.resize();
	charmap.resize(charMapLength);
	int charmap_compr_len = header.revision == 3 ? 7 : 1;
	charmap_compr.resize(charmap_compr_len * 4);
	glyphs.resize(charPointerLength);
	shadowGlyphs.resize(charPointerLength);
	firstGlyph = header.firstGlyph;

	// Parse out the char map (array where each entry is an irregular number of bits)
	// BPE = bits per entry, I think.
	for (size_t i = 0; i < charMapLength; i++) {
		if (!getBits(charMapBpe, charMap, charMapSize * 8, i * (size_t)charMapBpe, &charmap[i])) {
			return false;
		}
		// This check seems a little odd.
		if ((size_t)charmap[i] >= glyphs.size())
			charmap[i] = 65535;
	}

	std::vector<int> charPointers;
	std::vector<int> shadowMap;
	if (!getTable(charPointerTable, charPointerSize * 8, charPointerBpe, glyphs.size(), &charPointers) ||
		!getTable(shadowCharMap, shadowCharMapSize * 8, shadowMapBpe, shadowMapLength, &shadowMap)) {
		return false;
	}

	// Pregenerate glyphs.
	for (size_t i = 0; i < glyphs.size(); i++) {
		if (!ReadCharGlyph(fontData, charPointers[i] * 4 * 8  /* ??? */, glyphs[i])) {
			return false;
		}
	}

	// And shadow glyphs.
	for (size_t i = 0; i < glyphs.size(); i++) {
		size_t shadowId = glyphs[i].shadowID;
		if (shadowId < shadowMap.size()) {
			size_t charId = shadowMap[shadowId];
			if (charId < shadowGlyphs.size()) {
				// TODO: check for pre existing shadow glyph
				if (!ReadShadowGlyph(fontData, charPointers[charId] * 4 * 8  /* ??? */, shadowGlyphs[charId])) {
					return false;
				}
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
	charInfo->sfp26Descender = charInfo->sfp26Ascender - (s32)charInfo->sfp26Height;
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

	const size_t fontDataBits = fontDataSize * 8;
	// Skip over the char data.
	if (charPtr > fontDataBits || 14 > fontDataBits - charPtr)
		return false;
	int glyphSize;
	if (!getBits(14, fontdata, fontDataBits, charPtr, &glyphSize))
		return false;
	if ((size_t)glyphSize > (fontDataBits - charPtr) / 8)
		return false;
	charPtr += (size_t)glyphSize * 8;
	if (charPtr > fontDataBits || 42 > fontDataBits - charPtr)
		return false;

	// Skip size.
	charPtr += 14;

	if (!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.w) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.h) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.left) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.top)) {
		return false;
	}

	if (glyph.left >= 64) {
		glyph.left -= 128;
	}

	if (glyph.top >= 64) {
		glyph.top -= 128;
	}

	glyph.ptr = (u32)(charPtr / 8);
	return true;
}

bool PGF::ReadCharGlyph(const u8 *fontdata, size_t charPtr, Glyph &glyph) {
	const size_t fontDataBits = fontDataSize * 8;
	if (charPtr > fontDataBits || 14 > fontDataBits - charPtr) {
		return false;
	}
	// Skip size.
	charPtr += 14;

	if (!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.w) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.h) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.left) ||
		!consumeBits(7, fontdata, fontDataBits, charPtr, &glyph.top) ||
		!consumeBits(6, fontdata, fontDataBits, charPtr, &glyph.flags)) {
		return false;
	}
	if (glyph.left >= 64) {
		glyph.left -= 128;
	}

	if (glyph.top >= 64) {
		glyph.top -= 128;
	}

	int shadowFlagHigh;
	int shadowFlagMiddle;
	int shadowFlagLow;
	if (!consumeBits(2, fontdata, fontDataBits, charPtr, &shadowFlagHigh) ||
		!consumeBits(2, fontdata, fontDataBits, charPtr, &shadowFlagMiddle) ||
		!consumeBits(3, fontdata, fontDataBits, charPtr, &shadowFlagLow) ||
		!consumeBits(9, fontdata, fontDataBits, charPtr, &glyph.shadowID)) {
		return false;
	}
	glyph.shadowFlags = shadowFlagHigh << (2 + 3);
	glyph.shadowFlags |= shadowFlagMiddle << 3;
	glyph.shadowFlags |= shadowFlagLow;

	if ((glyph.flags & FONT_PGF_METRIC_DIMENSION_INDEX) == FONT_PGF_METRIC_DIMENSION_INDEX)
	{
		int dimensionIndex;
		if (!consumeBits(8, fontdata, fontDataBits, charPtr, &dimensionIndex))
			return false;

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
		if (!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.dimensionWidth) ||
			!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.dimensionHeight))
			return false;
	}

	if ((glyph.flags & FONT_PGF_METRIC_BEARING_X_INDEX) == FONT_PGF_METRIC_BEARING_X_INDEX)
	{
		int xAdjustIndex;
		if (!consumeBits(8, fontdata, fontDataBits, charPtr, &xAdjustIndex))
			return false;

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
		if (!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.xAdjustH) ||
			!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.xAdjustV))
			return false;
	}

	if ((glyph.flags & FONT_PGF_METRIC_BEARING_Y_INDEX) == FONT_PGF_METRIC_BEARING_Y_INDEX)
	{
		int yAdjustIndex;
		if (!consumeBits(8, fontdata, fontDataBits, charPtr, &yAdjustIndex))
			return false;

		if (yAdjustIndex < header.yAdjustTableLength) {
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
		if (!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.yAdjustH) ||
			!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.yAdjustV))
			return false;
	}

	if ((glyph.flags & FONT_PGF_METRIC_ADVANCE_INDEX) == FONT_PGF_METRIC_ADVANCE_INDEX)
	{
		int advanceIndex;
		if (!consumeBits(8, fontdata, fontDataBits, charPtr, &advanceIndex))
			return false;

		if (advanceIndex < header.advanceTableLength) {
			glyph.advanceH = advanceTable[0][advanceIndex];
			glyph.advanceV = advanceTable[1][advanceIndex];
		}
	}
	else
	{
		if (!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.advanceH) ||
			!consumeBits(32, fontdata, fontDataBits, charPtr, &glyph.advanceV))
			return false;
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
		if (charCode < firstGlyph) {
			// Don't draw anything if the character is before the first available glyph.
			return;
		}
		// No Glyph available for this charCode, try to use the alternate char.
		charCode = altCharCode;
		if (!GetCharGlyph(charCode, glyphType, glyph)) {
			return;
		}
	}

	if (glyph.w <= 0 || glyph.h <= 0) {
		DEBUG_LOG(Log::sceFont, "Glyph with negative size, not rendering");
		return;
	}

	if (((glyph.flags & FONT_PGF_BMP_OVERLAY) != FONT_PGF_BMP_H_ROWS) &&
		((glyph.flags & FONT_PGF_BMP_OVERLAY) != FONT_PGF_BMP_V_ROWS)) {
		ERROR_LOG_REPORT(Log::sceFont, "Nonsense glyph bitmap direction flag");
		return;
	}

	size_t bitPtr = (size_t)glyph.ptr * 8;
	int numberPixels = glyph.w * glyph.h;
	int pixelIndex = 0;

	int x = image->xPos64 >> 6;
	int y = image->yPos64 >> 6;
	u8 xFrac = image->xPos64 & 0x3F;
	u8 yFrac = image->yPos64 & 0x3F;

	// Negative means don't clip on that side.
	if (clipX < 0)
		clipX = 0;
	if (clipY < 0)
		clipY = 0;
	if (clipWidth < 0)
		clipWidth = 8192;
	if (clipHeight < 0)
		clipHeight = 8192;

	// Use a buffer so we can apply subpixel rendering.
	// TODO: Cache this buffer per glyph?  Maybe even transpose it first?
	std::vector<u8> decodedPixels;
	decodedPixels.resize(numberPixels);

	const size_t fontDataBits = fontDataSize * 8;
	while (pixelIndex < numberPixels && bitPtr <= fontDataBits && 8 <= fontDataBits - bitPtr) {
		// This is some kind of nibble based RLE compression.
		int nibble;
		if (!consumeBits(4, fontData, fontDataBits, bitPtr, &nibble))
			return;

		int count;
		int value = 0;
		if (nibble < 8) {
			if (!consumeBits(4, fontData, fontDataBits, bitPtr, &value))
				return;
			count = nibble + 1;
		} else {
			count = 16 - nibble;
		}

		for (int i = 0; i < count && pixelIndex < numberPixels; i++) {
			if (nibble >= 8) {
				if (!consumeBits(4, fontData, fontDataBits, bitPtr, &value))
					return;
			}

			decodedPixels[pixelIndex++] = value | (value << 4);
		}
	}

	auto samplePixel = [&](int xx, int yy) -> u8 {
		if (xx < 0 || yy < 0 || xx >= glyph.w || yy >= glyph.h) {
			return 0;
		}

		int index;
		if ((glyph.flags & FONT_PGF_BMP_OVERLAY) == FONT_PGF_BMP_H_ROWS) {
			index = yy * glyph.w + xx;
		} else {
			index = xx * glyph.h + yy;
		}

		return decodedPixels[index];
	};

	int renderX1 = std::max(clipX, x) - x;
	int renderY1 = std::max(clipY, y) - y;
	// We can render up to frac beyond the glyph w/h, so add 1px if necessary.
	int renderX2 = std::min(clipX + clipWidth - x, glyph.w + (xFrac > 0 ? 1 : 0));
	int renderY2 = std::min(clipY + clipHeight - y, glyph.h + (yFrac > 0 ? 1 : 0));

	if (gpu && renderX1 < renderX2 && renderY1 < renderY2) {
		// The game may reuse this glyph buffer as a texture immediately after drawing it.
		gpu->Flush();
	}

	if (xFrac == 0 && yFrac == 0) {
		for (int yy = renderY1; yy < renderY2; ++yy) {
			for (int xx = renderX1; xx < renderX2; ++xx) {
				u8 pixelColor = samplePixel(xx, yy);
				SetFontPixel(image->bufferPtr, image->bytesPerLine, image->bufWidth, image->bufHeight, x + xx, y + yy, pixelColor, (FontPixelFormat)(u32)image->pixelFormat);
			}
		}
	} else {
		for (int yy = renderY1; yy < renderY2; ++yy) {
			for (int xx = renderX1; xx < renderX2; ++xx) {
				// First, blend horizontally.  Tests show we blend swizzled to 8 bit.
				u32 horiz1 = samplePixel(xx - 1, yy - 1) * xFrac + samplePixel(xx, yy - 1) * (64 - xFrac);
				u32 horiz2 = samplePixel(xx - 1, yy + 0) * xFrac + samplePixel(xx, yy + 0) * (64 - xFrac);
				// Now blend those together vertically.
				u32 blended = horiz1 * yFrac + horiz2 * (64 - yFrac);

				// We multiplied an 8 bit value by 64 twice, so now we have a 20 bit value.
				u8 pixelColor = blended >> 12;
				SetFontPixel(image->bufferPtr, image->bytesPerLine, image->bufWidth, image->bufHeight, x + xx, y + yy, pixelColor, (FontPixelFormat)(u32)image->pixelFormat);
			}
		}
	}

	gpu->InvalidateCache(image->bufferPtr, image->bytesPerLine * image->bufHeight, GPU_INVALIDATE_SAFE);
}

void PGF::SetFontPixel(u32 base, int bpl, int bufWidth, int bufHeight, int x, int y, u8 pixelColor, FontPixelFormat pixelformat) const {
	if (x < 0 || x >= bufWidth || y < 0 || y >= bufHeight) {
		return;
	}

	static const u8 fontPixelSizeInBytes[] = { 0, 0, 1, 3, 4 }; // 0 means 2 pixels per byte
	if (pixelformat < 0 || pixelformat > PSP_FONT_PIXELFORMAT_32) {
		ERROR_LOG_REPORT_ONCE(pfgbadformat, Log::sceFont, "Invalid image format in image: %d", (int)pixelformat);
		return;
	}
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
			// We always get a 8-bit value, so take only the top 4 bits.
			const u8 pix4 = pixelColor >> 4;

			int oldColor = Memory::Read_U8(framebufferAddr);
			int newColor;
			if ((x & 1) != pixelformat) {
				newColor = (pix4 << 4) | (oldColor & 0xF);
			} else {
				newColor = (oldColor & 0xF0) | pix4;
			}
			Memory::Write_U8(newColor, framebufferAddr);
			break;
		}
	case PSP_FONT_PIXELFORMAT_8:
		{
			Memory::Write_U8(pixelColor, framebufferAddr);
			break;
		}
	case PSP_FONT_PIXELFORMAT_24:
		{
			// Each channel has the same value.
			Memory::Write_U8(pixelColor, framebufferAddr + 0);
			Memory::Write_U8(pixelColor, framebufferAddr + 1);
			Memory::Write_U8(pixelColor, framebufferAddr + 2);
			break;
		}
	case PSP_FONT_PIXELFORMAT_32:
		{
			// Spread the 8 bits out into one write of 32 bits.
			u32 pix32 = pixelColor;
			pix32 |= pix32 << 8;
			pix32 |= pix32 << 16;
			Memory::Write_U32(pix32, framebufferAddr);
			break;
		}
	}
}
