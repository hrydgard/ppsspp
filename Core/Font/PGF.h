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

// Thanks to the JPCSP project! This sceFont implementation is basically a C++ take on JPCSP's font code.

#pragma once

#include <string>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

enum {
	FONT_FILETYPE_PGF = 0x00,
	FONT_FILETYPE_BWFON = 0x01,
};

enum {
	FONT_PGF_BMP_H_ROWS = 0x01,
	FONT_PGF_BMP_V_ROWS = 0x02,
	FONT_PGF_BMP_OVERLAY = 0x03,
	// Metric names according to JPCSP findings
	FONT_PGF_METRIC_DIMENSION_INDEX = 0x04,
	FONT_PGF_METRIC_BEARING_X_INDEX = 0x08,
	FONT_PGF_METRIC_BEARING_Y_INDEX = 0x10,
	FONT_PGF_METRIC_ADVANCE_INDEX = 0x20,
	FONT_PGF_CHARGLYPH = 0x20,
	FONT_PGF_SHADOWGLYPH = 0x40,
};

enum Family {
	FONT_FAMILY_SANS_SERIF = 1,
	FONT_FAMILY_SERIF      = 2,
};

enum Style {
	FONT_STYLE_REGULAR     = 1,
	FONT_STYLE_ITALIC      = 2,
	FONT_STYLE_BOLD        = 5,
	FONT_STYLE_BOLD_ITALIC = 6,
	FONT_STYLE_DB          = 103, // Demi-Bold / semi-bold
};

enum Language {
	FONT_LANGUAGE_JAPANESE = 1,
	FONT_LANGUAGE_LATIN    = 2,
	FONT_LANGUAGE_KOREAN   = 3,
	FONT_LANGUAGE_CHINESE  = 4,
};

enum FontPixelFormat {
	PSP_FONT_PIXELFORMAT_4     = 0, // 2 pixels packed in 1 byte (natural order)
	PSP_FONT_PIXELFORMAT_4_REV = 1, // 2 pixels packed in 1 byte (reversed order)
	PSP_FONT_PIXELFORMAT_8     = 2, // 1 pixel in 1 byte
	PSP_FONT_PIXELFORMAT_24    = 3, // 1 pixel in 3 bytes (RGB)
	PSP_FONT_PIXELFORMAT_32    = 4, // 1 pixel in 4 bytes (RGBA)
};


struct PGFFontStyle {
	float_le  fontH;
	float_le  fontV;
	float_le  fontHRes;
	float_le  fontVRes;
	float_le  fontWeight;
	u16_le    fontFamily;
	u16_le    fontStyle;
	// Check.
	u16_le    fontStyleSub;
	u16_le    fontLanguage;
	u16_le    fontRegion;
	u16_le    fontCountry;
	char      fontName[64];
	char      fontFileName[64];
	u32_le    fontAttributes;
	u32_le    fontExpire;
};


struct Glyph {
	int w;
	int h;
	int left;
	int top;
	int flags;
	int shadowFlags;
	int shadowID;
	int advanceH;
	int advanceV;
	int dimensionWidth, dimensionHeight;
	int xAdjustH, xAdjustV;
	int yAdjustH, yAdjustV;
	u32 ptr;
};


#if COMMON_LITTLE_ENDIAN
typedef FontPixelFormat FontPixelFormat_le;
#else
typedef swap_struct_t<FontPixelFormat, swap_32_t<FontPixelFormat> > FontPixelFormat_le;
#endif

struct GlyphImage {
	FontPixelFormat_le pixelFormat;
	s32_le xPos64;
	s32_le yPos64;
	u16_le bufWidth;
	u16_le bufHeight;
	u16_le bytesPerLine;
	u16_le pad;
	u32_le bufferPtr;
};

#pragma pack(push,1)
struct PGFHeader
{
	u16_le headerOffset;
	u16_le headerSize;

	char PGFMagic[4];
	s32_le revision;
	s32_le version;

	s32_le charMapLength;
	s32_le charPointerLength;
	s32_le charMapBpe;
	s32_le charPointerBpe;

	// TODO: This has values in it (0404)...
	u8 pad1[2];
	u8 bpp;
	u8 pad2[1];

	s32_le hSize;
	s32_le vSize;
	s32_le hResolution;
	s32_le vResolution;

	u8 pad3[1];
	char fontName[64];
	char fontType[64];
	u8 pad4[1];

	u16_le firstGlyph;
	u16_le lastGlyph;

	// TODO: This has a few 01s in it in the official fonts.
	u8 pad5[26];

