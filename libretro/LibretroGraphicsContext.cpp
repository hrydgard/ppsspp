
#include "libretro/libretro.h"
#include "libretro/LibretroGraphicsContext.h"
#include "libretro/LibretroGLContext.h"
#ifndef NO_VULKAN
#include "libretro/LibretroVulkanContext.h"
#endif

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"

retro_video_refresh_t LibretroGraphicsContext::video_cb;
retro_hw_get_proc_address_t libretro_get_proc_address;

void retro_set_video_refresh(retro_video_refresh_t cb) { LibretroGraphicsContext::video_cb = cb; }
static void context_reset() { ((LibretroHWRenderContext *)Libretro::ctx)->ContextReset(); }
static void context_destroy() { ((LibretroHWRenderContext *)Libretro::ctx)->ContextDestroy(); }

bool LibretroHWRenderContext::Init() { return Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render_); }

LibretroHWRenderContext::LibretroHWRenderContext(retro_hw_context_type context_type, unsigned version_major, unsigned version_minor)
{
	hw_render_.context_type = context_type;
	hw_render_.version_major = version_major;
	hw_render_.version_minor = version_minor;
	hw_render_.context_reset = context_reset;
	hw_render_.context_destroy = context_destroy;
	hw_render_.depth = true;
}

void LibretroHWRenderContext::ContextReset()
{
	INFO_LOG(G3D, "Context reset");

	//	 needed to restart the thread
	//	 TODO: find a way to move this to ContextDestroy.
	if (Libretro::useEmuThread && draw_ && Libretro::emuThreadState != Libretro::EmuThreadState::PAUSED)
		DestroyDrawContext();

	if (!draw_)
	{
		CreateDrawContext();
		PSP_CoreParameter().thin3d = draw_;
		draw_->CreatePresets();
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
	}

	if (gpu)
		gpu->DeviceRestore();
}

void LibretroHWRenderContext::ContextDestroy()
{
	INFO_LOG(G3D, "Context destroy");

	if (Libretro::useEmuThread)
	{
#if 0
		Libretro::EmuThreadPause();
#else
		Libretro::EmuThreadStop();
#endif
	}

	gpu->DeviceLost();
}

LibretroGraphicsContext *LibretroGraphicsContext::CreateGraphicsContext()
{
	LibretroGraphicsContext *ctx;

	ctx = new LibretroGLContext();

	if (ctx->Init())
		return ctx;

	delete ctx;

#ifndef NO_VULKAN
	ctx = new LibretroVulkanContext();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

#ifdef _WIN32
	ctx = new LibretroD3D11Context();

	if (ctx->Init())
		return ctx;

	delete ctx;

	ctx = new LibretroD3D9Context();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

#if 1
	ctx = new LibretroSoftwareContext();

	if (ctx->Init())
		return ctx;

	delete ctx;
#endif

	return new LibretroNullContext();
}
