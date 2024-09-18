#include "ppsspp_config.h"

#include <algorithm>

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/LogReporting.h"

UIContext::UIContext() {
	fontStyle_ = new UI::FontStyle();
	bounds_ = Bounds(0, 0, g_display.dp_xres, g_display.dp_yres);
}

UIContext::~UIContext() {
	sampler_->Release();
	delete fontStyle_;
	delete textDrawer_;
	if (uitexture_)
		uitexture_->Release();
	if (fontTexture_)
		fontTexture_->Release();
}

void UIContext::Init(Draw::DrawContext *thin3d, Draw::Pipeline *uipipe, Draw::Pipeline *uipipenotex, DrawBuffer *uidrawbuffer) {
	using namespace Draw;
	draw_ = thin3d;
	sampler_ = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR, 0.0, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, });
	ui_pipeline_ = uipipe;
	ui_pipeline_notex_ = uipipenotex;
	uidrawbuffer_ = uidrawbuffer;
	textDrawer_ = TextDrawer::Create(thin3d);  // May return nullptr if no implementation is available for this platform.
}

void UIContext::setUIAtlas(const std::string &name) {
	_dbg_assert_(!name.empty());
	UIAtlas_ = name;
}

void UIContext::BeginFrame() {
	frameStartTime_ = time_now_d();
	if (!uitexture_ || UIAtlas_ != lastUIAtlas_) {
		uitexture_ = CreateTextureFromFile(draw_, UIAtlas_.c_str(), ImageFileType::ZIM, false);
		lastUIAtlas_ = UIAtlas_;
		if (!fontTexture_) {
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID)
			// Don't bother with loading font_atlas.zim
#else
			fontTexture_ = CreateTextureFromFile(draw_, "font_atlas.zim", ImageFileType::ZIM, false);
#endif
			if (!fontTexture_) {
				// Load the smaller ascii font only, like on Android. For debug ui etc.
				fontTexture_ = CreateTextureFromFile(draw_, "asciifont_atlas.zim", ImageFileType::ZIM, false);
				if (!fontTexture_) {
					WARN_LOG(Log::System, "Failed to load font_atlas.zim or asciifont_atlas.zim");
				}
			}
		}
	}
	uidrawbuffer_->SetCurZ(0.0f);
	ActivateTopScissor();
}

void UIContext::SetTintSaturation(float tint, float sat) {
	uidrawbuffer_->SetTintSaturation(tint, sat);
}

void UIContext::Begin() {
	BeginPipeline(ui_pipeline_, sampler_);
}

void UIContext::BeginNoTex() {
	draw_->BindSamplerStates(0, 1, &sampler_);
	UIBegin(ui_pipeline_notex_);
}

void UIContext::BeginPipeline(Draw::Pipeline *pipeline, Draw::SamplerState *samplerState) {
	_assert_(pipeline != nullptr);
	// Also clear out any other textures bound.
	Draw::SamplerState *samplers[3]{ samplerState };
	draw_->BindSamplerStates(0, 3, samplers);
	Draw::Texture *textures[2]{};
	draw_->BindTextures(1, 2, textures);
	RebindTexture();
	UIBegin(pipeline);
}

void UIContext::RebindTexture() const {
	_dbg_assert_(uitexture_);
	if (uitexture_)
		draw_->BindTexture(0, uitexture_);
}

void UIContext::BindFontTexture() const {
	// Fall back to the UI texture, in case they have an old atlas.
	if (fontTexture_)
		draw_->BindTexture(0, fontTexture_);
	else if (uitexture_)
		draw_->BindTexture(0, uitexture_);
}

void UIContext::Flush() {
	if (uidrawbuffer_) {
		uidrawbuffer_->Flush();
	}
}

void UIContext::SetCurZ(float curZ) {
	ui_draw2d.SetCurZ(curZ);
}

// TODO: Support transformed bounds using stencil instead.
void UIContext::PushScissor(const Bounds &bounds) {
	Flush();
	Bounds clipped = TransformBounds(bounds);
	if (scissorStack_.size())
		clipped.Clip(scissorStack_.back());
	else
		clipped.Clip(bounds_);
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

Bounds UIContext::GetLayoutBounds() const {
	Bounds bounds = GetBounds();

	float left = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);
	float right = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT);
	float top = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP);
	float bottom = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM);

	// ILOG("Insets: %f %f %f %f", left, right, top, bottom);

	// Adjust left edge to compensate for cutouts (notches) if any.
	bounds.x += left;
	bounds.w -= (left + right);
	bounds.y += top;
	bounds.h -= (top + bottom);

	return bounds;
}

