#pragma once

// "Immediate mode"-lookalike buffered drawing. Very fast way to draw 2D.

#include "base/basictypes.h"
#include "base/color.h"
#include "base/display.h"
#include "gfx/gl_lost_manager.h"

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

	// Only for text drawing
	ROTATE_90DEG_LEFT = 256,
	ROTATE_90DEG_RIGHT = 512,
	ROTATE_180DEG = 1024,
};

struct GLSLProgram;

enum DrawBufferMode {
	DBMODE_NORMAL = 0,
	DBMODE_LINES = 1
};

struct GradientStop {
	float t;
	uint32_t color;
};

class DrawBuffer : public GfxResourceHolder {
public:
	DrawBuffer();
	~DrawBuffer();

	void Begin(const GLSLProgram *program, DrawBufferMode mode = DBMODE_NORMAL);
	void End();

	// TODO: Enforce these. Now Init is autocalled and shutdown not called.
	void Init(bool registerAsHolder = true);
	void Shutdown();
	virtual void GLLost();

	int Count() const { return count_; }

	void Flush(bool set_blend_state=true);

	void Rect(float x, float y, float w, float h, uint32 color, int align = ALIGN_TOPLEFT);
	void hLine(float x1, float y, float x2, uint32 color) { Rect(x1, y, x2 - x1, pixel_in_dps, color); }
	void vLine(float x, float y1, float y2, uint32 color) { Rect(x, y1, pixel_in_dps, y2 - y1, color); }
	void vLineAlpha50(float x, float y1, float y2, uint32 color) { Rect(x, y1, pixel_in_dps, y2 - y1, (color | 0xFF000000) & 0x7F000000); }

	void RectOutline(float x, float y, float w, float h, uint32 color, int align = ALIGN_TOPLEFT) {
		hLine(x, y, x + w + pixel_in_dps, color);
		hLine(x, y + h, x + w + pixel_in_dps, color);

		vLine(x, y, y + h + pixel_in_dps, color);
		vLine(x + w, y, y + h + pixel_in_dps, color);
	}
	
	void RectVGradient(float x, float y, float w, float h, uint32 colorTop, uint32 colorBottom);
	void RectVDarkFaded(float x, float y, float w, float h, uint32 colorTop) {
		RectVGradient(x, y, w, h, colorTop, darkenColor(colorTop));
	}

	void MultiVGradient(float x, float y, float w, float h, GradientStop *stops, int numStops);

	void RectCenter(float x, float y, float w, float h, uint32 color) {
		Rect(x - w/2, y - h/2, w, h, color);
	}
	void Rect(float x, float y, float w, float h, float u, float v, float uw, float uh, uint32 color);

	void V(float x, float y, float z, uint32 color, float u, float v);
	void V(float x, float y, uint32 color, float u, float v) {
		V(x, y, 0.0f, color, u, v);
	}

	void Circle(float x, float y, float radius, float thickness, int segments, float startAngle, uint32 color, float u_mul);

	// New drawing APIs

	// Must call this before you use any functions with atlas_image etc.
	void SetAtlas(const Atlas *_atlas) {
		atlas = _atlas;
	}
	const Atlas *GetAtlas() const { return atlas; }
	void MeasureImage(int atlas_image, float *w, float *h);
	void DrawImage(int atlas_image, float x, float y, float scale, Color color = COLOR(0xFFFFFF), int align = ALIGN_TOPLEFT);
	void DrawImageStretch(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF));
	void DrawImageRotated(int atlas_image, float x, float y, float scale, float angle, Color color = COLOR(0xFFFFFF), bool mirror_h = false);	// Always centers
	void DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color);
	// Results in 18 triangles. Kind of expensive for a button.
	void DrawImage4Grid(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF), float corner_scale = 1.0);
	// This is only 6 triangles, much cheaper.
	void DrawImage2GridH(int atlas_image, float x1, float y1, float x2, Color color = COLOR(0xFFFFFF), float scale = 1.0);

	void MeasureText(int font, const char *text, float *w, float *h);
	void DrawTextRect(int font, const char *text, float x, float y, float w, float h, Color color = 0xFFFFFFFF, int align = 0);
	void DrawText(int font, const char *text, float x, float y, Color color = 0xFFFFFFFF, int align = 0);
	void DrawTextShadow(int font, const char *text, float x, float y, Color color = 0xFFFFFFFF, int align = 0);

	void RotateSprite(int atlas_entry, float x, float y, float angle, float scale, Color color);
	void SetFontScale(float xs, float ys) {
		fontscalex = xs;
		fontscaley = ys;
	}

	// Utility to avoid having to include gl.h just for this in UI code.
	void EnableBlend(bool enable);

	// Rectangular clipping, implemented using scissoring.
	// Must flush before and after.
	void SetClipRect(float x1, float y1, float x2, float y2);
	void NoClip();

private:
	void DoAlign(int flags, float *x, float *y, float *w, float *h);
	struct Vertex {
		float x, y, z;
		uint32_t rgba;
		float u, v;
	};

	const GLSLProgram *program_;

	int vbo_;
	Vertex *verts_;
	int count_;
	DrawBufferMode mode_;
	const Atlas *atlas;

	bool inited_;
	float fontscalex;
	float fontscaley;
};

