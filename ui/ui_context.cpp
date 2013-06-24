#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"
#include "gfx/texture.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"

void UIContext::Begin() {
	glstate.blend.enable();
	glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin(uishader_);
	/*
	if (uidrawbuffer_ && uishader_)
		uidrawbuffer_->Begin();
	if (uidrawbufferTop_ && uishader_)
		uidrawbufferTop_->Begin();*/
}

void UIContext::BeginNoTex() {
	glstate.blend.enable();
	glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin(uishadernotex_);
	/*
	if (uidrawbuffer_ && uishader_)
		uidrawbuffer_->Begin();
	if (uidrawbufferTop_ && uishader_)
		uidrawbufferTop_->Begin();*/
}


void UIContext::RebindTexture() const {
	if (uitexture_)
		uitexture_->Bind(0);
}

void UIContext::Flush() {
	if (uidrawbuffer_) {
		uidrawbuffer_->End();
		uidrawbuffer_->Flush();
	}
	if (uidrawbufferTop_) {
		uidrawbufferTop_->End();
		uidrawbufferTop_->Flush();
	}
}

void UIContext::End() {
	UIEnd();
	Flush();
}

// TODO: Support transformed bounds using stencil
void UIContext::PushScissor(const Bounds &bounds) {
	Flush();
	scissorStack_.push_back(bounds);
	ActivateTopScissor();
}

void UIContext::PopScissor() {
	Flush();
	scissorStack_.pop_back();
	ActivateTopScissor();
}

Bounds UIContext::GetScissorBounds() {
	if (!scissorStack_.empty())
		return scissorStack_.back();
	else
		return Bounds(0, 0, dp_xres, dp_yres);
}


void UIContext::ActivateTopScissor() {
	if (scissorStack_.size()) {
		const Bounds &bounds = scissorStack_.back();
		float scale = 1.0f / g_dpi_scale;
		int x = scale * bounds.x;
		int y = scale * (dp_yres - bounds.y2());
		int w = scale * bounds.w;
		int h = scale * bounds.h;

		glstate.scissorRect.set(x,y,w,h);
		glstate.scissorTest.enable();
	} else {
		glstate.scissorTest.disable();
	}
}

void UIContext::FillRect(const UI::Drawable &drawable, const Bounds &bounds) {
	switch (drawable.type) {
	case UI::DRAW_SOLID_COLOR:
		uidrawbuffer_->DrawImageStretch(theme->whiteImage, bounds.x, bounds.y, bounds.x2(), bounds.y2(), drawable.color);
		break;
	case UI::DRAW_4GRID:
		uidrawbuffer_->DrawImage4Grid(drawable.image, bounds.x, bounds.y, bounds.x2(), bounds.y2(), drawable.color);
		break;
	case UI::DRAW_STRETCH_IMAGE:
		uidrawbuffer_->DrawImageStretch(drawable.image, bounds.x, bounds.y, bounds.x2(), bounds.y2(), drawable.color);
		break;
	} 
}