// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#include <algorithm>
#ifdef USING_QT_UI
#include <QtGui/QImage>
#else
#include <libpng17/png.h>
#include "ext/jpge/jpge.h"
#endif

#include "Common/ColorConv.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/Screenshot.h"
#include "Core/Core.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#ifndef USING_QT_UI
// This is used to make non-ASCII paths work for filename.
// Technically only needed on Windows.
class JPEGFileStream : public jpge::output_stream
{
public:
	JPEGFileStream(const char *filename) {
		fp_ = File::OpenCFile(filename, "wb");
	}
	~JPEGFileStream() override {
		if (fp_ ) {
			fclose(fp_);
		}
	}

	bool put_buf(const void *buf, int len) override
	{
		if (fp_) {
			if (fwrite(buf, len, 1, fp_) != 1) {
				fclose(fp_);
				fp_ = nullptr;
			}
		}
		return Valid();
	}

	bool Valid() {
		return fp_ != nullptr;
	}

private:
	FILE *fp_;
};

static bool WriteScreenshotToJPEG(const char *filename, int width, int height, int num_channels, const uint8_t *image_data, const jpge::params &comp_params) {
	JPEGFileStream dst_stream(filename);
	if (!dst_stream.Valid()) {
		ERROR_LOG(SYSTEM, "Unable to open screenshot file for writing.");
		return false;
	}

	jpge::jpeg_encoder dst_image;
	if (!dst_image.init(&dst_stream, width, height, num_channels, comp_params)) {
		ERROR_LOG(SYSTEM, "Screenshot JPEG encode init failed.");
		return false;
	}

	for (u32 pass_index = 0; pass_index < dst_image.get_total_passes(); pass_index++) {
		for (int i = 0; i < height; i++) {
			const uint8_t *buf = image_data + i * width * num_channels;
			if (!dst_image.process_scanline(buf)) {
				ERROR_LOG(SYSTEM, "Screenshot JPEG encode scanline failed.");
				return false;
			}
		}
		if (!dst_image.process_scanline(NULL)) {
			ERROR_LOG(SYSTEM, "Screenshot JPEG encode scanline flush failed.");
			return false;
		}
	}

	if (!dst_stream.Valid()) {
		ERROR_LOG(SYSTEM, "Screenshot file write failed.");
	}

	dst_image.deinit();
	return dst_stream.Valid();
}

static bool WriteScreenshotToPNG(png_imagep image, const char *filename, int convert_to_8bit, const void *buffer, png_int_32 row_stride, const void *colormap) {
	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(SYSTEM, "Unable to open screenshot file for writing.");
		return false;
	}

	if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer, row_stride, colormap)) {
		if (fclose(fp) != 0) {
			ERROR_LOG(SYSTEM, "Screenshot file write failed.");
			return false;
		}
		return true;
	} else {
		ERROR_LOG(SYSTEM, "Screenshot PNG encode failed.");
		fclose(fp);
		remove(filename);
		return false;
	}
}
#endif

