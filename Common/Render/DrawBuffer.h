#pragma once

// "Immediate mode"-lookalike buffered drawing. Very fast way to draw 2D.

#include <vector>
#include <cstdint>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Math/geom2d.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/GPU/thin3d.h"

struct Atlas;

enum {
	ALIGN_LEFT = 0,
	ALIGN_RIGHT = 16,
	ALIGN_TOP = 0,
	ALIGN_BOTTOM = 1,
	ALIGN_HCENTER = 4,
	ALIGN_VCENTER = 8,
	ALIGN_VBASELINE = 32,	// text only, possibly not yet working

	ALIGN_CENTER = ALIGN_HCENTER | ALIGN_VCENTER,
	ALIGN_TOPLEFT = ALIGN_TOP | ALIGN_LEFT,
	ALIGN_TOPRIGHT = ALIGN_TOP | ALIGN_RIGHT,
	ALIGN_BOTTOMLEFT = ALIGN_BOTTOM | ALIGN_LEFT,
	ALIGN_BOTTOMRIGHT = ALIGN_BOTTOM | ALIGN_RIGHT,

	// For "uncachable" text like debug log.
	// Avoids using system font drawing as it's too slow.
	// Not actually used here but is reserved for whatever system wraps DrawBuffer.
	FLAG_DYNAMIC_ASCII = 2048,
	FLAG_NO_PREFIX = 4096,  // means to not process ampersands
	FLAG_WRAP_TEXT = 8192,
	FLAG_ELLIPSIZE_TEXT = 16384,
};

namespace Draw {
	class Pipeline;
}

struct GradientStop {
	float t;
	uint32_t color;
};

class TextDrawer;

class DrawBuffer {
public:
	DrawBuffer();
	~DrawBuffer();

	void Begin(Draw::Pipeline *pipeline);
	void Flush(bool set_blend_state = true);

	// TODO: Enforce these. Now Init is autocalled and shutdown not called.
	void Init(Draw::DrawContext *t3d, Draw::Pipeline *pipeline);
	void Shutdown();

	// So that callers can create appropriate pipelines.
	Draw::InputLayout *CreateInputLayout(Draw::DrawContext *t3d);

	int Count() const { return count_; }

	void Rect(float x, float y, float w, float h, uint32_t color, int align = ALIGN_TOPLEFT);
	void hLine(float x1, float y, float x2, uint32_t color);
	void vLine(float x, float y1, float y2, uint32_t color);

	void Line(ImageID atlas_image, float x1, float y1, float x2, float y2, float thickness, uint32_t color);

	void RectOutline(float x, float y, float w, float h, uint32_t color, int align = ALIGN_TOPLEFT);

	// NOTE: This one takes x2/y2 instead of w/h, better for gap-free graphics.
	void RectVGradient(float x1, float y1, float x2, float y2, uint32_t colorTop, uint32_t colorBottom);
	void RectVDarkFaded(float x, float y, float w, float h, uint32_t colorTop) {
		RectVGradient(x, y, x + w, y + h, colorTop, darkenColor(colorTop));
	}

	void MultiVGradient(float x, float y, float w, float h, const GradientStop *stops, int numStops);

	void RectCenter(float x, float y, float w, float h, uint32_t color) {
		Rect(x - w/2, y - h/2, w, h, color);
	}
	void Rect(float x, float y, float w, float h, float u, float v, float uw, float uh, uint32_t color);

	void V(float x, float y, float z, uint32_t color, float u, float v);
	void V(float x, float y, uint32_t color, float u, float v) {
		V(x, y, curZ_, color, u, v);
	}

	void Circle(float x, float y, float radius, float thickness, int segments, float startAngle, uint32_t color, float u_mul);
	void FillCircle(float x, float y, float radius, int segments, uint32_t color);

	// New drawing APIs

	// Must call this before you use any functions with atlas_image etc.
	void SetAtlas(const Atlas *_atlas) {
		atlas = _atlas;
	}
	const Atlas *GetAtlas() const { return atlas; }
	void SetFontAtlas(const Atlas *_atlas) {
		fontAtlas_ = _atlas;
	}
	const Atlas *GetFontAtlas() const { return fontAtlas_; }
	bool MeasureImage(ImageID atlas_image, float *w, float *h);
	void DrawImage(ImageID atlas_image, float x, float y, float scale, Color color = COLOR(0xFFFFFF), int align = ALIGN_TOPLEFT);

