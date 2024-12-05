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

#include "Common/CommonWindows.h"
#include <commctrl.h>
#include <array>
#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/System/Request.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Windows/resource.h"
#include "Windows/InputBox.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/TabState.h"
#include "Windows/W32Util/ContextMenu.h"
#include "GPU/GPUState.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Debugger/State.h"

using namespace GPUBreakpoints;

// First column is the breakpoint icon.
static const GenericListViewColumn stateValuesCols[] = {
	{ L"", 0.03f },
	{ L"Name", 0.40f },
	{ L"Value", 0.57f },
};

GenericListViewDef stateValuesListDef = {
	stateValuesCols,
	ARRAY_SIZE(stateValuesCols),
	nullptr,
	false,
};

enum StateValuesCols {
	STATEVALUES_COL_BREAKPOINT,
	STATEVALUES_COL_NAME,
	STATEVALUES_COL_VALUE,
};

struct TabStateRow {
	std::string_view title;
	uint8_t cmd;
	CmdFormatType fmt;
	uint8_t enableCmd;
	uint8_t otherCmd;
	uint8_t otherCmd2;
};

static const TabStateRow g_stateFlagsRows[] = {
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

static const TabStateRow g_stateLightingRows[] = {
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

static const TabStateRow g_stateTextureRows[] = {
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

static const TabStateRow g_stateSettingsRows[] = {
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

// TODO: Commands not present in the above lists (some because they don't have meaningful values...):
//   GE_CMD_PRIM, GE_CMD_BEZIER, GE_CMD_SPLINE, GE_CMD_BOUNDINGBOX,
//   GE_CMD_JUMP, GE_CMD_BJUMP, GE_CMD_CALL, GE_CMD_RET, GE_CMD_END, GE_CMD_SIGNAL, GE_CMD_FINISH,
//   GE_CMD_BONEMATRIXNUMBER, GE_CMD_BONEMATRIXDATA, GE_CMD_WORLDMATRIXNUMBER, GE_CMD_WORLDMATRIXDATA,
//   GE_CMD_VIEWMATRIXNUMBER, GE_CMD_VIEWMATRIXDATA, GE_CMD_PROJMATRIXNUMBER, GE_CMD_PROJMATRIXDATA,
//   GE_CMD_TGENMATRIXNUMBER, GE_CMD_TGENMATRIXDATA,
//   GE_CMD_LOADCLUT, GE_CMD_TEXFLUSH, GE_CMD_TEXSYNC,
//   GE_CMD_TRANSFERSTART,
//   GE_CMD_UNKNOWN_*

static std::vector<TabStateRow> watchList;

static void ToggleWatchList(const TabStateRow &info) {
	for (size_t i = 0; i < watchList.size(); ++i) {
		if (watchList[i].cmd == info.cmd) {
			watchList.erase(watchList.begin() + i);
			return;
		}
	}

	watchList.push_back(info);
}

static bool ToggleBreakpoint(const TabStateRow &info) {
	if (IsCmdBreakpoint(info.cmd)) {
		RemoveCmdBreakpoint(info.cmd);
		if (info.otherCmd)
			RemoveCmdBreakpoint(info.otherCmd);
		if (info.otherCmd2)
			RemoveCmdBreakpoint(info.otherCmd2);
		return false;
	}

	AddCmdBreakpoint(info.cmd);
	if (info.otherCmd)
		AddCmdBreakpoint(info.otherCmd);
	if (info.otherCmd2)
		AddCmdBreakpoint(info.otherCmd2);
	return true;
}

bool PromptStateValue(const TabStateRow &info, HWND hparent, const char *title, u32 &value) {
	wchar_t wtitle[1024];
	ConvertUTF8ToWString(wtitle, ARRAY_SIZE(wtitle), title);

	if (info.fmt == CMD_FMT_FLOAT24 || info.fmt == CMD_FMT_XYZ) {
		union {
			u32 u;
			float f;
		} temp = { value << 8 };

		std::string strvalue = StringFromFormat("%f", temp.f);
		bool res = InputBox_GetString(GetModuleHandle(NULL), hparent, wtitle, strvalue, strvalue);
		if (!res)
			return false;

		// Okay, the result could be a simple float, hex (0x...), or invalid.
		if (sscanf(strvalue.c_str(), "0x%08x", &value) == 1)
			return true;
		if (sscanf(strvalue.c_str(), "%f", &temp.f) == 1) {
			value = temp.u >> 8;
			return true;
		}
		return false;
	}
	return InputBox_GetHex(GetModuleHandle(NULL), hparent, wtitle, value, value);
}

CtrlStateValues::CtrlStateValues(const TabStateRow *rows, int rowCount, HWND hwnd)
	: GenericListControl(hwnd, stateValuesListDef),
	  rows_(rows), rowCount_(rowCount) {
	SetIconList(12, 12, { (HICON)LoadIcon(GetModuleHandle(nullptr), (LPCWSTR)IDI_BREAKPOINT_SMALL) });
	Update();
}

void CtrlStateValues::GetColumnText(wchar_t *dest, size_t destSize, int row, int col) {
	if (row < 0 || row >= rowCount_) {
		return;
	}

	switch (col) {
	case STATEVALUES_COL_BREAKPOINT:
		wcscpy(dest, L" ");
		break;

	case STATEVALUES_COL_NAME:
		ConvertUTF8ToWString(dest, destSize, rows_[row].title);
		break;

	case STATEVALUES_COL_VALUE:
		{
			if (!gpuDebug) {
				wcscpy(dest, L"N/A");
				break;
			}

			const auto info = rows_[row];
			const auto state = gpuDebug->GetGState();
			const bool enabled = info.enableCmd == 0 || (state.cmdmem[info.enableCmd] & 1) == 1;
			const u32 value = state.cmdmem[info.cmd] & 0xFFFFFF;
			const u32 otherValue = state.cmdmem[info.otherCmd] & 0xFFFFFF;
			const u32 otherValue2 = state.cmdmem[info.otherCmd2] & 0xFFFFFF;
			char temp[256];
			FormatStateRow(gpuDebug, temp, sizeof(temp), info.fmt, value, enabled, otherValue, otherValue2);
			ConvertUTF8ToWString(dest, destSize, temp);
			break;
		}
	}
}

void CtrlStateValues::OnDoubleClick(int row, int column) {
	if (gpuDebug == nullptr || row >= rowCount_) {
		return;
	}

	const auto info = rows_[row];

	if (column == STATEVALUES_COL_BREAKPOINT) {
		bool proceed = true;
		if (GetCmdBreakpointCond(info.cmd, nullptr)) {
			int ret = MessageBox(GetHandle(), L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			proceed = ret == IDYES;
		}
		if (proceed)
			SetItemState(row, ToggleBreakpoint(info) ? 1 : 0);
		return;
	}

	switch (info.fmt) {
	case CMD_FMT_FLAG:
		{
			const auto state = gpuDebug->GetGState();
			u32 newValue = state.cmdmem[info.cmd] ^ 1;
			SetCmdValue(newValue);
		}
		break;

	default:
		{
			char title[1024];
			const auto state = gpuDebug->GetGState();

			u32 newValue = state.cmdmem[info.cmd] & 0x00FFFFFF;
			snprintf(title, sizeof(title), "New value for %.*s", (int)info.title.size(), info.title.data());
			if (PromptStateValue(info, GetHandle(), title, newValue)) {
				newValue |= state.cmdmem[info.cmd] & 0xFF000000;
				SetCmdValue(newValue);

				if (info.otherCmd) {
					newValue = state.cmdmem[info.otherCmd] & 0x00FFFFFF;
					snprintf(title, sizeof(title), "New value for %.*s (secondary)", (int)info.title.size(), info.title.data());
					if (PromptStateValue(info, GetHandle(), title, newValue)) {
						newValue |= state.cmdmem[info.otherCmd] & 0xFF000000;
						SetCmdValue(newValue);

						if (info.otherCmd2) {
							newValue = state.cmdmem[info.otherCmd2] & 0x00FFFFFF;
							snprintf(title, sizeof(title), "New value for %.*s (tertiary)", (int)info.title.size(), info.title.data());
							if (PromptStateValue(info, GetHandle(), title, newValue)) {
								newValue |= state.cmdmem[info.otherCmd2] & 0xFF000000;
								SetCmdValue(newValue);
							}
						}
					}
				}
			}
		}
		break;
	}
}

void CtrlStateValues::OnRightClick(int row, int column, const POINT &point) {
	if (gpuDebug == nullptr) {
		return;
	}

	const auto info = rows_[row];
	const auto state = gpuDebug->GetGState();

	POINT screenPt(point);
	ClientToScreen(GetHandle(), &screenPt);

	HMENU subMenu = GetContextMenu(ContextMenuID::GEDBG_STATE);
	SetMenuDefaultItem(subMenu, ID_REGLIST_CHANGE, FALSE);
	EnableMenuItem(subMenu, ID_GEDBG_SETCOND, GPUBreakpoints::IsCmdBreakpoint(info.cmd) ? MF_ENABLED : MF_GRAYED);

	// Ehh, kinda ugly.
	if (!watchList.empty() && rows_ == &watchList[0]) {
		ModifyMenu(subMenu, ID_GEDBG_WATCH, MF_BYCOMMAND | MF_STRING, ID_GEDBG_WATCH, L"Remove Watch");
	} else {
		ModifyMenu(subMenu, ID_GEDBG_WATCH, MF_BYCOMMAND | MF_STRING, ID_GEDBG_WATCH, L"Add Watch");
	}
	if (info.fmt == CMD_FMT_FLAG) {
		ModifyMenu(subMenu, ID_REGLIST_CHANGE, MF_BYCOMMAND | MF_STRING, ID_REGLIST_CHANGE, L"Toggle Flag");
	} else {
		ModifyMenu(subMenu, ID_REGLIST_CHANGE, MF_BYCOMMAND | MF_STRING, ID_REGLIST_CHANGE, L"Change...");
	}

	switch (TriggerContextMenu(ContextMenuID::GEDBG_STATE, GetHandle(), ContextPoint::FromClient(point)))
	{
	case ID_DISASM_TOGGLEBREAKPOINT: {
		bool proceed = true;
		if (GetCmdBreakpointCond(info.cmd, nullptr)) {
			int ret = MessageBox(GetHandle(), L"This breakpoint has a custom condition.\nDo you want to remove it?", L"Confirmation", MB_YESNO);
			proceed = ret == IDYES;
		}
		if (proceed)
			SetItemState(row, ToggleBreakpoint(info) ? 1 : 0);
		break;
	}

	case ID_GEDBG_SETCOND:
		PromptBreakpointCond(info);
		break;

	case ID_DISASM_COPYINSTRUCTIONHEX: {
		char temp[16];
		snprintf(temp, sizeof(temp), "%08x", gstate.cmdmem[info.cmd] & 0x00FFFFFF);
		System_CopyStringToClipboard(temp);
		break;
	}

	case ID_DISASM_COPYINSTRUCTIONDISASM: {
		const bool enabled = info.enableCmd == 0 || (state.cmdmem[info.enableCmd] & 1) == 1;
		const u32 value = state.cmdmem[info.cmd] & 0xFFFFFF;
		const u32 otherValue = state.cmdmem[info.otherCmd] & 0xFFFFFF;
		const u32 otherValue2 = state.cmdmem[info.otherCmd2] & 0xFFFFFF;

		char dest[512];
		FormatStateRow(gpuDebug, dest, sizeof(dest), info.fmt, value, enabled, otherValue, otherValue2);
		System_CopyStringToClipboard(dest);
		break;
	}

	case ID_GEDBG_COPYALL:
		CopyRows(0, GetRowCount());
		break;

	case ID_REGLIST_CHANGE:
		OnDoubleClick(row, STATEVALUES_COL_VALUE);
		break;

	case ID_GEDBG_WATCH:
		ToggleWatchList(info);
		SendMessage(GetParent(GetParent(GetHandle())), WM_GEDBG_UPDATE_WATCH, 0, 0);
		break;
	}
}

bool CtrlStateValues::OnRowPrePaint(int row, LPNMLVCUSTOMDRAW msg) {
	if (gpuDebug && RowValuesChanged(row)) {
		msg->clrText = RGB(255, 0, 0);
		return true;
	}
	return false;
}

void CtrlStateValues::SetCmdValue(u32 op) {
	SendMessage(GetParent(GetParent(GetHandle())), WM_GEDBG_SETCMDWPARAM, op, NULL);
	Update();
}

bool CtrlStateValues::RowValuesChanged(int row) {
	_assert_(gpuDebug != nullptr && row >= 0 && row < rowCount_);

	const auto info = rows_[row];
	const auto state = gpuDebug->GetGState();
	const auto lastState = GPUStepping::LastState();

	if (state.cmdmem[info.cmd] != lastState.cmdmem[info.cmd])
		return true;
	if (info.otherCmd && state.cmdmem[info.otherCmd] != lastState.cmdmem[info.otherCmd])
		return true;
	if (info.otherCmd2 && state.cmdmem[info.otherCmd2] != lastState.cmdmem[info.otherCmd2])
		return true;

	return false;
}

void CtrlStateValues::PromptBreakpointCond(const TabStateRow &info) {
	std::string expression;
	GPUBreakpoints::GetCmdBreakpointCond(info.cmd, &expression);
	if (!InputBox_GetString(GetModuleHandle(NULL), GetHandle(), L"Expression", expression, expression))
		return;

	std::string error;
	if (!GPUBreakpoints::SetCmdBreakpointCond(info.cmd, expression, &error)) {
		MessageBox(GetHandle(), ConvertUTF8ToWString(error).c_str(), L"Invalid expression", MB_OK | MB_ICONEXCLAMATION);
	} else {
		if (info.otherCmd)
			GPUBreakpoints::SetCmdBreakpointCond(info.otherCmd, expression, &error);
		if (info.otherCmd2)
			GPUBreakpoints::SetCmdBreakpointCond(info.otherCmd2, expression, &error);
	}

}

TabStateValues::TabStateValues(const TabStateRow *rows, int rowCount, LPCSTR dialogID, HINSTANCE _hInstance, HWND _hParent)
	: Dialog(dialogID, _hInstance, _hParent) {
	values = new CtrlStateValues(rows, rowCount, GetDlgItem(m_hDlg, IDC_GEDBG_VALUES));
}

TabStateValues::~TabStateValues() {
	delete values;
}

void TabStateValues::UpdateSize(WORD width, WORD height) {
	struct Position {
		int x,y;
		int w,h;
	};

	Position position;
	static const int borderMargin = 5;

	position.x = borderMargin;
	position.y = borderMargin;
	position.w = width - 2 * borderMargin;
	position.h = height - 2 * borderMargin;

	HWND handle = GetDlgItem(m_hDlg,IDC_GEDBG_VALUES);
	MoveWindow(handle, position.x, position.y, position.w, position.h, TRUE);
}

BOOL TabStateValues::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_SIZE:
		UpdateSize(LOWORD(lParam), HIWORD(lParam));
		return TRUE;

	case WM_NOTIFY:
		switch (wParam)
		{
		case IDC_GEDBG_VALUES:
			SetWindowLongPtr(m_hDlg, DWLP_MSGRESULT, values->HandleNotify(lParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}

TabStateFlags::TabStateFlags(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateFlagsRows, ARRAY_SIZE(g_stateFlagsRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateLighting::TabStateLighting(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateLightingRows, ARRAY_SIZE(g_stateLightingRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateSettings::TabStateSettings(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateSettingsRows, ARRAY_SIZE(g_stateSettingsRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateTexture::TabStateTexture(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(g_stateTextureRows, ARRAY_SIZE(g_stateTextureRows), (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

TabStateWatch::TabStateWatch(HINSTANCE _hInstance, HWND _hParent)
	: TabStateValues(nullptr, 0, (LPCSTR)IDD_GEDBG_TAB_VALUES, _hInstance, _hParent) {
}

void TabStateWatch::Update() {
	if (watchList.empty()) {
		values->UpdateRows(nullptr, 0);
	} else {
		values->UpdateRows(&watchList[0], (int)watchList.size());
	}
	TabStateValues::Update();
}
