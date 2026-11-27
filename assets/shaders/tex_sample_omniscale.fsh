/* OmniScale, as a texture sampling shader (Type=Sampling, see defaultshaders.ini).
   From SameBoy's Shaders/OmniScale.fsh and MasterShader.fsh.

   Copyright (c) 2015-2026 Lior Halphon

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   OmniScale is derived from the pattern based design of HQnx, but with the following general differences:
    - The actual output calculating was completely redesigned as resolution independent graphic generator. This allows
      scaling to any factor.
    - HQnx approximations that were good enough for a 2x/3x/4x factor were refined, creating smoother gradients.
    - "Quarters" can be interpolated in more ways than in the HQnx filters
    - If a pattern does not provide enough information to determine the suitable scaling interpolation, up to 16 pixels
      per quarter are sampled (in contrast to the usual 9) in order to determine the best interpolation.

   Changes for PPSSPP: texels come from texFetch (premultiplied, so alpha is part of is_different), with SameBoy's
   gamma 2.2 linearization done here, and output_resolution / input_resolution is texScale.
*/

#define OMNI_GAMMA 2.2

vec4 omniFetch(highp vec2 texel) {
	vec4 t = texFetch(texel);
	return vec4(pow(t.rgb, vec3(OMNI_GAMMA, OMNI_GAMMA, OMNI_GAMMA)), t.a);
}

/* We use the same colorspace as the HQ algorithms. */
vec3 omniHqColorspace(vec4 rgb) {
	return vec3( 0.250 * rgb.r + 0.250 * rgb.g + 0.250 * rgb.b,
	             0.250 * rgb.r - 0.000 * rgb.g - 0.250 * rgb.b,
	            -0.125 * rgb.r + 0.250 * rgb.g - 0.125 * rgb.b);
}

bool omniIsDifferent(vec4 a, vec4 b) {
	vec3 diff = abs(omniHqColorspace(a) - omniHqColorspace(b));
	return diff.x > 0.018 || diff.y > 0.002 || diff.z > 0.005 || abs(a.a - b.a) > 0.05;
}

#define P(m, r) ((pattern & (m)) == (r))

