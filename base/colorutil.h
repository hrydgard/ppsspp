#pragma once

#include "base/basictypes.h"

uint32_t whiteAlpha(float alpha);
uint32_t blackAlpha(float alpha);
uint32_t colorAlpha(uint32_t color, float alpha);
uint32_t alphaMul(uint32_t color, float alphaMul);
uint32_t rgba(float r, float g, float b, float alpha);
uint32_t rgba_clamp(float r, float g, float b, float alpha);
uint32_t hsva(float h, float s, float v, float alpha);
