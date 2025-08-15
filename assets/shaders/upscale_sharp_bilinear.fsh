#ifdef GL_ES
#extension GL_OES_standard_derivatives : enable
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_position;

uniform vec2 u_texelDelta; // (1.0 / bufferWidth, 1.0 / bufferHeight)
uniform vec2 u_pixelDelta; // (1.0 / targetWidth, 1.0 / targetHeight)

// Returns pixel sharpened to nearest pixel boundary.
vec2 sharpSample(vec4 texSize, vec2 coord) {
	vec2 boxSize = clamp(fwidth(coord) * texSize.zw, 1e-5, 1.0);
	coord = coord * texSize.zw - 0.5 * boxSize;
	vec2 txOffset = smoothstep(vec2(1.0) - boxSize, vec2(1.0), fract(coord));
	return (floor(coord) + 0.5 + txOffset) * texSize.xy;
}

void main() {
	vec2 sharpPos = sharpSample(vec4(u_texelDelta, 1.0 / u_texelDelta), v_position);
	vec4 color = texture2D(sampler0, sharpPos);
	gl_FragColor = color;
}
