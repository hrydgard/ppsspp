#pragma once

#include <vector>

#include "math/geom2d.h"
#include "gfx/texture_atlas.h"

// Kind of ugly connection to UI.
namespace UI {
	struct Theme;
}

class DrawBuffer;

class DrawContext {
public:
	DrawContext();
	~DrawContext();

	DrawBuffer *draw;
	DrawBuffer *drawTop;
	const UI::Theme *theme;

	// TODO: Support transformed bounds using stencil
	void PushScissor(const Bounds &bounds);
	void PopScissor();

	void ActivateTopScissor();

	void Flush();

private:
	std::vector<Bounds> scissorStack_;
};