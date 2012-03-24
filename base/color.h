#pragma once

// #define COLOR16

#ifdef COLOR16
typedef unsigned short Color;
#else
typedef unsigned int Color;
#endif


//have to use a define to ensure constant folding.. with an inline I don't get that, sucks
#ifdef COLOR16
#error
#define COLOR(i) (short16)(((i&0xF80000)>>8) | ((i&0xFC00)>>5) | ((i&0xF8)>>3))
inline Color darkenColor(Color color) {
  return (color>>1)&0x7BEF;
}
inline Color whitenColor(Color color) {
  return ((color>>1)&0x7BEF)+0x7BEF;
}
//single multiplication 16-bit color alpha blending... pretty cool huh?
inline Color colorInterpol(Color x, Color y, int n)
{
  uint32 c1 = (x|(x<<16))&0x7E0F81F;
  uint32 c2 = (y|(y<<16))&0x7E0F81F;
  uint32 c  = (c1 + ((c2 - c1)*n >> 5)) & 0x7E0F81F;
  return c | (c >> 16);
}

#else
#define COLOR(i) (((i << 16) & 0xFF0000) | (i & 0xFF00) | ((i >> 16) & 0xFF) | 0xFF000000)
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
#endif