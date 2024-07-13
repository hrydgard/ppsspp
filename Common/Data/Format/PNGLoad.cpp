#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <png.h>

#include "Common/Data/Format/PNGLoad.h"
#include "Common/Log.h"

// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, int *pheight, unsigned char **image_data_ptr) {
	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;

	png_image_begin_read_from_file(&png, file);

	if (PNG_IMAGE_FAILED(png))
	{
		WARN_LOG(Log::IO, "pngLoad: %s (%s)", png.message, file);
		*image_data_ptr = nullptr;
		return 0;
	}
	*pwidth = png.width;
	*pheight = png.height;
	png.format = PNG_FORMAT_RGBA;

	int stride = PNG_IMAGE_ROW_STRIDE(png);
	*image_data_ptr = (unsigned char *)malloc(PNG_IMAGE_SIZE(png));
	png_image_finish_read(&png, NULL, *image_data_ptr, stride, NULL);
	return 1;
}

int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr) {
	png_image png{};
	png.version = PNG_IMAGE_VERSION;

	png_image_begin_read_from_memory(&png, input_ptr, input_len);

	if (PNG_IMAGE_FAILED(png)) {
		WARN_LOG(Log::IO, "pngLoad: %s", png.message);
		*image_data_ptr = nullptr;
		return 0;
	}
	*pwidth = png.width;
	*pheight = png.height;
	png.format = PNG_FORMAT_RGBA;

	int stride = PNG_IMAGE_ROW_STRIDE(png);

	size_t size = PNG_IMAGE_SIZE(png);
	if (!size) {
		ERROR_LOG(Log::IO, "pngLoad: empty image");
		*image_data_ptr = nullptr;
		return 0;
	}

	*image_data_ptr = (unsigned char *)malloc(size);
	png_image_finish_read(&png, NULL, *image_data_ptr, stride, NULL);
	return 1;
}

bool PNGHeaderPeek::IsValidPNGHeader() const {
	if (magic != 0x474e5089 || ihdrTag != 0x52444849) {
		return false;
	}
	// Reject crazy sized images, too.
	if (Width() > 32768 && Height() > 32768) {
		return false;
	}
	return true;
}
