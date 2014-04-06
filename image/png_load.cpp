#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpng16/png.h"

#include "png_load.h"
#include "base/logging.h"

// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, int *pheight, unsigned char **image_data_ptr, bool flip) {
	if (flip)
		ELOG("pngLoad: flip flag not supported, image will be loaded upside down");
	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;

	png_image_begin_read_from_file(&png, file);

	if (PNG_IMAGE_FAILED(png))
	{
		ELOG("pngLoad: %s", png.message);
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

int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr,
	bool flip) {
	if (flip)
		ELOG("pngLoad: flip flag not supported, image will be loaded upside down");
	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;

	png_image_begin_read_from_memory(&png, input_ptr, input_len);

	if (PNG_IMAGE_FAILED(png))
	{
		ELOG("pngLoad: %s", png.message);
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
