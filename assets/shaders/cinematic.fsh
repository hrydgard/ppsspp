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
//   .w = Debanding (IGN)         [0, 1]
//
// v8 changes:
//   - Debanding now uses Interleaved Gradient Noise (IGN, Castaño /
//     Call of Duty: Advanced Warfare) instead of an 8x8 Bayer LUT.
//     The previous code used `float[](...)` which is GLSL ES 3.00
//     syntax and fails to compile on the Vulkan/D3D11 backends via
//     SPIRV-Cross. IGN is a single line, ES 1.00 safe, and is the
//     recommended modern debanding technique (arguably better than
//     Bayer for banding removal).
//   - Film-grain luminance weight unified to Rec.709 (was BT.601).

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

// Interleaved Gradient Noise (Castaño, CoD:AW). ES 1.00 safe.
float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(floor(p), vec2(0.06711056, 0.00583715))));
}

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
        // Coarse + fine mix, balanced for natural film look.
        float grain = n1 * 0.5 + n2 * 0.5;
        // Slightly luma-aware: grain is more visible in midtones,
        // less in pure black/white (real film behaves this way).
        float lum = dot(color, vec3(0.2126, 0.7152, 0.0722)); // Rec.709
        float lumaWeight = 1.0 - 4.0 * (lum - 0.5) * (lum - 0.5);
        lumaWeight = clamp(lumaWeight, 0.4, 1.0);
        // Slider 0..0.5 -> grain strength 0..0.175 (subtle at default,
        // push higher for chunky 16mm look).
        color += grain * u_setting.y * 0.35 * lumaWeight;
    }

    // ---- 3. Vignette (cubic falloff) ----
    if (u_setting.z > 0.0) {
        vec2 center = v_texcoord0 - 0.5;
        float dist2 = dot(center, center);
        // u_setting.z in [0,1] -> power in [0, 4.0]
        float vignette = 1.0 - pow(dist2 * u_setting.z * 2.6, 1.2);
        color *= clamp(vignette, 0.0, 1.0);
    }

    // ---- 4. Debanding (IGN, Castaño / CoD:AW) ----
    if (u_setting.w > 0.0) {
        float d = ign(gl_FragCoord.xy) - 0.5;
        color += d * u_setting.w * (1.0 / 255.0);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
