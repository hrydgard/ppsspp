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

#include "Common/Log.h"
#include "Common/Math/expression_parser.h"
#include "Common/StringUtils.h"
#include "Core/Debugger/SymbolMap.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/GECommandTable.h"
#include "GPU/GPUState.h"

enum class GEReferenceIndex : uint32_t {
	VADDR = 0x100,
	IADDR,
	OFFSET,
	PC,
	STALL,
	BFLAG,
	OP,
	DATA,
	CLUTADDR,
	TRANSFERSRC,
	TRANSFERDST,
	PRIMCOUNT,
	LASTPRIMCOUNT,

	TEXADDR0,
	TEXADDR1,
	TEXADDR2,
	TEXADDR3,
	TEXADDR4,
	TEXADDR5,
	TEXADDR6,
	TEXADDR7,

	BONE_MATRIX = 0x200,
	WORLD_MATRIX = 0x260,
	VIEW_MATRIX = 0x26C,
	PROJ_MATRIX = 0x278,
	TGEN_MATRIX = 0x288,
	MATRIX_END = 0x294,

	FIELD_START = 0x1000,
	FIELD_END = 0xFF000,
};
ENUM_CLASS_BITOPS(GEReferenceIndex);

struct ReferenceName {
	GEReferenceIndex index;
	const char *name;
};

static constexpr ReferenceName referenceNames[] = {
	{ GEReferenceIndex::VADDR, "vaddr" },
	{ GEReferenceIndex::IADDR, "iaddr" },
	{ GEReferenceIndex::OFFSET, "offset" },
	{ GEReferenceIndex::PC, "pc" },
	{ GEReferenceIndex::STALL, "stall" },
	{ GEReferenceIndex::BFLAG, "bflag" },
	{ GEReferenceIndex::BFLAG, "boundflag" },
	{ GEReferenceIndex::OP, "op" },
	{ GEReferenceIndex::DATA, "data" },
	{ GEReferenceIndex::CLUTADDR, "clutaddr" },
	{ GEReferenceIndex::TRANSFERSRC, "transfersrc" },
	{ GEReferenceIndex::TRANSFERDST, "transferdst" },
	{ GEReferenceIndex::PRIMCOUNT, "primcount" },
	{ GEReferenceIndex::LASTPRIMCOUNT, "lastprimcount" },
	{ GEReferenceIndex::TEXADDR0, "texaddr0" },
	{ GEReferenceIndex::TEXADDR1, "texaddr1" },
	{ GEReferenceIndex::TEXADDR2, "texaddr2" },
	{ GEReferenceIndex::TEXADDR3, "texaddr3" },
	{ GEReferenceIndex::TEXADDR4, "texaddr4" },
	{ GEReferenceIndex::TEXADDR5, "texaddr5" },
	{ GEReferenceIndex::TEXADDR6, "texaddr6" },
	{ GEReferenceIndex::TEXADDR7, "texaddr7" },
};

enum class GECmdField : uint8_t {
	DATA, // Alias for the entire data.
	LOW_FLAG,
	LOW_U2,
	LOW_U4,
	LOW_U7,
	LOW_U8,
	LOW_U10,
	LOW_U10_P1,
	LOW_U11,
	LOW_U16,
	MID_U8,
	MID_U10, // At 10, 10 bits.
	MID_U10_P1, // At 10, 10 bits (add 1 to value.)
	TOP_U8,
	FLAG_AFTER_1, // At 1, 1 bit.
	FLAG_AFTER_2, // At 2, 1 bit.
	FLAG_AFTER_8, // At 8, 1 bit.
	FLAG_AFTER_9, // At 9, 1 bit.
	FLAG_AFTER_10, // At 10, 1 bit.
	FLAG_AFTER_11, // At 11, 1 bit.
	FLAG_AFTER_16, // At 16, 1 bit.
	FLAG_AFTER_17, // At 17, 1 bit.
	FLAG_AFTER_18, // At 18, 1 bit.
	FLAG_AFTER_19, // At 19, 1 bit.
	FLAG_AFTER_20, // At 20, 1 bit.
	FLAG_AFTER_21, // At 21, 1 bit.
	FLAG_AFTER_22, // At 22, 1 bit.
	FLAG_AFTER_23, // At 23, 1 bit.
	U2_AFTER_8, // At 8, 2 bits.
	U3_AFTER_16, // At 16, 3 bits.
	U12_AFTER_4, // At 4, 12 bits.
	PRIM_TYPE, // At 16, 3 bits.
	SIGNAL_TYPE, // At 16, 8 bits.
	VTYPE_TC, // At 0, 2 bits.
	VTYPE_COL, // At 2, 3 bits.
	VTYPE_NRM, // At 5, 2 bits.
	VTYPE_POS, // At 7, 2 bits.
	VTYPE_WEIGHTTYPE, // At 9, 2 bits.
	VTYPE_INDEX, // At 11, 2 bits.
	VTYPE_WEIGHTCOUNT, // At 14, 3 bits.
	VTYPE_MORPHCOUNT, // At 18, 3 bits.
	PATCH_PRIM_TYPE, // At 0, 2 bits.
	LIGHT_COMP, // At 0, 2 bits.
	LIGHT_TYPE, // At 8, 2 bits.
	LIGHT_TYPE_SPECULAR, // 1 if comp is 1 (but not 3.)
	HIGH_ADDR, // At 16, 8 bits moved left to top 8 bits.
	TEX_W, // At 0, 4 bits - 1 to this power.
	TEX_H, // At 8, 4 bits - 1 to this power.
	UVGEN_TYPE, // At 0, 2 bits.
	UVGEN_PROJ, // At 8, 2 bits.
	TEX_FORMAT, // At 0, 4 bits.
	TEX_MINFILTER, // At 0, 3 bits.
	TEX_MAGFILTER, // At 8, 1 bit.
	TEX_LEVEL_MODE, // At 0, 2 bits.
	LOW_U12_4_FLOAT, // At 0, 12.4 converted to float.
	HIGH_S4_4_FLOAT, // At 16, s.3.4 converted to float.
	TEX_FUNC, // At 0, 3 bits.
	CLUT_BYTES, // At 0, 6 bits, multiplied by 8.
	CLUT_FORMAT, // At 0, 2 bits.
	CLUT_SHIFT, // At 2, 5 bits.
	CLUT_OFFSET, // At 16, 5 bits, multiplied by 16.
	COMPARE_FUNC2, // At 0, 2 bits.
	COMPARE_FUNC3, // At 0, 3 bits.
	STENCIL_OP_AT_0, // At 0, 3 bits.
	STENCIL_OP_AT_8, // At 8, 3 bits.
	STENCIL_OP_AT_16, // At 16, 3 bits.
	BLEND_SRC, // At 0, 4 bits.
	BLEND_DST, // At 4, 4 bits.
	BLEND_EQUATION, // At 8, 3 bits.
	LOGIC_OP, // At 0, 4 bits.
};

struct FieldName {
	GECmdFormat fmt;
	GECmdField field;
	const char *name;
};

