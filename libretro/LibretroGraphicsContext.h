#pragma once
#include <retro_atomic.h>

#include <libretro.h>
#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/thin3d_create.h"

#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/Software/SoftGpu.h"
#include "Common/Data/Convert/ColorConv.h"

#define NATIVEWIDTH  480
#define NATIVEHEIGHT 272

class LibretroGraphicsContext : public GraphicsContext {
public:
	LibretroGraphicsContext() {}
	~LibretroGraphicsContext() override { ShutdownAPI(); }

	virtual void SetRenderTarget() {}
	virtual GPUCore GetGPUCore() = 0;
	virtual const char *Ident() = 0;

	void ShutdownAPI() override {
		DestroyDrawContext();
	}
	virtual void SwapBuffers() = 0;
	void Resize() override {}
	// Emu thread backends only: the emu thread queues a marker once parked, and
	// the frontend thread runs the queue up to it. False means the emu thread exited.
	virtual void NotifyEmuThreadPaused() {}
	virtual bool ThreadRunUntilPaused() { return true; }

	virtual void GotBackbuffer();
	virtual void LostBackbuffer();

	// Backends whose context the frontend shares by taking turns (D3D11
	// interface version 2). FrameBegin takes it for the frame and
	// SwapBuffers gives it back; LockGPU/UnlockGPU bracket GPU work
	// outside a frame. Recursive. No-ops elsewhere.
	virtual void FrameBegin() {}
	virtual void LockGPU() {}
	virtual void UnlockGPU() {}

	virtual void CreateDrawContext() {}
	virtual void DestroyDrawContext() {
		if (!draw_) {
			return;
		}
		delete draw_;
		draw_ = nullptr;
	}
	Draw::DrawContext *GetDrawContext() override { return draw_; }

	static LibretroGraphicsContext *CreateGraphicsContext();

	static retro_video_refresh_t video_cb;

protected:
	Draw::DrawContext *draw_ = nullptr;
};

class LibretroHWRenderContext : public LibretroGraphicsContext {
public:
	LibretroHWRenderContext(retro_hw_context_type context_type, unsigned version_major = 0, unsigned version_minor = 0);
	bool InitHW(bool cache_context);
	void SetRenderTarget() override {}
	void SwapBuffers() override {
		video_cb(RETRO_HW_FRAME_BUFFER_VALID, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, 0);
	}
	virtual void ContextReset();
	virtual void ContextDestroy();

protected:
	retro_hw_render_callback hw_render_ = {};
};

bool ConvertFramebufferForLibretro(const GPUDebugBuffer *buffer, u32 stride, u32 h, std::vector<u32> &data);

class LibretroSoftwareContext : public LibretroGraphicsContext {
public:
	LibretroSoftwareContext() {}
	void SwapBuffers() override {
		u32 w = NATIVEWIDTH;
		u32 h = NATIVEHEIGHT;
		if (!gpu) {
			// Nothing rendered yet, dupe the previous frame.
			video_cb(NULL, w, h, w * sizeof(u32));
			return;
		}
		GPUDebugBuffer buf;
		gpu->GetOutputFramebuffer(buf);
		// video_cb is synchronous; keep backing storage across calls and hand
		// the converted pixels over without another frame-sized allocation.
		if (!ConvertFramebufferForLibretro(&buf, w, h, pixels_)) {
			// Unsupported debug buffer format, conversion returned nothing.
			video_cb(NULL, w, h, w * sizeof(u32));
			return;
		}
		const u32 *data = pixels_.data();
		if (g_Config.bDisplayCropTo16x9) {
			// 480x272 -> 480x270: skip the first row, drop the last.
			data += w;
			h -= 2;
		}
		video_cb(data, w, h, w * sizeof(u32));
	}
	GPUCore GetGPUCore() override { return GPUCORE_SOFTWARE; }
	const char *Ident() override { return "Software"; }
private:
	std::vector<u32> pixels_;
};

namespace Libretro {
extern LibretroGraphicsContext *ctx;
extern retro_environment_t environ_cb;
extern retro_hw_context_type backend;

enum class EmuThreadState {
	DISABLED,
	RUNNING,
	PAUSE_REQUESTED,
	PAUSED,
	QUIT_REQUESTED,
	STOPPED,
};
extern bool useEmuThread;
extern retro_atomic_int_t emuThreadState;
inline EmuThreadState EmuThreadStateGet() { return (EmuThreadState)retro_atomic_load_acquire_int(&emuThreadState); }
inline void EmuThreadStateSet(EmuThreadState state) { retro_atomic_store_release_int(&emuThreadState, (int)state); }
void EmuThreadStart();
void EmuThreadStop();
void EmuThreadPause();
} // namespace Libretro