vec4 omniScale(highp vec2 uv) {
	highp vec2 tc = uv * texSize;
	highp vec2 t = floor(tc);
	float pixelSize1 = length(1.0 / texScale);

	// o = offset, the width of a pixel
	vec2 o = vec2(1.0, 1.0);

	/* We always calculate the top left quarter.  If we need a different quarter, we flip our co-ordinates */

	// p = the position within a pixel [0...1]
	vec2 p = fract(tc);

	if (p.x > 0.5) {
		o.x = -o.x;
		p.x = 1.0 - p.x;
	}
	if (p.y > 0.5) {
		o.y = -o.y;
		p.y = 1.0 - p.y;
	}

	vec4 w0 = omniFetch(t + vec2(-o.x, -o.y));
	vec4 w1 = omniFetch(t + vec2( 0.0, -o.y));
	vec4 w2 = omniFetch(t + vec2( o.x, -o.y));
	vec4 w3 = omniFetch(t + vec2(-o.x,  0.0));
	vec4 w4 = omniFetch(t);
	vec4 w5 = omniFetch(t + vec2( o.x,  0.0));
	vec4 w6 = omniFetch(t + vec2(-o.x,  o.y));
	vec4 w7 = omniFetch(t + vec2( 0.0,  o.y));
	vec4 w8 = omniFetch(t + vec2( o.x,  o.y));

	int pattern = 0;
	if (omniIsDifferent(w0, w4)) pattern |= 1 << 0;
	if (omniIsDifferent(w1, w4)) pattern |= 1 << 1;
	if (omniIsDifferent(w2, w4)) pattern |= 1 << 2;
	if (omniIsDifferent(w3, w4)) pattern |= 1 << 3;
	if (omniIsDifferent(w5, w4)) pattern |= 1 << 4;
	if (omniIsDifferent(w6, w4)) pattern |= 1 << 5;
	if (omniIsDifferent(w7, w4)) pattern |= 1 << 6;
	if (omniIsDifferent(w8, w4)) pattern |= 1 << 7;

	if ((P(0xBF,0x37) || P(0xDB,0x13)) && omniIsDifferent(w1, w5)) {
		return mix(w4, w3, 0.5 - p.x);
	}
	if ((P(0xDB,0x49) || P(0xEF,0x6D)) && omniIsDifferent(w7, w3)) {
		return mix(w4, w1, 0.5 - p.y);
	}
	if ((P(0x0B,0x0B) || P(0xFE,0x4A) || P(0xFE,0x1A)) && omniIsDifferent(w3, w1)) {
		return w4;
	}
	if ((P(0x6F,0x2A) || P(0x5B,0x0A) || P(0xBF,0x3A) || P(0xDF,0x5A) ||
	     P(0x9F,0x8A) || P(0xCF,0x8A) || P(0xEF,0x4E) || P(0x3F,0x0E) ||
	     P(0xFB,0x5A) || P(0xBB,0x8A) || P(0x7F,0x5A) || P(0xAF,0x8A) ||
	     P(0xEB,0x8A)) && omniIsDifferent(w3, w1)) {
		return mix(w4, mix(w4, w0, 0.5 - p.x), 0.5 - p.y);
	}
	if (P(0x0B,0x08)) {
		return mix(mix(w0 * 0.375 + w1 * 0.25 + w4 * 0.375, w4 * 0.5 + w1 * 0.5, p.x * 2.0), w4, p.y * 2.0);
	}
	if (P(0x0B,0x02)) {
		return mix(mix(w0 * 0.375 + w3 * 0.25 + w4 * 0.375, w4 * 0.5 + w3 * 0.5, p.y * 2.0), w4, p.x * 2.0);
	}
	if (P(0x2F,0x2F)) {
		float dist = length(p - vec2(0.5, 0.5));
		float pixel_size = pixelSize1;
		if (dist < 0.5 - pixel_size / 2.0) {
			return w4;
		}
		vec4 r;
		if (omniIsDifferent(w0, w1) || omniIsDifferent(w0, w3)) {
			r = mix(w1, w3, p.y - p.x + 0.5);
		} else {
			r = mix(mix(w1 * 0.375 + w0 * 0.25 + w3 * 0.375, w3, p.y * 2.0), w1, p.x * 2.0);
		}

		if (dist > 0.5 + pixel_size / 2.0) {
			return r;
		}
		return mix(w4, r, (dist - 0.5 + pixel_size / 2.0) / pixel_size);
	}
	if (P(0xBF,0x37) || P(0xDB,0x13)) {
		float dist = p.x - 2.0 * p.y;
		float pixel_size = pixelSize1 * sqrt(5.0);
		if (dist > pixel_size / 2.0) {
			return w1;
		}
		vec4 r = mix(w3, w4, p.x + 0.5);
		if (dist < -pixel_size / 2.0) {
			return r;
		}
		return mix(r, w1, (dist + pixel_size / 2.0) / pixel_size);
	}
	if (P(0xDB,0x49) || P(0xEF,0x6D)) {
		float dist = p.y - 2.0 * p.x;
		float pixel_size = pixelSize1 * sqrt(5.0);
		if (p.y - 2.0 * p.x > pixel_size / 2.0) {
			return w3;
		}
		vec4 r = mix(w1, w4, p.x + 0.5);
		if (dist < -pixel_size / 2.0) {
			return r;
		}
		return mix(r, w3, (dist + pixel_size / 2.0) / pixel_size);
	}
	if (P(0xBF,0x8F) || P(0x7E,0x0E)) {
		float dist = p.x + 2.0 * p.y;
		float pixel_size = pixelSize1 * sqrt(5.0);

		if (dist > 1.0 + pixel_size / 2.0) {
			return w4;
		}

		vec4 r;
		if (omniIsDifferent(w0, w1) || omniIsDifferent(w0, w3)) {
			r = mix(w1, w3, p.y - p.x + 0.5);
		} else {
			r = mix(mix(w1 * 0.375 + w0 * 0.25 + w3 * 0.375, w3, p.y * 2.0), w1, p.x * 2.0);
		}

		if (dist < 1.0 - pixel_size / 2.0) {
			return r;
		}

		return mix(r, w4, (dist + pixel_size / 2.0 - 1.0) / pixel_size);
	}

	if (P(0x7E,0x2A) || P(0xEF,0xAB)) {
		float dist = p.y + 2.0 * p.x;
		float pixel_size = pixelSize1 * sqrt(5.0);

		if (p.y + 2.0 * p.x > 1.0 + pixel_size / 2.0) {
			return w4;
		}

		vec4 r;

		if (omniIsDifferent(w0, w1) || omniIsDifferent(w0, w3)) {
			r = mix(w1, w3, p.y - p.x + 0.5);
		} else {
			r = mix(mix(w1 * 0.375 + w0 * 0.25 + w3 * 0.375, w3, p.y * 2.0), w1, p.x * 2.0);
		}

		if (dist < 1.0 - pixel_size / 2.0) {
			return r;
		}

		return mix(r, w4, (dist + pixel_size / 2.0 - 1.0) / pixel_size);
	}

	if (P(0x1B,0x03) || P(0x4F,0x43) || P(0x8B,0x83) || P(0x6B,0x43)) {
		return mix(w4, w3, 0.5 - p.x);
	}

	if (P(0x4B,0x09) || P(0x8B,0x89) || P(0x1F,0x19) || P(0x3B,0x19)) {
		return mix(w4, w1, 0.5 - p.y);
	}

	if (P(0xFB,0x6A) || P(0x6F,0x6E) || P(0x3F,0x3E) || P(0xFB,0xFA) ||
	    P(0xDF,0xDE) || P(0xDF,0x1E)) {
		return mix(w4, w0, (1.0 - p.x - p.y) / 2.0);
	}

	if (P(0x4F,0x4B) || P(0x9F,0x1B) || P(0x2F,0x0B) ||
	    P(0xBE,0x0A) || P(0xEE,0x0A) || P(0x7E,0x0A) || P(0xEB,0x4B) ||
	    P(0x3B,0x1B)) {
		float dist = p.x + p.y;
		float pixel_size = pixelSize1;

		if (dist > 0.5 + pixel_size / 2.0) {
			return w4;
		}

		vec4 r;
		if (omniIsDifferent(w0, w1) || omniIsDifferent(w0, w3)) {
			r = mix(w1, w3, p.y - p.x + 0.5);
		} else {
			r = mix(mix(w1 * 0.375 + w0 * 0.25 + w3 * 0.375, w3, p.y * 2.0), w1, p.x * 2.0);
		}

		if (dist < 0.5 - pixel_size / 2.0) {
			return r;
		}

		return mix(r, w4, (dist + pixel_size / 2.0 - 0.5) / pixel_size);
	}

	if (P(0x0B,0x01)) {
		return mix(mix(w4, w3, 0.5 - p.x), mix(w1, (w1 + w3) / 2.0, 0.5 - p.x), 0.5 - p.y);
	}

	if (P(0x0B,0x00)) {
		return mix(mix(w4, w3, 0.5 - p.x), mix(w1, w0, 0.5 - p.x), 0.5 - p.y);
	}

	float dist = p.x + p.y;
	float pixel_size = pixelSize1;

	if (dist > 0.5 + pixel_size / 2.0) {
		return w4;
	}

	/* We need more samples to "solve" this diagonal */
	vec4 x0 = omniFetch(t + vec2(-o.x * 2.0, -o.y * 2.0));
	vec4 x1 = omniFetch(t + vec2(-o.x,       -o.y * 2.0));
	vec4 x2 = omniFetch(t + vec2( 0.0,       -o.y * 2.0));
	vec4 x3 = omniFetch(t + vec2( o.x,       -o.y * 2.0));
	vec4 x4 = omniFetch(t + vec2(-o.x * 2.0, -o.y));
	vec4 x5 = omniFetch(t + vec2(-o.x * 2.0,  0.0));
	vec4 x6 = omniFetch(t + vec2(-o.x * 2.0,  o.y));

	if (omniIsDifferent(x0, w4)) pattern |= 1 << 8;
	if (omniIsDifferent(x1, w4)) pattern |= 1 << 9;
	if (omniIsDifferent(x2, w4)) pattern |= 1 << 10;
	if (omniIsDifferent(x3, w4)) pattern |= 1 << 11;
	if (omniIsDifferent(x4, w4)) pattern |= 1 << 12;
	if (omniIsDifferent(x5, w4)) pattern |= 1 << 13;
	if (omniIsDifferent(x6, w4)) pattern |= 1 << 14;

	int diagonal_bias = -7;
	while (pattern != 0) {
		diagonal_bias += pattern & 1;
		pattern >>= 1;
	}

	if (diagonal_bias <= 0) {
		vec4 r = mix(w1, w3, p.y - p.x + 0.5);
		if (dist < 0.5 - pixel_size / 2.0) {
			return r;
		}
		return mix(r, w4, (dist + pixel_size / 2.0 - 0.5) / pixel_size);
	}

	return w4;
}

vec4 sampleTexture(highp vec2 uv) {
	vec4 res = omniScale(uv);
	float invGamma = 1.0 / OMNI_GAMMA;
	return vec4(pow(max(res.rgb, vec3(0.0, 0.0, 0.0)), vec3(invGamma, invGamma, invGamma)), res.a);
}
