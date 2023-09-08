// Copyright (c) 2022- PPSSPP Project.

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

#pragma once

#include "GPU/ge_constants.h"

enum class GECmdFormat {
	DATA, // Just regular data.
	DATA16, // Data, but only 16 bits of it used.
	DATA8, // 8 bits value (alpha, etc.)
	NONE, // Should have no arguments or value.
	RELATIVE_ADDR, // 24 bits added to base + offset.
	PRIM, // 16 bits count, 3 bits type.
	BEZIER, // 8 bits ucount, 8 bits vcount.
	SPLINE, // 8 bits ucount, 8 bits vcount, 2 bits utype, 2 bits vtype.
	JUMP, // Like RELATIVE_ADDR, but lower 2 bits ignored.
	SIGNAL, // 16 bits data, 8 bits signal type.
	HIGH_ADDR_ONLY, // 16 bits ignored, 8 bits for a high address.
	LOW_ADDR_ONLY, // 24 bits low address.
	LOW_ADDR, // 24 bits low address.
	HIGH_ADDR, // 16 bits ignored, 8 bits for a high address.
	VERTEX_TYPE, // 24-bits of vtype flags (complex.)
	OFFSET_ADDR, // 24 bits become the high bits of offset.
	X10_Y10, // 10 bits X, 10 bits Y.
	FLAG, // 1 bit on/off.
	BONE_NUM, // 7 bits number.
	MATRIX_NUM, // 4 bits number.
	FLOAT, // 24 bits float data.
	PATCH_DIVISION, // 8 bits divisionu, 8 bits divisionv.
	PATCH_PRIM, // 2 bits primitive type.
	SUBPIXEL_COORD, // 4 bits subpixel, 12 bits integer value.
	MATERIAL_UPDATE, // 3 bits colors to update.
	RGB, // 24 bits RGB color.
	LIGHT_TYPE, // 8 bits computation (2 bits), 2 bits type.
	STRIDE, // 11 bits.
	STRIDE_HIGH_ADDR, // 11 bits stride, 5 ignored, 8 bits high address.
	TEX_SIZE, // 4 bits width power, 4 bits ignored, 4 bits height power.
	TEX_MAP_MODE, // 8 bits mode (2 bits), 2 bits matrix factor.
	TEX_LIGHT_SRC, // 8 bits lightu (2 bits), 2 bits lightv.
	TEX_MODE, // 8 bits swizzle (1 bit), 8 bits separate clut4 (1 bit), 3 bits max level.
	TEX_FORMAT, // 4 bits format.
	TEX_FILTER, // 3 bits minify mode, 5 bits ignored, 1 bit magnify mode.
	TEX_CLAMP, // 1 bit clampu, 7 bits ignored, 1 bit clampv.
	TEX_LEVEL_MODE, // 2 bits mode, 15 bits ignored, 4.4 (8 bits) bias.
	TEX_FUNC, // 3 bits function, 5 bits ignored, 1 bit use alpha, 7 bits ignored, 1 bit double color.
	CLUT_BLOCKS, // 6 bits block count.
	CLUT_FORMAT, // 2 bits format, 5 bits shift, 1 bit ignored, 8 bits mask, 5 bits base.
	CLEAR_MODE, // 1 bit on, 7 bits ignored, 3 bits aspect.
	COLOR_TEST_FUNC, // 2 bits test function.
	ALPHA_TEST, // 3 bits function, 5 bits ignored, 8 bits ref, 8 bits mask.
	STENCIL_OP, // 8 bits sfail (3 bits), 8 bits zfail (3 bits), 8 bits zpass (3 bits.)
	DEPTH_TEST_FUNC, // 3 bits function.
	BLEND_MODE, // 4 bits srcfactor, 4 bits dstfactor, 3 bits equation.
	DITHER_ROW, // 4 s.3.0 fixed point dither offsets.
	LOGIC_OP, // 4 bits logic operation.
	ALPHA_PRIM, // 8 bits alpha, 3 bits primitive type, 1 bit antialias, 6 bit clip?, 1 bit shading, 1 bit cullenable, 1 bit cullface, 1 bit tex enable, 1 bit fog, 1 bit dither.
};

struct GECmdInfo {
	GECommand reg;
	const char *name;
	GECmdFormat fmt;
};

bool GECmdInfoByName(const char *name, GECmdInfo &info);
GECmdInfo GECmdInfoByCmd(GECommand reg);
