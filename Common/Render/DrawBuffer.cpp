#include <algorithm>
#include <cmath>
#include <vector>
#include <stddef.h>

#include "Common/System/Display.h"
#include "Common/Math/math_util.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/WrapText.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Common/Math/math_util.h"

DrawBuffer::DrawBuffer() {
	verts_ = new Vertex[MAX_VERTS];
	fontscalex = 1.0f;
	fontscaley = 1.0f;
}

DrawBuffer::~DrawBuffer() {
	delete [] verts_;
}

void DrawBuffer::Init(Draw::DrawContext *t3d, Draw::Pipeline *pipeline) {
	using namespace Draw;

	if (inited_)
		return;

	draw_ = t3d;
	inited_ = true;
}

Draw::InputLayout *DrawBuffer::CreateInputLayout(Draw::DrawContext *t3d) {
	using namespace Draw;
	InputLayoutDesc desc = {
		sizeof(Vertex),
		{
			{ SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	return t3d->CreateInputLayout(desc);
}

void DrawBuffer::Shutdown() {
	inited_ = false;
	alphaStack_.clear();
	drawMatrixStack_.clear();
	pipeline_ = nullptr;
	draw_ = nullptr;
	count_ = 0;
}

void DrawBuffer::Begin(Draw::Pipeline *program) {
	pipeline_ = program;
	count_ = 0;
}

void DrawBuffer::Flush(bool set_blend_state) {
	using namespace Draw;
	if (count_ == 0)
		return;
	if (!pipeline_) {
		ERROR_LOG(Log::G3D, "DrawBuffer: No program set, skipping flush!");
		count_ = 0;
		return;
	}
	draw_->BindPipeline(pipeline_);

	VsTexColUB ub{};
	memcpy(ub.WorldViewProj, drawMatrix_.getReadPtr(), sizeof(Lin::Matrix4x4));
	ub.tint = tint_;
	ub.saturation = saturation_;
	draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	draw_->DrawUP((const void *)verts_, count_);
	count_ = 0;
}

void DrawBuffer::V(float x, float y, float z, uint32_t color, float u, float v) {
	_dbg_assert_msg_(count_ < MAX_VERTS, "Overflowed the DrawBuffer");

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

void DrawBuffer::Rect(float x, float y, float w, float h, uint32_t color, int align) {
	DoAlign(align, &x, &y, &w, &h);
	RectVGradient(x, y, x + w, y + h, color, color);
}

void DrawBuffer::hLine(float x1, float y, float x2, uint32_t color) {
	// Round Y to the closest full pixel, since we're making it 1-pixel-thin.
	y -= fmodf(y, g_display.pixel_in_dps_y);
	Rect(x1, y, x2 - x1, g_display.pixel_in_dps_y, color);
}

void DrawBuffer::vLine(float x, float y1, float y2, uint32_t color) {
	// Round X to the closest full pixel, since we're making it 1-pixel-thin.
	x -= fmodf(x, g_display.pixel_in_dps_x);
	Rect(x, y1, g_display.pixel_in_dps_x, y2 - y1, color);
}

void DrawBuffer::RectVGradient(float x1, float y1, float x2, float y2, uint32_t colorTop, uint32_t colorBottom) {
	V(x1, y1, 0, colorTop,    0, 0);
	V(x2, y1, 0, colorTop,    1, 0);
	V(x2, y2, 0, colorBottom, 1, 1);
	V(x1, y1, 0, colorTop,    0, 0);
	V(x2, y2, 0, colorBottom, 1, 1);
	V(x1, y2, 0, colorBottom, 0, 1);
}

void DrawBuffer::RectOutline(float x, float y, float w, float h, uint32_t color, int align) {
	hLine(x, y, x + w + g_display.pixel_in_dps_x, color);
	hLine(x, y + h, x + w + g_display.pixel_in_dps_x, color);

	vLine(x, y, y + h + g_display.pixel_in_dps_y, color);
	vLine(x + w, y, y + h + g_display.pixel_in_dps_y, color);
}

void DrawBuffer::MultiVGradient(float x, float y, float w, float h, const GradientStop *stops, int numStops) {
	for (int i = 0; i < numStops - 1; i++) {
		float t0 = stops[i].t, t1 = stops[i+1].t;
		uint32_t c0 = stops[i].color, c1 = stops[i+1].color;
		RectVGradient(x, y + h * t0, x + w, y + h * (t1 - t0), c0, c1);
	}
}

void DrawBuffer::Rect(float x, float y, float w, float h,
	float u, float v, float uw, float uh,
	uint32_t color) {
		V(x,	   y,     0, color, u, v);
		V(x + w, y,	   0, color, u + uw, v);
		V(x + w, y + h, 0, color, u + uw, v + uh);
		V(x,	   y,     0, color, u, v);
		V(x + w, y + h, 0, color, u + uw, v + uh);
		V(x,	   y + h, 0, color, u, v + uh);
}

void DrawBuffer::Line(ImageID atlas_image, float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;

	// No caps yet!
	// Pre-rotated - we are making a thick line here
	float dx = -(y2 - y1);
	float dy = x2 - x1;
	float len = sqrtf(dx * dx + dy * dy) / thickness;
	if (len <= 0.0f)
		len = 1.0f;

	dx /= len;
	dy /= len;

	float x[4] = { x1 - dx, x2 - dx, x1 + dx, x2 + dx };
	float y[4] = { y1 - dy, y2 - dy, y1 + dy, y2 + dy };

	V(x[0],	y[0], color, image->u1, image->v1);
	V(x[1],	y[1], color, image->u2, image->v1);
	V(x[2],	y[2], color, image->u1, image->v2);
	V(x[2],	y[2], color, image->u1, image->v2);
	V(x[1],	y[1], color, image->u2, image->v1);
	V(x[3],	y[3], color, image->u2, image->v2);
}

bool DrawBuffer::MeasureImage(ImageID atlas_image, float *w, float *h) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (image) {
		*w = (float)image->w;
		*h = (float)image->h;
		return true;
	} else {
		*w = 0;
		*h = 0;
		return false;
	}
}

void DrawBuffer::DrawImage(ImageID atlas_image, float x, float y, float scale, Color color, int align) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;

	float w = (float)image->w * scale;
	float h = (float)image->h * scale;
	if (align & ALIGN_HCENTER) x -= w / 2;
	if (align & ALIGN_RIGHT) x -= w;
	if (align & ALIGN_VCENTER) y -= h / 2;
	if (align & ALIGN_BOTTOM) y -= h;
	DrawImageStretch(atlas_image, x, y, x + w, y + h, color);
}

void DrawBuffer::DrawImageCenterTexel(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;
	float centerU = (image->u1 + image->u2) * 0.5f;
	float centerV = (image->v1 + image->v2) * 0.5f;
	V(x1,	y1, color, centerU, centerV);
	V(x2,	y1, color, centerU, centerV);
	V(x2,	y2, color, centerU, centerV);
	V(x1,	y1, color, centerU, centerV);
	V(x2,	y2, color, centerU, centerV);
	V(x1,	y2, color, centerU, centerV);
}

void DrawBuffer::DrawImageStretch(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;
	V(x1,	y1, color, image->u1, image->v1);
	V(x2,	y1, color, image->u2, image->v1);
	V(x2,	y2, color, image->u2, image->v2);
	V(x1,	y1, color, image->u1, image->v1);
	V(x2,	y2, color, image->u2, image->v2);
	V(x1,	y2, color, image->u1, image->v2);
}

void DrawBuffer::DrawImageStretchVGradient(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color1, Color color2) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;
	V(x1, y1, color1, image->u1, image->v1);
	V(x2, y1, color1, image->u2, image->v1);
	V(x2, y2, color2, image->u2, image->v2);
	V(x1, y1, color1, image->u1, image->v1);
	V(x2, y2, color2, image->u2, image->v2);
	V(x1, y2, color2, image->u1, image->v2);
}

inline void rot(float *v, float angle, float xc, float yc) {
	const float x = v[0] - xc;
	const float y = v[1] - yc;
	const float sa = sinf(angle);
	const float ca = cosf(angle);
	v[0] = x * ca + y * -sa + xc;
	v[1] = x * sa + y *  ca + yc;
}

void DrawBuffer::DrawImageRotated(ImageID atlas_image, float x, float y, float scale, float angle, Color color, bool mirror_h) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;

	float w = (float)image->w * scale;
	float h = (float)image->h * scale;
	float x1 = x - w / 2;
	float x2 = x + w / 2;
	float y1 = y - h / 2;
	float y2 = y + h / 2;
	float v[6][2] = {
		{x1, y1},
		{x2, y1},
		{x2, y2},
		{x1, y1},
		{x2, y2},
		{x1, y2},
	};
	float u1 = image->u1;
	float u2 = image->u2;
	if (mirror_h) {
		float temp = u1;
		u1 = u2;
		u2 = temp;
	}
	const float uv[6][2] = {
		{u1, image->v1},
		{u2, image->v1},
		{u2, image->v2},
		{u1, image->v1},
		{u2, image->v2},
		{u1, image->v2},
	};
	for (int i = 0; i < 6; i++) {
		if (angle != 0.0f) {
			rot(v[i], angle, x, y);
		}
		V(v[i][0], v[i][1], 0, color, uv[i][0], uv[i][1]);
	}
}

