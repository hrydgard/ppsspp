#include <cstdio>
#include "Common/Common.h"
#include "Common/StringUtils.h"
#include "GPU/Debugger/State.h"
#include "GPU/GPU.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "Core/System.h"

const TabStateRow g_stateFlagsRows[] = {
	{ "Lighting enable",      GE_CMD_LIGHTINGENABLE,          CMD_FMT_FLAG },
	{ "Light 0 enable",       GE_CMD_LIGHTENABLE0,            CMD_FMT_FLAG },
	{ "Light 1 enable",       GE_CMD_LIGHTENABLE1,            CMD_FMT_FLAG },
	{ "Light 2 enable",       GE_CMD_LIGHTENABLE2,            CMD_FMT_FLAG },
	{ "Light 3 enable",       GE_CMD_LIGHTENABLE3,            CMD_FMT_FLAG },
	{ "Depth clamp enable",   GE_CMD_DEPTHCLAMPENABLE,        CMD_FMT_FLAG },
	{ "Cullface enable",      GE_CMD_CULLFACEENABLE,          CMD_FMT_FLAG },
	{ "Texture map enable",   GE_CMD_TEXTUREMAPENABLE,        CMD_FMT_FLAG },
	{ "Fog enable",           GE_CMD_FOGENABLE,               CMD_FMT_FLAG },
	{ "Dither enable",        GE_CMD_DITHERENABLE,            CMD_FMT_FLAG },
	{ "Alpha blend enable",   GE_CMD_ALPHABLENDENABLE,        CMD_FMT_FLAG },
	{ "Alpha test enable",    GE_CMD_ALPHATESTENABLE,         CMD_FMT_FLAG },
	{ "Depth test enable",    GE_CMD_ZTESTENABLE,             CMD_FMT_FLAG },
	{ "Stencil test enable",  GE_CMD_STENCILTESTENABLE,       CMD_FMT_FLAG },
	{ "Antialias enable",     GE_CMD_ANTIALIASENABLE,         CMD_FMT_FLAG },
	{ "Patch cull enable",    GE_CMD_PATCHCULLENABLE,         CMD_FMT_FLAG },
	{ "Color test enable",    GE_CMD_COLORTESTENABLE,         CMD_FMT_FLAG },
	{ "Logic op enable",      GE_CMD_LOGICOPENABLE,           CMD_FMT_FLAG },
	{ "Depth write disable",  GE_CMD_ZWRITEDISABLE,           CMD_FMT_FLAG },
};
const size_t g_stateFlagsRowsSize = ARRAY_SIZE(g_stateFlagsRows);

