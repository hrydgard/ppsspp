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

// Custom error handler
void pngErrorHandler(png_structp png_ptr, png_const_charp error_msg) {
	ERROR_LOG(Log::System, "libpng error: %s\n", error_msg);
	longjmp(png_jmpbuf(png_ptr), 1);
}

void pngWarningHandler(png_structp png_ptr, png_const_charp warning_msg) {
	DEBUG_LOG(Log::System, "libpng warning: %s\n", warning_msg);
}

int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr) {
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, pngErrorHandler, pngWarningHandler);
	if (!png) {
		return 0;
	}
	if (input_len == 0) {
		return 0;
	}

	// Ignore incorrect sRGB profiles
	png_set_option(png, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
	png_set_benign_errors(png, PNG_OPTION_ON);

	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		return 0;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, NULL);
		if (*image_data_ptr) {
			free(*image_data_ptr);
			*image_data_ptr = NULL;
		}
		return 0;
	}

	png_set_read_fn(png, (png_voidp)&input_ptr, [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
		const unsigned char **input = (const unsigned char **)png_get_io_ptr(png_ptr);
		memcpy(outBytes, *input, byteCountToRead);
		*input += byteCountToRead;
	});

	png_read_info(png, info);

	*pwidth = png_get_image_width(png, info);
	*pheight = png_get_image_height(png, info);

	const int color_type = png_get_color_type(png, info);
	png_set_strip_16(png);
	png_set_packing(png);
	if (color_type == PNG_COLOR_TYPE_GRAY)
		png_set_expand_gray_1_2_4_to_8(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	} else if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png);
	}

	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	// Force 8-bit RGBA format
	png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	png_set_interlace_handling(png);
	// Ignore the file's gamma correction
	png_set_gamma(png, 1.0, 1.0);

	png_read_update_info(png, info);

	size_t row_bytes = png_get_rowbytes(png, info);
	size_t size = row_bytes * (*pheight);

	*image_data_ptr = (unsigned char *)malloc(size);
	if (!*image_data_ptr) {
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * (*pheight));
	for (int y = 0; y < *pheight; y++) {
		row_pointers[y] = *image_data_ptr + y * row_bytes;
	}

	png_read_image(png, row_pointers);
	free(row_pointers);
	png_destroy_read_struct(&png, &info, NULL);
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