void DrawBuffer::DrawImageRotatedStretch(ImageID atlas_image, const Bounds &bounds, float scales[2], float angle, Color color, bool mirror_h) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	if (!image)
		return;

	if (scales[0] == 0.0f || scales[1] == 0.0f) {
		float rotatedSize[2]{ (float)image->w, (float)image->h };
		rot(rotatedSize, angle, 0.0f, 0.0f);

		// With that, we calculate the scale to stretch to, and rotate it back.
		scales[0] = bounds.w / rotatedSize[0];
		scales[1] = bounds.h / rotatedSize[1];
		rot(scales, -angle, 0.0f, 0.0f);
	}

	float w = (float)image->w * scales[0];
	float h = (float)image->h * scales[1];
	float x1 = bounds.centerX() - w / 2;
	float x2 = bounds.centerX() + w / 2;
	float y1 = bounds.centerY() - h / 2;
	float y2 = bounds.centerY() + h / 2;
	float v[6][2] = {
		{x1, y1},
		{x2, y1},
		{x2, y2},
		{x1, y1},
		{x2, y2},
		{x1, y2},
	};
	float u1 = image->u1;
	float u2 = image->u2;
	if (mirror_h) {
		float temp = u1;
		u1 = u2;
		u2 = temp;
	}
	const float uv[6][2] = {
		{u1, image->v1},
		{u2, image->v1},
		{u2, image->v2},
		{u1, image->v1},
		{u2, image->v2},
		{u1, image->v2},
	};
	for (int i = 0; i < 6; i++) {
		rot(v[i], angle, bounds.centerX(), bounds.centerY());
		V(v[i][0], v[i][1], 0, color, uv[i][0], uv[i][1]);
	}
}

