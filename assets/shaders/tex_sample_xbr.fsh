/*
   Hyllian's xBR-lv2 Shader, as a texture sampling shader (Type=Sampling, see defaultshaders.ini).

   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.

   Ported from libretro's xbr-lv2.glsl (CORNER_C, smooth tips). The fixed XBR_SCALE is replaced by the screen
   pixels per texel at each pixel, so it works at any scale. Colors are premultiplied, and alpha counts in the
   luma used for edge detection, so transparent texels don't fringe sprites.
*/

#define XBR_EQ_THRESHOLD 15.0
#define XBR_LV2_COEFFICIENT 2.0

STATIC const vec4 xbrAo = vec4( 1.0, -1.0, -1.0,  1.0);
STATIC const vec4 xbrBo = vec4( 1.0,  1.0, -1.0, -1.0);
STATIC const vec4 xbrCo = vec4( 1.5,  0.5, -0.5,  0.5);
STATIC const vec4 xbrAx = vec4( 1.0, -1.0, -1.0,  1.0);
STATIC const vec4 xbrBx = vec4( 0.5,  2.0, -0.5, -2.0);
STATIC const vec4 xbrCx = vec4( 1.0,  1.0, -0.5,  0.0);
STATIC const vec4 xbrAy = vec4( 1.0, -1.0, -1.0,  1.0);
STATIC const vec4 xbrBy = vec4( 2.0,  0.5, -2.0, -0.5);
STATIC const vec4 xbrCy = vec4( 2.0,  0.0, -1.0,  0.5);
STATIC const vec4 xbrCi = vec4(0.25, 0.25, 0.25, 0.25);

// Luma in the original's units, plus alpha (premultiplied colors make transparent texels black).
float xbrY(vec4 c) {
	return dot(c.rgb, vec3(14.352, 28.176, 5.472)) + c.a * 48.0;
}

vec4 xbrDf(vec4 A, vec4 B) {
	return abs(A - B);
}

// Components that differ at all.
vec4 xbrDiff(vec4 A, vec4 B) {
	return step(vec4(0.001, 0.001, 0.001, 0.001), abs(A - B));
}

vec4 xbrEq(vec4 A, vec4 B) {
	return step(xbrDf(A, B), vec4(XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD, XBR_EQ_THRESHOLD));
}

vec4 xbrNeq(vec4 A, vec4 B) {
	return vec4(1.0, 1.0, 1.0, 1.0) - xbrEq(A, B);
}

vec4 xbrWd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return xbrDf(a, b) + xbrDf(a, c) + xbrDf(d, e) + xbrDf(d, f) + 4.0 * xbrDf(g, h);
}

float xbrColorDf(vec4 c1, vec4 c2) {
	vec4 d = abs(c1 - c2);
	return d.r + d.g + d.b + d.a;
}