	s32_le maxAscender;
	s32_le maxDescender;
	s32_le maxLeftXAdjust;
	s32_le maxBaseYAdjust;
	s32_le minCenterXAdjust;
	s32_le maxTopYAdjust;

	s32_le maxAdvance[2];
	s32_le maxSize[2];
	u16_le maxGlyphWidth;
	u16_le maxGlyphHeight;
	u8 pad6[2];

	u8 dimTableLength;
	u8 xAdjustTableLength;
	u8 yAdjustTableLength;
	u8 advanceTableLength;
	u8 pad7[102];

	s32_le shadowMapLength;
	s32_le shadowMapBpe;
	float_le unknown1;
	s32_le shadowScale[2];
	u8 pad8[8];
};

struct PGFHeaderRev3Extra {
	s32_le compCharMapBpe1;
	s32_le compCharMapLength1;
	s32_le compCharMapBpe2;
	s32_le compCharMapLength2;
	u32_le unknown;
};

struct PGFCharInfo {
	u32_le bitmapWidth;
	u32_le bitmapHeight;
	u32_le bitmapLeft;
	u32_le bitmapTop;
	// Glyph metrics (in 26.6 signed fixed-point).
	u32_le sfp26Width;
	u32_le sfp26Height;
	s32_le sfp26Ascender;
	s32_le sfp26Descender;
	s32_le sfp26BearingHX;
	s32_le sfp26BearingHY;
	s32_le sfp26BearingVX;
	s32_le sfp26BearingVY;
	s32_le sfp26AdvanceH;
	s32_le sfp26AdvanceV;
	s16_le shadowFlags;
	s16_le shadowId;
};

struct PGFFontInfo {
	// Glyph metrics (in 26.6 signed fixed-point).
	s32_le maxGlyphWidthI;
	s32_le maxGlyphHeightI;
	s32_le maxGlyphAscenderI;
	s32_le maxGlyphDescenderI;
	s32_le maxGlyphLeftXI;
	s32_le maxGlyphBaseYI;
	s32_le minGlyphCenterXI;
	s32_le maxGlyphTopYI;
	s32_le maxGlyphAdvanceXI;
	s32_le maxGlyphAdvanceYI;

	// Glyph metrics (replicated as float).
	float_le maxGlyphWidthF;
	float_le maxGlyphHeightF;
	float_le maxGlyphAscenderF;
	float_le maxGlyphDescenderF;
	float_le maxGlyphLeftXF;
	float_le maxGlyphBaseYF;
	float_le minGlyphCenterXF;
	float_le maxGlyphTopYF;
	float_le maxGlyphAdvanceXF;
	float_le maxGlyphAdvanceYF;

	// Bitmap dimensions.
	s16_le maxGlyphWidth;
	s16_le maxGlyphHeight;
	s32_le numGlyphs;
	s32_le shadowMapLength; // Number of elements in the font's shadow charmap.

	// Font style (used by font comparison functions).
	PGFFontStyle fontStyle;

	u8 BPP; // Font's BPP.
	u8 pad[3];
};

#pragma pack(pop)

class PGF {
public:
	PGF();
	~PGF();

	bool ReadPtr(const u8 *ptr, size_t dataSize);

	bool GetCharInfo(int charCode, PGFCharInfo *ci, int altCharCode, int glyphType = FONT_PGF_CHARGLYPH) const;
	void GetFontInfo(PGFFontInfo *fi) const;
	void DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int altCharCode, int glyphType) const;

	void DoState(PointerWrap &p);

	PGFHeader header;

private:
	bool ReadCharGlyph(const u8 *fontdata, size_t charPtr, Glyph &glyph);
	bool ReadShadowGlyph(const u8 *fontdata, size_t charPtr, Glyph &glyph);
	bool GetCharGlyph(int charCode, int glyphType, Glyph &glyph) const;

	// Unused
	int GetCharIndex(int charCode, const std::vector<int> &charmapCompressed);

	void SetFontPixel(u32 base, int bpl, int bufWidth, int bufHeight, int x, int y, u8 pixelColor, FontPixelFormat pixelformat) const;

	PGFHeaderRev3Extra rev3extra;

	// Font character image data
	u8 *fontData;
	size_t fontDataSize;

	std::string fileName;

	std::vector<int> dimensionTable[2];
	std::vector<int> xAdjustTable[2];
	std::vector<int> yAdjustTable[2];
	std::vector<int> advanceTable[2];

	// Unused
	std::vector<int> charmapCompressionTable1[2];
	std::vector<int> charmapCompressionTable2[2];

	std::vector<int> charmap_compr;
	std::vector<int> charmap;

	std::vector<Glyph> glyphs;
	std::vector<Glyph> shadowGlyphs;
	int firstGlyph;
};
