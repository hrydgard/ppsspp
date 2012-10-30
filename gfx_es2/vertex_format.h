#pragma once

// TODO: Actually use for something :)

#include "base/basictypes.h"

// Vertex format flags
enum VtxFmt {
	POS_FLOAT   = 1,
	POS_FLOAT16 = 2,
	POS_UINT16  = 3,
	POS_UINT8   = 4,
	POS_101010  = 5,

	NRM_FLOAT   = 1 << 4,
	NRM_FLOAT16 = 2 << 4,
	NRM_SINT16  = 3 << 4,
	NRM_UINT8   = 4 << 4,
	NRM_101010  = 5 << 4,

	TANGENT_FLOAT = 1 << 8,
	//....

	UV0_NONE    = 1 << 12,
	UV0_FLOAT   = 1 << 12,
	// ....
	UV1_NONE    = 1 << 16,
	UV1_FLOAT   = 1 << 16,

	RGBA_NONE    = 0 << 20,
	RGBA_FLOAT   = 1 << 20,
	RGBA_FLOAT16 = 2 << 20,
	RGBA_UINT16  = 3 << 20,
	RGBA_UINT8   = 4 << 20,
	RGBA_101010  = 5 << 20,

	POS_MASK     = 0x0000000F,
	NRM_MASK     = 0x000000F0,
	TANGENT_MASK = 0x00000F00,
	UV0_MASK     = 0x0000F000,
	UV1_MASK     = 0x000F0000,
	RGBA_MASK    = 0x00F00000,

	// Can add more here, such as a generic AUX or something. Don't know what to use it for though. Hardness for cloth sim?
};

struct GLSLProgram;

// When calling this, the relevant vertex buffer must be bound to GL_ARRAY_BUFFER.
void SetVertexFormat(const GLSLProgram *program, uint32_t format);
void UnsetVertexFormat(const GLSLProgram *program, uint32_t format);
