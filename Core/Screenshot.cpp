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

#include <png.h>
#include "ext/jpge/jpge.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/Thread/Promise.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Screenshot.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/GPUCommon.h"

struct PendingScreenshot {
	ScreenshotType type;
	ScreenshotFormat format;
	Path path;
	std::function<void(ScreenshotResult result)> callback;
	int maxRes;
};

PendingScreenshot g_pendingScreenshot;

class JPEGFileStream : public jpge::output_stream {
public:
	JPEGFileStream(const Path &filename) {
		fp_ = File::OpenCFile(filename, "wb");
	}
	~JPEGFileStream() {
		if (fp_ ) {
			fclose(fp_);
		}
	}
	bool put_buf(const void *buf, int len) override {
		if (!fp_) {
			return false;
		}
		if (fwrite(buf, len, 1, fp_) != 1) {
			fclose(fp_);
			fp_ = nullptr;
			return false;
		}
		return true;
	}
	bool Valid() {
		return fp_ != nullptr;
	}
private:
	FILE *fp_;
};

static bool WriteScreenshotToJPEG(const Path &filename, int width, int height, int num_channels, const uint8_t *image_data, const jpge::params &comp_params) {
	JPEGFileStream dst_stream(filename);
	if (!dst_stream.Valid()) {
		ERROR_LOG(Log::IO, "Unable to open screenshot file for writing.");
		return false;
	}

	jpge::jpeg_encoder dst_image;
	if (!dst_image.init(&dst_stream, width, height, num_channels, comp_params)) {
		return false;
	}

	for (u32 pass_index = 0; pass_index < dst_image.get_total_passes(); pass_index++) {
		for (int i = 0; i < height; i++) {
			const uint8_t *buf = image_data + i * width * num_channels;
			if (!dst_image.process_scanline(buf)) {
				return false;
			}
		}
		if (!dst_image.process_scanline(nullptr)) {
			return false;
		}
	}

	if (!dst_stream.Valid()) {
		ERROR_LOG(Log::System, "Screenshot file write failed.");
	}

	dst_image.deinit();
	return dst_stream.Valid();
}

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
		_assert_msg_(false, "Unsupported framebuffer format for screenshot: %d", fmt);
		return false;
	}

	return true;
}

