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

#include "Common/Log.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"

enum {
	FONT_FILETYPE_PGF = 0x00,
	FONT_FILETYPE_BWFON = 0x01,
};

enum {
	FONT_PGF_BMP_H_ROWS = 0x01,
	FONT_PGF_BMP_V_ROWS = 0x02,
	FONT_PGF_BMP_OVERLAY = 0x03,
	FONT_PGF_METRIC_FLAG1 = 0x04,
	FONT_PGF_METRIC_FLAG2 = 0x08,
	FONT_PGF_METRIC_FLAG3 = 0x10,
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
	PSP_FONT_PIXELFORMAT_4 = 0, // 2 pixels packed in 1 byte (natural order)
	PSP_FONT_PIXELFORMAT_4_REV = 1, // 2 pixels packed in 1 byte (reversed order)
	PSP_FONT_PIXELFORMAT_8 = 2, // 1 pixel in 1 byte
	PSP_FONT_PIXELFORMAT_24 = 3, // 1 pixel in 3 bytes (RGB)
	PSP_FONT_PIXELFORMAT_32 = 4, // 1 pixel in 4 bytes (RGBA)
};


struct PGFFontStyle {
	float  fontH;
	float  fontV;
	float  fontHRes;
	float  fontVRes;
	float  fontWeight;
	u16    fontFamily;
	u16    fontStyle;
	// Check.
	u16    fontStyleSub;
	u16    fontLanguage;
	u16    fontRegion;
	u16    fontCountry;
	char   fontName[64];
	char   fontFileName[64];
	u32    fontAttributes;
	u32    fontExpire;
};


struct Glyph {
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
};

struct GlyphImage {
	FontPixelFormat pixelFormat;
	s32 xPos64;
	s32 yPos64;
	u16 bufWidth;
	u16 bufHeight;
	u16 bytesPerLine;
	u16 pad;
	u32 bufferPtr;
};

#pragma pack(push,1)
struct PGFHeader
{
	u16 headerOffset;
	u16 headerSize;

	char PGFMagic[4];
	int revision;
	int version;

	int charMapLength;
	int charPointerLength;
	int charMapBpe;
	int charPointerBpe;

	u8 pad1[2];
	u8 bpp;
	u8 pad2[1];

	int hSize;
	int vSize;
	int hResolution;
	int vResolution;

	u8 pad3[1];
	char fontName[64];
	char fontType[64];
	u8 pad4[1];

	u16 firstGlyph;
	u16 lastGlyph;

	u8 pad5[26];

	int maxAscender;
	int maxDescender;
	int maxLeftXAdjust;
	int maxBaseYAdjust;
	int minCenterXAdjust;
	int maxTopYAdjust;

	int maxAdvance[2];
	int maxSize[2];
	u16 maxGlyphWidth;
	u16 maxGlyphHeight;
	u8 pad6[2];

	u8 dimTableLength;
	u8 xAdjustTableLength;
	u8 yAdjustTableLength;
	u8 advanceTableLength;
	u8 pad7[102];

	int shadowMapLength;
	int shadowMapBpe;
	float unknown1;
	int shadowScale[2];
	u8 pad8[8];
};

struct PGFHeaderRev3Extra {
	int compCharMapBpe1;
	int compCharMapLength1;
	int compCharMapBpe2;
	int compCharMapLength2;
	u32 unknown;
};

struct PGFCharInfo {
	u32 bitmapWidth;
	u32 bitmapHeight;
	u32 bitmapLeft;
	u32 bitmapTop;
	// Glyph metrics (in 26.6 signed fixed-point).
	u32 sfp26Width;
	u32 sfp26Height;
	s32 sfp26Ascender;
	s32 sfp26Descender;
	s32 sfp26BearingHX;
	s32 sfp26BearingHY;
	s32 sfp26BearingVX;
	s32 sfp26BearingVY;
	s32 sfp26AdvanceH;
	s32 sfp26AdvanceV;
	u8 pad[4];
};

struct PGFFontInfo {
	// Glyph metrics (in 26.6 signed fixed-point).
	int maxGlyphWidthI;
	int maxGlyphHeightI;
	int maxGlyphAscenderI;
	int maxGlyphDescenderI;
	int maxGlyphLeftXI;
	int maxGlyphBaseYI;
	int minGlyphCenterXI;
	int maxGlyphTopYI;
	int maxGlyphAdvanceXI;
	int maxGlyphAdvanceYI;

	// Glyph metrics (replicated as float).
	float maxGlyphWidthF;
	float maxGlyphHeightF;
	float maxGlyphAscenderF;
	float maxGlyphDescenderF;
	float maxGlyphLeftXF;
	float maxGlyphBaseYF;
	float minGlyphCenterXF;
	float maxGlyphTopYF;
	float maxGlyphAdvanceXF;
	float maxGlyphAdvanceYF;

	// Bitmap dimensions.
	short maxGlyphWidth;
	short maxGlyphHeight;
	int charMapLength;   // Number of elements in the font's charmap.
	int shadowMapLength; // Number of elements in the font's shadow charmap.

	// Font style (used by font comparison functions).
	PGFFontStyle fontStyle;

	int BPP; // Font's BPP.
};

#pragma pack(pop)

class PGF {
public:
	PGF();
	~PGF();

	void ReadPtr(const u8 *ptr, size_t dataSize);

	bool GetCharInfo(int charCode, PGFCharInfo *ci);
	void GetFontInfo(PGFFontInfo *fi);
	void DrawCharacter(const GlyphImage *image, int clipX, int clipY, int clipWidth, int clipHeight, int charCode, int altCharCode, int glyphType);

	void DoState(PointerWrap &p);

	PGFHeader header;

private:
	bool GetGlyph(const u8 *fontdata, size_t charPtr, int glyphType, Glyph &glyph);
	bool GetCharGlyph(int charCode, int glyphType, Glyph &glyph);

	// Unused
	int GetCharIndex(int charCode, const std::vector<int> &charmapCompressed);

	void SetFontPixel(u32 base, int bpl, int bufWidth, int bufHeight, int x, int y, int pixelColor, int pixelformat);

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
