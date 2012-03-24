#pragma once

#include "base/basictypes.h"
#include "base/color.h"

struct Atlas;

enum {
  ALIGN_LEFT = 0,
	ALIGN_RIGHT = 16,
	ALIGN_TOP = 0,
	ALIGN_BOTTOM = 1,
  ALIGN_HCENTER = 4,
  ALIGN_VCENTER = 8,
	ALIGN_VBASELINE = 32,  // text only, possibly not yet working

	ALIGN_TOPLEFT = ALIGN_TOP | ALIGN_LEFT,
	ALIGN_TOPRIGHT = ALIGN_TOP | ALIGN_RIGHT,
	ALIGN_BOTTOMLEFT = ALIGN_BOTTOM | ALIGN_LEFT,
	ALIGN_BOTTOMRIGHT = ALIGN_BOTTOM | ALIGN_RIGHT,
};

struct GLSLProgram;

enum DrawBufferMode {
  DBMODE_NORMAL = 0,
  DBMODE_LINES = 1
};

struct GradientStop 
{
	float t;
	uint32_t color;
};

// Similar to QuadBuffer but only uses a vertex array that it keeps
// around.
class DrawBuffer {
 public:
  DrawBuffer();
  ~DrawBuffer();

  void Begin(DrawBufferMode mode = DBMODE_NORMAL);
  void End();  // Currently does nothing, but call it!

  int Count() const { return count_; }

  void Flush(const GLSLProgram *program, bool set_blend_state=true);
  void Rect(float x, float y, float w, float h, uint32 color, int align = ALIGN_TOPLEFT);
	void RectVGradient(float x, float y, float w, float h, uint32 colorTop, uint32 colorBottom);

	void MultiVGradient(float x, float y, float w, float h, GradientStop *stops, int numStops);

  void RectCenter(float x, float y, float w, float h, uint32 color) {
    Rect(x - w/2, y - h/2, w, h, color);
  }
  void Rect(float x, float y, float w, float h,
            float u, float v, float uw, float uh, uint32 color);
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
  void MeasureImage(int atlas_image, float *w, float *h);
  void DrawImage(int atlas_image, float x, float y, float scale, Color color = COLOR(0xFFFFFF), int align = ALIGN_TOPLEFT);
  void DrawImageStretch(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF));
  void DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color);
  // Results in 18 triangles. Kind of expensive for a button.
  void DrawImage4Grid(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF), float corner_scale = 1.0);
  // This is only 6 triangles, much cheaper.
  void DrawImage2GridH(int atlas_image, float x1, float y1, float x2, Color color = COLOR(0xFFFFFF), float scale = 1.0);

  void MeasureText(int font, const char *text, float *w, float *h);
  void DrawText(int font, const char *text, float x, float y, Color color = 0xFFFFFFFF, int flags = 0);
  void DrawTextShadow(int font, const char *text, float x, float y, Color color = 0xFFFFFFFF, int flags = 0);

  void RotateSprite(int atlas_entry, float x, float y, float angle, float scale, Color color);
  void SetFontScale(float xs, float ys) {
    fontscalex = xs;
    fontscaley = ys;
  }

	// Utility to avoid having to include gl.h just for this in UI code.
	void EnableBlend(bool enable);

 private:
	void DoAlign(int align, float *x, float *y, float w, float h);
  struct Vertex {
    float x, y, z;
    uint8 r, g, b, a;
    float u, v;
  };

  Vertex *verts_;
  int count_;
  DrawBufferMode mode_;
  const Atlas *atlas;

  float fontscalex;
  float fontscaley;
};