const u8 *ConvertBufferToScreenshot(const GPUDebugBuffer &buf, bool alpha, u8 *&temp, u32 &w, u32 &h) {
	size_t pixelSize = alpha ? 4 : 3;
	GPUDebugBufferFormat nativeFmt = alpha ? GPU_DBG_FORMAT_8888 : GPU_DBG_FORMAT_888_RGB;

	w = std::min(w, buf.GetStride());
	h = std::min(h, buf.GetHeight());

	// The temp buffer will be freed by the caller if set, and can be the return value.
	temp = nullptr;

	const u8 *buffer = buf.GetData();
	if (buf.GetFlipped() && buf.GetFormat() == nativeFmt) {
		temp = new u8[pixelSize * w * h];
		// Silly OpenGL reads upside down, we flip to another buffer for simplicity.
		for (u32 y = 0; y < h; y++) {
			memcpy(temp + y * w * pixelSize, buffer + (buf.GetHeight() - y - 1) * buf.GetStride() * pixelSize, w * pixelSize);
		}
	} else if (buf.GetFormat() < GPU_DBG_FORMAT_FLOAT && buf.GetFormat() != nativeFmt) {
		temp = new u8[pixelSize * w * h];
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
		temp = new u8[pixelSize * w * h];
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

static GPUDebugBuffer ApplyRotation(const GPUDebugBuffer &buf, DisplayRotation rotation) {
	GPUDebugBuffer rotated;

	// This is a simple but not terribly efficient rotation.
	if (rotation == DisplayRotation::ROTATE_90) {
		rotated.Allocate(buf.GetHeight(), buf.GetStride(), buf.GetFormat(), false);
		for (u32 y = 0; y < buf.GetStride(); ++y) {
			for (u32 x = 0; x < buf.GetHeight(); ++x) {
				rotated.SetRawPixel(x, y, buf.GetRawPixel(buf.GetStride() - y - 1, x));
			}
		}
	} else if (rotation == DisplayRotation::ROTATE_180) {
		rotated.Allocate(buf.GetStride(), buf.GetHeight(), buf.GetFormat(), false);
		for (u32 y = 0; y < buf.GetHeight(); ++y) {
			for (u32 x = 0; x < buf.GetStride(); ++x) {
				rotated.SetRawPixel(x, y, buf.GetRawPixel(buf.GetStride() - x - 1, buf.GetHeight() - y - 1));
			}
		}
	} else {
		rotated.Allocate(buf.GetHeight(), buf.GetStride(), buf.GetFormat(), false);
		for (u32 y = 0; y < buf.GetStride(); ++y) {
			for (u32 x = 0; x < buf.GetHeight(); ++x) {
				rotated.SetRawPixel(x, y, buf.GetRawPixel(y, buf.GetHeight() - x - 1));
			}
		}
	}
	rotated.SetIsBackbuffer(buf.IsBackBuffer());
	return rotated;
}

static void SaveScreenshotAsync(GPUDebugBuffer &&buf, int w, int h, int maxRes);

void ScheduleScreenshot(const Path &filename, ScreenshotFormat fmt, ScreenshotType type, int maxRes, std::function<void(ScreenshotResult)> &&callback) {
	g_pendingScreenshot = {};
	g_pendingScreenshot.path = filename;
	g_pendingScreenshot.format = fmt;
	g_pendingScreenshot.type = type;
	g_pendingScreenshot.callback = std::move(callback);
	g_pendingScreenshot.maxRes = maxRes;
}

bool ScreenshotNotifyEndOfFrame(Draw::DrawContext *draw) {
	if (g_pendingScreenshot.path.empty() || g_pendingScreenshot.type != ScreenshotType::Output) {
		return false;
	}

	_dbg_assert_(draw);

	GPUDebugBuffer buf;
	if (g_display.rotation != DisplayRotation::ROTATE_0) {
		GPUDebugBuffer temp;
		if (!::GetOutputFramebuffer(draw, temp)) {
			g_pendingScreenshot.callback(ScreenshotResult::ScreenshotNotPossible);
			g_pendingScreenshot = {};
			return false;
		}
		_dbg_assert_msg_(temp.IsBackBuffer(), "temp isn't backbuffer??");
		buf = ApplyRotation(temp, g_display.rotation);
	} else {
		if (!::GetOutputFramebuffer(draw, buf)) {
			g_pendingScreenshot.callback(ScreenshotResult::ScreenshotNotPossible);
			g_pendingScreenshot = {};
			return false;
		}
	}
	_dbg_assert_msg_(buf.IsBackBuffer(), "buf isn't backbuffer?? rotation: %d", g_display.rotation);
	const int w = buf.GetStride();
	const int h = buf.GetHeight();
	SaveScreenshotAsync(std::move(buf), w, h, g_pendingScreenshot.maxRes);
	// Wipe the screenshot state.
	g_pendingScreenshot = {};
	return true;
}

bool ScreenshotNotifyPostGameRender(Draw::DrawContext *draw) {
	if (g_pendingScreenshot.path.empty() || g_pendingScreenshot.type == ScreenshotType::Output) {
		return false;
	}

	GPUDebugBuffer buf;
	if (!gpuDebug) {
		ERROR_LOG(Log::System, "Can't take screenshots when GPU not running");
		g_pendingScreenshot.callback(ScreenshotResult::ScreenshotNotPossible);
		g_pendingScreenshot = {};
		return false;
	}

	const int maxRes = g_pendingScreenshot.maxRes;

	if (!gpuDebug->GetCurrentFramebuffer(buf, g_pendingScreenshot.type == ScreenshotType::Render ? GPU_DBG_FRAMEBUF_RENDER : GPU_DBG_FRAMEBUF_DISPLAY, maxRes)) {
		g_pendingScreenshot.callback(ScreenshotResult::ScreenshotNotPossible);
		g_pendingScreenshot = {};
		return false;
	}

	int w;
	int h;
	if (buf.IsBackBuffer()) {
		w = buf.GetStride();
		h = buf.GetHeight();
	} else {
		w = maxRes > 0 ? 480 * maxRes : buf.GetStride();
		h = maxRes > 0 ? 272 * maxRes : buf.GetHeight();
	}

	SaveScreenshotAsync(std::move(buf), w, h, g_pendingScreenshot.maxRes);
	// Wipe the screenshot state.
	g_pendingScreenshot = {};
	return true;
}

static void SaveScreenshotAsync(GPUDebugBuffer &&buf, int w, int h, int maxRes) {
	g_threadManager.EnqueueTask(new IndependentTask(TaskType::IO_BLOCKING, TaskPriority::LOW,
		[filename = std::move(g_pendingScreenshot.path), buf = std::move(buf), callback = std::move(g_pendingScreenshot.callback), fmt = g_pendingScreenshot.format, w, h, maxRes]() {
		u8 *flipbuffer = nullptr;
		u32 width = w, height = h;
		const u8 *buffer = ConvertBufferToScreenshot(buf, false, flipbuffer, width, height);

		ScreenshotResult result;
		if (maxRes <= 0 || (int)width <= 480 * maxRes) {
			result = Save888RGBScreenshot(filename, fmt, buffer, width, height) ? ScreenshotResult::Success : ScreenshotResult::FailedToWriteFile;
			delete[] flipbuffer;
		} else {
			u8 *shrinkBuffer = new u8[width * height * 3];
			memcpy(shrinkBuffer, buffer, width * height * 3);
			delete[] flipbuffer;

			// TODO: Speed this thing up.
			while ((int)width > 480 * maxRes) {
				u8 *halfSize = new u8[(width / 2) * (height / 2) * 3];
				for (u32 y = 0; y < height / 2; y++) {
					for (u32 x = 0; x < width / 2; x++) {
						for (int c = 0; c < 3; c++) {
							halfSize[(y * (width / 2) + x) * 3 + c] = (shrinkBuffer[((y * 2) * width + (x * 2)) * 3 + c] +
									shrinkBuffer[((y * 2) * width + (x * 2 + 1)) * 3 + c] +
									shrinkBuffer[(((y * 2) + 1) * width + (x * 2)) * 3 + c] +
									shrinkBuffer[(((y * 2) + 1) * width + (x * 2 + 1)) * 3 + c]) / 4;
						}
					}
				}
				std::swap(shrinkBuffer, halfSize);
				delete[] halfSize;
				width /= 2;
				height /= 2;
			}
			result = Save888RGBScreenshot(filename, fmt, shrinkBuffer, width, height) ? ScreenshotResult::Success : ScreenshotResult::FailedToWriteFile;
		}

		System_RunOnMainThread([result, callback = std::move(callback)]() {
			callback(result);
		});
	}));
}

bool Save888RGBScreenshot(const Path &filename, ScreenshotFormat fmt, const u8 *bufferRGB888, int w, int h) {
	if (fmt == ScreenshotFormat::PNG) {
		return pngSave(filename, bufferRGB888, w, h, 3);
	} else if (fmt == ScreenshotFormat::JPG) {
		jpge::params params;
		params.m_quality = 90;
		return WriteScreenshotToJPEG(filename, w, h, 3, bufferRGB888, params);
	} else {
		return false;
	}
}

bool Save8888RGBAScreenshot(const Path &filename, const u8 *buffer, int w, int h) {
	return pngSave(filename, buffer, w, h, 4);
}

bool Save8888RGBAScreenshot(std::vector<uint8_t> &bufferPNG, const u8 *bufferRGBA8888, int w, int h) {
	png_image png{};
	png.version = PNG_IMAGE_VERSION;
	png.format = PNG_FORMAT_RGBA;
	png.width = w;
	png.height = h;

	png_alloc_size_t allocSize = bufferPNG.size();
	int result = png_image_write_to_memory(&png, allocSize == 0 ? nullptr : bufferPNG.data(), &allocSize, 0, bufferRGBA8888, w * 4, nullptr);
	bool success = result != 0 && png.warning_or_error <= 1;
	if (!success && allocSize != bufferPNG.size()) {
		bufferPNG.resize(allocSize);
		png.warning_or_error = 0;
		result = png_image_write_to_memory(&png, bufferPNG.data(), &allocSize, 0, bufferRGBA8888, w * 4, nullptr);
		success = result != 0 && png.warning_or_error <= 1;
	}
	if (success)
		bufferPNG.resize(allocSize);
	png_image_free(&png);

	if (!success) {
		ERROR_LOG(Log::IO, "Buffering screenshot to PNG produced errors.");
		bufferPNG.clear();
	}
	return success;
}

void TakeUserScreenshotImpl() {
	Path path = GetSysDirectory(DIRECTORY_SCREENSHOT);
	// Make sure the screenshot directory exists.
	File::CreateDir(path);

	// First, find a free filename.
	//
	// NOTE: On Android, the old approach of checking filenames one by one doesn't scale.
	// So let's just grab the full file listing, and then find a name that's not in it.
	//
	// TODO: Also, we could do this on a thread too. Not sure if worth it.

	const std::string gameId = g_paramSFO.GetDiscID();

	std::vector<File::FileInfo> files;
	const std::string prefix = gameId + "_";
	File::GetFilesInDir(path, &files, nullptr, 0, prefix);
	std::set<std::string> existingNames;
	for (auto &file : files) {
		existingNames.insert(file.name);
	}

	Path filename;
	int i = 0;
	for (int i = 0; i < 20000; i++) {
		const std::string pngName = prefix + StringFromFormat("%05d.png", i);
		const std::string jpgName = prefix + StringFromFormat("%05d.jpg", i);
		if (existingNames.find(pngName) == existingNames.end() && existingNames.find(jpgName) == existingNames.end()) {
			filename = path / (g_Config.bScreenshotsAsPNG ? pngName : jpgName);
			break;
		}
	}

	if (filename.empty()) {
		// Overwrite this one over and over.
		filename = path / (prefix + (g_Config.bScreenshotsAsPNG ? "20000.png" : "20000.jpg"));
	}

	ScreenshotType type = g_Config.iScreenshotMode == (int)ScreenshotMode::GameImage ? ScreenshotType::Display : ScreenshotType::Output;

	if (GetUIState() != UISTATE_INGAME) {
		// We're out in the UI, no game. The only type of screenshot available is output.
		type = ScreenshotType::Output;
	}

	ScheduleScreenshot(filename, g_Config.bScreenshotsAsPNG ? ScreenshotFormat::PNG : ScreenshotFormat::JPG, type, -1, [filename](ScreenshotResult result) {
		if (result == ScreenshotResult::Success) {
			const std::string friendlyPath = GetFriendlyPath(filename);
			g_OSD.Show(OSDType::MESSAGE_FILE_LINK, friendlyPath, 0.0f, "screenshot_link");
			if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
				g_OSD.SetClickCallback("screenshot_link", [filename]() -> void {
					System_ShowFileInFolder(filename);
				});
			}
		} else if (result == ScreenshotResult::FailedToWriteFile) {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Could not save screenshot file"));
			WARN_LOG(Log::System, "Failed to take screenshot.");
		}
		// TODO: What to do about ScreenshotNotPossible?
	});
}

void TakeUserScreenshot() {
	System_RunOnMainThread([]() {
		TakeUserScreenshotImpl();
	});
}
