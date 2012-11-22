#include "sceFont.h"

#include "base/timeutil.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceFont.h"



const HLEFunction sceLibFont[] = 
{
	{0x67f17ed7, 0, "sceFontNewLib"},	
	{0x574b6fbc, 0, "sceFontDoneLib"},
	{0x48293280, 0, "sceFontSetResolution"},	
	{0x27f6e642, 0, "sceFontGetNumFontList"},
	{0xbc75d85b, 0, "sceFontGetFontList"},	
	{0x099ef33c, 0, "sceFontFindOptimumFont"},	
	{0x681e61a7, 0, "sceFontFindFont"},	
	{0x2f67356a, 0, "sceFontCalcMemorySize"},	
	{0x5333322d, 0, "sceFontGetFontInfoByIndexNumber"},
	{0xa834319d, 0, "sceFontOpen"},	
	{0x57fcb733, 0, "sceFontOpenUserFile"},	
	{0xbb8e7fe6, 0, "sceFontOpenUserMemory"},	
	{0x3aea8cb6, 0, "sceFontClose"},	
	{0x0da7535e, 0, "sceFontGetFontInfo"},	
	{0xdcc80c2f, 0, "sceFontGetCharInfo"},	
	{0x5c3e4a9e, 0, "sceFontGetCharImageRect"},	
	{0x980f4895, 0, "sceFontGetCharGlyphImage"},	
	{0xca1e6945, 0, "sceFontGetCharGlyphImage_Clip"},
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