void UIContext::ActivateTopScissor() {
	Bounds bounds;
	if (scissorStack_.size()) {
		float scale_x = g_display.pixel_in_dps_x;
		float scale_y = g_display.pixel_in_dps_y;
		bounds = scissorStack_.back();
		int x = floorf(scale_x * bounds.x);
		int y = floorf(scale_y * bounds.y);
		int w = std::max(0.0f, ceilf(scale_x * bounds.w));
		int h = std::max(0.0f, ceilf(scale_y * bounds.h));
		if (x < 0 || y < 0 || x + w > g_display.pixel_xres || y + h > g_display.pixel_yres) {
			DEBUG_LOG(Log::G3D, "UI scissor out of bounds: %d,%d-%d,%d / %d,%d", x, y, w, h, g_display.pixel_xres, g_display.pixel_yres);
			if (x < 0) { w += x; x = 0; }
			if (y < 0) { h += y; y = 0; }
			if (x >= g_display.pixel_xres) { x = g_display.pixel_xres - 1; }
			if (y >= g_display.pixel_yres) { y = g_display.pixel_yres - 1; }
			if (x + w > g_display.pixel_xres) { w = std::min(w, g_display.pixel_xres - x); }
			if (y + w > g_display.pixel_yres) { h = std::min(h, g_display.pixel_yres - y); }
			if (w == 0) w = 1;
			if (h == 0) h = 1;
			draw_->SetScissorRect(x, y, w, h);
		} else {
			// Avoid invalid rects
			if (w == 0) w = 1;
			if (h == 0) h = 1;
			draw_->SetScissorRect(x, y, w, h);
		}
	} else {
		// Avoid rounding errors
		draw_->SetScissorRect(0, 0, g_display.pixel_xres, g_display.pixel_yres);
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

void UIContext::MeasureText(const UI::FontStyle &style, float scaleX, float scaleY, std::string_view str, float *x, float *y, int align) const {
	_dbg_assert_(str.data() != nullptr);
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)style.sizePts / 24.0f;
		Draw()->SetFontScale(scaleX * sizeFactor, scaleY * sizeFactor);
		Draw()->MeasureText(style.atlasFont, str, x, y);
	} else {
		textDrawer_->SetFont(style.fontName.c_str(), style.sizePts, style.flags);
		textDrawer_->SetFontScale(scaleX, scaleY);
		textDrawer_->MeasureString(str, x, y);
		textDrawer_->SetFont(fontStyle_->fontName.c_str(), fontStyle_->sizePts, fontStyle_->flags);
	}
}

void UIContext::MeasureTextRect(const UI::FontStyle &style, float scaleX, float scaleY, std::string_view str, const Bounds &bounds, float *x, float *y, int align) const {
	_dbg_assert_(str.data() != nullptr);
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		float sizeFactor = (float)style.sizePts / 24.0f;
		Draw()->SetFontScale(scaleX * sizeFactor, scaleY * sizeFactor);
		Draw()->MeasureTextRect(style.atlasFont, str, bounds, x, y, align);
	} else {
		textDrawer_->SetFont(style.fontName.c_str(), style.sizePts, style.flags);
		textDrawer_->SetFontScale(scaleX, scaleY);
		textDrawer_->MeasureStringRect(str, bounds, x, y, align);
		textDrawer_->SetFont(fontStyle_->fontName.c_str(), fontStyle_->sizePts, fontStyle_->flags);
	}
}

void UIContext::DrawText(std::string_view str, float x, float y, uint32_t color, int align) {
	_dbg_assert_(str.data() != nullptr);
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		// Use the font texture if this font is in that texture instead.
		bool useFontTexture = Draw()->GetFontAtlas()->getFont(fontStyle_->atlasFont) != nullptr;
		if (useFontTexture) {
			Flush();
			BindFontTexture();
		}
		float sizeFactor = (float)fontStyle_->sizePts / 24.0f;
		Draw()->SetFontScale(fontScaleX_ * sizeFactor, fontScaleY_ * sizeFactor);
		Draw()->DrawText(fontStyle_->atlasFont, str, x, y, color, align);
		if (useFontTexture)
			Flush();
	} else {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		textDrawer_->DrawString(*Draw(), str, x, y, color, align);
	}
	RebindTexture();
}

void UIContext::DrawTextShadow(std::string_view str, float x, float y, uint32_t color, int align) {
	uint32_t alpha = (color >> 1) & 0xFF000000;
	DrawText(str, x + 2, y + 2, alpha, align);
	DrawText(str, x, y, color, align);
}

void UIContext::DrawTextRect(std::string_view str, const Bounds &bounds, uint32_t color, int align) {
	if (!textDrawer_ || (align & FLAG_DYNAMIC_ASCII)) {
		// Use the font texture if this font is in that texture instead.
		bool useFontTexture = Draw()->GetFontAtlas()->getFont(fontStyle_->atlasFont) != nullptr;
		if (useFontTexture) {
			Flush();
			BindFontTexture();
		}
		float sizeFactor = (float)fontStyle_->sizePts / 24.0f;
		Draw()->SetFontScale(fontScaleX_ * sizeFactor, fontScaleY_ * sizeFactor);
		Draw()->DrawTextRect(fontStyle_->atlasFont, str, bounds.x, bounds.y, bounds.w, bounds.h, color, align);
		if (useFontTexture)
			Flush();
	} else {
		textDrawer_->SetFontScale(fontScaleX_, fontScaleY_);
		Bounds rounded = bounds;
		rounded.x = floorf(rounded.x);
		rounded.y = floorf(rounded.y);
		textDrawer_->DrawStringRect(*Draw(), str, rounded, color, align);
	}
	RebindTexture();
}

