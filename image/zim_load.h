#pragma once

#include "base/basictypes.h"

// LoadZIM's responsibility:
// * Parse the ZIM format
// * Extract all mip levels so they can be uploaded to GPU
//
// * NOT convert formats to anything, except converting ETC1 to RGBA8888 when running on the PC

// ZIM format:
// 4 byte ZIMG
// 4 byte width
// 4 byte height
// 4 byte flags
// Uncompressed or ZLibbed data. If multiple mips, zlibbed separately.

// Defined flags:

enum {
	ZIM_RGBA8888 = 0,	// Assumed format if no other format is set
	ZIM_RGBA4444 = 1,	// GL_UNSIGNED_SHORT_4_4_4_4
	ZIM_RGB565 = 2,		// GL_UNSIGNED_SHORT_5_6_5
	ZIM_ETC1 = 3,
	ZIM_RGB888 = 4,
	ZIM_LUMINANCE_ALPHA = 5,
	ZIM_LUMINANCE = 6,
	ZIM_ALPHA = 7,
	// There's space for plenty more formats.
	ZIM_FORMAT_MASK = 15,
	ZIM_HAS_MIPS = 16,	// If set, assumes that a full mip chain is present. Mips are zlib-compressed individually and stored in sequence. Always half sized.
	ZIM_GEN_MIPS = 32,	// If set, the caller is advised to automatically generate mips. (maybe later, the ZIM lib will generate the mips for you).
	ZIM_DITHER = 64,	// If set, dithers during save if color reduction is necessary.
	ZIM_CLAMP = 128,	// Texture should default to clamp instead of wrap.
	ZIM_ZLIB_COMPRESSED = 256,
	ZIM_ETC1_LOW = 512,
	ZIM_ETC1_MEDIUM = 1024,
	ZIM_ETC1_HIGH = 0, // default
	ZIM_ETC1_DITHER = 2048,
};

// ZIM will only ever support up to 12 levels (4096x4096 max).
enum {
	ZIM_MAX_MIP_LEVELS = 12,
};

// Delete the returned pointer using free()
// Watch out! If the image has mipmaps, multiple values will be written
// to width, height, and image, as if they were arrays, up to 12 (max texture size is 4096 which is 2^12).
int LoadZIM(const char *filename, int *width, int *height, int *flags, uint8_t **image);
int LoadZIMPtr(char *zim, int datasize, int *width, int *height, int *flags, uint8_t **image);