#pragma once

#include <cstdio>
#include <cstdint>

// For the type enums etc.
#include "Common/Data/Format/ZIMLoad.h"

// SaveZIM's responsibility:
// * Write the ZIM format
// * Generate mipmaps if requested
// * Convert images to the requested format
// Input image is always 8888 RGBA. SaveZIM takes care of downsampling and mipmap generation.
void SaveZIM(FILE *f, int width, int height, int pitch, int format, const uint8_t *image, int compressLevel = 0);