static constexpr float MIN_TEXT_SCALE = 0.7f;

float UIContext::CalculateTextScale(std::string_view str, float availWidth, float availHeight) const {
	float actualWidth, actualHeight;
	Bounds availBounds(0, 0, availWidth, availHeight);
	MeasureTextRect(theme->uiFont, 1.0f, 1.0f, str, availBounds, &actualWidth, &actualHeight, ALIGN_VCENTER);
	if (actualWidth > availWidth) {
		return std::max(MIN_TEXT_SCALE, availWidth / actualWidth);
	}
	return 1.0f;
}

void UIContext::DrawTextRectSqueeze(std::string_view str, const Bounds &bounds, uint32_t color, int align) {
	float origScaleX = fontScaleX_;
	float origScaleY = fontScaleY_;
	float scale = CalculateTextScale(str, bounds.w / origScaleX, bounds.h / origScaleY);
	SetFontScale(scale * origScaleX, scale * origScaleY);
	Bounds textBounds(bounds.x, bounds.y, bounds.w, bounds.h);
	DrawTextRect(str, textBounds, color, align);
	SetFontScale(origScaleX, origScaleY);
}

void UIContext::DrawTextShadowRect(std::string_view str, const Bounds &bounds, uint32_t color, int align) {
	uint32_t alpha = (color >> 1) & 0xFF000000;
	Bounds shadowBounds(bounds.x+2, bounds.y+2, bounds.w, bounds.h);
	DrawTextRect(str, shadowBounds, alpha, align);
	DrawTextRect(str, bounds, color, align);
}

void UIContext::FillRect(const UI::Drawable &drawable, const Bounds &bounds) {
	// Only draw if alpha is non-zero.
	if ((drawable.color & 0xFF000000) == 0)
		return;

	switch (drawable.type) {
	case UI::DRAW_SOLID_COLOR:
		uidrawbuffer_->DrawImageCenterTexel(theme->whiteImage, bounds.x, bounds.y, bounds.x2(), bounds.y2(), drawable.color);
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

void UIContext::DrawRectDropShadow(const Bounds &bounds, float radius, float alpha, uint32_t color) {
	if (alpha <= 0.0f)
		return;

	color = colorAlpha(color, alpha);

	// Bias the shadow downwards a bit.
	Bounds shadowBounds = bounds.Expand(radius, 0.5 * radius, radius, 1.5 * radius);
	Draw()->DrawImage4Grid(theme->dropShadow4Grid, shadowBounds.x, shadowBounds.y, shadowBounds.x2(), shadowBounds.y2(), color, radius * (1.0f / 24.0f) * 2.0f);
}

void UIContext::DrawImageVGradient(ImageID image, uint32_t color1, uint32_t color2, const Bounds &bounds) {
	uidrawbuffer_->DrawImageStretchVGradient(image, bounds.x, bounds.y, bounds.x2(), bounds.y2(), color1, color2);
}

void UIContext::PushTransform(const UITransform &transform) {
	Flush();

	using namespace Lin;

	Matrix4x4 m = Draw()->GetDrawMatrix();
	const Vec3 &t = transform.translate;
	Vec3 scaledTranslate = Vec3(
		t.x * m.xx + t.y * m.xy + t.z * m.xz + m.xw,
		t.x * m.yx + t.y * m.yy + t.z * m.yz + m.yw,
		t.x * m.zx + t.y * m.zy + t.z * m.zz + m.zw);

	m.translateAndScale(scaledTranslate, transform.scale);
	Draw()->PushDrawMatrix(m);
	Draw()->PushAlpha(transform.alpha);

	transformStack_.push_back(transform);
}

void UIContext::PopTransform() {
	Flush();

	transformStack_.pop_back();

	Draw()->PopDrawMatrix();
	Draw()->PopAlpha();
}

Bounds UIContext::TransformBounds(const Bounds &bounds) {
	if (!transformStack_.empty()) {
		const UITransform t = transformStack_.back();
		Bounds translated = bounds.Offset(t.translate.x, t.translate.y);

		// Scale around the center as the origin.
		float scaledX = (translated.x - g_display.dp_xres * 0.5f) * t.scale.x + g_display.dp_xres * 0.5f;
		float scaledY = (translated.y - g_display.dp_yres * 0.5f) * t.scale.y + g_display.dp_yres * 0.5f;

		return Bounds(scaledX, scaledY, translated.w * t.scale.x, translated.h * t.scale.y);
	}

	return bounds;
}
