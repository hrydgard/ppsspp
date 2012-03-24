// OpenGL ES 1.1

#ifdef ANDROID
#include <GLES/gl.h>
#else
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif
#include <stdio.h>
#include <stdlib.h>

#include "gfx_es1/draw_buffer.h"
#include "gfx/texture_atlas.h"

#include "main_atlas.h"

LAMEBuffer buffer;
LAMEBuffer topbuffer;

#define MAX_VERTS 16384

LAMEBuffer::LAMEBuffer() {
  verts = new Vertex[MAX_VERTS];
  vcount = 0;
  xoffset = 0;
  yoffset = 0;
  fontscalex = 0.37f;
  fontscaley = 0.37f;
}

LAMEBuffer::~LAMEBuffer() {
  delete [] verts;
  verts = 0;
}

void LAMEBuffer::Setup() {
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisable(GL_TEXTURE_2D);
}

void LAMEBuffer::Finish() {
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

// Draws what we have collected so far, so that we can change blend modes etc.
// TODO: Collect states and then blast at the end?
void LAMEBuffer::Flush() {
  if (vcount > 0) {
    glVertexPointer  (3, GL_FLOAT, sizeof(Vertex), (void *)&verts[0].x);
    glColorPointer   (4, GL_UNSIGNED_BYTE, sizeof(Vertex), (void *)&verts[0].rgba);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void *)&verts[0].u);
    glDrawArrays(GL_TRIANGLES, 0, vcount);
    // printf("Drawing %i triangles\n", vcount / 3);
  }
  vcount = 0;
}

void LAMEBuffer::V(float x, float y, uint32 color, float u, float v) {
#ifndef ANDROID
  if (vcount >= MAX_VERTS) {
    printf("Hit max # verts\n");
  }
#endif
  verts[vcount].x = x + xoffset;
  verts[vcount].y = y + yoffset;
  verts[vcount].z = 0.0;
  verts[vcount].rgba = color;
  verts[vcount].u = u;
  verts[vcount].v = v;
  vcount++;
}

void LAMEBuffer::rectFill(int x1, int y1, int x2, int y2, Color color) {
  rectFillFaded(x1, y1, x2, y2, color, color);
}

void LAMEBuffer::rectFillFaded(int x1, int y1, int x2, int y2, Color color1, Color color2) {
  V(x1, y1, color1, 0, 0);
  V(x2, y1, color1, 1, 0);
  V(x2, y2, color2, 1, 1);
  V(x1, y1, color1, 0, 0);
  V(x2, y2, color2, 1, 1);
  V(x1, y2, color2, 0, 1);
}

void LAMEBuffer::MeasureImage(int atlas_image, float *w, float *h) {
  const AtlasImage &image = atlas->images[atlas_image];
  *w = image.w;
  *h = image.h;
}

void LAMEBuffer::DrawImage(int atlas_image, float x, float y, Color color) {
  const AtlasImage &image = atlas->images[atlas_image];
  float w = image.w;
  float h = image.h;
  DrawImageStretch(atlas_image, x, y, x + w, y + h, color);
}

void LAMEBuffer::DrawImageCenter(int atlas_image, float x, float y, Color color) {
  const AtlasImage &image = atlas->images[atlas_image];
  DrawImage(atlas_image, x - image.w/2, y - image.h/2, color);
}

void LAMEBuffer::DrawImageStretch(int atlas_image, float x1, float y1, float x2, float y2, Color color) {
  const AtlasImage &image = atlas->images[atlas_image];
  V(x1,  y1, color, image.u1, image.v1);
  V(x2,  y1, color, image.u2, image.v1);
  V(x2,  y2, color, image.u2, image.v2);
  V(x1,  y1, color, image.u1, image.v1);
  V(x2,  y2, color, image.u2, image.v2);
  V(x1,  y2, color, image.u1, image.v2);
}

void LAMEBuffer::DrawTexRect(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, Color color) {
  V(x1,  y1, color, u1, v1);
  V(x2,  y1, color, u2, v1);
  V(x2,  y2, color, u2, v2);
  V(x1,  y1, color, u1, v1);
  V(x2,  y2, color, u2, v2);
  V(x1,  y2, color, u1, v2);
}

