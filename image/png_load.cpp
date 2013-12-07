#include <stdlib.h>
#include <string.h>
#include "png_load.h"
#include "base/logging.h"


#if 0
#include "ext/stb_image/stb_image.h"
// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, int *pheight, unsigned char **image_data_ptr,
	bool flip) {
	if (flip)
	{
		ELOG("pngLoad: flip flag not supported, image will be loaded upside down");
	}

	int x,y,n;
	unsigned char *data = stbi_load(file, &x, &y, &n, 4);	// 4 = force RGBA
	if (!data)
		return 0;

	*pwidth = x;
	*pheight = y;
	// ... process data if not NULL ...
	// ... x = width, y = height, n = # 8-bit components per pixel ...
	// ... replace '0' with '1'..'4' to force that many components per pixel
	// ... but 'n' will always be the number that it would have been if you said 0

	// TODO: Get rid of this silly copy which is only to make the buffer free-able with free()
	*image_data_ptr = (unsigned char *)malloc(x * y * 4);
	memcpy(*image_data_ptr, data, x * y * 4);
	stbi_image_free(data);
	return 1;
}

int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr,
	bool flip) {
	if (flip)
	{
		ELOG("pngLoad: flip flag not supported, image will be loaded upside down");
	}

	int x,y,n;
	unsigned char *data = stbi_load_from_memory(input_ptr,(int)input_len, &x, &y, &n, 4);	// 4 = force RGBA
	if (!data)
		return 0;

	*pwidth = x;
	*pheight = y;
	// ... process data if not NULL ...
	// ... x = width, y = height, n = # 8-bit components per pixel ...
	// ... replace '0' with '1'..'4' to force that many components per pixel
	// ... but 'n' will always be the number that it would have been if you said 0

	// TODO: Get rid of this silly copy which is only to make the buffer free-able with free()
	*image_data_ptr = (unsigned char *)malloc(x * y * 4);
	memcpy(*image_data_ptr, data, x * y * 4);
	stbi_image_free(data);
	return 1;
}

#else

#include <png.h>
#include <stdio.h>

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

#endif
