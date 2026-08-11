#pragma once

#include <string_view>

#include <cstdint>

#include "Common/CommonTypes.h"

#include "GPU/Debugger/GECommandTable.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/GPUCommon.h"

// TODO: Add back a true raw vertex list?

enum class VertexListDecodedCol {
	X,
	Y,
	Z,
	U,
	V,
	COLOR,
	NX,
	NY,
	NZ,
	W0,
	W1,
	W2,
	W3,
	W4,
	W5,
	W6,
	W7,
	COUNT,
};
extern const char *const g_vertexListDecodedColNames[];

enum class VertexListTransformedCol {
	X,
	Y,
	Z,
	W,
	U,
	V,
	COLOR0,
	COLOR1,
	FOG,
	COUNT,
};

// Indexed by the above enum;
extern const char *const g_vertexListTransformedColNames[];

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

void FormatStateRow(char *dest, size_t destSize, CmdFormatType fmt, u32 value, bool enabled, u32 otherValue, u32 otherValue2);

void FormatVertColDecoded(char *dest, size_t destSize, const GPUDebugVertex &vert, VertexListDecodedCol col);
void FormatVertColTransformed(char *dest, size_t destSize, const GPUDebugVertex &vert, VertexListTransformedCol col);

// These are utilities used by the debugger vertex preview.
bool GetPrimPreview(u32 op, GEPrimitiveType *prim, std::vector<GPUDebugVertex> *vertices, std::vector<u16> *indices, int *lowerIndexBound, bool transformed);
void DescribePixel(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
void DescribePixelRGBA(u32 pix, GPUDebugBufferFormat fmt, int x, int y, char desc[256]);
