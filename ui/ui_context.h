#pragma once

#include <vector>

#include "base/basictypes.h"
#include "math/geom2d.h"
#include "gfx/texture_atlas.h"

// Everything you need to draw a UI collected into a single unit that can be passed around.
// Everything forward declared so this header is safe everywhere.

struct GLSLProgram;
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

	void Init(const GLSLProgram *uishader, const GLSLProgram *uishadernotex, Texture *uitexture, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop);

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
	void MeasureText(const UI::FontStyle &style, const char *str, float *x, float *y, int align = 0) const;
	void DrawText(const char *str, float x, float y, uint32_t color, int align = 0);
	void DrawTextRect(const char *str, const Bounds &bounds, uint32_t color, int align = 0);
	void FillRect(const UI::Drawable &drawable, const Bounds &bounds);

private:
	float fontScaleX_;
	float fontScaleY_;
	UI::FontStyle *fontStyle_;
	TextDrawer *textDrawer_;
	// TODO: Collect these into a UIContext
	const GLSLProgram *uishader_;
	const GLSLProgram *uishadernotex_;
	Texture *uitexture_;
	DrawBuffer *uidrawbuffer_;
	DrawBuffer *uidrawbufferTop_;

	std::vector<Bounds> scissorStack_;
};
