#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <png.h>

#include "Common/Data/Format/PNGLoad.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"

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

struct PngReadContext {
	const unsigned char *ptr;
	size_t remaining;
};


int pngLoadPtr(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr, int maxWidth, int maxHeight) {
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

	PngReadContext readContext = {input_ptr, input_len};

	png_set_read_fn(png, &readContext, [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
		PngReadContext *ctx = (PngReadContext *)png_get_io_ptr(png_ptr);
		if (byteCountToRead > ctx->remaining) {
			// This triggers the longjmp to your pngErrorHandler
			png_error(png_ptr, "Read past end of buffer");
			return;
		}

		memcpy(outBytes, ctx->ptr, byteCountToRead);
		ctx->ptr += byteCountToRead;
		ctx->remaining -= byteCountToRead;
	});

	png_read_info(png, info);

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

	*pwidth = png_get_image_width(png, info);
	*pheight = png_get_image_height(png, info);

	// Reject images larger than the caller's limits to avoid decompression
	// bombs (attacker-controlled dimensions would otherwise drive a huge
	// allocation here).
	if (*pwidth > maxWidth || *pheight > maxHeight) {
		DEBUG_LOG(Log::IO, "PNG too large: %dx%d (max %dx%d)", *pwidth, *pheight, maxWidth, maxHeight);
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	size_t row_bytes = png_get_rowbytes(png, info);
	*image_data_ptr = (unsigned char *)malloc(row_bytes * (*pheight));
	if (!*image_data_ptr) {
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	std::vector<png_bytep> row_pointers(*pheight);
	for (int y = 0; y < *pheight; y++) {
		row_pointers[y] = *image_data_ptr + y * row_bytes;
	}

	png_read_image(png, row_pointers.data());
	png_destroy_read_struct(&png, &info, NULL);
	return 1;
}

int pngLoadIndexed(const unsigned char *input_ptr, size_t input_len, int *pwidth, int *pheight, unsigned char **image_data_ptr, int maxWidth, int maxHeight) {
	*image_data_ptr = nullptr;
	if (input_len == 0) {
		return 0;
	}

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, pngErrorHandler, pngWarningHandler);
	if (!png) {
		return 0;
	}

	png_set_option(png, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
	png_set_benign_errors(png, PNG_OPTION_ON);

	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		return 0;
	}

	unsigned char *data = nullptr;
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, NULL);
		free(data);
		return 0;
	}

	PngReadContext readContext = {input_ptr, input_len};

	png_set_read_fn(png, &readContext, [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
		PngReadContext *ctx = (PngReadContext *)png_get_io_ptr(png_ptr);
		if (byteCountToRead > ctx->remaining) {
			png_error(png_ptr, "Read past end of buffer");
			return;
		}

		memcpy(outBytes, ctx->ptr, byteCountToRead);
		ctx->ptr += byteCountToRead;
		ctx->remaining -= byteCountToRead;
	});

	png_read_info(png, info);

	// Only formats where a pixel is a single index: palette images (the index into the file's
	// palette) and grayscale images (the gray value itself). Nothing is expanded or scaled,
	// so with 1, 2 and 4 bit images the index keeps its raw value.
	const int color_type = png_get_color_type(png, info);
	const int bit_depth = png_get_bit_depth(png, info);
	if ((color_type != PNG_COLOR_TYPE_PALETTE && color_type != PNG_COLOR_TYPE_GRAY) || bit_depth > 8) {
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	png_set_packing(png);
	png_set_interlace_handling(png);
	png_read_update_info(png, info);

	*pwidth = png_get_image_width(png, info);
	*pheight = png_get_image_height(png, info);
	if (*pwidth <= 0 || *pheight <= 0 || *pwidth > maxWidth || *pheight > maxHeight) {
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	const size_t row_bytes = png_get_rowbytes(png, info);
	data = (unsigned char *)malloc(row_bytes * (*pheight));
	if (!data) {
		png_destroy_read_struct(&png, &info, NULL);
		return 0;
	}

	std::vector<png_bytep> row_pointers(*pheight);
	for (int y = 0; y < *pheight; y++) {
		row_pointers[y] = data + y * row_bytes;
	}

	png_read_image(png, row_pointers.data());
	png_destroy_read_struct(&png, &info, NULL);
	*image_data_ptr = data;
	return 1;
}

bool PNGHeaderPeek::IsValidPNGHeader() const {
	if (magic != 0x474e5089 || ihdrTag != 0x52444849) {
		return false;
	}
	// Keep the peeker's limit consistent with pngLoadPtr(). A replacement
	// texture is decoded into an uncompressed RGBA buffer, so accepting one
	// oversized dimension would still allow a decompression bomb.
	const int width = Width();
	const int height = Height();
	if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
		return false;
	}
	return true;
}

