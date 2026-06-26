#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — Lightweight luminance-driven fake ambient occlusion
// =================================================================
// v2 — uses the same variance-based approach as pseudo_rt but
// with a smaller, cheaper sample footprint (16 taps, 2 rings).
// Designed for low-end mobile where pseudo_rt's 36 taps is too
// expensive.
//
// u_setting:
//   .x = AO strength  [0, 1]   (0 = off)
//   .y = AO radius    [1, 4]   texel distance of the outer ring
//   .z = AO color (R) [0, 1]   shadow tint R
//   .w = AO color (B) [0, 1]   shadow tint B (G is fixed at 0.5)
//
// The previous version of this file used a binary "spot" detection
// that produced sharp black dots/lines. This version uses mean /
// std-dev of the surrounding luminance and a smooth step, giving
// a soft falloff that scales with the radius slider.

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float strength = u_setting.x;
    if (strength <= 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    float R = max(u_setting.y, 1.0);
    // 8-tap ring + 8-tap outer ring, total 16 taps.
    float sum = 0.0;
    float sumSq = 0.0;
    float totalWeight = 0.0;

    for (int ring = 1; ring <= 2; ring++) {
        float rDist = R * float(ring);
        float w = 1.0 / float(ring);  // closer ring weighs more
        for (int i = 0; i < 8; i++) {
            float angle = float(i) * 0.7853981633 + float(ring) * 0.21;
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 offset = dir * u_texelDelta * rDist;
            float l = luminance(texture2D(sampler0, v_texcoord0 + offset).rgb);
            sum += l * w;
            sumSq += l * l * w;
            totalWeight += w;
        }
    }

    float mean = sum / max(totalWeight, 0.0001);
    float variance = max((sumSq / max(totalWeight, 0.0001)) - mean * mean, 0.0);

    // AO condition: I'm dim relative to local mean, AND the local
    // scene has some variance (i.e. there's actually a contrast
    // here, not just a uniform field).
    float dim = clamp((mean - centerLum) / max(mean, 0.15), 0.0, 1.0);
    float varNorm = clamp(variance * 5.0, 0.0, 1.0);
    float aoMask = dim * (0.4 + 0.6 * varNorm);

    // AO color: build from user-controlled R / B (G = mid).
    // .z = R tint (0..1), .w = B tint (0..1), G = 0.5.
    // Default (.z=0.5, .w=0.5) is neutral gray.
    vec3 aoColor = vec3(u_setting.z, 0.5, u_setting.w);
    // Mix toward AO color (instead of multiplying, which can crush
    // detail in already-dark areas).
    vec3 result = mix(color, aoColor, aoMask * strength * 0.85);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