static bool ConvertPixelTo8888RGBA(GPUDebugBufferFormat fmt, u8 &r, u8 &g, u8 &b, u8 &a, const void *buffer, int offset, bool rev) {
	const u8 *buf8 = (const u8 *)buffer;
	const u16 *buf16 = (const u16 *)buffer;
	const u32 *buf32 = (const u32 *)buffer;
	const float *fbuf = (const float *)buffer;

	// NOTE: a and r might be the same channel.  This is used for RGB.

	u32 src;
	double fsrc;
	switch (fmt) {
	case GPU_DBG_FORMAT_565:
		src = buf16[offset];
		if (rev) {
			src = bswap16(src);
		}
		a = 255;
		r = Convert5To8((src >> 0) & 0x1F);
		g = Convert6To8((src >> 5) & 0x3F);
		b = Convert5To8((src >> 11) & 0x1F);
		break;
	case GPU_DBG_FORMAT_5551:
		src = buf16[offset];
		if (rev) {
			src = bswap16(src);
		}
		a = (src >> 15) ? 255 : 0;
		r = Convert5To8((src >> 0) & 0x1F);
		g = Convert5To8((src >> 5) & 0x1F);
		b = Convert5To8((src >> 10) & 0x1F);
		break;
	case GPU_DBG_FORMAT_4444:
		src = buf16[offset];
		if (rev) {
			src = bswap16(src);
		}
		a = Convert4To8((src >> 12) & 0xF);
		r = Convert4To8((src >> 0) & 0xF);
		g = Convert4To8((src >> 4) & 0xF);
		b = Convert4To8((src >> 8) & 0xF);
		break;
	case GPU_DBG_FORMAT_8888:
		src = buf32[offset];
		if (rev) {
			src = bswap32(src);
		}
		a = (src >> 24) & 0xFF;
		r = (src >> 0) & 0xFF;
		g = (src >> 8) & 0xFF;
		b = (src >> 16) & 0xFF;
		break;
	case GPU_DBG_FORMAT_FLOAT:
		fsrc = fbuf[offset];
		r = 255;
		g = 0;
		b = 0;
		a = fsrc >= 1.0 ? 255 : (fsrc < 0.0 ? 0 : (int)(fsrc * 255.0));
		break;
	case GPU_DBG_FORMAT_16BIT:
		src = buf16[offset];
		r = 255;
		g = 0;
		b = 0;
		a = src >> 8;
		break;
	case GPU_DBG_FORMAT_8BIT:
		src = buf8[offset];
		r = 255;
		g = 0;
		b = 0;
		a = src;
		break;
	case GPU_DBG_FORMAT_24BIT_8X:
		src = buf32[offset];
		r = 255;
		g = 0;
		b = 0;
		a = (src >> 16) & 0xFF;
		break;
	case GPU_DBG_FORMAT_24X_8BIT:
		src = buf32[offset];
		r = 255;
		g = 0;
		b = 0;
		a = (src >> 24) & 0xFF;
		break;
	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
		src = buf32[offset]& 0x00FFFFFF;
		src = src - 0x800000 + 0x8000;
		r = 255;
		g = 0;
		b = 0;
		a = (src >> 8) & 0xFF;
		break;
	case GPU_DBG_FORMAT_FLOAT_DIV_256:
		fsrc = fbuf[offset];
		src = (int)(fsrc * 16777215.0);
		src = src - 0x800000 + 0x8000;
		r = 255;
		g = 0;
		b = 0;
		a = (src >> 8) & 0xFF;
		break;
	default:
		_assert_msg_(SYSTEM, false, "Unsupported framebuffer format for screenshot: %d", fmt);
		return false;
	}

	return true;
}

const u8 *ConvertBufferToScreenshot(const GPUDebugBuffer &buf, bool alpha, u8 *&temp, u32 &w, u32 &h) {
	int pixelSize = alpha ? 4 : 3;
	GPUDebugBufferFormat nativeFmt = alpha ? GPU_DBG_FORMAT_8888 : GPU_DBG_FORMAT_888_RGB;

	w = std::min(w, buf.GetStride());
	h = std::min(h, buf.GetHeight());

	// The temp buffer will be freed by the caller if set, and can be the return value.
	if (buf.GetFlipped() || buf.GetFormat() != nativeFmt) {
		temp = new u8[pixelSize * w * h];
	} else {
		temp = nullptr;
	}

	const u8 *buffer = buf.GetData();
	if (buf.GetFlipped() && buf.GetFormat() == nativeFmt) {
		// Silly OpenGL reads upside down, we flip to another buffer for simplicity.
		for (u32 y = 0; y < h; y++) {
			memcpy(temp + y * w * pixelSize, buffer + (buf.GetHeight() - y - 1) * buf.GetStride() * pixelSize, w * pixelSize);
		}
	} else if (buf.GetFormat() < GPU_DBG_FORMAT_FLOAT && buf.GetFormat() != nativeFmt) {
		// Let's boil it down to how we need to interpret the bits.
		int baseFmt = buf.GetFormat() & ~(GPU_DBG_FORMAT_REVERSE_FLAG | GPU_DBG_FORMAT_BRSWAP_FLAG);
		bool rev = (buf.GetFormat() & GPU_DBG_FORMAT_REVERSE_FLAG) != 0;
		bool brswap = (buf.GetFormat() & GPU_DBG_FORMAT_BRSWAP_FLAG) != 0;
		bool flip = buf.GetFlipped();

		// This is pretty inefficient.
		for (u32 y = 0; y < h; y++) {
			for (u32 x = 0; x < w; x++) {
				u8 *dst;
				if (flip) {
					dst = &temp[(h - y - 1) * w * pixelSize + x * pixelSize];
				} else {
					dst = &temp[y * w * pixelSize + x * pixelSize];
				}

				u8 &r = brswap ? dst[2] : dst[0];
				u8 &g = dst[1];
				u8 &b = brswap ? dst[0] : dst[2];
				u8 &a = alpha ? dst[3] : r;

				if (!ConvertPixelTo8888RGBA(GPUDebugBufferFormat(baseFmt), r, g, b, a, buffer, y * buf.GetStride() + x, rev)) {
					return nullptr;
				}
			}
		}
	} else if (buf.GetFormat() != nativeFmt) {
		bool flip = buf.GetFlipped();

		// This is pretty inefficient.
		for (u32 y = 0; y < h; y++) {
			for (u32 x = 0; x < w; x++) {
				u8 *dst;
				if (flip) {
					dst = &temp[(h - y - 1) * w * pixelSize + x * pixelSize];
				} else {
					dst = &temp[y * w * pixelSize + x * pixelSize];
				}

				u8 &a = alpha ? dst[3] : dst[0];
				if (!ConvertPixelTo8888RGBA(buf.GetFormat(), dst[0], dst[1], dst[2], a, buffer, y * buf.GetStride() + x, false)) {
					return nullptr;
				}
			}
		}
	}

	return temp ? temp : buffer;
}

