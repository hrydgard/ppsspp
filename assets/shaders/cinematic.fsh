#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Cinematic post-process combo
// Four independent effects, all bypassed when their slider is 0:
//
//   .x = Chromatic Aberration  [0, 0.05]
//   .y = Film Grain             [0, 0.5]
//   .z = Vignette               [0, 1]
//   .w = Dithering (Bayer 8x8)  [0, 1]
//
// The grain is animated by gl_FragCoord * u_time noise so it actually
// "breathes" (hrydgard's feedback on PR #21854).
// Chromatic aberration is RADIAL (distance from center), not
// horizontal — that is the natural behaviour of real lenses.

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

// 8x8 Bayer matrix for ordered dithering (gives a visible
// dither pattern that softens color banding on LDR output)
const float bayer8[64] = float[](
     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0
);

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // 1. Chromatic Aberration — radial offset from center
    if (u_setting.x > 0.0) {
        vec2 fromCenter = v_texcoord0 - 0.5;
        float r = length(fromCenter);
        // Direction OUT from center, scaled by radial distance
        // (r=0 -> no offset, r=0.7+ -> strongest CA at edges)
        vec2 dir = (r > 0.0001) ? fromCenter / r : vec2(0.0);
        float amount = u_setting.x * r * 1.5;
        float r_chan = texture2D(sampler0, v_texcoord0 + dir * amount).r;
        float g_chan = texture2D(sampler0, v_texcoord0).g;
        float b_chan = texture2D(sampler0, v_texcoord0 - dir * amount).b;
        color = vec3(r_chan, g_chan, b_chan);
    }

    // 2. Film Grain — animated pseudo-random noise.
    //    u_time is PPSSPP's global time uniform, present in the
    //    post-shader uniform block (PostShaderUniforms). Falling
    //    back to a static seed if it's zero (offline / paused).
    if (u_setting.y > 0.0) {
        float t = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        float n = fract(sin(t * 100.0 + 1.5) * 43758.5453) - 0.5;
        // Subtle grain — heavy grain looks like a broken TV, not film.
        color += n * u_setting.y * 0.12;
    }

    // 3. Vignette — quadratic falloff toward corners
    if (u_setting.z > 0.0) {
        vec2 center = v_texcoord0 - 0.5;
        float dist2 = dot(center, center);
        // u_setting.z in [0,1] -> power in [1.0, 4.0] gives a soft
        // corner darkening without crushing the whole frame.
        float vignette = 1.0 - dist2 * u_setting.z * 2.5;
        color *= clamp(vignette, 0.0, 1.0);
    }

    // 4. Dithering (8x8 Bayer) — reduces LDR color banding
    if (u_setting.w > 0.0) {
        vec2 p = floor(gl_FragCoord.xy);
        int x = int(mod(p.x, 8.0));
        int y = int(mod(p.y, 8.0));
        int n = y * 8 + x;
        float d = bayer8[n] / 64.0 - 0.5;
        // Apply dither in [-0.5, +0.5] * 1/255 range,
        // scaled by the strength slider.
        color += d * u_setting.w * (1.0 / 255.0);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
