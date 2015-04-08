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
#include "GPU/Common/GPUDebugInterface.h"
#ifdef _WIN32
#include "GPU/Directx9/GPU_DX9.h"
#endif
#include "GPU/GLES/GLES_GPU.h"
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

static bool WriteScreenshotToJPEG(const char *filename, int width, int height, int num_channels, const uint8 *image_data, const jpge::params &comp_params) {
	JPEGFileStream dst_stream(filename);
	if (!dst_stream.Valid()) {
		ERROR_LOG(COMMON, "Unable to open screenshot file for writing.");
		return false;
	}

	jpge::jpeg_encoder dst_image;
	if (!dst_image.init(&dst_stream, width, height, num_channels, comp_params)) {
		ERROR_LOG(COMMON, "Screenshot JPEG encode init failed.");
		return false;
	}

	for (u32 pass_index = 0; pass_index < dst_image.get_total_passes(); pass_index++) {
		for (int i = 0; i < height; i++) {
			const uint8 *buf = image_data + i * width * num_channels;
			if (!dst_image.process_scanline(buf)) {
				ERROR_LOG(COMMON, "Screenshot JPEG encode scanline failed.");
				return false;
			}
		}
		if (!dst_image.process_scanline(NULL)) {
			ERROR_LOG(COMMON, "Screenshot JPEG encode scanline flush failed.");
			return false;
		}
	}

	if (!dst_stream.Valid()) {
		ERROR_LOG(COMMON, "Screenshot file write failed.");
	}

	dst_image.deinit();
	return dst_stream.Valid();
}

static bool WriteScreenshotToPNG(png_imagep image, const char *filename, int convert_to_8bit, const void *buffer, png_int_32 row_stride, const void *colormap) {
	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(COMMON, "Unable to open screenshot file for writing.");
		return false;
	}

	if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer, row_stride, colormap)) {
		if (fclose(fp) != 0) {
			ERROR_LOG(COMMON, "Screenshot file write failed.");
			return false;
		}
		return true;
	} else {
		ERROR_LOG(COMMON, "Screenshot PNG encode failed.");
		fclose(fp);
		remove(filename);
		return false;
	}
}
#endif

static const u8 *ConvertBufferTo888RGB(const GPUDebugBuffer &buf, u8 *&temp) {
	// The temp buffer will be freed by the caller if set, and can be the return value.
	temp = nullptr;

	const u8 *buffer = buf.GetData();
	if (buf.GetFlipped() && buf.GetFormat() == GPU_DBG_FORMAT_888_RGB) {
		// Silly OpenGL reads upside down, we flip to another buffer for simplicity.
		temp = new u8[3 * buf.GetStride() * buf.GetHeight()];
		for (u32 y = 0; y < buf.GetHeight(); y++) {
			memcpy(temp + y * buf.GetStride() * 3, buffer + (buf.GetHeight() - y - 1) * buf.GetStride() * 3, buf.GetStride() * 3);
		}
		buffer = temp;
	} else if (buf.GetFormat() != GPU_DBG_FORMAT_888_RGB) {
		// Let's boil it down to how we need to interpret the bits.
		int baseFmt = buf.GetFormat() & ~(GPU_DBG_FORMAT_REVERSE_FLAG | GPU_DBG_FORMAT_BRSWAP_FLAG);
		bool rev = (buf.GetFormat() & GPU_DBG_FORMAT_REVERSE_FLAG) != 0;
		bool brswap = (buf.GetFormat() & GPU_DBG_FORMAT_BRSWAP_FLAG) != 0;
		bool flip = buf.GetFlipped();

		temp = new u8[3 * buf.GetStride() * buf.GetHeight()];

		// This is pretty inefficient.
		const u16 *buf16 = (const u16 *)buffer;
		const u32 *buf32 = (const u32 *)buffer;
		for (u32 y = 0; y < buf.GetHeight(); y++) {
			for (u32 x = 0; x < buf.GetStride(); x++) {
				u8 *dst;
				if (flip) {
					dst = &temp[(buf.GetHeight() - y - 1) * buf.GetStride() * 3 + x * 3];
				} else {
					dst = &temp[y * buf.GetStride() * 3 + x * 3];
				}

				u8 &r = brswap ? dst[2] : dst[0];
				u8 &g = dst[1];
				u8 &b = brswap ? dst[0] : dst[2];

				u32 src;
				switch (baseFmt) {
				case GPU_DBG_FORMAT_565:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert5To8((src >> 0) & 0x1F);
					g = Convert6To8((src >> 5) & 0x3F);
					b = Convert5To8((src >> 11) & 0x1F);
					break;
				case GPU_DBG_FORMAT_5551:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert5To8((src >> 0) & 0x1F);
					g = Convert5To8((src >> 5) & 0x1F);
					b = Convert5To8((src >> 10) & 0x1F);
					break;
				case GPU_DBG_FORMAT_4444:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert4To8((src >> 0) & 0xF);
					g = Convert4To8((src >> 4) & 0xF);
					b = Convert4To8((src >> 8) & 0xF);
					break;
				case GPU_DBG_FORMAT_8888:
					src = buf32[y * buf.GetStride() + x];
					if (rev) {
						src = bswap32(src);
					}
					r = (src >> 0) & 0xFF;
					g = (src >> 8) & 0xFF;
					b = (src >> 16) & 0xFF;
					break;
				default:
					ERROR_LOG(COMMON, "Unsupported framebuffer format for screenshot: %d", buf.GetFormat());
					return nullptr;
				}
			}
		}
		buffer = temp;
	}

	return buffer;
}

