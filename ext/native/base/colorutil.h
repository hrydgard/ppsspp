#pragma once

#include "base/basictypes.h"

uint32_t whiteAlpha(float alpha);
uint32_t blackAlpha(float alpha);
uint32_t colorAlpha(uint32_t color, float alpha);
uint32_t colorBlend(uint32_t color, uint32_t color2, float alpha);
uint32_t alphaMul(uint32_t color, float alphaMul);
uint32_t rgba(float r, float g, float b, float alpha);
uint32_t rgba_clamp(float r, float g, float b, float alpha);
uint32_t hsva(float h, float s, float v, float alpha);

typedef unsigned int Color;

#define COLOR(i) (((i&0xFF) << 16) | (i & 0xFF00) | ((i & 0xFF0000) >> 16) | 0xFF000000)
inline Color darkenColor(Color color) {
	return (color & 0xFF000000) | ((color >> 1) & 0x7F7F7F);
}
inline Color whitenColor(Color color) {
	return ((color & 0xFF000000) | ((color >> 1) & 0x7F7F7F)) + 0x7F7F7F;
}
inline Color colorInterpol(Color x, Color y, int n) {
	// TODO
	return x;
}
