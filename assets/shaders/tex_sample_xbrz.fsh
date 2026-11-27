// xBRZ freescale as a texture sampling shader (Type=Sampling, see defaultshaders.ini).
//
// Based on Hyllian's xBRZ freescale shader and the 4xBRZ shader by the DeSmuME team:
//
// 4xBRZ shader - Copyright (C) 2014-2016 DeSmuME team
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with the this software.  If not, see <http://www.gnu.org/licenses/>.
//
// Hyllian's xBR-vertex code and texel mapping - Copyright (C) 2011/2016 Hyllian - sergiogdb@gmail.com
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Colors are premultiplied (texFetch), with alpha in the distance, so transparent texels don't fringe sprites.
// Only the bottom-right corner of the center texel is written out; the other three are rotations of it.

float xbDist(vec4 a, vec4 b) {
	vec3 d = a.rgb - b.rgb;
	float Y = dot(d, vec3(0.2627, 0.6780, 0.0593));
	float Cb = 0.5 / (1.0 - 0.0593) * (d.b - Y);
	float Cr = 0.5 / (1.0 - 0.2627) * (d.r - Y);
	return sqrt(Y * Y + Cb * Cb + Cr * Cr) + abs(a.a - b.a);
}

bool xbEq(vec4 a, vec4 b) { return xbDist(a, b) < 0.001; }
bool xbPixEq(vec4 a, vec4 b) { return xbDist(a, b) < 30.0 / 255.0; }

// How far pos is on the blended side of the line through origin along dir, antialiased over a screen pixel.
float xbRatio(highp vec2 pos, vec2 origin, vec2 dir, highp vec2 scale) {
	highp vec2 p0 = pos - origin;
	highp vec2 distv = p0 - dir * (dot(p0, dir) / dot(dir, dir));
	float side = sign(dot(p0, vec2(-dir.y, dir.x)));
	return smoothstep(-0.70710678, 0.70710678, side * length(distv * scale));
}

// 0: no blend, 1: normal, 2: dominant.
int xbPre(vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I, vec4 H5, vec4 I5, vec4 F4, vec4 I4) {
	if ((xbEq(E, F) && xbEq(H, I)) || (xbEq(E, H) && xbEq(F, I))) return 0;
	float dHF = xbDist(G, E) + xbDist(E, C) + xbDist(H5, I) + xbDist(I, F4) + 4.0 * xbDist(H, F);
	float dEI = xbDist(D, H) + xbDist(H, I5) + xbDist(B, F) + xbDist(F, I4) + 4.0 * xbDist(E, I);
	if (!(dHF < dEI) || xbEq(E, F) || xbEq(E, H)) return 0;
	return (3.6 * dHF < dEI) ? 2 : 1;
}

vec4 xbBlend(vec4 res, int bZ, int bY, int bW, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I, highp vec2 pos, highp vec2 scale) {
	if (bZ == 0) return res;
	float dFG = xbDist(F, G);
	float dHC = xbDist(H, C);
	bool doLine = bZ == 2 || !((bY != 0 && !xbPixEq(E, G)) || (bW != 0 && !xbPixEq(E, C)) ||
		(xbPixEq(G, H) && xbPixEq(H, I) && xbPixEq(I, F) && xbPixEq(F, C) && !xbPixEq(E, I)));
	vec2 origin = vec2(0.0, 0.70710678);
	vec2 dir = vec2(1.0, -1.0);
	if (doLine) {
		bool shallow = 2.2 * dFG <= dHC && !xbEq(E, G) && !xbEq(D, G);
		bool steep = 2.2 * dHC <= dFG && !xbEq(E, C) && !xbEq(B, C);
		origin = vec2(0.0, shallow ? 0.25 : 0.5);
		dir.x += shallow ? 1.0 : 0.0;
		dir.y -= steep ? 1.0 : 0.0;
	}
	vec4 blendPix = mix(H, F, step(xbDist(E, F), xbDist(E, H)));
	return mix(res, blendPix, xbRatio(pos, origin, dir, scale));
}

vec4 sampleTexture(highp vec2 uv) {
	highp vec2 tc = uv * texSize;
	highp vec2 pos = fract(tc) - vec2(0.5, 0.5);
	highp vec2 c = floor(tc);
	vec4 A = texFetch(c + vec2(-1.0, -1.0)); vec4 B = texFetch(c + vec2(0.0, -1.0)); vec4 C = texFetch(c + vec2(1.0, -1.0));
	vec4 D = texFetch(c + vec2(-1.0, 0.0)); vec4 E = texFetch(c); vec4 F = texFetch(c + vec2(1.0, 0.0));
	vec4 G = texFetch(c + vec2(-1.0, 1.0)); vec4 H = texFetch(c + vec2(0.0, 1.0)); vec4 I = texFetch(c + vec2(1.0, 1.0));
	vec4 A1 = texFetch(c + vec2(-1.0, -2.0)); vec4 C1 = texFetch(c + vec2(1.0, -2.0));
	vec4 A0 = texFetch(c + vec2(-2.0, -1.0)); vec4 G0 = texFetch(c + vec2(-2.0, 1.0));
	vec4 C4 = texFetch(c + vec2(2.0, -1.0)); vec4 I4 = texFetch(c + vec2(2.0, 1.0));
	vec4 G5 = texFetch(c + vec2(-1.0, 2.0)); vec4 I5 = texFetch(c + vec2(1.0, 2.0));
	vec4 B1 = texFetch(c + vec2(0.0, -2.0)); vec4 D0 = texFetch(c + vec2(-2.0, 0.0));
	vec4 H5 = texFetch(c + vec2(0.0, 2.0)); vec4 F4 = texFetch(c + vec2(2.0, 0.0));
	int bZ = xbPre(B, C, D, E, F, G, H, I, H5, I5, F4, I4);
	int bW = xbPre(F, I, B, E, H, A, D, G, D0, G0, H5, G5);
	int bY = xbPre(D, A, H, E, B, I, F, C, F4, C4, B1, C1);
	int bX = xbPre(H, G, F, E, D, C, B, A, B1, A1, D0, A0);
	vec4 res = E;
	res = xbBlend(res, bZ, bY, bW, B, C, D, E, F, G, H, I, pos, texScale);
	res = xbBlend(res, bW, bZ, bX, F, I, B, E, H, A, D, G, vec2(pos.y, -pos.x), texScale.yx);
	res = xbBlend(res, bY, bX, bZ, D, A, H, E, B, I, F, C, vec2(-pos.y, pos.x), texScale.yx);
	res = xbBlend(res, bX, bW, bY, H, G, F, E, D, C, B, A, -pos, texScale);
	return res;
}
