#include <cstdio>
#include "Common/Common.h"
#include "Common/StringUtils.h"
#include "Common/Data/Convert/ColorConv.h"
#include "GPU/Debugger/State.h"
#include "GPU/GPU.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "Core/System.h"

void FormatStateRow(GPUDebugInterface *gpudebug, char *dest, size_t destSize, CmdFormatType fmt, u32 value, bool enabled, u32 otherValue, u32 otherValue2) {
	value &= 0xFFFFFF;
	otherValue &= 0xFFFFFF;
	otherValue2 &= 0xFFFFFF;
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
	if (PSP_GetBootState() != BootState::Complete) {
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


static void SwapUVs(GPUDebugVertex &a, GPUDebugVertex &b) {
	float tempu = a.u;
	float tempv = a.v;
	a.u = b.u;
	a.v = b.v;
	b.u = tempu;
	b.v = tempv;
}

static void RotateUVThrough(GPUDebugVertex v[4]) {
	float x1 = v[2].x;
	float x2 = v[0].x;
	float y1 = v[2].y;
	float y2 = v[0].y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2))
		SwapUVs(v[1], v[3]);
}

static void ExpandRectangles(std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices, int &count, bool throughMode) {
	static std::vector<GPUDebugVertex> newVerts;
	static std::vector<u16> newInds;

	bool useInds = true;
	size_t numInds = indices.size();
	if (indices.empty()) {
		useInds = false;
		numInds = count;
	}

	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numInds = numInds & ~1;

	// Will need 4 coords and 6 points per rectangle (currently 2 each.)
	newVerts.resize(numInds * 2);
	newInds.resize(numInds * 3);

	u16 v = 0;
	GPUDebugVertex *vert = &newVerts[0];
	u16 *ind = &newInds[0];
	for (size_t i = 0; i < numInds; i += 2) {
		const auto &orig_tl = useInds ? vertices[indices[i + 0]] : vertices[i + 0];
		const auto &orig_br = useInds ? vertices[indices[i + 1]] : vertices[i + 1];

		vert[0] = orig_br;

		// Top right.
		vert[1] = orig_br;
		vert[1].y = orig_tl.y;
		vert[1].v = orig_tl.v;

		vert[2] = orig_tl;

		// Bottom left.
		vert[3] = orig_br;
		vert[3].x = orig_tl.x;
		vert[3].u = orig_tl.u;

		// That's the four corners. Now process UV rotation.
		// This is the same for through and non-through, since it's already transformed.
		RotateUVThrough(vert);

		// Build the two 3 point triangles from our 4 coordinates.
		*ind++ = v + 0;
		*ind++ = v + 1;
		*ind++ = v + 2;
		*ind++ = v + 3;
		*ind++ = v + 0;
		*ind++ = v + 2;

		vert += 4;
		v += 4;
	}

	std::swap(vertices, newVerts);
	std::swap(indices, newInds);
	count *= 3;
}

static void ExpandBezier(int &count, int op, const std::vector<SimpleVertex> &simpleVerts, const std::vector<u16> &indices, std::vector<SimpleVertex> &generatedVerts, std::vector<u16> &generatedInds) {
	using namespace Spline;

	int count_u = (op >> 0) & 0xFF;
	int count_v = (op >> 8) & 0xFF;
	// Real hardware seems to draw nothing when given < 4 either U or V.
	if (count_u < 4 || count_v < 4)
		return;

	BezierSurface surface;
	surface.num_points_u = count_u;
	surface.num_points_v = count_v;
	surface.tess_u = gstate.getPatchDivisionU();
	surface.tess_v = gstate.getPatchDivisionV();
	surface.num_patches_u = (count_u - 1) / 3;
	surface.num_patches_v = (count_v - 1) / 3;
	surface.primType = gstate.getPatchPrimitiveType();
	surface.patchFacing = false;

	int num_points = count_u * count_v;
	// Make an array of pointers to the control points, to get rid of indices.
	std::vector<const SimpleVertex *> points(num_points);
	for (int idx = 0; idx < num_points; idx++)
		points[idx] = simpleVerts.data() + (!indices.empty() ? indices[idx] : idx);

	int total_patches = surface.num_patches_u * surface.num_patches_v;
	generatedVerts.resize((surface.tess_u + 1) * (surface.tess_v + 1) * total_patches);
	generatedInds.resize(surface.tess_u * surface.tess_v * 6 * total_patches);

	OutputBuffers output;
	output.vertices = generatedVerts.data();
	output.indices = generatedInds.data();
	output.count = 0;

	ControlPoints cpoints;
	cpoints.pos = new Vec3f[num_points];
	cpoints.tex = new Vec2f[num_points];
	cpoints.col = new Vec4f[num_points];
	cpoints.Convert(points.data(), num_points);

	surface.Init((int)generatedVerts.size());
	SoftwareTessellation(output, surface, gstate.vertType, cpoints);
	count = output.count;

	delete[] cpoints.pos;
	delete[] cpoints.tex;
	delete[] cpoints.col;
}

