// Multi-frame motion blur / LCD persistence example.
// Demonstrates PreviousFrames=N in the ini. Previous frames are bound as sampler2 (1 frame ago),
// sampler3 (2 frames ago), ... and PPSSPP_PREVIOUS_FRAMES tells how many we actually got on this
// device (it can be less than requested, e.g. 4 on GLES 2.0).

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
#if PPSSPP_PREVIOUS_FRAMES >= 1
uniform sampler2D sampler2;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 2
uniform sampler2D sampler3;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 3
uniform sampler2D sampler4;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 4
uniform sampler2D sampler5;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 5
uniform sampler2D sampler6;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 6
uniform sampler2D sampler7;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 7
uniform sampler2D sampler8;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 8
uniform sampler2D sampler9;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 9
uniform sampler2D sampler10;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 10
uniform sampler2D sampler11;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 11
uniform sampler2D sampler12;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 12
uniform sampler2D sampler13;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 13
uniform sampler2D sampler14;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 14
uniform sampler2D sampler15;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 15
uniform sampler2D sampler16;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 16
uniform sampler2D sampler17;
#endif

uniform vec4 u_setting;
varying vec2 v_texcoord0;

void main() {
	vec2 uv = v_texcoord0.xy;
	float decay = u_setting.x;      // How much each older frame contributes relative to the next newer one.
	float maxFrames = u_setting.y;  // How many previous frames to use (we may have fewer.)

	vec3 accum = texture2D(sampler0, uv).rgb;
	float total = 1.0;
	float w = 1.0;

#if PPSSPP_PREVIOUS_FRAMES >= 1
	w *= decay;
	w *= step(1.0, maxFrames + 0.5);
	accum += texture2D(sampler2, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 2
	w *= decay;
	w *= step(2.0, maxFrames + 0.5);
	accum += texture2D(sampler3, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 3
	w *= decay;
	w *= step(3.0, maxFrames + 0.5);
	accum += texture2D(sampler4, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 4
	w *= decay;
	w *= step(4.0, maxFrames + 0.5);
	accum += texture2D(sampler5, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 5
	w *= decay;
	w *= step(5.0, maxFrames + 0.5);
	accum += texture2D(sampler6, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 6
	w *= decay;
	w *= step(6.0, maxFrames + 0.5);
	accum += texture2D(sampler7, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 7
	w *= decay;
	w *= step(7.0, maxFrames + 0.5);
	accum += texture2D(sampler8, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 8
	w *= decay;
	w *= step(8.0, maxFrames + 0.5);
	accum += texture2D(sampler9, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 9
	w *= decay;
	w *= step(9.0, maxFrames + 0.5);
	accum += texture2D(sampler10, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 10
	w *= decay;
	w *= step(10.0, maxFrames + 0.5);
	accum += texture2D(sampler11, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 11
	w *= decay;
	w *= step(11.0, maxFrames + 0.5);
	accum += texture2D(sampler12, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 12
	w *= decay;
	w *= step(12.0, maxFrames + 0.5);
	accum += texture2D(sampler13, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 13
	w *= decay;
	w *= step(13.0, maxFrames + 0.5);
	accum += texture2D(sampler14, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 14
	w *= decay;
	w *= step(14.0, maxFrames + 0.5);
	accum += texture2D(sampler15, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 15
	w *= decay;
	w *= step(15.0, maxFrames + 0.5);
	accum += texture2D(sampler16, uv).rgb * w;
	total += w;
#endif
#if PPSSPP_PREVIOUS_FRAMES >= 16
	w *= decay;
	w *= step(16.0, maxFrames + 0.5);
	accum += texture2D(sampler17, uv).rgb * w;
	total += w;
#endif

	gl_FragColor.rgb = accum / total;
	gl_FragColor.a = 1.0;
}
