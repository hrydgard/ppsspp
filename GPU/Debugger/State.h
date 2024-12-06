#pragma once

#include <string_view>

#include <cstdint>

#include "Common/CommonTypes.h"

// Extracted from Windows/GE Debugger/TabState.cpp

enum CmdFormatType {
	CMD_FMT_HEX = 0,
	CMD_FMT_NUM,
	CMD_FMT_FLOAT24,
	CMD_FMT_PTRWIDTH,
	CMD_FMT_XY,
	CMD_FMT_XYXY,
	CMD_FMT_XYZ,
	CMD_FMT_XYPLUS1,
	CMD_FMT_TEXSIZE,
	CMD_FMT_F16_XY,
	CMD_FMT_VERTEXTYPE,
	CMD_FMT_TEXFMT,
	CMD_FMT_CLUTFMT,
	CMD_FMT_COLORTEST,
	CMD_FMT_ALPHATEST,
	CMD_FMT_STENCILTEST,
	CMD_FMT_ZTEST,
	CMD_FMT_OFFSETADDR,
	CMD_FMT_VADDR,
	CMD_FMT_IADDR,
	CMD_FMT_MATERIALUPDATE,
	CMD_FMT_STENCILOP,
	CMD_FMT_BLENDMODE,
	CMD_FMT_FLAG,
	CMD_FMT_CLEARMODE,
	CMD_FMT_TEXFUNC,
	CMD_FMT_TEXMODE,
	CMD_FMT_LOGICOP,
	CMD_FMT_TEXWRAP,
	CMD_FMT_TEXLEVEL,
	CMD_FMT_TEXFILTER,
	CMD_FMT_TEXMAPMODE,
	CMD_FMT_TEXSHADELS,
	CMD_FMT_SHADEMODEL,
	CMD_FMT_LIGHTMODE,
	CMD_FMT_LIGHTTYPE,
	CMD_FMT_CULL,
	CMD_FMT_PATCHPRIMITIVE,
};

enum VertexListCols {
	VERTEXLIST_COL_X,
	VERTEXLIST_COL_Y,
	VERTEXLIST_COL_Z,
	VERTEXLIST_COL_U,
	VERTEXLIST_COL_V,
	VERTEXLIST_COL_COLOR,
	VERTEXLIST_COL_NX,
	VERTEXLIST_COL_NY,
	VERTEXLIST_COL_NZ,
	VERTEXLIST_COL_COUNT,
};

class GPUDebugInterface;

struct TabStateRow {
	std::string_view title;
	uint8_t cmd;
	CmdFormatType fmt;
	uint8_t enableCmd;
	uint8_t otherCmd;
	uint8_t otherCmd2;
};

extern const TabStateRow g_stateFlagsRows[];
extern const TabStateRow g_stateLightingRows[];
extern const TabStateRow g_stateTextureRows[];
extern const TabStateRow g_stateSettingsRows[];
extern const size_t g_stateFlagsRowsSize;
extern const size_t g_stateLightingRowsSize;
extern const size_t g_stateTextureRowsSize;
extern const size_t g_stateSettingsRowsSize;

struct GPUDebugVertex;
class VertexDecoder;

void FormatStateRow(GPUDebugInterface *debug, char *dest, size_t destSize, CmdFormatType fmt, u32 value, bool enabled, u32 otherValue, u32 otherValue2);
void FormatVertCol(char *dest, size_t destSize, const GPUDebugVertex &vert, int col);
void FormatVertColRaw(VertexDecoder *decoder, char *dest, size_t destSize, int row, int col);