// With a palette, buffer holds one byte per pixel (the index) and bytesPerPixel is 1.
static bool pngSaveImpl(const Path &filename, const void *buffer, int w, int h, int bytesPerPixel, const uint32_t *palette, int numColors) {
	png_bytepp row_ptrs = nullptr;

	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(Log::IO, "Unable to open png file for writing: %s", filename.c_str());
		return false;
	}

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, pngErrorHandler, pngWarningHandler);
	if (png_ptr == nullptr) {
		fclose(fp);
		ERROR_LOG(Log::IO, "PNG encode failed.");
		return false;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr) {
		png_destroy_write_struct(&png_ptr, nullptr);
		fclose(fp);
		ERROR_LOG(Log::IO, "PNG encode failed.");
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		if (row_ptrs != nullptr) {
			png_free(png_ptr, row_ptrs);
		}
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);

		// Should we even do this?
		File::Delete(filename);

		ERROR_LOG(Log::IO, "PNG encode failed.");
		return false;
	}

	png_set_write_fn(png_ptr, fp, [](png_structp png_ptr, png_bytep data, png_size_t size) {
		if (fwrite(data, 1, size, (FILE *)png_get_io_ptr(png_ptr)) < size) {
			png_error(png_ptr, "Failed to write to file.");
		}
	}, [](png_structp png_ptr) {
		fflush((FILE *)png_get_io_ptr(png_ptr));
	});

	int colorType = bytesPerPixel == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA;
	if (palette) {
		colorType = PNG_COLOR_TYPE_PALETTE;
	}
	png_set_IHDR(png_ptr, info_ptr, w, h, 8, colorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	if (palette) {
		// The palette entries are RGBA, in memory order.
		png_color colors[256];
		png_byte alphas[256];
		int numAlphas = 0;
		for (int i = 0; i < numColors; ++i) {
			const uint8_t *rgba = (const uint8_t *)&palette[i];
			colors[i].red = rgba[0];
			colors[i].green = rgba[1];
			colors[i].blue = rgba[2];
			alphas[i] = rgba[3];
			if (rgba[3] != 0xFF) {
				numAlphas = i + 1;
			}
		}
		png_set_PLTE(png_ptr, info_ptr, colors, numColors);
		if (numAlphas > 0) {
			// Entries past the last one that isn't opaque can be left out.
			png_set_tRNS(png_ptr, info_ptr, alphas, numAlphas, nullptr);
		}
	}

	row_ptrs = (png_bytepp)png_malloc(png_ptr, (png_alloc_size_t)h * (png_alloc_size_t)sizeof(png_bytep));
	for (png_alloc_size_t i = 0; i < h; ++i) {
		row_ptrs[i] = (png_bytep)buffer + (png_alloc_size_t)w * (png_alloc_size_t)bytesPerPixel * i;
	}
	png_set_rows(png_ptr, info_ptr, row_ptrs);

	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	png_free(png_ptr, row_ptrs);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	fclose(fp);
	return true;
}

bool pngSave(const Path &filename, const void *buffer, int w, int h, int bytesPerPixel) {
	return pngSaveImpl(filename, buffer, w, h, bytesPerPixel, nullptr, 0);
}

bool pngSaveIndexed(const Path &filename, const uint8_t *indices, int w, int h, const uint32_t *palette, int numColors) {
	if (!palette || numColors < 1 || numColors > 256) {
		return false;
	}
	return pngSaveImpl(filename, indices, w, h, 1, palette, numColors);
}
