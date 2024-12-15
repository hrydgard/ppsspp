#pragma once

#include <string_view>

#include <cstdint>

#include "Common/CommonTypes.h"

#include "GPU/Debugger/GECommandTable.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/GPUDebugInterface.h"

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

extern const GECommand g_stateFlagsRows[];
extern const GECommand g_stateLightingRows[];
extern const GECommand g_stateTextureRows[];
extern const GECommand g_stateSettingsRows[];
extern const size_t g_stateFlagsRowsSize;
extern const size_t g_stateLightingRowsSize;
extern const size_t g_stateTextureRowsSize;
extern const size_t g_stateSettingsRowsSize;

struct GPUDebugVertex;
class VertexDecoder;

void FormatStateRow(GPUDebugInterface *debug, char *dest, size_t destSize, CmdFormatType fmt, u32 value, bool enabled, u32 otherValue, u32 otherValue2);
void FormatVertCol(char *dest, size_t destSize, const GPUDebugVertex &vert, int col);
void FormatVertColRaw(VertexDecoder *decoder, char *dest, size_t destSize, int row, int col);

// These are utilities used by the debugger vertex preview.
bool GetPrimPreview(u32 op, GEPrimitiveType &prim, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices, int &count);
void DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
void DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