void LAMEBuffer::DrawImage4Grid(int atlas_image, float x1, float y1, float x2, float y2, Color color, float corner_scale) {
  const AtlasImage &image = atlas->images[atlas_image];

  float um = (image.u2 - image.u1) * 0.5f;
  float vm = (image.v2 - image.v1) * 0.5f;
  float iw2 = (image.w * 0.5f) * corner_scale;
  float ih2 = (image.h * 0.5f) * corner_scale;
  float xa = x1 + iw2;
  float xb = x2 - iw2;
  float ya = y1 + ih2;
  float yb = y2 - ih2;
  float u1 = image.u1, v1 = image.v1, u2 = image.u2, v2 = image.v2;
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

void LAMEBuffer::DrawImage2GridH(int atlas_image, float x1, float y1, float x2, Color color, float corner_scale) {
  const AtlasImage &image = atlas->images[atlas_image];
  float um = (image.u2 - image.u1) * 0.5f;
  float iw2 = (image.w * 0.5f) * corner_scale;
  float xa = x1 + iw2;
  float xb = x2 - iw2;
  float u1 = image.u1, v1 = image.v1, u2 = image.u2, v2 = image.v2;
  float y2 = y1 + image.h;
  DrawTexRect(x1, y1, xa, y2, u1, v1, um, v2, color);
  DrawTexRect(xa, y1, xb, y2, um, v1, um, v2, color);
  DrawTexRect(xb, y1, x2, y2, um, v1, u2, v2, color);
}

void LAMEBuffer::MeasureText(int font, const char *text, float *w, float *h) {
  const AtlasFont &atlasfont = *atlas->fonts[font];
  unsigned char cval;
  float wacc = 0, maxh = 0;
  while ((cval = *text++) != '\0') {
    if (cval < 32) continue;
    if (cval > 127) continue;
    AtlasChar c = atlasfont.chars[cval - 32];
    wacc += c.wx * fontscalex;
    maxh = 10.0;
  }
  *w = wacc;
  *h = maxh;
}

void LAMEBuffer::DrawText(int font, const char *text, float x, float y, Color color, int flags) {
  const AtlasFont &atlasfont = *atlas->fonts[font];
  unsigned char cval;
  if (flags) {
    float w, h;
    MeasureText(font, text, &w, &h);
    if (flags & TEXT_HCENTER) x -= w / 2;
    if (flags & TEXT_RIGHT) x -= w;
    if (flags & TEXT_VCENTER) y -= h / 2;
  }
  float sx = x;
  while ((cval = *text++) != '\0') {
    if (cval == '\n') {
      y += 10;
      x = sx;
      continue;
    }
    if (cval < 32) continue;
    if (cval > 127) continue;
    AtlasChar c = atlasfont.chars[cval - 32];
    float cx1 = x + c.ox * fontscalex;
    float cy1 = y + c.oy * fontscaley;
    float cx2 = x + (c.ox + c.pw) * fontscalex;
    float cy2 = y + (c.oy + c.ph) * fontscaley;
    V(cx1,  cy1, color, c.sx, c.sy);
    V(cx2,  cy1, color, c.ex, c.sy);
    V(cx2,  cy2, color, c.ex, c.ey);
    V(cx1,  cy1, color, c.sx, c.sy);
    V(cx2,  cy2, color, c.ex, c.ey);
    V(cx1,  cy2, color, c.sx, c.ey);
    x += c.wx * fontscalex;
  }
}


void LAMEBuffer::hLine(int x1, int y, int x2, Color color) {
  rectFill(x1, y, x2, y + 1, color | 0xFF000000);
}

void LAMEBuffer::hLineDarken(int x1, int y, int x2) {
  rectFill(x1, y, x2, y + 1, 0x80000000);
}

void LAMEBuffer::vLine(int x, int y1, int y2, Color color) {
  rectFill(x, y1, x + 1, y2, color);
}

void LAMEBuffer::vLineAlpha50(int x, int y1, int y2, Color color) {
  vLine(x, y1, y2, (color & 0x00FFFFFF) | 0x80);
}

void LAMEBuffer::rect(int x1, int y1, int x2, int y2, Color color) {
  hLine(x1, y1, x2, color);
  hLine(x1, y2, x2, color);
  vLine(x1, y1, y2, color);
  vLine(x2, y1, y2, color);
}

void LAMEBuffer::rectFillDarken(int x1, int y1, int x2, int y2) {
  rectFill(x1, y1, x2, y2, 0x80000000);
}

void LAMEBuffer::RotateSprite(int atlas_entry, float x, float y, float angle, float scale, Color color) {
  // TODO - will be good for knobs
  /*
  float c = cos(angle + PI/4);
  float s = sin(angle + PI/4);
  
  float x1 = x + c * scale;
  float y1 = y + s * scale;

  float x2 = x + c * scale;
  float y2 = y + s * scale;

  float x3 = x + c * scale;
  float y3 = y + s * scale;

  V(x1,  y1, color1, 0, 0);
  V(x2,  y1, color1, 1, 0);
  V(x2,  y2, color2, 1, 1);
  V(x1,  y1, color1, 0, 0);
  V(x2,  y2, color2, 1, 1);
  V(x1,  y2, color2, 0, 1);
*/
}

void LAMEBuffer::drawText(const TCHAR *text, int x, int y, Color color, int font) {
  DrawText(UBUNTU24, text, x, y+3, color);
}
void LAMEBuffer::drawTextCenter(const TCHAR *text, int x, int y, Color color, int font) {
  DrawText(UBUNTU24, text, x, y+3, color, TEXT_HCENTER);
}
void LAMEBuffer::drawTextShadow(const TCHAR *text, int x, int y, Color color, Color shadowColor, int font) {
  DrawText(UBUNTU24, text, x, y+3, color);
}
void LAMEBuffer::drawTextShadowCenter(const TCHAR *text, int x, int y, Color color, Color shadowColor, int font) {
  DrawText(UBUNTU24, text, x, y+3, color, TEXT_HCENTER);
}
void LAMEBuffer::drawTextContrastCenter(const TCHAR *text, int x, int y, Color color, Color shadowColor, int font) {
  DrawText(UBUNTU24, text, x, y+3, color, TEXT_HCENTER);
}
void LAMEBuffer::drawTextContrast(const TCHAR *text, int x, int y, Color color, Color shadowColor, int font) {
  DrawText(UBUNTU24, text, x, y+3, color);
}
