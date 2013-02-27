

#include "base/timeutil.h"

#include "../Config.h"
#include "../Host.h"
#include "../SaveState.h"
#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../System.h"



#include "sceFont.h"

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

static const char *font_list[16] = {
	"flash0:/font/jpn0.pgf",
	"flash0:/font/ltn0.pgf",
	"flash0:/font/ltn2.pgf",
	"flash0:/font/ltn3.pgf",
	"flash0:/font/ltn4.pgf",
	"flash0:/font/ltn5.pgf",
	"flash0:/font/ltn6.pgf",
	"flash0:/font/ltn7.pgf",
	"flash0:/font/ltn8.pgf",
	"flash0:/font/ltn9.pgf",
	"flash0:/font/ltn10.pgf",
	"flash0:/font/ltn11.pgf",
	"flash0:/font/ltn12.pgf",
	"flash0:/font/ltn13.pgf",
	"flash0:/font/ltn14.pgf",
	"flash0:/font/ltn15.pgf",
};
static int font_num = 16;

static PGF_FONT *font_slot[8] = {NULL, };

static int get_font_slot(void)
{
	int i;

	for(i=0; i<8; i++){
		if(font_slot[i]==NULL)
			return i;
	}

	return -1;
}

static float f26_float(int value)
{
	float f, t;

	f = (float)(value>>6);
	t = (float)(value&0x3f);
	f += t/64;

	return f;
}

/******************************************************************************/


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
	DEBUG_LOG(HLE, "sceFontNewLib %x, %x", FontNewLibParamsPtr, errorCodePtr);

	if (Memory::IsValidAddress(FontNewLibParamsPtr)&&Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
		Memory::ReadStruct(FontNewLibParamsPtr, &fontLib);
	}

	return 1;
}

int sceFontDoneLib(u32 FontLibraryHandlePtr)
{
	DEBUG_LOG(HLE, "sceFontDoneLib %x", FontLibraryHandlePtr);
	return 0;
}


u32 sceFontOpen(u32 libHandle, u32 index, u32 mode, u32 errorCodePtr)
{
	int retv, slot;
	u8 *buf;

	DEBUG_LOG(HLE, "sceFontDoneLib %x, %x, %x, %x", libHandle, index, mode, errorCodePtr);

	retv = 0;
	slot = get_font_slot();
	if(slot==-1){
		retv = -1;
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}

	index -= 1;
	u32 h = pspFileSystem.OpenFile(font_list[index], FILEACCESS_READ);
	if (h == 0){
		ERROR_LOG(HLE, "sceFontOpen: %s not found", font_list[index]);
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(font_list[index]);
	buf = (u8*)malloc((size_t)info.size);
	pspFileSystem.ReadFile(h, buf, info.size);
	pspFileSystem.CloseFile(h);

	font_slot[slot] = load_pgf_from_buf(buf, (int)info.size);

	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);

	return slot+1;
}

u32 sceFontOpenUserMemory(u32 libHandle, u32 memoryFontAddrPtr, u32 memoryFontLength, u32 errorCodePtr)
{
	int retv, slot;
	u8 *buf;

	DEBUG_LOG(HLE, "sceFontOpenUserMemory %x, %x, %x, %x", libHandle, memoryFontAddrPtr, memoryFontLength, errorCodePtr);

	retv = 0;
	slot = get_font_slot();
	if(slot==-1){
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}

	if (!Memory::IsValidAddress(memoryFontAddrPtr)){
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}
	buf = (u8*) Memory::GetPointer(memoryFontAddrPtr);

	font_slot[slot] = load_pgf_from_buf(buf, memoryFontLength);

	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);

	return slot+1;
}

u32 sceFontOpenUserFile(u32 libHandle, u32 fileNamePtr, u32 mode, u32 errorCodePtr)
{
	int retv, slot;
	u8 *buf;
	char *filename;

	DEBUG_LOG(HLE, "sceFontOpenUserFile %x, %x, %x, %x", libHandle, fileNamePtr, mode, errorCodePtr);

	retv = 0;
	slot = get_font_slot();
	if(slot==-1){
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}

	if (!Memory::IsValidAddress(fileNamePtr)){
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}
	filename = (char*) Memory::GetPointer(fileNamePtr);

	u32 h = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
	if (h == 0){
		ERROR_LOG(HLE, "sceFontOpenUserFIle: %s not found", filename);
		if (Memory::IsValidAddress(errorCodePtr))
			Memory::Write_U32(-1, errorCodePtr);
		return 0;
	}

	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	buf = (u8*)malloc((size_t)info.size);
	pspFileSystem.ReadFile(h, buf, info.size);
	pspFileSystem.CloseFile(h);

	font_slot[slot] = load_pgf_from_buf(buf, (int)info.size);

	if (Memory::IsValidAddress(errorCodePtr))
		Memory::Write_U32(0, errorCodePtr);

	return slot+1;
}

int sceFontClose(u32 fontHandle)
{
	int slot;

	DEBUG_LOG(HLE, "sceFontClose %x", fontHandle);

	slot = fontHandle-1;
	free_pgf_font(font_slot[slot]);
	font_slot[slot] = NULL;

	return 0;
}

int sceFontFindOptimumFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr)
{
	DEBUG_LOG(HLE, "sceFontFindOptimumFont %x, %x, %x", libHandlePtr, fontStylePtr, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr)){
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

int sceFontFindFont(u32 libHandlePtr, u32 fontStylePtr, u32 errorCodePtr)
{
	DEBUG_LOG(HLE, "sceFontFindFont %x, %x, %x", libHandlePtr, fontStylePtr, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr)){
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
}

int sceFontGetFontInfo(u32 fontHandle, u32 fontInfoPtr)
{
	PGF_FONT *pgft;
	PGF_HEADER *ph;
	int slot;

	DEBUG_LOG(HLE, "sceFontGetFontInfo %x, %x", fontHandle, fontInfoPtr);

	slot = fontHandle-1;
	pgft = font_slot[slot];
	ph = (PGF_HEADER*)pgft->buf;

	FontInfo fi;
	memset (&fi, 0, sizeof(fi));
	if (Memory::IsValidAddress(fontInfoPtr))
	{
		fi.BPP = 4;
		fi.charMapLength = ph->charmap_len;
		fi.shadowMapLength = ph->shadowmap_len;

		fi.maxInfoI.width      = ph->max_glyph_w<<6;
		fi.maxInfoI.height     = ph->max_glyph_h<<6;
		fi.maxInfoI.ascender   = ph->ascender;
		fi.maxInfoI.descender  = ph->descender;
		fi.maxInfoI.h_bearingX = ph->max_h_bearingX;
		fi.maxInfoI.h_bearingY = ph->max_h_bearingY;
		fi.maxInfoI.v_bearingX = ph->min_v_bearingX;
		fi.maxInfoI.v_bearingY = ph->max_v_bearingY;
		fi.maxInfoI.h_advance  = ph->max_h_advance;
		fi.maxInfoI.v_advance  = ph->max_v_advance;

		fi.maxInfoF.width      = (float)(ph->max_glyph_w);
		fi.maxInfoF.height     = (float)(ph->max_glyph_h);
		fi.maxInfoF.ascender   = f26_float(ph->ascender);
		fi.maxInfoF.descender  = f26_float(ph->descender);
		fi.maxInfoF.h_bearingX = f26_float(ph->max_h_bearingX);
		fi.maxInfoF.h_bearingY = f26_float(ph->max_h_bearingY);
		fi.maxInfoF.v_bearingX = f26_float(ph->min_v_bearingX);
		fi.maxInfoF.v_bearingY = f26_float(ph->max_v_bearingY);
		fi.maxInfoF.h_advance  = f26_float(ph->max_h_advance);
		fi.maxInfoF.v_advance  = f26_float(ph->max_v_advance);

		fi.maxGlyphHeight      = ph->max_glyph_h;
		fi.maxGlyphWidth       = ph->max_glyph_w;

		fi.fontStyle.fontAttributes = 1;
		fi.fontStyle.fontCountry    = 1;
		fi.fontStyle.fontExpire     = 1;
		fi.fontStyle.fontFamily     = 1;
		fi.fontStyle.fontH          = f26_float(ph->h_size);
		fi.fontStyle.fontHRes       = f26_float(ph->h_res);
		fi.fontStyle.fontLanguage   = 1;
		fi.fontStyle.fontRegion     = 9;
		fi.fontStyle.fontV          = f26_float(ph->v_size);
		fi.fontStyle.fontVRes       = f26_float(ph->v_res);
		fi.fontStyle.fontWeight     = 32;
		strcpy(fi.fontStyle.fontFileName, font_list[slot]);
		strcpy(fi.fontStyle.fontName,     (const char*)ph->font_name);

		Memory::WriteStruct(fontInfoPtr, &fi);
	}

	return 0;
}

int sceFontGetFontInfoByIndexNumber(u32 libHandle, u32 fontInfoPtr, u32 fontIndex)
{
	ERROR_LOG(HLE, "HACK sceFontGetFontInfoByIndexNumber %x, %x, %x", libHandle, fontInfoPtr, fontIndex);
	// clearly wrong..
	return -1;

}

int sceFontGetCharInfo(u32 fontHandle, u32 charCode, u32 charInfoPtr)
{
	PGF_FONT *pgft;
	PGF_HEADER *ph;
	PGF_GLYPH *gh;
	int slot;

	DEBUG_LOG(HLE, "HACK sceFontGetCharInfo %x, %x, %x", fontHandle, charCode, charInfoPtr);

	slot = fontHandle-1;
	pgft = font_slot[slot];
	if(pgft==NULL)
		return -1;

	ph = (PGF_HEADER*)pgft->buf;
	gh = pgft->char_glyph[charCode];
	if(gh==NULL)
		return -2;

	if (Memory::IsValidAddress(charInfoPtr)) {
		CharInfo pspCharInfo;
		memset(&pspCharInfo, 0, sizeof(pspCharInfo));

		pspCharInfo.bitmapWidth  = gh->width;
		pspCharInfo.bitmapHeight = gh->height;
		pspCharInfo.bitmapLeft   = gh->left;
		pspCharInfo.bitmapTop    = gh->top;

		pspCharInfo.info.width      = gh->dimension.h;
		pspCharInfo.info.height     = gh->dimension.v;
		pspCharInfo.info.ascender   = ph->ascender;
		pspCharInfo.info.descender  = ph->descender;
		pspCharInfo.info.h_bearingX = gh->bearingX.h;
		pspCharInfo.info.h_bearingY = gh->bearingY.h;
		pspCharInfo.info.v_bearingX = gh->bearingX.v;
		pspCharInfo.info.v_bearingY = gh->bearingY.v;
		pspCharInfo.info.h_advance  = gh->advance.h;
		pspCharInfo.info.v_advance  = gh->advance.v;

		Memory::WriteStruct(charInfoPtr, &pspCharInfo);
	}
	return 0;
}

int sceFontGetCharImageRect(u32 fontHandle, u32 charCode, u32 charRectPtr)
{
	PGF_FONT *pgft;
	PGF_GLYPH *gh;
	int slot;

	DEBUG_LOG(HLE, "HACK sceFontGetCharImageRect %x, %x (%c)", fontHandle, charRectPtr, charCode);

	slot = fontHandle-1;
	pgft = font_slot[slot];
	if(pgft==NULL)
		return -1;

	gh = pgft->char_glyph[charCode];
	if(gh==NULL)
		return -2;

	if (Memory::IsValidAddress(charRectPtr)) {
		Memory::Write_U16(gh->width, charRectPtr);    // character bitmap width in pixels
		Memory::Write_U16(gh->width, charRectPtr+2);  // character bitmap height in pixels
	}

	return 0;
}

int sceFontGetCharGlyphImage(u32 fontHandle, u32 charCode, u32 glyphImagePtr)
{
	PGF_FONT *pgft;
	PGF_GLYPH *gh;
	int slot;
	u8 line_buf[512];

	DEBUG_LOG(HLE, "HACK sceFontGetCharGlyphImage %x, %x, %x (%c)", fontHandle, charCode, glyphImagePtr, charCode);

	slot = fontHandle-1;
	pgft = font_slot[slot];
	if(pgft==NULL)
		return -1;

	gh = pgft->char_glyph[charCode];
	if(gh==NULL)
		return -2;

	int pixelFormat  = Memory::Read_U32(glyphImagePtr);
	int xPos64       = Memory::Read_U32(glyphImagePtr+4);
	int yPos64       = Memory::Read_U32(glyphImagePtr+8);
	int bufWidth     = Memory::Read_U16(glyphImagePtr+12);
	int bufHeight    = Memory::Read_U16(glyphImagePtr+14);
	int bytesPerLine = Memory::Read_U16(glyphImagePtr+16);
	int buffer       = Memory::Read_U32(glyphImagePtr+20);

	xPos64 = (xPos64+32)>>6;
	yPos64 = (yPos64+32)>>6;

	int line = buffer + yPos64*bytesPerLine;
	u8 *src = gh->bmp;

	for (int y=0; y<gh->height; y++) {
		Memory::Memcpy(line_buf, line, bytesPerLine);
		int xp = xPos64;
		for (int x=0; x<gh->width; x++) {
			if(xp&1){
				line_buf[xp/2] &= 0x0f; 
				line_buf[xp/2] |= (*src)<<4; 
			}else{
				line_buf[xp/2] &= 0xf0; 
				line_buf[xp/2] |= (*src); 
			}
			xp += 1;
			src += 1;
		}
		Memory::Memcpy(line, line_buf, bytesPerLine);
		line += bytesPerLine;
	}

	return 0;
}

int sceFontGetCharGlyphImage_Clip(u32 libHandler, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight)
{
	DEBUG_LOG(HLE, "sceFontGetCharGlyphImage_Clip %x, %x, %x (%c)", libHandler, charCode, glyphImagePtr, charCode);
	return sceFontGetCharGlyphImage(libHandler, charCode, glyphImagePtr);
}

int sceFontSetAltCharacterCode(u32 libHandle, u32 charCode)
{
	DEBUG_LOG(HLE, "sceFontSetAltCharacterCode %x (%c)", libHandle, charCode);
	return 0;
}

int sceFontFlush(u32 fontHandle)
{
	DEBUG_LOG(HLE, "sceFontFlush(%i)", fontHandle);
	return 0;
}

int sceFontGetFontList(u32 fontLibHandle, u32 fontStylePtr, u32 numFonts)
{
	DEBUG_LOG(HLE, "sceFontGetFontList %x, %x, %x", fontLibHandle, fontStylePtr, numFonts);

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
	DEBUG_LOG(HLE, "UNIMPL sceFontGetNumFontList %x, %x", libHandle, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return font_num;
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
	{0x5333322d, WrapI_UUU<sceFontGetFontInfoByIndexNumber>, "sceFontGetFontInfoByIndexNumber"},
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

