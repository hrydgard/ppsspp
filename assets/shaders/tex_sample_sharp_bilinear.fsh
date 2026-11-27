// Sharp bilinear, as a texture sampling shader (Type=Sampling, see defaultshaders.ini).
//
// The idea of sharp-bilinear by Themaister (public domain): texels stay square and sharp, and only the boundary
// between two of them is blended, over about one screen pixel. It doesn't detect edges, so it can't invent shapes,
// and it's the cheapest of the sampling shaders (four texel reads).

vec4 sampleTexture(highp vec2 uv) {
	// Texel space with the texel centers on integers, so base and base + 1 are the four texels around uv.
	highp vec2 tc = uv * texSize - vec2(0.5, 0.5);
	highp vec2 base = floor(tc);
	highp vec2 s = tc - base;
	// Flat inside each texel, a ramp one screen pixel wide across the boundary at s = 0.5.
	highp vec2 f = clamp((s - vec2(0.5, 0.5)) * max(texScale, vec2(1.0, 1.0)) + vec2(0.5, 0.5), 0.0, 1.0);
	vec4 top = mix(texFetch(base), texFetch(base + vec2(1.0, 0.0)), f.x);
	vec4 bottom = mix(texFetch(base + vec2(0.0, 1.0)), texFetch(base + vec2(1.0, 1.0)), f.x);
	return mix(top, bottom, f.y);
}
