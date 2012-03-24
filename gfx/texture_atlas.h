#pragma once

struct AtlasChar {
  // texcoords
  float sx, sy, ex, ey;
  // offset from the origin
  float ox, oy;
  // distance to move the origin forward
  float wx;
  // size in pixels
  unsigned short pw, ph;
};

struct AtlasFont {
  float padding;
  float height;
  float ascend;
  float distslope;
  AtlasChar chars[96];
};

struct AtlasImage {
  float u1, v1, u2, v2;
  int w, h;
};

struct Atlas {
  const char *filename;
  const AtlasFont **fonts;
  int num_fonts;
  const AtlasImage *images;
  int num_images;
};

enum {
  PRINT_RIGHT = 1,
  PRINT_CENTER = 2,
};
