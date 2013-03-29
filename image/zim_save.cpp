#include <stdio.h>
#include <string.h>
#include <math.h>
#include "base/logging.h"
#include "ext/rg_etc1/rg_etc1.h"
#include "image/zim_save.h"
#include "zlib.h"

static const char magic[5] = "ZIMG";

/*int num_levels = 1;
if (flags & ZIM_HAS_MIPS) {
num_levels = log2i(width > height ? width : height);
}*/
static unsigned int log2i(unsigned int val) {
	unsigned int ret = -1;
	while (val != 0) {
		val >>= 1; ret++;
	}
	return ret;
}


int ezcompress(unsigned char* pDest, long* pnDestLen, const unsigned char* pSrc, long nSrcLen) {
	z_stream stream;
	int err;

	int nExtraChunks;
	uInt destlen;

	stream.next_in = (Bytef*)pSrc;
	stream.avail_in = (uInt)nSrcLen;
#ifdef MAXSEG_64K
	/* Check for source > 64K on 16-bit machine: */
	if ((uLong)stream.avail_in != nSrcLen) return Z_BUF_ERROR;
#endif
	destlen = (uInt)*pnDestLen;
	if ((uLong)destlen != (uLong)*pnDestLen) return Z_BUF_ERROR;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	stream.opaque = (voidpf)0;

	err = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
	if (err != Z_OK) return err;
	nExtraChunks = 0;
	do {
		stream.next_out = pDest;
		stream.avail_out = destlen;
		err = deflate(&stream, Z_FINISH);
		if (err == Z_STREAM_END )
			break;
		if (err != Z_OK) {
			deflateEnd(&stream);
			return err;
		}
		nExtraChunks += 1;
	} while (stream.avail_out == 0);

	*pnDestLen = stream.total_out;

	err = deflateEnd(&stream);
	if (err != Z_OK) return err;

	return nExtraChunks ? Z_BUF_ERROR : Z_OK;
}

inline int clamp16(int x) { if (x < 0) return 0; if (x > 15) return 15; return x; }
inline int clamp32(int x) { if (x < 0) return 0; if (x > 31) return 31; return x; }
inline int clamp64(int x) { if (x < 0) return 0; if (x > 63) return 63; return x; }


bool ispowerof2 (int x) {
	if (!x || (x&(x-1)))
		return false;
	else
		return true; 
} 



void Convert(const uint8_t *image_data, int width, int height, int pitch, int flags,
	uint8_t **data, int *data_size) {
		// For 4444 and 565. Ordered dither matrix. looks really surprisingly good on cell phone screens at 4444. 
		int dith[16] = {
			1, 9, 3, 11,
			13, 5, 15, 7,
			4, 12, 2, 10,
			16, 8, 14, 6
		};
		if ((flags & ZIM_DITHER) == 0) {
			for (int i = 0; i < 16; i++) { dith[i] = 8; }
		}
		switch (flags & ZIM_FORMAT_MASK) {
		case ZIM_RGBA8888:
			{
				*data_size = width * height * 4;
				*data = new uint8_t[width * height * 4];
				for (int y = 0; y < height; y++) {
					memcpy((*data) + y * width * 4, image_data + y * pitch, width * 4);
				}
				break;
			}
		case ZIM_ETC1: {
			rg_etc1::pack_etc1_block_init();
			rg_etc1::etc1_pack_params params;
			params.m_dithering = false; //(flags & ZIM_ETC1_DITHER) != 0;
			if (flags & ZIM_ETC1_LOW) {
				params.m_quality = rg_etc1::cLowQuality;
			} else if (flags & ZIM_ETC1_MEDIUM) {
				params.m_quality = rg_etc1::cMediumQuality;
			} else {
				params.m_quality = rg_etc1::cHighQuality;
			}

			// Check for power of 2
			if (!ispowerof2(width) || !ispowerof2(height)) {
				FLOG("Image must have power of 2 dimensions, %ix%i just isn't that.", width, height);
			}
			// Convert RGBX to ETC1 before saving.
			int blockw = width/4;
			int blockh = height/4;
			*data_size = blockw * blockh * 8;
			*data = new uint8_t[*data_size];
#pragma omp parallel for 
			for (int y = 0; y < blockh; y++) {
				for (int x = 0; x < blockw; x++) {
					uint32_t block[16];
					for (int iy = 0; iy < 4; iy++) {
						memcpy(block + 4 * iy, image_data + ((y * 4 + iy) * (pitch/4) + x * 4) * 4, 16);
					}
					rg_etc1::pack_etc1_block((*data) + (blockw * y + x) * 8, block, params);
				}
			}
			width = blockw * 4;
			height = blockh * 4;
			break;
									 }
		case ZIM_RGBA4444:
			{
				*data_size = width * height * 2;
				*data = new uint8_t[*data_size];
				uint16_t *dst = (uint16_t *)(*data);
				int i = 0;
				for (int y = 0; y < height; y++) {
					for (int x = 0; x < width; x++) {
						int dithval = dith[(x&3)+((y&0x3)<<2)] - 8;
						int r = clamp16((image_data[i * 4] + dithval) >> 4);
						int g = clamp16((image_data[i * 4 + 1] + dithval) >> 4);
						int b = clamp16((image_data[i * 4 + 2] + dithval) >> 4);
						int a = clamp16((image_data[i * 4 + 3] + dithval) >> 4);	// really dither alpha?
						// Note: GL_UNSIGNED_SHORT_4_4_4_4, not GL_UNSIGNED_SHORT_4_4_4_4_REV
						*dst++ = (r << 12) | (g << 8) | (b << 4) | (a << 0);
						i++;
					}
				}
				break;
			}
		case ZIM_RGB565:
			{
				*data_size = width * height * 2;
				*data = new uint8_t[*data_size];
				uint16_t *dst = (uint16_t *)(*data);
				int i = 0;
				for (int y = 0; y < height; y++) {
					for (int x = 0; x < width; x++) {
						int dithval = dith[(x&3)+((y&0x3)<<2)] - 8;
						dithval = 0;
						int r = clamp32((image_data[i * 4] + dithval/2) >> 3);
						int g = clamp64((image_data[i * 4 + 1] + dithval/4) >> 2);
						int b = clamp32((image_data[i * 4 + 2] + dithval/2) >> 3);
						// Note: GL_UNSIGNED_SHORT_5_6_5, not GL_UNSIGNED_SHORT_5_6_5_REV
						*dst++ = (r << 11) | (g << 5) | (b);
						i++;
					}
				}
			}
			break;

		default:
			ELOG("Unhandled ZIM format %i", flags & ZIM_FORMAT_MASK);
			*data = 0;
			*data_size = 0;
			return;
		}	
}

