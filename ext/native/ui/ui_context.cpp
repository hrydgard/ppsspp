#include "base/display.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/draw_text.h"

UIContext::UIContext()
	: ui_pipeline_(0), uitexture_(0), uidrawbuffer_(0), uidrawbufferTop_(0) {
	fontScaleX_ = 1.0f;
	fontScaleY_ = 1.0f;
	fontStyle_ = new UI::FontStyle();
	bounds_ = Bounds(0, 0, dp_xres, dp_yres);
}

UIContext::~UIContext() {
	sampler_->Release();
	delete fontStyle_;
	delete textDrawer_;
}

void UIContext::Init(Draw::DrawContext *thin3d, Draw::Pipeline *uipipe, Draw::Pipeline *uipipenotex, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop) {
	using namespace Draw;
	draw_ = thin3d;
	sampler_ = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR });
	ui_pipeline_ = uipipe;
	ui_pipeline_notex_ = uipipenotex;
	uidrawbuffer_ = uidrawbuffer;
	uidrawbufferTop_ = uidrawbufferTop;
#if defined(_WIN32) || defined(USING_QT_UI)
	textDrawer_ = new TextDrawer(thin3d);
#else
	textDrawer_ = nullptr;
#endif
}

void UIContext::FrameSetup(Draw::Texture *uiTexture) {
	uitexture_ = uiTexture;
}

void UIContext::Begin() {
	draw_->BindSamplerStates(0, 1, &sampler_);
	draw_->BindTexture(0, uitexture_);
	ActivateTopScissor();
	UIBegin(ui_pipeline_);
}

void UIContext::BeginNoTex() {
	draw_->BindSamplerStates(0, 1, &sampler_);
	UIBegin(ui_pipeline_notex_);
}

void UIContext::RebindTexture() const {
	draw_->BindTexture(0, uitexture_);
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
		return bounds_;
}

void UIContext::ActivateTopScissor() {
	Bounds bounds;
	if (scissorStack_.size()) {
		bounds = scissorStack_.back();
	}
	else {
		bounds = bounds_;
	}
	float scale = pixel_in_dps;
	int x = scale * bounds.x;
	int y = scale * bounds.y;
	int w = scale * bounds.w;
	int h = scale * bounds.h;
	draw_->SetScissorRect(x, y, w, h);
}

void UIContext::SetFontScale(float scaleX, float scaleY) {
	fontScaleX_ = scaleX;
	fontScaleY_ = scaleY;
}

void UIContext::SetFontStyle(const UI::FontStyle &fontStyle) {
	*fontStyle_ = fontStyle;
	if (textDrawer_) {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		textDrawer_->SetFont(fontStyle.fontName.c_str(), fontStyle.sizePts, fontStyle.flags);
	}
}

void UIContext::MeasureText(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, float *x, float *y, int align) const {
	MeasureTextCount(style, scaleX, scaleY, str, (int)strlen(str), x, y, align);
}

void UIContext::MeasureTextCount(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, int count, float *x, float *y, int align) const {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)style.sizePts / 24.0f;
		Draw()->SetFontScale(scaleX * sizeFactor, scaleY * sizeFactor);
		Draw()->MeasureTextCount(style.atlasFont, str, count, x, y);
	} else {
		textDrawer_->SetFont(style.fontName.c_str(), style.sizePts, style.flags);
		textDrawer_->SetFontScale(scaleX, scaleY);
		textDrawer_->MeasureString(str, count, x, y);
		textDrawer_->SetFont(fontStyle_->fontName.c_str(), fontStyle_->sizePts, fontStyle_->flags);
	}
}

void UIContext::MeasureTextRect(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, int count, const Bounds &bounds, float *x, float *y, int align) const {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)style.sizePts / 24.0f;
		Draw()->SetFontScale(scaleX * sizeFactor, scaleY * sizeFactor);
		Draw()->MeasureTextRect(style.atlasFont, str, count, bounds, x, y, align);
	} else {
		textDrawer_->SetFont(style.fontName.c_str(), style.sizePts, style.flags);
		textDrawer_->SetFontScale(scaleX, scaleY);
		textDrawer_->MeasureStringRect(str, count, bounds, x, y, align);
		textDrawer_->SetFont(fontStyle_->fontName.c_str(), fontStyle_->sizePts, fontStyle_->flags);
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

void UIContext::DrawTextShadow(const char *str, float x, float y, uint32_t color, int align) {
	uint32_t alpha = (color >> 1) & 0xFF000000;
	DrawText(str, x + 2, y + 2, alpha, align);
	DrawText(str, x, y, color, align);
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
