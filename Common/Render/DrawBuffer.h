#pragma once

// "Immediate mode"-lookalike buffered drawing. Very fast way to draw 2D.

#include <vector>
#include <cstdint>
#include <cmath>

#include "Common/Common.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Math/geom2d.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/thin3d.h"
#include "Common/Render/DrawEnum.h"

struct Atlas;

namespace Draw {
	class Pipeline;
}

struct GradientStop {
	float t;
	uint32_t color;
};

class TextDrawer;

const char *RoundRectCornerImageName(int radiusIndex, RoundRectStyle style);
// The corner image is (center + 1) texels square. The arc is centered on texel corner (center, center),
// and the last row and column are the straight edge profile.
inline int RoundRectCornerCenter(float radiusPx) {
	return (int)ceilf(radiusPx) + 1;
}

class DrawBuffer {
public:
	DrawBuffer(const DrawBuffer &) = delete;
	DrawBuffer &operator=(const DrawBuffer &) = delete;
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

	void V(float x, float y, float z, uint32_t color, float u, float v) {
		_dbg_assert_msg_(count_ < MAX_VERTS, "Overflowed the DrawBuffer");
		if (count_ >= MAX_VERTS) {
			// _dbg_assert_msg_ above is compiled out in release builds - don't write
			// past the end of verts_ there either, just drop the vertex.
			return;
		}

#ifdef _DEBUG
		if (my_isnanorinf(x) || my_isnanorinf(y) || my_isnanorinf(z)) {
			_assert_(false);
		}
#endif
		Vertex *vert = &verts_[count_++];
		vert->x = x;
		vert->y = y;
		vert->z = z;
		vert->rgba = alpha_ == 1.0f ? color : alphaMul(color, alpha_);
		vert->u = u;
		vert->v = v;
	}
	void V(float x, float y, uint32_t color, float u, float v) {
		V(x, y, curZ_, color, u, v);
	}

	void CircleSegment(float x, float y, float radius, float thickness, int segments, float startAngle, float endAngle, uint32_t color, float u_mul);
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

	// Uses the nearest precomputed corner radius, scaled to fit, so outlines get thinner or thicker
	// when radius isn't one of ROUND_RECT_RADII. 18 triangles filled, 16 outlined.
	// Corners not in `corners` are drawn square, from the edge profiles, so they match the rounded ones.
	void DrawRoundRect(float x1, float y1, float x2, float y2, float radius, Color color, RoundRectStyle style = RoundRectStyle::Filled, RoundCorners corners = RoundCorners::All);
	void DrawRoundRect(const Bounds &bounds, float radius, Color color, RoundRectStyle style = RoundRectStyle::Filled, RoundCorners corners = RoundCorners::All) {
		DrawRoundRect(bounds.x, bounds.y, bounds.x2(), bounds.y2(), radius, color, style, corners);
	}

	// This is only 6 triangles, much cheaper.
	void DrawImage2GridH(ImageID atlas_image, float x1, float y1, float x2, Color color = COLOR(0xFFFFFF), float scale = 1.0);

	void MeasureText(FontID font, std::string_view text, float *w, float *h);
	void MeasureTextRect(FontID font, std::string_view text, float maxWidth, float *w, float *h, int align = 0);

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

	// Atlas lookups are linear searches by name, so the corner images are looked up once per atlas.
	// One corner cell of a round rect, drawn square. xs/ys/us/vs point at the cell's two entries, ox/oy select the outer corner.
	void DrawSquareCorner(const float *xs, const float *ys, const float *us, const float *vs, int ox, int oy, float edgeU, float edgeV, Color color);

	const AtlasImage *roundRectCorners_[ROUND_RECT_RADIUS_COUNT][(int)RoundRectStyle::COUNT]{};
	const AtlasImage *roundRectAtlasImages_ = nullptr;

	bool inited_ = false;
	float fontscalex = 1.0f;
	float fontscaley = 1.0f;

	float tint_ = 0.0f;
	float saturation_ = 1.0f;
	float curZ_ = 0.0f;
};
