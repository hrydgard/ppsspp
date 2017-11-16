#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USING_QT_UI
#include <QtGui/QImage>
#else
#include "libpng17/png.h"
#endif

#include "png_load.h"
#include "base/logging.h"

// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, int *pheight, unsigned char **image_data_ptr, bool flip) {
#ifdef USING_QT_UI
	QImage image(file, "PNG");
	if (image.isNull()) {
		ELOG("pngLoad: Error loading image %s", file);
		return 0;
	}

	if (flip)
		image = image.mirrored();
	*pwidth = image.width();
	*pheight = image.height();
	image = image.convertToFormat(QImage::Format_ARGB32);
	*image_data_ptr = (unsigned char *)malloc(image.byteCount());
	uint32_t *src = (uint32_t*) image.bits();
	uint32_t *dest = (uint32_t*) *image_data_ptr;
        // Qt4 does not support RGBA 
	for (size_t sz = 0; sz < (size_t)image.byteCount(); sz+=4, ++src, ++dest) {
		const uint32_t v = *src;
		*dest = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | (( v >> 16 ) & 0xFF); // ARGB -> RGBA
	}
#else
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
#endif

	return 1;
}

int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr,
	bool flip) {
#ifdef USING_QT_UI
	QImage image;
	if (!image.loadFromData(input_ptr, input_len, "PNG")) {
		ELOG("pngLoad: Error loading image");
		return 0;
	}
	if (flip)
		image = image.mirrored();
	*pwidth = image.width();
	*pheight = image.height();
	image = image.convertToFormat(QImage::Format_ARGB32);
	*image_data_ptr = (unsigned char *)malloc(image.byteCount());
	uint32_t *src = (uint32_t*) image.bits();
	uint32_t *dest = (uint32_t*) *image_data_ptr;
	// Qt4 does not support RGBA
	for (size_t sz = 0; sz < (size_t)image.byteCount(); sz+=4, ++src, ++dest) {
		const uint32_t v = *src;
		*dest = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | (( v >> 16 ) & 0xFF); // convert it!
	}
#else
	if (flip)
		ELOG("pngLoad: flip flag not supported, image will be loaded upside down");
	png_image png{};
	png.version = PNG_IMAGE_VERSION;

	png_image_begin_read_from_memory(&png, input_ptr, input_len);

	if (PNG_IMAGE_FAILED(png)) {
		ELOG("pngLoad: %s", png.message);
		return 0;
	}
	*pwidth = png.width;
	*pheight = png.height;
	png.format = PNG_FORMAT_RGBA;

	int stride = PNG_IMAGE_ROW_STRIDE(png);

	size_t size = PNG_IMAGE_SIZE(png);
	if (!size) {
		ELOG("pngLoad: empty image");
		return 0;
	}

	*image_data_ptr = (unsigned char *)malloc(size);
	png_image_finish_read(&png, NULL, *image_data_ptr, stride, NULL);
#endif

	return 1;
}