void DrawBuffer::Circle(float xc, float yc, float radius, float thickness, int segments, float startAngle, uint32_t color, float u_mul) {
	float angleDelta = PI * 2 / segments;
	float uDelta = 1.0f / segments;
	float t2 = thickness / 2.0f;
	float r1 = radius + t2;
	float r2 = radius - t2;
	for (int i = 0; i < segments + 1; i++) {
		float angle1 = i * angleDelta;
		float angle2 = (i + 1) * angleDelta;
		float u1 = u_mul * i * uDelta;
		float u2 = u_mul * (i + 1) * uDelta;
		// TODO: get rid of one pair of cos/sin per loop, can reuse from last iteration
		float c1 = cosf(angle1), s1 = sinf(angle1), c2 = cosf(angle2), s2 = sinf(angle2);
		const float x[4] = {c1 * r1 + xc, c2 * r1 + xc, c1 * r2 + xc, c2 * r2 + xc};
		const float y[4] = {s1 * r1 + yc, s2 * r1 + yc, s1 * r2 + yc, s2 * r2 + yc};
		V(x[0],	y[0], color, u1, 0.0f);
		V(x[1],	y[1], color, u2, 0.0f);
		V(x[2],	y[2], color, u1, 1.0f);
		V(x[1],	y[1], color, u2, 0.0f);
		V(x[3],	y[3], color, u2, 1.0f);
		V(x[2],	y[2], color, u1, 1.0f);
	}
}

