#pragma once

class PointerWrap;

void Register_sceFont();

void __FontInit();
void __FontShutdown();
void __FontDoState(PointerWrap &p);

typedef u32 FontLibraryHandle;
typedef u32 FontHandle;

struct FontNewLibParams {
	u32 userDataAddr;
	u32 numFonts;
	u32 cacheDataAddr;

	// Driver callbacks.
	u32 allocFuncAddr;
	u32 freeFuncAddr;
	u32 openFuncAddr;
	u32 closeFuncAddr;
	u32 readFuncAddr;
	u32 seekFuncAddr;
	u32 errorFuncAddr;
	u32 ioFinishFuncAddr;
};

typedef enum Family {
	FONT_FAMILY_SANS_SERIF = 1,
	FONT_FAMILY_SERIF      = 2,
};

typedef enum Style {
	FONT_STYLE_REGULAR     = 1,
	FONT_STYLE_ITALIC      = 2,
	FONT_STYLE_BOLD        = 5,
	FONT_STYLE_BOLD_ITALIC = 6,
	FONT_STYLE_DB          = 103, // Demi-Bold / semi-bold
};

typedef enum Language {
	FONT_LANGUAGE_JAPANESE = 1,
	FONT_LANGUAGE_LATIN    = 2,
	FONT_LANGUAGE_KOREAN   = 3,
};

struct FontStyle {
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

struct iMerticsInfo{
	// Glyph metrics (in 26.6 signed fixed-point).
	u32 width;
	u32 height;
	u32 ascender;
	u32 descender;
	u32 h_bearingX;
	u32 h_bearingY;
	u32 v_bearingX;
	u32 v_bearingY;
	u32 h_advance;
	u32 v_advance;
};

struct fMerticsInfo{
	// Glyph metrics (in 26.6 signed fixed-point).
	float width;
	float height;
	float ascender;
	float descender;
	float h_bearingX;
	float h_bearingY;
	float v_bearingX;
	float v_bearingY;
	float h_advance;
	float v_advance;
};

struct FontInfo {
	struct iMerticsInfo maxInfoI;
	struct fMerticsInfo maxInfoF;

	// Bitmap dimensions.
	short maxGlyphWidth;
	short maxGlyphHeight;
	u32  charMapLength;   // Number of elements in the font's charmap.
	u32  shadowMapLength; // Number of elements in the font's shadow charmap.

	// Font style (used by font comparison functions).
	FontStyle fontStyle;

	u8 BPP; // Font's BPP.
	u8 pad[3];
};

struct CharInfo {
	u32 bitmapWidth;
	u32 bitmapHeight;
	u32 bitmapLeft;
	u32 bitmapTop;
	// Glyph metrics (in 26.6 signed fixed-point).
	struct iMerticsInfo info;
	u8 pad[4];
};

enum FontPixelFormat {
	PSP_FONT_PIXELFORMAT_4 = 0,
	PSP_FONT_PIXELFORMAT_4_REV = 1,
	PSP_FONT_PIXELFORMAT_8 = 2,
	PSP_FONT_PIXELFORMAT_24 = 3,
	PSP_FONT_PIXELFORMAT_32 = 4
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


typedef struct f26_pairs {
	int h;
	int v;
} F26_PAIRS;

typedef struct pgf_header_t {
	/* 0x0000 */
	u16 header_start;
	u16 header_len;
	u8  pgf_id[4];
	u32 revision;
	u32 version;

	/* 0x0010 */
	int charmap_len;
	int charptr_len;
	int charmap_bpe;
	int charptr_bpe;

	/* 0x0020 */
	u8  unk_20[2];  /* 04 04 */
	u8  bpp;		/* 04 */
	u8  unk_23;		/* 00 */

	u32 h_size;
	u32 v_size;
	u32 h_res;
	u32 v_res;

	u8  unk_34;		/* 00 */
	u8  font_name[64];	/* "FTT-NewRodin Pro DB" */
	u8  font_type[64];	/* "Regular" */
	u8  unk_B5;		/* 00 */

	u16 charmap_min;
	u16 charmap_max;

	/* 0x00BA */
	u16 unk_BA;		/* 0x0000 */
	u32 unk_BC;		/* 0x00010000 */
	u32 unk_C0;		/* 0x00000000 */
	u32 unk_C4;		/* 0x00000000 */
	u32 unk_C8;		/* 0x00010000 */
	u32 unk_CC;		/* 0x00000000 */
	u32 unk_D0;		/* 0x00000000 */

	int ascender;
	int descender;
	int max_h_bearingX;
	int max_h_bearingY;
	int min_v_bearingX;
	int max_v_bearingY;
	int max_h_advance;
	int max_v_advance;
	int max_h_dimension;
	int max_v_dimension;
	u16 max_glyph_w;
	u16 max_glyph_h;

	/* 0x0100 */
	u16 charptr_scale;	/* 0004 */
	u8  dimension_len;
	u8  bearingX_len;
	u8  bearingY_len;
	u8  advance_len;
	u8  unk_106[102];	/* 00 00 ... ... 00 */

	u32 shadowmap_len;
	u32 shadowmap_bpe;
	u32 unk_174;
	u32 shadowscale_x;
	u32 shadowscale_y;
	u32 unk_180;
	u32 unk_184;
} PGF_HEADER;

typedef struct glyph_t {
	int index;
	int ucs;
	int have_shadow;

	int size;		/* 14bits */
	int width;		/* 7bits */
	int height;		/* 7bits */
	int left;		/* 7bits signed */
	int top;		/* 7bits signed */
	int flag;		/* 6bits: 2+1+1+1+1 */

	int shadow_flag;/* 7bits: 2+2+3 */
	int shadow_id;	/* 9bits */

	F26_PAIRS dimension;
	F26_PAIRS bearingX;
	F26_PAIRS bearingY;
	F26_PAIRS advance;

	u8 *data;
	u8 *bmp;
} PGF_GLYPH;

typedef struct pgf_font_t {
	u8 *buf;

	PGF_HEADER *ph;

	struct f26_pairs *dimension;
	struct f26_pairs *bearingX;
	struct f26_pairs *bearingY;
	struct f26_pairs *advance;

	int *charmap;
	int *charptr;
	int *shadowmap;

	u8 *glyphdata;
	PGF_GLYPH *char_glyph[65536];
	PGF_GLYPH *shadow_glyph[512];

} PGF_FONT;




