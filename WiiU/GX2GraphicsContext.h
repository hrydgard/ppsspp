#pragma once

#include "Common/GraphicsContext.h"
#include <wiiu/gx2.h>

class GX2GraphicsContext : public GraphicsContext {
public:
	GX2GraphicsContext() {}

	bool Init();

	void Shutdown() override;
	void SwapBuffers() override;
	virtual void SwapInterval(int interval) override { GX2SetSwapInterval(interval); }
	virtual void Resize() override {}

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	typedef struct {
		int width;
		int height;
		GX2TVRenderMode mode;
	} RenderMode;
	Draw::DrawContext *draw_ = nullptr;
	void *cmd_buffer_;
	RenderMode render_mode_;
	void *drc_scan_buffer_;
	void *tv_scan_buffer_;
	GX2ColorBuffer color_buffer_ = {};
	GX2DepthBuffer depth_buffer_ = {};
	GX2ContextState *ctx_state_;
};