void DrawBuffer::FillCircle(float xc, float yc, float radius, int segments, uint32_t color) {
	float angleDelta = PI * 2 / segments;
	float uDelta = 1.0f / segments;
	float r1 = radius;
	for (int i = 0; i < segments + 1; i++) {
		float angle1 = i * angleDelta;
		float angle2 = (i + 1) * angleDelta;
		float u1 = i * uDelta;
		float u2 = (i + 1) * uDelta;
		// TODO: get rid of one pair of cos/sin per loop, can reuse from last iteration
		float c1 = cosf(angle1), s1 = sinf(angle1), c2 = cosf(angle2), s2 = sinf(angle2);
		const float x[2] = { c1 * r1 + xc, c2 * r1 + xc };
		const float y[2] = { s1 * r1 + yc, s2 * r1 + yc };
		V(xc, yc, color, 0.0f, 0.0f);
		V(x[0], y[0], color, u1, 0.0f);
		V(x[1], y[1], color, u2, 1.0f);
	}
}

void DrawBuffer::DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color) {
	V(x1,	y1, color, u1, v1);
	V(x2,	y1, color, u2, v1);
	V(x2,	y2, color, u2, v2);
	V(x1,	y1, color, u1, v1);
	V(x2,	y2, color, u2, v2);
	V(x1,	y2, color, u1, v2);
}

void DrawBuffer::DrawImage4Grid(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color, float corner_scale) {
	const AtlasImage *image = atlas->getImage(atlas_image);

	if (!image) {
		return;
	}

	float u1 = image->u1, v1 = image->v1, u2 = image->u2, v2 = image->v2;
	float um = (u2 + u1) * 0.5f;
	float vm = (v2 + v1) * 0.5f;
	float iw2 = (image->w * 0.5f) * corner_scale;
	float ih2 = (image->h * 0.5f) * corner_scale;
	float xa = x1 + iw2;
	float xb = x2 - iw2;
	float ya = y1 + ih2;
	float yb = y2 - ih2;
	// Top row
	DrawTexRect(x1, y1, xa, ya, u1, v1, um, vm, color);
	DrawTexRect(xa, y1, xb, ya, um, v1, um, vm, color);
	DrawTexRect(xb, y1, x2, ya, um, v1, u2, vm, color);
	// Middle row
	DrawTexRect(x1, ya, xa, yb, u1, vm, um, vm, color);
	DrawTexRect(xa, ya, xb, yb, um, vm, um, vm, color);
	DrawTexRect(xb, ya, x2, yb, um, vm, u2, vm, color);
	// Bottom row
	DrawTexRect(x1, yb, xa, y2, u1, vm, um, v2, color);
	DrawTexRect(xa, yb, xb, y2, um, vm, um, v2, color);
	DrawTexRect(xb, yb, x2, y2, um, vm, u2, v2, color);
}

void DrawBuffer::DrawImage2GridH(ImageID atlas_image, float x1, float y1, float x2, Color color, float corner_scale) {
	const AtlasImage *image = atlas->getImage(atlas_image);
	float um = (image->u1 + image->u2) * 0.5f;
	float iw2 = (image->w * 0.5f) * corner_scale;
	float xa = x1 + iw2;
	float xb = x2 - iw2;
	float u1 = image->u1, v1 = image->v1, u2 = image->u2, v2 = image->v2;
	float y2 = y1 + image->h;
	DrawTexRect(x1, y1, xa, y2, u1, v1, um, v2, color);
	DrawTexRect(xa, y1, xb, y2, um, v1, um, v2, color);
	DrawTexRect(xb, y1, x2, y2, um, v1, u2, v2, color);
}