const TabStateRow g_stateLightingRows[] = {
	{ "Ambient color",        GE_CMD_AMBIENTCOLOR,            CMD_FMT_HEX },
	{ "Ambient alpha",        GE_CMD_AMBIENTALPHA,            CMD_FMT_HEX },
	{ "Material update",      GE_CMD_MATERIALUPDATE,          CMD_FMT_MATERIALUPDATE },
	{ "Material emissive",    GE_CMD_MATERIALEMISSIVE,        CMD_FMT_HEX },
	{ "Material ambient",     GE_CMD_MATERIALAMBIENT,         CMD_FMT_HEX },
	{ "Material diffuse",     GE_CMD_MATERIALDIFFUSE,         CMD_FMT_HEX },
	{ "Material alpha",       GE_CMD_MATERIALALPHA,           CMD_FMT_HEX },
	{ "Material specular",    GE_CMD_MATERIALSPECULAR,        CMD_FMT_HEX },
	{ "Mat. specular coef",   GE_CMD_MATERIALSPECULARCOEF,    CMD_FMT_FLOAT24 },
	{ "Reverse normals",      GE_CMD_REVERSENORMAL,           CMD_FMT_FLAG },
	{ "Shade model",          GE_CMD_SHADEMODE,               CMD_FMT_SHADEMODEL },
	{ "Light mode",           GE_CMD_LIGHTMODE,               CMD_FMT_LIGHTMODE, GE_CMD_LIGHTINGENABLE },
	{ "Light type 0",         GE_CMD_LIGHTTYPE0,              CMD_FMT_LIGHTTYPE, GE_CMD_LIGHTENABLE0 },
	{ "Light type 1",         GE_CMD_LIGHTTYPE1,              CMD_FMT_LIGHTTYPE, GE_CMD_LIGHTENABLE1 },
	{ "Light type 2",         GE_CMD_LIGHTTYPE2,              CMD_FMT_LIGHTTYPE, GE_CMD_LIGHTENABLE2 },
	{ "Light type 3",         GE_CMD_LIGHTTYPE3,              CMD_FMT_LIGHTTYPE, GE_CMD_LIGHTENABLE3 },
	{ "Light pos 0",          GE_CMD_LX0,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE0, GE_CMD_LY0, GE_CMD_LZ0 },
	{ "Light pos 1",          GE_CMD_LX1,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE1, GE_CMD_LY1, GE_CMD_LZ1 },
	{ "Light pos 2",          GE_CMD_LX2,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE2, GE_CMD_LY2, GE_CMD_LZ2 },
	{ "Light pos 3",          GE_CMD_LX3,                     CMD_FMT_XYZ, GE_CMD_LIGHTENABLE3, GE_CMD_LY3, GE_CMD_LZ3 },
	{ "Light dir 0",          GE_CMD_LDX0,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE0, GE_CMD_LDY0, GE_CMD_LDZ0 },
	{ "Light dir 1",          GE_CMD_LDX1,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE1, GE_CMD_LDY1, GE_CMD_LDZ1 },
	{ "Light dir 2",          GE_CMD_LDX2,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE2, GE_CMD_LDY2, GE_CMD_LDZ2 },
	{ "Light dir 3",          GE_CMD_LDX3,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE3, GE_CMD_LDY3, GE_CMD_LDZ3 },
	{ "Light att 0",          GE_CMD_LKA0,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE0, GE_CMD_LKB0, GE_CMD_LKC0 },
	{ "Light att 1",          GE_CMD_LKA1,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE1, GE_CMD_LKB1, GE_CMD_LKC1 },
	{ "Light att 2",          GE_CMD_LKA2,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE2, GE_CMD_LKB2, GE_CMD_LKC2 },
	{ "Light att 3",          GE_CMD_LKA3,                    CMD_FMT_XYZ, GE_CMD_LIGHTENABLE3, GE_CMD_LKB3, GE_CMD_LKC3 },
	{ "Lightspot coef 0",     GE_CMD_LKS0,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE0 },
	{ "Lightspot coef 1",     GE_CMD_LKS1,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE1 },
	{ "Lightspot coef 2",     GE_CMD_LKS2,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE2 },
	{ "Lightspot coef 3",     GE_CMD_LKS3,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE3 },
	{ "Light angle 0",        GE_CMD_LKO0,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE0 },
	{ "Light angle 1",        GE_CMD_LKO1,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE1 },
	{ "Light angle 2",        GE_CMD_LKO2,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE2 },
	{ "Light angle 3",        GE_CMD_LKO3,                    CMD_FMT_FLOAT24, GE_CMD_LIGHTENABLE3 },
	{ "Light ambient 0",      GE_CMD_LAC0,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE0 },
	{ "Light diffuse 0",      GE_CMD_LDC0,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE0 },
	{ "Light specular 0",     GE_CMD_LSC0,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE0 },
	{ "Light ambient 1",      GE_CMD_LAC1,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE1 },
	{ "Light diffuse 1",      GE_CMD_LDC1,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE1 },
	{ "Light specular 1",     GE_CMD_LSC1,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE1 },
	{ "Light ambient 2",      GE_CMD_LAC2,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE2 },
	{ "Light diffuse 2",      GE_CMD_LDC2,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE2 },
	{ "Light specular 2",     GE_CMD_LSC2,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE2 },
	{ "Light ambient 3",      GE_CMD_LAC3,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE3 },
	{ "Light diffuse 3",      GE_CMD_LDC3,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE3 },
	{ "Light specular 3",     GE_CMD_LSC3,                    CMD_FMT_HEX, GE_CMD_LIGHTENABLE3 },
};
const size_t g_stateLightingRowsSize = ARRAY_SIZE(g_stateLightingRows);

