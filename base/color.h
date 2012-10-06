#pragma once

typedef unsigned int Color;

//have to use a define to ensure constant folding.. with an inline I don't get that, sucks
#define COLOR(i) (((i&0xFF) << 16) | (i & 0xFF00) | ((i & 0xFF0000) >> 16) | 0xFF000000)
inline Color darkenColor(Color color) {
  return (color & 0xFF000000) | ((color >> 1)&0x7F7F7F);
}
inline Color whitenColor(Color color) {
  return ((color & 0xFF000000) | ((color >> 1)&0x7F7F7F)) + 0x7F7F7F;
}
inline Color colorInterpol(Color x, Color y, int n) {
  // TODO
  return x;
}