class AtlasWordWrapper : public WordWrapper {
public:
	// Note: maxW may be height if rotated.
	AtlasWordWrapper(const AtlasFont &atlasfont, float scale, std::string_view str, float maxW, int flags)
		: WordWrapper(str, maxW, flags), atlasfont_(atlasfont), scale_(scale) {
	}

protected:
	float MeasureWidth(std::string_view str) override;

	const AtlasFont &atlasfont_;
	const float scale_;
};

float AtlasWordWrapper::MeasureWidth(std::string_view str) {
	float w = 0.0f;
	for (UTF8 utf(str); !utf.end(); ) {
		uint32_t c = utf.next();
		const AtlasChar *ch = atlasfont_.getChar(c);
		if (!ch) {
			ch = atlasfont_.getChar('?');
		}
		w += ch->wx * scale_;
	}
	return w;
}

void DrawBuffer::MeasureText(FontID font, std::string_view text, float *w, float *h) {
	const AtlasFont *atlasfont = fontAtlas_->getFont(font);
	if (!atlasfont)
		atlasfont = atlas->getFont(font);
	if (!atlasfont) {
		*w = 0.0f;
		*h = 0.0f;
		return;
	}

	unsigned int cval;
	float wacc = 0;
	float maxX = 0.0f;
	int lines = 1;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		if (utf.byteIndex() >= text.length())
			break;
		cval = utf.next();
		// Translate non-breaking space to space.
		if (cval == 0xA0) {
			cval = ' ';
		} else if (cval == '\n') {
			maxX = std::max(maxX, wacc);
			wacc = 0;
			lines++;
			continue;
		} else if (cval == '\t') {
			cval = ' ';
		} else if (cval == '&' && utf.peek() != '&') {
			// Ignore lone ampersands
			continue;
		}
		const AtlasChar *c = atlasfont->getChar(cval);
		if (c) {
			wacc += c->wx * fontscalex;
		}
	}
	if (w) *w = std::max(wacc, maxX);
	if (h) *h = atlasfont->height * fontscaley * lines;
}

void DrawBuffer::MeasureTextRect(FontID font_id, std::string_view text, const Bounds &bounds, float *w, float *h, int align) {
	if (text.empty() || font_id.isInvalid()) {
		*w = 0.0f;
		*h = 0.0f;
		return;
	}

	std::string toMeasure = std::string(text);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	if (wrap) {
		const AtlasFont *font = fontAtlas_->getFont(font_id);
		if (!font)
			font = atlas->getFont(font_id);
		if (!font) {
			*w = 0.0f;
			*h = 0.0f;
			return;
		}
		AtlasWordWrapper wrapper(*font, fontscalex, toMeasure, bounds.w, wrap);
		toMeasure = wrapper.Wrapped();
	}
	MeasureText(font_id, toMeasure, w, h);
}

void DrawBuffer::DrawTextShadow(FontID font, std::string_view text, float x, float y, Color color, int flags) {
	uint32_t alpha = (color >> 1) & 0xFF000000;
	DrawText(font, text, x + 2, y + 2, alpha, flags);
	DrawText(font, text, x, y, color, flags);
}

void DrawBuffer::DoAlign(int flags, float *x, float *y, float *w, float *h) {
	if (flags & ALIGN_HCENTER) *x -= *w / 2;
	if (flags & ALIGN_RIGHT) *x -= *w;
	if (flags & ALIGN_VCENTER) *y -= *h / 2;
	if (flags & ALIGN_BOTTOM) *y -= *h;
}

