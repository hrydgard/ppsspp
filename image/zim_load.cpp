#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef USING_QT_UI
#include <QFile>
#endif

#include "base/logging.h"
#include "zlib.h"
#include "image/zim_load.h"
#include "file/vfs.h"

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

static const char magic[5] = "ZIMG";

static unsigned int log2i(unsigned int val) {
	unsigned int ret = -1;
	while (val != 0) {
		val >>= 1; ret++;
	}
	return ret;
}

int LoadZIMPtr(uint8_t *zim, int datasize, int *width, int *height, int *flags, uint8 **image) {
	if (zim[0] != 'Z' || zim[1] != 'I' || zim[2] != 'M' || zim[3] != 'G') {
		ELOG("Not a ZIM file");
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
		case ZIM_ETC1:
			{
				int data_width = width[i];
				int data_height = height[i];
				if (data_width < 4) data_width = 4;
				if (data_height < 4) data_height = 4;
				image_data_size[i] = data_width * data_height / 2;
				break;
			}
		default:
			ELOG("Invalid ZIM format %i", *flags & ZIM_FORMAT_MASK);
			return 0;
		}
		total_data_size += image_data_size[i];
	}

	if (total_data_size == 0)
	{
		ELOG("Invalid ZIM data size 0");
		return 0;
	}

	image[0] = (uint8 *)malloc(total_data_size);
	for (int i = 1; i < num_levels; i++) {
		image[i] = image[i-1] + image_data_size[i-1];
	}

	if (*flags & ZIM_ZLIB_COMPRESSED) {
		long outlen = total_data_size;
		if (Z_OK != ezuncompress(*image, &outlen, (unsigned char *)(zim + 16), datasize - 16)) {
			free(*image);
			*image = 0;
			return 0;
		}
		if (outlen != total_data_size) {
			ELOG("Wrong size data in ZIM: %i vs %i", (int)outlen, (int)total_data_size);
		}
	} else {
		memcpy(*image, zim + 16, datasize - 16);
		if (datasize - 16 != total_data_size) {
			ELOG("Wrong size data in ZIM: %i vs %i", (int)(datasize-16), (int)total_data_size);
		}
	}
	return num_levels;
}

int LoadZIM(const char *filename, int *width, int *height, int *format, uint8_t **image) {
#ifdef USING_QT_UI
	QFile asset(QString(":/assets/") + filename);
	if (!asset.open(QIODevice::ReadOnly))
		return 0;
	int retval = LoadZIMPtr((uint8_t*)asset.readAll().data(), asset.size(), width, height, format, image);
	asset.close();
#else
	size_t size;
	uint8_t *buffer = VFSReadFile(filename, &size);
	if (!buffer) {
		return 0;
	}
	int retval = LoadZIMPtr(buffer, (int)size, width, height, format, image);
	if (!retval) {
		ELOG("Not a valid ZIM file: %s", filename);
	}
	delete [] buffer;
#endif
	return retval;
}
