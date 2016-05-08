#include "base/colorutil.h"

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

uint32_t alphaMul(uint32_t color, float alphaMul) {
	uint32_t rgb = color & 0xFFFFFF;
	uint32_t alpha = color >> 24;
	alpha *= alphaMul;
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 255.0f) alpha = 255.0f;
	return ((int)(alpha)<<24) | (rgb & 0xFFFFFF);
}

uint32_t rgba(float r, float g, float b, float alpha) {
	uint32_t color = (int)(alpha*255)<<24;
	color |= (int)(b*255)<<16;
	color |= (int)(g*255)<<8;
	color |= (int)(r*255);
	return color;
}

uint32_t rgba_clamp(float r, float g, float b, float a) {
	if (r > 1.0f) r = 1.0f;
	if (g > 1.0f) g = 1.0f;
	if (b > 1.0f) b = 1.0f;
	if (a > 1.0f) a = 1.0f;

	if (r < 0.0f) r = 0.0f;
	if (g < 0.0f) g = 0.0f;
	if (b < 0.0f) b = 0.0f;
	if (a < 0.0f) a = 0.0f;

	return rgba(r,g,b,a);
}

/* hsv2rgb.c
* Convert Hue Saturation Value to Red Green Blue
*
* P.J. 08-Aug-98
*
* Reference:
* D. F. Rogers
* Procedural Elements for Computer Graphics
* McGraw Hill 1985
*/
uint32_t hsva(float H, float S, float V, float alpha) {
	/*
	* Purpose:
	* Convert HSV values to RGB values
	* All values are in the range [0.0 .. 1.0]
	*/
	float F, M, N, K;
	int	 I;
	if ( S == 0.0 ) {
		// Achromatic case, set level of grey
		return rgba(V, V, V, alpha);
	} else {
		/*
		* Determine levels of primary colours.
		*/
		if (H >= 1.0) {
			H = 0.0;
		} else {
			H = H * 6;
		}
		I = (int) H;	 /* should be in the range 0..5 */
		F = H - I;		 /* fractional part */

		M = V * (1 - S);
		N = V * (1 - S * F);
		K = V * (1 - S * (1 - F));

		float r, g, b;
		if (I == 0) { r = V; g = K; b = M; }
		else if (I == 1) { r = N; g = V; b = M; }
		else if (I == 2) { r = M; g = V; b = K; }
		else if (I == 3) { r = M; g = N; b = V; }
		else if (I == 4) { r = K; g = M; b = V; }
		else if (I == 5) { r = V; g = M; b = N; }
		else return 0;
		return rgba(r, g, b, alpha);
	}
}
