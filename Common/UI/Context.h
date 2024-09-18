#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>

#include "Common/Math/geom2d.h"
#include "Common/Math/lin/vec3.h"
#include "Common/Render/TextureAtlas.h"

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
	struct EventParams;
	struct Theme;
	struct FontStyle;
	class Event;
	class View;
}

class DrawBuffer;

struct UITransform {
	// TODO: Or just use a matrix?
	Lin::Vec3 translate;
	Lin::Vec3 scale;
	float alpha;
};

class UIContext {
public:
	UIContext();
	~UIContext();

	void Init(Draw::DrawContext *thin3d, Draw::Pipeline *uipipe, Draw::Pipeline *uipipenotex, DrawBuffer *uidrawbuffer);

	void BeginFrame();

	void Begin();
	void BeginNoTex();
	void BeginPipeline(Draw::Pipeline *pipeline, Draw::SamplerState *samplerState);
	void Flush();

	void RebindTexture() const;
	void BindFontTexture() const;

	double FrameStartTime() const { return frameStartTime_; }

	// TODO: Support transformed bounds using stencil
	void PushScissor(const Bounds &bounds);
	void PopScissor();
	Bounds GetScissorBounds();

	void ActivateTopScissor();

	DrawBuffer *Draw() const { return uidrawbuffer_; }

	// Utility methods
	TextDrawer *Text() const { return textDrawer_; }

	void SetTintSaturation(float tint, float sat);

	// High level drawing functions. They generally assume the default texture to be bounds.

	void SetFontStyle(const UI::FontStyle &style);
	const UI::FontStyle &GetFontStyle() { return *fontStyle_; }
	void SetFontScale(float scaleX, float scaleY);
	void MeasureText(const UI::FontStyle &style, float scaleX, float scaleY, std::string_view str, float *x, float *y, int align = 0) const;
	void MeasureTextRect(const UI::FontStyle &style, float scaleX, float scaleY, std::string_view str, const Bounds &bounds, float *x, float *y, int align = 0) const;
	void DrawText(std::string_view str, float x, float y, uint32_t color, int align = 0);
	void DrawTextShadow(std::string_view str, float x, float y, uint32_t color, int align = 0);
	void DrawTextRect(std::string_view str, const Bounds &bounds, uint32_t color, int align = 0);
	void DrawTextShadowRect(std::string_view str, const Bounds &bounds, uint32_t color, int align = 0);
	// Will squeeze the text into the bounds if needed.
	void DrawTextRectSqueeze(std::string_view str, const Bounds &bounds, uint32_t color, int align = 0);

	float CalculateTextScale(std::string_view str, float availWidth, float availHeight) const;

	void FillRect(const UI::Drawable &drawable, const Bounds &bounds);
	void DrawRectDropShadow(const Bounds &bounds, float radius, float alpha, uint32_t color = 0);
	void DrawImageVGradient(ImageID image, uint32_t color1, uint32_t color2, const Bounds &bounds);

	// in dps, like dp_xres and dp_yres
	void SetBounds(const Bounds &b) { bounds_ = b; }
	const Bounds &GetBounds() const { return bounds_; }
	Bounds GetLayoutBounds() const;
	Draw::DrawContext *GetDrawContext() { return draw_; }
	const UI::Theme &GetTheme() const {
		return *theme;
	}
	void SetCurZ(float curZ);

	void PushTransform(const UITransform &transform);
	void PopTransform();
	Bounds TransformBounds(const Bounds &bounds);

	void setUIAtlas(const std::string &name);

	// TODO: Move to private.
	const UI::Theme *theme;

private:
	Draw::DrawContext *draw_ = nullptr;
	Bounds bounds_;

	double frameStartTime_ = 0.0;

	float fontScaleX_ = 1.0f;
	float fontScaleY_ = 1.0f;
	UI::FontStyle *fontStyle_ = nullptr;
	TextDrawer *textDrawer_ = nullptr;

	Draw::SamplerState *sampler_ = nullptr;
	Draw::Pipeline *ui_pipeline_ = nullptr;
	Draw::Pipeline *ui_pipeline_notex_ = nullptr;
	Draw::Texture *uitexture_ = nullptr;
	Draw::Texture *fontTexture_ = nullptr;

	DrawBuffer *uidrawbuffer_ = nullptr;

	std::vector<Bounds> scissorStack_;
	std::vector<UITransform> transformStack_;

	std::string lastUIAtlas_;
	std::string UIAtlas_ = "ui_atlas.zim";
};