bool TakeGameScreenshot(const char *filename, ScreenshotFormat fmt, ScreenshotType type, int *width, int *height, int maxRes) {
	if (!gpuDebug) {
		ERROR_LOG(SYSTEM, "Can't take screenshots when GPU not running");
		return false;
	}
	GPUDebugBuffer buf;
	bool success = false;
	u32 w = (u32)-1;
	u32 h = (u32)-1;

	if (type == SCREENSHOT_DISPLAY || type == SCREENSHOT_RENDER) {
		success = gpuDebug->GetCurrentFramebuffer(buf, type == SCREENSHOT_RENDER ? GPU_DBG_FRAMEBUF_RENDER : GPU_DBG_FRAMEBUF_DISPLAY, maxRes);

		// Only crop to the top left when using a render screenshot.
		w = maxRes > 0 ? 480 * maxRes : PSP_CoreParameter().renderWidth;
		h = maxRes > 0 ? 272 * maxRes : PSP_CoreParameter().renderHeight;
	} else {
		success = gpuDebug->GetOutputFramebuffer(buf);
	}

	if (!success) {
		ERROR_LOG(G3D, "Failed to obtain screenshot data.");
		return false;
	}

	u8 *flipbuffer = nullptr;
	if (success) {
		const u8 *buffer = ConvertBufferToScreenshot(buf, false, flipbuffer, w, h);
		success = buffer != nullptr;
		if (success) {
			if (width)
				*width = w;
			if (height)
				*height = h;

			success = Save888RGBScreenshot(filename, fmt, buffer, w, h);
		}
	}
	delete [] flipbuffer;

	if (!success) {
		ERROR_LOG(SYSTEM, "Failed to write screenshot.");
	}
	return success;
}

bool Save888RGBScreenshot(const char *filename, ScreenshotFormat fmt, const u8 *bufferRGB888, int w, int h) {
#ifdef USING_QT_UI
	QImage image(bufferRGB888, w, h, QImage::Format_RGB888);
	return image.save(filename, fmt == ScreenshotFormat::PNG ? "PNG" : "JPG");
#else
	if (fmt == ScreenshotFormat::PNG) {
		png_image png;
		memset(&png, 0, sizeof(png));
		png.version = PNG_IMAGE_VERSION;
		png.format = PNG_FORMAT_RGB;
		png.width = w;
		png.height = h;
		bool success = WriteScreenshotToPNG(&png, filename, 0, bufferRGB888, w * 3, nullptr);
		png_image_free(&png);

		if (png.warning_or_error >= 2) {
			ERROR_LOG(SYSTEM, "Saving screenshot to PNG produced errors.");
			success = false;
		}
		return success;
	} else if (fmt == ScreenshotFormat::JPG) {
		jpge::params params;
		params.m_quality = 90;
		return WriteScreenshotToJPEG(filename, w, h, 3, bufferRGB888, params);
	} else {
		return false;
	}
#endif
}

bool Save8888RGBAScreenshot(const char *filename, const u8 *buffer, int w, int h) {
#ifdef USING_QT_UI
	QImage image(buffer, w, h, QImage::Format_RGBA8888);
	return image.save(filename, "PNG");
#else
	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	png.format = PNG_FORMAT_RGBA;
	png.width = w;
	png.height = h;
	bool success = WriteScreenshotToPNG(&png, filename, 0, buffer, w * 4, nullptr);
	png_image_free(&png);

	if (png.warning_or_error >= 2) {
		ERROR_LOG(SYSTEM, "Saving screenshot to PNG produced errors.");
		success = false;
	}
	return success;
#endif
}
