#pragma once

class PointerWrap;

void Register_sceFont();

void __FontInit();
void __FontDoState(PointerWrap &p);


typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;

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




