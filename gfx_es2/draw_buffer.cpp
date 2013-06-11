#include <algorithm>
#include <cmath>
#ifdef PANDORA
#include <stddef.h>
#endif

#include "base/display.h"
#include "base/logging.h"
#include "math/math_util.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "gfx/texture_atlas.h"
#include "gfx/gl_debug_log.h"
#include "gfx/gl_common.h"
#include "util/text/utf8.h"

enum {
	// Enough?
	MAX_VERTS = 65536,
};

// #define USE_VBO

DrawBuffer::DrawBuffer() : count_(0), atlas(0) {
	verts_ = new Vertex[MAX_VERTS];
	fontscalex = 1.0f;
	fontscaley = 1.0f;
	inited_ = false;
}

DrawBuffer::~DrawBuffer() {
	delete [] verts_;
}

void DrawBuffer::Init(bool registerAsHolder) {
	if (inited_)
		return;
	inited_ = true;
	glGenBuffers(1, (GLuint *)&vbo_);
	if (registerAsHolder)
		register_gl_resource_holder(this);
}

void DrawBuffer::Shutdown() {
	glDeleteBuffers(1, (GLuint *)&vbo_);
	vbo_ = 0;
	inited_ = false;
	unregister_gl_resource_holder(this);
}

void DrawBuffer::GLLost() {
	inited_ = false;
	Init(false);
}

void DrawBuffer::Begin(const GLSLProgram *program, DrawBufferMode dbmode) {
	Init();
	program_ = program;
	count_ = 0;
	mode_ = dbmode;
}

void DrawBuffer::End() {
	// Currently does nothing, but call it!
}

void DrawBuffer::Flush(bool set_blend_state) {
	glsl_bind(program_);
	if (count_ == 0)
		return;
#ifdef USE_VBO
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * count_, verts_, GL_STREAM_DRAW);
	if (set_blend_state) {
		glstate.blend.enable();
		glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glUniform1i(program_->sampler0, 0);
	glEnableVertexAttribArray(program_->a_position);
	glEnableVertexAttribArray(program_->a_color);
	if (program_->a_texcoord0 != -1)
		glEnableVertexAttribArray(program_->a_texcoord0);
	GL_CHECK();
	glVertexAttribPointer(program_->a_position, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
	glVertexAttribPointer(program_->a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, rgba));
	if (program_->a_texcoord0 != -1)
		glVertexAttribPointer(program_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
	glDrawArrays(mode_ == DBMODE_LINES ? GL_LINES : GL_TRIANGLES, 0, count_);
	GL_CHECK();
	glDisableVertexAttribArray(program_->a_position);
	glDisableVertexAttribArray(program_->a_color);
	if (program_->a_texcoord0 != -1)
		glDisableVertexAttribArray(program_->a_texcoord0);
	GL_CHECK();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
	if (set_blend_state) {
		glstate.blend.enable();
		glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUniform1i(program_->sampler0, 0);
	glEnableVertexAttribArray(program_->a_position);
	glEnableVertexAttribArray(program_->a_color);
	if (program_->a_texcoord0 != -1)
		glEnableVertexAttribArray(program_->a_texcoord0);
	GL_CHECK();
	glVertexAttribPointer(program_->a_position, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)&verts_[0].x);
	glVertexAttribPointer(program_->a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)&verts_[0].rgba);
	if (program_->a_texcoord0 != -1)
		glVertexAttribPointer(program_->a_texcoord0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)&verts_[0].u);
	glDrawArrays(mode_ == DBMODE_LINES ? GL_LINES : GL_TRIANGLES, 0, count_);
	GL_CHECK();
	glDisableVertexAttribArray(program_->a_position);
	glDisableVertexAttribArray(program_->a_color);
	if (program_->a_texcoord0 != -1)
		glDisableVertexAttribArray(program_->a_texcoord0);
	GL_CHECK();
#endif
	count_ = 0;
}

void DrawBuffer::V(float x, float y, float z, uint32 color, float u, float v) {
	if (count_ >= MAX_VERTS) {
		FLOG("Overflowed the DrawBuffer");
		return;
	}

	Vertex *vert = &verts_[count_++];
	vert->x = x;
	vert->y = y;
	vert->z = z;
	vert->rgba = color;
	vert->u = u;
	vert->v = v;
}

void DrawBuffer::Rect(float x, float y, float w, float h, uint32 color, int align) {
	DoAlign(align, &x, &y, &w, &h);
	RectVGradient(x, y, w, h, color, color);
}

