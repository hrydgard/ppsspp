#include "base/display.h"
#include "ui/drawing.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/gl_state.h"

DrawContext::DrawContext() {

}

DrawContext::~DrawContext() {

}

// TODO: Support transformed bounds using stencil
void DrawContext::PushScissor(const Bounds &bounds) {
	Flush();
	scissorStack_.push_back(bounds);
	ActivateTopScissor();
}

void DrawContext::PopScissor() {
	Flush();
	scissorStack_.pop_back();
	ActivateTopScissor();
}

void DrawContext::ActivateTopScissor() {
	if (scissorStack_.size()) {
		const Bounds &bounds = scissorStack_.back();
		int x = g_dpi_scale * bounds.x;
		int y = g_dpi_scale * (dp_yres - bounds.y2());
		int w = g_dpi_scale * bounds.w;
		int h = g_dpi_scale * bounds.h;

		glstate.scissorRect.set(x,y,w,h);
		glstate.scissorTest.enable();
	} else {
		glstate.scissorTest.disable();
	}
}

void DrawContext::Flush() {
	draw->Flush(true);
	drawTop->Flush(true);
}