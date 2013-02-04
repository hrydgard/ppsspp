#include "sceFont.h"

#include "base/timeutil.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "ChunkFile.h"


typedef u32 FontLibraryHandle;
typedef u32 FontHandle;

typedef struct {
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
} FontNewLibParams;

typedef enum {
	FONT_FAMILY_SANS_SERIF = 1,
	FONT_FAMILY_SERIF      = 2,
} Family;

typedef enum {
	FONT_STYLE_REGULAR     = 1,
	FONT_STYLE_ITALIC      = 2,
	FONT_STYLE_BOLD        = 5,
	FONT_STYLE_BOLD_ITALIC = 6,
	FONT_STYLE_DB          = 103, // Demi-Bold / semi-bold
} Style;

typedef enum {
	FONT_LANGUAGE_JAPANESE = 1,
	FONT_LANGUAGE_LATIN    = 2,
	FONT_LANGUAGE_KOREAN   = 3,
} Language;

typedef struct {
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
} FontStyle;

typedef struct {
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
} FontInfo;

typedef struct {
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
} CharInfo;

typedef enum {
	PSP_FONT_PIXELFORMAT_4 = 0,
	PSP_FONT_PIXELFORMAT_4_REV = 1,
	PSP_FONT_PIXELFORMAT_8 = 2,
	PSP_FONT_PIXELFORMAT_24 = 3,
	PSP_FONT_PIXELFORMAT_32 = 4
} FontPixelFormat;

typedef struct {
	FontPixelFormat pixelFormat;
	s32 xPos64;
	s32 yPos64;
	u16 bufWidth;
	u16 bufHeight;
	u16 bytesPerLine;
	u16 pad;
	u32 bufferPtr;
} GlyphImage;

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


int sceFontGetNumFontList(u32 libHandle, u32 errorCodePtr)
{	
	ERROR_LOG(HLE, "sceFontGetNumFontList %x, %x", libHandle, errorCodePtr);
	if (Memory::IsValidAddress(errorCodePtr))
	{
		Memory::Write_U32(0, errorCodePtr);
	}
	return 1;
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
		//fi.fontStyle.fontFileName="asd";
		fi.fontStyle.fontH=32;
		fi.fontStyle.fontHRes=32;
		fi.fontStyle.fontLanguage=1;
		//	fi.fontStyle.fontName="ppsspp";
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
	ERROR_LOG(HLE, "sceFontGetFontInfoByIndexNumber %x, %x, %x, %x", libHandle, fontInfoPtr, unknown, fontIndex);
	// clearly wrong..
	return sceFontGetFontInfo(libHandle, fontInfoPtr);

}

int sceFontGetCharInfo(u32 libHandler, u32 charCode, u32 charInfoPtr)
{
	ERROR_LOG(HLE, "sceFontGetCharInfo %x, %x, %x", libHandler, charCode, charInfoPtr);
	if (Memory::IsValidAddress(charInfoPtr))
	{
		CharInfo pspCharInfo;
		memset(&pspCharInfo, 0, sizeof(pspCharInfo));		
		pspCharInfo.bitmapWidth = 32;
		pspCharInfo.bitmapHeight = 32;

		pspCharInfo.spf26Width = pspCharInfo.bitmapWidth << 6;
		pspCharInfo.spf26Height = pspCharInfo.bitmapHeight << 6;
		pspCharInfo.spf26AdvanceH = pspCharInfo.bitmapWidth << 6;
		pspCharInfo.spf26AdvanceV = pspCharInfo.bitmapHeight << 6;
		Memory::WriteStruct(charInfoPtr, &pspCharInfo);
	}
	return 0;
}

