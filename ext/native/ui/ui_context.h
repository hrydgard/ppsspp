#pragma once

#include <vector>

#include "base/basictypes.h"
#include "math/geom2d.h"
#include "math/lin/vec3.h"
#include "gfx/texture_atlas.h"
#include "UI/TextureUtil.h"

// Everything you need to draw a UI collected into a single unit that can be passed around.
// Everything forward declared so this header is safe everywhere.

namespace Draw {
	class DrawContext;
	class Pipeline;
	class DepthStencilState;
	class Texture;
	class BlendState;
	class SamplerState;
	class RasterState;
}

class Texture;
class DrawBuffer;
class TextDrawer;

namespace UI {
	struct Drawable;
	struct Theme;
	struct FontStyle;
}

class DrawBuffer;

struct UITransform {
	// TODO: Or just use a matrix?
	Vec3 translate;
	Vec3 scale;
	float alpha;
};

class UIContext {
public:
	UIContext();
	~UIContext();

	void Init(Draw::DrawContext *thin3d, Draw::Pipeline *uipipe, Draw::Pipeline *uipipenotex, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop);

	void BeginFrame();

	void Begin();
	void BeginNoTex();
	void BeginPipeline(Draw::Pipeline *pipeline, Draw::SamplerState *samplerState);
	void Flush();

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
	Draw::DrawContext *GetDrawContext() { return draw_; }
	void SetCurZ(float curZ);

	void PushTransform(const UITransform &transform);
	void PopTransform();
	Bounds TransformBounds(const Bounds &bounds);

private:
	Draw::DrawContext *draw_;
	Bounds bounds_;

	float fontScaleX_ = 1.0f;
	float fontScaleY_ = 1.0f;
	UI::FontStyle *fontStyle_ = nullptr;
	TextDrawer *textDrawer_ = nullptr;

	Draw::SamplerState *sampler_;
	Draw::Pipeline *ui_pipeline_ = nullptr;
	Draw::Pipeline *ui_pipeline_notex_ = nullptr;
	std::unique_ptr<ManagedTexture> uitexture_;

	DrawBuffer *uidrawbuffer_ = nullptr;
	DrawBuffer *uidrawbufferTop_ = nullptr;

	std::vector<Bounds> scissorStack_;
	std::vector<UITransform> transformStack_;
};
