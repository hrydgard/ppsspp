#include "base/display.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/draw_text.h"

UIContext::UIContext()
	: uishader_(0), uitexture_(0), uidrawbuffer_(0), uidrawbufferTop_(0) {
	fontScaleX_ = 1.0f;
	fontScaleY_ = 1.0f;
	fontStyle_ = new UI::FontStyle();
	bounds_ = Bounds(0, 0, dp_xres, dp_yres);
}

UIContext::~UIContext() {
	delete fontStyle_;
	delete textDrawer_;
	// Not releasing blend_, it's a preset. Should really make them AddRef, though..
	depth_->Release();
}

void UIContext::Init(Thin3DContext *thin3d, Thin3DShaderSet *uishader, Thin3DShaderSet *uishadernotex, Thin3DTexture *uitexture, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop) {
	thin3d_ = thin3d;
	blend_ = thin3d_->GetBlendStatePreset(T3DBlendStatePreset::BS_STANDARD_ALPHA);
	sampler_ = thin3d_->GetSamplerStatePreset(T3DSamplerStatePreset::SAMPS_LINEAR);
	depth_ = thin3d_->CreateDepthStencilState(false, false, T3DComparison::LESS);

	uishader_ = uishader;
	uishadernotex_ = uishadernotex;
	uitexture_ = uitexture;
	uidrawbuffer_ = uidrawbuffer;
	uidrawbufferTop_ = uidrawbufferTop;
#if defined(_WIN32) || defined(USING_QT_UI)
	textDrawer_ = new TextDrawer(thin3d);
#else
	textDrawer_ = nullptr;
#endif
}

void UIContext::Begin() {
	thin3d_->SetBlendState(blend_);
	thin3d_->SetSamplerStates(0, 1, &sampler_);
	thin3d_->SetDepthStencilState(depth_);
	thin3d_->SetRenderState(T3DRenderState::CULL_MODE, T3DCullMode::NO_CULL);
	thin3d_->SetTexture(0, uitexture_);
	thin3d_->SetScissorEnabled(false);
	UIBegin(uishader_);
}

void UIContext::BeginNoTex() {
	thin3d_->SetBlendState(blend_);
	thin3d_->SetSamplerStates(0, 1, &sampler_);
	thin3d_->SetRenderState(T3DRenderState::CULL_MODE, T3DCullMode::NO_CULL);

	UIBegin(uishadernotex_);
}

void UIContext::RebindTexture() const {
	thin3d_->SetTexture(0, uitexture_);
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
	if (scissorStack_.size()) {
		const Bounds &bounds = scissorStack_.back();
		float scale = pixel_in_dps;
		int x = scale * bounds.x;
		int y = scale * bounds.y;
		int w = scale * bounds.w;
		int h = scale * bounds.h;
		thin3d_->SetScissorRect(x, y, w, h);
		thin3d_->SetScissorEnabled(true);
	} else {
		thin3d_->SetScissorEnabled(false);
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
