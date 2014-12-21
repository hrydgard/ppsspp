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
#endif
#include "ext/jpge/jpge.h"

#include "Core/Config.h"
#include "Core/Screenshot.h"
#include "GPU/Common/GPUDebugInterface.h"
#ifdef _WIN32
#include "GPU/Directx9/GPU_DX9.h"
#endif
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

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

#ifdef USING_QT_UI
	if (success) {
		// TODO: Handle other formats (e.g. Direct3D.)  Would only happen on Qt/Windows.
		const u8 *buffer = buf.GetData();
		QImage image(buffer, buf.GetStride(), buf.GetHeight(), QImage::Format_RGB888);
		image = image.mirrored();
		success = image.save(filename, fmt == SCREENSHOT_PNG ? "PNG" : "JPG");
	}
#else
	if (success) {
		const u8 *buffer = buf.GetData();
		u8 *flipbuffer = nullptr;
		if (buf.GetFlipped()) {
			// Silly OpenGL reads upside down, we flip to another buffer for simplicity.
			flipbuffer = new u8[3 * buf.GetStride() * buf.GetHeight()];
			if (buf.GetFormat() == GPU_DBG_FORMAT_888_RGB) {
				for (u32 y = 0; y < buf.GetHeight(); y++) {
					memcpy(flipbuffer + y * buf.GetStride() * 3, buffer + (buf.GetHeight() - y - 1) * buf.GetStride() * 3, buf.GetStride() * 3);
				}
			} else if (buf.GetFormat() == GPU_DBG_FORMAT_8888) {
				for (u32 y = 0; y < buf.GetHeight(); y++) {
					for (u32 x = 0; x < buf.GetStride(); x++) {
						u8 *dst = &flipbuffer[(buf.GetHeight() - y - 1) * buf.GetStride() * 3 + x * 3];
						const u8 *src = &buffer[y * buf.GetStride() * 4 + x * 4];
						memcpy(dst, src, 3);
					}
				}
			} else {
				success = false;
			}
			buffer = flipbuffer;
		} else if (buf.GetFormat() == GPU_DBG_FORMAT_8888_BGRA) {
			// Yay, we need to swap AND remove alpha.
			flipbuffer = new u8[3 * buf.GetStride() * buf.GetHeight()];
			for (u32 y = 0; y < buf.GetHeight(); y++) {
				for (u32 x = 0; x < buf.GetStride(); x++) {
					u8 *dst = &flipbuffer[y * buf.GetStride() * 3 + x * 3];
					const u8 *src = &buffer[y * buf.GetStride() * 4 + x * 4];
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
				}
			}
			buffer = flipbuffer;
		}

		if (success && fmt == SCREENSHOT_PNG) {
			png_image png;
			memset(&png, 0, sizeof(png));
			png.version = PNG_IMAGE_VERSION;
			png.format = PNG_FORMAT_RGB;
			png.width = buf.GetStride();
			png.height = buf.GetHeight();
			png_image_write_to_file(&png, filename, 0, flipbuffer, buf.GetStride() * 3, NULL);
			png_image_free(&png);

			success = png.warning_or_error < 2;
		} else if (success && fmt == SCREENSHOT_JPG) {
			jpge::params params;
			params.m_quality = 90;
			success = compress_image_to_jpeg_file(filename, buf.GetStride(), buf.GetHeight(), 3, flipbuffer, params);
		} else {
			success = false;
		}
		delete [] flipbuffer;
	}
#endif
	return success;
}
