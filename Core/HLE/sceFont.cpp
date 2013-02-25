#include "sceFont.h"

#include "base/timeutil.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "ChunkFile.h"


/******************************************************************************/

static int get_value(int bpe, u8 *buf, int *pos)
{
	int i, v;

	v = 0;
	for(i=0; i<bpe; i++){
		v += ( ( buf[(*pos)/8] >> ((*pos)%8) ) &1 ) << i;
		(*pos)++;
	}

	return v;
}

static int read_table(u8 *buf, int *table, int num, int bpe)
{
	int i, p, len;

	len = ((num*bpe+31)/32)*4;

	p = 0;
	for(i=0; i<num; i++){
		table[i] = get_value(bpe, buf, &p);
	}

	return len;
}

static u32 ptr2ucs(PGF_FONT *pgft, int ptr)
{
	PGF_HEADER *ph;
	int i, n_charmap;

	ph = (PGF_HEADER*)pgft->buf;
	n_charmap = ph->charmap_len;

	for(i=0; i<n_charmap; i++){
		if(pgft->charmap[i]==ptr){
			return i+ph->charmap_min;
		}
	}

	return 0xffff;
}

static int have_shadow(PGF_FONT *pgft, u32 ucs)
{
	PGF_HEADER *ph;
	int i, n_shadowmap;

	ph = (PGF_HEADER*)pgft->buf;
	n_shadowmap = ph->shadowmap_len;

	for(i=0; i<n_shadowmap; i++){
		if(pgft->shadowmap[i]==ucs){
			return 1;
		}
	}

	return 0;
}

static void get_bitmap(PGF_GLYPH *glyph)
{
	int i, j, p, nb, data, len;
	u8 *bmp, temp_buf[64*64];

	i = 0;
	p = 0;
	len = glyph->width * glyph->height;
	if((glyph->flag&3)==2){
		bmp = temp_buf;
	}else{
		bmp = glyph->bmp;
	}

	while(i<len){
		nb = get_value(4, glyph->data, &p);
		if(nb<8){
			data = get_value(4, glyph->data, &p);
			for(j=0; j<nb+1; j++){
				bmp[i] = data;
				i++;
			}
		}else{
			for(j=0; j<(16-nb); j++){
				data = get_value(4, glyph->data, &p);
				bmp[i] = data;
				i++;
			}
		}
	}

	if((glyph->flag&3)==2){
		int h, v;

		i = 0;
		for(h=0; h<glyph->width; h++){
			for(v=0; v<glyph->height; v++){
				glyph->bmp[v*glyph->width+h] = bmp[i];
				i++;
			}
		}
	}

}

static void load_shadow_glyph(u8 *ptr, PGF_GLYPH *glyph)
{
	int pos;

	pos = 0;

	glyph->size   = get_value(14, ptr, &pos);
	glyph->width  = get_value(7, ptr, &pos);
	glyph->height = get_value(7, ptr, &pos);
	glyph->left   = get_value(7, ptr, &pos);
	glyph->top    = get_value(7, ptr, &pos);
	glyph->flag   = get_value(6, ptr, &pos);

	if(glyph->left>63) glyph->left |= 0xffffff80;
	if(glyph->top >63) glyph->top  |= 0xffffff80;

	glyph->data = ptr+(pos/8);
	glyph->bmp = (u8*)malloc(glyph->width*glyph->height);
	get_bitmap(glyph);
}