static constexpr FieldName fieldNames[] = {
	{ GECmdFormat::PRIM, GECmdField::LOW_U16, "count" },
	{ GECmdFormat::PRIM, GECmdField::PRIM_TYPE, "type" },
	{ GECmdFormat::BEZIER, GECmdField::LOW_U8, "ucount" },
	{ GECmdFormat::BEZIER, GECmdField::LOW_U8, "u" },
	{ GECmdFormat::BEZIER, GECmdField::MID_U8, "vcount" },
	{ GECmdFormat::BEZIER, GECmdField::MID_U8, "v" },
	{ GECmdFormat::SPLINE, GECmdField::LOW_U8, "ucount" },
	{ GECmdFormat::SPLINE, GECmdField::LOW_U8, "u" },
	{ GECmdFormat::SPLINE, GECmdField::MID_U8, "vcount" },
	{ GECmdFormat::SPLINE, GECmdField::MID_U8, "v" },
	{ GECmdFormat::SPLINE, GECmdField::FLAG_AFTER_16, "ufirstopen" },
	{ GECmdFormat::SPLINE, GECmdField::FLAG_AFTER_17, "ulastopen" },
	{ GECmdFormat::SPLINE, GECmdField::FLAG_AFTER_18, "vfirstopen" },
	{ GECmdFormat::SPLINE, GECmdField::FLAG_AFTER_19, "vlastopen" },
	{ GECmdFormat::SIGNAL, GECmdField::LOW_U16, "data" },
	{ GECmdFormat::SIGNAL, GECmdField::SIGNAL_TYPE, "type" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_TC, "texcoord" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_TC, "tc" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_COL, "col" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_COL, "color" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_NRM, "normal" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_POS, "pos" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_POS, "position" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_WEIGHTTYPE, "weighttype" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_WEIGHTTYPE, "weight" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_INDEX, "index" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_WEIGHTCOUNT, "weightcount" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::VTYPE_MORPHCOUNT, "morphcount" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::FLAG_AFTER_23, "through" },
	{ GECmdFormat::VERTEX_TYPE, GECmdField::FLAG_AFTER_23, "throughmode" },
	{ GECmdFormat::X10_Y10, GECmdField::LOW_U10, "x" },
	{ GECmdFormat::X10_Y10, GECmdField::MID_U10, "y" },
	{ GECmdFormat::X10_Y10, GECmdField::LOW_U10_P1, "w" },
	{ GECmdFormat::X10_Y10, GECmdField::MID_U10_P1, "h" },
	{ GECmdFormat::FLAG, GECmdField::LOW_FLAG, "flag" },
	{ GECmdFormat::BONE_NUM, GECmdField::LOW_U7, "num" },
	{ GECmdFormat::MATRIX_NUM, GECmdField::LOW_U4, "num" },
	{ GECmdFormat::FLOAT, GECmdField::DATA, "data" },
	{ GECmdFormat::PATCH_DIVISION, GECmdField::LOW_U8, "u" },
	{ GECmdFormat::PATCH_DIVISION, GECmdField::MID_U8, "v" },
	{ GECmdFormat::PATCH_PRIM, GECmdField::PATCH_PRIM_TYPE, "type" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::LOW_U4, "frac" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::LOW_U4, "sub" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::LOW_U4, "subpixels" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::U12_AFTER_4, "int" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::U12_AFTER_4, "integer" },
	{ GECmdFormat::SUBPIXEL_COORD, GECmdField::LOW_U12_4_FLOAT, "pixels" },
	{ GECmdFormat::MATERIAL_UPDATE, GECmdField::LOW_FLAG, "ambient" },
	{ GECmdFormat::MATERIAL_UPDATE, GECmdField::FLAG_AFTER_1, "diffuse" },
	{ GECmdFormat::MATERIAL_UPDATE, GECmdField::FLAG_AFTER_2, "specular" },
	{ GECmdFormat::RGB, GECmdField::LOW_U8, "r" },
	{ GECmdFormat::RGB, GECmdField::LOW_U8, "red" },
	{ GECmdFormat::RGB, GECmdField::MID_U8, "g" },
	{ GECmdFormat::RGB, GECmdField::MID_U8, "green" },
	{ GECmdFormat::RGB, GECmdField::TOP_U8, "b" },
	{ GECmdFormat::RGB, GECmdField::TOP_U8, "blue" },
	{ GECmdFormat::LIGHT_TYPE, GECmdField::LIGHT_COMP, "computation" },
	{ GECmdFormat::LIGHT_TYPE, GECmdField::LIGHT_TYPE, "type" },
	{ GECmdFormat::LIGHT_TYPE, GECmdField::LIGHT_TYPE_SPECULAR, "specular" },
	{ GECmdFormat::STRIDE, GECmdField::LOW_U11, "stride" },
	{ GECmdFormat::STRIDE_HIGH_ADDR, GECmdField::HIGH_ADDR, "highaddr" },
	{ GECmdFormat::HIGH_ADDR, GECmdField::HIGH_ADDR, "highaddr" },
	{ GECmdFormat::HIGH_ADDR_ONLY, GECmdField::HIGH_ADDR, "highaddr" },
	{ GECmdFormat::TEX_SIZE, GECmdField::TEX_W, "w" },
	{ GECmdFormat::TEX_SIZE, GECmdField::TEX_W, "width" },
	{ GECmdFormat::TEX_SIZE, GECmdField::TEX_H, "h" },
	{ GECmdFormat::TEX_SIZE, GECmdField::TEX_H, "height" },
	{ GECmdFormat::TEX_MAP_MODE, GECmdField::UVGEN_TYPE, "type" },
	{ GECmdFormat::TEX_MAP_MODE, GECmdField::UVGEN_TYPE, "uv" },
	{ GECmdFormat::TEX_MAP_MODE, GECmdField::UVGEN_PROJ, "proj" },
	{ GECmdFormat::TEX_MAP_MODE, GECmdField::UVGEN_PROJ, "factor" },
	{ GECmdFormat::TEX_LIGHT_SRC, GECmdField::LOW_U2, "u" },
	{ GECmdFormat::TEX_LIGHT_SRC, GECmdField::U2_AFTER_8, "v" },
	{ GECmdFormat::TEX_MODE, GECmdField::LOW_FLAG, "swizzle" },
	{ GECmdFormat::TEX_MODE, GECmdField::FLAG_AFTER_8, "separateclut" },
	{ GECmdFormat::TEX_MODE, GECmdField::FLAG_AFTER_8, "separate" },
	{ GECmdFormat::TEX_MODE, GECmdField::U3_AFTER_16, "maxlevel" },
	{ GECmdFormat::TEX_MODE, GECmdField::U3_AFTER_16, "level" },
	{ GECmdFormat::TEX_FORMAT, GECmdField::TEX_FORMAT, "format" },
	{ GECmdFormat::TEX_FORMAT, GECmdField::TEX_FORMAT, "fmt" },
	{ GECmdFormat::TEX_FORMAT, GECmdField::FLAG_AFTER_2, "indexed" },
	{ GECmdFormat::TEX_FILTER, GECmdField::TEX_MINFILTER, "min" },
	{ GECmdFormat::TEX_FILTER, GECmdField::TEX_MINFILTER, "minify" },
	{ GECmdFormat::TEX_FILTER, GECmdField::TEX_MAGFILTER, "mag" },
	{ GECmdFormat::TEX_FILTER, GECmdField::TEX_MAGFILTER, "magnify" },
	{ GECmdFormat::TEX_CLAMP, GECmdField::LOW_FLAG, "s" },
	{ GECmdFormat::TEX_CLAMP, GECmdField::FLAG_AFTER_8, "t" },
	{ GECmdFormat::TEX_LEVEL_MODE, GECmdField::TEX_LEVEL_MODE, "mode" },
	{ GECmdFormat::TEX_LEVEL_MODE, GECmdField::HIGH_S4_4_FLOAT, "bias" },
	{ GECmdFormat::TEX_FUNC, GECmdField::TEX_FUNC, "func" },
	{ GECmdFormat::TEX_FUNC, GECmdField::TEX_FUNC, "function" },
	{ GECmdFormat::TEX_FUNC, GECmdField::FLAG_AFTER_8, "alpha" },
	{ GECmdFormat::TEX_FUNC, GECmdField::FLAG_AFTER_8, "a" },
	{ GECmdFormat::TEX_FUNC, GECmdField::FLAG_AFTER_16, "double" },
	{ GECmdFormat::TEX_FUNC, GECmdField::FLAG_AFTER_16, "doubling" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::CLUT_FORMAT, "format" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::CLUT_FORMAT, "fmt" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::CLUT_SHIFT, "shift" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::MID_U8, "mask" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::CLUT_OFFSET, "offset" },
	{ GECmdFormat::CLUT_FORMAT, GECmdField::CLUT_OFFSET, "base" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::LOW_FLAG, "on" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::LOW_FLAG, "enable" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::LOW_FLAG, "flag" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::FLAG_AFTER_8, "color" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::FLAG_AFTER_9, "alpha" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::FLAG_AFTER_9, "stencil" },
	{ GECmdFormat::CLEAR_MODE, GECmdField::FLAG_AFTER_10, "depth" },
	{ GECmdFormat::COLOR_TEST_FUNC, GECmdField::COMPARE_FUNC2, "func" },
	{ GECmdFormat::ALPHA_TEST, GECmdField::COMPARE_FUNC3, "func" },
	{ GECmdFormat::ALPHA_TEST, GECmdField::MID_U8, "ref" },
	{ GECmdFormat::ALPHA_TEST, GECmdField::MID_U8, "reference" },
	{ GECmdFormat::ALPHA_TEST, GECmdField::TOP_U8, "mask" },
	{ GECmdFormat::STENCIL_OP, GECmdField::STENCIL_OP_AT_0, "sfail" },
	{ GECmdFormat::STENCIL_OP, GECmdField::STENCIL_OP_AT_8, "zfail" },
	{ GECmdFormat::STENCIL_OP, GECmdField::STENCIL_OP_AT_16, "zpass" },
	{ GECmdFormat::STENCIL_OP, GECmdField::STENCIL_OP_AT_16, "pass" },
	{ GECmdFormat::DEPTH_TEST_FUNC, GECmdField::COMPARE_FUNC3, "func" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_SRC, "src" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_SRC, "srcfactor" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_DST, "dst" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_DST, "dstfactor" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_DST, "dest" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_DST, "destfactor" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_EQUATION, "eq" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_EQUATION, "equation" },
	{ GECmdFormat::BLEND_MODE, GECmdField::BLEND_EQUATION, "mode" },
	{ GECmdFormat::LOGIC_OP, GECmdField::LOGIC_OP, "op" },
	{ GECmdFormat::LOGIC_OP, GECmdField::LOGIC_OP, "mode" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::LOW_U8, "alpha" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::PRIM_TYPE, "type" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::PRIM_TYPE, "prim" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_11, "antialias" },
	// TODO: Clip bits?
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_18, "shading" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_18, "gouraud" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_19, "cull" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_20, "cullccw" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_21, "tex" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_22, "fog" },
	{ GECmdFormat::ALPHA_PRIM, GECmdField::FLAG_AFTER_23, "dither" },
};