static void ExpandSpline(int &count, int op, const std::vector<SimpleVertex> &simpleVerts, const std::vector<u16> &indices, std::vector<SimpleVertex> &generatedVerts, std::vector<u16> &generatedInds) {
	using namespace Spline;

	int count_u = (op >> 0) & 0xFF;
	int count_v = (op >> 8) & 0xFF;
	// Real hardware seems to draw nothing when given < 4 either U or V.
	if (count_u < 4 || count_v < 4)
		return;

	SplineSurface surface;
	surface.num_points_u = count_u;
	surface.num_points_v = count_v;
	surface.tess_u = gstate.getPatchDivisionU();
	surface.tess_v = gstate.getPatchDivisionV();
	surface.type_u = (op >> 16) & 0x3;
	surface.type_v = (op >> 18) & 0x3;
	surface.num_patches_u = count_u - 3;
	surface.num_patches_v = count_v - 3;
	surface.primType = gstate.getPatchPrimitiveType();
	surface.patchFacing = false;

	int num_points = count_u * count_v;
	// Make an array of pointers to the control points, to get rid of indices.
	std::vector<const SimpleVertex *> points(num_points);
	for (int idx = 0; idx < num_points; idx++)
		points[idx] = simpleVerts.data() + (!indices.empty() ? indices[idx] : idx);

	int patch_div_s = surface.num_patches_u * surface.tess_u;
	int patch_div_t = surface.num_patches_v * surface.tess_v;
	generatedVerts.resize((patch_div_s + 1) * (patch_div_t + 1));
	generatedInds.resize(patch_div_s * patch_div_t * 6);

	OutputBuffers output;
	output.vertices = generatedVerts.data();
	output.indices = generatedInds.data();
	output.count = 0;

	ControlPoints cpoints;
	cpoints.pos = (Vec3f *)AllocateAlignedMemory(sizeof(Vec3f) * num_points, 16);
	cpoints.tex = (Vec2f *)AllocateAlignedMemory(sizeof(Vec2f) * num_points, 16);
	cpoints.col = (Vec4f *)AllocateAlignedMemory(sizeof(Vec4f) * num_points, 16);
	cpoints.Convert(points.data(), num_points);

	surface.Init((int)generatedVerts.size());
	SoftwareTessellation(output, surface, gstate.vertType, cpoints);
	count = output.count;

	FreeAlignedMemory(cpoints.pos);
	FreeAlignedMemory(cpoints.tex);
	FreeAlignedMemory(cpoints.col);
}