	// Good for stretching out a white image without edge artifacts that I'm getting on iOS.
	void DrawImageCenterTexel(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF));
	void DrawImageStretch(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF));
	void DrawImageStretchVGradient(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color1, Color color2);
	void DrawImageStretch(ImageID atlas_image, const Bounds &bounds, Color color = COLOR(0xFFFFFF)) {
		DrawImageStretch(atlas_image, bounds.x, bounds.y, bounds.x2(), bounds.y2(), color);
	}
	void DrawImageRotated(ImageID atlas_image, float x, float y, float scale, float angle, Color color = COLOR(0xFFFFFF), bool mirror_h = false);	// Always centers
	void DrawImageRotatedStretch(ImageID atlas_image, const Bounds &bounds, float scales[2], float angle, Color color = COLOR(0xFFFFFF), bool mirror_h = false);
	void DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color);
	void DrawTexRect(const Bounds &bounds, float u1, float v1, float u2, float v2, Color color) {
		DrawTexRect(bounds.x, bounds.y, bounds.x2(), bounds.y2(), u1, v1, u2, v2, color);
	}
	// Results in 18 triangles. Kind of expensive for a button.
	void DrawImage4Grid(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF), float corner_scale = 1.0);
	// This is only 6 triangles, much cheaper.
	void DrawImage2GridH(ImageID atlas_image, float x1, float y1, float x2, Color color = COLOR(0xFFFFFF), float scale = 1.0);

	void MeasureText(FontID font, std::string_view text, float *w, float *h);

	void MeasureTextRect(FontID font, std::string_view text, const Bounds &bounds, float *w, float *h, int align = 0);

	void DrawTextRect(FontID font, std::string_view text, float x, float y, float w, float h, Color color = 0xFFFFFFFF, int align = 0);
	void DrawText(FontID font, std::string_view text, float x, float y, Color color = 0xFFFFFFFF, int align = 0);
	void DrawTextShadow(FontID font, std::string_view text, float x, float y, Color color = 0xFFFFFFFF, int align = 0);

	void SetFontScale(float xs, float ys) {
		fontscalex = xs;
		fontscaley = ys;
	}

	static void DoAlign(int flags, float *x, float *y, float *w, float *h);

	void PushDrawMatrix(const Lin::Matrix4x4 &m) {
		drawMatrixStack_.push_back(drawMatrix_);
		drawMatrix_ = m;
	}

	void PopDrawMatrix() {
		drawMatrix_ = drawMatrixStack_.back();
		drawMatrixStack_.pop_back();
	}

	Lin::Matrix4x4 GetDrawMatrix() {
		return drawMatrix_;
	}

	void PushAlpha(float a) {
		alphaStack_.push_back(alpha_);
		alpha_ *= a;
	}

	void PopAlpha() {
		alpha_ = alphaStack_.back();
		alphaStack_.pop_back();
	}

	void SetCurZ(float curZ) {
		curZ_ = curZ;
	}

	void SetTintSaturation(float tint, float saturation) {
		tint_ = tint;
		saturation_ = saturation;
	}

	enum {
		// TODO: Can probably shrink this. Currently consumes 1.5MB.
		MAX_VERTS = 65536,
	};

private:
	struct Vertex {
		float x, y, z;
		float u, v;
		uint32_t rgba;
	};

	Lin::Matrix4x4 drawMatrix_;
	std::vector<Lin::Matrix4x4> drawMatrixStack_;

	float alpha_ = 1.0f;
	std::vector<float> alphaStack_;

	Draw::DrawContext *draw_ = nullptr;
	Draw::Pipeline *pipeline_ = nullptr;

	Vertex *verts_;
	int count_ = 0;
	const Atlas *atlas = nullptr;
	const Atlas *fontAtlas_ = nullptr;

	bool inited_ = false;
	float fontscalex = 1.0f;
	float fontscaley = 1.0f;

	float tint_ = 0.0f;
	float saturation_ = 1.0f;
	float curZ_ = 0.0f;
};