vec4 sampleTexture(highp vec2 uv) {
	highp vec2 tc = uv * texSize;
	highp vec2 fp = fract(tc);
	highp vec2 t = floor(tc);

	vec4 A1 = texFetch(t + vec2(-1.0, -2.0));
	vec4 B1 = texFetch(t + vec2( 0.0, -2.0));
	vec4 C1 = texFetch(t + vec2( 1.0, -2.0));
	vec4 A  = texFetch(t + vec2(-1.0, -1.0));
	vec4 B  = texFetch(t + vec2( 0.0, -1.0));
	vec4 C  = texFetch(t + vec2( 1.0, -1.0));
	vec4 D  = texFetch(t + vec2(-1.0,  0.0));
	vec4 E  = texFetch(t);
	vec4 F  = texFetch(t + vec2( 1.0,  0.0));
	vec4 G  = texFetch(t + vec2(-1.0,  1.0));
	vec4 H  = texFetch(t + vec2( 0.0,  1.0));
	vec4 I  = texFetch(t + vec2( 1.0,  1.0));
	vec4 G5 = texFetch(t + vec2(-1.0,  2.0));
	vec4 H5 = texFetch(t + vec2( 0.0,  2.0));
	vec4 I5 = texFetch(t + vec2( 1.0,  2.0));
	vec4 A0 = texFetch(t + vec2(-2.0, -1.0));
	vec4 D0 = texFetch(t + vec2(-2.0,  0.0));
	vec4 G0 = texFetch(t + vec2(-2.0,  1.0));
	vec4 C4 = texFetch(t + vec2( 2.0, -1.0));
	vec4 F4 = texFetch(t + vec2( 2.0,  0.0));
	vec4 I4 = texFetch(t + vec2( 2.0,  1.0));

	// The four corners at once: x = bottom-right, y = top-right, z = top-left, w = bottom-left.
	vec4 b = vec4(xbrY(B), xbrY(D), xbrY(H), xbrY(F));
	vec4 c = vec4(xbrY(C), xbrY(A), xbrY(G), xbrY(I));
	vec4 d = b.yzwx;
	float eY = xbrY(E);
	vec4 e = vec4(eY, eY, eY, eY);
	vec4 f = b.wxyz;
	vec4 g = c.zwxy;
	vec4 h = b.zwxy;
	vec4 i = c.wxyz;
	vec4 i4 = vec4(xbrY(I4), xbrY(C1), xbrY(A0), xbrY(G5));
	vec4 i5 = vec4(xbrY(I5), xbrY(C4), xbrY(A1), xbrY(G0));
	vec4 h5 = vec4(xbrY(H5), xbrY(F4), xbrY(B1), xbrY(D0));
	vec4 f4 = h5.yzwx;

	// Antialiasing width: the original's 1/XBR_SCALE, from the actual scale here.
	float invScale = 2.0 / (texScale.x + texScale.y);
	vec4 delta = vec4(invScale, invScale, invScale, invScale);
	vec4 deltaL = vec4(0.5, 1.0, 0.5, 1.0) * invScale;
	vec4 deltaU = deltaL.yxwz;

	// These inequations define the line below which interpolation occurs.
	vec4 fx  = xbrAo * fp.y + xbrBo * fp.x;
	vec4 fxL = xbrAx * fp.y + xbrBx * fp.x;
	vec4 fxU = xbrAy * fp.y + xbrBy * fp.x;

	vec4 irlv0 = xbrDiff(e, f) * xbrDiff(e, h);
	vec4 irlv1 = irlv0 * (xbrNeq(f, b) * xbrNeq(f, c) + xbrNeq(h, d) * xbrNeq(h, g) +
		xbrEq(e, i) * (xbrNeq(f, f4) * xbrNeq(f, i4) + xbrNeq(h, h5) * xbrNeq(h, i5)) + xbrEq(e, g) + xbrEq(e, c));
	vec4 irlv2l = xbrDiff(e, g) * xbrDiff(d, g);
	vec4 irlv2u = xbrDiff(e, c) * xbrDiff(b, c);

	vec4 fx45i = clamp((fx  + delta  - xbrCo - xbrCi) / (2.0 * delta), 0.0, 1.0);
	vec4 fx45  = clamp((fx  + delta  - xbrCo)         / (2.0 * delta), 0.0, 1.0);
	vec4 fx30  = clamp((fxL + deltaL - xbrCx)         / (2.0 * deltaL), 0.0, 1.0);
	vec4 fx60  = clamp((fxU + deltaU - xbrCy)         / (2.0 * deltaU), 0.0, 1.0);

	vec4 wd1 = xbrWd(e, c, g, i, h5, f4, h, f);
	vec4 wd2 = xbrWd(h, d, i5, f, i4, b, e, i);

	vec4 edri = step(wd1, wd2) * irlv0;
	vec4 edr  = step(wd1 + vec4(0.1, 0.1, 0.1, 0.1), wd2) * step(vec4(0.5, 0.5, 0.5, 0.5), irlv1);
	vec4 edrL = step(XBR_LV2_COEFFICIENT * xbrDf(f, g), xbrDf(h, c)) * irlv2l * edr;
	vec4 edrU = step(XBR_LV2_COEFFICIENT * xbrDf(h, c), xbrDf(f, g)) * irlv2u * edr;

	fx45  = edr  * fx45;
	fx30  = edrL * fx30;
	fx60  = edrU * fx60;
	fx45i = edri * fx45i;

	vec4 px = step(xbrDf(e, f), xbrDf(e, h));
	vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));

	vec4 res1 = E;
	res1 = mix(res1, mix(H, F, px.x), maximos.x);
	res1 = mix(res1, mix(B, D, px.z), maximos.z);

	vec4 res2 = E;
	res2 = mix(res2, mix(F, B, px.y), maximos.y);
	res2 = mix(res2, mix(D, H, px.w), maximos.w);

	return mix(res1, res2, step(xbrColorDf(E, res1), xbrColorDf(E, res2)));
}
