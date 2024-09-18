#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <zstd.h>

#include "zlib.h"

#include "Common/Log.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Math/math_util.h"
#include "Common/File/VFS/VFS.h"

int ezuncompress(unsigned char* pDest, long* pnDestLen, const unsigned char* pSrc, long nSrcLen) {
	z_stream stream;
	stream.next_in = (Bytef*)pSrc;
	stream.avail_in = (uInt)nSrcLen;
	/* Check for source > 64K on 16-bit machine: */
	if ((uLong)stream.avail_in != (uLong)nSrcLen) return Z_BUF_ERROR;

	uInt destlen = (uInt)*pnDestLen;
	if ((uLong)destlen != (uLong)*pnDestLen) return Z_BUF_ERROR;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;

	int err = inflateInit(&stream);
	if (err != Z_OK) return err;

	int nExtraChunks = 0;
	do {
		stream.next_out = pDest;
		stream.avail_out = destlen;
		err = inflate(&stream, Z_FINISH);
		if (err == Z_STREAM_END )
			break;
		if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
			err = Z_DATA_ERROR;
		if (err != Z_BUF_ERROR) {
			inflateEnd(&stream);
			return err;
		}
		nExtraChunks += 1;
	} while (stream.avail_out == 0);

	*pnDestLen = stream.total_out;

	err = inflateEnd(&stream);
	if (err != Z_OK) return err;

	return nExtraChunks ? Z_BUF_ERROR : Z_OK;
}

int LoadZIMPtr(const uint8_t *zim, size_t datasize, int *width, int *height, int *flags, uint8_t **image) {
	if (zim[0] != 'Z' || zim[1] != 'I' || zim[2] != 'M' || zim[3] != 'G') {
		ERROR_LOG(Log::IO, "Not a ZIM file");
		return 0;
	}
	memcpy(width, zim + 4, 4);
	memcpy(height, zim + 8, 4);
	memcpy(flags, zim + 12, 4);

	int num_levels = 1;
	int image_data_size[ZIM_MAX_MIP_LEVELS];
	if (*flags & ZIM_HAS_MIPS) {
		num_levels = log2i(*width < *height ? *width : *height) + 1;
	}
	int total_data_size = 0;
	for (int i = 0; i < num_levels; i++) {
		if (i > 0) {
			width[i] = width[i-1] / 2;
			height[i] = height[i-1] / 2;
		}
		switch (*flags & ZIM_FORMAT_MASK) {
		case ZIM_RGBA8888:
			image_data_size[i] = width[i] * height[i] * 4; 
			break;
		case ZIM_RGBA4444:
		case ZIM_RGB565:
			image_data_size[i] = width[i] * height[i] * 2;
			break;
		default:
			ERROR_LOG(Log::IO, "Invalid ZIM format %i", *flags & ZIM_FORMAT_MASK);
			return 0;
		}
		total_data_size += image_data_size[i];
	}

	if (total_data_size == 0) {
		ERROR_LOG(Log::IO, "Invalid ZIM data size 0");
		return 0;
	}

	image[0] = (uint8_t *)malloc(total_data_size);
	for (int i = 1; i < num_levels; i++) {
		image[i] = image[i-1] + image_data_size[i-1];
	}

	if (*flags & ZIM_ZLIB_COMPRESSED) {
		long outlen = (long)total_data_size;
		int retcode = ezuncompress(*image, &outlen, (unsigned char *)(zim + 16), (long)datasize - 16);
		if (Z_OK != retcode) {
			ERROR_LOG(Log::IO, "ZIM zlib format decompression failed: %d", retcode);
			free(*image);
			*image = 0;
			return 0;
		}
		if (outlen != total_data_size) {
			// Shouldn't happen if return value was Z_OK.
			ERROR_LOG(Log::IO, "Wrong size data in ZIM: %i vs %i", (int)outlen, (int)total_data_size);
		}
	} else if (*flags & ZIM_ZSTD_COMPRESSED) {
		size_t outlen = ZSTD_decompress(*image, total_data_size, zim + 16, datasize - 16);
		if (outlen != (size_t)total_data_size) {
			ERROR_LOG(Log::IO, "ZIM zstd format decompression failed: %lld", (long long)outlen);
			free(*image);
			*image = 0;
			return 0;
		}
	} else {
		memcpy(*image, zim + 16, datasize - 16);
		if (datasize - 16 != (size_t)total_data_size) {
			ERROR_LOG(Log::IO, "Wrong size data in ZIM: %i vs %i", (int)(datasize-16), (int)total_data_size);
		}
	}
	return num_levels;
}

int LoadZIM(const char *filename, int *width, int *height, int *format, uint8_t **image) {
	size_t size;
	uint8_t *buffer = g_VFS.ReadFile(filename, &size);
	if (!buffer) {
		ERROR_LOG(Log::IO, "Couldn't read data for '%s'", filename);
		return 0;
	}

	int retval = LoadZIMPtr(buffer, size, width, height, format, image);
	if (!retval) {
		ERROR_LOG(Log::IO, "Not a valid ZIM file: %s (size: %lld bytes)", filename, (long long)size);
	}
	delete [] buffer;
	return retval;
}
