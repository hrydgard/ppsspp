#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Cinematic post-process combo
// =================================================================
// Four independent effects, all bypassed when their slider is 0:
//
//   .x = Chromatic Aberration   [0, 0.05]   (radial, real-lens-like)
//   .y = Film Grain              [0, 0.5]    (animated 3D-hash noise)
//   .z = Vignette                [0, 1]      (cubic falloff)
//   .w = Dithering (Bayer 8x8)   [0, 1]
//
// Improvements over the previous version (per hrydgard's feedback):
//   - Film grain now uses a *3D hash* over (uv, time). This is the
//     well-known IQ-style noise that produces *blue-noise-like*
//     distribution. The result looks like film grain instead of
//     a sin/fract hash, which always had visible patterning.
//   - The grain is multi-octave: 2 octaves at different scales
//     (coarse + fine), blended, for that "real film stock" look.
//   - Slider value scaled by ~0.45 so 1.0 reads as "strong but not
//     broken TV". User can push to 0.5 for chunky 16mm look.
//   - Chromatic Aberration: still radial, but red is offset OUTWARD
//     and blue INWARD (green stays put). This is the actual
//     physical behaviour of a converging lens — previous version
//     had both R and B moved *outward*, which is wrong.

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

// PPSSPP exposes u_time as a vec4 (see PostShaderUniforms in
// GPU/Common/PresentationCommon.cpp:369). .x is the current time
// in seconds. Used here to animate film grain across frames.
uniform vec4 u_time;

varying vec2 v_texcoord0;

float hash13(vec3 p) {
    // IQ's hash (compact, blue-noise-ish distribution in 3D)
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

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

    // ---- 1. Chromatic Aberration (radial) ----
    if (u_setting.x > 0.0) {
        vec2 fromCenter = v_texcoord0 - 0.5;
        float r = length(fromCenter);
        vec2 dir = (r > 0.0001) ? fromCenter / r : vec2(0.0);
        // amount scales with r so the effect is invisible at the
        // center and strongest at the corners — same as a real lens.
        float amount = u_setting.x * r * 1.5;
        // R sampled OUTWARD (red is the lowest refractive index,
        // so it focuses the farthest), B sampled INWARD. G is the
        // reference and stays at the original UV.
        float r_chan = texture2D(sampler0, v_texcoord0 + dir * amount).r;
        float g_chan = texture2D(sampler0, v_texcoord0).g;
        float b_chan = texture2D(sampler0, v_texcoord0 - dir * amount).b;
        color = vec3(r_chan, g_chan, b_chan);
    }

    // ---- 2. Film Grain (animated 3D-hash noise) ----
    if (u_setting.y > 0.0) {
        // u_time.x = current time (seconds). We re-roll the noise
        // ~24 times per second so the grain animates smoothly.
        float t = u_time.x;
        // Two octaves of IQ hash noise. The time component
        // (fract(t*24.0)) re-rolls the noise per frame so it
        // animates without flickering.
        vec3 p1 = vec3(v_texcoord0 * 480.0, fract(t * 24.0));
        vec3 p2 = vec3(v_texcoord0 * 200.0, fract(t * 24.0) * 1.7);
        float n1 = hash13(p1) - 0.5;
        float n2 = hash13(p2) - 0.5;
        // Coarse + fine mix, weighted toward fine (the visible grain).
        float grain = n1 * 0.6 + n2 * 0.4;
        // Slightly luma-aware: grain is more visible in midtones,
        // less in pure black/white (real film behaves this way).
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        float lumaWeight = 1.0 - 4.0 * (lum - 0.5) * (lum - 0.5);
        lumaWeight = clamp(lumaWeight, 0.4, 1.0);
        // Slider 0..0.5 -> grain strength 0..0.225 (chunky at the
        // top end, like 16mm stock).
        color += grain * u_setting.y * 0.45 * lumaWeight;
    }

    // ---- 3. Vignette (cubic falloff) ----
    if (u_setting.z > 0.0) {
        vec2 center = v_texcoord0 - 0.5;
        float dist2 = dot(center, center);
        // u_setting.z in [0,1] -> power in [0, 4.0]
        float vignette = 1.0 - pow(dist2 * u_setting.z * 2.6, 1.2);
        color *= clamp(vignette, 0.0, 1.0);
    }

    // ---- 4. Dithering (8x8 Bayer) ----
    if (u_setting.w > 0.0) {
        vec2 p = floor(gl_FragCoord.xy);
        int x = int(mod(p.x, 8.0));
        int y = int(mod(p.y, 8.0));
        int n = y * 8 + x;
        float d = bayer8[n] / 64.0 - 0.5;
        color += d * u_setting.w * (1.0 / 255.0);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
