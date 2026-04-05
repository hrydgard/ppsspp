#include "Common/Data/Color/RGBAUtil.h"

template <typename T>
static T clamp(T f, T low, T high) {
	if (f < low)
		return low;
	if (f > high)
		return high;
	return f;
}

uint32_t whiteAlpha(float alpha) {
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	uint32_t color = (int)(alpha*255) << 24;
	color |= 0xFFFFFF;
	return color;
}

uint32_t blackAlpha(float alpha) {
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	return (int)(alpha*255)<<24;
}

uint32_t colorAlpha(uint32_t rgb, float alpha) {
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;
	return ((int)(alpha*255)<<24) | (rgb & 0xFFFFFF);
}

uint32_t colorBlend(uint32_t rgb1, uint32_t rgb2, float alpha) {
	const float invAlpha = (1.0f - alpha);
	int r = (int)(((rgb1 >> 0) & 0xFF) * alpha + ((rgb2 >> 0) & 0xFF) * invAlpha);
	int g = (int)(((rgb1 >> 8) & 0xFF) * alpha + ((rgb2 >> 8) & 0xFF) * invAlpha);
	int b = (int)(((rgb1 >> 16) & 0xFF) * alpha + ((rgb2 >> 16) & 0xFF) * invAlpha);
	int a = (int)(((rgb1 >> 24) & 0xFF) * alpha + ((rgb2 >> 24) & 0xFF) * invAlpha);

	uint32_t c = clamp(a, 0, 255) << 24;
	c |= clamp(b, 0, 255) << 16;
	c |= clamp(g, 0, 255) << 8;
	c |= clamp(r, 0, 255);
	return c;
}

uint32_t colorAdd(uint32_t color, uint32_t color2) {
	const int r = (int)((color >> 0) & 0xFF) + (int)((color2 >> 0) & 0xFF);
	const int g = (int)((color >> 8) & 0xFF) + (int)((color2 >> 8) & 0xFF);
	const int b = (int)((color >> 16) & 0xFF) + (int)((color2 >> 16) & 0xFF);
	const int a = (int)((color >> 24) & 0xFF) + (int)((color2 >> 24) & 0xFF);
	uint32_t c = clamp(a, 0, 255) << 24;
	c |= clamp(b, 0, 255) << 16;
	c |= clamp(g, 0, 255) << 8;
	c |= clamp(r, 0, 255);
	return c;
}

uint32_t alphaMul(uint32_t color, float alphaMul) {
	const uint32_t rgb = color & 0xFFFFFF;
	int32_t alpha = color >> 24;
	alpha = (int32_t)(alpha * alphaMul);
	if (alpha < 0)
		alpha = 0;
	if (alpha > 255)
		alpha = 255;
	return (alpha << 24) | rgb;
}

uint32_t rgba(float r, float g, float b, float alpha) {
	uint32_t color = (int)(alpha * 255.0f) << 24;
	color |= (int)(b * 255.0f) << 16;
	color |= (int)(g * 255.0f) << 8;
	color |= (int)(r * 255.0f);
	return color;
}

uint32_t rgba_clamp(float r, float g, float b, float a) {
	return rgba(clamp(r, 0.0f, 1.0f), clamp(g, 0.0f, 1.0f), clamp(b, 0.0f, 1.0f), clamp(a, 0.0f, 1.0f));
}