bool GetPrimPreview(u32 op, GEPrimitiveType &prim, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices, int &count) {
	u32 prim_type = GE_PRIM_INVALID;
	int count_u = 0;
	int count_v = 0;

	const u32 cmd = op >> 24;
	if (cmd == GE_CMD_PRIM) {
		prim_type = (op >> 16) & 0x7;
		count = op & 0xFFFF;
	} else {
		const GEPrimitiveType primLookup[] = { GE_PRIM_TRIANGLES, GE_PRIM_LINES, GE_PRIM_POINTS, GE_PRIM_POINTS };
		prim_type = primLookup[gstate.getPatchPrimitiveType()];
		count_u = (op & 0x00FF) >> 0;
		count_v = (op & 0xFF00) >> 8;
		count = count_u * count_v;
	}

	if (prim_type >= 7) {
		ERROR_LOG(Log::G3D, "Unsupported prim type: %x", op);
		return false;
	}
	if (!gpuDebug) {
		ERROR_LOG(Log::G3D, "Invalid debugging environment, shutting down?");
		return false;
	}
	if (count == 0) {
		return false;
	}

	prim = static_cast<GEPrimitiveType>(prim_type);

	if (!gpuDebug->GetCurrentDrawAsDebugVertices(count, vertices, indices)) {
		ERROR_LOG(Log::G3D, "Vertex preview not yet supported");
		return false;
	}

	if (cmd != GE_CMD_PRIM) {
		static std::vector<SimpleVertex> generatedVerts;
		static std::vector<u16> generatedInds;

		static std::vector<SimpleVertex> simpleVerts;
		simpleVerts.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			// For now, let's just copy back so we can use TessellateBezierPatch/TessellateSplinePatch...
			simpleVerts[i].uv[0] = vertices[i].u;
			simpleVerts[i].uv[1] = vertices[i].v;
			simpleVerts[i].pos = Vec3Packedf(vertices[i].x, vertices[i].y, vertices[i].z);
		}

		if (cmd == GE_CMD_BEZIER) {
			ExpandBezier(count, op, simpleVerts, indices, generatedVerts, generatedInds);
		} else if (cmd == GE_CMD_SPLINE) {
			ExpandSpline(count, op, simpleVerts, indices, generatedVerts, generatedInds);
		}

		vertices.resize(generatedVerts.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			vertices[i].u = generatedVerts[i].uv[0];
			vertices[i].v = generatedVerts[i].uv[1];
			vertices[i].x = generatedVerts[i].pos.x;
			vertices[i].y = generatedVerts[i].pos.y;
			vertices[i].z = generatedVerts[i].pos.z;
		}
		indices = generatedInds;
	}

	if (prim == GE_PRIM_RECTANGLES) {
		ExpandRectangles(vertices, indices, count, gpuDebug->GetGState().isModeThrough());
	}

	// TODO: Probably there's a better way and place to do this.
	u16 minIndex = 0;
	u16 maxIndex = count - 1;
	if (!indices.empty()) {
		_dbg_assert_(count <= indices.size());
		minIndex = 0xFFFF;
		maxIndex = 0;
		for (int i = 0; i < count; ++i) {
			if (minIndex > indices[i]) {
				minIndex = indices[i];
			}
			if (maxIndex < indices[i]) {
				maxIndex = indices[i];
			}
		}
	}

	auto wrapCoord = [](float &coord) {
		if (coord < 0.0f) {
			coord += ceilf(-coord);
		}
		if (coord > 1.0f) {
			coord -= floorf(coord);
		}
	};

	const float invTexWidth = 1.0f / gpuDebug->GetGState().getTextureWidth(0);
	const float invTexHeight = 1.0f / gpuDebug->GetGState().getTextureHeight(0);
	bool clampS = gpuDebug->GetGState().isTexCoordClampedS();
	bool clampT = gpuDebug->GetGState().isTexCoordClampedT();
	for (u16 i = minIndex; i <= maxIndex; ++i) {
		vertices[i].u *= invTexWidth;
		vertices[i].v *= invTexHeight;
		if (!clampS)
			wrapCoord(vertices[i].u);
		if (!clampT)
			wrapCoord(vertices[i].v);
	}

	return true;
}

void DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]) {
	switch (fmt) {
	case GPU_DBG_FORMAT_565:
	case GPU_DBG_FORMAT_565_REV:
	case GPU_DBG_FORMAT_5551:
	case GPU_DBG_FORMAT_5551_REV:
	case GPU_DBG_FORMAT_5551_BGRA:
	case GPU_DBG_FORMAT_4444:
	case GPU_DBG_FORMAT_4444_REV:
	case GPU_DBG_FORMAT_4444_BGRA:
	case GPU_DBG_FORMAT_8888:
	case GPU_DBG_FORMAT_8888_BGRA:
		DescribePixelRGBA(pix, fmt, x, y, desc);
		break;

	case GPU_DBG_FORMAT_16BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, pix, pix * (1.0f / 65535.0f));
		break;

	case GPU_DBG_FORMAT_8BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, pix, pix * (1.0f / 255.0f));
		break;

	case GPU_DBG_FORMAT_24BIT_8X:
	{
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		// These are only ever going to be depth values, so let's also show scaled to 16 bit.
		snprintf(desc, 256, "%d,%d: %d / %f / %f", x, y, pix & 0x00FFFFFF, (pix & 0x00FFFFFF) * (1.0f / 16777215.0f), depthScale.DecodeToU16((pix & 0x00FFFFFF) * (1.0f / 16777215.0f)));
		break;
	}

	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
	{
		// These are only ever going to be depth values, so let's also show scaled to 16 bit.
		int z24 = pix & 0x00FFFFFF;
		int z16 = z24 - 0x800000 + 0x8000;
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, z16, z16 * (1.0f / 65535.0f));
	}
	break;

	case GPU_DBG_FORMAT_24X_8BIT:
		snprintf(desc, 256, "%d,%d: %d / %f", x, y, (pix >> 24) & 0xFF, ((pix >> 24) & 0xFF) * (1.0f / 255.0f));
		break;

	case GPU_DBG_FORMAT_FLOAT:
	{
		float pixf = *(float *)&pix;
		DepthScaleFactors depthScale = GetDepthScaleFactors(gstate_c.UseFlags());
		snprintf(desc, 256, "%d,%d: %f / %f", x, y, pixf, depthScale.DecodeToU16(pixf));
		break;
	}

	case GPU_DBG_FORMAT_FLOAT_DIV_256:
	{
		double z = *(float *)&pix;
		int z24 = (int)(z * 16777215.0);

		DepthScaleFactors factors = GetDepthScaleFactors(gstate_c.UseFlags());
		// TODO: Use GetDepthScaleFactors here too, verify it's the same.
		int z16 = z24 - 0x800000 + 0x8000;

		int z16_2 = factors.DecodeToU16(z);

		snprintf(desc, 256, "%d,%d: %d / %f", x, y, z16, (z - 0.5 + (1.0 / 512.0)) * 256.0);
	}
	break;

	default:
		snprintf(desc, 256, "Unexpected format");
	}
}

void DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]) {
	u32 r = -1, g = -1, b = -1, a = -1;

	switch (fmt) {
	case GPU_DBG_FORMAT_565:
		r = Convert5To8((pix >> 0) & 0x1F);
		g = Convert6To8((pix >> 5) & 0x3F);
		b = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_565_REV:
		b = Convert5To8((pix >> 0) & 0x1F);
		g = Convert6To8((pix >> 5) & 0x3F);
		r = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_5551:
		r = Convert5To8((pix >> 0) & 0x1F);
		g = Convert5To8((pix >> 5) & 0x1F);
		b = Convert5To8((pix >> 10) & 0x1F);
		a = (pix >> 15) & 1 ? 255 : 0;
		break;
	case GPU_DBG_FORMAT_5551_REV:
		a = pix & 1 ? 255 : 0;
		b = Convert5To8((pix >> 1) & 0x1F);
		g = Convert5To8((pix >> 6) & 0x1F);
		r = Convert5To8((pix >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_5551_BGRA:
		b = Convert5To8((pix >> 0) & 0x1F);
		g = Convert5To8((pix >> 5) & 0x1F);
		r = Convert5To8((pix >> 10) & 0x1F);
		a = (pix >> 15) & 1 ? 255 : 0;
		break;
	case GPU_DBG_FORMAT_4444:
		r = Convert4To8((pix >> 0) & 0x0F);
		g = Convert4To8((pix >> 4) & 0x0F);
		b = Convert4To8((pix >> 8) & 0x0F);
		a = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_4444_REV:
		a = Convert4To8((pix >> 0) & 0x0F);
		b = Convert4To8((pix >> 4) & 0x0F);
		g = Convert4To8((pix >> 8) & 0x0F);
		r = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_4444_BGRA:
		b = Convert4To8((pix >> 0) & 0x0F);
		g = Convert4To8((pix >> 4) & 0x0F);
		r = Convert4To8((pix >> 8) & 0x0F);
		a = Convert4To8((pix >> 12) & 0x0F);
		break;
	case GPU_DBG_FORMAT_8888:
		r = (pix >> 0) & 0xFF;
		g = (pix >> 8) & 0xFF;
		b = (pix >> 16) & 0xFF;
		a = (pix >> 24) & 0xFF;
		break;
	case GPU_DBG_FORMAT_8888_BGRA:
		b = (pix >> 0) & 0xFF;
		g = (pix >> 8) & 0xFF;
		r = (pix >> 16) & 0xFF;
		a = (pix >> 24) & 0xFF;
		break;

	default:
		snprintf(desc, 256, "Unexpected format");
		return;
	}

	snprintf(desc, 256, "%d,%d: r=%d, g=%d, b=%d, a=%d", x, y, r, g, b, a);
}
