#include "Common/Math/CrossSIMD.h"
#include "GPU/Common/DepthRaster.h"

// TODO: Should respect the scissor rect.

void DepthRasterRect(uint16_t *dest, int stride, int x1, int x2, int y1, int y2, short depthValue, GEComparison depthCompare) {
	__m128i valueX8 = _mm_set1_epi16(depthValue);

	for (int y = y1; y < y2; y++) {
		__m128i *ptr = (__m128i *)(dest + stride * y + x1);
		int w = x2 - x1;

		switch (depthCompare) {
		case GE_COMP_ALWAYS:
			while (w >= 8) {
				_mm_storeu_si128(ptr, valueX8);
				ptr++;
				w -= 8;
			}
			break;
			// TODO: Trailer
		case GE_COMP_NEVER:
			break;
		default:
			// TODO
			break;
		}
	}
}