static void load_char_glyph(PGF_FONT *pgft, int index, PGF_GLYPH *glyph)
{
	int id, pos;
	u8 *ptr;

	ptr = pgft->glyphdata + pgft->charptr[index];
	pos = 0;

	glyph->index = index;
	glyph->have_shadow = have_shadow(pgft, glyph->ucs);

	glyph->size   = get_value(14, ptr, &pos);
	glyph->width  = get_value( 7, ptr, &pos);
	glyph->height = get_value( 7, ptr, &pos);
	glyph->left   = get_value( 7, ptr, &pos);
	glyph->top    = get_value( 7, ptr, &pos);
	glyph->flag   = get_value( 6, ptr, &pos);

	if(glyph->left>63) glyph->left |= 0xffffff80;
	if(glyph->top >63) glyph->top  |= 0xffffff80;

	/* read extension info */
	glyph->shadow_flag = get_value(7, ptr, &pos);
	glyph->shadow_id   = get_value(9, ptr, &pos);
	if(glyph->flag&0x04){
		id = get_value(8, ptr, &pos);
		glyph->dimension.h = pgft->dimension[id].h;
		glyph->dimension.v = pgft->dimension[id].v;
	}else{
		glyph->dimension.h = get_value(32, ptr, &pos);
		glyph->dimension.v = get_value(32, ptr, &pos);
	}
	if(glyph->flag&0x08){
		id = get_value(8, ptr, &pos);
		glyph->bearingX.h = pgft->bearingX[id].h;
		glyph->bearingX.v = pgft->bearingX[id].v;
	}else{
		glyph->bearingX.h = get_value(32, ptr, &pos);
		glyph->bearingX.v = get_value(32, ptr, &pos);
	}
	if(glyph->flag&0x10){
		id = get_value(8, ptr, &pos);
		glyph->bearingY.h = pgft->bearingY[id].h;
		glyph->bearingY.v = pgft->bearingY[id].v;
	}else{
		glyph->bearingY.h = get_value(32, ptr, &pos);
		glyph->bearingY.v = get_value(32, ptr, &pos);
	}
	if(glyph->flag&0x20){
		id = get_value(8, ptr, &pos);
		glyph->advance.h = pgft->advance[id].h;
		glyph->advance.v = pgft->advance[id].v;
	}else{
		glyph->advance.h = get_value(32, ptr, &pos);
		glyph->advance.v = get_value(32, ptr, &pos);
	}

	glyph->data = ptr+(pos/8);
	glyph->bmp = (u8*)malloc(glyph->width*glyph->height);
	get_bitmap(glyph);

	if(glyph->have_shadow){
		id = glyph->shadow_id;
		pgft->shadow_glyph[id] = (PGF_GLYPH*)malloc(sizeof(PGF_GLYPH));
		memset(pgft->shadow_glyph[id], 0, sizeof(PGF_GLYPH));
		load_shadow_glyph(ptr+glyph->size, pgft->shadow_glyph[id]);
	}

}


int load_all_glyph(PGF_FONT *pgft)
{
	PGF_GLYPH *glyph;
	PGF_HEADER *ph;
	int i, n_chars, ucs;

	ph = (PGF_HEADER*)pgft->buf;
	n_chars = ph->charptr_len;

	for(i=0; i<n_chars; i++){
		glyph = (PGF_GLYPH*)malloc(sizeof(PGF_GLYPH));
		memset(glyph, 0, sizeof(PGF_GLYPH));
		ucs = ptr2ucs(pgft, i);
		glyph->ucs = ucs;
		pgft->char_glyph[ucs] = glyph;
		load_char_glyph(pgft, i, glyph);
	}

	return 0;
}


PGF_FONT *load_pgf_from_buf(u8 *buf, int length)
{
	PGF_FONT *pgft;
	PGF_HEADER *ph;
	int i;

	pgft = (PGF_FONT*)malloc(sizeof(PGF_FONT));
	memset(pgft, 0, sizeof(PGF_FONT));

	pgft->buf = buf;

	/* pgf header */
	ph = (PGF_HEADER*)buf;
	buf += ph->header_len;

	/* dimension table */
	pgft->dimension = (F26_PAIRS*)buf;
	buf += (ph->dimension_len*8);

	/* left bearing table */
	pgft->bearingX = (F26_PAIRS*)buf;
	buf += (ph->bearingX_len*8);

	/* top bearing table */
	pgft->bearingY = (F26_PAIRS*)buf;
	buf += (ph->bearingY_len*8);

	/* advance table */
	pgft->advance = (F26_PAIRS*)buf;
	buf += (ph->advance_len*8);

	/* read shadowmap table */
	if(ph->shadowmap_len){
		pgft->shadowmap = (int*)malloc(ph->shadowmap_len*4);
		buf += read_table(buf, pgft->shadowmap, ph->shadowmap_len, ph->shadowmap_bpe);
	}

	/* read charmap table */
	pgft->charmap = (int*)malloc(ph->charmap_len*4);
	buf += read_table(buf, pgft->charmap, ph->charmap_len, ph->charmap_bpe);

	/* read charptr table */
	pgft->charptr = (int*)malloc(ph->charptr_len*4);
	buf += read_table(buf, pgft->charptr, ph->charptr_len, ph->charptr_bpe);
	for(i=0; i<ph->charptr_len; i++){
		pgft->charptr[i] *= ph->charptr_scale;
	}

	/* font glyph data */
	pgft->glyphdata = buf;

	load_all_glyph(pgft);

	return pgft;
}

PGF_GLYPH *get_glyph(PGF_FONT *pgft, int ucs)
{
	return pgft->char_glyph[ucs];
}