int sceFontGetCharGlyphImage(u32 libHandler, u32 charCode, u32 glyphImagePtr)
{
	ERROR_LOG(HLE, "sceFontGetCharGlyphImage %x, %x, %x (%c)", libHandler, charCode, glyphImagePtr, charCode);

	int pixelFormat = Memory::Read_U32(glyphImagePtr);
	int xPos64 = Memory::Read_U32(glyphImagePtr+4);
	int yPos64 = Memory::Read_U32(glyphImagePtr+8);
	int bufWidth = Memory::Read_U16(glyphImagePtr+12);
	int bufHeight = Memory::Read_U16(glyphImagePtr+14);
	int bytesPerLine = Memory::Read_U16(glyphImagePtr+16);
	int buffer =Memory::Read_U32(glyphImagePtr+20);

	for (int y= 0; y < bufHeight; y++)
	{
		for (int x=0; x<bytesPerLine; x++)
		{
			Memory::Write_U8(0xff, buffer + (x * y));
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

	for (u32 i = 0; i < numFonts; i++)
	{
		Memory::WriteStruct(fontStylePtr+ (sizeof(style)), &style);
	}
	return 0;
}

const HLEFunction sceLibFont[] = 
{
	{0x67f17ed7, WrapU_UU<sceFontNewLib>, "sceFontNewLib"},	
	{0x574b6fbc, WrapI_U<sceFontDoneLib>, "sceFontDoneLib"},
	{0x48293280, 0, "sceFontSetResolution"},	
	{0x27f6e642, WrapI_UU<sceFontGetNumFontList>, "sceFontGetNumFontList"},
	{0xbc75d85b, WrapI_UUU<sceFontGetFontList>, "sceFontGetFontList"},	
	{0x099ef33c, WrapI_UUU<sceFontFindOptimumFont>, "sceFontFindOptimumFont"},	
	{0x681e61a7, WrapI_UUU<sceFontFindFont>, "sceFontFindFont"},	
	{0x2f67356a, 0, "sceFontCalcMemorySize"},	
	{0x5333322d, WrapI_UUUU<sceFontGetFontInfoByIndexNumber>, "sceFontGetFontInfoByIndexNumber"},
	{0xa834319d, WrapU_UUUU<sceFontOpen>, "sceFontOpen"},	
	{0x57fcb733, WrapU_UUUU<sceFontOpenUserFile>, "sceFontOpenUserFile"},	
	{0xbb8e7fe6, WrapU_UUUU<sceFontOpenUserMemory>, "sceFontOpenUserMemory"},	
	{0x3aea8cb6, WrapI_U<sceFontClose>, "sceFontClose"},	
	{0x0da7535e, WrapI_UU<sceFontGetFontInfo>, "sceFontGetFontInfo"},	
	{0xdcc80c2f, WrapI_UUU<sceFontGetCharInfo>, "sceFontGetCharInfo"},	
	{0x5c3e4a9e, 0, "sceFontGetCharImageRect"},	
	{0x980f4895, WrapI_UUU<sceFontGetCharGlyphImage>, "sceFontGetCharGlyphImage"},	
	{0xca1e6945, WrapI_UUUIIII<sceFontGetCharGlyphImage_Clip>, "sceFontGetCharGlyphImage_Clip"},
	{0x74b21701, 0, "sceFontPixelToPointH"},	
	{0xf8f0752e, 0, "sceFontPixelToPointV"},	
	{0x472694cd, 0, "sceFontPointToPixelH"},	
	{0x3c4b7e82, 0, "sceFontPointToPixelV"},	
	{0xee232411, 0, "sceFontSetAltCharacterCode"},
	{0xaa3de7b5, 0, "sceFontGetShadowInfo"}, 	 
	{0x48b06520, 0, "sceFontGetShadowImageRect"},
	{0x568be516, 0, "sceFontGetShadowGlyphImage"},
	{0x5dcf6858, 0, "sceFontGetShadowGlyphImage_Clip"},
	{0x02d7f94b, 0, "sceFontFlush"},

};
void Register_sceFont()
{
	RegisterModule("sceLibFont", ARRAY_SIZE(sceLibFont), sceLibFont);
}

