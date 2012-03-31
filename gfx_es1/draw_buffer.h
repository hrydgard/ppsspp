// OpenGL-based 2D primitive buffer. For GLES 1.1.

#ifndef __LAMEBUFFER_H__
#define __LAMEBUFFER_H__

#include "base/basictypes.h"
#include "base/color.h"

class Atlas;

enum {
  TEXT_LEFT = 0,
  TEXT_BASELINE = 0,
  TEXT_TOP = 1,
  TEXT_BOTTOM = 2,
  TEXT_HCENTER = 4,
  TEXT_VCENTER = 8,
  TEXT_RIGHT = 16,
};

// Do not inherit from this class.
class LAMEBuffer {
 public:
  LAMEBuffer();
  ~LAMEBuffer();

  void SetAtlas(const Atlas *_atlas) {
    atlas = _atlas;
  }

  void hLine(int x1, int y, int x2, Color color);
  void hLineDarken(int x1, int y, int x2);
  void vLine(int x, int y1, int y2, Color color);
  void vLineAlpha50(int x, int y1, int y2, Color color);

  void rect(int x1, int y1, int x2, int y2, Color color);
  void rectFill(int x1, int y1, int x2, int y2, Color color);
  void rectRectFill(int x1, int y1, int x2, int y2, Color border, Color fill) {
    rectFill(x1,y1,x2,y2,fill);
    rect(x1,y1,x2,y2,border);
  }
  void rectFillFaded(int x1, int y1, int x2, int y2, Color color1, Color color2);
  void rectFillDarkFaded(int x1, int y1, int x2, int y2, Color color) {
    rectFillFaded(x1,y1,x2,y2,color, darkenColor(color));
  }

  void rectFillDarken(int x1, int y1, int x2, int y2);

  void MeasureImage(int atlas_image, float *w, float *h);
  void DrawImage(int atlas_image, float x, float y, Color color = COLOR(0xFFFFFF));
  void DrawImageCenter(int atlas_image, float x, float y, Color color = COLOR(0xFFFFFF));
  void DrawImageStretch(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF));
  void DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color);
  // Results in 18 triangles. Kind of expensive for a button.
  void DrawImage4Grid(int atlas_image, float x1, float y1, float x2, float y2, Color color = COLOR(0xFFFFFF), float corner_scale = 1.0);
  // This is only 6 triangles, much cheaper.
  void DrawImage2GridH(int atlas_image, float x1, float y1, float x2, Color color = COLOR(0xFFFFFF), float scale = 1.0);

  void MeasureText(int font, const char *text, float *w, float *h);
  void DrawText(int font, const char *text, float x, float y, Color color = 0xFFFFFFFF, int flags = 0);

  void RotateSprite(int atlas_entry, float x, float y, float angle, float scale, Color color);

  void drawText(const TCHAR *text, int x, int y, Color color = 0, int font=0);
  void drawTextCenter(const TCHAR *text, int x, int y, Color color, int font=0);
  void drawTextShadow(const TCHAR *text, int x, int y, Color color=0xffffffff, Color shadowColor=0xFF000000, int font=0);
  void drawTextShadowCenter(const TCHAR *text, int x, int y, Color color=0xffffffff, Color shadowColor=0xFF000000, int font=0);
  void drawTextContrastCenter(const TCHAR *text, int x, int y, Color color=0xffffffff, Color shadowColor=0xFF000000, int font=0);
  void drawTextContrast(const TCHAR *text, int x, int y, Color color=0xffffffff, Color shadowColor=0xFF000000, int font=0);

  void SetFontScale(float xs, float ys) {
    fontscalex = xs;
    fontscaley = ys;
  }

  // Offset management, for easier hierarchical drawing
  void PushOffset(int xoff, int yoff) {
    // TODO: Use a stack
    xoffset = xoff; yoffset = yoff;
  }
  void PopOffset(int xoff, int yoff) {
    xoffset = 0; yoffset = 0;
  }

  // Only use in bunches of three. To draw triangles.
  inline void V(float x, float y, uint32 color, float u, float v);

  // Call these around all Flush calls of drawbuffers. More than one flush call
  // is fine.
  static void Setup();  // Enables client state.
  static void Finish();  // Disables client state

  // Draws what we have collected so far, so that we can change blend modes etc.
  void Flush();

 private:
  const Atlas *atlas;

  float xoffset, yoffset;

  float fontscalex;
  float fontscaley;

  struct Vertex {
    float x, y, z;
    uint32 rgba;
    float u, v;
  };
  int vcount;
  Vertex *verts;
};

// For regular non blended drawing. No alpha, no alpha test.
extern LAMEBuffer buffer;

// The two blend buffers could be combined using premultiplied alpha.
// But who cares.

// For regular blended drawing. Standard alpha, no alpha test.
extern LAMEBuffer topbuffer;

#endif //__LAMEBUFFER_H__