struct GECmdConstant {
	const char *name;
	uint32_t value;
};

// TODO: It would be nice if these were somehow typed...
static constexpr GECmdConstant constantNames[] = {
	{ "GE_PRIM_POINTS", GE_PRIM_POINTS },
	{ "GE_PRIM_LINES", GE_PRIM_LINES },
	{ "GE_PRIM_LINE_STRIP", GE_PRIM_LINE_STRIP },
	{ "GE_PRIM_TRIANGLES", GE_PRIM_TRIANGLES },
	{ "GE_PRIM_TRIANGLE_STRIP", GE_PRIM_TRIANGLE_STRIP },
	{ "GE_PRIM_TRIANGLE_FAN", GE_PRIM_TRIANGLE_FAN },
	{ "GE_PRIM_RECTANGLES", GE_PRIM_RECTANGLES },
	{ "GE_PRIM_KEEP_PREVIOUS", GE_PRIM_KEEP_PREVIOUS },
	{ "POINTS", GE_PRIM_POINTS },
	{ "LINES", GE_PRIM_LINES },
	{ "LINE_STRIP", GE_PRIM_LINE_STRIP },
	{ "TRIANGLES", GE_PRIM_TRIANGLES },
	{ "TRIANGLE_STRIP", GE_PRIM_TRIANGLE_STRIP },
	{ "TRIANGLE_FAN", GE_PRIM_TRIANGLE_FAN },
	{ "RECTANGLES", GE_PRIM_RECTANGLES },
	{ "SPRITES", GE_PRIM_RECTANGLES },
	{ "KEEP_PREVIOUS", GE_PRIM_KEEP_PREVIOUS },
	{ "CONTINUE", GE_PRIM_KEEP_PREVIOUS },
	{ "GE_PATCHPRIM_TRIANGLES", GE_PATCHPRIM_TRIANGLES, },
	{ "GE_PATCHPRIM_LINES", GE_PATCHPRIM_LINES, },
	{ "GE_PATCHPRIM_POINTS", GE_PATCHPRIM_POINTS, },
	{ "GE_SIGNAL_NONE", PSP_GE_SIGNAL_NONE },
	{ "GE_SIGNAL_HANDLER_SUSPEND", PSP_GE_SIGNAL_HANDLER_SUSPEND },
	{ "GE_SIGNAL_HANDLER_CONTINUE", PSP_GE_SIGNAL_HANDLER_CONTINUE },
	{ "GE_SIGNAL_HANDLER_PAUSE", PSP_GE_SIGNAL_HANDLER_PAUSE },
	{ "GE_SIGNAL_SYNC", PSP_GE_SIGNAL_SYNC },
	{ "GE_SIGNAL_JUMP", PSP_GE_SIGNAL_JUMP },
	{ "GE_SIGNAL_CALL", PSP_GE_SIGNAL_CALL },
	{ "GE_SIGNAL_RET", PSP_GE_SIGNAL_RET },
	{ "GE_SIGNAL_RJUMP", PSP_GE_SIGNAL_RJUMP },
	{ "GE_SIGNAL_RCALL", PSP_GE_SIGNAL_RCALL },
	{ "GE_SIGNAL_OJUMP", PSP_GE_SIGNAL_OJUMP },
	{ "GE_SIGNAL_OCALL", PSP_GE_SIGNAL_OCALL },
	{ "GE_VTYPE_TC_NONE", GE_VTYPE_TC_NONE >> GE_VTYPE_TC_SHIFT },
	{ "GE_VTYPE_TC_8BIT", GE_VTYPE_TC_8BIT >> GE_VTYPE_TC_SHIFT },
	{ "GE_VTYPE_TC_16BIT", GE_VTYPE_TC_16BIT >> GE_VTYPE_TC_SHIFT },
	{ "GE_VTYPE_TC_FLOAT", GE_VTYPE_TC_FLOAT >> GE_VTYPE_TC_SHIFT },
	{ "GE_VTYPE_COL_NONE", GE_VTYPE_COL_NONE >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_COL_565", GE_VTYPE_COL_565 >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_COL_5650", GE_VTYPE_COL_565 >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_COL_5551", GE_VTYPE_COL_5551 >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_COL_4444", GE_VTYPE_COL_4444 >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_COL_8888", GE_VTYPE_COL_8888 >> GE_VTYPE_COL_SHIFT },
	{ "GE_VTYPE_NRM_NONE", GE_VTYPE_NRM_NONE >> GE_VTYPE_NRM_SHIFT },
	{ "GE_VTYPE_NRM_8BIT", GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT },
	{ "GE_VTYPE_NRM_16BIT", GE_VTYPE_NRM_16BIT >> GE_VTYPE_NRM_SHIFT },
	{ "GE_VTYPE_NRM_FLOAT", GE_VTYPE_NRM_FLOAT >> GE_VTYPE_NRM_SHIFT },
	{ "GE_VTYPE_POS_8BIT", GE_VTYPE_POS_8BIT >> GE_VTYPE_POS_SHIFT },
	{ "GE_VTYPE_POS_16BIT", GE_VTYPE_POS_16BIT >> GE_VTYPE_POS_SHIFT },
	{ "GE_VTYPE_POS_FLOAT", GE_VTYPE_POS_FLOAT >> GE_VTYPE_POS_SHIFT },
	{ "GE_VTYPE_WEIGHT_NONE", GE_VTYPE_WEIGHT_NONE >> GE_VTYPE_WEIGHT_SHIFT },
	{ "GE_VTYPE_WEIGHT_8BIT", GE_VTYPE_WEIGHT_8BIT >> GE_VTYPE_WEIGHT_SHIFT },
	{ "GE_VTYPE_WEIGHT_16BIT", GE_VTYPE_WEIGHT_16BIT >> GE_VTYPE_WEIGHT_SHIFT },
	{ "GE_VTYPE_WEIGHT_FLOAT", GE_VTYPE_WEIGHT_FLOAT >> GE_VTYPE_WEIGHT_SHIFT },
	{ "GE_VTYPE_IDX_NONE", GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT },
	{ "GE_VTYPE_IDX_8BIT", GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT },
	{ "GE_VTYPE_IDX_16BIT", GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT },
	{ "GE_VTYPE_IDX_32BIT", GE_VTYPE_IDX_32BIT >> GE_VTYPE_IDX_SHIFT },
	{ "GE_LIGHTCOMP_ONLYDIFFUSE", GE_LIGHTCOMP_ONLYDIFFUSE },
	{ "GE_LIGHTCOMP_BOTH", GE_LIGHTCOMP_BOTH },
	{ "GE_LIGHTCOMP_ONLYPOWDIFFUSE", GE_LIGHTCOMP_ONLYPOWDIFFUSE },
	{ "GE_LIGHTTYPE_DIRECTIONAL", GE_LIGHTTYPE_DIRECTIONAL },
	{ "GE_LIGHTTYPE_POINT", GE_LIGHTTYPE_POINT },
	{ "GE_LIGHTTYPE_SPOT", GE_LIGHTTYPE_SPOT },
	{ "GE_TEXMAP_TEXTURE_COORDS", GE_TEXMAP_TEXTURE_COORDS },
	{ "GE_TEXMAP_TEXTURE_MATRIX", GE_TEXMAP_TEXTURE_MATRIX },
	{ "GE_TEXMAP_ENVIRONMENT_MAP", GE_TEXMAP_ENVIRONMENT_MAP },
	{ "GE_PROJMAP_POSITION", GE_PROJMAP_POSITION },
	{ "GE_PROJMAP_UV", GE_PROJMAP_UV },
	{ "GE_PROJMAP_NORMALIZED_NORMAL", GE_PROJMAP_NORMALIZED_NORMAL },
	{ "GE_PROJMAP_NORMAL", GE_PROJMAP_NORMAL },
	{ "GE_TFMT_565", GE_TFMT_5650 },
	{ "GE_TFMT_5650", GE_TFMT_5650 },
	{ "GE_TFMT_5551", GE_TFMT_5551 },
	{ "GE_TFMT_4444", GE_TFMT_4444 },
	{ "GE_TFMT_8888", GE_TFMT_8888 },
	{ "GE_TFMT_CLUT4", GE_TFMT_CLUT4 },
	{ "GE_TFMT_CLUT8", GE_TFMT_CLUT8 },
	{ "GE_TFMT_CLUT16", GE_TFMT_CLUT16 },
	{ "GE_TFMT_CLUT32", GE_TFMT_CLUT32 },
	{ "GE_TFMT_DXT1", GE_TFMT_DXT1 },
	{ "GE_TFMT_DXT3", GE_TFMT_DXT3 },
	{ "GE_TFMT_DXT5", GE_TFMT_DXT5 },
	{ "GE_CMODE_16BIT_BGR5650", GE_CMODE_16BIT_BGR5650 },
	{ "GE_CMODE_16BIT_ABGR5551", GE_CMODE_16BIT_ABGR5551 },
	{ "GE_CMODE_16BIT_ABGR4444", GE_CMODE_16BIT_ABGR4444 },
	{ "GE_CMODE_32BIT_ABGR8888", GE_CMODE_32BIT_ABGR8888 },
	{ "GE_TFILT_NEAREST", GE_TFILT_NEAREST },
	{ "GE_TFILT_LINEAR", GE_TFILT_LINEAR },
	{ "GE_TFILT_NEAREST_MIPMAP_NEAREST", GE_TFILT_NEAREST_MIPMAP_NEAREST },
	{ "GE_TFILT_LINEAR_MIPMAP_NEAREST", GE_TFILT_LINEAR_MIPMAP_NEAREST },
	{ "GE_TFILT_NEAREST_MIPMAP_LINEAR", GE_TFILT_NEAREST_MIPMAP_LINEAR },
	{ "GE_TFILT_LINEAR_MIPMAP_LINEAR", GE_TFILT_LINEAR_MIPMAP_LINEAR },
	{ "NEAREST", GE_TFILT_NEAREST },
	{ "LINEAR", GE_TFILT_LINEAR },
	{ "GE_TEXLEVEL_MODE_AUTO", GE_TEXLEVEL_MODE_AUTO },
	{ "GE_TEXLEVEL_MODE_CONST", GE_TEXLEVEL_MODE_CONST },
	{ "GE_TEXLEVEL_MODE_SLOPE", GE_TEXLEVEL_MODE_SLOPE },
	{ "GE_TEXFUNC_MODULATE", GE_TEXFUNC_MODULATE },
	{ "GE_TEXFUNC_DECAL", GE_TEXFUNC_DECAL },
	{ "GE_TEXFUNC_BLEND", GE_TEXFUNC_BLEND },
	{ "GE_TEXFUNC_REPLACE", GE_TEXFUNC_REPLACE },
	{ "GE_TEXFUNC_ADD", GE_TEXFUNC_ADD },
	{ "GE_COMP_NEVER", GE_COMP_NEVER },
	{ "GE_COMP_ALWAYS", GE_COMP_ALWAYS },
	{ "GE_COMP_EQUAL", GE_COMP_EQUAL },
	{ "GE_COMP_NOTEQUAL", GE_COMP_NOTEQUAL },
	{ "GE_COMP_LESS", GE_COMP_LESS },
	{ "GE_COMP_LEQUAL", GE_COMP_LEQUAL },
	{ "GE_COMP_GREATER", GE_COMP_GREATER },
	{ "GE_COMP_GEQUAL", GE_COMP_GEQUAL },
	{ "NEVER", GE_COMP_NEVER },
	{ "ALWAYS", GE_COMP_ALWAYS },
	{ "EQUAL", GE_COMP_EQUAL },
	{ "NOTEQUAL", GE_COMP_NOTEQUAL },
	{ "LESS", GE_COMP_LESS },
	{ "LEQUAL", GE_COMP_LEQUAL },
	{ "LESSEQUAL", GE_COMP_LEQUAL },
	{ "GREATER", GE_COMP_GREATER },
	{ "GEQUAL", GE_COMP_GEQUAL },
	{ "GREATEREQUAL", GE_COMP_GEQUAL },
	{ "GE_STENCILOP_KEEP", GE_STENCILOP_KEEP },
	{ "GE_STENCILOP_ZERO", GE_STENCILOP_ZERO },
	{ "GE_STENCILOP_REPLACE", GE_STENCILOP_REPLACE },
	{ "GE_STENCILOP_INVERT", GE_STENCILOP_INVERT },
	{ "GE_STENCILOP_INCR", GE_STENCILOP_INCR },
	{ "GE_STENCILOP_DECR", GE_STENCILOP_DECR },
	{ "GE_BLENDMODE_MUL_AND_ADD", GE_BLENDMODE_MUL_AND_ADD },
	{ "GE_BLENDMODE_MUL_AND_SUBTRACT", GE_BLENDMODE_MUL_AND_SUBTRACT },
	{ "GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE", GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE },
	{ "GE_BLENDMODE_MIN", GE_BLENDMODE_MIN },
	{ "GE_BLENDMODE_MAX", GE_BLENDMODE_MAX },
	{ "GE_BLENDMODE_ABSDIFF", GE_BLENDMODE_ABSDIFF },
	{ "ADD", GE_BLENDMODE_MUL_AND_ADD },
	{ "SUBTRACT", GE_BLENDMODE_MUL_AND_SUBTRACT },
	{ "SUBTRACT_REVERSE", GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE },
	{ "MIN", GE_BLENDMODE_MIN },
	{ "MAX", GE_BLENDMODE_MAX },
	{ "ABSDIFF", GE_BLENDMODE_ABSDIFF },
	{ "GE_SRCBLEND_DSTCOLOR", GE_SRCBLEND_DSTCOLOR },
	{ "GE_SRCBLEND_INVDSTCOLOR", GE_SRCBLEND_INVDSTCOLOR },
	{ "GE_SRCBLEND_SRCALPHA", GE_SRCBLEND_SRCALPHA },
	{ "GE_SRCBLEND_INVSRCALPHA", GE_SRCBLEND_INVSRCALPHA },
	{ "GE_SRCBLEND_DSTALPHA", GE_SRCBLEND_DSTALPHA },
	{ "GE_SRCBLEND_INVDSTALPHA", GE_SRCBLEND_INVDSTALPHA },
	{ "GE_SRCBLEND_DOUBLESRCALPHA", GE_SRCBLEND_DOUBLESRCALPHA },
	{ "GE_SRCBLEND_DOUBLEINVSRCALPHA", GE_SRCBLEND_DOUBLEINVSRCALPHA },
	{ "GE_SRCBLEND_DOUBLEDSTALPHA", GE_SRCBLEND_DOUBLEDSTALPHA },
	{ "GE_SRCBLEND_DOUBLEINVDSTALPHA", GE_SRCBLEND_DOUBLEINVDSTALPHA },
	{ "GE_SRCBLEND_FIXA", GE_SRCBLEND_FIXA },
	{ "GE_DSTBLEND_SRCCOLOR", GE_DSTBLEND_SRCCOLOR },
	{ "GE_DSTBLEND_INVSRCCOLOR", GE_DSTBLEND_INVSRCCOLOR },
	{ "GE_DSTBLEND_SRCALPHA", GE_DSTBLEND_SRCALPHA },
	{ "GE_DSTBLEND_INVSRCALPHA", GE_DSTBLEND_INVSRCALPHA },
	{ "GE_DSTBLEND_DSTALPHA", GE_DSTBLEND_DSTALPHA },
	{ "GE_DSTBLEND_INVDSTALPHA", GE_DSTBLEND_INVDSTALPHA },
	{ "GE_DSTBLEND_DOUBLESRCALPHA", GE_DSTBLEND_DOUBLESRCALPHA },
	{ "GE_DSTBLEND_DOUBLEINVSRCALPHA", GE_DSTBLEND_DOUBLEINVSRCALPHA },
	{ "GE_DSTBLEND_DOUBLEDSTALPHA", GE_DSTBLEND_DOUBLEDSTALPHA },
	{ "GE_DSTBLEND_DOUBLEINVDSTALPHA", GE_DSTBLEND_DOUBLEINVDSTALPHA },
	{ "GE_DSTBLEND_FIXB", GE_DSTBLEND_FIXB },
	{ "SRCALPHA", GE_SRCBLEND_SRCALPHA },
	{ "INVSRCALPHA", GE_SRCBLEND_INVSRCALPHA },
	{ "INVERSESRCALPHA", GE_SRCBLEND_INVSRCALPHA },
	{ "DSTALPHA", GE_DSTBLEND_DSTALPHA },
	{ "INVDSTALPHA", GE_DSTBLEND_INVDSTALPHA },
	{ "DOUBLESRCALPHA", GE_DSTBLEND_DOUBLESRCALPHA },
	{ "DOUBLEINVSRCALPHA", GE_DSTBLEND_DOUBLEINVSRCALPHA },
	{ "DOUBLEDSTALPHA", GE_DSTBLEND_DOUBLEDSTALPHA },
	{ "DOUBLEINVDSTALPHA", GE_DSTBLEND_DOUBLEINVDSTALPHA },
	{ "FIXED", GE_DSTBLEND_FIXB },
	{ "GE_LOGIC_CLEAR", GE_LOGIC_CLEAR },
	{ "GE_LOGIC_AND", GE_LOGIC_AND },
	{ "GE_LOGIC_AND_REVERSE", GE_LOGIC_AND_REVERSE },
	{ "GE_LOGIC_COPY", GE_LOGIC_COPY },
	{ "GE_LOGIC_AND_INVERTED", GE_LOGIC_AND_INVERTED },
	{ "GE_LOGIC_NOOP", GE_LOGIC_NOOP },
	{ "GE_LOGIC_XOR", GE_LOGIC_XOR },
	{ "GE_LOGIC_OR", GE_LOGIC_OR },
	{ "GE_LOGIC_NOR", GE_LOGIC_NOR },
	{ "GE_LOGIC_EQUIV", GE_LOGIC_EQUIV },
	{ "GE_LOGIC_INVERTED", GE_LOGIC_INVERTED },
	{ "GE_LOGIC_OR_REVERSE", GE_LOGIC_OR_REVERSE },
	{ "GE_LOGIC_COPY_INVERTED", GE_LOGIC_COPY_INVERTED },
	{ "GE_LOGIC_OR_INVERTED", GE_LOGIC_OR_INVERTED },
	{ "GE_LOGIC_NAND", GE_LOGIC_NAND },
	{ "GE_LOGIC_SET", GE_LOGIC_SET },
};

class GEExpressionFunctions : public IExpressionFunctions {
public:
	GEExpressionFunctions(GPUDebugInterface *gpu) : gpu_(gpu) {}

	bool parseReference(char *str, uint32_t &referenceIndex) override;
	bool parseSymbol(char *str, uint32_t &symbolValue) override;
	uint32_t getReferenceValue(uint32_t referenceIndex) override;
	ExpressionType getReferenceType(uint32_t referenceIndex) override;
	bool getMemoryValue(uint32_t address, int size, uint32_t &dest, std::string *error) override;

private:
	bool parseFieldReference(const char *ref, const char *field, uint32_t &referenceIndex);
	static uint32_t getFieldValue(GECmdFormat fmt, GECmdField field, uint32_t value);
	static ExpressionType getFieldType(GECmdFormat fmt, GECmdField field);

	GPUDebugInterface *gpu_;
};

bool GEExpressionFunctions::parseReference(char *str, uint32_t &referenceIndex) {
	// TODO: Support formats and a form of fields (i.e. vtype.throughmode.)
	// For now, let's just support the register bits directly.
	GECmdInfo info;
	if (GECmdInfoByName(str, info)) {
		referenceIndex = info.reg;
		return true;
	}

	char *dot = strchr(str, '.');
	if (dot != nullptr) {
		*dot = '\0';
		bool success = parseFieldReference(str, dot + 1, referenceIndex);
		*dot = '.';
		if (success)
			return true;
	}

	// Also allow non-register references.
	for (const auto &entry : referenceNames) {
		if (strcasecmp(str, entry.name) == 0) {
			referenceIndex = (uint32_t)entry.index;
			return true;
		}
	}

	// And matrix data.  Maybe should allow column/row specification.
	int subindex = -1;
	int len = -1;

	if (sscanf(str, "bone%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 96) {
			referenceIndex = (uint32_t)GEReferenceIndex::BONE_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "world%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::WORLD_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "view%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::VIEW_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "proj%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 16) {
			referenceIndex = (uint32_t)GEReferenceIndex::PROJ_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "tgen%i%n", &subindex, &len) == 1 || sscanf(str, "texgen%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::TGEN_MATRIX + subindex;
			return true;
		}
	}

	return false;
}

bool GEExpressionFunctions::parseFieldReference(const char *ref, const char *field, uint32_t &referenceIndex) {
	GECmdInfo info;
	if (!GECmdInfoByName(ref, info)) {
		return false;
	}

	for (const auto &entry : fieldNames) {
		if (entry.fmt == info.fmt && strcasecmp(field, entry.name) == 0) {
			referenceIndex = (info.reg << 12) | (uint32_t)entry.field;
			return true;
		}
	}

	return false;
}

bool GEExpressionFunctions::parseSymbol(char *str, uint32_t &symbolValue) {
	// Mainly useful for checking memory addresses and constants.

	for (const auto &entry : constantNames) {
		if (strcasecmp(str, entry.name) == 0) {
			symbolValue = entry.value;
			return true;
		}
	}

	return g_symbolMap->GetLabelValue(str, symbolValue);
}

uint32_t GEExpressionFunctions::getReferenceValue(uint32_t referenceIndex) {
	GPUgstate state = gpu_->GetGState();
	if (referenceIndex < 0x100) {
		GECmdFormat fmt = GECmdInfoByCmd(GECommand(referenceIndex)).fmt;
		uint32_t value = state.cmdmem[referenceIndex];
		if (fmt == GECmdFormat::FLOAT)
			return value << 8;
		return value & 0x00FFFFFF;
	}

	if (referenceIndex >= (uint32_t)GEReferenceIndex::FIELD_START && referenceIndex <= (uint32_t)GEReferenceIndex::FIELD_END) {
		uint32_t value = state.cmdmem[referenceIndex >> 12] & 0x00FFFFFF;
		GECmdFormat fmt = GECmdInfoByCmd(GECommand(referenceIndex >> 12)).fmt;
		return getFieldValue(fmt, GECmdField(referenceIndex & 0xFF), value);
	}

	// We return the matrix value as float bits, which gets interpreted correctly in the parser.
	if (referenceIndex >= (uint32_t)GEReferenceIndex::BONE_MATRIX && referenceIndex < (uint32_t)GEReferenceIndex::MATRIX_END) {
		float value;
		if (referenceIndex >= (uint32_t)GEReferenceIndex::TGEN_MATRIX) {
			value = state.tgenMatrix[referenceIndex - (uint32_t)GEReferenceIndex::TGEN_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::PROJ_MATRIX) {
			value = state.projMatrix[referenceIndex - (uint32_t)GEReferenceIndex::PROJ_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::VIEW_MATRIX) {
			value = state.viewMatrix[referenceIndex - (uint32_t)GEReferenceIndex::VIEW_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::WORLD_MATRIX) {
			value = state.worldMatrix[referenceIndex - (uint32_t)GEReferenceIndex::WORLD_MATRIX];
		} else {
			value = state.boneMatrix[referenceIndex - (uint32_t)GEReferenceIndex::BONE_MATRIX];
		}

		uint32_t result;
		memcpy(&result, &value, sizeof(result));
		return result;
	}

	GEReferenceIndex ref = (GEReferenceIndex)referenceIndex;
	DisplayList list;
	switch (ref) {
	case GEReferenceIndex::VADDR:
		return gpu_->GetVertexAddress();
	case GEReferenceIndex::IADDR:
		return gpu_->GetIndexAddress();
	case GEReferenceIndex::OFFSET:
		// TODO: Should use an interface method, probably.
		return gstate_c.offsetAddr;
	case GEReferenceIndex::PC:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.pc;
		}
		return 0;
	case GEReferenceIndex::STALL:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.stall;
		}
		return 0;
	case GEReferenceIndex::BFLAG:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.bboxResult ? 1 : 0;
		}
		return 0;
	case GEReferenceIndex::OP:
		if (gpu_->GetCurrentDisplayList(list)) {
			return Memory::Read_U32(list.pc);
		}
		return 0;
	case GEReferenceIndex::DATA:
		if (gpu_->GetCurrentDisplayList(list)) {
			return Memory::Read_U32(list.pc) & 0x00FFFFFF;
		}
		return 0;

	case GEReferenceIndex::CLUTADDR:
		return state.getClutAddress();

	case GEReferenceIndex::TRANSFERSRC:
		return state.getTransferSrcAddress();

	case GEReferenceIndex::TRANSFERDST:
		return state.getTransferDstAddress();

	case GEReferenceIndex::PRIMCOUNT:
		return GPUDebug::PrimsThisFrame();

	case GEReferenceIndex::LASTPRIMCOUNT:
		return GPUDebug::PrimsLastFrame();

	case GEReferenceIndex::TEXADDR0:
	case GEReferenceIndex::TEXADDR1:
	case GEReferenceIndex::TEXADDR2:
	case GEReferenceIndex::TEXADDR3:
	case GEReferenceIndex::TEXADDR4:
	case GEReferenceIndex::TEXADDR5:
	case GEReferenceIndex::TEXADDR6:
	case GEReferenceIndex::TEXADDR7:
		return state.getTextureAddress((int)ref - (int)GEReferenceIndex::TEXADDR0);

	case GEReferenceIndex::BONE_MATRIX:
	case GEReferenceIndex::WORLD_MATRIX:
	case GEReferenceIndex::VIEW_MATRIX:
	case GEReferenceIndex::PROJ_MATRIX:
	case GEReferenceIndex::TGEN_MATRIX:
	case GEReferenceIndex::MATRIX_END:
	case GEReferenceIndex::FIELD_START:
	case GEReferenceIndex::FIELD_END:
		// Shouldn't have gotten here.
		break;
	}

	_assert_msg_(false, "Invalid reference index");
	return 0;
}

uint32_t GEExpressionFunctions::getFieldValue(GECmdFormat fmt, GECmdField field, uint32_t value) {
	switch (field) {
	case GECmdField::DATA:
		return value;
	case GECmdField::LOW_FLAG:
		return value & 1;
	case GECmdField::LOW_U2:
		return value & 3;
	case GECmdField::LOW_U4:
		return value & 0xF;
	case GECmdField::LOW_U7:
		return value & 0x7F;
	case GECmdField::LOW_U8:
		return value & 0xFF;
	case GECmdField::LOW_U10:
		return value & 0x03FF;
	case GECmdField::LOW_U10_P1:
		return (value & 0x03FF) + 1;
	case GECmdField::LOW_U11:
		return value & 0x07FF;
	case GECmdField::LOW_U16:
		return value & 0xFFFF;
	case GECmdField::MID_U8:
		return (value >> 8) & 0xFF;
	case GECmdField::MID_U10:
		return (value >> 10) & 0x03FF;
	case GECmdField::MID_U10_P1:
		return ((value >> 10) & 0x03FF) + 1;
	case GECmdField::TOP_U8:
		return (value >> 16) & 0xFF;
	case GECmdField::FLAG_AFTER_1:
		return (value >> 1) & 1;
	case GECmdField::FLAG_AFTER_2:
		return (value >> 2) & 1;
	case GECmdField::FLAG_AFTER_8:
		return (value >> 8) & 1;
	case GECmdField::FLAG_AFTER_9:
		return (value >> 9) & 1;
	case GECmdField::FLAG_AFTER_10:
		return (value >> 10) & 1;
	case GECmdField::FLAG_AFTER_11:
		return (value >> 11) & 1;
	case GECmdField::FLAG_AFTER_16:
		return (value >> 16) & 1;
	case GECmdField::FLAG_AFTER_17:
		return (value >> 17) & 1;
	case GECmdField::FLAG_AFTER_18:
		return (value >> 18) & 1;
	case GECmdField::FLAG_AFTER_19:
		return (value >> 19) & 1;
	case GECmdField::FLAG_AFTER_20:
		return (value >> 20) & 1;
	case GECmdField::FLAG_AFTER_21:
		return (value >> 21) & 1;
	case GECmdField::FLAG_AFTER_22:
		return (value >> 22) & 1;
	case GECmdField::FLAG_AFTER_23:
		return (value >> 23) & 1;
	case GECmdField::U2_AFTER_8:
		return (value >> 8) & 3;
	case GECmdField::U3_AFTER_16:
		return (value >> 16) & 7;
	case GECmdField::U12_AFTER_4:
		return (value >> 4) & 0x0FFF;

	// Below here are "typed" values, maybe they'll be exposed differently than integers someday.

	case GECmdField::PRIM_TYPE:
		return (value >> 16) & 7;
	case GECmdField::SIGNAL_TYPE:
		return (value >> 16) & 0xFF;
	case GECmdField::VTYPE_TC:
		return value & 3;
	case GECmdField::VTYPE_COL:
		return (value >> 2) & 7;
	case GECmdField::VTYPE_NRM:
		return (value >> 5) & 3;
	case GECmdField::VTYPE_POS:
		return (value >> 7) & 3;
	case GECmdField::VTYPE_WEIGHTTYPE:
		return (value >> 9) & 3;
	case GECmdField::VTYPE_INDEX:
		return (value >> 11) & 3;
	case GECmdField::VTYPE_WEIGHTCOUNT:
		return ((value >> 14) & 7) + 1;
	case GECmdField::VTYPE_MORPHCOUNT:
		return ((value >> 18) & 7) + 1;
	case GECmdField::PATCH_PRIM_TYPE:
		return value & 3;
	case GECmdField::LIGHT_COMP:
		return value & 3;
	case GECmdField::LIGHT_TYPE:
		return (value >> 8) & 3;
	case GECmdField::LIGHT_TYPE_SPECULAR:
		return (value & 3) == 1;
	case GECmdField::HIGH_ADDR:
		return (value << 8) & 0xFF000000;
	case GECmdField::TEX_W:
		return 1 << (value & 0xF);
	case GECmdField::TEX_H:
		return 1 << ((value >> 8) & 0xF);
	case GECmdField::UVGEN_TYPE:
		return value & 3;
	case GECmdField::UVGEN_PROJ:
		return (value >> 8) & 3;
	case GECmdField::TEX_FORMAT:
		return value & 0xF;
	case GECmdField::TEX_MINFILTER:
		return value & 7;
	case GECmdField::TEX_MAGFILTER:
		return (value >> 8) & 1;
	case GECmdField::TEX_LEVEL_MODE:
		return value & 3;
	case GECmdField::LOW_U12_4_FLOAT:
	{
		float f = (value & 0xFFFF) / 16.0f;
		uint32_t result;
		memcpy(&result, &f, sizeof(result));
		return result;
	}
	case GECmdField::HIGH_S4_4_FLOAT: // At 16, s.3.4 converted to float.
	{
		int value8 = (int)(s8)((value >> 16) & 0xFF);
		float f = value8 / 16.0f;
		uint32_t result;
		memcpy(&result, &f, sizeof(result));
		return result;
	}
	case GECmdField::TEX_FUNC:
		return value & 7;
	case GECmdField::CLUT_BYTES:
		return (value & 0x3F) * 8;
	case GECmdField::CLUT_FORMAT:
		return value & 3;
	case GECmdField::CLUT_SHIFT:
		return (value >> 2) & 0x1F;
	case GECmdField::CLUT_OFFSET:
		return ((value >> 16) & 0x1F) << 4;
	case GECmdField::COMPARE_FUNC2:
		return value & 3;
	case GECmdField::COMPARE_FUNC3:
		return value & 7;
	case GECmdField::STENCIL_OP_AT_0:
		return value & 7;
	case GECmdField::STENCIL_OP_AT_8:
		return (value >> 8) & 7;
	case GECmdField::STENCIL_OP_AT_16:
		return (value >> 16) & 7;
	case GECmdField::BLEND_SRC:
		return value & 0xF;
	case GECmdField::BLEND_DST:
		return (value >> 4) & 0xF;
	case GECmdField::BLEND_EQUATION:
		return (value >> 8) & 7;
	case GECmdField::LOGIC_OP:
		return value & 0xF;
	}

	_assert_msg_(false, "Invalid field type");
	return 0;
}

ExpressionType GEExpressionFunctions::getReferenceType(uint32_t referenceIndex) {
	if (referenceIndex < 0x100) {
		GECmdFormat fmt = GECmdInfoByCmd(GECommand(referenceIndex)).fmt;
		if (fmt == GECmdFormat::FLOAT)
			return EXPR_TYPE_FLOAT;
		return EXPR_TYPE_UINT;
	}

	if (referenceIndex >= (uint32_t)GEReferenceIndex::FIELD_START && referenceIndex <= (uint32_t)GEReferenceIndex::FIELD_END) {
		GECmdFormat fmt = GECmdInfoByCmd(GECommand(referenceIndex >> 12)).fmt;
		return getFieldType(fmt, GECmdField(referenceIndex & 0xFF));
	}

	if (referenceIndex >= (uint32_t)GEReferenceIndex::BONE_MATRIX && referenceIndex < (uint32_t)GEReferenceIndex::MATRIX_END)
		return EXPR_TYPE_FLOAT;
	return EXPR_TYPE_UINT;
}

ExpressionType GEExpressionFunctions::getFieldType(GECmdFormat fmt, GECmdField field) {
	switch (field) {
	case GECmdField::LOW_U12_4_FLOAT:
	case GECmdField::HIGH_S4_4_FLOAT:
		return EXPR_TYPE_FLOAT;

	default:
		// Almost all of these are uint, so not listing each one.
		break;
	}

	return EXPR_TYPE_UINT;
}

bool GEExpressionFunctions::getMemoryValue(uint32_t address, int size, uint32_t &dest, std::string *error) {
	// We allow, but ignore, bad access.
	// If we didn't, log/condition statements that reference registers couldn't be configured.
	uint32_t valid = Memory::ValidSize(address, size);
	uint8_t buf[4]{};
	if (valid != 0)
		memcpy(buf, Memory::GetPointerUnchecked(address), valid);

	switch (size) {
	case 1:
		dest = buf[0];
		return true;
	case 2:
		dest = (buf[1] << 8) | buf[0];
		return true;
	case 4:
		dest = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		return true;
	}

	*error = StringFromFormat("Unexpected memory access size %d", size);
	return false;
}

bool GPUDebugInitExpression(GPUDebugInterface *g, const char *str, PostfixExpression &exp) {
	GEExpressionFunctions funcs(g);
	return initPostfixExpression(str, &funcs, exp);
}

bool GPUDebugExecExpression(GPUDebugInterface *g, PostfixExpression &exp, uint32_t &result) {
	GEExpressionFunctions funcs(g);
	return parsePostfixExpression(exp, &funcs, result);
}

bool GPUDebugExecExpression(GPUDebugInterface *g, const char *str, uint32_t &result) {
	GEExpressionFunctions funcs(g);
	return parseExpression(str, &funcs, result);
}

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GEBufferFormat fmt, bool flipped, bool reversed) {
	GPUDebugBufferFormat actualFmt = GPUDebugBufferFormat(fmt);
	if (reversed && actualFmt < GPU_DBG_FORMAT_8888) {
		actualFmt |= GPU_DBG_FORMAT_REVERSE_FLAG;
	}
	Allocate(stride, height, actualFmt, flipped);
}

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GPUDebugBufferFormat fmt, bool flipped) {
	if (alloc_ && stride_ == stride && height_ == height && fmt_ == fmt) {
		// Already allocated the right size.
		flipped_ = flipped;
		return;
	}

	Free();
	alloc_ = true;
	height_ = height;
	stride_ = stride;
	fmt_ = fmt;
	flipped_ = flipped;

	u32 pixelSize = PixelSize();
	data_ = new u8[pixelSize * stride * height];
}

