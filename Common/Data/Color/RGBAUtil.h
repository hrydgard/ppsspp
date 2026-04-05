#pragma once

#include <cstdint>

uint32_t whiteAlpha(float alpha);
uint32_t blackAlpha(float alpha);
uint32_t colorAlpha(uint32_t color, float alpha);
uint32_t colorBlend(uint32_t color, uint32_t color2, float alpha);
uint32_t colorAdd(uint32_t color, uint32_t color2);
uint32_t alphaMul(uint32_t color, float alphaMul);
uint32_t rgba(float r, float g, float b, float alpha);
uint32_t rgba_clamp(float r, float g, float b, float alpha);

typedef unsigned int Color;

#define COLOR(i) (((i&0xFF) << 16) | (i & 0xFF00) | ((i & 0xFF0000) >> 16) | 0xFF000000)
inline Color darkenColor(Color color) {
	return (color & 0xFF000000) | ((color >> 1) & 0x7F7F7F);
}

inline Color lightenColor(Color color) {
	color = ~color;
	color = (color & 0xFF000000) | ((color >> 1) & 0x7F7F7F);
	return ~color;
}
