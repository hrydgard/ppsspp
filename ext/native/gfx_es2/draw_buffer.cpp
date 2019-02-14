#include <algorithm>
#include <cmath>
#include <vector>
#include <stddef.h>

#include "base/display.h"
#include "base/logging.h"
#include "base/stringutil.h"
#include "math/math_util.h"
#include "gfx/texture_atlas.h"
#include "gfx/gl_debug_log.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/draw_text.h"
#include "util/text/utf8.h"
#include "util/text/wrap_text.h"

enum {
	// Enough?
	MAX_VERTS = 65536,
};

DrawBuffer::DrawBuffer() : count_(0), atlas(0) {
	verts_ = new Vertex[MAX_VERTS];
	fontscalex = 1.0f;
	fontscaley = 1.0f;
	inited_ = false;
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

	if (pipeline->RequiresBuffer()) {
		vbuf_ = draw_->CreateBuffer(MAX_VERTS * sizeof(Vertex), BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	} else {
		vbuf_ = nullptr;
	}
}

Draw::InputLayout *DrawBuffer::CreateInputLayout(Draw::DrawContext *t3d) {
	using namespace Draw;
	InputLayoutDesc desc = {
		{
			{ sizeof(Vertex), false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ 0, SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	return t3d->CreateInputLayout(desc);
}

void DrawBuffer::Shutdown() {
	if (vbuf_) {
		vbuf_->Release();
		vbuf_ = nullptr;
	}
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
		ELOG("DrawBuffer: No program set, skipping flush!");
		count_ = 0;
		return;
	}
	draw_->BindPipeline(pipeline_);

	VsTexColUB ub{};
	memcpy(ub.WorldViewProj, drawMatrix_.getReadPtr(), sizeof(Matrix4x4));
	draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	if (vbuf_) {
		draw_->UpdateBuffer(vbuf_, (const uint8_t *)verts_, 0, sizeof(Vertex) * count_, Draw::UPDATE_DISCARD);
		draw_->BindVertexBuffers(0, 1, &vbuf_, nullptr);
		int offset = 0;
		draw_->Draw(count_, offset);
	} else {
		draw_->DrawUP((const void *)verts_, count_);
	}
	count_ = 0;
}

void DrawBuffer::V(float x, float y, float z, uint32_t color, float u, float v) {
	if (count_ >= MAX_VERTS) {
		FLOG("Overflowed the DrawBuffer");
		return;
	}

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
	RectVGradient(x, y, w, h, color, color);
}

void DrawBuffer::hLine(float x1, float y, float x2, uint32_t color) {
	Rect(x1, y, x2 - x1, pixel_in_dps_y, color);
}

void DrawBuffer::vLine(float x, float y1, float y2, uint32_t color) {
	Rect(x, y1, pixel_in_dps_x, y2 - y1, color);
}

void DrawBuffer::vLineAlpha50(float x, float y1, float y2, uint32_t color) {
	Rect(x, y1, pixel_in_dps_x, y2 - y1, (color | 0xFF000000) & 0x7F000000);
}

void DrawBuffer::RectVGradient(float x, float y, float w, float h, uint32_t colorTop, uint32_t colorBottom) {
	V(x,		 y,     0, colorTop,    0, 0);
	V(x + w, y,		 0, colorTop,    1, 0);
	V(x + w, y + h, 0, colorBottom, 1, 1);
	V(x,		 y,     0, colorTop,    0, 0);
	V(x + w, y + h, 0, colorBottom, 1, 1);
	V(x,		 y + h, 0, colorBottom, 0, 1);
}

void DrawBuffer::RectOutline(float x, float y, float w, float h, uint32_t color, int align) {
	hLine(x, y, x + w + pixel_in_dps_x, color);
	hLine(x, y + h, x + w + pixel_in_dps_x, color);

	vLine(x, y, y + h + pixel_in_dps_y, color);
	vLine(x + w, y, y + h + pixel_in_dps_y, color);
}

void DrawBuffer::MultiVGradient(float x, float y, float w, float h, GradientStop *stops, int numStops) {
	for (int i = 0; i < numStops - 1; i++) {
		float t0 = stops[i].t, t1 = stops[i+1].t;
		uint32_t c0 = stops[i].t, c1 = stops[i+1].t;
		RectVGradient(x, y + h * t0, w, h * (t1 - t0), c0, c1);
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

void DrawBuffer::Line(int atlas_image, float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
	const AtlasImage &image = atlas->images[atlas_image];

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

	V(x[0],	y[0], color, image.u1, image.v1);
	V(x[1],	y[1], color, image.u2, image.v1);
	V(x[2],	y[2], color, image.u1, image.v2);
	V(x[2],	y[2], color, image.u1, image.v2);
	V(x[1],	y[1], color, image.u2, image.v1);
	V(x[3],	y[3], color, image.u2, image.v2);
}

void DrawBuffer::MeasureImage(ImageID atlas_image, float *w, float *h) {
	const AtlasImage &image = atlas->images[atlas_image];
	*w = (float)image.w;
	*h = (float)image.h;
}

void DrawBuffer::DrawImage(ImageID atlas_image, float x, float y, float scale, Color color, int align) {
	const AtlasImage &image = atlas->images[atlas_image];
	float w = (float)image.w * scale;
	float h = (float)image.h * scale;
	if (align & ALIGN_HCENTER) x -= w / 2;
	if (align & ALIGN_RIGHT) x -= w;
	if (align & ALIGN_VCENTER) y -= h / 2;
	if (align & ALIGN_BOTTOM) y -= h;
	DrawImageStretch(atlas_image, x, y, x + w, y + h, color);
}

void DrawBuffer::DrawImageStretch(ImageID atlas_image, float x1, float y1, float x2, float y2, Color color) {
	const AtlasImage &image = atlas->images[atlas_image];
	V(x1,	y1, color, image.u1, image.v1);
	V(x2,	y1, color, image.u2, image.v1);
	V(x2,	y2, color, image.u2, image.v2);
	V(x1,	y1, color, image.u1, image.v1);
	V(x2,	y2, color, image.u2, image.v2);
	V(x1,	y2, color, image.u1, image.v2);
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
	const AtlasImage &image = atlas->images[atlas_image];
	float w = (float)image.w * scale;
	float h = (float)image.h * scale;
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
	float u1 = image.u1;
	float u2 = image.u2;
	if (mirror_h) {
		float temp = u1;
		u1 = u2;
		u2 = temp;
	}
	const float uv[6][2] = {
		{u1, image.v1},
		{u2, image.v1},
		{u2, image.v2},
		{u1, image.v1},
		{u2, image.v2},
		{u1, image.v2},
	};
	for (int i = 0; i < 6; i++) {
		rot(v[i], angle, x, y);
		V(v[i][0], v[i][1], 0, color, uv[i][0], uv[i][1]);
	}
}

// TODO: add arc support
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
		V(x[0],	y[0], color, u1, 0);
		V(x[1],	y[1], color, u2, 0);
		V(x[2],	y[2], color, u1, 1);
		V(x[1],	y[1], color, u2, 0);
		V(x[3],	y[3], color, u2, 1);
		V(x[2],	y[2], color, u1, 1);
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
	const AtlasImage &image = atlas->images[atlas_image];

	float u1 = image.u1, v1 = image.v1, u2 = image.u2, v2 = image.v2;
	float um = (u2 + u1) * 0.5f;
	float vm = (v2 + v1) * 0.5f;
	float iw2 = (image.w * 0.5f) * corner_scale;
	float ih2 = (image.h * 0.5f) * corner_scale;
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
	const AtlasImage &image = atlas->images[atlas_image];
	float um = (image.u1 + image.u2) * 0.5f;
	float iw2 = (image.w * 0.5f) * corner_scale;
	float xa = x1 + iw2;
	float xb = x2 - iw2;
	float u1 = image.u1, v1 = image.v1, u2 = image.u2, v2 = image.v2;
	float y2 = y1 + image.h;
	DrawTexRect(x1, y1, xa, y2, u1, v1, um, v2, color);
	DrawTexRect(xa, y1, xb, y2, um, v1, um, v2, color);
	DrawTexRect(xb, y1, x2, y2, um, v1, u2, v2, color);
}

class AtlasWordWrapper : public WordWrapper {
public:
	// Note: maxW may be height if rotated.
	AtlasWordWrapper(const AtlasFont &atlasfont, float scale, const char *str, float maxW) : WordWrapper(str, maxW), atlasfont_(atlasfont), scale_(scale) {
	}

protected:
	float MeasureWidth(const char *str, size_t bytes) override;

	const AtlasFont &atlasfont_;
	const float scale_;
};

float AtlasWordWrapper::MeasureWidth(const char *str, size_t bytes) {
	float w = 0.0f;
	for (UTF8 utf(str); utf.byteIndex() < (int)bytes; ) {
		uint32_t c = utf.next();
		if (c == '&') {
			// Skip ampersand prefixes ("&&" is an ampersand.)
			c = utf.next();
		}
		const AtlasChar *ch = atlasfont_.getChar(c);
		if (!ch)
			ch = atlasfont_.getChar('?');

		w += ch->wx * scale_;
	}
	return w;
}

void DrawBuffer::MeasureTextCount(int font, const char *text, int count, float *w, float *h) {
	const AtlasFont &atlasfont = *atlas->fonts[font];

	unsigned int cval;
	float wacc = 0;
	float maxX = 0.0f;
	int lines = 1;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		if (utf.byteIndex() >= count)
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
		const AtlasChar *c = atlasfont.getChar(cval);
		if (c) {
			wacc += c->wx * fontscalex;
		}
	}
	if (w) *w = std::max(wacc, maxX);
	if (h) *h = atlasfont.height * fontscaley * lines;
}

void DrawBuffer::MeasureTextRect(int font, const char *text, int count, const Bounds &bounds, float *w, float *h, int align) {
	if (!text || (uint32_t)font >= (uint32_t)atlas->num_fonts) {
		*w = 0;
		*h = 0;
		return;
	}

	std::string toMeasure = std::string(text, count);
	if (align & FLAG_WRAP_TEXT) {
		AtlasWordWrapper wrapper(*atlas->fonts[font], fontscalex, toMeasure.c_str(), bounds.w);
		toMeasure = wrapper.Wrapped();
	}
	MeasureTextCount(font, toMeasure.c_str(), (int)toMeasure.length(), w, h);
}

void DrawBuffer::MeasureText(int font, const char *text, float *w, float *h) {
	return MeasureTextCount(font, text, (int)strlen(text), w, h);
}

void DrawBuffer::DrawTextShadow(int font, const char *text, float x, float y, Color color, int flags) {
	uint32_t alpha = (color >> 1) & 0xFF000000;
	DrawText(font, text, x + 2, y + 2, alpha, flags);
	DrawText(font, text, x, y, color, flags);
}

void DrawBuffer::DoAlign(int flags, float *x, float *y, float *w, float *h) {
	if (flags & ALIGN_HCENTER) *x -= *w / 2;
	if (flags & ALIGN_RIGHT) *x -= *w;
	if (flags & ALIGN_VCENTER) *y -= *h / 2;
	if (flags & ALIGN_BOTTOM) *y -= *h;
	if (flags & (ROTATE_90DEG_LEFT | ROTATE_90DEG_RIGHT)) {
		std::swap(*w, *h);
		std::swap(*x, *y);
	}
}


// TODO: Actually use the rect properly, take bounds.
void DrawBuffer::DrawTextRect(int font, const char *text, float x, float y, float w, float h, Color color, int align) {
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

	std::string toDraw = text;
	if (align & FLAG_WRAP_TEXT) {
		AtlasWordWrapper wrapper(*atlas->fonts[font], fontscalex, toDraw.c_str(), w);
		toDraw = wrapper.Wrapped();
	}

	float totalWidth, totalHeight;
	MeasureTextRect(font, toDraw.c_str(), (int)toDraw.size(), Bounds(x, y, w, h), &totalWidth, &totalHeight, align);

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
		MeasureText(font, line.c_str(), &tw, &th);
		baseY += th;
	}
}

// ROTATE_* doesn't yet work right.
void DrawBuffer::DrawText(int font, const char *text, float x, float y, Color color, int align) {
	// rough estimate
	if (count_ + strlen(text) * 6 > MAX_VERTS) {
		Flush(true);
	}

	const AtlasFont &atlasfont = *atlas->fonts[font];
	unsigned int cval;
	float w, h;
	MeasureText(font, text, &w, &h);
	if (align) {
		DoAlign(align, &x, &y, &w, &h);
	}

	if (align & ROTATE_90DEG_LEFT) {
		x -= atlasfont.ascend*fontscaley;
		// y += h;
	}
	else
		y += atlasfont.ascend*fontscaley;
	float sx = x;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		cval = utf.next();
		// Translate non-breaking space to space.
		if (cval == 0xA0) {
			cval = ' ';
		} else if (cval == '\n') {
			y += atlasfont.height * fontscaley;
			x = sx;
			continue;
		} else if (cval == '\t') {
			cval = ' ';
		} else if (cval == '&' && utf.peek() != '&') {
			// Ignore lone ampersands
			continue;
		}
		const AtlasChar *ch = atlasfont.getChar(cval);
		if (!ch)
			ch = atlasfont.getChar('?');
		if (ch) {
			const AtlasChar &c = *ch;
			float cx1, cy1, cx2, cy2;
			if (align & ROTATE_90DEG_LEFT) {
				cy1 = y - c.ox * fontscalex;
				cx1 = x + c.oy * fontscaley;
				cy2 = y - (c.ox + c.pw) * fontscalex;
				cx2 = x + (c.oy + c.ph) * fontscaley;
			} else {
				cx1 = x + c.ox * fontscalex;
				cy1 = y + c.oy * fontscaley;
				cx2 = x + (c.ox + c.pw) * fontscalex;
				cy2 = y + (c.oy + c.ph) * fontscaley;
			}
			V(cx1,	cy1, color, c.sx, c.sy);
			V(cx2,	cy1, color, c.ex, c.sy);
			V(cx2,	cy2, color, c.ex, c.ey);
			V(cx1,	cy1, color, c.sx, c.sy);
			V(cx2,	cy2, color, c.ex, c.ey);
			V(cx1,	cy2, color, c.sx, c.ey);
			if (align & ROTATE_90DEG_LEFT)
				y -= c.wx * fontscalex;
			else
				x += c.wx * fontscalex;
		}
	}
}
