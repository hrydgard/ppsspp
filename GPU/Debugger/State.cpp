#include <cstdio>
#include "Common/Common.h"
#include "Common/StringUtils.h"
#include "GPU/Debugger/State.h"
#include "GPU/GPU.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "Core/System.h"

const GECommand g_stateFlagsRows[] = {
	GE_CMD_LIGHTINGENABLE,
	GE_CMD_LIGHTENABLE0,
	GE_CMD_LIGHTENABLE1,
	GE_CMD_LIGHTENABLE2,
	GE_CMD_LIGHTENABLE3,
	GE_CMD_DEPTHCLAMPENABLE,
	GE_CMD_CULLFACEENABLE,
	GE_CMD_TEXTUREMAPENABLE,
	GE_CMD_FOGENABLE,
	GE_CMD_DITHERENABLE,
	GE_CMD_ALPHABLENDENABLE,
	GE_CMD_ALPHATESTENABLE,
	GE_CMD_ZTESTENABLE,
	GE_CMD_STENCILTESTENABLE,
	GE_CMD_ANTIALIASENABLE,
	GE_CMD_PATCHCULLENABLE,
	GE_CMD_COLORTESTENABLE,
	GE_CMD_LOGICOPENABLE,
	GE_CMD_ZWRITEDISABLE,
};
const size_t g_stateFlagsRowsSize = ARRAY_SIZE(g_stateFlagsRows);

const GECommand g_stateLightingRows[] = {
	GE_CMD_AMBIENTCOLOR,
	GE_CMD_AMBIENTALPHA,
	GE_CMD_MATERIALUPDATE,
	GE_CMD_MATERIALEMISSIVE,
	GE_CMD_MATERIALAMBIENT,
	GE_CMD_MATERIALDIFFUSE,
	GE_CMD_MATERIALALPHA,
	GE_CMD_MATERIALSPECULAR,
	GE_CMD_MATERIALSPECULARCOEF,
	GE_CMD_REVERSENORMAL,
	GE_CMD_SHADEMODE,
	GE_CMD_LIGHTMODE,
	GE_CMD_LIGHTTYPE0,
	GE_CMD_LIGHTTYPE1,
	GE_CMD_LIGHTTYPE2,
	GE_CMD_LIGHTTYPE3,
	GE_CMD_LX0,
	GE_CMD_LX1,
	GE_CMD_LX2,
	GE_CMD_LX3,
	GE_CMD_LDX0,
	GE_CMD_LDX1,
	GE_CMD_LDX2,
	GE_CMD_LDX3,
	GE_CMD_LKA0,
	GE_CMD_LKA1,
	GE_CMD_LKA2,
	GE_CMD_LKA3,
	GE_CMD_LKS0,
	GE_CMD_LKS1,
	GE_CMD_LKS2,
	GE_CMD_LKS3,
	GE_CMD_LKO0,
	GE_CMD_LKO1,
	GE_CMD_LKO2,
	GE_CMD_LKO3,
	GE_CMD_LAC0,
	GE_CMD_LDC0,
	GE_CMD_LSC0,
	GE_CMD_LAC1,
	GE_CMD_LDC1,
	GE_CMD_LSC1,
	GE_CMD_LAC2,
	GE_CMD_LDC2,
	GE_CMD_LSC2,
	GE_CMD_LAC3,
	GE_CMD_LDC3,
	GE_CMD_LSC3,
};
const size_t g_stateLightingRowsSize = ARRAY_SIZE(g_stateLightingRows);

const GECommand g_stateTextureRows[] = {
	GE_CMD_TEXADDR0,
	GE_CMD_TEXSIZE0,
	GE_CMD_TEXFORMAT,
	GE_CMD_CLUTADDR,
	GE_CMD_CLUTFORMAT,
	GE_CMD_TEXSCALEU,
	GE_CMD_TEXSCALEV,
	GE_CMD_TEXOFFSETU,
	GE_CMD_TEXOFFSETV,
	GE_CMD_TEXMAPMODE,
	GE_CMD_TEXSHADELS,
	GE_CMD_TEXFUNC,
	GE_CMD_TEXENVCOLOR,
	GE_CMD_TEXMODE,
	GE_CMD_TEXFILTER,
	GE_CMD_TEXWRAP,
	GE_CMD_TEXLEVEL,
	GE_CMD_TEXLODSLOPE,
	GE_CMD_TEXADDR1,
	GE_CMD_TEXADDR2,
	GE_CMD_TEXADDR3,
	GE_CMD_TEXADDR4,
	GE_CMD_TEXADDR5,
	GE_CMD_TEXADDR6,
	GE_CMD_TEXADDR7,
	GE_CMD_TEXSIZE1,
	GE_CMD_TEXSIZE2,
	GE_CMD_TEXSIZE3,
	GE_CMD_TEXSIZE4,
	GE_CMD_TEXSIZE5,
	GE_CMD_TEXSIZE6,
	GE_CMD_TEXSIZE7,
};
const size_t g_stateTextureRowsSize = ARRAY_SIZE(g_stateTextureRows);

const GECommand g_stateSettingsRows[] = {
	GE_CMD_FRAMEBUFPTR,
	GE_CMD_FRAMEBUFPIXFORMAT,
	GE_CMD_ZBUFPTR,
	GE_CMD_VIEWPORTXSCALE,
	GE_CMD_VIEWPORTXCENTER,
	GE_CMD_SCISSOR1,
	GE_CMD_REGION1,
	GE_CMD_COLORTEST,
	GE_CMD_ALPHATEST,
	GE_CMD_CLEARMODE,
	GE_CMD_STENCILTEST,
	GE_CMD_STENCILOP,
	GE_CMD_ZTEST,
	GE_CMD_MASKRGB,
	GE_CMD_MASKALPHA,
	GE_CMD_TRANSFERSRC,
	GE_CMD_TRANSFERSRCPOS,
	GE_CMD_TRANSFERDST,
	GE_CMD_TRANSFERDSTPOS,
	GE_CMD_TRANSFERSIZE,
	GE_CMD_VERTEXTYPE,
	GE_CMD_OFFSETADDR,
	GE_CMD_VADDR,
	GE_CMD_IADDR,
	GE_CMD_MINZ,
	GE_CMD_MAXZ,
	GE_CMD_OFFSETX,
	GE_CMD_CULL,
	GE_CMD_BLENDMODE,
	GE_CMD_BLENDFIXEDA,
	GE_CMD_BLENDFIXEDB,
	GE_CMD_LOGICOP,
	GE_CMD_FOG1,
	GE_CMD_FOG2,
	GE_CMD_FOGCOLOR,
	GE_CMD_MORPHWEIGHT0,
	GE_CMD_MORPHWEIGHT1,
	GE_CMD_MORPHWEIGHT2,
	GE_CMD_MORPHWEIGHT3,
	GE_CMD_MORPHWEIGHT4,
	GE_CMD_MORPHWEIGHT5,
	GE_CMD_MORPHWEIGHT6,
	GE_CMD_MORPHWEIGHT7,
	GE_CMD_PATCHDIVISION,
	GE_CMD_PATCHPRIMITIVE,
	GE_CMD_PATCHFACING,
	GE_CMD_DITH0,
	GE_CMD_DITH1,
	GE_CMD_DITH2,
	GE_CMD_DITH3,
	GE_CMD_VSCX,
	GE_CMD_VSCZ,
	GE_CMD_VTCS,
	GE_CMD_VCV,
	GE_CMD_VSCV,
	GE_CMD_VFC,
	GE_CMD_VAP,
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
