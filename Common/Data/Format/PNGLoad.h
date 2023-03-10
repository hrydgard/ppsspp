#pragma once

#include <cstdint>

#include "Common/BitSet.h"

// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, 
            int *pheight, unsigned char **image_data_ptr);

int pngLoadPtr(const unsigned  char *input_ptr, size_t input_len, int *pwidth,
            int *pheight, unsigned char **image_data_ptr);

// PNG peeker - just read the start of a PNG straight into this struct, in order to
// look at basic parameters like width and height. Note that while PNG is a chunk-based
// format, the IHDR chunk is REQUIRED to be the first one, so this will work.
// Does not handle Apple's weirdo extension CgBI. http://iphonedevwiki.net/index.php/CgBI_file_format
// That should not be an issue.
struct PNGHeaderPeek {
	uint32_t magic;
	uint32_t ignore0;
	uint32_t ignore1;
	uint32_t ihdrTag;
	uint32_t be_width;  // big endian
	uint32_t be_height;
	uint8_t bitDepth;   // bits per channel, can be 1, 2, 4, 8, 16
	uint8_t colorType;  // really, pixel format. 0 = grayscale, 2 = rgb, 3 = palette index, 4 = gray+alpha, 6 = rgba

	bool IsValidPNGHeader() const;
	int Width() const { return swap32(be_width); }
	int Height() const { return swap32(be_height); }
};
