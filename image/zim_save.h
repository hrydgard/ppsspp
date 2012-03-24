#pragma once

#include "base/basictypes.h"
#include "image/zim_load.h"

// SaveZIM's responsibility:
// * Write the ZIM format
// * Generate mipmaps if requested
// * Convert images to the requested format
// Input image is always 8888 RGBA. SaveZIM takes care of downsampling and mipmap generation.
void SaveZIM(const char *filename, int width, int height, int pitch, int format, const uint8_t *image);