void free_glyph(PGF_FONT *pgft, PGF_GLYPH *glyph)
{
	int ucs;

	ucs = glyph->ucs;
	free(glyph->bmp);
	free(glyph);
	pgft->char_glyph[ucs] = 0;
}

void free_pgf_font(PGF_FONT *pgft)
{


}



/******************************************************************************/

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

struct FontInfo {
	// Glyph metrics (in 26.6 signed fixed-point).
	u32 maxGlyphWidthI;
	u32 maxGlyphHeightI;
	u32 maxGlyphAscenderI;
	u32 maxGlyphDescenderI;
	u32 maxGlyphLeftXI;
	u32 maxGlyphBaseYI;
	u32 minGlyphCenterXI;
	u32 maxGlyphTopYI;
	u32 maxGlyphAdvanceXI;
	u32 maxGlyphAdvanceYI;

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
	u32 spf26Width;
	u32 spf26Height;
	s32 spf26Ascender;
	s32 spf26Descender;
	s32 spf26BearingHX;
	s32 spf26BearingHY;
	s32 spf26BearingVX;
	s32 spf26BearingVY;
	s32 spf26AdvanceH;
	s32 spf26AdvanceV;
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

FontNewLibParams fontLib;

void __FontInit()
{
	memset(&fontLib, 0, sizeof(fontLib));
}

void __FontDoState(PointerWrap &p)
{
	p.Do(fontLib);
	p.DoMarker("sceFont");
}

u32 sceFontNewLib(u32 FontNewLibParamsPtr, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontNewLib %x, %x", FontNewLibParamsPtr, errorCodePtr);

	if (Memory::IsValidAddress(FontNewLibParamsPtr)&&Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
		Memory::ReadStruct(FontNewLibParamsPtr, &fontLib);
	}

	return 1;
}


int sceFontDoneLib(u32 FontLibraryHandlePtr)
{
	ERROR_LOG(HLE, "sceFontDoneLib %x", FontLibraryHandlePtr);
	return 0;
}


u32 sceFontOpen(u32 libHandle, u32 index, u32 mode, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontDoneLib %x, %x, %x, %x", libHandle, index, mode, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

u32 sceFontOpenUserMemory(u32 libHandle, u32 memoryFontAddrPtr, u32 memoryFontLength, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontOpenUserMemory %x, %x, %x, %x", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

u32 sceFontOpenUserFile(u32 libHandle, u32 fileNamePtr, u32 mode, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontOpenUserFile %x, %x, %x, %x", libHandle, fileNamePtr, mode, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

int sceFontClose(u32 fontHandle)
{
	ERROR_LOG(HLE, "sceFontClose %x", fontHandle);
	return 0;
}

int sceFontFindOptimumFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontFindOptimumFont %x, %x, %x", libHandlePtr, fontStylePtr, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

int sceFontFindFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontFindFont %x, %x, %x", libHandlePtr, fontStylePtr, errorCodePtr);
	return 1;
}

int sceFontGetFontInfo(u32 fontHandle, u32 fontInfoPtr)
{
	ERROR_LOG(HLE, "sceFontGetFontInfo %x, %x", fontHandle, fontInfoPtr);

	FontInfo fi;
	memset (&fi, 0, sizeof(fi));
	if (Memory::IsValidAddress(fontInfoPtr))
	{
		fi.BPP = 4;
		fi.charMapLength = 255;
		fi.maxGlyphAdvanceXF = 2.0;
		fi.maxGlyphAdvanceXI = 2;
		fi.maxGlyphAdvanceYF = 2.0;
		fi.maxGlyphAdvanceYI = 32 << 6;
		fi.maxGlyphAscenderF = 32 << 6;
		fi.maxGlyphAscenderI = 32 << 6;
		fi.maxGlyphBaseYF = 0.0;
		fi.maxGlyphBaseYI = 0;
		fi.maxGlyphDescenderF = 0;
		fi.maxGlyphDescenderI = 0;
		fi.maxGlyphHeight = 32;
		fi.maxGlyphHeightF = 32;
		fi.maxGlyphHeightI = 32;
		fi.maxGlyphLeftXF = 0;
		fi.maxGlyphLeftXI = 0;
		fi.maxGlyphTopYF = 0;
		fi.maxGlyphTopYI = 0;
		fi.maxGlyphWidth = 32;
		fi.maxGlyphWidthF = 32;
		fi.maxGlyphWidthI = 32;
		fi.minGlyphCenterXF = 16;
		fi.minGlyphCenterXI = 16;
		fi.shadowMapLength = 0;
		fi.fontStyle.fontAttributes=1;
		fi.fontStyle.fontCountry= 1;
		fi.fontStyle.fontExpire= 1;
		fi.fontStyle.fontFamily= 1;
		strcpy(fi.fontStyle.fontFileName, "asd");
		fi.fontStyle.fontH=32;
		fi.fontStyle.fontHRes=32;
		fi.fontStyle.fontLanguage=1;
		strcpy(fi.fontStyle.fontName, "ppsspp");
		fi.fontStyle.fontRegion=9;
		fi.fontStyle.fontV=32;
		fi.fontStyle.fontVRes=32;
		fi.fontStyle.fontWeight= 32;
		Memory::WriteStruct(fontInfoPtr, &fi);
	}

	return 0;
}

int sceFontGetFontInfoByIndexNumber(u32 libHandle, u32 fontInfoPtr, u32 unknown, u32 fontIndex)
{
	ERROR_LOG(HLE, "HACK sceFontGetFontInfoByIndexNumber %x, %x, %x, %x", libHandle, fontInfoPtr, unknown, fontIndex);
	// clearly wrong..
	return sceFontGetFontInfo(libHandle, fontInfoPtr);

}

int sceFontGetCharInfo(u32 fontHandle, u32 charCode, u32 charInfoPtr)
{
	ERROR_LOG(HLE, "HACK sceFontGetCharInfo %x, %x, %x", fontHandle, charCode, charInfoPtr);
	if (Memory::IsValidAddress(charInfoPtr))
	{
		CharInfo pspCharInfo;
		memset(&pspCharInfo, 0, sizeof(pspCharInfo));		
		pspCharInfo.bitmapWidth = 16;
		pspCharInfo.bitmapHeight = 16;

		pspCharInfo.spf26Width = pspCharInfo.bitmapWidth << 6;
		pspCharInfo.spf26Height = pspCharInfo.bitmapHeight << 6;
		pspCharInfo.spf26AdvanceH = pspCharInfo.bitmapWidth << 6;
		pspCharInfo.spf26AdvanceV = pspCharInfo.bitmapHeight << 6;
		Memory::WriteStruct(charInfoPtr, &pspCharInfo);
	}
	return 0;
}

int sceFontGetCharImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr)
{
	ERROR_LOG(HLE, "HACK sceFontGetCharImageRect %x, %x (%c)", fontHandle, charRectPtr, charCode);
	if (Memory::IsValidAddress(charRectPtr)) {
		Memory::Write_U16(16, charRectPtr);  // character bitmap width in pixels
		Memory::Write_U16(16, charRectPtr + 2);  // character bitmap height in pixels
	}
	return 0;
}

int sceFontGetCharGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr)
{
	ERROR_LOG(HLE, "HACK sceFontGetCharGlyphImage %x, %x, %x (%c)", fontHandle, charCode, glyphImagePtr, charCode);

	int pixelFormat = Memory::Read_U32(glyphImagePtr);
	int xPos64 = Memory::Read_U32(glyphImagePtr+4);
	int yPos64 = Memory::Read_U32(glyphImagePtr+8);
	int bufWidth = Memory::Read_U16(glyphImagePtr+12);
	int bufHeight = Memory::Read_U16(glyphImagePtr+14);
	int bytesPerLine = Memory::Read_U16(glyphImagePtr+16);
	int buffer = Memory::Read_U32(glyphImagePtr+20);

	// Small chessboard. Does not respect pixelformat currently...

	// Actually should be really easy to substitute in a proper font here...
	// could even grab pixel data from the PPGe one.
	for (int y = 0; y < bufHeight; y++)
	{
		for (int x = 0; x < bytesPerLine; x++)
		{
			Memory::Write_U8((((x >> 1) ^ (y >> 1)) & 1) ? 0xff : 0x00, buffer + (y * bytesPerLine + x));
		}
	}

	return 0;
}

int sceFontGetCharGlyphImage_Clip(u32 libHandler, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight)
{
	ERROR_LOG(HLE, "sceFontGetCharGlyphImage_Clip %x, %x, %x (%c)", libHandler, charCode, glyphImagePtr, charCode);
	//sceFontGetCharGlyphImage(libHandler, charCode, glyphImagePtr);
	return 0;
}

int sceFontSetAltCharacterCode(u32 libHandle, u32 charCode)
{
	ERROR_LOG(HLE, "sceFontSetAltCharacterCode %x (%c)", libHandle, charCode);
	return 0;
}

int sceFontFlush(u32 fontHandle)
{
	DEBUG_LOG(HLE, "sceFontFlush(%i)", fontHandle);
	return 0;
}

int sceFontGetFontList(u32 fontLibHandle, u32 fontStylePtr, u32 numFonts)
{
	ERROR_LOG(HLE, "sceFontGetFontList %x, %x, %x", fontLibHandle, fontStylePtr, numFonts);

	FontStyle style;
	memset(&style, 0, sizeof (style));

	style.fontH = 20 / 64.f;
	style.fontV = 20 / 64.f;
	style.fontHRes = 20 / 64.f;
	style.fontVRes = 20 / 64.f;
	style.fontStyle = 1;
	//style.fontFamily

	for (u32 i = 0; i < numFonts; i++)
	{
		Memory::WriteStruct(fontStylePtr + (sizeof(style)) * i, &style);
	}
	return 0;
}

int sceFontGetNumFontList(u32 libHandle, u32 errorCodePtr)
{	
	ERROR_LOG(HLE, "UNIMPL sceFontGetNumFontList %x, %x", libHandle, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

int sceFontSetResolution(u32 fontLibHandle, float hRes, float vRes) 
{
	ERROR_LOG(HLE, "UNIMPL sceFontSetResolution(%i, %f, %f)", fontLibHandle, hRes, vRes);
	return 0;
}

int sceFontCalcMemorySize() {
	ERROR_LOG(HLE, "UNIMPL sceFontCalcMemorySize()");
	return 0;
}



const HLEFunction sceLibFont[] = 
{
	{0x67f17ed7, WrapU_UU<sceFontNewLib>, "sceFontNewLib"},	
	{0x574b6fbc, WrapI_U<sceFontDoneLib>, "sceFontDoneLib"},
	{0x48293280, WrapI_UFF<sceFontSetResolution>, "sceFontSetResolution"},	
	{0x27f6e642, WrapI_UU<sceFontGetNumFontList>, "sceFontGetNumFontList"},
	{0xbc75d85b, WrapI_UUU<sceFontGetFontList>, "sceFontGetFontList"},	
	{0x099ef33c, WrapI_UUU<sceFontFindOptimumFont>, "sceFontFindOptimumFont"},	
	{0x681e61a7, WrapI_UUU<sceFontFindFont>, "sceFontFindFont"},	
	{0x2f67356a, WrapI_V<sceFontCalcMemorySize>, "sceFontCalcMemorySize"},	
	{0x5333322d, WrapI_UUUU<sceFontGetFontInfoByIndexNumber>, "sceFontGetFontInfoByIndexNumber"},
	{0xa834319d, WrapU_UUUU<sceFontOpen>, "sceFontOpen"},	
	{0x57fcb733, WrapU_UUUU<sceFontOpenUserFile>, "sceFontOpenUserFile"},	
	{0xbb8e7fe6, WrapU_UUUU<sceFontOpenUserMemory>, "sceFontOpenUserMemory"},	
	{0x3aea8cb6, WrapI_U<sceFontClose>, "sceFontClose"},	
	{0x0da7535e, WrapI_UU<sceFontGetFontInfo>, "sceFontGetFontInfo"},	
	{0xdcc80c2f, WrapI_UUU<sceFontGetCharInfo>, "sceFontGetCharInfo"},	
	{0x5c3e4a9e, WrapI_UUU<sceFontGetCharImageRect>, "sceFontGetCharImageRect"},	
	{0x980f4895, WrapI_UUU<sceFontGetCharGlyphImage>, "sceFontGetCharGlyphImage"},	
	{0xca1e6945, WrapI_UUUIIII<sceFontGetCharGlyphImage_Clip>, "sceFontGetCharGlyphImage_Clip"},
	{0x74b21701, 0, "sceFontPixelToPointH"},	
	{0xf8f0752e, 0, "sceFontPixelToPointV"},	
	{0x472694cd, 0, "sceFontPointToPixelH"},	
	{0x3c4b7e82, 0, "sceFontPointToPixelV"},	
	{0xee232411, WrapI_UU<sceFontSetAltCharacterCode>, "sceFontSetAltCharacterCode"},
	{0xaa3de7b5, 0, "sceFontGetShadowInfo"}, 	 
	{0x48b06520, 0, "sceFontGetShadowImageRect"},
	{0x568be516, 0, "sceFontGetShadowGlyphImage"},
	{0x5dcf6858, 0, "sceFontGetShadowGlyphImage_Clip"},
	{0x02d7f94b, WrapI_U<sceFontFlush>, "sceFontFlush"},

};
void Register_sceFont()
{
	RegisterModule("sceLibFont", ARRAY_SIZE(sceLibFont), sceLibFont);
}