// Deletes the old buffer.
uint8_t *DownsampleBy2(const uint8_t *image, int width, int height, int pitch) {
	uint8_t *out = new uint8_t[(width/2) * (height/2) * 4];

	int degamma[256];
	int gamma[32768];
	for (int i =0; i < 256; i++) {
		degamma[i] = powf((float)i / 255.0f, 1.0f/2.2f) * 8191.0f;
	}
	for (int i = 0; i < 32768; i++) {
		gamma[i] = powf((float)i / 32764.0f, 2.2f) * 255.0f;
	}

	// Really stupid mipmap downsampling - at least it does gamma though.
	for (int y = 0; y < height; y+=2) {
		for (int x = 0; x < width; x+=2) {
			const uint8_t *tl = image + pitch * y + x*4;
			const uint8_t *tr = tl + 4;
			const uint8_t *bl = tl + pitch;
			const uint8_t *br = bl + 4;
			uint8_t *d = out + ((y/2) * ((width/2)) + x / 2) * 4;
			for (int c = 0; c < 4; c++) {
				d[c] = gamma[degamma[tl[c]] + degamma[tr[c]] + degamma[bl[c]] + degamma[br[c]]];
			}
		}
	}
	return out;
}

void SaveZIM(const char *filename, int width, int height, int pitch, int flags, const uint8_t *image_data) {
	FILE *f = fopen(filename, "wb");
	fwrite(magic, 1, 4, f);
	fwrite(&width, 1, 4, f);
	fwrite(&height, 1, 4, f);
	fwrite(&flags, 1, 4, f);

	int num_levels = 1;
	if (flags & ZIM_HAS_MIPS) {
		num_levels = log2i(width > height ? height : width) + 1;
	}
	for (int i = 0; i < num_levels; i++) {
		uint8_t *data = 0;
		int data_size;
		Convert(image_data, width, height, pitch, flags, &data, &data_size);
		if (flags & ZIM_ZLIB_COMPRESSED) {
			long dest_len = data_size * 2;
			uint8_t *dest = new uint8_t[dest_len];
			if (Z_OK == ezcompress(dest, &dest_len, data, data_size)) {
				fwrite(dest, 1, dest_len, f);
			} else {
				ELOG("Zlib compression failed.\n");
			}
			delete [] dest;
		} else {
			fwrite(data, 1, data_size, f);
		}
		delete [] data;

		if (i != num_levels - 1) {
			uint8_t *smaller = DownsampleBy2(image_data, width, height, pitch);
			if (i != 0) {
				delete [] image_data;
			}
			image_data = smaller;
			width /= 2;
			height /= 2;
			if ((flags & ZIM_FORMAT_MASK) == ZIM_ETC1) {
				if (width < 4) width = 4;
				if (height < 4) height = 4;
			}
			pitch = width * 4;
		}
	}
	delete [] image_data;
	fclose(f);
}