const TabStateRow g_stateTextureRows[] = {
	{ "Texture L0 addr",      GE_CMD_TEXADDR0,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH0 },
	{ "Texture L0 size",      GE_CMD_TEXSIZE0,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex format",           GE_CMD_TEXFORMAT,               CMD_FMT_TEXFMT, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex CLUT",             GE_CMD_CLUTADDR,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_CLUTADDRUPPER },
	{ "Tex CLUT format",      GE_CMD_CLUTFORMAT,              CMD_FMT_CLUTFMT, GE_CMD_TEXTUREMAPENABLE },

	{ "Tex U scale",          GE_CMD_TEXSCALEU,               CMD_FMT_FLOAT24, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex V scale",          GE_CMD_TEXSCALEV,               CMD_FMT_FLOAT24, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex U offset",         GE_CMD_TEXOFFSETU,              CMD_FMT_FLOAT24, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex V offset",         GE_CMD_TEXOFFSETV,              CMD_FMT_FLOAT24, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex mapping mode",     GE_CMD_TEXMAPMODE,              CMD_FMT_TEXMAPMODE, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex shade srcs",       GE_CMD_TEXSHADELS,              CMD_FMT_TEXSHADELS, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex func",             GE_CMD_TEXFUNC,                 CMD_FMT_TEXFUNC, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex env color",        GE_CMD_TEXENVCOLOR,             CMD_FMT_HEX, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex mode",             GE_CMD_TEXMODE,                 CMD_FMT_TEXMODE, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex filtering",        GE_CMD_TEXFILTER,               CMD_FMT_TEXFILTER, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex wrapping",         GE_CMD_TEXWRAP,                 CMD_FMT_TEXWRAP, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex level/bias",       GE_CMD_TEXLEVEL,                CMD_FMT_TEXLEVEL, GE_CMD_TEXTUREMAPENABLE },
	{ "Tex lod slope",        GE_CMD_TEXLODSLOPE,             CMD_FMT_FLOAT24, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L1 addr",      GE_CMD_TEXADDR1,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH1 },
	{ "Texture L2 addr",      GE_CMD_TEXADDR2,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH2 },
	{ "Texture L3 addr",      GE_CMD_TEXADDR3,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH3 },
	{ "Texture L4 addr",      GE_CMD_TEXADDR4,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH4 },
	{ "Texture L5 addr",      GE_CMD_TEXADDR5,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH5 },
	{ "Texture L6 addr",      GE_CMD_TEXADDR6,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH6 },
	{ "Texture L7 addr",      GE_CMD_TEXADDR7,                CMD_FMT_PTRWIDTH, GE_CMD_TEXTUREMAPENABLE, GE_CMD_TEXBUFWIDTH7 },
	{ "Texture L1 size",      GE_CMD_TEXSIZE1,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L2 size",      GE_CMD_TEXSIZE2,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L3 size",      GE_CMD_TEXSIZE3,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L4 size",      GE_CMD_TEXSIZE4,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L5 size",      GE_CMD_TEXSIZE5,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L6 size",      GE_CMD_TEXSIZE6,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
	{ "Texture L7 size",      GE_CMD_TEXSIZE7,                CMD_FMT_TEXSIZE, GE_CMD_TEXTUREMAPENABLE },
};
const size_t g_stateTextureRowsSize = ARRAY_SIZE(g_stateTextureRows);

const TabStateRow g_stateSettingsRows[] = {
	{ "Framebuffer",          GE_CMD_FRAMEBUFPTR,             CMD_FMT_PTRWIDTH, 0, GE_CMD_FRAMEBUFWIDTH },
	{ "Framebuffer format",   GE_CMD_FRAMEBUFPIXFORMAT,       CMD_FMT_TEXFMT },
	{ "Depthbuffer",          GE_CMD_ZBUFPTR,                 CMD_FMT_PTRWIDTH, 0, GE_CMD_ZBUFWIDTH },
	{ "Viewport Scale",       GE_CMD_VIEWPORTXSCALE,          CMD_FMT_XYZ, 0, GE_CMD_VIEWPORTYSCALE, GE_CMD_VIEWPORTZSCALE },
	{ "Viewport Offset",      GE_CMD_VIEWPORTXCENTER,         CMD_FMT_XYZ, 0, GE_CMD_VIEWPORTYCENTER, GE_CMD_VIEWPORTZCENTER },
	{ "Scissor",              GE_CMD_SCISSOR1,                CMD_FMT_XYXY, 0, GE_CMD_SCISSOR2 },
	{ "Region",               GE_CMD_REGION1,                 CMD_FMT_XYXY, 0, GE_CMD_REGION2 },
	{ "Color test",           GE_CMD_COLORTEST,               CMD_FMT_COLORTEST, GE_CMD_COLORTESTENABLE, GE_CMD_COLORREF, GE_CMD_COLORTESTMASK },
	{ "Alpha test",           GE_CMD_ALPHATEST,               CMD_FMT_ALPHATEST, GE_CMD_ALPHATESTENABLE },
	{ "Clear mode",           GE_CMD_CLEARMODE,               CMD_FMT_CLEARMODE },
	{ "Stencil test",         GE_CMD_STENCILTEST,             CMD_FMT_STENCILTEST, GE_CMD_STENCILTESTENABLE },
	{ "Stencil test op",      GE_CMD_STENCILOP,               CMD_FMT_STENCILOP, GE_CMD_STENCILTESTENABLE },
	{ "Depth test",           GE_CMD_ZTEST,                   CMD_FMT_ZTEST, GE_CMD_ZTESTENABLE },
	{ "RGB mask",             GE_CMD_MASKRGB,                 CMD_FMT_HEX },
	{ "Stencil/alpha mask",   GE_CMD_MASKALPHA,               CMD_FMT_HEX },
	{ "Transfer src",         GE_CMD_TRANSFERSRC,             CMD_FMT_PTRWIDTH, 0, GE_CMD_TRANSFERSRCW },
	{ "Transfer src pos",     GE_CMD_TRANSFERSRCPOS,          CMD_FMT_XY },
	{ "Transfer dst",         GE_CMD_TRANSFERDST,             CMD_FMT_PTRWIDTH, 0, GE_CMD_TRANSFERDSTW },
	{ "Transfer dst pos",     GE_CMD_TRANSFERDSTPOS,          CMD_FMT_XY },
	{ "Transfer size",        GE_CMD_TRANSFERSIZE,            CMD_FMT_XYPLUS1 },
	{ "Vertex type",          GE_CMD_VERTEXTYPE,              CMD_FMT_VERTEXTYPE },
	{ "Offset addr",          GE_CMD_OFFSETADDR,              CMD_FMT_OFFSETADDR },
	{ "Vertex addr",          GE_CMD_VADDR,                   CMD_FMT_VADDR },
	{ "Index addr",           GE_CMD_IADDR,                   CMD_FMT_IADDR },
	{ "Min Z",                GE_CMD_MINZ,                    CMD_FMT_HEX },
	{ "Max Z",                GE_CMD_MAXZ,                    CMD_FMT_HEX },
	{ "Offset",               GE_CMD_OFFSETX,                 CMD_FMT_F16_XY, 0, GE_CMD_OFFSETY },
	{ "Cull mode",            GE_CMD_CULL,                    CMD_FMT_CULL, GE_CMD_CULLFACEENABLE },
	{ "Alpha blend mode",     GE_CMD_BLENDMODE,               CMD_FMT_BLENDMODE, GE_CMD_ALPHABLENDENABLE },
	{ "Blend color A",        GE_CMD_BLENDFIXEDA,             CMD_FMT_HEX, GE_CMD_ALPHABLENDENABLE },
	{ "Blend color B",        GE_CMD_BLENDFIXEDB,             CMD_FMT_HEX, GE_CMD_ALPHABLENDENABLE },
	{ "Logic Op",             GE_CMD_LOGICOP,                 CMD_FMT_LOGICOP, GE_CMD_LOGICOPENABLE },
	{ "Fog 1",                GE_CMD_FOG1,                    CMD_FMT_FLOAT24, GE_CMD_FOGENABLE },
	{ "Fog 2",                GE_CMD_FOG2,                    CMD_FMT_FLOAT24, GE_CMD_FOGENABLE },
	{ "Fog color",            GE_CMD_FOGCOLOR,                CMD_FMT_HEX, GE_CMD_FOGENABLE },
	{ "Morph Weight 0",       GE_CMD_MORPHWEIGHT0,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 1",       GE_CMD_MORPHWEIGHT1,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 2",       GE_CMD_MORPHWEIGHT2,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 3",       GE_CMD_MORPHWEIGHT3,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 4",       GE_CMD_MORPHWEIGHT4,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 5",       GE_CMD_MORPHWEIGHT5,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 6",       GE_CMD_MORPHWEIGHT6,            CMD_FMT_FLOAT24 },
	{ "Morph Weight 7",       GE_CMD_MORPHWEIGHT7,            CMD_FMT_FLOAT24 },
	// TODO: Format?
	{ "Patch division",       GE_CMD_PATCHDIVISION,           CMD_FMT_HEX },
	{ "Patch primitive",      GE_CMD_PATCHPRIMITIVE,          CMD_FMT_PATCHPRIMITIVE },
	// TODO: Format?
	{ "Patch facing",         GE_CMD_PATCHFACING,             CMD_FMT_HEX, GE_CMD_PATCHCULLENABLE },
	{ "Dither 0",             GE_CMD_DITH0,                   CMD_FMT_HEX, GE_CMD_DITHERENABLE },
	{ "Dither 1",             GE_CMD_DITH1,                   CMD_FMT_HEX, GE_CMD_DITHERENABLE },
	{ "Dither 2",             GE_CMD_DITH2,                   CMD_FMT_HEX, GE_CMD_DITHERENABLE },
	{ "Dither 3",             GE_CMD_DITH3,                   CMD_FMT_HEX, GE_CMD_DITHERENABLE },
	{ "Imm vertex XY",        GE_CMD_VSCX,                    CMD_FMT_F16_XY, 0, GE_CMD_VSCY },
	{ "Imm vertex Z",         GE_CMD_VSCZ,                    CMD_FMT_HEX },
	{ "Imm vertex tex STQ",   GE_CMD_VTCS,                    CMD_FMT_XYZ, 0, GE_CMD_VTCT, GE_CMD_VTCQ },
	{ "Imm vertex color0",    GE_CMD_VCV,                     CMD_FMT_HEX },
	{ "Imm vertex color1",    GE_CMD_VSCV,                    CMD_FMT_HEX },
	{ "Imm vertex fog",       GE_CMD_VFC,                     CMD_FMT_HEX },
	// TODO: Format?
	{ "Imm vertex prim",      GE_CMD_VAP,                     CMD_FMT_HEX },
};
const size_t g_stateSettingsRowsSize = ARRAY_SIZE(g_stateSettingsRows);

// TODO: Commands not present in the above lists (some because they don't have meaningful values...):
//   GE_CMD_PRIM, GE_CMD_BEZIER, GE_CMD_SPLINE, GE_CMD_BOUNDINGBOX,
//   GE_CMD_JUMP, GE_CMD_BJUMP, GE_CMD_CALL, GE_CMD_RET, GE_CMD_END, GE_CMD_SIGNAL, GE_CMD_FINISH,
//   GE_CMD_BONEMATRIXNUMBER, GE_CMD_BONEMATRIXDATA, GE_CMD_WORLDMATRIXNUMBER, GE_CMD_WORLDMATRIXDATA,
//   GE_CMD_VIEWMATRIXNUMBER, GE_CMD_VIEWMATRIXDATA, GE_CMD_PROJMATRIXNUMBER, GE_CMD_PROJMATRIXDATA,
//   GE_CMD_TGENMATRIXNUMBER, GE_CMD_TGENMATRIXDATA,
//   GE_CMD_LOADCLUT, GE_CMD_TEXFLUSH, GE_CMD_TEXSYNC,
//   GE_CMD_TRANSFERSTART,
//   GE_CMD_UNKNOWN_*

void FormatStateRow(GPUDebugInterface *gpudebug, char *dest, size_t destSize, CmdFormatType fmt, u32 value, bool enabled, u32 otherValue, u32 otherValue2) {
	switch (fmt) {
	case CMD_FMT_HEX:
		snprintf(dest, destSize, "%06x", value);
		break;

	case CMD_FMT_NUM:
		snprintf(dest, destSize, "%d", value);
		break;

	case CMD_FMT_FLOAT24:
		snprintf(dest, destSize, "%f", getFloat24(value));
		break;

	case CMD_FMT_PTRWIDTH:
		value |= (otherValue & 0x00FF0000) << 8;
		otherValue &= 0xFFFF;
		snprintf(dest, destSize, "%08x, w=%d", value, otherValue);
		break;

	case CMD_FMT_XY:
	{
		int x = value & 0x3FF;
		int y = value >> 10;
		snprintf(dest, destSize, "%d,%d", x, y);
	}
	break;

	case CMD_FMT_XYPLUS1:
	{
		int x = value & 0x3FF;
		int y = value >> 10;
		snprintf(dest, destSize, "%d,%d", x + 1, y + 1);
	}
	break;

	case CMD_FMT_XYXY:
	{
		int x1 = value & 0x3FF;
		int y1 = value >> 10;
		int x2 = otherValue & 0x3FF;
		int y2 = otherValue >> 10;
		snprintf(dest, destSize, "%d,%d - %d,%d", x1, y1, x2, y2);
	}
	break;

	case CMD_FMT_XYZ:
	{
		float x = getFloat24(value);
		float y = getFloat24(otherValue);
		float z = getFloat24(otherValue2);
		snprintf(dest, destSize, "%f, %f, %f", x, y, z);
	}
	break;

	case CMD_FMT_TEXSIZE:
	{
		int w = 1 << (value & 0x1f);
		int h = 1 << ((value >> 8) & 0x1f);
		snprintf(dest, destSize, "%dx%d", w, h);
	}
	break;

	case CMD_FMT_F16_XY:
	{
		float x = (float)value / 16.0f;
		float y = (float)otherValue / 16.0f;
		snprintf(dest, destSize, "%fx%f", x, y);
	}
	break;

	case CMD_FMT_VERTEXTYPE:
	{
		char buffer[256];
		GeDescribeVertexType(value, buffer);
		snprintf(dest, destSize, "%s", buffer);
	}
	break;

	case CMD_FMT_TEXFMT:
	{
		static const char *texformats[] = {
			"5650",
			"5551",
			"4444",
			"8888",
			"CLUT4",
			"CLUT8",
			"CLUT16",
			"CLUT32",
			"DXT1",
			"DXT3",
			"DXT5",
		};
		if (value < (u32)ARRAY_SIZE(texformats)) {
			snprintf(dest, destSize, "%s", texformats[value]);
		} else if ((value & 0xF) < (u32)ARRAY_SIZE(texformats)) {
			snprintf(dest, destSize, "%s (extra bits %06x)", texformats[value & 0xF], value & ~0xF);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_CLUTFMT:
	{
		const char *clutformats[] = {
			"BGR 5650",
			"ABGR 1555",
			"ABGR 4444",
			"ABGR 8888",
		};
		const u8 palette = (value >> 0) & 3;
		const u8 shift = (value >> 2) & 0x3F;
		const u8 mask = (value >> 8) & 0xFF;
		const u8 offset = (value >> 16) & 0xFF;
		if (offset < 0x20 && shift < 0x20) {
			if (offset == 0 && shift == 0) {
				snprintf(dest, destSize, "%s ind & %02x", clutformats[palette], mask);
			} else {
				snprintf(dest, destSize, "%s (ind >> %d) & %02x, offset +%d", clutformats[palette], shift, mask, offset);
			}
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_COLORTEST:
	{
		static const char *colorTests[] = { "NEVER", "ALWAYS", " == ", " != " };
		const u32 mask = otherValue2;
		const u32 ref = otherValue;
		if (value < (u32)ARRAY_SIZE(colorTests)) {
			snprintf(dest, destSize, "pass if (c & %06x) %s (%06x & %06x)", mask, colorTests[value], ref, mask);
		} else {
			snprintf(dest, destSize, "%06x, ref=%06x, maks=%06x", value, ref, mask);
		}
	}
	break;

	case CMD_FMT_ALPHATEST:
	case CMD_FMT_STENCILTEST:
	{
		static const char *alphaTestFuncs[] = { "NEVER", "ALWAYS", "==", "!=", "<", "<=", ">", ">=" };
		const u8 mask = (value >> 16) & 0xff;
		const u8 ref = (value >> 8) & 0xff;
		const u8 func = (value >> 0) & 0xff;
		if (func < (u8)ARRAY_SIZE(alphaTestFuncs)) {
			if (fmt == CMD_FMT_ALPHATEST) {
				snprintf(dest, destSize, "pass if (a & %02x) %s (%02x & %02x)", mask, alphaTestFuncs[func], ref, mask);
			} else if (fmt == CMD_FMT_STENCILTEST) {
				// Stencil test is the other way around.
				snprintf(dest, destSize, "pass if (%02x & %02x) %s (a & %02x)", ref, mask, alphaTestFuncs[func], mask);
			}
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_ZTEST:
	{
		static const char *zTestFuncs[] = { "NEVER", "ALWAYS", "==", "!=", "<", "<=", ">", ">=" };
		if (value < (u32)ARRAY_SIZE(zTestFuncs)) {
			snprintf(dest, destSize, "pass if src %s dst", zTestFuncs[value]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_OFFSETADDR:
		snprintf(dest, destSize, "%08x", gpuDebug->GetRelativeAddress(0));
		break;

	case CMD_FMT_VADDR:
		snprintf(dest, destSize, "%08x", gpuDebug->GetVertexAddress());
		break;

	case CMD_FMT_IADDR:
		snprintf(dest, destSize, "%08x", gpuDebug->GetIndexAddress());
		break;

	case CMD_FMT_MATERIALUPDATE:
	{
		static const char *materialTypes[] = {
			"none",
			"ambient",
			"diffuse",
			"ambient, diffuse",
			"specular",
			"ambient, specular",
			"diffuse, specular",
			"ambient, diffuse, specular",
		};
		if (value < (u32)ARRAY_SIZE(materialTypes)) {
			snprintf(dest, destSize, "%s", materialTypes[value]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_SHADEMODEL:
		if (value == 0) {
			snprintf(dest, destSize, "flat");
		} else if (value == 1) {
			snprintf(dest, destSize, "gouraud");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	case CMD_FMT_STENCILOP:
	{
		static const char *stencilOps[] = { "KEEP", "ZERO", "REPLACE", "INVERT", "INCREMENT", "DECREMENT" };
		const u8 sfail = (value >> 0) & 0xFF;
		const u8 zfail = (value >> 8) & 0xFF;
		const u8 pass = (value >> 16) & 0xFF;
		const u8 totalValid = (u8)ARRAY_SIZE(stencilOps);
		if (sfail < totalValid && zfail < totalValid && pass < totalValid) {
			snprintf(dest, destSize, "fail=%s, pass/depthfail=%s, pass=%s", stencilOps[sfail], stencilOps[zfail], stencilOps[pass]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_BLENDMODE:
	{
		const char *blendModes[] = {
			"add",
			"subtract",
			"reverse subtract",
			"min",
			"max",
			"abs subtract",
		};
		const char *blendFactorsA[] = {
			"dst",
			"1.0 - dst",
			"src.a",
			"1.0 - src.a",
			"dst.a",
			"1.0 - dst.a",
			"2.0 * src.a",
			"1.0 - 2.0 * src.a",
			"2.0 * dst.a",
			"1.0 - 2.0 * dst.a",
			"fixed",
		};
		const char *blendFactorsB[] = {
			"src",
			"1.0 - src",
			"src.a",
			"1.0 - src.a",
			"dst.a",
			"1.0 - dst.a",
			"2.0 * src.a",
			"1.0 - 2.0 * src.a",
			"2.0 * dst.a",
			"1.0 - 2.0 * dst.a",
			"fixed",
		};
		const u8 blendFactorA = (value >> 0) & 0xF;
		const u8 blendFactorB = (value >> 4) & 0xF;
		const u32 blendMode = (value >> 8);

		if (blendFactorA < (u8)ARRAY_SIZE(blendFactorsA) && blendFactorB < (u8)ARRAY_SIZE(blendFactorsB) && blendMode < (u32)ARRAY_SIZE(blendModes)) {
			snprintf(dest, destSize, "%s: %s, %s", blendModes[blendMode], blendFactorsA[blendFactorA], blendFactorsB[blendFactorB]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_CLEARMODE:
		if (value == 0) {
			snprintf(dest, destSize, "%d", value);
		} else if ((value & ~(GE_CLEARMODE_ALL | 1)) == 0) {
			const char *clearmodes[] = {
				"1, write disabled",
				"1, write color",
				"1, write alpha/stencil",
				"1, write color, alpha/stencil",
				"1, write depth",
				"1, write color, depth",
				"1, write alpha/stencil, depth",
				"1, write color, alpha/stencil, depth",
			};
			snprintf(dest, destSize, "%s", clearmodes[value >> 8]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	case CMD_FMT_TEXFUNC:
	{
		const char *texfuncs[] = {
			"modulate",
			"decal",
			"blend",
			"replace",
			"add",
		};
		const u8 func = (value >> 0) & 0xFF;
		const u8 rgba = (value >> 8) & 0xFF;
		const u8 colorDouble = (value >> 16) & 0xFF;

		if (rgba <= 1 && colorDouble <= 1 && func < (u8)ARRAY_SIZE(texfuncs)) {
			snprintf(dest, destSize, "%s, %s%s", texfuncs[func], rgba ? "RGBA" : "RGB", colorDouble ? ", color doubling" : "");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXMODE:
	{
		const u8 swizzle = (value >> 0) & 0xFF;
		const u8 clutLevels = (value >> 8) & 0xFF;
		const u8 maxLevel = (value >> 16) & 0xFF;

		if (swizzle <= 1 && clutLevels <= 1 && maxLevel <= 7) {
			snprintf(dest, destSize, "%s%d levels%s", swizzle ? "swizzled, " : "", maxLevel + 1, clutLevels ? ", CLUT per level" : "");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_LOGICOP:
	{
		const char *logicOps[] = {
			"clear",
			"and",
			"reverse and",
			"copy",
			"inverted and",
			"noop",
			"xor",
			"or",
			"negated or",
			"equivalence",
			"inverted",
			"reverse or",
			"inverted copy",
			"inverted or",
			"negated and",
			"set",
		};

		if (value < ARRAY_SIZE(logicOps)) {
			snprintf(dest, destSize, "%s", logicOps[value]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXWRAP:
	{
		if ((value & ~0x0101) == 0) {
			const bool clampS = (value & 0x0001) != 0;
			const bool clampT = (value & 0x0100) != 0;
			snprintf(dest, destSize, "%s s, %s t", clampS ? "clamp" : "wrap", clampT ? "clamp" : "wrap");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXLEVEL:
	{
		static const char *mipLevelModes[3] = {
			"auto + bias",
			"bias",
			"slope + bias",
		};
		const int mipLevel = value & 0xFFFF;
		const int biasFixed = (s8)(value >> 16);
		const float bias = (float)biasFixed / 16.0f;

		if (mipLevel == 0 || mipLevel == 1 || mipLevel == 2) {
			snprintf(dest, destSize, "%s: %f", mipLevelModes[mipLevel], bias);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXFILTER:
	{
		const char *textureFilters[] = {
			"nearest",
			"linear",
			NULL,
			NULL,
			"nearest, mipmap nearest",
			"linear, mipmap nearest",
			"nearest, mipmap linear",
			"linear, mipmap linear",
		};
		if ((value & ~0x0107) == 0 && textureFilters[value & 7] != NULL) {
			const int min = (value & 0x0007) >> 0;
			const int mag = (value & 0x0100) >> 8;
			snprintf(dest, destSize, "min: %s, mag: %s", textureFilters[min], textureFilters[mag]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXMAPMODE:
	{
		static const char *const uvGenModes[] = {
			"tex coords",
			"tex matrix",
			"tex env map",
			"unknown (tex coords?)",
		};
		static const char *const uvProjModes[] = {
			"pos",
			"uv",
			"normalized normal",
			"normal",
		};
		if ((value & ~0x0303) == 0) {
			const int uvGen = (value & 0x0003) >> 0;
			const int uvProj = (value & 0x0300) >> 8;
			snprintf(dest, destSize, "gen: %s, proj: %s", uvGenModes[uvGen], uvProjModes[uvProj]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_TEXSHADELS:
		if ((value & ~0x0303) == 0) {
			const int sLight = (value & 0x0003) >> 0;
			const int tLight = (value & 0x0300) >> 8;
			snprintf(dest, destSize, "s: %d, t: %d", sLight, tLight);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	case CMD_FMT_LIGHTMODE:
		if (value == 0) {
			snprintf(dest, destSize, "mixed color");
		} else if (value == 1) {
			snprintf(dest, destSize, "separate specular");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	case CMD_FMT_LIGHTTYPE:
	{
		static const char * const lightComputations[] = {
			"diffuse",
			"diffuse + spec",
			"pow(diffuse)",
			"unknown (diffuse?)",
		};
		static const char * const lightTypes[] = {
			"directional",
			"point",
			"spot",
			"unknown (directional?)",
		};
		if ((value & ~0x0303) == 0) {
			const int comp = (value & 0x0003) >> 0;
			const int type = (value & 0x0300) >> 8;
			snprintf(dest, destSize, "type: %s, comp: %s", lightTypes[type], lightComputations[comp]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_CULL:
		if (value == 0) {
			snprintf(dest, destSize, "front (CW)");
		} else if (value == 1) {
			snprintf(dest, destSize, "back (CCW)");
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	case CMD_FMT_PATCHPRIMITIVE:
	{
		static const char * const patchPrims[] = {
			"triangles",
			"lines",
			"points",
		};
		if (value < (u32)ARRAY_SIZE(patchPrims)) {
			snprintf(dest, destSize, "%s", patchPrims[value]);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
	}
	break;

	case CMD_FMT_FLAG:
		if ((value & ~1) == 0) {
			snprintf(dest, destSize, "%d", value);
		} else {
			snprintf(dest, destSize, "%06x", value);
		}
		break;

	default:
		snprintf(dest, destSize, "BAD FORMAT %06x", value);
	}

	// TODO: Turn row grey or some such?
	if (!enabled) {
		strcat(dest, " (disabled)");
	}
}

void FormatVertCol(char *dest, size_t destSize, const GPUDebugVertex &vert, int col) {
	switch (col) {
	case VERTEXLIST_COL_X: snprintf(dest, destSize, "%f", vert.x); break;
	case VERTEXLIST_COL_Y: snprintf(dest, destSize, "%f", vert.y); break;
	case VERTEXLIST_COL_Z: snprintf(dest, destSize, "%f", vert.z); break;
	case VERTEXLIST_COL_U: snprintf(dest, destSize, "%f", vert.u); break;
	case VERTEXLIST_COL_V: snprintf(dest, destSize, "%f", vert.v); break;
	case VERTEXLIST_COL_COLOR:
		snprintf(dest, destSize, "%02x%02x%02x%02x", vert.c[0], vert.c[1], vert.c[2], vert.c[3]);
		break;
	case VERTEXLIST_COL_NX: snprintf(dest, destSize, "%f", vert.nx); break;
	case VERTEXLIST_COL_NY: snprintf(dest, destSize, "%f", vert.ny); break;
	case VERTEXLIST_COL_NZ: snprintf(dest, destSize, "%f", vert.nz); break;

	default:
		truncate_cpy(dest, destSize, "Invalid");
		break;
	}
}

static void FormatVertColRawType(char *dest, size_t destSize, const void *data, int type, int offset);
static void FormatVertColRawColor(char *dest, size_t destSize, const void *data, int type);

void FormatVertColRaw(VertexDecoder *decoder, char *dest, size_t destSize, int row, int col) {
	auto memLock = Memory::Lock();
	if (!PSP_IsInited()) {
		truncate_cpy(dest, destSize, "Invalid");
		return;
	}

	// We could use the vertex decoder and reader, but those already do some minor adjustments.
	// There's only a few values - let's just go after them directly.
	const u8 *vert = Memory::GetPointer(gpuDebug->GetVertexAddress()) + row * decoder->size;
	const u8 *pos = vert + decoder->posoff;
	const u8 *tc = vert + decoder->tcoff;
	const u8 *color = vert + decoder->coloff;
	const u8 *norm = vert + decoder->nrmoff;

	switch (col) {
	case VERTEXLIST_COL_X:
		FormatVertColRawType(dest, destSize, pos, decoder->pos, 0);
		break;
	case VERTEXLIST_COL_Y:
		FormatVertColRawType(dest, destSize, pos, decoder->pos, 1);
		break;
	case VERTEXLIST_COL_Z:
		FormatVertColRawType(dest, destSize, pos, decoder->pos, 2);
		break;
	case VERTEXLIST_COL_U:
		FormatVertColRawType(dest, destSize, tc, decoder->tc, 0);
		break;
	case VERTEXLIST_COL_V:
		FormatVertColRawType(dest, destSize, tc, decoder->tc, 1);
		break;
	case VERTEXLIST_COL_COLOR:
		FormatVertColRawColor(dest, destSize, color, decoder->col);
		break;

	case VERTEXLIST_COL_NX: FormatVertColRawType(dest, destSize, norm, decoder->nrm, 0); break;
	case VERTEXLIST_COL_NY: FormatVertColRawType(dest, destSize, norm, decoder->nrm, 1); break;
	case VERTEXLIST_COL_NZ: FormatVertColRawType(dest, destSize, norm, decoder->nrm, 2); break;

	default:
		truncate_cpy(dest, destSize, "Invalid");
		break;
	}
}

static void FormatVertColRawType(char *dest, size_t destSize, const void *data, int type, int offset) {
	switch (type) {
	case 0:
		truncate_cpy(dest, destSize, "-");
		break;

	case 1: // 8-bit
		snprintf(dest, destSize, "%02x", ((const u8 *)data)[offset]);
		break;

	case 2: // 16-bit
		snprintf(dest, destSize, "%04x", ((const u16_le *)data)[offset]);
		break;

	case 3: // float
		snprintf(dest, destSize, "%f", ((const float *)data)[offset]);
		break;

	default:
		truncate_cpy(dest, destSize, "Invalid");
		break;
	}
}

static void FormatVertColRawColor(char *dest, size_t destSize, const void *data, int type) {
	switch (type) {
	case GE_VTYPE_COL_NONE >> GE_VTYPE_COL_SHIFT:
		truncate_cpy(dest, destSize, "-");
		break;

	case GE_VTYPE_COL_565 >> GE_VTYPE_COL_SHIFT:
	case GE_VTYPE_COL_5551 >> GE_VTYPE_COL_SHIFT:
	case GE_VTYPE_COL_4444 >> GE_VTYPE_COL_SHIFT:
		snprintf(dest, destSize, "%04x", *(const u16_le *)data);
		break;

	case GE_VTYPE_COL_8888 >> GE_VTYPE_COL_SHIFT:
		snprintf(dest, destSize, "%08x", *(const u32_le *)data);
		break;

	default:
		truncate_cpy(dest, destSize, "Invalid");
		break;
	}
}
