#include "GPU/ge_constants.h"

const char *GeCmdToString(GECommand cmd) {
	switch (cmd) {
	case GE_CMD_NOP: return "NOP";
	case GE_CMD_VADDR: return "VADDR";
	case GE_CMD_IADDR: return "IADDR";
	case GE_CMD_PRIM: return "PRIM";
	case GE_CMD_BEZIER: return "BEZIER";
	case GE_CMD_SPLINE: return "SPLINE";
	case GE_CMD_BOUNDINGBOX: return "BOUNDINGBOX";
	case GE_CMD_JUMP: return "JUMP";
	case GE_CMD_BJUMP: return "BJUMP";
	case GE_CMD_CALL: return "CALL";
	case GE_CMD_RET: return "RET";
	case GE_CMD_END: return "END";
	case GE_CMD_SIGNAL: return "SIGNAL";
	case GE_CMD_FINISH: return "FINISH";
	case GE_CMD_BASE: return "BASE";
	case GE_CMD_VERTEXTYPE: return "VERTEXTYPE";
	case GE_CMD_OFFSETADDR: return "OFFSETADDR";
	case GE_CMD_ORIGIN: return "ORIGIN";
	case GE_CMD_REGION1: return "REGION1";
	case GE_CMD_REGION2: return "REGION2";
	case GE_CMD_LIGHTINGENABLE: return "LIGHTINGENABLE";
	case GE_CMD_LIGHTENABLE0: return "LIGHTENABLE0";
	case GE_CMD_LIGHTENABLE1: return "LIGHTENABLE1";
	case GE_CMD_LIGHTENABLE2: return "LIGHTENABLE2";
	case GE_CMD_LIGHTENABLE3: return "LIGHTENABLE3";
	case GE_CMD_DEPTHCLIPENABLE: return "DEPTHCLIPENABLE";
	case GE_CMD_CULLFACEENABLE: return "CULLFACEENABLE";
	case GE_CMD_TEXTUREMAPENABLE: return "TEXTUREMAPENABLE";
	case GE_CMD_FOGENABLE: return "FOGENABLE";
	case GE_CMD_DITHERENABLE: return "DITHERENABLE";
	case GE_CMD_ALPHABLENDENABLE: return "ALPHABLENDENABLE";
	case GE_CMD_ALPHATESTENABLE: return "ALPHATESTENABLE";
	case GE_CMD_ZTESTENABLE: return "ZTESTENABLE";
	case GE_CMD_STENCILTESTENABLE: return "STENCILTESTENABLE";
	case GE_CMD_ANTIALIASENABLE: return "ANTIALIASENABLE";
	case GE_CMD_PATCHCULLENABLE: return "PATCHCULLENABLE";
	case GE_CMD_COLORTESTENABLE: return "COLORTESTENABLE";
	case GE_CMD_LOGICOPENABLE: return "LOGICOPENABLE";
	case GE_CMD_BONEMATRIXNUMBER: return "BONEMATRIXNUMBER";
	case GE_CMD_BONEMATRIXDATA: return "BONEMATRIXDATA";
	case GE_CMD_MORPHWEIGHT0: return "MORPHWEIGHT0";
	case GE_CMD_MORPHWEIGHT1: return "MORPHWEIGHT1";
	case GE_CMD_MORPHWEIGHT2: return "MORPHWEIGHT2";
	case GE_CMD_MORPHWEIGHT3: return "MORPHWEIGHT3";
	case GE_CMD_MORPHWEIGHT4: return "MORPHWEIGHT4";
	case GE_CMD_MORPHWEIGHT5: return "MORPHWEIGHT5";
	case GE_CMD_MORPHWEIGHT6: return "MORPHWEIGHT6";
	case GE_CMD_MORPHWEIGHT7: return "MORPHWEIGHT7";
	case GE_CMD_PATCHDIVISION: return "PATCHDIVISION";
	case GE_CMD_PATCHPRIMITIVE: return "PATCHPRIMITIVE";
	case GE_CMD_PATCHFACING: return "PATCHFACING";
	case GE_CMD_WORLDMATRIXNUMBER: return "WORLDMATRIXNUMBER";
	case GE_CMD_WORLDMATRIXDATA: return "WORLDMATRIXDATA";
	case GE_CMD_VIEWMATRIXNUMBER: return "VIEWMATRIXNUMBER";
	case GE_CMD_VIEWMATRIXDATA: return "VIEWMATRIXDATA";
	case GE_CMD_PROJMATRIXNUMBER: return "PROJMATRIXNUMBER";
	case GE_CMD_PROJMATRIXDATA: return "PROJMATRIXDATA";
	case GE_CMD_TGENMATRIXNUMBER: return "TGENMATRIXNUMBER";
	case GE_CMD_TGENMATRIXDATA: return "TGENMATRIXDATA";
	case GE_CMD_VIEWPORTXSCALE: return "VIEWPORTXSCALE";
	case GE_CMD_VIEWPORTYSCALE: return "VIEWPORTYSCALE";
	case GE_CMD_VIEWPORTZSCALE: return "VIEWPORTZSCALE";
	case GE_CMD_VIEWPORTXCENTER: return "VIEWPORTXCENTER";
	case GE_CMD_VIEWPORTYCENTER: return "VIEWPORTYCENTER";
	case GE_CMD_VIEWPORTZCENTER: return "VIEWPORTZCENTER";
	case GE_CMD_TEXSCALEU: return "TEXSCALEU";
	case GE_CMD_TEXSCALEV: return "TEXSCALEV";
	case GE_CMD_TEXOFFSETU: return "TEXOFFSETU";
	case GE_CMD_TEXOFFSETV: return "TEXOFFSETV";
	case GE_CMD_OFFSETX: return "OFFSETX";
	case GE_CMD_OFFSETY: return "OFFSETY";
	case GE_CMD_SHADEMODE: return "SHADEMODE";  // flat or gouraud
	case GE_CMD_REVERSENORMAL: return "REVERSENORMAL";
	case GE_CMD_MATERIALUPDATE: return "MATERIALUPDATE";
	case GE_CMD_MATERIALEMISSIVE: return "MATERIALEMISSIVE"; //not sure about these but this makes sense
	case GE_CMD_MATERIALAMBIENT: return "MATERIALAMBIENT";  //gotta try enabling lighting and check :)
	case GE_CMD_MATERIALDIFFUSE: return "MATERIALDIFFUSE";
	case GE_CMD_MATERIALSPECULAR: return "MATERIALSPECULAR";
	case GE_CMD_MATERIALALPHA: return "MATERIALALPHA";
	case GE_CMD_MATERIALSPECULARCOEF: return "MATERIALSPECULARCOEF";
	case GE_CMD_AMBIENTCOLOR: return "AMBIENTCOLOR";
	case GE_CMD_AMBIENTALPHA: return "AMBIENTALPHA";
	case GE_CMD_LIGHTMODE: return "LIGHTMODE";
	case GE_CMD_LIGHTTYPE0: return "LIGHTTYPE0";
	case GE_CMD_LIGHTTYPE1: return "LIGHTTYPE1";
	case GE_CMD_LIGHTTYPE2: return "LIGHTTYPE2";
	case GE_CMD_LIGHTTYPE3: return "LIGHTTYPE3";
	case GE_CMD_LX0: return "LX0";
	case GE_CMD_LY0: return "LY0";
	case GE_CMD_LZ0: return "LZ0";
	case GE_CMD_LX1: return "LX1";
	case GE_CMD_LY1: return "LY1";
	case GE_CMD_LZ1: return "LZ1";
	case GE_CMD_LX2: return "LX2";
	case GE_CMD_LY2: return "LY2";
	case GE_CMD_LZ2: return "LZ2";
	case GE_CMD_LX3: return "LX3";
	case GE_CMD_LY3: return "LY3";
	case GE_CMD_LZ3: return "LZ3";
	case GE_CMD_LDX0: return "LDX0";
	case GE_CMD_LDY0: return "LDY0";
	case GE_CMD_LDZ0: return "LDZ0";
	case GE_CMD_LDX1: return "LDX1";
	case GE_CMD_LDY1: return "LDY1";
	case GE_CMD_LDZ1: return "LDZ1";
	case GE_CMD_LDX2: return "LDX2";
	case GE_CMD_LDY2: return "LDY2";
	case GE_CMD_LDZ2: return "LDZ2";
	case GE_CMD_LDX3: return "LDX3";
	case GE_CMD_LDY3: return "LDY3";
	case GE_CMD_LDZ3: return "LDZ3";
	case GE_CMD_LKA0: return "LKA0";
	case GE_CMD_LKB0: return "LKB0";
	case GE_CMD_LKC0: return "LKC0";
	case GE_CMD_LKA1: return "LKA1";
	case GE_CMD_LKB1: return "LKB1";
	case GE_CMD_LKC1: return "LKC1";
	case GE_CMD_LKA2: return "LKA2";
	case GE_CMD_LKB2: return "LKB2";
	case GE_CMD_LKC2: return "LKC2";
	case GE_CMD_LKA3: return "LKA3";
	case GE_CMD_LKB3: return "LKB3";
	case GE_CMD_LKC3: return "LKC3";
	case GE_CMD_LKS0: return "LKS0";
	case GE_CMD_LKS1: return "LKS1";
	case GE_CMD_LKS2: return "LKS2";
	case GE_CMD_LKS3: return "LKS3";
	case GE_CMD_LKO0: return "LKO0";
	case GE_CMD_LKO1: return "LKO1";
	case GE_CMD_LKO2: return "LKO2";
	case GE_CMD_LKO3: return "LKO3";
	case GE_CMD_LAC0: return "LAC0";
	case GE_CMD_LDC0: return "LDC0";
	case GE_CMD_LSC0: return "LSC0";
	case GE_CMD_LAC1: return "LAC1";
	case GE_CMD_LDC1: return "LDC1";
	case GE_CMD_LSC1: return "LSC1";
	case GE_CMD_LAC2: return "LAC2";
	case GE_CMD_LDC2: return "LDC2";
	case GE_CMD_LSC2: return "LSC2";
	case GE_CMD_LAC3: return "LAC3";
	case GE_CMD_LDC3: return "LDC3";
	case GE_CMD_LSC3: return "LSC3";
	case GE_CMD_CULL: return "CULL";
	case GE_CMD_FRAMEBUFPTR: return "FRAMEBUFPTR";
	case GE_CMD_FRAMEBUFWIDTH: return "FRAMEBUFWIDTH";
	case GE_CMD_ZBUFPTR: return "ZBUFPTR";
	case GE_CMD_ZBUFWIDTH: return "ZBUFWIDTH";
	case GE_CMD_TEXADDR0: return "TEXADDR0";
	case GE_CMD_TEXADDR1: return "TEXADDR1";
	case GE_CMD_TEXADDR2: return "TEXADDR2";
	case GE_CMD_TEXADDR3: return "TEXADDR3";
	case GE_CMD_TEXADDR4: return "TEXADDR4";
	case GE_CMD_TEXADDR5: return "TEXADDR5";
	case GE_CMD_TEXADDR6: return "TEXADDR6";
	case GE_CMD_TEXADDR7: return "TEXADDR7";
	case GE_CMD_TEXBUFWIDTH0: return "TEXBUFWIDTH0";
	case GE_CMD_TEXBUFWIDTH1: return "TEXBUFWIDTH1";
	case GE_CMD_TEXBUFWIDTH2: return "TEXBUFWIDTH2";
	case GE_CMD_TEXBUFWIDTH3: return "TEXBUFWIDTH3";
	case GE_CMD_TEXBUFWIDTH4: return "TEXBUFWIDTH4";
	case GE_CMD_TEXBUFWIDTH5: return "TEXBUFWIDTH5";
	case GE_CMD_TEXBUFWIDTH6: return "TEXBUFWIDTH6";
	case GE_CMD_TEXBUFWIDTH7: return "TEXBUFWIDTH7";
	case GE_CMD_CLUTADDR: return "CLUTADDR";
	case GE_CMD_CLUTADDRUPPER: return "CLUTADDRUPPER";
	case GE_CMD_TRANSFERSRC: return "TRANSFERSRC";
	case GE_CMD_TRANSFERSRCW: return "TRANSFERSRCW";
	case GE_CMD_TRANSFERDST: return "TRANSFERDST";
	case GE_CMD_TRANSFERDSTW: return "TRANSFERDSTW";
	case GE_CMD_TEXSIZE0: return "TEXSIZE0";
	case GE_CMD_TEXSIZE1: return "TEXSIZE1";
	case GE_CMD_TEXSIZE2: return "TEXSIZE2";
	case GE_CMD_TEXSIZE3: return "TEXSIZE3";
	case GE_CMD_TEXSIZE4: return "TEXSIZE4";
	case GE_CMD_TEXSIZE5: return "TEXSIZE5";
	case GE_CMD_TEXSIZE6: return "TEXSIZE6";
	case GE_CMD_TEXSIZE7: return "TEXSIZE7";
	case GE_CMD_TEXMAPMODE: return "TEXMAPMODE";
	case GE_CMD_TEXSHADELS: return "TEXSHADELS";
	case GE_CMD_TEXMODE: return "TEXMODE";
	case GE_CMD_TEXFORMAT: return "TEXFORMAT";
	case GE_CMD_LOADCLUT: return "LOADCLUT";
	case GE_CMD_CLUTFORMAT: return "CLUTFORMAT";
	case GE_CMD_TEXFILTER: return "TEXFILTER";
	case GE_CMD_TEXWRAP: return "TEXWRAP";
	case GE_CMD_TEXLEVEL: return "TEXLEVEL";
	case GE_CMD_TEXFUNC: return "TEXFUNC";
	case GE_CMD_TEXENVCOLOR: return "TEXENVCOLOR";
	case GE_CMD_TEXFLUSH: return "TEXFLUSH";
	case GE_CMD_TEXSYNC: return "TEXSYNC";
	case GE_CMD_FOG1: return "FOG1";
	case GE_CMD_FOG2: return "FOG2";
	case GE_CMD_FOGCOLOR: return "FOGCOLOR";
	case GE_CMD_TEXLODSLOPE: return "TEXLODSLOPE";
	case GE_CMD_FRAMEBUFPIXFORMAT: return "FRAMEBUFPIXFORMAT";
	case GE_CMD_CLEARMODE: return "CLEARMODE";
	case GE_CMD_SCISSOR1: return "SCISSOR1";
	case GE_CMD_SCISSOR2: return "SCISSOR2";
	case GE_CMD_MINZ: return "MINZ";
	case GE_CMD_MAXZ: return "MAXZ";
	case GE_CMD_COLORTEST: return "COLORTEST";
	case GE_CMD_COLORREF: return "COLORREF";
	case GE_CMD_COLORTESTMASK: return "COLORTESTMASK";
	case GE_CMD_ALPHATEST: return "ALPHATEST";
	case GE_CMD_STENCILTEST: return "STENCILTEST";
	case GE_CMD_STENCILOP: return "STENCILOP";
	case GE_CMD_ZTEST: return "ZTEST";
	case GE_CMD_BLENDMODE: return "BLENDMODE";
	case GE_CMD_BLENDFIXEDA: return "BLENDFIXEDA";
	case GE_CMD_BLENDFIXEDB: return "BLENDFIXEDB";
	case GE_CMD_DITH0: return "DITH0";
	case GE_CMD_DITH1: return "DITH1";
	case GE_CMD_DITH2: return "DITH2";
	case GE_CMD_DITH3: return "DITH3";
	case GE_CMD_LOGICOP: return "LOGICOP";
	case GE_CMD_ZWRITEDISABLE: return "ZWRITEDISABLE";
	case GE_CMD_MASKRGB: return "MASKRGB";
	case GE_CMD_MASKALPHA: return "MASKALPHA";
	case GE_CMD_TRANSFERSTART: return "TRANSFERSTART";
	case GE_CMD_TRANSFERSRCPOS: return "TRANSFERSRCPOS";
	case GE_CMD_TRANSFERDSTPOS: return "TRANSFERDSTPOS";
	case GE_CMD_TRANSFERSIZE: return "TRANSFERSIZE";
	case GE_CMD_VSCX: return "VSCX";
	case GE_CMD_VSCY: return "VSCY";
	case GE_CMD_VSCZ: return "VSCZ";
	case GE_CMD_VTCS: return "VTCS";
	case GE_CMD_VTCT: return "VTCT";
	case GE_CMD_VTCQ: return "VTCQ";
	case GE_CMD_VCV: return "VCV";
	case GE_CMD_VAP: return "VAP";
	case GE_CMD_VFC: return "VFC";
	case GE_CMD_VSCV: return "VSCV";
	case GE_CMD_UNKNOWN_03: return "UNKNOWN_03";
	case GE_CMD_UNKNOWN_0D: return "UNKNOWN_0D";
	case GE_CMD_UNKNOWN_11: return "UNKNOWN_11";
	case GE_CMD_UNKNOWN_29: return "UNKNOWN_29";
	case GE_CMD_UNKNOWN_34: return "UNKNOWN_34";
	case GE_CMD_UNKNOWN_35: return "UNKNOWN_35";
	case GE_CMD_UNKNOWN_39: return "UNKNOWN_39";
	case GE_CMD_UNKNOWN_4E: return "UNKNOWN_4E";
	case GE_CMD_UNKNOWN_4F: return "UNKNOWN_4F";
	case GE_CMD_UNKNOWN_52: return "UNKNOWN_52";
	case GE_CMD_UNKNOWN_59: return "UNKNOWN_59";
	case GE_CMD_UNKNOWN_5A: return "UNKNOWN_5A";
	case GE_CMD_UNKNOWN_B6: return "UNKNOWN_B6";
	case GE_CMD_UNKNOWN_B7: return "UNKNOWN_B7";
	case GE_CMD_UNKNOWN_D1: return "UNKNOWN_D1";
	case GE_CMD_UNKNOWN_ED: return "UNKNOWN_ED";
	case GE_CMD_UNKNOWN_EF: return "UNKNOWN_EF";
	case GE_CMD_UNKNOWN_FA: return "UNKNOWN_FA";
	case GE_CMD_UNKNOWN_FB: return "UNKNOWN_FB";
	case GE_CMD_UNKNOWN_FC: return "UNKNOWN_FC";
	case GE_CMD_UNKNOWN_FD: return "UNKNOWN_FD";
	case GE_CMD_UNKNOWN_FE: return "UNKNOWN_FE";
	case GE_CMD_NOP_FF: return "NOP_FF";
	default:
		return "INVALID";
	}
}

