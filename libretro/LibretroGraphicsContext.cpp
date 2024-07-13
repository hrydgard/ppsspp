
#include "libretro/LibretroGraphicsContext.h"
#include "libretro/LibretroGLContext.h"
#include "libretro/LibretroGLCoreContext.h"
#include "libretro/LibretroVulkanContext.h"
#ifdef _WIN32
#include "libretro/LibretroD3D11Context.h"
#endif

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"

retro_video_refresh_t LibretroGraphicsContext::video_cb;

extern "C" {
   retro_hw_get_proc_address_t libretro_get_proc_address;
};

void retro_set_video_refresh(retro_video_refresh_t cb) { LibretroGraphicsContext::video_cb = cb; }
static void context_reset() { ((LibretroHWRenderContext *)Libretro::ctx)->ContextReset(); }
static void context_destroy() { ((LibretroHWRenderContext *)Libretro::ctx)->ContextDestroy(); }

bool LibretroHWRenderContext::Init(bool cache_context) {
	hw_render_.cache_context = cache_context;
	if (!Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render_))
		return false;
	libretro_get_proc_address = hw_render_.get_proc_address;
	return true;
}

LibretroHWRenderContext::LibretroHWRenderContext(retro_hw_context_type context_type, unsigned version_major, unsigned version_minor) {
	hw_render_.context_type = context_type;
	hw_render_.version_major = version_major;
	hw_render_.version_minor = version_minor;
	hw_render_.context_reset = context_reset;
	hw_render_.context_destroy = context_destroy;
	hw_render_.depth = true;
}

void LibretroHWRenderContext::ContextReset() {
	INFO_LOG(Log::G3D, "Context reset");

	if (gpu && Libretro::useEmuThread) {
		Libretro::EmuThreadPause();
	}

	if (gpu) {
		gpu->DeviceLost();
	}

	if (!draw_) {
		CreateDrawContext();
		bool success = draw_->CreatePresets();
		_assert_(success);
	}

	GotBackbuffer();

	if (gpu) {
		gpu->DeviceRestore(draw_);
	}

	if (gpu && Libretro::useEmuThread) {
		Libretro::EmuThreadStart();
	}
}

void LibretroHWRenderContext::ContextDestroy() {
	INFO_LOG(Log::G3D, "Context destroy");

	if (Libretro::useEmuThread) {
		Libretro::EmuThreadStop();
	}

	if (gpu) {
		gpu->DeviceLost();
	}

	if (!hw_render_.cache_context && Libretro::useEmuThread && draw_ && Libretro::emuThreadState != Libretro::EmuThreadState::PAUSED) {
		DestroyDrawContext();
	}

	if (!hw_render_.cache_context && !Libretro::useEmuThread) {
		Shutdown();
	}
}

void LibretroGraphicsContext::GotBackbuffer() { draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight); }

void LibretroGraphicsContext::LostBackbuffer() { draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, -1, -1); }

LibretroGraphicsContext *LibretroGraphicsContext::CreateGraphicsContext() {
	LibretroGraphicsContext *ctx;

	retro_hw_context_type preferred;
	if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred))
		preferred = RETRO_HW_CONTEXT_DUMMY;

	if (Libretro::backend != RETRO_HW_CONTEXT_DUMMY)
		preferred = Libretro::backend;

#ifndef USING_GLES2
	if (preferred == RETRO_HW_CONTEXT_DUMMY || preferred == RETRO_HW_CONTEXT_OPENGL_CORE) {
		ctx = new LibretroGLCoreContext();

		if (ctx->Init()) {
			return ctx;
		}
		delete ctx;
	}
#endif

	if (preferred == RETRO_HW_CONTEXT_DUMMY || preferred == RETRO_HW_CONTEXT_OPENGL || preferred == RETRO_HW_CONTEXT_OPENGLES3) {
		ctx = new LibretroGLContext();

		if (ctx->Init()) {
			return ctx;
		}
		delete ctx;
	}

#ifndef HAVE_LIBNX
	if (preferred == RETRO_HW_CONTEXT_DUMMY || preferred == RETRO_HW_CONTEXT_VULKAN) {
		ctx = new LibretroVulkanContext();

		if (ctx->Init()) {
			return ctx;
		}
		delete ctx;
	}
#endif

#ifdef _WIN32
	if (preferred == RETRO_HW_CONTEXT_DUMMY || preferred == RETRO_HW_CONTEXT_DIRECT3D) {
		ctx = new LibretroD3D11Context();

		if (ctx->Init()) {
			return ctx;
		}
		delete ctx;

		ctx = new LibretroD3D9Context();
		if (ctx->Init()) {
			return ctx;
		}
		delete ctx;
	}
#endif

	ctx = new LibretroSoftwareContext();
	ctx->Init();
	return ctx;
}

std::vector<u32> TranslateDebugBufferToCompare(const GPUDebugBuffer *buffer, u32 stride, u32 h) {
	// If the output was small, act like everything outside was 0.
	// This can happen depending on viewport parameters.
	u32 safeW = std::min(stride, buffer->GetStride());
	u32 safeH = std::min(h, buffer->GetHeight());

	std::vector<u32> data;
	data.resize(stride * h, 0);

	const u32 *pixels32 = (const u32 *)buffer->GetData();
	const u16 *pixels16 = (const u16 *)buffer->GetData();
	int outStride = buffer->GetStride();
#if 0
	if (!buffer->GetFlipped()) {
		// Bitmaps are flipped, so we have to compare backwards in this case.
		int toLastRow = outStride * (h > buffer->GetHeight() ? buffer->GetHeight() - 1 : h - 1);
		pixels32 += toLastRow;
		pixels16 += toLastRow;
		outStride = -outStride;
	}
#endif
	// Skip the bottom of the image in the buffer was smaller.  Remember, we're flipped.
	u32 *dst = &data[0];
	if (safeH < h) {
		dst += (h - safeH) * stride;
	}

	for (u32 y = 0; y < safeH; ++y) {
		switch (buffer->GetFormat()) {
		case GPU_DBG_FORMAT_8888:
			ConvertBGRA8888ToRGBA8888(&dst[y * stride], pixels32, safeW);
			break;
		case GPU_DBG_FORMAT_8888_BGRA:
			memcpy(&dst[y * stride], pixels32, safeW * sizeof(u32));
			break;

		case GPU_DBG_FORMAT_565:
			ConvertRGB565ToBGRA8888(&dst[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_5551:
			ConvertRGBA5551ToBGRA8888(&dst[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_4444:
			ConvertRGBA4444ToBGRA8888(&dst[y * stride], pixels16, safeW);
			break;

		default:
			data.resize(0);
			return data;
		}

		pixels32 += outStride;
		pixels16 += outStride;
	}

	return data;
}