void GPUDebugBuffer::Free() {
	if (alloc_ && data_ != NULL) {
		delete [] data_;
	}
	data_ = NULL;
}

void GPUDebugBuffer::ZeroBytes() {
	_dbg_assert_(data_);
	memset(data_, 0, PixelSize() * stride_ * height_);
}

u32 GPUDebugBuffer::PixelSize() const {
	switch (fmt_) {
	case GPU_DBG_FORMAT_8888:
	case GPU_DBG_FORMAT_8888_BGRA:
	case GPU_DBG_FORMAT_FLOAT:
	case GPU_DBG_FORMAT_24BIT_8X:
	case GPU_DBG_FORMAT_24X_8BIT:
	case GPU_DBG_FORMAT_FLOAT_DIV_256:
	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
		return 4;

	case GPU_DBG_FORMAT_888_RGB:
		return 3;

	case GPU_DBG_FORMAT_8BIT:
		return 1;

	default:
		return 2;
	}
}

u32 GPUDebugBuffer::GetRawPixel(int x, int y) const {
	if (data_ == nullptr) {
		return 0;
	}

	if (flipped_) {
		y = height_ - y - 1;
	}

	u32 pixelSize = PixelSize();
	u32 byteOffset = pixelSize * (stride_ * y + x);
	const u8 *ptr = &data_[byteOffset];

	switch (pixelSize) {
	case 4:
		return *(const u32 *)ptr;
	case 3:
		return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
	case 2:
		return *(const u16 *)ptr;
	case 1:
		return *(const u8 *)ptr;
	default:
		return 0;
	}
}

void GPUDebugBuffer::SetRawPixel(int x, int y, u32 c) {
	if (data_ == nullptr) {
		return;
	}

	if (flipped_) {
		y = height_ - y - 1;
	}

	u32 pixelSize = PixelSize();
	u32 byteOffset = pixelSize * (stride_ * y + x);
	u8 *ptr = &data_[byteOffset];

	switch (pixelSize) {
	case 4:
		*(u32 *)ptr = c;
		break;
	case 3:
		ptr[0] = (c >> 0) & 0xFF;
		ptr[1] = (c >> 8) & 0xFF;
		ptr[2] = (c >> 16) & 0xFF;
		break;
	case 2:
		*(u16 *)ptr = (u16)c;
		break;
	case 1:
		*ptr = (u8)c;
		break;
	default:
		break;
	}
}