const char *GePrimTypeToString(GEPrimitiveType prim) {
	static constexpr const char * primTypes[8] = {
		"POINTS",
		"LINES",
		"LINE_STRIP",
		"TRI_LIST",
		"TRI_STRIP",
		"TRI_FAN",
		"RECTS",
		"CONTINUE_PREV",
	};
	const int p = static_cast<int>(prim);
	if (p < 0 || p >= 8)
		return "INVALID";
	return primTypes[p];
}

const char *GeBufferFormatToString(GEBufferFormat fmt) {
	switch (fmt) {
	case GE_FORMAT_4444: return "4444";
	case GE_FORMAT_5551: return "5551";
	case GE_FORMAT_565: return "565";
	case GE_FORMAT_8888: return "8888";
	case GE_FORMAT_DEPTH16: return "DEPTH16";
	case GE_FORMAT_CLUT8: return "CLUT8";
	default: return "N/A";
	}
}

const char *GEPaletteFormatToString(GEPaletteFormat pfmt) {
	switch (pfmt) {
	case GE_CMODE_16BIT_BGR5650: return "565";
	case GE_CMODE_16BIT_ABGR5551: return "5551";
	case GE_CMODE_16BIT_ABGR4444: return "4444";
	case GE_CMODE_32BIT_ABGR8888: return "8888";
	default: return "N/A";
	}
}

const char *GeTextureFormatToString(GETextureFormat fmt) {
	switch (fmt) {
	case GE_TFMT_5650: return "565";
	case GE_TFMT_5551: return "5551";
	case GE_TFMT_4444: return "4444";
	case GE_TFMT_8888: return "8888";
	case GE_TFMT_CLUT4: return "CLUT4";
	case GE_TFMT_CLUT8: return "CLUT8";
	case GE_TFMT_CLUT16: return "CLUT16";
	case GE_TFMT_CLUT32: return "CLUT32";
	case GE_TFMT_DXT1: return "DXT1";
	case GE_TFMT_DXT3: return "DXT3";
	case GE_TFMT_DXT5: return "DXT5";
	default: return "N/A";
	}
}

const char *GeTextureFormatToString(GETextureFormat tfmt, GEPaletteFormat pfmt) {
	switch (tfmt) {
	case GE_TFMT_CLUT4:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT4_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT4_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT4_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT4_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT8:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT8_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT8_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT8_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT8_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT16:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT16_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT16_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT16_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT16_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT32:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT32_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT32_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT32_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT32_8888";
		default: return "N/A";
		}
	default: return GeTextureFormatToString(tfmt);
	}
}
