#pragma once

#include <vector>

#include "base/basictypes.h"
#include "math/geom2d.h"
#include "gfx/texture_atlas.h"

// Everything you need to draw a UI collected into a single unit that can be passed around.
// Everything forward declared so this header is safe everywhere.

class Thin3DContext;
class Thin3DShaderSet;
class Thin3DDepthStencilState;
class Thin3DTexture;
class Thin3DBlendState;
class Thin3DSamplerState;
class Texture;
class DrawBuffer;
class TextDrawer;

namespace UI {
	struct Drawable;
	struct Theme;
	struct FontStyle;
}

class DrawBuffer;

class UIContext {
public:
	UIContext();
	~UIContext();

	void Init(Thin3DContext *thin3d, Thin3DShaderSet *uiShaderTex, Thin3DShaderSet *uiShaderNoTex, Thin3DTexture *uitexture, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop);

	void Begin();
	void BeginNoTex();
	void Flush();
	void End();

	void RebindTexture() const;

	// TODO: Support transformed bounds using stencil
	void PushScissor(const Bounds &bounds);
	void PopScissor();
	Bounds GetScissorBounds();

	void ActivateTopScissor();

	DrawBuffer *Draw() const { return uidrawbuffer_; }
	DrawBuffer *DrawTop() const { return uidrawbufferTop_; }
	const UI::Theme *theme;

	// Utility methods

	TextDrawer *Text() const { return textDrawer_; }

	void SetFontStyle(const UI::FontStyle &style);
	const UI::FontStyle &GetFontStyle() { return *fontStyle_; }
	void SetFontScale(float scaleX, float scaleY);
	void MeasureTextCount(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, int count, float *x, float *y, int align = 0) const;
	void MeasureText(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, float *x, float *y, int align = 0) const;
	void MeasureTextRect(const UI::FontStyle &style, float scaleX, float scaleY, const char *str, int count, const Bounds &bounds, float *x, float *y, int align = 0) const;
	void DrawText(const char *str, float x, float y, uint32_t color, int align = 0);
	void DrawTextShadow(const char *str, float x, float y, uint32_t color, int align = 0);
	void DrawTextRect(const char *str, const Bounds &bounds, uint32_t color, int align = 0);
	void FillRect(const UI::Drawable &drawable, const Bounds &bounds);

	// in dps, like dp_xres and dp_yres
	void SetBounds(const Bounds &b) { bounds_ = b; }
	const Bounds &GetBounds() const { return bounds_; }
	Thin3DContext *GetThin3DContext() { return thin3d_; }

private:
	Thin3DContext *thin3d_;
	Bounds bounds_;

	float fontScaleX_;
	float fontScaleY_;
	UI::FontStyle *fontStyle_;
	TextDrawer *textDrawer_;

	Thin3DContext *thin3D_;
	Thin3DDepthStencilState *depth_;
	Thin3DBlendState *blend_;
	Thin3DSamplerState *sampler_;
	Thin3DShaderSet *uishader_;
	Thin3DShaderSet *uishadernotex_;
	Thin3DTexture *uitexture_;

	DrawBuffer *uidrawbuffer_;
	DrawBuffer *uidrawbufferTop_;

	std::vector<Bounds> scissorStack_;
};
