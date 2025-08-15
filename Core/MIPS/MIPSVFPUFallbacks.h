#pragma once

// These are our old implementation of VFPU math functions, that don't make use of the
// accuracy-improving tables from #16984.

float vfpu_asin_fallback(float angle);
float vfpu_sqrt_fallback(float a);
float vfpu_rsqrt_fallback(float a);
float vfpu_sin_fallback(float a);
float vfpu_cos_fallback(float a);
void vfpu_sincos_fallback(float a, float &s, float &c);
float vfpu_rcp_fallback(float x);
float vfpu_log2_fallback(float x);
float vfpu_exp2_fallback(float x);
