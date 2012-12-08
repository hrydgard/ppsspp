#include "sceFont.h"

#include "base/timeutil.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceFont.h"


typedef u32 FontLibraryHandle;
typedef u32 FontHandle;

typedef struct {
	u32* userDataAddr;
	u32  numFonts;
	u32* cacheDataAddr;

	// Driver callbacks.
	void *(*allocFuncAddr)(void *, u32);
	void  (*freeFuncAddr )(void *, void *);
	u32* openFuncAddr;
	u32* closeFuncAddr;
	u32* readFuncAddr;
	u32* seekFuncAddr;
	u32* errorFuncAddr;
	u32* ioFinishFuncAddr;
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
	void *buffer;
} GlyphImage;

FontNewLibParams fontLib;

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
	return 1;
}

u32 sceFontOpenUserFile(u32 libHandle, u32 fileNamePtr, u32 mode, u32 errorCodePtr)
{
	ERROR_LOG(HLE, "sceFontOpenUserFile %x, %x, %x, %x", libHandle, fileNamePtr, mode, errorCodePtr);
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
	return 0;
}

int sceFontGetFontInfoByIndexNumber(u32 libHandle, u32 fontInfoPtr, u32 unknown, u32 fontIndex)
{
	ERROR_LOG(HLE, "sceFontGetFontInfoByIndexNumber %x, %x, %x, %x", libHandle, fontInfoPtr, unknown, fontIndex);
	return 0;
}

int sceFontGetCharInfo(u32 libHandler, u32 charCode, u32 charInfoPtr)
{
	ERROR_LOG(HLE, "sceFontGetCharInfo %x, %x, %x", libHandler, charCode, charInfoPtr);
	if (Memory::IsValidAddress(charInfoPtr))
	{
		CharInfo pspCharInfo;
		memset(&pspCharInfo, 0, sizeof(pspCharInfo));
		pspCharInfo.bitmapWidth = 16*2;
		pspCharInfo.bitmapHeight = 16*2;
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
	return 0;
}

int sceFontGetCharGlyphImage_Clip(u32 libHandler, u32 charCode, u32 glyphImagePtr, int clipXPos, int clipYPos, int clipWidth, int clipHeight)
{
	ERROR_LOG(HLE, "sceFontGetCharGlyphImage_Clip %x, %x, %x (%c)", libHandler, charCode, glyphImagePtr, charCode);
	return 0;
}


const HLEFunction sceLibFont[] = 
{
	{0x67f17ed7, WrapU_UU<sceFontNewLib>, "sceFontNewLib"},	
	{0x574b6fbc, WrapI_U<sceFontDoneLib>, "sceFontDoneLib"},
	{0x48293280, 0, "sceFontSetResolution"},	
	{0x27f6e642, WrapI_UU<sceFontGetNumFontList>, "sceFontGetNumFontList"},
	{0xbc75d85b, 0, "sceFontGetFontList"},	
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

