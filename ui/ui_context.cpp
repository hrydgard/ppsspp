#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"
#include "gfx/texture.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"

UIContext::UIContext()
	: uishader_(0), uitexture_(0), uidrawbuffer_(0), uidrawbufferTop_(0) {
	fontScaleX_ = 1.0f;
	fontScaleY_ = 1.0f;
	fontStyle_ = new UI::FontStyle();
}

UIContext::~UIContext() {
	delete fontStyle_;
	delete textDrawer_;
}

void UIContext::Init(const GLSLProgram *uishader, const GLSLProgram *uishadernotex, Texture *uitexture, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop) {
	uishader_ = uishader;
	uishadernotex_ = uishadernotex;
	uitexture_ = uitexture;
	uidrawbuffer_ = uidrawbuffer;
	uidrawbufferTop_ = uidrawbufferTop;
#if defined(_WIN32) || defined(USING_QT_UI)
	textDrawer_ = new TextDrawer();
#else
	textDrawer_ = 0;
#endif
}

void UIContext::Begin() {
	glstate.blend.enable();
	glstate.blendFuncSeparate.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.dither.enable();
#if !defined(USING_GLES2)
	glstate.colorLogicOp.disable();
#endif
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin(uishader_);
}

void UIContext::BeginNoTex() {
	glstate.blend.enable();
	glstate.blendFuncSeparate.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.dither.enable();
#if !defined(USING_GLES2)
	glstate.colorLogicOp.disable();
#endif
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin(uishadernotex_);
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
	Bounds clipped = bounds;
	if (scissorStack_.size())
		clipped.Clip(scissorStack_.back());
	scissorStack_.push_back(clipped);
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

void UIContext::SetFontScale(float scaleX, float scaleY) {
	fontScaleX_ = scaleX;
	fontScaleY_ = scaleY;
}

void UIContext::SetFontStyle(const UI::FontStyle &fontStyle) {
	*fontStyle_ = fontStyle;
	if (textDrawer_) {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		Text()->SetFont(fontStyle.fontName.c_str(), fontStyle.sizePts, fontStyle.flags);
	}
}

void UIContext::MeasureText(const UI::FontStyle &style, const char *str, float *x, float *y, int align) const {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)style.sizePts / 24.0f;
		Draw()->SetFontScale(fontScaleX_ * sizeFactor, fontScaleY_ * sizeFactor);
		Draw()->MeasureText(style.atlasFont, str, x, y);
	} else {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		textDrawer_->MeasureString(str, x, y);
	}
}

void UIContext::DrawText(const char *str, float x, float y, uint32_t color, int align) {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)fontStyle_->sizePts / 24.0f;
		Draw()->SetFontScale(fontScaleX_ * sizeFactor, fontScaleY_ * sizeFactor);
		Draw()->DrawText(fontStyle_->atlasFont, str, x, y, color, align);
	} else {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		textDrawer_->DrawString(*Draw(), str, x, y, color, align);
		RebindTexture();
	}
}

void UIContext::DrawTextRect(const char *str, const Bounds &bounds, uint32_t color, int align) {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)fontStyle_->sizePts / 24.0f;
		Draw()->SetFontScale(fontScaleX_ * sizeFactor, fontScaleY_ * sizeFactor);
		Draw()->DrawTextRect(fontStyle_->atlasFont, str, bounds.x, bounds.y, bounds.w, bounds.h, color, align);
	} else {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		textDrawer_->DrawStringRect(*Draw(), str, bounds, color, align);
		RebindTexture();
	}
}

void UIContext::FillRect(const UI::Drawable &drawable, const Bounds &bounds) {
	// Only draw if alpha is non-zero.
	if ((drawable.color & 0xFF000000) == 0)
		return;

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
	case UI::DRAW_NOTHING:
		break;
	} 
}