void DrawBuffer::RectVGradient(float x, float y, float w, float h, uint32 colorTop, uint32 colorBottom) {
	V(x,		 y,     0, colorTop,    0, 0);
	V(x + w, y,		 0, colorTop,    1, 0);
	V(x + w, y + h, 0, colorBottom, 1, 1);
	V(x,		 y,     0, colorTop,    0, 0);
	V(x + w, y + h, 0, colorBottom, 1, 1);
	V(x,		 y + h, 0, colorBottom, 0, 1);
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
	uint32 color) {
		V(x,	   y,     0, color, u, v);
		V(x + w, y,	   0, color, u + uw, v);
		V(x + w, y + h, 0, color, u + uw, v + uh);
		V(x,	   y,     0, color, u, v);
		V(x + w, y + h, 0, color, u + uw, v + uh);
		V(x,	   y + h, 0, color, u, v + uh);
}

void DrawBuffer::MeasureImage(int atlas_image, float *w, float *h) {
	const AtlasImage &image = atlas->images[atlas_image];
	*w = (float)image.w;
	*h = (float)image.h;
}

void DrawBuffer::DrawImage(int atlas_image, float x, float y, float scale, Color color, int align) {
	const AtlasImage &image = atlas->images[atlas_image];
	float w = (float)image.w * scale;
	float h = (float)image.h * scale;
	if (align & ALIGN_HCENTER) x -= w / 2;
	if (align & ALIGN_RIGHT) x -= w;
	if (align & ALIGN_VCENTER) y -= h / 2;
	if (align & ALIGN_BOTTOM) y -= h;
	DrawImageStretch(atlas_image, x, y, x + w, y + h, color);
}

void DrawBuffer::DrawImageStretch(int atlas_image, float x1, float y1, float x2, float y2, Color color) {
	const AtlasImage &image = atlas->images[atlas_image];
	V(x1,	y1, color, image.u1, image.v1);
	V(x2,	y1, color, image.u2, image.v1);
	V(x2,	y2, color, image.u2, image.v2);
	V(x1,	y1, color, image.u1, image.v1);
	V(x2,	y2, color, image.u2, image.v2);
	V(x1,	y2, color, image.u1, image.v2);
}

inline void rot(float *v, float angle, float xc, float yc) {
	v[0]-=xc;
	v[1]-=yc;

	float ca=cosf(angle),sa=sinf(angle);

	float t0 = v[0] * ca + v[1] * -sa;
	float t1 = v[0] * sa + v[1] *	ca;

	v[0] = t0 + xc;
	v[1] = t1 + yc;
}


void DrawBuffer::DrawImageRotated(int atlas_image, float x, float y, float scale, float angle, Color color, bool mirror_h) {
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
void DrawBuffer::Circle(float xc, float yc, float radius, float thickness, int segments, float startAngle, uint32 color, float u_mul) {
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

void DrawBuffer::DrawImage4Grid(int atlas_image, float x1, float y1, float x2, float y2, Color color, float corner_scale) {
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

void DrawBuffer::DrawImage2GridH(int atlas_image, float x1, float y1, float x2, Color color, float corner_scale) {
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

void DrawBuffer::MeasureText(int font, const char *text, float *w, float *h) {
	const AtlasFont &atlasfont = *atlas->fonts[font];

	unsigned int cval;
	float wacc = 0;
	float maxX = 0.0f;
	int lines = 1;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		cval = utf.next();
		if (cval == '\n') {
			maxX = std::max(maxX, wacc);
			wacc = 0;
			lines++;
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

// TODO: Unicode support.
// U+4E00–U+9FBF Kanji
// U+3040–U+309F Hiragana
// U+30A0–U+30FF Katakana

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

	DrawText(font, text, x, y, color, align);
}

// ROTATE_* doesn't yet work right.
void DrawBuffer::DrawText(int font, const char *text, float x, float y, Color color, int align) {
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
		if (cval == '\n') {
			y += atlasfont.height * fontscaley;
			x = sx;
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

void DrawBuffer::EnableBlend(bool enable) {
	glstate.blend.set(enable);
	glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void DrawBuffer::SetClipRect(float x, float y, float w, float h)
{
	// Sigh, OpenGL is upside down.
	glstate.scissorRect.set(x, dp_yres - y, w, h);
	glstate.scissorTest.enable();
}

void DrawBuffer::NoClip()
{
	glstate.scissorTest.disable();
}