bool TakeGameScreenshot(const char *filename, ScreenshotFormat fmt, ScreenshotType type) {
	GPUDebugBuffer buf;
	bool success = false;

	if (type == SCREENSHOT_RENDER) {
		if (gpuDebug) {
			success = gpuDebug->GetCurrentFramebuffer(buf);
		}
	} else {
		if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
			success = GLES_GPU::GetDisplayFramebuffer(buf);
#ifdef _WIN32
		} else if (g_Config.iGPUBackend == GPU_BACKEND_DIRECT3D9) {
			success = DX9::DIRECTX9_GPU::GetDisplayFramebuffer(buf);
#endif
		}
	}

	if (!success) {
		ERROR_LOG(COMMON, "Failed to obtain screenshot data.");
		return false;
	}

#ifdef USING_QT_UI
	if (success) {
		u8 *flipbuffer = nullptr;
		const u8 *buffer = ConvertBufferTo888RGB(buf, flipbuffer);
		// TODO: Handle other formats (e.g. Direct3D, raw framebuffers.)
		QImage image(buffer, buf.GetStride(), buf.GetHeight(), QImage::Format_RGB888);
		success = image.save(filename, fmt == SCREENSHOT_PNG ? "PNG" : "JPG");
		delete [] flipbuffer;
	}
#else
	if (success) {
		u8 *flipbuffer = nullptr;
		const u8 *buffer = ConvertBufferTo888RGB(buf, flipbuffer);
		if (buffer == nullptr) {
			success = false;
		}

		if (success && fmt == SCREENSHOT_PNG) {
			png_image png;
			memset(&png, 0, sizeof(png));
			png.version = PNG_IMAGE_VERSION;
			png.format = PNG_FORMAT_RGB;
			png.width = buf.GetStride();
			png.height = buf.GetHeight();
			success = WriteScreenshotToPNG(&png, filename, 0, flipbuffer, buf.GetStride() * 3, nullptr);
			png_image_free(&png);

			if (png.warning_or_error >= 2) {
				ERROR_LOG(COMMON, "Saving screenshot to PNG produced errors.");
				success = false;
			}
		} else if (success && fmt == SCREENSHOT_JPG) {
			jpge::params params;
			params.m_quality = 90;
			success = WriteScreenshotToJPEG(filename, buf.GetStride(), buf.GetHeight(), 3, flipbuffer, params);
		} else {
			success = false;
		}
		delete [] flipbuffer;
	}
#endif
	if (!success) {
		ERROR_LOG(COMMON, "Failed to write screenshot.");
	}
	return success;
}