// TODO: Actually use the rect properly, take bounds.
void DrawBuffer::DrawTextRect(FontID font, std::string_view text, float x, float y, float w, float h, Color color, int align) {
	if (align & ALIGN_HCENTER) {
		x += w / 2;
	} else if (align & ALIGN_RIGHT) {
		x += w;
	}
	if (align & ALIGN_VCENTER) {
		y += h / 2;
	} else if (align & ALIGN_BOTTOM) {
		y += h;
	}

	std::string toDraw(text);
	int wrap = align & (FLAG_WRAP_TEXT | FLAG_ELLIPSIZE_TEXT);
	const AtlasFont *atlasfont = fontAtlas_->getFont(font);
	if (!atlasfont)
		atlasfont = atlas->getFont(font);
	if (wrap && atlasfont) {
		AtlasWordWrapper wrapper(*atlasfont, fontscalex, toDraw, w, wrap);
		toDraw = wrapper.Wrapped();
	}

	float totalWidth, totalHeight;
	MeasureTextRect(font, toDraw, Bounds(x, y, w, h), &totalWidth, &totalHeight, align);

	std::vector<std::string> lines;
	SplitString(toDraw, '\n', lines);

	float baseY = y;
	if (align & ALIGN_VCENTER) {
		baseY -= totalHeight / 2;
		align = align & ~ALIGN_VCENTER;
	} else if (align & ALIGN_BOTTOM) {
		baseY -= totalHeight;
		align = align & ~ALIGN_BOTTOM;
	}

	// This allows each line to be horizontally centered by itself.
	for (const std::string &line : lines) {
		DrawText(font, line.c_str(), x, baseY, color, align);

		float tw, th;
		MeasureText(font, line, &tw, &th);
		baseY += th;
	}
}

// ROTATE_* doesn't yet work right.
void DrawBuffer::DrawText(FontID font, std::string_view text, float x, float y, Color color, int align) {
	// rough estimate
	int textLen = (int)text.length();
	if (count_ + textLen * 6 > MAX_VERTS) {
		Flush(true);
		if (textLen * 6 >= MAX_VERTS) {
			textLen = std::min(MAX_VERTS / 6 - 10, (int)textLen);
		}
	}
	text = text.substr(0, textLen);

	const AtlasFont *atlasfont = fontAtlas_->getFont(font);
	if (!atlasfont)
		atlasfont = atlas->getFont(font);
	if (!atlasfont)
		return;
	unsigned int cval;
	float w, h;
	MeasureText(font, text, &w, &h);
	if (align) {
		DoAlign(align, &x, &y, &w, &h);
	}

	y += atlasfont->ascend * fontscaley;

	float sx = x;
	UTF8 utf(text);
	for (size_t i = 0; i < textLen; i++) {
		if (utf.end())
			break;
		cval = utf.next();
		// Translate non-breaking space to space.
		if (cval == 0xA0) {
			cval = ' ';
		} else if (cval == '\n') {
			y += atlasfont->height * fontscaley;
			x = sx;
			continue;
		} else if (cval == '\t') {
			cval = ' ';
		} else if (cval == '&' && utf.peek() != '&') {
			// Ignore lone ampersands
			continue;
		}
		const AtlasChar *ch = atlasfont->getChar(cval);
		if (!ch)
			ch = atlasfont->getChar('?');
		if (ch) {
			const AtlasChar &c = *ch;
			float cx1, cy1, cx2, cy2;
			cx1 = x + c.ox * fontscalex;
			cy1 = y + c.oy * fontscaley;
			cx2 = x + (c.ox + c.pw) * fontscalex;
			cy2 = y + (c.oy + c.ph) * fontscaley;
			V(cx1,	cy1, color, c.sx, c.sy);
			V(cx2,	cy1, color, c.ex, c.sy);
			V(cx2,	cy2, color, c.ex, c.ey);
			V(cx1,	cy1, color, c.sx, c.sy);
			V(cx2,	cy2, color, c.ex, c.ey);
			V(cx1,	cy2, color, c.sx, c.ey);
			x += c.wx * fontscalex;
		}
	}
}
